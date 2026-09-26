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

def sensor(seq=1):
    return dict(device='test-board',boot='0123456789abcdef',seq=seq,uptime_ms=2000,
                config_revision=0,distance_mm=450,range_status=0,proximity_active=1,
                sound_dbfs=-420,sound_valid=1,sound_active=0,sound_masked=0,
                sound_events=2,audio_overruns=0,mic_error=0)


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
        self.assertIn(b'selectCapture', self.request('/app.js'))
        self.assertIn(b'.topbar', self.request('/style.css'))

    def test_capture_retry_order_export_and_notes(self):
        data = dict(device='test-board',boot=event()['boot'],capture_id=1,part=1,
                    total=5,pre_count=2,trigger_ms=40,source='raw_motion',outcome='no_rotation',
                    samples=[[i*20]+[0]*12+[1,0] for i in range(3,5)])
        route = '/api/capture?device=test-board&boot='+event()['boot']+'&id=1'
        self.assertEqual(self.request('/api/captures',data), b'ACK 0123456789abcdef-c1-1\n')
        self.request('/api/captures',data)
        self.assertFalse(json.loads(self.request(route))['complete'])
        with self.assertRaises(urllib.error.HTTPError): self.request(route+'&format=csv')
        changed=copy.deepcopy(data)
        changed['samples'][0][1]=8
        with self.assertRaises(urllib.error.HTTPError): self.request('/api/captures',changed)
        data['part']=0
        data['samples']=[[i*20]+[0]*12+[1,0] for i in range(3)]
        self.request('/api/captures',data)
        capture=json.loads(self.request(route))
        self.assertTrue(capture['complete'])
        self.assertEqual([r[0] for r in capture['samples']],[0,20,40,60,80])
        self.assertEqual(len(self.request(route+'&format=csv').decode().splitlines()),6)
        notes=dict(device=data['device'],boot=data['boot'],capture_id=1,label='missed_fall',note='Protected dummy; no rotation.')
        self.request('/api/notes',notes)
        self.assertEqual(json.loads(self.request(route))['label'],'missed_fall')
        reopened=receiver.Store(self.path)
        self.assertEqual(reopened.state()['captures'][0]['note'],notes['note'])
        reopened.db.close()
        with self.assertRaises(urllib.error.HTTPError): self.request(route,token='wrong')
        data['samples'][0][13]=5
        with self.assertRaises(urllib.error.HTTPError): self.request('/api/captures',data)

    def test_rejected_candidate(self):
        sample=event(5,'rejected','STARTUP')
        sample.update(reason='no_rotation',rejection_flags=10)
        self.request('/api/events',sample)
        self.assertEqual(json.loads(self.request('/api/state'))['events'][0]['rejection_flags'],10)

    def test_sensor_settings_delivery_and_confirmation(self):
        self.assertEqual(self.request('/api/sensors',sensor()),b'ACK 0123456789abcdef-s1\nCFG 1 1 1 0 300 800 -300\n')
        data=json.loads(self.request('/api/state'))['sensors'][0]
        self.assertTrue(data['online'])
        self.assertEqual(data['config_revision'],0)
        self.assertEqual(data['settings']['revision'],1)
        settings=dict(data['settings'],device='test-board',beeps=1,near_mm=200,far_mm=600,sound_threshold=-450)
        saved=json.loads(self.request('/api/sensor-settings',settings))
        self.assertEqual(saved['revision'],2)
        with self.assertRaises(urllib.error.HTTPError):self.request('/api/sensor-settings',settings)
        second=sensor(2)
        self.assertIn(b'CFG 2 1 1 1 200 600 -450\n',self.request('/api/sensors',second))
        second=sensor(3);second['config_revision']=2
        self.request('/api/sensors',second)
        data=json.loads(self.request('/api/state'))['sensors'][0]
        self.assertEqual(data['config_revision'],data['settings']['revision'])
        self.assertEqual(len(data['history']),3)
        reopened=receiver.Store(self.path)
        self.assertEqual(reopened.state()['sensors'][0]['settings']['beeps'],1)
        reopened.db.close()

    def test_sensor_validation_retries_and_staleness(self):
        self.request('/api/sensors',sensor())
        self.request('/api/sensors',sensor())
        self.assertEqual(len(json.loads(self.request('/api/state'))['sensors'][0]['history']),1)
        bad=sensor();bad['distance_mm']=999
        with self.assertRaises(urllib.error.HTTPError):self.request('/api/sensors',bad)
        for name,value in [('sound_dbfs',1),('distance_mm',-2),('sound_valid',True),('device','<script>')]:
            bad=sensor(2);bad[name]=value
            with self.assertRaises(urllib.error.HTTPError):self.request('/api/sensors',bad)
        with self.assertRaises(urllib.error.HTTPError):self.request('/api/sensors',sensor(3),token='wrong')
        bad=dict(receiver.SENSOR_DEFAULTS,device='test-board',near_mm=800,far_mm=700)
        with self.assertRaises(urllib.error.HTTPError):self.request('/api/sensor-settings',bad)
        rebooted=sensor(1);rebooted['boot']='abcdef0123456789'
        self.request('/api/sensors',rebooted)
        self.request('/api/sensors',sensor(9))
        self.assertEqual(json.loads(self.request('/api/state'))['sensors'][0]['boot'],rebooted['boot'])
        with self.server.store.lock,self.server.store.db:
            self.server.store.db.execute('UPDATE sensor_latest SET received=?',(time.time()-20,))
        self.assertFalse(json.loads(self.request('/api/state'))['sensors'][0]['online'])


if __name__ == '__main__':
    unittest.main()
