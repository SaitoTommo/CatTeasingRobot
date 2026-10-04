"""Passive USB capture: no writes, no input flushing, DTR/RTS disabled before open."""
import argparse
import datetime
import json
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor' / 'python'))
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--directory', required=True)
parser.add_argument('--seconds', type=float, default=1800)
args = parser.parse_args()
folder = pathlib.Path(args.directory)
folder.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
events = (folder / 'events.jsonl').open('a', encoding='utf-8', buffering=1)
channels = {}
for name, baud in [('COM10', 57600), ('COM11', 115200)]:
    channels[name] = dict(baud=baud, port=None, retry=0, bytes=0, lines=0,
                          partial=bytearray(), raw=(folder / (name+'.bin')).open('ab', buffering=0),
                          text=(folder / (name+'.jsonl')).open('a', encoding='utf-8', buffering=1))

def record(file, data):
    data = dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                elapsed=round(time.monotonic()-started, 3), **data)
    file.write(json.dumps(data, ensure_ascii=True)+'\n')

record(events, dict(event='start', usbWrites=0, duration=args.seconds))
next_report = started+2
try:
    while time.monotonic()-started < args.seconds:
        for name, c in channels.items():
            if c['port'] is None:
                if time.monotonic() < c['retry']:
                    continue
                port = serial.Serial(port=None, baudrate=c['baud'], timeout=0)
                port.dtr = False
                port.rts = False
                port.port = name
                try:
                    port.open()
                    c['port'] = port
                    record(events, dict(event='open', port=name, baud=c['baud'], dtr=False, rts=False))
                    print('OPEN '+name+' baud='+str(c['baud'])+' DTR=0 RTS=0 writes=0', flush=True)
                except Exception as exc:
                    port.close()
                    record(events, dict(event='open_failed', port=name, error=str(exc)))
                    c['retry'] = time.monotonic()+2
                    continue
            try:
                packet = c['port'].read(min(16384, c['port'].in_waiting))
                if not packet:
                    continue
                c['raw'].write(packet)
                c['bytes'] += len(packet)
                c['partial'].extend(packet)
                while b'\n' in c['partial']:
                    line, _, tail = c['partial'].partition(b'\n')
                    c['partial'] = bytearray(tail)
                    text = line.rstrip(b'\r').decode('utf-8', errors='backslashreplace')
                    c['lines'] += 1
                    record(c['text'], dict(port=name, line=text))
                    if any(marker in text for marker in ('READY ', 'STOP_UNCONFIRMED', 'UART_LOOPBACK', 'SCAN_HANDOFF_RETRY', 'FAULT', 'ERROR ')):
                        print(name+' '+text[:1000], flush=True)
                if len(c['partial']) > 65536:
                    record(events, dict(event='unterminated_bytes', port=name, hex=c['partial'].hex()))
                    c['partial'].clear()
            except Exception as exc:
                record(events, dict(event='disconnected', port=name, error=str(exc), partialHex=c['partial'].hex()))
                print('DISCONNECTED '+name+' '+str(exc), flush=True)
                c['port'].close()
                c['port'] = None
                c['partial'].clear()
                c['retry'] = time.monotonic()+2
        if time.monotonic() >= next_report:
            print('CAPTURE '+json.dumps({n:dict(open=c['port'] is not None, bytes=c['bytes'], lines=c['lines']) for n,c in channels.items()})+' writes=0', flush=True)
            next_report = time.monotonic()+30
        if sum(c['bytes'] for c in channels.values()) > 100_000_000:
            record(events, dict(event='size_limit'))
            break
        time.sleep(.01)
except KeyboardInterrupt:
    record(events, dict(event='interrupted'))
finally:
    for name, c in channels.items():
        record(events, dict(event='finish', port=name, bytes=c['bytes'], lines=c['lines'], partialHex=c['partial'].hex(), usbWrites=0))
        if c['port'] is not None:
            c['port'].close()
        c['raw'].close()
        c['text'].close()
    events.close()
