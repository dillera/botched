#!/usr/bin/env python3
"""A1b: real stock A500/KS1.3 + existing NIO broker/firmware + Botched server.
Licensed media paths come from workspace local/amiga.env. Only disposable copies
are modified. No accelerated fallback. Run after the Amiga and host builds.
"""
import json,os,pathlib,re,shutil,socket,subprocess,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[2]
WS=ROOT.parent.parent

def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def wait(predicate,seconds,label):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if predicate():return
        time.sleep(.2)
    raise RuntimeError('Timeout: '+label)
def listening(p):return f':{p:04X} ' in pathlib.Path('/proc/net/tcp').read_text()
def main(a1c=False, s1=False):
    out=ROOT/'build/amiga/evidence';out.mkdir(parents=True,exist_ok=True)
    work=pathlib.Path(tempfile.mkdtemp(prefix='a500-',dir=out));share=work/'share';share.mkdir()
    env=json.loads(subprocess.check_output(['bash','-c',
        'set -a; source "$1"; python3 -c \'import os,json; print(json.dumps({k:os.environ.get(k,"") for k in ["AMIGA_WB13_KICKSTART","AMIGA_WB13_ROM_KEY","AMIGA_WB13_ADF_WORKBENCH"]}))\'',
        'bash',str(WS/'local/amiga.env')],text=True))
    (work/'rom').mkdir()
    shutil.copy2(env['AMIGA_WB13_KICKSTART'],work/'rom/kickstart.rom')
    if env['AMIGA_WB13_ROM_KEY']:shutil.copy2(env['AMIGA_WB13_ROM_KEY'],work/'rom/rom.key')
    shutil.copy2(env['AMIGA_WB13_ADF_WORKBENCH'],work/'boot.adf');(work/'boot.adf').chmod(0o600)
    pkg=WS/'build/amiga-artifacts/wb13/NIO'
    for f in ('fujinet-nio.device','fujinet-load-resident'):shutil.copy2(pkg/f,share/f)
    shutil.copy2(ROOT/'build/amiga/botched-amiga',share/'botched-amiga')
    procs=[];logs=[];proxy=None
    def start(name,args,**kw):
        f=(work/(name+'.log')).open('w');logs.append(f)
        p=subprocess.Popen(list(map(str,args)),stdout=f,stderr=subprocess.STDOUT,**kw);procs.append(p);return p
    try:
        jp,bp,np,sp=port(),port(),port(),port()
        server_args=[ROOT/'build/botched-tcp-server','--port',jp,'--binary-port',bp]
        if s1:
            pool=json.loads((ROOT/'docs/examples/server-pool.json').read_text())
            first=pool['offerings'][0]
            first.update(instances=1,rounds=3,target_score=0,planning_seconds=0)
            second=dict(first,id='factory',name='Factory named bots',map_id='factory-01',rounds=2,planning_seconds=8)
            pool['offerings']=[first,second]
            (work/'pool.json').write_text(json.dumps(pool))
            server_args+=['--config',work/'pool.json','--bot-log']
        start('server',server_args)
        wait(lambda:listening(jp),10,'server')
        conn=socket.create_connection(('127.0.0.1',jp));stream=conn.makefile('rwb')
        def ask(data):
            stream.write(json.dumps(data).encode()+b'\n');stream.flush();return json.loads(stream.readline())
        if a1c:
            import reliability
            cases=reliability.setup(ask)
            proxy=reliability.Proxy(bp)
            (share/'run').write_text(reliability.startup(cases,proxy.port))
        else:
            if s1:
                entries=[json.loads(line[5:]) for line in (work/'server.log').read_text().splitlines() if line.startswith('pool {')]
                assert len(entries)==2 and entries[0]['bot_names']==['Nasty','Sneaky Pete','Bob']
                made=dict(ok=True,match_id=entries[0]['match_id'])
            else:
                made=ask(dict(type='CREATE_MATCH',map_id='foundry-01',player_count=4,bots=3,rounds=3,planning_seconds=0))
            assert made['ok'],made
            match=made['match_id']
            second=(dict(ok=True,match_id=entries[1]['match_id']) if s1 else
                    ask(dict(type='CREATE_MATCH',map_id='factory-01',player_count=4,bots=3,rounds=2,planning_seconds=8)))
            assert second['ok'],second
            (share/'run').write_text('FailAt 21\nStack 16384\nAvail >BT:os-memory.log\nBT:fujinet-load-resident BT:fujinet-nio.device fujinet-nio.device\nAvail >BT:broker-memory.log\n'
                +f'BT:botched-amiga >BT:client.log tcp://127.0.0.1:{bp} {match} --capture\n'
                +'If WARN\nEcho >BT:error ERROR\nEndIf\nAvail >BT:first-exit-memory.log\nEcho >BT:first-exited DONE\n'
                +f'BT:botched-amiga >BT:client2.log tcp://127.0.0.1:{bp} {second["match_id"]} --capture\n'
                +'If WARN\nEcho >BT:error ERROR\nEndIf\nAvail >BT:exit-memory.log\nEcho >BT:done DONE\n')
        (work/'startup').write_text('Path C: SYS:System S: ADD\nExecute BT:run\n')
        for cmd in [('delete','s/Startup-Sequence'),('write',str(work/'startup'),'s/Startup-Sequence')]:
            subprocess.run(['xdftool',str(work/'boot.adf'),*cmd],check=True,stdout=subprocess.DEVNULL)
        (work/'nio/fujinet-data').mkdir(parents=True)
        (work/'nio/fujinet-data/fujinet.yaml').write_text(f'''fujinet:
  device_name: "botched-a1"
wifi:
  enabled: false
netsio:
  enabled: false
channel:
  tcp_host: "127.0.0.1"
  tcp_port: {np}
  uart:
    baud_rate: 115200
    data_bits: 8
    parity: none
    stop_bits: 1
    flow_control: none
''')
        nio=start('nio',[WS/'repos/fujinet-nio/build/fujibus-tcp-debug/fujinet-nio'],cwd=work/'nio')
        wait(lambda:listening(np),15,'NIO')
        args=['amiberry','--log','--config',WS/'configs/amiga/base-68000.uae','-G','-r',work/'rom/kickstart.rom','-0',work/'boot.adf']
        settings=['chipset=ocs','cpu_model=68000','cpu_type=68000','chipmem_size=1','bogomem_size=2','fastmem_size=0','z3mem_size=0',
            'ntsc=false',f'serial_port=tcp://127.0.0.1:{sp}','serial_direct=true','serial_hardware_ctsrts=false',f'filesystem2=rw,DH1:BT:{share},0']
        (work/'machine.json').write_text(json.dumps(settings,indent=2))
        for s in settings:args+=['-s',s]
        start('amiberry',args)
        wait(lambda:listening(sp),60,'Amiberry serial')
        start('bridge',['socat',f'TCP:127.0.0.1:{sp}',f'TCP:127.0.0.1:{np}'])
        print('Guest evidence:',work,flush=True)
        if a1c:
            reliability.exercise(work,share,cases,ask,start,jp,bp,nio,proxy)
            return
        wait(lambda:(share/'state.bin').exists() or (share/'done').exists(),180,'snapshot')
        assert (share/'state.bin').exists(),(share/'client.log').read_text()
        wait(lambda:(share/'screen.bin').exists() and (share/'screen.bin').stat().st_size==24000,10,'display capture')
        log=(share/'client.log').read_text();assert 'RESULT error=0 map=1 snapshot=1' in log,log
        snapshot=ask(dict(type='GET_SNAPSHOT',match_id=match))
        (work/'snapshot.json').write_text(json.dumps(snapshot,indent=2))
        data=(share/'state.bin').read_bytes();assert len(data)==303
        # Compare the full wire player bodies against authoritative JSON below.
        from struct import unpack_from
        snap=snapshot['snapshot']
        for p in snap['players']:
            b=data[195+(p['id']-1)*27:195+p['id']*27]
            assert b[0]==p['id']
            assert b[2:4]==bytes([p['x'],p['y']]),(b,p)
            assert unpack_from('<I',b,10)[0]==p['score']
        ipc_path=re.search(r'IPC: Listening on (\S+)',(work/'amiberry.log').read_text())[1]
        def ipc(*cmd):
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(5);s.connect(ipc_path);s.sendall(('\t'.join(cmd)+'\n').encode());reply=s.recv(4096)
                assert reply.startswith(b'OK'),reply
        ipc('SCREENSHOT',str(work/'display.png'))
        def key(n):
            ipc('SEND_KEY',str(n),'1');time.sleep(.12);ipc('SEND_KEY',str(n),'0');time.sleep(.12)
        def client_log():return (share/'client.log').read_text()
        for round_number in range(1,4):
            wait(lambda:f'round={round_number} phase=0' in client_log(),120,'planning round')
            # FWD2 / TOGGLE / WAIT. Edit each slot through the real raw-key path.
            key(2);key(78);key(8);key(78);key(9)
            ipc('SCREENSHOT',str(work/f'orders-{round_number}.png'))
            key(68);key(68) # duplicate Enter while pending must not submit twice
            wait(lambda:f'SUBMIT round={round_number} orders=1,7,8 accepted=1' in client_log(),60,'accepted programme')
            if round_number==1:
                wait(lambda:'EVENT ordinal=' in client_log(),60,'visual event playback')
                ipc('SCREENSHOT',str(work/'replay.png'))
        wait(lambda:'phase=2' in client_log(),180,'match completion')
        log=client_log();assert log.count('SUBMIT round=')==3,log
        assert len(re.findall(r'EVENT ordinal=',log))>30,log
        finished=ask(dict(type='GET_SNAPSHOT',match_id=match))
        assert finished['snapshot']['phase']=='FINISHED',finished
        (work/'finished.json').write_text(json.dumps(finished,indent=2))
        event_names="PLANNING_CLOSED ROUND_STARTED ROUND_ENDED MATCH_ENDED ORDER_EXECUTED ORDER_NO_OP ORDER_SKIPPED_DISABLED ROBOT_MOVED ROBOT_PUSHED MOVE_BLOCKED ROBOT_ROTATED CRATE_SPAWNED CRATE_PICKED_UP CRATE_DROPPED CRATE_DELIVERED CRATE_MOVED CRATE_DESTROYED GROUP_TOGGLED LASER_HIT ROBOT_DISABLED RESPAWN_REDIRECTED ROBOT_RESPAWNED TRAP_ARMED TRAP_TRIGGERED TRAP_HIT TURRET_CAPTURED TURRET_CHARGING TURRET_FIRED TAR_SLOWED SCORE_AWARDED".split()
        def check_replay(match_id,log,baseline):
            authoritative=ask(dict(type='GET_EVENTS',match_id=match_id,since_version=baseline))['events']
            actual=[int(x) for x in re.findall(r'EVENT ordinal=\d+ type=(\d+)',log)]
            assert actual==[event_names.index(e['type'])+1 for e in authoritative],(len(actual),len(authoritative))
            return authoritative
        authoritative=check_replay(match,log,snap['state_version'])
        (work/'events.json').write_text(json.dumps(authoritative,indent=2))
        final_data=(share/'state.bin').read_bytes()
        for p in finished['snapshot']['players']:
            b=final_data[195+(p['id']-1)*27:195+p['id']*27]
            assert bool(b[1]&1)==p['respawn_pending']
            if p['x'] is not None:assert b[2:4]==bytes([p['x'],p['y']])
            assert unpack_from('<I',b,10)[0]==p['score']
        ipc('SCREENSHOT',str(work/'finished.png'))
        ipc('SEND_KEY','69','1');time.sleep(.1);ipc('SEND_KEY','69','0')
        wait(lambda:(share/'first-exited').exists(),60,'first clean exit')
        wait(lambda:(share/'client2.log').exists() and 'RESULT' in (share/'client2.log').read_text(),120,'second join')
        assert 'RESULT error=0 map=1 snapshot=1' in (share/'client2.log').read_text()
        wait(lambda:(share/'state.bin').read_bytes()[:2]!=data[:2],10,'second map')
        wait(lambda:'phase=2' in (share/'client2.log').read_text(),180,'deadline-only completion')
        second_log=(share/'client2.log').read_text()
        assert 'SUBMIT round=' not in second_log and 'type=1 ' in second_log,second_log
        baseline=int(re.search(r'RESULT .*?cursor=(\d+)',second_log)[1])
        second_events=check_replay(second['match_id'],second_log,baseline)
        (work/'deadline-events.json').write_text(json.dumps(second_events,indent=2))
        assert ask(dict(type='GET_SNAPSHOT',match_id=second['match_id']))['snapshot']['phase']=='FINISHED'
        ipc('SCREENSHOT',str(work/'display-second.png'))
        ipc('SEND_KEY','69','1');time.sleep(.1);ipc('SEND_KEY','69','0')
        wait(lambda:(share/'done').exists(),60,'second clean exit')
        # Both instances release their per-process resources; any lazy broker
        # allocation must stop growing after its first use.
        def avail(name):
            rows=re.findall(r'^(chip|fast|total)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',(share/name).read_text(),re.M)
            assert len(rows)==3,rows
            return {k:dict(zip(('available','used','maximum','largest'),map(int,v))) for k,*v in rows}
        before=avail('broker-memory.log');first=avail('first-exit-memory.log');last=avail('exit-memory.log')
        assert abs(last['total']['available']-first['total']['available'])<512,(first,last)
        assert not (share/'error').exists(),(share/'client.log').read_text()
        import hashlib
        client_log=(share/'client.log').read_text()
        usage=[]
        for stage,chip,other,total,lc,lo in re.findall(r'MEM (\w+) chip=(\d+) other=(\d+) total=(\d+) largest_chip=(\d+) largest_other=(\d+)',client_log):
            usage.append(dict(stage=stage,chip_free=int(chip),expansion_free=int(other),total_free=int(total),largest_chip=int(lc),largest_expansion=int(lo)))
        stacks=re.search(r'EXIT main_stack=(\d+) worker_stack=(\d+) reserved_main=(\d+) reserved_worker=(\d+)',client_log)
        assert stacks and int(stacks[1])<16384 and int(stacks[2])<8192
        assert min(m['chip_free'] for m in usage)>24000 and min(m['total_free'] for m in usage)>128000
        machine_log=(work/'amiberry.log').read_text()
        assert 'KS ROM v1.3' in machine_log and 'Slow memory' in machine_log
        result=dict(machine=settings,executable_bytes=(share/'botched-amiga').stat().st_size,
            executable_sha256=hashlib.sha256((share/'botched-amiga').read_bytes()).hexdigest(),
            library_sha256=hashlib.sha256((WS/'repos/fujinet-nio-lib/build/fujinet-nio-amiga.a').read_bytes()).hexdigest(),
            broker_sha256=hashlib.sha256((share/'fujinet-nio.device').read_bytes()).hexdigest(),memory=usage,
            stack=dict(zip(('main_measured','worker_measured','main_reserved','worker_reserved'),map(int,stacks.groups()))),
            chip_installed=524288,expansion_installed=524288,expansion_kind='A500 slow RAM (no true Fast RAM)',
            avail_before=before,avail_after_first=first,avail_after_second=last,
            verification=['two real NIO HELLO/JOIN/MAP/SNAPSHOT instances','authoritative player positions/scores','three raw-key programmes accepted exactly once','sequential event playback','server deadline WAIT completion','raw-key quit','clean exit'])
        (work/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        (ROOT/'build/amiga/latest-evidence').write_text(str(work)+'\n')
        print(client_log);print('PASS',work)
    finally:
        if proxy:proxy.close()
        try:
            ipc_path=re.search(r'IPC: Listening on (\S+)',(work/'amiberry.log').read_text())[1]
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(3);s.connect(ipc_path);s.sendall(('SCREENSHOT\t'+str(work/'exit.png')+'\n').encode());s.recv(4096)
        except (OSError,TypeError):pass
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                try:p.wait(timeout=8)
                except subprocess.TimeoutExpired:p.kill();p.wait()
        for f in logs:f.close()
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--a1c',action='store_true',help='mixed-human reliability scenarios')
    group.add_argument('--s1',action='store_true',help='join startup-provisioned named-bot matches')
    args=parser.parse_args()
    main(args.a1c,args.s1)
