#!/usr/bin/env python3
"""Compile and measure the isolated thin decoder, C vs 6502 load and crate-search helpers."""
import json,pathlib,re,struct,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'build/atari-spike';out.mkdir(parents=True,exist_ok=True)
def run(args):
    r=subprocess.run(list(map(str,args)),cwd=root,capture_output=True,text=True)
    if r.returncode:raise RuntimeError(str(args)+'\n'+r.stdout+r.stderr)
    return r.stdout
run(['cmake','--build','build','-j4','--target','botched_atari_spike'])
(out/'host.txt').write_text(run([out.parent/'botched_atari_spike',out/'corpus']))
def frames(data):
    result=[]
    while data:
        n=int.from_bytes(data[:2],'little');assert n<=240 and len(data)>=n+2
        result.append(data[2:2+n]);data=data[n+2:]
    return result
corpus={p.stem:frames(p.read_bytes()) for p in (out/'corpus').glob('*.bin')}
# Use exactly the B5 maximum-capacity factory snapshot for like-for-like cycles.
run(['python3','tests/b5/measure.py','--out',out/'b5'])
h=(out/'b5/factory-01/corpus.h').read_text()
worst=[bytes(map(int,m.split(','))) for m in re.findall(r'unsigned char p\d+\[\] = \{([\d,]+)\}',h)]
assert worst
examples=json.loads((root/'docs/binary-v1/fixtures.json').read_text())['examples']
submit=next(bytes.fromhex(e['hex']) for e in examples if e['name']=='SUBMIT-numeric')
# Reverse-ID moves force the current linear crate lookup to inspect the whole
# full mirror. This is synthetic worst-case work, not a measured normal round.
def record(tag,b):return bytes([tag,len(b)])+b
records=[record(1,b'\x02m1'),record(22,struct.pack('<II',1,96))]
for i in range(95,-1,-1):
    # Coordinates are inside the factory board; no prediction/rules executed.
    b=bytes([16])+struct.pack('<I',2)+bytes([1,1])+struct.pack('<I',96-i)+struct.pack('<I',70000+i)+bytes([1,1,1,1])
    records.append(record(21,b))
chunks=[];part=b''
for rec in records:
    if len(part)+len(rec)>218:chunks.append(part);part=b''
    part+=rec
chunks.append(part);total=sum(map(len,chunks));off=0;events=[]
for i,ch in enumerate(chunks):
    events.append(struct.pack('<BHIIIIBBB',0x82,i+1,2,off,total,2,0x16,int(off+len(ch)==total),len(ch))+ch);off+=len(ch)
cases=[]
for name in ('training-01','factory-01','foundry-01'):
    cases.append((name+'-initial',corpus[name+'-map'],corpus[name+'-initial']))
for name in ('training-01','foundry-01'):
    # Largest captured round by payload, with its actual preceding snapshot.
    key=max((k for k in corpus if k.startswith(name+'-round')),key=lambda k:sum(map(len,corpus[k])))
    round_=int(key.split('round')[1]);prior=name+('-initial' if round_==1 else '-after'+str(round_-1))
    cases.append((key,corpus[name+'-map']+corpus[prior],corpus[key]))
cases += [('factory-maximum',corpus['factory-01-map'],worst),('maximum-crate-moves',corpus['factory-01-map']+worst,events)]
# All thirty event IDs from frozen examples, already bounded. This corpus checks
# that the minimal decoder does not omit hazard events in favor of smaller code.
for e in examples:
    if e['kind']=='record' and e['name'].startswith('EVENT-'):
        b=bytes.fromhex(e['hex']); page=struct.pack('<BHIIIIBBB',0x82,1,2,0,len(b),1,0x16,1,len(b))+b
        cases.append((e['name'],[],[page]))
results=[]
for asm in (False,True):
    mode='asm' if asm else 'c'
    run(['cl65','-t','sim6502','--standard','c99','-O',*(['-DTH_ASM'] if asm else []),'-I','prototypes/atari','-c','-o',out/('thin-'+mode+'.o'),'prototypes/atari/thin.c'])
run(['cl65','-t','sim6502','-c','-o',out/'fast.o','prototypes/atari/fast.s'])
run(['cl65','-t','sim6502','-c','-o',out/'stack.o','tests/b5/stack.s'])
for name,seed,inputs in cases:
    directory=out/name;directory.mkdir(exist_ok=True)
    text='struct packet {const unsigned char *data; unsigned char n;};\n'
    def arrays(label,ps):
        s=''
        for i,p in enumerate(ps):s+=f'static const unsigned char {label}{i}[]={{'+','.join(map(str,p))+'};\n'
        s+='static const struct packet '+label+'[]={'+(','.join('{%s%d,%d}'%(label,i,len(p)) for i,p in enumerate(ps)) or '{0,0}')+'};\n'
        return s
    text+=arrays('seed',seed)+arrays('input',inputs)
    text+=f'#define SEED_COUNT {len(seed)}\n#define INPUT_COUNT {len(inputs)}\n#define CASE_NAME "{name}"\n'
    text+='static const unsigned char orders[]={1,4,7};\nstatic const unsigned char submit[]={'+','.join(map(str,submit))+'};\n'
    (directory/'corpus.h').write_text(text)
    run(['cl65','-t','sim6502','--standard','c99','-O','-I','prototypes/atari','-I',directory,'-c','-o',directory/'bench.o','tests/atari-spike/bench.c'])
    entry={'name':name,'pages':len(inputs),'payload_bytes':sum(map(len,inputs))}
    modes=('c','asm') if name.startswith('EVENT-') else ('c','asm','baseline')
    for mode in modes:
        if mode=='baseline':
            run(['cl65','-t','sim6502','--standard','c99','-O','-DBASELINE','-I','include','-I','prototypes/atari','-I',directory,'-c','-o',directory/'baseline.o','tests/atari-spike/bench.c'])
        run(['cl65','-t','sim6502','-Wl,-D,__STACKSIZE__=512','-m',directory/(mode+'.map'),'-o',directory/mode,directory/('baseline.o' if mode=='baseline' else 'bench.o'),out/('thin-'+('asm' if mode=='baseline' else mode)+'.o'),out/'stack.o',*([out/'fast.o'] if mode!='c' else []),*([out/'b5/wire.o'] if mode=='baseline' else [])])
        log=run(['sim65','-c','-x','200000000',directory/mode]);(directory/(mode+'.txt')).write_text(log)
        m=re.search(r'cycles=(\d+) software=(\d+) hardware=(\d+) world=(\d+) client=(\d+) checksum=(\d+)',log)
        entry[mode]=dict(zip(('cycles','software_stack','hardware_stack','world','client','checksum'),map(int,m.groups())))
        assert entry[mode]['software_stack']<512
    assert entry['c']['checksum']==entry['asm']['checksum']
    results.append(entry)
    print(name,entry['c']['cycles'],entry['asm']['cycles'])
(out/'decoder.json').write_text(json.dumps(results,indent=2)+'\n')
