"""COM11 integration checks. Fake Uno replies never go to physical UART."""
import json, sys, time, pathlib, re, argparse
sys.path.insert(0,str(pathlib.Path(__file__).parent/'vendor'/'python'))
import serial
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM11');args=parser.parse_args()
wire=serial.Serial(args.port,115200,timeout=.05,write_timeout=1)
events=[];snapshots=[];buffer=bytearray();motion=None;counts=0;last_mp=last_vision=0;vision={'cat':{'detected':False},'tags':[],'front':'CLEAR'};originalRoute=None
def send(x): wire.write((json.dumps(x,separators=(',',':'))+'\n').encode())
def crc(s):
    c=0
    for b in s.encode():
        c^=b
        for _ in range(8): c=((c<<1)^(7 if c&128 else 0))&255
    return f'{c:02X}'
def reply(s):send({'op':'uno','line':s})
def simulate(cmd):
    global motion,counts
    payload,check=cmd.rsplit('*',1);assert crc(payload)==check
    _,ident,command=payload.split(' ',2);ident=int(ident)
    reply(f'ACK {ident}')
    if command=='GET_CONFIG':reply(f'DATA {ident} CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=5 SAFE_RIGHT_CM=5')
    if command=='GET_TELEMETRY':
        for side in ('SL','SR'):reply(f'DATA {ident} HW {side} 0 0 -1 -1 100 I')
    if command.startswith('MOTOR '):
        motion=ident;counts=0;msg=f'MS {ident} 0 0';reply(msg+'*'+crc(msg));return
    if command in ('STOP','SESSION_STOP'):motion=None
    suffix=' SESSION_STOPPED' if command=='SESSION_STOP' else ' SESSION_STARTED' if command=='SESSION_START' else ''
    reply(f'DONE {ident}{suffix}')
def pump(seconds,feed=True):
    global buffer,last_mp,last_vision,counts
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        now=time.monotonic()
        if feed and now-last_vision>.15:send({'op':'vision','vision':vision});last_vision=now
        if feed and motion and now-last_mp>.1:
            last_mp=now;counts+=2;msg=f'MP {motion} {counts} {counts}';reply(msg+'*'+crc(msg))
        buffer.extend(wire.read(max(1,wire.in_waiting)))
        while b'\n' in buffer:
            line,_,buffer=buffer.partition(b'\n')
            try:d=json.loads(line)
            except (ValueError,UnicodeError):continue
            if 'firmware' in d:snapshots.append(d)
            else:
                events.append(d)
                if d.get('source')=='SIM_TX':simulate(d['line'])
def state():send({'op':'state'});pump(.6);assert snapshots,'no state received';return snapshots[-1]
try:
    send({'op':'mode','mode':'SIM_UNO'});pump(2)
    initial=state();assert initial['mode']=='SIM_UNO';originalRoute=initial['config']['route']
    send({'op':'config','config':{'route':[41,2,100,41,586,0]}});pump(.6)
    assert state()['config']['route']==[41,2,100,41,586,0]
    send({'op':'config','config':{'route':[-1]}});pump(.4)
    assert state()['config']['route']==[41,2,100,41,586,0]
    reply('MP 999 8 8*00');pump(.4);assert any(x.get('source')=='CRC_ERROR' for x in events)
    vision={'cat':{'detected':False},'tags':[{'id':41,'centerX':.5,'width':.3}],'front':'CLEAR'}
    send({'op':'control','action':'start'});pump(1.3)
    reached=state();assert reached['phase']=='PATROL',reached['phase'];assert any(x.get('source')=='TAG_REACHED' and x['line']=='41' for x in events)
    # No checked movement feedback: ESP32 must stop rather than renew forever.
    pump(1.1,feed=False);stopped=state();assert stopped['phase']=='STOPPED',stopped['phase']
    send({'op':'control','action':'stop'});pump(.4)
    send({'op':'config','config':{'route':originalRoute}});pump(.4)
    assert state()['config']['route']==originalRoute,'original route not restored'
    before=state();pump(3);after=state();assert after['cameraOk'];assert after['frameSeq']>before['frameSeq'],'camera frame sequence stalled'
    result={'port':args.port,'firmware':after['firmware'],'checks':['USB JSON','ordered route with repeated ID and six nodes','invalid route rejected','CRC error rejected','tag reached and target advanced','missing encoder feedback stops','camera frames advance'], 'cameraBefore':before['frameSeq'],'cameraAfter':after['frameSeq'],'cameraVision':after.get('cameraVision'),'phase':after['phase'],'heap':after['heap'],'psram':after['psram'],'events':events}
    dest=pathlib.Path(__file__).parent/'serial-smoke-result.json';dest.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='events'},ensure_ascii=False,indent=2))
finally:
    try:
        send({'op':'control','action':'stop'});pump(.3)
        if originalRoute is not None:send({'op':'config','config':{'route':originalRoute}});pump(.3)
    finally:wire.close()
