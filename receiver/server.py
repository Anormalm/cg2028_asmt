"""LAN demonstration receiver. Python standard library only.

Set ELDERCARE_TOKEN to a random 16-64-character printable token before starting.
HTTP is unencrypted: bind to a trusted private demo network, not the public Internet.
"""
import argparse
import csv
import io
from urllib.parse import urlparse, parse_qs
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import sqlite3
import threading
import time

TYPES = {'fall', 'sos', 'local_ack', 'heartbeat', 'rejected'}
STATES = {'STARTUP', 'NORMAL', 'WAIT_IMPACT', 'CONFIRM', 'FALL'}


def validate_event(event):
    if not isinstance(event, dict):
        raise ValueError('Expected an object')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,32}', str(event.get('device', ''))):
        raise ValueError('Invalid device ID')
    if not re.fullmatch(r'[0-9a-f]{16}', str(event.get('boot', ''))):
        raise ValueError('Invalid boot ID')
    if event.get('type') not in TYPES or event.get('state') not in STATES:
        raise ValueError('Invalid event type or state')
    if not isinstance(event.get('reason'), str) or not re.fullmatch(r'[a-z_]{1,23}', event['reason']):
        raise ValueError('Invalid reason')
    for key in ['seq', 'incident', 'uptime_ms', 'dropped']:
        if type(event.get(key)) is not int or not 0 <= event[key] <= 0xFFFFFFFF:
            raise ValueError('Invalid ' + key)
    if not event['seq'] or (event['type'] != 'heartbeat' and not event['incident']):
        raise ValueError('Missing sequence or incident')
    for key in ['accel_mg', 'gyro_dps', 'min_mg', 'peak_mg', 'peak_dps']:
        if type(event.get(key)) is not int or not -10000000 <= event[key] <= 10000000:
            raise ValueError('Invalid ' + key)
    if type(event.get('rejection_flags', 0)) is not int or not 0 <= event.get('rejection_flags', 0) <= 63:
        raise ValueError('Invalid rejection flags')
    return event


