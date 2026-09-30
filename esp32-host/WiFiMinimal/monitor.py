"""Capture the minimal firmware's USB output without sending commands."""
import argparse,pathlib,sys,time
root=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(root.parent/'vendor'/'python'))
import serial
parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM11')
parser.add_argument('--seconds',type=int,default=40)
args=parser.parse_args()
wire=serial.Serial()
wire.port=args.port;wire.baudrate=115200;wire.timeout=.2
wire.dtr=False;wire.rts=False;wire.open()
lines=[];end=time.monotonic()+args.seconds
try:
    while time.monotonic()<end:
        line=wire.readline().decode(errors='replace').strip()
        if line:
            lines.append(line);print(line,flush=True)
finally:
    wire.close()
    (root/'wifi-result.log').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('RESULT: '+('CONNECTED' if any(x.startswith('CONNECTED ') for x in lines) else 'NO_CONNECTION_OBSERVED'))
