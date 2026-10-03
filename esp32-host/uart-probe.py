"""Send only PING/GET_CONFIG over the selected physical UART; never moves motors."""
import argparse,json,pathlib,re,time,urllib.request,sys
parser=argparse.ArgumentParser();parser.add_argument('--host',default='192.168.137.155');parser.add_argument('--count',type=int,default=20);parser.add_argument('--uno-port');parser.add_argument('--baud',type=int,default=9600);parser.add_argument('--ensure-real',action='store_true');args=parser.parse_args()
base='http://'+args.host
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
def state():return json.load(urllib.request.urlopen(base+'/api/state',timeout=5))
def post(payload):
    data=json.dumps(payload).encode()
    urllib.request.urlopen(urllib.request.Request(base+'/api/input',data=data,headers={'Content-Type':'application/json'},method='POST'),timeout=5).read()
def send(command):post({'op':'command','command':command})
initial=state();assert initial['phase'] in ('IDLE','STOPPED') and not initial['debugBusy'],'stop policy before probe'
if initial['mode']!='UART' and args.ensure_real:post({'op':'mode','mode':'UART'});time.sleep(.5);initial=state()
assert initial['mode']=='UART','select Real UART before probe'
observer=None;observed=bytearray()
def sample(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if observer:observed.extend(observer.read(max(1,observer.in_waiting)))
        else:time.sleep(min(.05,max(0,end-time.monotonic())))
if args.uno_port:
    sys.path.insert(0,str(pathlib.Path(__file__).parent/'vendor'/'python'))
    import serial
    observer=serial.Serial(port=None,baudrate=args.baud,timeout=.05)
    observer.dtr=False;observer.rts=False;observer.port=args.uno_port;observer.open()
    sample(1);observed.clear()
start=max((x['seq'] for x in initial['logs']),default=0)
collected={}
def collect():
    current=state()
    assert current['mode']=='UART','mode changed during physical probe'
    for event in current['logs']:
        if event['seq']>start:collected[event['seq']]=event
    return current
print('Sending physical UART PING queries; no motor commands.',flush=True)
for i in range(args.count):
    collect();send('PING');sample(.5);collect()
send('GET_CONFIG');sample(1)
final=collect();events=sorted(collected.values(),key=lambda event:event['seq'])
transmitted=[e for e in events if e['source']=='UNO_TX' and re.search(r' PING\*',e['line'])]
ids={int(re.match(r'C (\d+) ',e['line'])[1]) for e in transmitted}
received=[e for e in events if e['source']=='UNO_RX']
pong=[e for e in received if (m:=re.match(r'DONE (\d+) PONG$',e['line'])) and int(m[1]) in ids]
usb_text=observed.decode('ascii',errors='replace')
usb_pongs=[line for line in usb_text.splitlines() if (m:=re.match(r'DONE (\d+) PONG$',line)) and int(m[1]) in ids]
if observer:observer.close()
result={'host':args.host,'firmware':final['firmware'],'mode':final['mode'],'requestedPings':args.count,'uartTxPings':len(transmitted),'confirmedPongs':len(pong),'unoReplies':received,'unoUsbPort':args.uno_port,'unoUsbObservedPongs':len(usb_pongs),'unoUsbReadOnly':True,'unoUsbText':usb_text[-4000:],'lastUnoAgeMs':final['lastUnoAgeMs'],'sourceBaud':{'ESP32_Arduino_UART':args.baud,'Uno_Serial':args.baud,'ESP32_USB_debug':115200},'physicalMotionIssued':False,'events':events}
pathlib.Path(__file__).with_name('uart-probe-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in result.items() if k not in ('events','unoReplies','unoUsbText')},ensure_ascii=False,indent=2))
assert len(transmitted)==args.count and len(pong)==args.count,'not every physical PING has a matched PONG'