class Store:
    def __init__(self, path):
        self.lock = threading.Lock()
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS events (
                device TEXT, boot TEXT, seq INTEGER, received REAL, payload TEXT,
                PRIMARY KEY(device, boot, seq));
            CREATE TABLE IF NOT EXISTS devices (
                device TEXT PRIMARY KEY, boot TEXT, seq INTEGER, last_seen REAL, payload TEXT);
            CREATE TABLE IF NOT EXISTS sessions (
                device TEXT, boot TEXT, first_seen REAL, PRIMARY KEY(device, boot));
            CREATE TABLE IF NOT EXISTS acknowledgements (
                device TEXT, boot TEXT, incident INTEGER, acknowledged REAL,
                PRIMARY KEY(device, boot, incident));
            CREATE TABLE IF NOT EXISTS captures (
                device TEXT, boot TEXT, capture_id INTEGER, received REAL, meta TEXT,
                label TEXT DEFAULT 'unlabelled', note TEXT DEFAULT '',
                PRIMARY KEY(device, boot, capture_id));
            CREATE TABLE IF NOT EXISTS capture_parts (
                device TEXT, boot TEXT, capture_id INTEGER, part INTEGER, samples TEXT,
                PRIMARY KEY(device, boot, capture_id, part));
        ''')

    def accept(self, event):
        validate_event(event)
        now = time.time()
        device, boot, seq = event['device'], event['boot'], event['seq']
        payload = json.dumps(event, sort_keys=True)
        with self.lock, self.db:
            old = self.db.execute('SELECT payload FROM events WHERE device=? AND boot=? AND seq=?',
                                  (device, boot, seq)).fetchone()
            if old and old[0] != payload:
                raise ValueError('Event ID reused with different contents')
            known_session = self.db.execute('SELECT 1 FROM sessions WHERE device=? AND boot=?',
                                            (device, boot)).fetchone()
            self.db.execute('INSERT OR IGNORE INTO sessions VALUES (?,?,?)', (device, boot, now))
            self.db.execute('INSERT OR IGNORE INTO events VALUES (?,?,?,?,?)',
                            (device, boot, seq, now, payload))
            current = self.db.execute('SELECT boot,seq FROM devices WHERE device=?', (device,)).fetchone()
            if not current or (current[0] == boot and seq > current[1]) or not known_session:
                self.db.execute('INSERT OR REPLACE INTO devices VALUES (?,?,?,?,?)',
                                (device, boot, seq, now, payload))
            elif current[0] == boot:
                self.db.execute('UPDATE devices SET last_seen=? WHERE device=?', (now, device))
        # Transaction committed before the caller returns the delivery ACK.
        return f'ACK {boot}-{seq}\n'

    def acknowledge(self, device, boot, incident):
        if not isinstance(device, str) or not isinstance(boot, str) or type(incident) is not int:
            raise ValueError('Invalid acknowledgement')
        with self.lock, self.db:
            rows = self.db.execute('SELECT payload FROM events WHERE device=? AND boot=?', (device, boot))
            if not any((event := json.loads(row[0]))['incident'] == incident and
                       event['type'] in {'fall', 'sos'} for row in rows):
                raise ValueError('Unknown alarm incident')
            self.db.execute('INSERT OR IGNORE INTO acknowledgements VALUES (?,?,?,?)',
                            (device, boot, incident, time.time()))

    def accept_capture(self, data):
        if not isinstance(data, dict): raise ValueError('Expected an object')
        for key, pattern in [('device', r'[A-Za-z0-9_-]{1,32}'), ('boot', r'[0-9a-f]{16}'),
                             ('outcome', r'[a-z_]{1,23}')]:
            if not isinstance(data.get(key), str) or not re.fullmatch(pattern, data[key]):
                raise ValueError('Invalid ' + key)
        for key, low, high in [('capture_id', 1, 0xFFFFFFFF), ('part', 0, 99),
                               ('total', 1, 300), ('pre_count', 0, 100), ('trigger_ms', 0, 0xFFFFFFFF)]:
            if type(data.get(key)) is not int or not low <= data[key] <= high:
                raise ValueError('Invalid ' + key)
        if data.get('source') not in {'candidate', 'raw_motion', 'manual'}:
            raise ValueError('Invalid capture source')
        offset = data['part'] * 3
        rows = data.get('samples')
        if offset >= data['total'] or data['pre_count'] >= data['total'] or not isinstance(rows, list) or len(rows) != min(3, data['total']-offset):
            raise ValueError('Invalid capture part length')
        for row in rows:
            if not isinstance(row, list) or len(row) != 15 or any(type(v) is not int for v in row):
                raise ValueError('Invalid sample')
            if not 0 <= row[0] <= 0xFFFFFFFF or not 0 <= row[13] <= 4 or not 0 <= row[14] <= 127 or any(not -2147483648 <= v <= 2147483647 for v in row[1:13]):
                raise ValueError('Sample outside range')
        meta = {k: data[k] for k in ['device','boot','capture_id','total','pre_count','trigger_ms','source','outcome']}
        key = (data['device'], data['boot'], data['capture_id'])
        encoded, samples = json.dumps(meta, sort_keys=True), json.dumps(rows)
        with self.lock, self.db:
            old = self.db.execute('SELECT meta FROM captures WHERE device=? AND boot=? AND capture_id=?', key).fetchone()
            if old and old[0] != encoded: raise ValueError('Capture metadata changed')
            old_part = self.db.execute('SELECT samples FROM capture_parts WHERE device=? AND boot=? AND capture_id=? AND part=?', (*key, data['part'])).fetchone()
            if old_part and old_part[0] != samples: raise ValueError('Conflicting capture part')
            self.db.execute('INSERT OR IGNORE INTO captures(device,boot,capture_id,received,meta) VALUES(?,?,?,?,?)', (*key,time.time(),encoded))
            self.db.execute('INSERT OR IGNORE INTO capture_parts VALUES(?,?,?,?,?)', (*key,data['part'],samples))
        return f"ACK {data['boot']}-c{data['capture_id']}-{data['part']}\n"

    def capture(self, device, boot, capture_id):
        with self.lock:
            record = self.db.execute('SELECT received,meta,label,note FROM captures WHERE device=? AND boot=? AND capture_id=?', (device,boot,capture_id)).fetchone()
            if not record: raise ValueError('Capture not found')
            result = json.loads(record[1])
            result.update(received=record[0], label=record[2], note=record[3])
            rows = []
            for part, encoded in self.db.execute('SELECT part,samples FROM capture_parts WHERE device=? AND boot=? AND capture_id=? ORDER BY part', (device,boot,capture_id)):
                rows.extend(json.loads(encoded))
            result.update(samples=rows, complete=len(rows)==result['total'])
            return result

    def annotate(self, data):
        if not isinstance(data, dict) or data.get('label') not in {'unlabelled','fall_trial','normal_activity','false_alarm','missed_fall'}:
            raise ValueError('Invalid trial label')
        if not isinstance(data.get('note'),str) or len(data['note']) > 1000:
            raise ValueError('Note must be at most 1000 characters')
        if not isinstance(data.get('device'),str) or not isinstance(data.get('boot'),str) or type(data.get('capture_id')) is not int:
            raise ValueError('Invalid capture key')
        with self.lock, self.db:
            result = self.db.execute('UPDATE captures SET label=?,note=? WHERE device=? AND boot=? AND capture_id=?',
                                    (data['label'],data['note'],data['device'],data['boot'],data['capture_id']))
            if not result.rowcount: raise ValueError('Capture not found')

    def state(self):
        now = time.time()
        with self.lock:
            devices = []
            for device, boot, seq, seen, payload in self.db.execute('SELECT * FROM devices ORDER BY device'):
                item = json.loads(payload)
                item.update(last_seen=seen, online=(now-seen < 35))
                devices.append(item)
            history = []
            # Heartbeats update device status but do not clutter alarm history.
            for received, payload in self.db.execute("SELECT received,payload FROM events WHERE json_extract(payload, '$.type') != 'heartbeat' ORDER BY received DESC LIMIT 100"):
                item = json.loads(payload)
                if item['type'] == 'heartbeat':
                    continue
                ack = self.db.execute('SELECT acknowledged FROM acknowledgements WHERE device=? AND boot=? AND incident=?',
                                      (item['device'], item['boot'], item['incident'])).fetchone()
                item.update(received=received, caregiver_ack=ack[0] if ack else None)
                history.append(item)
                if len(history) >= 100:
                    break
            captures = []
            for device, boot, ident, received, meta, label, note in self.db.execute('SELECT * FROM captures ORDER BY received DESC LIMIT 60'):
                item = json.loads(meta)
                parts = self.db.execute('SELECT samples FROM capture_parts WHERE device=? AND boot=? AND capture_id=?', (device,boot,ident))
                received_rows = sum(len(json.loads(row[0])) for row in parts)
                item.update(received=received, label=label, note=note, received_rows=received_rows,
                            complete=received_rows==item['total'])
                captures.append(item)
        return {'devices': devices, 'events': history, 'captures': captures, 'server_time': now}


class Handler(BaseHTTPRequestHandler):
    server_version = 'ElderCare/1.0'

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def reply(self, status, body, content_type='application/json'):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(body)

    def authenticated(self):
        supplied = self.headers.get('Authorization', '')
        if not hmac.compare_digest(supplied.encode('utf-8'), ('Bearer ' + self.server.token).encode('ascii')):
            self.reply(401, '{"error":"Authentication required"}')
            return False
        return True

    def do_GET(self):
        route = urlparse(self.path)
        static = {'/': ('index.html','text/html; charset=utf-8'),
                  '/app.js': ('app.js','text/javascript; charset=utf-8'),
                  '/style.css': ('style.css','text/css; charset=utf-8')}
        if route.path in static:
            name, kind = static[route.path]
            self.reply(200, Path(__file__).with_name(name).read_bytes(), kind)
            return
        if not self.authenticated(): return
        try:
            if route.path == '/api/state':
                self.reply(200, json.dumps(self.server.store.state()))
            elif route.path == '/api/capture':
                query = parse_qs(route.query)
                capture = self.server.store.capture(query['device'][0],query['boot'][0],int(query['id'][0]))
                if query.get('format') == ['csv']:
                    if not capture['complete']: raise ValueError('Capture upload incomplete')
                    output = io.StringIO(newline='')
                    writer = csv.writer(output)
                    writer.writerow(['uptime_ms','raw_ax_mg','raw_ay_mg','raw_az_mg','filtered_ax_mg','filtered_ay_mg','filtered_az_mg','raw_gx_mdps','raw_gy_mdps','raw_gz_mdps','filtered_gx_mdps','filtered_gy_mdps','filtered_gz_mdps','state','flags'])
                    writer.writerows(capture['samples'])
                    self.reply(200, output.getvalue(), 'text/csv; charset=utf-8')
                else: self.reply(200, json.dumps(capture))
            else: self.reply(404, '{"error":"Not found"}')
        except (ValueError,KeyError,TypeError) as error:
            self.reply(400, json.dumps({'error':str(error)}))

    def do_POST(self):
        if not self.authenticated():
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= (8192 if self.path == '/api/notes' else 2048):
                raise ValueError('Invalid request length')
            if self.headers.get('Transfer-Encoding'):
                raise ValueError('Chunked requests are not supported')
            event = json.loads(self.rfile.read(length))
            if self.path == '/api/events':
                self.reply(200, self.server.store.accept(event), 'text/plain')
            elif self.path == '/api/captures':
                self.reply(200, self.server.store.accept_capture(event), 'text/plain')
            elif self.path == '/api/notes':
                self.server.store.annotate(event)
                self.reply(200, '{"ok":true}')
            elif self.path == '/api/ack':
                self.server.store.acknowledge(event['device'], event['boot'], event['incident'])
                self.reply(200, '{"ok":true}')
            else:
                self.reply(404, '{"error":"Not found"}')
        except (ValueError, KeyError, TypeError) as error:
            self.reply(400, json.dumps({'error': str(error)}))


def make_server(host, port, token, database):
    if not 16 <= len(token) <= 64 or any(ord(c) <= 32 or ord(c) > 126 for c in token):
        raise ValueError('ELDERCARE_TOKEN must contain 16-64 printable characters without spaces')
    server = ThreadingHTTPServer((host, port), Handler)
    server.token = token
    server.store = Store(database)
    return server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1', help='Use 0.0.0.0 for a trusted LAN demo')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--database', default=str(Path(__file__).with_name('events.db')))
    args = parser.parse_args()
    server = make_server(args.host, args.port, os.environ.get('ELDERCARE_TOKEN', ''), args.database)
    print(f'ElderCare receiver at http://{args.host}:{server.server_port}; Ctrl+C to stop')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        server.store.db.close()


if __name__ == '__main__':
    main()
