"""On-device debug checks. Motion and setting changes are SIM_UNO only.
Real UART checks send STOP/SESSION_STOP, PING, GET_CONFIG and telemetry only.
"""
import argparse, gzip, json, pathlib, time, urllib.request, sys
sys.path.insert(0,str(pathlib.Path(__file__).parent/'vendor'/'python'))
import serial

parser=argparse.ArgumentParser();parser.add_argument('--host',default='192.168.137.155');parser.add_argument('--port',default='COM11');args=parser.parse_args()
base='http://'+args.host
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
wire=serial.Serial(port=None,baudrate=115200,timeout=.05,write_timeout=1)
wire.dtr=False;wire.rts=False;wire.port=args.port;wire.open()
def state():return json.load(urllib.request.urlopen(base+'/api/state',timeout=5))
def send(data):
    request=urllib.request.Request(base+'/api/input',data=json.dumps(data).encode(),headers={'Content-Type':'application/json'},method='POST')
    urllib.request.urlopen(request,timeout=5).read()
def reply(line):wire.write((json.dumps({'op':'uno','line':line})+'\n').encode())
def crc(text):
    result=0
    for value in text.encode():
        result^=value
        for _ in range(8):result=((result<<1)^(7 if result&128 else 0))&255
    return f'{result:02X}'

seen=set();commands=[];checks=[];values=[5.,5.,67.5,120.];real_rx=[];full_telemetry_sent=False
def pump(seconds,answer=True):
    global full_telemetry_sent
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        snapshot=state()
        for event in snapshot['logs']:
            if event['seq'] in seen:continue
            seen.add(event['seq'])
            if event['source']!='SIM_TX':continue
            payload,checksum=event['line'].rsplit('*',1);assert crc(payload)==checksum
            _,ident,command=payload.split(' ',2);commands.append(command)
            if not answer:continue
            reply(f'ACK {ident}')
            if command.startswith('SET_SAFE_DISTANCE LEFT '):values[0]=float(command.split()[-1])
            if command.startswith('SET_SAFE_DISTANCE RIGHT '):values[1]=float(command.split()[-1])
            if command.startswith('SET_GEOMETRY '):values[2:]=map(float,command.split()[1:])
            if command=='GET_CONFIG':reply(f'DATA {ident} CONFIG WHEEL_MM={values[2]:.1f} TRACK_MM={values[3]:.1f} SLOTS_PER_REV=20 SAFE_LEFT_CM={values[0]:.1f} SAFE_RIGHT_CM={values[1]:.1f}')
            if command=='GET_TELEMETRY':
                rows=[('STATE','1234 STOPPED 0 0 16383'),('SL','1 1 0 32 32 P'),('SR','1 1 0 40 40 P'),('EL','1 0 12 0.0 P'),('ER','1 0 13 0.0 P'),('T','TOP 1 1 0 P'),('T','LEFT 1 1 0 P'),('T','RIGHT 1 1 0 P'),('T','REAR 1 1 0 P'),('OUT','1 0 0 1 0 0'),('FRONT','-1 HOST')] if not full_telemetry_sent else []
                # HTTP is only the simulated peer. Repeating 11 synchronous
                # requests would delay its control ACKs beyond UART deadlines.
                for kind,fields in rows:reply(f'DATA {ident} HW {kind} {fields}')
                full_telemetry_sent=True
            suffix=' SESSION_STARTED' if command=='SESSION_START' else ' CAMERA_FRONT_SET' if command.startswith('CAMERA_FRONT ') else ''
            reply(f'DONE {ident}{suffix}')
        time.sleep(.08)
    return state()

