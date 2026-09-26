"""LAN demonstration receiver. Python standard library only.

Set ELDERCARE_TOKEN to a random 16-64-character printable token before starting.
HTTP is unencrypted: bind to a trusted private demo network, not the public Internet.
"""
import argparse
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import sqlite3
import threading
import time

TYPES = {'fall', 'sos', 'local_ack', 'heartbeat'}
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
        return {'devices': devices, 'events': history, 'server_time': now}


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
        if self.path == '/':
            self.reply(200, Path(__file__).with_name('index.html').read_bytes(), 'text/html; charset=utf-8')
        elif self.path == '/api/state' and self.authenticated():
            self.reply(200, json.dumps(self.server.store.state()))
        elif self.path != '/api/state':
            self.reply(404, '{"error":"Not found"}')

    def do_POST(self):
        if not self.authenticated():
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 2048:
                raise ValueError('Invalid request length')
            if self.headers.get('Transfer-Encoding'):
                raise ValueError('Chunked requests are not supported')
            event = json.loads(self.rfile.read(length))
            if self.path == '/api/events':
                self.reply(200, self.server.store.accept(event), 'text/plain')
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
