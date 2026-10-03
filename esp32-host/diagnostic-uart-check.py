"""ESP USB JSON commands -> physical UART -> Uno; Uno USB is read-only."""
import argparse
import json
import pathlib
import re
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor' / 'python'))
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=120)
parser.add_argument('--result', required=True)
args = parser.parse_args()
ports, partial, records, snapshots, commands = {}, {}, [], [], []
started = time.monotonic()
armed = False

def send(payload):
    ports['COM11'].write((json.dumps(payload) + '\n').encode())

def pump(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for name, port in ports.items():
            packet = port.read(min(16384, max(1, port.in_waiting)))
            partial[name].extend(packet)
            while b'\n' in partial[name]:
                raw, _, tail = partial[name].partition(b'\n')
                partial[name] = bytearray(tail)
                line = raw.rstrip(b'\r').decode('ascii', errors='backslashreplace')
                record = {'seconds': round(time.monotonic()-started, 3), 'port': name, 'line': line}
                records.append(record)
                if name == 'COM11':
                    try:
                        data = json.loads(line)
                    except ValueError:
                        continue
                    if 'firmware' in data:
                        snapshots.append(data)
                    if data.get('source') in ('UNO_TX', 'UNO_RX', 'UNO_PROTOCOL_ERROR', 'CRC_ERROR', 'TELEMETRY_TIMEOUT'):
                        record['event'] = data
                    if armed and data.get('source') in ('FAULT', 'CRC_ERROR', 'UART_BAD_FRAME', 'TELEMETRY_TIMEOUT'):
                        raise AssertionError('ESP fault during check: ' + line)
                else:
                    if armed and line.startswith(('READY ', 'ERROR ')):
                        raise AssertionError('Uno reset/error during check: ' + line)
                    match = re.match(r'EVENT DIAG M (-?\d+) (\d+) (\d+) (-?\d+) (-?\d+) ([01])\*', line)
                    if match and (int(match[3]) or int(match[6])):
                        raise AssertionError('Uno memory warning during check: ' + line)
        time.sleep(.005)

def get_state():
    previous = len(snapshots)
    send({'op': 'state'})
    deadline = time.monotonic()+5
    while len(snapshots) == previous and time.monotonic() < deadline:
        pump(.1)
    assert len(snapshots) > previous, 'ESP state reply missing'
    return snapshots[-1]

def command(text):
    start = len(records)
    send({'op': 'command', 'command': text})
    deadline, identifier = time.monotonic()+5, None
    while time.monotonic() < deadline:
        pump(.1)
        for record in records[start:]:
            event = record.get('event', {})
            if event.get('source') == 'UNO_TX':
                match = re.fullmatch(r'C (\d+) ' + re.escape(text) + r'\*[0-9A-F]{2}', event['line'])
                if match:
                    identifier = int(match[1])
            if identifier and event.get('source') == 'UNO_RX' and event['line'].startswith(f'DONE {identifier} '):
                result = {'command': text, 'id': identifier, 'reply': event['line']}
                commands.append(result)
                print(json.dumps(result), flush=True)
                return
    raise AssertionError('No matched DONE for ' + text)

failure = None
try:
    for name, baud in [('COM10', 57600), ('COM11', 115200)]:
        port = serial.Serial(port=None, baudrate=baud, timeout=.01, write_timeout=1)
        port.dtr = False
        port.rts = False
        port.port = name
        port.open()
        ports[name], partial[name] = port, bytearray()
    pump(2)
    initial = get_state()
    assert initial['firmware'] == 'ESP32_CAT_HOST_0.7', 'ESP upgrade not confirmed'
    assert initial['phase'] in ('IDLE', 'STOPPED'), 'stop policy before communication check'
    send({'op': 'mode', 'mode': 'UART'})
    pump(3)
    current = get_state()
    assert current['mode'] == 'UART' and current['phase'] in ('IDLE', 'STOPPED')
    assert current['lastUnoAgeMs'] < 2500, 'no fresh physical Uno reply'
    armed = True
    for text in ['PING']*10 + ['GET_CONFIG', 'DEBUG_DIAG 2', 'PING', 'DEBUG_DIAG 1']:
        command(text)
        pump(.25)
    next_report = 0
    while time.monotonic()-started < args.seconds:
        pump(.2)
        elapsed = time.monotonic()-started
        if elapsed >= next_report:
            current = get_state()
            assert current['mode'] == 'UART' and current['phase'] in ('IDLE', 'STOPPED')
            assert current['lastUnoAgeMs'] < 2500, 'physical Uno reply stale'
            print(json.dumps({'elapsed': round(elapsed, 1), 'mode': current['mode'],
                              'phase': current['phase'], 'lastUnoAgeMs': current['lastUnoAgeMs']}), flush=True)
            next_report = elapsed+10
except Exception as exc:
    failure = str(exc)
    print('CHECK_STOPPED: ' + failure, flush=True)
finally:
    for port in ports.values():
        port.close()
    result = {'durationSeconds': round(time.monotonic()-started, 3), 'failure': failure,
              'unoUsbWrites': 0, 'motionCommandsIssued': False, 'commands': commands,
              'records': records, 'state': snapshots[-1] if snapshots else None}
    pathlib.Path(args.result).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
if failure:
    sys.exit(1)
