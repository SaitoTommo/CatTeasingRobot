"""Physical, stopped UART check via ESP HTTP. No USB writes or motor commands."""
import argparse
import json
import pathlib
import re
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
parser.add_argument('--seconds', type=float, default=60)
parser.add_argument('--result', required=True)
args = parser.parse_args()
base = 'http://' + args.host
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
events, commands = {}, []
started = time.monotonic()
initial_seq = None
failure, final = None, None

def state():
    global initial_seq, final
    final = json.load(urllib.request.urlopen(base+'/api/state', timeout=5))
    if initial_seq is None:
        initial_seq = max((e['seq'] for e in final['logs']), default=0)
    for event in final['logs']:
        if event['seq'] > initial_seq:
            events[event['seq']] = event
    return final

def send(data):
    request = urllib.request.Request(base+'/api/input', data=json.dumps(data).encode(),
        headers={'Content-Type': 'application/json'}, method='POST')
    urllib.request.urlopen(request, timeout=5).read()

def verify(current):
    assert current['mode'] == 'UART' and current['phase'] in ('IDLE', 'STOPPED')
    assert current['lastUnoAgeMs'] < 2500, 'Uno physical reply stale'
    for event in events.values():
        if event['source'] in ('CRC_ERROR', 'UART_BAD_FRAME', 'TELEMETRY_TIMEOUT', 'UNO_PROTOCOL_ERROR'):
            raise AssertionError(json.dumps(event))
        if event['source'] == 'UNO_RX':
            assert not event['line'].startswith(('READY ', 'ERROR ')), 'Uno reset or error: '+event['line']
            match = re.fullmatch(r'EVENT DIAG M (-?\d+) (\d+) (\d+) (-?\d+) (-?\d+) ([01])', event['line'])
            if match:
                assert int(match[3]) == 0 and int(match[6]) == 0, 'Uno memory warning: '+event['line']

try:
    initial = state()
    assert initial['firmware'] == 'ESP32_CAT_HOST_0.7'
    assert initial['phase'] in ('IDLE', 'STOPPED') and not initial['debugBusy']
    if initial['mode'] != 'UART':
        send({'op': 'mode', 'mode': 'UART'})
        time.sleep(1)
    verify(state())
    for text in ['PING']*5 + ['GET_CONFIG', 'DEBUG_DIAG 2', 'PING', 'DEBUG_DIAG 1']:
        before = max(events, default=initial_seq)
        send({'op': 'command', 'command': text})
        deadline, identifier, done = time.monotonic()+5, None, None
        while time.monotonic() < deadline and not done:
            current = state()
            verify(current)
            recent = [e for e in events.values() if e['seq'] > before]
            for event in recent:
                match = re.fullmatch(r'C (\d+) '+re.escape(text)+r'\*[0-9A-F]{2}', event['line'])
                if match and event['source'] == 'UNO_TX':
                    identifier = int(match[1])
                if identifier and event['source'] == 'UNO_RX' and re.match(r'DONE '+str(identifier)+r'(?: |$)', event['line']):
                    done = event['line']
            time.sleep(.12)
        assert done, 'No matched DONE for '+text
        commands.append({'command': text, 'id': identifier, 'reply': done})
        print(json.dumps(commands[-1]), flush=True)
    next_report = 0
    while time.monotonic()-started < args.seconds:
        current = state()
        verify(current)
        elapsed = time.monotonic()-started
        if elapsed >= next_report:
            print(json.dumps({'seconds': round(elapsed, 1), 'lastUnoAgeMs': current['lastUnoAgeMs']}), flush=True)
            next_report = elapsed+10
        time.sleep(.25)
except Exception as exc:
    failure = str(exc)
    print('CHECK_STOPPED: '+failure, flush=True)
finally:
    pathlib.Path(args.result).write_text(json.dumps({'host': args.host, 'durationSeconds': round(time.monotonic()-started, 3),
        'failure': failure, 'motionCommandsIssued': False, 'commands': commands,
        'events': list(events.values()), 'state': final}, ensure_ascii=False, indent=2), encoding='utf8')
if failure:
    raise SystemExit(1)
