"""A1c real guest scenarios. Imported by accept.py --a1c; no guest test backdoor.
Linux SDL scripted programmes occupy human seats (no server/client bots).
"""
import hashlib,json,os,pathlib,re,signal,socket,struct,threading,time
from accept import ROOT,WS,port,wait

class Proxy:
    """Drop complete server responses after execution, then break TCP.
    Exact resumed exchanges must match byte for byte; never log recovery secrets.
    """
    def __init__(self,server):
        self.port=port(); self.server=server; self.drop={0x12,0x14}
        self.lost={}; self.retried=[]; self.errors=[]
        self.sock=socket.socket();self.sock.bind(('127.0.0.1',self.port));self.sock.listen()
        self.sock.settimeout(.5);self.stopped=False
        threading.Thread(target=self.accept,daemon=True).start()
    @staticmethod
    def frame(s):
        def take(n):
            b=b''
            while len(b)<n:
                chunk=s.recv(n-len(b))
                if not chunk:raise EOFError()
                b+=chunk
            return b
        h=take(2);return h+take(int.from_bytes(h,'little'))
    def accept(self):
        while not self.stopped:
            try:c,_=self.sock.accept()
            except socket.timeout:continue
            except OSError:return
            threading.Thread(target=self.pipe,args=(c,),daemon=True).start()
    def pipe(self,c):
        try:
            with c,socket.create_connection(('127.0.0.1',self.server)) as s:
                while True:
                    req=self.frame(c);op=req[2]
                    s.sendall(req);reply=self.frame(s)
                    if op in self.lost and op not in self.retried:
                        assert req==self.lost[op], 'changed uncertain request'
                        self.retried.append(op)
                    if op in self.drop:
                        self.drop.remove(op);self.lost[op]=req;return
                    c.sendall(reply)
        except (EOFError,ConnectionError,OSError):pass
        except Exception as e:self.errors.append(str(e))
    def close(self):self.stopped=True;self.sock.close()

def setup(ask):
    cases=[dict(name='target',map_id='training-01',rounds=10,target_score=5,planning_seconds=0),
           dict(name='deadline',map_id='foundry-01',rounds=3,planning_seconds=8),
           dict(name='long',map_id='foundry-01',rounds=12,planning_seconds=5),
           dict(name='stall',map_id='foundry-01',rounds=3,planning_seconds=0),
           dict(name='repeat',map_id='foundry-01',rounds=3,planning_seconds=5)]
    for c in cases:
        reply=ask(dict(type='CREATE_MATCH',player_count=3,bots=0,**{k:v for k,v in c.items() if k!='name'}))
        assert reply['ok'],reply;c['match']=reply['match_id']
    return cases

def startup(cases,port):
    text='FailAt 21\nStack 16384\nAvail >BT:os-memory.log\nBT:fujinet-load-resident BT:fujinet-nio.device fujinet-nio.device\nAvail >BT:broker-memory.log\n'
    for i,c in enumerate(cases):
        text+=f'BT:botched-amiga >BT:client{i}.log tcp://127.0.0.1:{port} {c["match"]} --capture\n'
        text+=f'If WARN\nEcho >BT:error{i} ERROR\nEndIf\nAvail >BT:exit{i}.log\nEcho >BT:done{i} DONE\n'
    return text

