"""Check that on-board inference produces observations; not an accuracy test."""
import sys,pathlib,time,json,argparse,subprocess
root=pathlib.Path(__file__).parent
sys.path.insert(0,str(root/'vendor'/'python'))
import serial
parser=argparse.ArgumentParser();parser.add_argument('--host');parser.add_argument('--modes',nargs='+',choices=['OFF','TAGS','CAT','BOTH'],default=['OFF','TAGS','CAT','BOTH']);args=parser.parse_args()
wire=serial.Serial('COM11',115200,timeout=.1,write_timeout=2)
buffer=bytearray();records=[];raw=[]
def send(d):wire.write((json.dumps(d)+'\n').encode())
def sample(mode,seconds):
    global buffer
    send({'op':'vision_mode','mode':mode});end=time.monotonic()+seconds;states=[];last=0
    while time.monotonic()<end:
        if time.monotonic()-last>1:send({'op':'state'});last=time.monotonic()
        buffer.extend(wire.read(max(1,wire.in_waiting)))
        while b'\n' in buffer:
            line,_,buffer=buffer.partition(b'\n');raw.append(line.decode(errors='replace'))
            try:d=json.loads(line)
            except (ValueError,UnicodeError):continue
            if 'firmware' in d:states.append(d)
    assert len(states)>=2,(mode,'no states')
    assert states[-1]['frameSeq']>states[0]['frameSeq'],(mode,'camera stalled')
    if mode!='OFF':
        assert any(s.get('cameraVision',{} ) and s['cameraVision'].get('mode')==mode for s in states),(mode,'no inference observation')
    if args.host and mode!='OFF':
        check=subprocess.run([sys.executable,str(root/'http-smoke.py'),'--host',args.host],capture_output=True)
        if check.returncode:
            print(check.stderr.decode(errors='replace'),flush=True)
            raise RuntimeError(mode+' HTTP check failed')
    record={'mode':mode,'samples':len(states),'frameBefore':states[0]['frameSeq'],'frameAfter':states[-1]['frameSeq'],'heap':states[-1]['heap'],'cameraVision':states[-1].get('cameraVision')}
    records.append(record);print(json.dumps(record),flush=True)
try:
    send({'op':'control','action':'stop'})
    for mode in args.modes:sample(mode,35 if mode=='OFF' else 15)
finally:
    send({'op':'vision_mode','mode':'OFF'});wire.close()
    (root/'vendor'/'vision-runtime.log').write_text('\n'.join(raw),encoding='utf-8')
    (root/'vision-smoke-result.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
