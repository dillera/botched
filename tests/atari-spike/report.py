#!/usr/bin/env python3
"""Collect reproducible measurements; run measure.py and real.py first."""
import hashlib,json,pathlib,re,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'build/atari-spike'
workspace=root.parent.parent
m=(out/'guest.map').read_text()
segments={name:int(size,16) for name,size in re.findall(r'^(ZEROPAGE|STARTUP|ONCE|CODE|RODATA|DATA|BSS)\s+[0-9A-F]+\s+[0-9A-F]+\s+([0-9A-F]+)',m,re.M)}
# Allocation-free application, including actual linked transport and runtime.
for forbidden in ('_malloc','_calloc','_realloc','_free','_fn_open','_fn_read','_fn_write'):
    assert not re.search(r'\b'+forbidden+r'\b',m),forbidden
working=sum(segments[n] for n in ('BSS','DATA','ZEROPAGE'))+512+256
resident=sum(segments[n] for n in ('STARTUP','ONCE','CODE','RODATA'))
lib=workspace/'repos/fujinet-nio-lib'
archive=lib/'build/fujinet-nio-atari-compact.lib'
def command(args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT).strip()
# Full maps retained locally; only aggregate data is checked in.
decoder=json.loads((out/'decoder.json').read_text())
submit=re.search(r'submit cycles=(\d+) software=(\d+) hardware=(\d+)',(out/'factory-maximum/asm.txt').read_text())
packet_sizes=[]
for file in (out/'corpus').glob('*.bin'):
    data=file.read_bytes()
    while data:
        n=int.from_bytes(data[:2],'little')
        assert 3<=n<=240 and len(data)>=n+2
        packet_sizes.append(n);data=data[n+2:]
result=dict(max_captured_payload=max(packet_sizes),captured_packets=len(packet_sizes),compiler=command(['cl65','--version']),library_commit=command(['git','-C',str(lib),'rev-parse','HEAD']),
 library_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
 firmware_commit=command(['git','-C',str(workspace/'repos/fujinet-nio'),'rev-parse','HEAD']),
 firmware_sha256=hashlib.sha256((workspace/'repos/fujinet-nio/build/atari-fujibus-netsio-debug/fujinet-nio').read_bytes()).hexdigest(),segments=segments,
 software_stack_reserved=512,hardware_stack_reserved=256,working_ram=working,
 resident_code_constants_startup=resident,total_application_ram=working+resident,
 world=1291,client=478,stream_context=262,stream_adapter_state=9,
 allocator_or_general_nio_linked=False,
 submit=dict(zip(('cycles','software_stack','hardware_stack'),map(int,submit.groups()))),
 decoder=decoder,real_netsio=json.loads((out/'real.json').read_text()))
(out/'measurements.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('decoder','real_netsio')},indent=2))
