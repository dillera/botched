#!/usr/bin/env python3
"""S1 executable startup + real JSON/binary TCP integration. No server mocks."""
import copy,json,pathlib,re,socket,subprocess,sys,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
SERVER,CLIENT=map(pathlib.Path,sys.argv[1:3])
def wait(fn,seconds=30):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.03)
    raise AssertionError('timed out')
class JsonPeer:
    def __init__(self,port):
        self.socket=socket.create_connection(('127.0.0.1',port));self.socket.settimeout(10);self.stream=self.socket.makefile('rwb')
    def call(self,kind,**fields):
        self.stream.write(json.dumps(dict(type=kind,**fields)).encode()+b'\n');self.stream.flush()
        return json.loads(self.stream.readline())
    def close(self):self.stream.close();self.socket.close()
with tempfile.TemporaryDirectory(prefix='botched-s1-') as td:
    root=pathlib.Path(td);maps=root/'maps';maps.mkdir()
    for f in (ROOT/'resources/maps').glob('*.map'):(maps/f.name).write_bytes(f.read_bytes())
    text=(maps/'training-01.map').read_text()
    rows='\n'.join(line for line in text.splitlines() if line and not line.startswith(';'))
    for token in ('S3','D3','S4','D4'):rows=rows.replace(token,'.')
    (maps/'duel-only.map').write_text('; id: duel-only\n; name: Two seats only\n; players: 2\n'+rows+'\n')
    config=json.loads((ROOT/'docs/examples/server-pool.json').read_text())
    config['server']['max_concurrent_matches']=3
    config['server']['default_planning_seconds']=999
    named,human=config['offerings']
    named.update(instances=1,rounds=3,target_score=0,planning_seconds=0)
    human.update(instances=2,map_id='duel-only',rounds=2,planning_seconds=0)
    del human['player_count']
    path=root/'pool.json';path.write_text(json.dumps(config))
    out=open(root/'stdout','w+');err=open(root/'stderr','w+')
    proc=subprocess.Popen([SERVER,'--port','0','--binary-port','0','--maps-dir',maps,'--planning-seconds','0','--config',path,'--max-matches','4','--bot-log'],stdout=out,stderr=err)
    clients=[];peer=None
    def output():return (root/'stdout').read_text()
    try:
        wait(lambda:'binary wire v1 listening' in output())
        report=[json.loads(l[5:]) for l in output().splitlines() if l.startswith('pool {')]
        assert [r['match_id'] for r in report]==['m1','m2','m3'],report
        assert report[0]['bot_names']==['Nasty','Sneaky Pete','Bob']
        assert report[1]['player_count']==2 and report[2]['player_count']==2
        assert 'max_concurrent_matches=4' in output() and '(planning 0 s' in output()
        jp=int(re.search(r'listening on 127.0.0.1:(\d+)',output())[1])
        bp=int(re.search(r'binary wire v1 listening on 127.0.0.1:(\d+)',output())[1])
        peer=JsonPeer(jp)
        assert peer.call('GET_MATCH_STATUS',match_id='m1')['status']['phase']=='LOBBY'
        joined=peer.call('JOIN_MATCH',match_id='m2');assert not joined['started']
        assert peer.call('GET_MATCH_STATUS',match_id='m2')['status']['phase']=='LOBBY'
        assert not peer.call('START_MATCH',match_id='m2')['ok']
        def linux(match):
            log=open(root/(match+'.log'),'w+')
            p=subprocess.Popen([CLIENT,'--wire','binary','--port',str(bp),'--join',match,'--bot','--think-ms','0','--plain'],stdout=log,stderr=log)
            clients.append((p,log));return p
        mixed=linux('m2')
        seen=set()
        def play():
            response=peer.call('GET_SNAPSHOT',match_id='m2')
            if not response.get('ok'):return False
            s=response['snapshot']
            if s['phase']=='FINISHED':return True
            if s['round'] not in seen:
                r=peer.call('SUBMIT_PROGRAM',match_id='m2',player_id=joined['player_id'],session=joined['session'],round=s['round'],revision=1,orders=['WAIT']*3,ready=True,request_id='r'+str(s['round']))
                assert r['ok'],r;seen.add(s['round'])
            return False
        wait(play);assert mixed.wait(timeout=10)==0
        botmatch=linux('m1');assert botmatch.wait(timeout=40)==0,(root/'m1.log').read_text()
        status=peer.call('GET_MATCH_STATUS',match_id='m1')['status'];assert status['phase']=='FINISHED' and status['bots']==3
        assert status['bot_players']==[2,3,4]
        assert all(k in ['hauler','opportunist','saboteur'] for k in status['bot_personalities'])
        assert 'Sneaky Pete' not in json.dumps(status) and 'blend' not in json.dumps(status)
        assert peer.call('GET_MATCH_STATUS',match_id='m3')['status']['phase']=='LOBBY'
        assert peer.call('CREATE_MATCH')['match_id']=='m4'
        assert peer.call('CREATE_MATCH')['code']=='TOO_MANY_MATCHES'
        assert peer.call('GET_MATCH_STATUS',match_id='m1')['status']==status
        log=(root/'stderr').read_text()
        assert 'P2 name="Nasty"' in log and 'P3 name="Sneaky Pete"' in log and '200000/300000/500000' in log
    finally:
        if peer:peer.close()
        for p,log in clients:
            if p.poll() is None:p.terminate();p.wait(timeout=5)
            log.close()
        proc.terminate();proc.wait(timeout=5);out.close();err.close()
    # Invalid declarations after a valid first offering must produce no pool or listener.
    for mode in ('unknown-map','capacity','incompatible-count','missing-count','bad-cli'):
        bad=copy.deepcopy(config);args=[]
        if mode=='unknown-map':bad['offerings'][1]['map_id']='missing'
        elif mode=='capacity':bad['server']['max_concurrent_matches']=2
        elif mode=='incompatible-count':bad['offerings'][1]['player_count']=4
        elif mode=='missing-count':bad['offerings'][1]['map_id']='training-01'
        elif mode=='bad-cli':args=['--planning-seconds','junk']
        path.write_text(json.dumps(bad))
        p=subprocess.run([SERVER,'--config',path,'--maps-dir',maps,'--port','0',*args],capture_output=True,text=True,timeout=10)
        assert p.returncode==2,(mode,p.stdout,p.stderr)
        assert 'pool {' not in p.stdout and 'listening' not in p.stdout and 'config:' in p.stderr,(mode,p.stdout,p.stderr)
print('S1 real TCP: startup report, CLI precedence, two-seat inference, mixed JSON/binary match, named blend match, cap and atomic failure PASS')
