"""Bounded read-only USB capture. Opening USB can still reset some Uno adapters."""
import argparse
import json
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor' / 'python'))
import serial
from serial.tools import list_ports

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=60)
parser.add_argument('--port', default='COM10')
parser.add_argument('--result', required=True)
args = parser.parse_args()
print(json.dumps({'ports': [{'port': p.device, 'description': p.description}
                           for p in list_ports.comports()]}), flush=True)
port = serial.Serial(port=None, baudrate=57600, timeout=.1)
port.dtr = False
port.rts = False
port.port = args.port
try:
    port.open()
except Exception as exc:
    print(f'PORT_OPEN_FAILED {args.port}: {exc}', flush=True)
    sys.exit(2)
started = time.monotonic()
records = []
partial = bytearray()
next_report = 0
printed = 0
received_bytes = 0
failure = None
try:
    while time.monotonic() - started < args.seconds:
        packet = port.read(min(4096, max(1, port.in_waiting)))
        received_bytes += len(packet)
        if received_bytes > 16384 + (time.monotonic()-started)*7000:
            raise RuntimeError('Read rate exceeds 57600 8N1 capacity; capture is not trustworthy')
        for byte in packet:
            if byte == 10:
                line = bytes(partial).rstrip(b'\r').decode('ascii', errors='backslashreplace')
                partial.clear()
                records.append({'seconds': round(time.monotonic() - started, 3), 'line': line})
                if printed < 30 and line.startswith(('READY ', 'EVENT DIAG', 'ERROR ')):
                    print(line, flush=True)
                    printed += 1
            elif len(partial) < 1024:
                partial.append(byte)
        elapsed = time.monotonic() - started
        if elapsed >= next_report:
            print(f'CAPTURE {elapsed:.1f}s lines={len(records)} usbWrites=0', flush=True)
            next_report = elapsed + 10
except Exception as exc:
    failure = str(exc)
    print('CAPTURE_STOPPED: ' + failure, flush=True)
finally:
    port.close()
    result = {'port': args.port, 'baud': 57600, 'usbWrites': 0,
              'durationSeconds': round(time.monotonic() - started, 3),
              'receivedBytes': received_bytes, 'failure': failure,
              'partial': bytes(partial).decode('ascii', errors='backslashreplace'), 'lines': records}
    pathlib.Path(args.result).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
if failure:
    sys.exit(1)
