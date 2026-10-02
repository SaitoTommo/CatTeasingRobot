"""Test the actual Uno parser with all physical outputs disabled first."""
import json, pathlib, re, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor' / 'python'))
import serial

wire = serial.Serial(port=None, baudrate=9600, timeout=.05, write_timeout=2)
wire.dtr = wire.rts = False
wire.port = 'COM10'
wire.open()
records = []

def crc(text):
    value = 0
    for byte in text.encode('ascii'):
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (7 if value & 128 else 0)) & 255
    return value

def command(text, expected='DONE'):
    identifier = 5000 + len(records)
    payload = f'C {identifier} {text}'
    wire.write(f'{payload}*{crc(payload):02X}\n'.encode('ascii'))
    rows, deadline = [], time.monotonic() + 6
    while time.monotonic() < deadline:
        row = wire.readline().decode('ascii', errors='replace').strip()
        if row:
            rows.append(row)
            if re.match(fr'(?:DONE|ERROR|MD|MA) {identifier}(?: |\*|$)', row):
                records.append({'command': text, 'id': identifier, 'lines': rows})
                assert row.startswith(expected + f' {identifier}'), row
                print(text + ': ' + row, flush=True)
                return rows
    records.append({'command': text, 'id': identifier, 'lines': rows})
    raise AssertionError('timeout ' + text)

failure = None
outputs_verified_off = False
try:
    wire.reset_input_buffer()
    command('SESSION_STOP')
    command('DEBUG_MODE ON')
    command('DEBUG_MOTOR_MODE DRY_RUN')
    rows = command('GET_IO')
    io = [row for row in rows if re.match(r'DATA \d+ IO ', row)]
    assert len(io) == 14 and all(row.endswith(' OFF') for row in io), 'physical I/O must be off before tests'
    outputs_verified_off = True
    command('SESSION_START')
    rows = command('TEST_MOTOR 150 150 100 SAFE', 'ERROR')
    assert any('FRONT_OBSTACLE_OR_CAMERA_INVALID' in row for row in rows), rows
    command('DEBUG_INJECT TOUCH TOP PRESS')
    for left, right in ((150,150),(-150,-150),(-150,150),(150,-150),(150,0),(-150,0),(0,150),(0,-150)):
        command(f'TEST_MOTOR {left} {right} 100 BYPASS', 'MD')
    command('TEST_MOTOR 150 0 2001 BYPASS', 'ERROR')
    command('SESSION_STOP')
    command('TEST_MOTOR 150 0 100 BYPASS', 'ERROR')
    command('DEBUG_MOTOR_MODE LIVE')
    command('DEBUG_MODE OFF')
except Exception as error:
    failure = str(error)
finally:
    try:
        command('SESSION_STOP')
    except Exception as cleanup_error:
        records.append({'cleanupError': str(cleanup_error)})
    wire.close()
    result = {'passed': failure is None, 'error': failure, 'physicalMotionIssued': False,
              'outputsVerifiedOffBeforeTests': outputs_verified_off, 'mode': 'DRY_RUN for test commands', 'records': records}
    pathlib.Path(__file__).with_name('bench-uno-validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({key: value for key, value in result.items() if key != 'records'}, ensure_ascii=False, indent=2))
if failure:
    raise SystemExit(1)
