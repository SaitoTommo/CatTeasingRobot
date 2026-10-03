"""Check ESP32 D6->D7 direct loopback. Disconnect Uno/level-shifter first.
Marked PING frames are for loopback only, not Uno command execution.
"""
import argparse,collections,json,pathlib,time,urllib.request
parser=argparse.ArgumentParser();parser.add_argument('--host',default='192.168.137.155');parser.add_argument('--count',type=int,default=10);args=parser.parse_args()
base='http://'+args.host
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
def state():return json.load(urllib.request.urlopen(base+'/api/state',timeout=5))
def post(data):
    req=urllib.request.Request(base+'/api/input',data=json.dumps(data).encode(),headers={'Content-Type':'application/json'},method='POST')
    urllib.request.urlopen(req,timeout=5).read()
def crc(text):
    value=0
    for byte in text.encode():
        value^=byte
        for _ in range(8):value=((value<<1)^(7 if value&128 else 0))&255
    return f'{value:02X}'
initial=state();assert initial['phase'] in ('IDLE','STOPPED') and not initial['debugBusy'],'stop policy before loopback'
if initial['mode']!='UART':post({'op':'mode','mode':'UART'});time.sleep(.5)
initial=state();assert initial['mode']=='UART'
start=max(e['seq'] for e in initial['logs']);marker='LOOPBACK_'+str(time.time_ns())
for i in range(args.count):
    post({'op':'command','command':f'PING {marker}_{i}'});time.sleep(.2)
time.sleep(.5);final=state()
events=[e for e in final['logs'] if e['seq']>start]
tx=[e['line'] for e in events if e['source']=='UNO_TX' and marker in e['line']]
rx=[e['line'] for e in events if e['source']=='UART_LOOPBACK' and marker in e['line']]
for line in rx:
    payload,checksum=line.rsplit('*',1);assert crc(payload)==checksum,'returned CRC mismatch'
matched=sum((collections.Counter(tx)&collections.Counter(rx)).values())
result={'host':args.host,'firmware':final['firmware'],'baud':57600,'format':'8N1','rxGpio':44,'txGpio':43,'requestedFrames':args.count,'transmittedFrames':len(tx),'receivedCheckedFrames':len(rx),'exactMatches':matched,'passed':len(tx)==len(rx)==matched==args.count,'unoAcknowledgementClaimed':False,'physicalMotionIssued':False,'mode':final['mode'],'phase':final['phase'],'lastUnoAgeMs':final['lastUnoAgeMs'],'tx':tx,'rx':rx,'badFrames':[e for e in events if e['source']=='UART_BAD_FRAME']}
pathlib.Path(__file__).with_name('uart-loopback-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
assert result['passed'],'loopback incomplete; inspect returned frame counts and wiring'
