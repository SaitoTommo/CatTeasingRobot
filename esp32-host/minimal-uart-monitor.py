"""Read both USB logs; never inject traffic that could fake GPIO UART success."""
import argparse, collections, json, pathlib, sys, time

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor' / 'python'))
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=60)
parser.add_argument('--result', default='minimal-uart-result.json')
args = parser.parse_args()
channels = {}
for name, baud in [('COM10', 9600), ('COM11', 115200)]:
    state = {'baud': baud, 'bytes': 0, 'zeroBytes': 0, 'roundTrips': 0,
             'timeouts': 0, 'requestsReceived': 0, 'rawLines': 0,
             'error': None, 'lines': collections.deque(maxlen=120),
             'partial': bytearray(), 'port': None}
    channels[name] = state
    try:
        port = serial.Serial(port=None, baudrate=baud, timeout=.05)
        port.dtr = False
        port.rts = False
        port.port = name
        port.open()
        # Drop pre-existing USB queues; count only the new capture window.
        time.sleep(.3)
        port.reset_input_buffer()
        state['port'] = port
    except Exception as exc:
        state['error'] = str(exc)

started = time.monotonic()
next_report = started
print('Read-only monitoring; USB writes=0. Wiring confirmation is separate.', flush=True)
try:
    while time.monotonic() - started < args.seconds:
        for name, state in channels.items():
            port = state['port']
            if port is None:
                continue
            try:
                packet = port.read(min(4096, max(1, port.in_waiting)))
            except Exception as exc:
                state['error'] = str(exc)
                port.close()
                state['port'] = None
                continue
            state['bytes'] += len(packet)
            state['zeroBytes'] += packet.count(0)
            for byte in packet:
                if byte == 10:
                    raw = bytes(state['partial']).rstrip(b'\r')
                    state['partial'].clear()
                    if not raw:
                        continue
                    line = raw.decode('ascii', errors='backslashreplace')
                    state['lines'].append(line)
                    if line.startswith('ROUND_TRIP_OK ' if name == 'COM11' else 'DBG UNO ROUND_TRIP_OK '):
                        state['roundTrips'] += 1
                    if line == ('PONG_TIMEOUT' if name == 'COM11' else 'DBG UNO PONG_TIMEOUT'):
                        state['timeouts'] += 1
                    if line == ('RX PING UNO' if name == 'COM11' else 'DBG UNO REPLIED_TO_ESP'):
                        state['requestsReceived'] += 1
                    if line.startswith('RXHEX') or line.startswith('DBG UNO RXHEX'):
                        state['rawLines'] += 1
                elif len(state['partial']) < 512:
                    state['partial'].append(byte)
        now = time.monotonic()
        if now >= next_report:
            summary = {name: {key: state[key] for key in
                       ('bytes', 'zeroBytes', 'roundTrips', 'timeouts', 'requestsReceived', 'rawLines', 'error')}
                       for name, state in channels.items()}
            print(json.dumps({'elapsedSeconds': round(now-started, 1), 'ports': summary}), flush=True)
            next_report = now + 10
        time.sleep(.02)
finally:
    for state in channels.values():
        if state['port'] is not None:
            state['port'].close()

result = {'durationSeconds': round(time.monotonic()-started, 1), 'usbWrites': 0,
          'wiringConfirmed': False,
          'ports': {name: {**{key: state[key] for key in
                     ('baud', 'bytes', 'zeroBytes', 'roundTrips', 'timeouts', 'requestsReceived', 'rawLines', 'error')},
                     'lines': list(state['lines'])} for name, state in channels.items()}}
pathlib.Path(__file__).with_name(args.result).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({name: {key: state[key] for key in ('roundTrips', 'timeouts', 'requestsReceived', 'error')}
                  for name, state in channels.items()}), flush=True)