EVENT_NAMES='PLANNING_CLOSED ROUND_STARTED ROUND_ENDED MATCH_ENDED ORDER_EXECUTED ORDER_NO_OP ORDER_SKIPPED_DISABLED ROBOT_MOVED ROBOT_PUSHED MOVE_BLOCKED ROBOT_ROTATED CRATE_SPAWNED CRATE_PICKED_UP CRATE_DROPPED CRATE_DELIVERED CRATE_MOVED CRATE_DESTROYED GROUP_TOGGLED LASER_HIT ROBOT_DISABLED RESPAWN_REDIRECTED ROBOT_RESPAWNED TRAP_ARMED TRAP_TRIGGERED TRAP_HIT TURRET_CAPTURED TURRET_CHARGING TURRET_FIRED TAR_SLOWED SCORE_AWARDED'.split()
def exercise(work,share,cases,ask,start,jp,bp,nio,proxy):
    ipc_path=re.search(r'IPC: Listening on (\S+)',(work/'amiberry.log').read_text())[1]
    def ipc(*cmd):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(5);s.connect(ipc_path);s.sendall(('\t'.join(cmd)+'\n').encode());assert s.recv(4096).startswith(b'OK')
    def key(n):
        ipc('SEND_KEY',str(n),'1');time.sleep(.12);ipc('SEND_KEY',str(n),'0');time.sleep(.12)
    reports=[]
    for i,c in enumerate(cases):
        path=share/f'client{i}.log'
        def log():return path.read_text() if path.exists() else ''
        wait(lambda:ask(dict(type='GET_MATCH_STATUS',match_id=c['match']))['status'].get('joined',0)==1,120,'Amiga seat '+c['name'])
        peers=[]
        for wire,p in [('binary',bp),('json',jp)]:
            peers.append(start(f'linux-{i}-{wire}',[ROOT/'build/botched-client','--join',c['match'],'--port',p,'--wire',wire,'--ui','--no-anim','--programs',';'.join(['W W W']*20)],env=dict(os.environ,SDL_VIDEODRIVER='dummy')))
        wait(lambda:'RESULT error=0' in log(),120,'initial snapshot '+c['name'])
        baseline=int(re.search(r'RESULT .*?cursor=(\d+)',log())[1])
        print('Scenario',c['name'],'baseline',baseline,flush=True)
        if c['name']=='target':
            for rnd,orders in [(1,[2,2,6]),(2,[2,1,9])]:
                wait(lambda:f'round={rnd} phase=0' in log(),120,'target round')
                for slot,k in enumerate(orders):
                    key(k)
                    if slot!=2:key(78)
                key(68);key(68)
                wait(lambda:f'SUBMIT round={rnd} ' in log(),90,'accepted target programme')
        if c['name']=='stall':
            os.kill(nio.pid,signal.SIGSTOP)
            try:
                key(19);time.sleep(2);key(69)
                time.sleep(3)
                # A blocked worker must not unload application code/resources.
                assert not (share/f'done{i}').exists()
                ipc('SCREENSHOT',str(work/'pending-close.png'))
            finally:os.kill(nio.pid,signal.SIGCONT)
        else:
            wait(lambda:'phase=2' in log(),420,'completion '+c['name'])
            snap=ask(dict(type='GET_SNAPSHOT',match_id=c['match']))['snapshot']
            ev=ask(dict(type='GET_EVENTS',match_id=c['match'],since_version=baseline))['events']
            types=[int(x) for x in re.findall(r'EVENT ordinal=\d+ type=(\d+)',log())]
            assert types==[EVENT_NAMES.index(e['type'])+1 for e in ev],(c['name'],len(types),len(ev))
            assert snap['phase']=='FINISHED'
            closures=[e for e in ev if e['type']=='PLANNING_CLOSED']
            assert len(closures)==snap['round']
            assert all(e['params']['missing']==([] if c['name']=='target' else [1]) for e in closures),closures
            data=(share/'state.bin').read_bytes()
            for p in snap['players']:
                b=data[195+(p['id']-1)*27:195+p['id']*27]
                if p['x'] is not None:assert list(b[2:4])==[p['x'],p['y']]
                assert struct.unpack_from('<I',b,10)[0]==p['score']
            if c['name']=='target':
                assert snap['round']<c['rounds'] and snap['players'][0]['score']>=5,snap
                assert log().count('SUBMIT round=')==2 and log().count('accepted=1')==2
            else:
                assert snap['round']==c['rounds'] and 'SUBMIT round=' not in log()
            (work/f'{c["name"]}-snapshot.json').write_text(json.dumps(snap,indent=2))
            (work/f'{c["name"]}-events.json').write_text(json.dumps(ev,indent=2))
            ipc('SCREENSHOT',str(work/f'{c["name"]}.png'));key(69)
        wait(lambda:(share/f'done{i}').exists(),120,'worker shutdown '+c['name'])
        assert not (share/f'error{i}').exists(),log()
        assert 'CLOSE confirmed=1' in log(),log()
        for p in peers:p.terminate();p.wait(timeout=10)
        stamps=[tuple(map(int,m)) for m in re.findall(r'RESULT .*?ack=(\d+) cursor=(\d+)',log())]
        assert all(a[0]<=b[0] for a,b in zip(stamps,stamps[1:]))
        memory=[dict(zip(('chip','expansion','total','largest_chip','largest_expansion'),map(int,v))) for v in re.findall(r'MEM \w+ chip=(\d+) other=(\d+) total=(\d+) largest_chip=(\d+) largest_other=(\d+)',log())]
        stack=re.search(r'EXIT main_stack=(\d+) worker_stack=(\d+)',log());assert stack
        report=dict(case=c,event_count=len(re.findall('EVENT ordinal=',log())),memory=memory,stack=list(map(int,stack.groups())),close_wait_frames=int(re.search(r'CLOSE confirmed=1 wait_frames=(\d+)',log())[1]))
        if c['name']=='stall':assert report['close_wait_frames']>20
        assert max(report['stack'])<8192
        reports.append(report)
    assert sorted(proxy.retried)==[0x12,0x14] and not proxy.errors,proxy.errors
    avails=[]
    for i in range(len(cases)):
        rows=re.findall(r'^(chip|fast|total)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',(share/f'exit{i}.log').read_text(),re.M)
        avails.append({k:list(map(int,v)) for k,*v in rows})
    assert max(a['total'][0] for a in avails)-min(a['total'][0] for a in avails)<512,avails
    machine_log=(work/'amiberry.log').read_text()
    assert 'KS ROM v1.3' in machine_log and 'Slow memory' in machine_log
    result=dict(scenarios=reports,post_exit_avail=avails,executable_bytes=(share/'botched-amiga').stat().st_size,
        executable_sha256=hashlib.sha256((share/'botched-amiga').read_bytes()).hexdigest(),
        library_sha256=hashlib.sha256((WS/'repos/fujinet-nio-lib/build/fujinet-nio-amiga.a').read_bytes()).hexdigest(),
        broker_sha256=hashlib.sha256((share/'fujinet-nio.device').read_bytes()).hexdigest(),
        exact_retried_opcodes=proxy.retried,machine=json.loads((work/'machine.json').read_text()))
    (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    (ROOT/'build/amiga/latest-a1c-evidence').write_text(str(work)+'\n')
    print('A1c PASS',work,flush=True)
