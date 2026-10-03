"""Authorized raised-wheel characterization. USB is read-only; all motion uses bounded ESP tests."""
import json, pathlib, re, statistics, threading, time, urllib.request
import serial

BASE='http://192.168.137.15'
OUT=pathlib.Path(__file__).with_name('pwm-characterize-20261003.json')
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
events=[]; trials=[]; api=[]; stopped=threading.Event()
def state():
    s=json.load(urllib.request.urlopen(BASE+'/api/state',timeout=8))
    assert s['mode']=='UART' and s['phase'] in ('IDLE','STOPPED'),'another controller is active'
    assert s['lastUnoAgeMs']<2500,'Uno link stale'
    assert not s['stopUnconfirmed'],'stop not confirmed'
    api.append(s);return s
def post(body):
    req=urllib.request.Request(BASE+'/api/input',data=json.dumps(body).encode(),headers={'Content-Type':'application/json'},method='POST')
    return urllib.request.urlopen(req,timeout=8).read().decode()
def crc(s):
    c=0
    for b in s.encode('ascii'):
        c^=b
        for _ in range(8):c=((c<<1)^(7 if c&128 else 0))&255
    return c
def save(error=None):
    OUT.write_text(json.dumps({'trials':trials,'events':events,'api':api,'error':error},ensure_ascii=False,indent=2),encoding='utf-8')
port=serial.Serial(port=None,baudrate=57600,timeout=.1)
port.dtr=False;port.rts=False;port.port='COM10'
def reader():
    while not stopped.is_set():
        raw=port.readline()
        if not raw:continue
        line=raw.decode('ascii',errors='replace').strip();payload=line;checked=False
        if '*' in line:
            payload,suffix=line.rsplit('*',1)
            try:checked=int(suffix,16)==crc(payload)
            except (ValueError,UnicodeError):pass
        events.append({'at':time.monotonic(),'line':line,'payload':payload,'checked':checked})
def motion_events(mark):
    return [e for e in events[mark:] if e['checked'] and re.match(r'^(MS|MP|MD|MA) ',e['payload'])]
try:
    initial=state();assert not initial['debugBusy'];assert not initial['config']['frontAvoidanceEnabled']
    port.open();thread=threading.Thread(target=reader,daemon=True);thread.start()
    time.sleep(6);state()
    for pwm in [30,50,70,90,110,130,150]:
        before=state();assert not before['debugBusy'],'previous test still active'
        mark=len(events);start=time.monotonic()
        post({'op':'manual_test','action':'forward','pwm':pwm,'durationMs':2000,'expectedMode':'UART'})
        terminal=None;identifier=None
        while time.monotonic()-start<9:
            replies=motion_events(mark)
            starts=[e for e in replies if e['payload'].startswith('MS ')]
            if starts:identifier=int(starts[0]['payload'].split()[1])
            terminals=[e for e in replies if identifier is not None and re.match(rf'^(MD|MA) {identifier} ',e['payload'])]
            if terminals:terminal=terminals[0];break
            time.sleep(.1)
        trial={'pwm':pwm,'direction':'forward','durationLimitMs':2000,'motorId':identifier,'terminal':terminal}
        trials.append(trial);save()
        if not terminal:raise RuntimeError('no checked motor completion; stop characterization')
        reason=terminal['payload'].split()[2] if terminal['payload'].startswith('MA ') else 'DONE'
        if reason not in ('DONE','ENCODER_STALL'):raise RuntimeError('unexpected motion abort: '+reason)
        active=starts[0];end=terminal['payload'].split();pulses=list(map(int,end[-2:]));elapsed=terminal['at']-active['at']
        trial.update(elapsedSeconds=elapsed,pulses=pulses,meanRpm=[n*3/elapsed for n in pulses])
        progress=[e for e in replies if e['payload'].startswith(f'MP {identifier} ')]
        stable=[e for e in progress if e['at']-active['at']>=.5]
        if len(stable)>=2:
            first,last=stable[0],stable[-1];dt=last['at']-first['at'];a=list(map(int,first['payload'].split()[2:]));b=list(map(int,last['payload'].split()[2:]))
            trial['laterWindowRpm']=[(y-x)*3/dt for x,y in zip(a,b)]
        time.sleep(1.5)
        after=state();deadline=time.monotonic()+5
        while after['debugBusy'] and time.monotonic()<deadline:time.sleep(.5);after=state()
        assert not after['debugBusy'],'test cleanup did not complete'
        assert not after['stopPendingId'],'cleanup stop unconfirmed'
        for e in events[mark:]:
            if e['checked'] and e['payload'].startswith('EVENT DIAG M '):
                assert int(e['payload'].split()[5])==0,'memory diagnostic flagged abnormal state'
        save();print(json.dumps({k:v for k,v in trial.items() if k!='terminal'}),flush=True)
    save()
except Exception as e:
    save(str(e));print('CHARACTERIZATION_STOPPED: '+str(e),flush=True)
    raise
finally:
    try:post({'op':'control','action':'stop','expectedMode':'UART'})
    except Exception:pass
    stopped.set()
    if port.is_open:port.close()
