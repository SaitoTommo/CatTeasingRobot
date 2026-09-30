import urllib.request, time, json, pathlib, io, argparse
from PIL import Image
root=pathlib.Path(__file__).parent
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
start=time.monotonic()
parser=argparse.ArgumentParser();parser.add_argument('--host',default='192.168.4.1');args=parser.parse_args()
base='http://'+args.host
state=json.load(urllib.request.urlopen(base+'/api/state',timeout=5))
assert state['cameraOk']
page=urllib.request.urlopen(base+'/',timeout=5).read();assert 'Uno'.encode() in page
assert state['mode']=='SIM_UNO','HTTP injection check requires SIM_UNO'
assert state['phase'] in ('IDLE','STOPPED'),'stop policy before HTTP check'
marker='ACK 65000'
request=urllib.request.Request(base+'/api/input',data=json.dumps({'op':'uno','line':marker}).encode(),headers={'Content-Type':'application/json'},method='POST')
urllib.request.urlopen(request,timeout=5).read()
time.sleep(.3)
received=json.load(urllib.request.urlopen(base+'/api/state',timeout=5))
assert any(x['source']=='SIM_RX' and x['line']==marker for x in received['logs']),'received Uno message absent from debug log'
frame=urllib.request.urlopen(base+'/capture',timeout=5).read();img=Image.open(io.BytesIO(frame));img.load();assert img.size==(320,240)
(root/'http-camera-snapshot.jpg').write_bytes(frame)
frames=[];buffer=b''
with urllib.request.urlopen(base+':81/stream',timeout=5) as stream:
    assert 'multipart/x-mixed-replace' in stream.headers['content-type']
    deadline=time.monotonic()+8
    while len(frames)<5 and time.monotonic()<deadline:
        buffer+=stream.read(1024)
        while b'\xff\xd8' in buffer and b'\xff\xd9' in buffer:
            a=buffer.index(b'\xff\xd8');b=buffer.index(b'\xff\xd9',a)+2
            data=buffer[a:b];buffer=buffer[b:];image=Image.open(io.BytesIO(data));image.load();frames.append({'bytes':len(data),'size':image.size,'seconds':time.monotonic()-start})
assert len(frames)>=3,frames
result={'checks':['ESP32 debug HTTP page','state endpoint','HTTP simulated Uno input appears in debug log','capture JPEG decoded','MJPEG multipart stream decoded'], 'host':args.host,'frames':frames,'cameraVision':state.get('cameraVision'),'firmware':state['firmware'],'elapsedSeconds':time.monotonic()-start}
(root/'http-smoke-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
