"""Verify deployed ESP bench mappings in SIM_UNO with a server mode guard."""
import json, pathlib, re, time, urllib.request
BASE = 'http://192.168.137.155'
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
seen, commands, checks = set(), [], []

def post(data):
    data = dict(data, expectedMode='SIM_UNO')
    urllib.request.urlopen(urllib.request.Request(BASE + '/api/input', data=json.dumps(data).encode(),
        headers={'Content-Type': 'application/json'}, method='POST'), timeout=5).read()

def state():
    snapshot = json.load(urllib.request.urlopen(BASE + '/api/state', timeout=5))
    assert snapshot['mode'] == 'SIM_UNO', 'simulation mode changed; abort all tests'
    return snapshot

def pump(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        snapshot = state()
        for event in snapshot['logs']:
            if event['seq'] in seen:
                continue
            seen.add(event['seq'])
            if event['source'] != 'SIM_TX':
                continue
            _, identifier, text = event['line'].rsplit('*', 1)[0].split(' ', 2)
            commands.append(text)
            post({'op': 'uno', 'line': f'DONE {identifier}'})
        time.sleep(.03)
    return state()

initial = state()
seen.update(e['seq'] for e in initial['logs'])
error = None
try:
    post({'op': 'manual_test', 'action': 'forward', 'pwm': 150, 'durationMs': 200})
    snapshot = pump(.5)
    assert '前向观测无效' in snapshot['debugResult'], snapshot['debugResult']
    checks.append('SAFE forward rejects missing front observation')
    post({'op': 'bench_safety', 'bypass': True})
    assert state()['benchBypass'] is True
    mapping = {'forward': (150,150), 'backward': (-150,-150), 'left': (-150,150), 'right': (150,-150),
               'left_forward': (150,0), 'left_reverse': (-150,0), 'right_forward': (0,150), 'right_reverse': (0,-150)}
    for action, (left, right) in mapping.items():
        start = len(commands)
        post({'op': 'manual_test', 'action': action, 'pwm': 150, 'durationMs': 200})
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            snapshot = pump(.2)
            sequence = commands[start:]
            if not snapshot['debugBusy'] and 'SESSION_STOP' in sequence:
                break
        expected = f'TEST_MOTOR {left} {right} 200 BYPASS'
        assert expected in sequence, (action, sequence)
        assert sequence.index('SESSION_START') < sequence.index(expected) < sequence.index('SESSION_STOP'), sequence
        assert not any(text.startswith('CAMERA_FRONT ') for text in sequence), sequence
        assert not snapshot['debugBusy']
        checks.append(action + ' mapping, unlock order, no front calibration, timed session stop')
        print(action + ': ' + expected, flush=True)
    post({'op': 'bench_safety', 'bypass': False})
    assert state()['benchBypass'] is False
    checks.append('avoidance restored by readback')
except Exception as failure:
    error = str(failure)
finally:
    try:
        post({'op': 'manual_test', 'action': 'stop'})
        pump(.5)
    except Exception as cleanup_error:
        error = error or str(cleanup_error)
    result = {'firmware': initial['firmware'], 'passed': error is None, 'error': error,
              'mode': 'SIM_UNO', 'physicalMotionIssued': False, 'serverExpectedModeGuard': True,
              'checks': checks, 'commands': commands}
    pathlib.Path(__file__).with_name('bench-host-validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({key: value for key, value in result.items() if key != 'commands'}, ensure_ascii=False, indent=2), flush=True)
if error:
    raise SystemExit(1)
