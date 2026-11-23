#!/usr/bin/env python3
"""Actual fn_stream assembly in headless Atari -> existing POSIX NIO over NetSIO -> B3 TCP.
No game renderer, firmware edits, patched library or mocked stream operations.
"""
import json,os,pathlib,re,socket,subprocess,sys,time,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
workspace=root.parent.parent
out=root/'build/atari-spike'
sys.path.insert(0,str(workspace/'repos/nio-config/scripts'))
from atari_ui import symbols
sys.path.insert(0,str(workspace/"integration-tests/atari-sio"))
import run_boot_acceptance as acc
sys.path.insert(0,str(acc.SDK))
from altirra_bridge import AltirraBridge
server=subprocess.Popen([str(root/'build/botched-tcp-server'),'--port','0','--binary-port','0'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    jp=int(re.search(r'127\.0\.0\.1:(\d+)',server.stdout.readline())[1])
    bp=int(re.search(r'127\.0\.0\.1:(\d+)',server.stdout.readline())[1])
    sock=socket.create_connection(('127.0.0.1',jp),timeout=10);stream=sock.makefile('rwb')
    def ask(j):
        stream.write(json.dumps(j).encode()+b'\n');stream.flush();return json.loads(stream.readline())
    made=ask(dict(type='CREATE_MATCH',map_id='foundry-01',player_count=4,bots=3,rounds=3,planning_seconds=0))
    assert made['ok'],made
    (out/'endpoint.h').write_text(f'#define SPIKE_URL "tcp://127.0.0.1:{bp}"\n#define SPIKE_MATCH "{made["match_id"]}"\n')
    lib=workspace/'repos/fujinet-nio-lib'
    def run(cmd): subprocess.run(list(map(str,cmd)),cwd=root,check=True)
    for src,name in [('prototypes/atari/thin.c','thin-atari'),('prototypes/atari/stream.c','stream-atari'),('tests/atari-spike/guest.c','guest')]:
        run(['cl65','-t','atari','--standard','c99','-O','-DTH_ASM','-I','prototypes/atari','-I',out,'-I',lib/'include','-c','-o',out/(name+'.o'),src])
    run(['cl65','-t','atari','-c','-o',out/'fast.o','prototypes/atari/fast.s'])
    run(['cl65','-t','atari','-c','-o',out/'stack.o','tests/b5/stack.s'])
    run(['cl65','-t','atari','-Wl,-D,__STACKSIZE__=512','-m',out/'guest.map','-Ln',out/'guest.lbl','-o',out/'guest.xex',out/'guest.o',out/'thin-atari.o',out/'stream-atari.o',out/'fast.o',out/'stack.o',lib/'build/fujinet-nio-atari-compact.lib'])
    labels=symbols(out/'guest.lbl')
    began=time.monotonic();history=[]
    def inspect(token,run_dir):
      with AltirraBridge.from_token_file(token) as bridge:
        class View: pass
        view=View();view.bridge=bridge
        until=time.monotonic()+180
        while time.monotonic()<until:
            stage=view.bridge.peek(labels['_guest_stage'],1)[0]
            sample=(stage,view.bridge.peek(labels['_guest_round'],1)[0])
            if not history or history[-1][1:]!=list(sample):history.append([round(time.monotonic()-began,3),*sample])
            if stage==128:break
            if stage&64:
                raise RuntimeError(f'guest failure stage={stage} error={view.bridge.peek(labels["_guest_error"],1)[0]}')
            time.sleep(.1)
        else: raise TimeoutError(history)
        result=dict(seconds=time.monotonic()-began,history=history,
                    pages=view.bridge.peek16(labels['_guest_pages']),events=view.bridge.peek16(labels['_guest_events']),
                    software_stack=view.bridge.peek16(labels['_guest_sw']),hardware_stack=view.bridge.peek16(labels['_guest_hw']),
                    ticks=[view.bridge.peek16(labels['_guest_marks']+2*i) for i in range(7)])
        assert result['software_stack']<512,result
        status=ask(dict(type='GET_MATCH_STATUS',match_id=made['match_id']))
        assert status['status']['phase']=='FINISHED',status
        result['status']=status
        (out/'real.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result))
        return result
    # Never displace another emulator or NetSIO service using the shared ports.
    for port in (9996,9997):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:probe.bind(('127.0.0.1',port))
    original=acc.altirra_command
    def headless(*args,**kw):
        cmd,env,temps=original(*args,**kw)
        cmd.append('--headless');return cmd,env,temps
    acc.altirra_command=headless
    profile=out/'profile.yaml'
    profile.write_text(re.sub(r'(ram_kb:\s*)\d+',r'\g<1>64',acc.PROFILE.read_text()))
    acc.PROFILE=profile
    os.chdir(workspace)
    acc.run_netsio(4,pathlib.Path(tempfile.mkdtemp(prefix='real-',dir=out)),boot_atr=None,program=out/'guest.xex',inspector=inspect)
finally:
    server.terminate();server.wait(timeout=5)
