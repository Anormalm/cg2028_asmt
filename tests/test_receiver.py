import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('receiver', ROOT / 'receiver/server.py')
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)
TOKEN = 'test-token-for-local-test-only'

def event(seq=1, kind='fall', state='FALL'):
    return dict(device='test-board', boot='0123456789abcdef', seq=seq, incident=1,
                type=kind, state=state, reason='posture', uptime_ms=2000,
                accel_mg=1000, gyro_dps=40, min_mg=900, peak_mg=1800,
                peak_dps=150, dropped=0)


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'events.db'
        self.server = receiver.make_server('127.0.0.1', 0, TOKEN, self.path)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}'

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.server.store.db.close()
        self.temp.cleanup()

    def request(self, path, body=None, token=TOKEN):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data,
                    headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.read()

    def test_delivery_retry_ack_and_persistence(self):
        expected = b'ACK 0123456789abcdef-1\n'
        self.assertEqual(self.request('/api/events', event()), expected)
        self.assertEqual(self.request('/api/events', event()), expected)
        state = json.loads(self.request('/api/state'))
        self.assertEqual(len(state['events']), 1)
        self.assertTrue(state['devices'][0]['online'])
        self.request('/api/ack', dict(device='test-board', boot=event()['boot'], incident=1))
        state = json.loads(self.request('/api/state'))
        self.assertIsNotNone(state['events'][0]['caregiver_ack'])
        self.assertEqual(state['devices'][0]['state'], 'FALL')  # Remote ACK never clears board state.
        self.request('/api/events', event(2, 'local_ack', 'STARTUP'))
        self.request('/api/events', event(3, 'heartbeat', 'NORMAL'))
        self.request('/api/events', event())  # Late retry must not revert current state.
        state = json.loads(self.request('/api/state'))
        self.assertEqual(state['devices'][0]['state'], 'NORMAL')
        self.assertEqual(len(state['events']), 2)
        reopened = receiver.Store(self.path)
        self.assertEqual(len(reopened.state()['events']), 2)
        reopened.db.close()

    def test_invalid_token_and_payload(self):
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.request('/api/events', event(), token='wrong')
        self.assertEqual(error.exception.code, 401)
        bad = event(9)
        bad['device'] = '<script>alert(1)</script>'
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.request('/api/events', bad)
        self.assertEqual(error.exception.code, 400)

    def test_conflicting_duplicate(self):
        original = event(100)
        original['device'] = 'conflict-test'
        self.request('/api/events', original)
        original['peak_mg'] = 2000
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.request('/api/events', original)
        self.assertEqual(error.exception.code, 400)

    def test_offline_and_old_boot(self):
        store = receiver.Store(':memory:')
        store.accept(event())
        rebooted = event()
        rebooted['boot'] = 'abcdef0123456789'
        rebooted['state'] = 'NORMAL'
        rebooted['type'] = 'heartbeat'
        store.accept(rebooted)
        store.accept(event())
        self.assertEqual(store.state()['devices'][0]['state'], 'NORMAL')
        store.db.execute('UPDATE devices SET last_seen=?', (time.time()-40,))
        store.db.commit()
        self.assertFalse(store.state()['devices'][0]['online'])
        store.db.close()

    def test_dashboard_served(self):
        self.assertIn(b'ElderCare alerts', self.request('/'))


if __name__ == '__main__':
    unittest.main()
