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
def main(a1c=False, s1=False, a2a=False, a2b=False, a2b2=False):
    a2b = a2b or a2b2
    a2a = a2a or a2b
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
        previews=[]
        if a2a:
            for map_id in ('training-01','laserworks-01','trapworks-01','turretworks-01','tarworks-01','shuntworks-01','crossroads-01'):
                mixed=a2b2 and map_id=='shuntworks-01'
                room=ask(dict(type='CREATE_MATCH',map_id=map_id,player_count=2 if mixed else 4,bots=0 if mixed else 3,rounds=3 if mixed else (2 if a2b and map_id=='trapworks-01' else 1),planning_seconds=0))
                assert room['ok'],room
                previews.append((map_id,room['match_id']))
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
        if a2a:
            script=(share/'run').read_text().replace('Echo >BT:done DONE\n','Echo >BT:games-done DONE\n')
            for i,(map_id,mid) in enumerate(previews):
                script+=f'BT:botched-amiga >BT:preview-{i}.log tcp://127.0.0.1:{bp} {mid} --capture\nIf WARN\nEcho >BT:error ERROR\nEndIf\nAvail >BT:preview-{i}-memory.log\nEcho >BT:preview-{i}-done DONE\n'
            script+='Echo >BT:done DONE\n'
            (share/'run').write_text(script)
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
            'ntsc=false','immediate_blits=false',f'serial_port=tcp://127.0.0.1:{sp}','serial_direct=true','serial_hardware_ctsrts=false',f'filesystem2=rw,DH1:BT:{share},0']
        (work/'machine.json').write_text(json.dumps(settings,indent=2))
        for s in settings:args+=['-s',s]
        start('amiberry',args)
        wait(lambda:listening(sp),60,'Amiberry serial')
        start('bridge',['socat',f'TCP:127.0.0.1:{sp}',f'TCP:127.0.0.1:{np}'])
        print('Guest evidence:',work,flush=True)
        if a1c:
            reliability.exercise(work,share,cases,ask,start,jp,bp,nio,proxy)
            return
        wait(lambda:((share/'client.log').exists() and 'RESULT error=0 map=1 snapshot=1' in (share/'client.log').read_text()) or (share/'done').exists(),180,'snapshot publication')
        assert (share/'state.bin').exists(),(share/'client.log').read_text()
        wait(lambda:(share/'screen.bin').exists() and (share/'screen.bin').stat().st_size==40960,10,'display capture')
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
        mouse_timings=[]
        if a2b:
            def ui_state():
                lines=[line for line in (share/'client.log').read_text().splitlines() if line.startswith('UI ')]
                return dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',lines[-1])) if lines else {}
            for raw,name,field,value in ((0x51,'scores','panel',2),(0x51,'scores-closed','panel',0),
                (0x50,'guide','panel',1),(0x50,'guide-closed','panel',0),
                (0x34,'overview','overview',1),(0x34,'detail','overview',0),(0x17,'inspect','inspect',1)):
                key(raw);wait(lambda:ui_state().get(field)==value,30,'UI '+name)
                ipc('SCREENSHOT',str(work/('ui-'+name+'.png')))
            for y in range(2,13):
                key(0x4d);wait(lambda:ui_state().get('y')==y,30,'inspection row')
            assert ui_state()['cy']==3
            ipc('SCREENSHOT',str(work/'ui-pan-south.png'))
            for y in range(11,-1,-1):
                key(0x4c);wait(lambda:ui_state().get('y')==y,30,'inspection return')
            key(0x17);wait(lambda:ui_state().get('inspect')==0,30,'inspection close')
            def click(x,y):
                target=(x,y)
                # Relative hardware mouse injection works on KS1.3 without a
                # guest tablet driver or host window focus automation.
                # Amiberry clamps individual relative packets; spread motion
                # over emulated frames rather than assuming a 250-count jump.
                x*=2;y*=2 # KS1.3 low-resolution pointer: two counts per pixel
                for _ in range(16):
                    ipc('SEND_MOUSE','-50','-50','0');time.sleep(.06)
                while x or y:
                    dx=min(20,x);dy=min(20,y)
                    ipc('SEND_MOUSE',str(dx),str(dy),'0');time.sleep(.06)
                    x-=dx;y-=dy
                time.sleep(.2)
                count=(share/'client.log').read_text().count('UI ')
                started=time.monotonic()
                ipc('SEND_MOUSE','0','0','1')
                # Observe publication from outside the guest, including queue,
                # drawing, IPC and log propagation. Disabled Lock has no redraw.
                while time.monotonic()-started<1.5:
                    if (share/'client.log').read_text().count('UI ')>count:
                        mouse_timings.append(dict(x=target[0],y=target[1],host_publication_ms=round((time.monotonic()-started)*1000)))
                        break
                    time.sleep(.01)
                time.sleep(.12)
                ipc('SEND_MOUSE','0','0','0');time.sleep(.15)
            # Real Intuition mouse messages, not direct editor calls.
            click(250,214);time.sleep(.5)
            assert not ui_state()['pending'] and 'SUBMIT' not in (share/'client.log').read_text()
            click(245,186) # explicit WAIT into first empty slot
            wait(lambda:ui_state().get('o0')==8,30,'mouse WAIT')
            click(20,214) # occupied first register clears
            wait(lambda:ui_state().get('o0')==255,30,'mouse clear')
            key(0x44) # Enter places explicit WAIT; never submits
            wait(lambda:ui_state().get('o0')==8,30,'Enter places WAIT')
            key(0x41)
            wait(lambda:ui_state().get('o0')==255,30,'Backspace clears')
            click(28,64) # board inspection
            wait(lambda:ui_state().get('inspect')==1,30,'mouse board inspection')
            ipc('SCREENSHOT',str(work/'ui-mouse-inspect.png'))
            ipc('SEND_MOUSE','0','0','2');time.sleep(.15)
            ipc('SEND_MOUSE','0','0','0')
            wait(lambda:ui_state().get('inspect')==0,30,'right mouse inspection close')
        def client_log():return (share/'client.log').read_text()
        for round_number in range(1,4):
            wait(lambda:f'round={round_number} phase=0' in client_log(),120,'planning round')
            # FWD2 / TOGGLE / WAIT. Edit each slot through the real raw-key path.
            if a2b and round_number==1:
                while ui_state().get('choice')!=1:
                    previous=ui_state()['choice'];key(0x4d)
                    wait(lambda:ui_state()['choice']!=previous,30,'browse command')
                key(0x40) # explicitly place FORWARD 2
            elif a2b and round_number==2:
                click(240,74)
            else: key(2)
            key(78);key(8);key(78);key(9)
            ipc('SCREENSHOT',str(work/f'orders-{round_number}.png'))
            if a2b and round_number==2:
                click(250,214);click(250,214)
            else: key(40);key(40) # duplicate Lock while pending must not submit twice
            wait(lambda:f'SUBMIT round={round_number} orders=1,7,8 accepted=1' in client_log(),60,'accepted programme')
            if round_number==1:
                wait(lambda:re.search(r'EVENT ordinal=\d+ type=(8|9) ',client_log()),60,'authoritative movement animation')
                for frame in range(8):
                    ipc('SCREENSHOT',str(work/f'animation-{frame}.png'))
                    time.sleep(.05)
                ipc('SCREENSHOT',str(work/'replay.png'))
                if a2b2:
                    key(0x25);wait(lambda:ui_state().get('panel')==3,30,'history open')
                    ipc('SCREENSHOT',str(work/'caption-history.png'))
                    key(0x4c);key(0x4d)
                    key(0x25);wait(lambda:ui_state().get('panel')==0,30,'history close')
                    def machinery_frame():
                        captions=re.findall(r'CAPTION ordinal=(\d+) phase=(\d+)',client_log())
                        return captions and captions[-1][1]=='2' and re.search(r'FRAME ordinal='+captions[-1][0]+r' step=[1-9] ',client_log())
                    wait(machinery_frame,60,'machinery frame')
                    ipc('SCREENSHOT',str(work/'machinery.png'))
        wait(lambda:re.search(r'^RESULT .* phase=2 ',client_log(),re.M),180,'match completion')
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
            rows=[tuple(map(int,m)) for m in re.findall(r'EVENT ordinal=(\d+) type=(\d+) round=(\d+) tick=(\d+)',log)]
            assert [r[0] for r in rows]==list(range(1,len(authoritative)+1))
            assert [(r[2],r[3]) for r in rows]==[(e['round'],e['tick']) for e in authoritative]
            animations=[tuple(map(int,m)) for m in re.findall(r'ANIM ordinal=(\d+) type=(\d+)',log)]
            assert animations==[r[:2] for r in rows],(len(animations),len(rows))
            if a2b2:
                captions=[int(n) for n in re.findall(r'CAPTION ordinal=(\d+)',log)]
                assert captions==list(range(1,len(authoritative)+1)),captions
                frame_rows=[tuple(map(int,m)) for m in re.findall(r'FRAME ordinal=(\d+) step=(\d+) render_fields=(\d+) interval_fields=(\d+) px=(-?\d+) py=(-?\d+)',log)]
                for ordinal,event in enumerate(authoritative,1):
                    fr=[r for r in frame_rows if r[0]==ordinal]
                    assert fr and [r[1] for r in fr]==list(range(len(fr))),fr
                    if event['type'] in ('ROBOT_MOVED','ROBOT_PUSHED'):
                        p=event['params']
                        assert fr[0][4:]==(p['from_x']*16,p['from_y']*16),fr
                        assert fr[-1][4:]==(p['x']*16,p['y']*16),fr
                        assert len(fr)==(17 if event['type']=='ROBOT_PUSHED' else 13)
                        assert all(abs(b[4]-a[4])+abs(b[5]-a[5])<=2 for a,b in zip(fr,fr[1:])),fr
                assert 'ROUND round=' in log
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
        wait(lambda:re.search(r'^RESULT .* phase=2 ',(share/'client2.log').read_text(),re.M),180,'deadline-only completion')
        second_log=(share/'client2.log').read_text()
        assert 'SUBMIT round=' not in second_log and 'type=1 ' in second_log,second_log
        baseline=int(re.search(r'RESULT .*?cursor=(\d+)',second_log)[1])
        second_events=check_replay(second['match_id'],second_log,baseline)
        (work/'deadline-events.json').write_text(json.dumps(second_events,indent=2))
        assert ask(dict(type='GET_SNAPSHOT',match_id=second['match_id']))['snapshot']['phase']=='FINISHED'
        ipc('SCREENSHOT',str(work/'display-second.png'))
        ipc('SEND_KEY','69','1');time.sleep(.1);ipc('SEND_KEY','69','0')
        if a2a:
            wait(lambda:(share/'games-done').exists(),60,'second clean exit')
            for i,(map_id,mid) in enumerate(previews):
                plog=share/f'preview-{i}.log'
                if a2b2 and map_id=='shuntworks-01':
                    wait(lambda:ask(dict(type='GET_MATCH_STATUS',match_id=mid))['status']['joined']==1,180,'Amiga first seat')
                    peer=ask(dict(type='JOIN_MATCH',match_id=mid))
                    assert peer['ok'] and peer['player_id']==2
                wait(lambda:plog.exists() and 'RESULT error=0 map=1 snapshot=1' in plog.read_text(),180,'map '+map_id)
                # RESULT precedes planar capture; wait for completed display publication.
                time.sleep(1)
                ipc('SCREENSHOT',str(work/f'map-{map_id}.png'))
                if a2b and map_id=='trapworks-01':
                    baseline=int(re.search(r'RESULT .*?cursor=(\d+)',plog.read_text())[1])
                    for rnd,program in ((1,(2,2,5)),(2,(2,9,9))):
                        wait(lambda:f'round={rnd} phase=0' in plog.read_text(),120,'trap planning')
                        for order in program:key(order)
                        key(40)
                        wait(lambda:f'SUBMIT round={rnd} ' in plog.read_text(),60,'trap programme')
                    wait(lambda:re.search(r'^RESULT .* phase=2 ',plog.read_text(),re.M),180,'trap completion')
                    trap_events=check_replay(mid,plog.read_text(),baseline)
                    assert any(e['type']=='TRAP_TRIGGERED' for e in trap_events),trap_events
                    final_traps=ask(dict(type='GET_SNAPSHOT',match_id=mid))['snapshot']['traps']
                    assert all(not t['armed'] for t in final_traps),final_traps
                    (work/'trap-events.json').write_text(json.dumps(trap_events,indent=2))
                    ipc('SCREENSHOT',str(work/'trap-disarmed.png'))
                if a2b2 and map_id=='shuntworks-01':
                    baseline=int(re.search(r'RESULT .*?cursor=(\d+)',plog.read_text())[1])
                    programmes=[((2,2,5),['FORWARD_2','FORWARD_2','TURN_LEFT']),((1,4,1),['FORWARD_2','WAIT','WAIT']),((5,1,9),['WAIT']*3)]
                    for rnd,(keys,orders) in enumerate(programmes,1):
                        wait(lambda:f'round={rnd} phase=0' in plog.read_text(),120,'mixed planning')
                        reply=ask(dict(type='SUBMIT_PROGRAM',match_id=mid,player_id=2,session=peer['session'],round=rnd,revision=1,orders=orders,ready=True,request_id=f'shunt-{rnd}'))
                        assert reply['ok'],reply
                        for raw in keys:key(raw)
                        key(40)
                        wait(lambda:f'SUBMIT round={rnd} ' in plog.read_text(),60,'mixed accepted')
                    wait(lambda:re.search(r'EVENT ordinal=\d+ type=9 ',plog.read_text()),120,'authoritative push')
                    for frame in range(4):
                        ipc('SCREENSHOT',str(work/f'push-{frame}.png'));time.sleep(.15)
                    def shunt_caption_frame():
                        match=re.search(r'CAPTION ordinal=(\d+).*SHUNT',plog.read_text())
                        return match and re.search(r'FRAME ordinal='+match[1]+r' step=1 ',plog.read_text())
                    wait(shunt_caption_frame,60,'SHUNT caption frame')
                    ipc('SCREENSHOT',str(work/'shunt-score.png'))
                    wait(lambda:re.search(r'^RESULT .* phase=2 ',plog.read_text(),re.M),180,'mixed completion')
                    shunt_events=check_replay(mid,plog.read_text(),baseline)
                    assert any(e['type']=='SCORE_AWARDED' and e['params']['reason']=='SHUNT' and e['params']['points']==8 for e in shunt_events)
                    (work/'shunt-events.json').write_text(json.dumps(shunt_events,indent=2))
                    key(0x25);time.sleep(.5)
                    ipc('SCREENSHOT',str(work/'shunt-history.png'))
                    key(0x25) # close history before Escape quits the client
                ipc('SEND_KEY','69','1');time.sleep(.12);ipc('SEND_KEY','69','0')
                wait(lambda:(share/f'preview-{i}-done').exists(),60,'preview exit')
        wait(lambda:(share/'done').exists(),60,'all clean exits')
        # Both instances release their per-process resources; any lazy broker
        # allocation must stop growing after its first use.
        def avail(name):
            rows=re.findall(r'^(chip|fast|total)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',(share/name).read_text(),re.M)
            assert len(rows)==3,rows
            return {k:dict(zip(('available','used','maximum','largest'),map(int,v))) for k,*v in rows}
        before=avail('broker-memory.log');first=avail('first-exit-memory.log');last=avail('exit-memory.log')
        assert abs(last['total']['available']-first['total']['available'])<512,(first,last)
        preview_memory=[avail(f'preview-{i}-memory.log') for i in range(len(previews))]
        for reading in preview_memory:
            assert abs(reading['total']['available']-last['total']['available'])<512,reading
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
        render=[dict(zip(('draws','dirty_cells','full_boards','fields','max_fields','chip_bitplanes','chip_atlas'),map(int,m))) for m in re.findall(r'RENDER draws=(\d+) dirty_cells=(\d+) full_boards=(\d+) fields=(\d+) max_fields=(\d+) chip_bitplanes=(\d+) chip_atlas=(\d+)',client_log)]
        assert render and 1 <= render[0]['full_boards'] < render[0]['draws']//2,render
        result=dict(mouse_timings=mouse_timings,ui_actions=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line)) for line in client_log.splitlines() if line.startswith('UI ')],chip_font_bytes=576,chip_ground_bytes=40960,preview_memory=preview_memory,preview_maps=[p[0] for p in previews],render=render,timing_resolution_ms=20,machine=settings,executable_bytes=(share/'botched-amiga').stat().st_size,
            executable_sha256=hashlib.sha256((share/'botched-amiga').read_bytes()).hexdigest(),
            library_sha256=hashlib.sha256((WS/'repos/fujinet-nio-lib/build/fujinet-nio-amiga.a').read_bytes()).hexdigest(),
            broker_sha256=hashlib.sha256((share/'fujinet-nio.device').read_bytes()).hexdigest(),memory=usage,
            stack=dict(zip(('main_measured','worker_measured','main_reserved','worker_reserved'),map(int,stacks.groups()))),
            chip_installed=524288,expansion_installed=524288,expansion_kind='A500 slow RAM (no true Fast RAM)',
            avail_before=before,avail_after_first=first,avail_after_second=last,
            verification=['two real NIO HELLO/JOIN/MAP/SNAPSHOT instances','authoritative player positions/scores','three keyboard/mouse programmes accepted exactly once' if a2b else 'three raw-key programmes accepted exactly once','sequential event playback','server deadline WAIT completion','raw-key quit','clean exit'])
        if a2b2:
            from replay_metrics import measure
            (work/'replay-metrics.json').write_text(json.dumps(measure(work),indent=2)+'\n')
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
    group.add_argument('--a2a',action='store_true',help='visual gameplay plus all nine maps on stock A500')
    group.add_argument('--a2b',action='store_true',help='UI, inspection, palette and all nine maps')
    group.add_argument('--a2b2',action='store_true',help='narration, history and continuous replay timing')
    args=parser.parse_args()
    main(args.a1c,args.s1,args.a2a,args.a2b,args.a2b2)
