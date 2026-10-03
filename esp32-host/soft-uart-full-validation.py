"""Validate full firmware through ESP HTTP -> D12 -> D1; never moves motors.

Does not open or write either USB serial port. The buzzer check verifies D13
telemetry only; an attached buzzer and audible sound require separate testing.
"""
import json, pathlib, re, time, urllib.request

BASE = 'http://192.168.137.155'
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
events, commands, checks = {}, [], []

def state():
    result = json.load(urllib.request.urlopen(BASE + '/api/state', timeout=5))
    assert result['mode'] == 'UART', 'physical mode changed'
    assert result['phase'] in ('IDLE', 'STOPPED'), 'policy is running'
    for event in result['logs']:
        events[event['seq']] = event
    return result

def command(text, attempt=0):
    before = state()
    assert not before['debugBusy'], 'another operation is active'
    start = max(e['seq'] for e in before['logs'])
    body = json.dumps({'op': 'command', 'command': text}).encode()
    urllib.request.urlopen(urllib.request.Request(BASE + '/api/input', data=body,
        headers={'Content-Type': 'application/json'}, method='POST'), timeout=5).read()
    deadline, identifier = time.monotonic() + 7, None
    while time.monotonic() < deadline:
        current = state()
        recent = [e for e in events.values() if e['seq'] > start]
        for event in recent:
            match = re.fullmatch(r'C (\d+) ' + re.escape(text) + r'\*[0-9A-F]{2}', event['line'])
            if event['source'] == 'UNO_TX' and match:
                identifier = int(match[1])
        if identifier is not None:
            replies = [e['line'] for e in recent if e['source'] == 'UNO_RX'
                       and re.match(r'(?:AK|ACK|DATA|DONE|ERROR) ' + str(identifier) + r'(?: |$)', e['line'])]
            terminal = next((line for line in replies if re.match(r'(?:DONE|ERROR) ' + str(identifier) + r'(?: |$)', line)), None)
            if terminal:
                commands.append({'command': text, 'id': identifier, 'replies': replies})
                if terminal.endswith(' TELEMETRY_BUSY') and text == 'GET_TELEMETRY' and attempt < 5:
                    time.sleep(.19 + .11 * attempt)
                    return command(text, attempt + 1)
                assert terminal.startswith('DONE '), terminal
                print(text + ': ' + terminal, flush=True)
                return current, replies
        time.sleep(.08)
    raise AssertionError('No matched reply for ' + text)

def check(name, condition, evidence):
    checks.append({'name': name, 'passed': bool(condition), 'evidence': evidence})
    assert condition, name

def telemetry():
    # The ESP already requests complete frames continuously; do not compete
    # with that request for Uno's one telemetry transaction slot.
    start = max(events, default=0)
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        current = state()
        for event in list(events.values()):
            match = re.fullmatch(r'DONE (\d+) TELEMETRY', event['line'])
            if event['seq'] > start and event['source'] == 'UNO_RX' and match:
                prefix = 'DATA ' + match[1] + ' HW '
                rows = [e['line'] for e in events.values() if e['source'] == 'UNO_RX' and e['line'].startswith(prefix)]
                if len(rows) == 13:
                    return current, rows
        time.sleep(.08)
    raise AssertionError('No complete automatic physical telemetry frame')

initial = state()
result = {'firmware': initial['firmware'], 'uartBaud': 9600, 'unoRxPin': 'D12',
          'unoTxPin': 'D1', 'usbSerialWrites': 0, 'physicalMotionIssued': False,
          'audibleBuzzerVerified': False, 'commands': commands, 'checks': checks}
failure = None
try:
    for text in ('PING', 'GET_CONFIG', 'GET_IO', 'GET_STATUS'):
        current, replies = command(text)
    current, replies = telemetry()
    rows = current['unoTelemetry']
    check('physical telemetry rows', len(rows) == 13 and all(r['source'] == 'UNO_RX' for r in rows), rows)
    command('SESSION_START')
    current, _ = command('LED ON')
    check('ESP onboard LED on', current['hostLed']['mode'] == 'ON' and current['hostLed']['gpioLevel'] == 0, current['hostLed'])
    current, _ = command('LED OFF')
    check('ESP onboard LED off', current['hostLed']['mode'] == 'OFF' and current['hostLed']['gpioLevel'] == 1, current['hostLed'])
    current, _ = command('LED FLASH 400 3000')
    check('ESP onboard LED flashing', current['hostLed']['mode'] == 'FLASH', current['hostLed'])
    samples = [current['hostLed']]
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        samples.append(state()['hostLed'])
        time.sleep(.13)
    check('flash switches both GPIO levels and expires', {s['gpioLevel'] for s in samples} == {0, 1} and samples[-1]['mode'] == 'OFF', samples)
    command('BEEP 3000')
    current, replies = telemetry()
    out = next((line for line in replies if ' HW OUT ' in line), '')
    check('Uno D13 buzzer requested and high', out.endswith(' 1 1 1'), out)
    time.sleep(3)
    _, replies = telemetry()
    out = next((line for line in replies if ' HW OUT ' in line), '')
    check('Uno D13 buzzer expires low', out.endswith(' 1 0 0'), out)
    command('LED ON')
    current, _ = command('SESSION_STOP')
    check('session stop shuts ESP LED off', current['hostLed']['mode'] == 'OFF', current['hostLed'])
except Exception as error:
    failure = str(error)
finally:
    try:
        command('SESSION_STOP')
    except Exception as cleanup_error:
        result['cleanupError'] = str(cleanup_error)
    result['passed'] = failure is None
    result['error'] = failure
    result['events'] = sorted(events.values(), key=lambda event: event['seq'])
    pathlib.Path(__file__).with_name('soft-uart-full-validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('events', 'commands', 'checks')}, ensure_ascii=False, indent=2), flush=True)
if failure:
    raise SystemExit(1)