initial=state();assert initial['phase'] in ('IDLE','STOPPED'),'stop policy before test'
assert initial['mode']=='SIM_UNO','test starts and ends in simulated mode'
original=initial['config']
try:
    with urllib.request.urlopen(base+'/',timeout=5) as response:
        page=response.read()
        if response.headers.get('Content-Encoding')=='gzip':page=gzip.decompress(page)
    for marker in (b'Real UART',b'Simulated UART',b'saveUnoConfig',b'HardwareTelemetry',b'sensor_'):assert marker in page
    checks.append('gzip page includes UART buttons, manual tests, configuration and shared sensor parser')
    seen.update(x['seq'] for x in state()['logs'])
    send({'op':'mode','mode':'SIM_UNO'});snapshot=pump(1)
    assert snapshot['unoConfig']['line'];assert len(snapshot['unoTelemetry'])==11
    # A later SL row must not refresh an older motor row.
    reply('DATA 62000 HW ML 0 0 0 1 0 0');time.sleep(.2)
    old=next(x['at'] for x in state()['unoTelemetry'] if ' HW ML ' in x['line'])
    snapshot=pump(2.7)
    row=next(x for x in snapshot['unoTelemetry'] if ' HW ML ' in x['line'])
    assert row['at']==old and snapshot['ms']-old>2500
    checks.append('sensor cache retains independent timestamps across partial telemetry')
    before=len(commands)
    send({'op':'uno_config','leftCm':4,'rightCm':8,'wheelMm':70,'trackMm':125});snapshot=pump(.3)
    assert '参数范围错误' in snapshot['debugResult']
    assert not any(c.startswith('SET_') for c in commands[before:])
    send({'op':'uno_config','leftCm':7,'rightCm':8,'wheelMm':70,'trackMm':125});snapshot=pump(6)
    assert '四项参数回读一致' in snapshot['debugResult'],snapshot['debugResult']
    settings=[c for c in commands[before:] if c.startswith('SET_') or c=='GET_CONFIG']
    assert settings==['SET_SAFE_DISTANCE LEFT 7.0','SET_SAFE_DISTANCE RIGHT 8.0','SET_GEOMETRY 70.0 125.0','GET_CONFIG'],settings
    checks.append('configuration validates ranges, waits for DONE in sequence, verifies readback')
    send({'op':'manual_test','action':'forward','pwm':150,'durationMs':300});snapshot=pump(.2)
    assert '前向观测无效' in snapshot['debugResult']
    before=len(commands)
    send({'op':'manual_test','action':'left','pwm':150,'durationMs':300});snapshot=pump(6)
    sequence=commands[before:]
    for expected in ('SESSION_START','CAMERA_FRONT INVALID','TURN LEFT 150 300','SESSION_STOP'):assert expected in sequence,sequence
    assert sequence.index('SESSION_START')<sequence.index('CAMERA_FRONT INVALID')<sequence.index('TURN LEFT 150 300')<sequence.index('SESSION_STOP')
    assert not snapshot['debugBusy']
    checks.append('SIM timed turn waits for session/front confirmation and ends with session stop')
    send({'op':'uno_config','leftCm':9,'rightCm':10,'wheelMm':70,'trackMm':125});snapshot=pump(3,False)
    assert '确认超时' in snapshot['debugResult'] and not snapshot['debugBusy']
    checks.append('missing Arduino confirmation cancels transaction and stops session')
    send({'op':'mode','mode':'UART'});time.sleep(.4)
    send({'op':'uno','line':'DATA 65000 HW SL 1 1 0 999 999 P'})
    send({'op':'manual_test','action':'ping'});send({'op':'command','command':'GET_CONFIG'});time.sleep(3)
    snapshot=state()
    assert snapshot['mode']=='UART'
    assert any(e['source']=='UNO_TX' and ' PING*' in e['line'] for e in snapshot['logs'])
    assert any(e['source']=='INPUT_REJECTED' for e in snapshot['logs'])
    assert not any('999 999' in r['line'] for r in snapshot['unoTelemetry'])
    real_rx=[e['line'] for e in snapshot['logs'] if e['source']=='UNO_RX']
    checks.append('real UART sends physical PING and rejects HTTP simulated reception')
finally:
    send({'op':'control','action':'stop'});send({'op':'mode','mode':'SIM_UNO'});send({'op':'vision_mode','mode':'OFF'});time.sleep(.3)
    wire.close()
final=state();assert final['config']==original
assert final['mode']=='SIM_UNO' and final['phase']=='STOPPED' and final['visionMode']=='OFF'
result={'host':args.host,'firmware':final['firmware'],'checks':checks,'simulatedCommands':commands,'realUnoReplies':real_rx,'realUnoReplyVerified':bool(real_rx),'physicalMotionIssued':False,'final':{k:final[k] for k in ('mode','phase','visionMode','cameraOk','heap','psram')}}
path=pathlib.Path(__file__).with_name('debug-smoke-result.json');path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
