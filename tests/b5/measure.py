#!/usr/bin/env python3
"""Reproducible frozen-codec cc65/sim65 acceptance. No firmware edits or downloads."""
import argparse
import hashlib
import os
import json
import pathlib
import re
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--out', type=pathlib.Path, default=ROOT / 'build/b5')
p.add_argument('--nio-library', type=pathlib.Path,
               default=ROOT.parent.parent/'repos/fujinet-nio-lib/build/fujinet-nio-atari-compact.lib')
args = p.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
def run(cmd):
    result = subprocess.run(list(map(str, cmd)), cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(str(cmd) + "\n" + result.stdout + result.stderr)
    return result.stdout

def rec(tag, b):
    return bytes([tag, len(b)]) + b

def pages(records, origin, high, payload=240):
    total = sum(map(len, records)); offset = 0; chunks = []; chunk = b''
    for r in records:
        assert len(r) <= payload - 22
        if len(chunk) + len(r) > payload - 22:
            chunks.append(chunk); chunk = b''
        chunk += r
    chunks.append(chunk)
    result = []
    for chunk in chunks:
        final = offset + len(chunk) == total
        result.append(struct.pack('<BHIIIIBBB', 0x82, len(result)+1, 1, offset, total, high,
                                  origin, int(final), len(chunk)) + chunk)
        offset += len(chunk)
    return result

def array(name, b):
    return f'static const unsigned char {name}[] = {{' + ','.join(map(str, b)) + '};\n'

examples = json.loads((ROOT / 'docs/binary-v1/fixtures.json').read_text())['examples']
valid = [e for e in examples if e['kind'] in ('record', 'payload')]
fixture = {e['name']: bytes.fromhex(e['hex']) for e in valid}
# Four ranked rows, full u32 counters: the largest legal event (68 bytes).
large = rec(21, bytes([4]) + struct.pack('<I', 1) + bytes([1, 3]) + struct.pack('<I', 1)
            + bytes([15, 2, 4]) + b''.join(bytes([i])+struct.pack('<III',1000000,1000000,1000000) for i in range(1,5)))
assert len(large) == 68
full = pages([large]*3 + [fixture['EVENT-PLANNING_CLOSED']], 0x16, 1)[0]
assert len(full) == 240
valid += [dict(name='EVENT-largest-68', kind='record', hex=large.hex()),
          dict(name='PAGE-full-240', kind='payload', hex=full.hex())]
common = ''.join(array('f'+str(i), bytes.fromhex(e['hex'])) for i,e in enumerate(valid))
common += 'struct input {const char *name; const unsigned char *p; unsigned n; unsigned char kind;};\n'
common += 'static const struct input fixtures[] = {\n' + ',\n'.join(
    '{"%s",f%d,sizeof f%d,%d}' % (e['name'],i,i,e['kind']=='payload') for i,e in enumerate(valid)) + '};\n'
common += '#define FIXTURE_COUNT (sizeof fixtures/sizeof fixtures[0])\n'
common += array('submit', fixture['SUBMIT-numeric']) + array('full_page', full)

# Immutable map census: blocked are wall, pit, emitter, trap, tar generator, turret.
# Attribute-bearing tokens are all except floor/wall/pit/tar generator.
census=[]
for path in sorted((ROOT/'resources/maps').glob('*.map')):
    rows = [line.split() for line in path.read_text().splitlines() if line.strip() and not line.lstrip().startswith(';')]
    cells = [t for row in rows for t in row]
    walk = [(x,y) for y,row in enumerate(rows) for x,t in enumerate(row)
            if t not in ('#','O','A') and not t.startswith(('L','X','Y'))]
    groups = sorted({t[-1] if len(t)==3 else 'A' for t in cells if t.startswith('C')}
                    | {t[-1] if len(t)==2 else 'A' for t in cells if t.startswith('T')})
    traps = sorted({int(t[1:]) for t in cells if t.startswith('X')})
    turrets = sorted({int(t[1:]) for t in cells if t.startswith('Y')})
    a = sum(t not in ('.','#','O','A') for t in cells)
    census.append(dict(name=path.stem, width=len(rows[0]), height=len(rows), attributes=a,
                       walk=walk, groups=groups, traps=traps, turrets=turrets))
# Independent upper envelope is a reservation bound, not an invented shipped map.
census.append(dict(name='global-envelope',width=20,height=16,attributes=320,
                   walk=[(x,y) for y in range(16) for x in range(20)],
                   groups=list('ABCDEFGHIJKLMNOPQRSTUVWXYZ'),traps=list(range(1,10)),turrets=list(range(1,10))))

versions = {tool: subprocess.run([tool,'--version'], capture_output=True, text=True).stderr.strip() for tool in ('cc65','cl65','sim65')}
(out/'versions.json').write_text(json.dumps(versions,indent=2)+'\n')
print(versions)
for source, name in [('wire/v1.c','wire'),('client/nio.c','nio'),('tests/b5/stack.s','stack')]:
    cmd = ['cl65','-t','sim6502','--standard','c99','-O','-I','include','-c','-o',out/(name+'.o'),source]
    run(cmd)
report=[]
for m in census:
    name=m['name']; directory=out/name; directory.mkdir(exist_ok=True)
    w,h=m['width'],m['height']; c=len(m['walk'])+4; g=len(m['groups']); t=len(m['traps']); u=len(m['turrets'])
    variable=(w*h+1)//2 + m['attributes']*5 + c*7 + g*2 + (t+u)*4
    map_id = 'm'*32 if name == 'global-envelope' else name
    meta=struct.pack('<I',1)+bytes([len(map_id)])+map_id.encode()+bytes([w,h])+struct.pack('<HH',w*h,m['attributes'])
    context = rec(1, bytes([16])+b'm'*16) if name == 'global-envelope' else fixture['CONTEXT']
    records=[context,rec(16,meta)]
    records.append(rec(9,bytes([0,1,0,200])+struct.pack('<HII',0,70000+c,1)+bytes([0,4])+struct.pack('<H',c)+bytes([g,t,u,0,0])+bytes(8)))
    for i in range(4):
        x,y=m['walk'][i]
        records.append(rec(10,bytes([i+1,2,x,y,0,0])+struct.pack('<IIIII',70000+c-4+i,0,0,0,0)+bytes([0])))
    for i in range(c):
        x,y=m['walk'][i] if i<c-4 else (0,0)
        records.append(rec(11,struct.pack('<I',70000+i)+bytes([x,y,0 if i<c-4 else i-(c-4)+1])))
    records += [rec(12,bytes([ord(ch)-65,0])) for ch in m['groups']]
    records += [rec(13,bytes([1,i,1,0])) for i in m['traps']]
    records += [rec(13,bytes([2,i,2,0])) for i in m['turrets']]
    ps=pages(records,0x15,1)
    assert all(22 < len(b) <= 240 for b in ps)
    assert all(22 < len(b) <= 128 for b in pages(records,0x15,1,128))
    if name == 'global-envelope': assert sum(map(len, records)) == 3341
    source=common + ''.join(array('p'+str(i),b) for i,b in enumerate(ps))
    source+='static const struct input pages[] = {'+','.join('{0,p%d,sizeof p%d,1}'%(i,i) for i in range(len(ps)))+'};\n'
    source+=f'#define PAGE_COUNT {len(ps)}\n#define B5_VARIABLE {variable}\n#define TILE_BYTES {(w*h+1)//2}\n#define CRATES {c}\n#define GROUPS {g}\n#define HAZARDS {t+u}\n'
    (directory/'corpus.h').write_text(source)
    run(['cl65','-t','sim6502','--standard','c99','-O','-I','include','-I',directory,
         '-c','-o',directory/'bench.o','tests/b5/bench.c'])
    run(['cl65','-t','sim6502','-Wl,-D,__STACKSIZE__=512','-m',directory/'bench.map','-o',directory/'bench',directory/'bench.o',out/'wire.o',out/'nio.o',out/'stack.o'])
    result=run(['sim65','-c','-x','200000000',directory/'bench'])
    (directory/'result.txt').write_text(result)
    assert int(re.search(r'peak software=(\d+)', result)[1]) < 512
    core_map=(directory/'bench.map').read_text().split('Segment list:')[1].split('Exports list by name:')[0]
    core_sizes={key:int(v,16) for key,v in re.findall(r'^(\w+) +[0-9A-F]+ +[0-9A-F]+ +([0-9A-F]+) +[0-9A-F]+$',core_map,re.M)}
    core_working=sum(core_sizes[key] for key in ('BSS','DATA','ZEROPAGE'))+512+256
    assert core_working <= (8192 if name == 'global-envelope' else 4096)

    print(name, '\n'.join(result.splitlines()[-4:]))
    run([os.environ.get('CC','cc'), '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wvla', '-pedantic-errors',
         '-Iinclude', '-I'+str(directory), 'tests/b5/bench.c', 'tests/b5/allocation.c',
         'wire/v1.c', '-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc', '-Wl,--wrap=free',
         '-o',directory/'host'])
    (directory/'host-result.txt').write_text(run([directory/'host']))
    report.append(dict(name=name, variable=variable, crates=c, core_working_ram=core_working, snapshot_pages=len(ps),
                       snapshot_payload_bytes=sum(map(len,ps)), output=result))
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')

# Link exact production dependency graph with the installed general Atari backend.
# The default 2048-byte software stack is counted, not replaced by a guessed peak.
include = args.nio_library.resolve().parent.parent/'include'
for source,name in [('wire/v1.c','wire-atari'),('client/nio.c','nio-atari'),('client/nio_fujinet.c','binding')]:
    run(['cl65','-t','atari','--standard','c99','-O','-I','include','-I',include,
         '-c','-o',out/(name+'.o'),source])
footprints=[]
for m in report:
    directory=out/m['name']
    run(['cl65','-t','atari','--standard','c99','-O','-DB5_VARIABLE='+str(m['variable']),
         '-I','include','-I',include,'-c','-o',directory/'link.o','tests/b5/link.c'])
    run(['cl65','-t','atari','-m',directory/'atari.map','-o',directory/'atari.xex',
         directory/'link.o',out/'wire-atari.o',out/'nio-atari.o',out/'binding.o',args.nio_library])
    text=(directory/'atari.map').read_text()
    segments=text.split('Segment list:')[1].split('Exports list by name:')[0]
    sizes={k:int(v,16) for k,v in re.findall(r'^(\w+) +[0-9A-F]+ +[0-9A-F]+ +([0-9A-F]+) +[0-9A-F]+$',segments,re.M)}
    # Link contains no allocator entry points, including through the NIO archive.
    assert not re.search(r'\b_(?:malloc|calloc|realloc|free)\b', text)
    working=sum(sizes[k] for k in ('BSS','DATA','ZEROPAGE'))+2048+256
    if m['name'] != 'global-envelope': assert working <= 8192
    resident=sum(sizes[k] for k in ('STARTUP','ONCE','CODE','RODATA'))+working
    footprints.append(dict(name=m['name'], segments=sizes, software_stack_reserved=2048,
                           hardware_stack_reserved=256, working_ram=working, resident_ram=resident,
                           minimum_profile='K4' if working<=4096 else 'K8' if working<=8192 else 'none'))
    print(m['name'], 'complete working RAM',working,'resident RAM',resident)
(out/'footprints.json').write_text(json.dumps(dict(library_sha256=hashlib.sha256(args.nio_library.read_bytes()).hexdigest(),
                                                  maps=footprints),indent=2)+'\n')
run(['cl65','-t','sim6502','--standard','c99','-O','-I','include','-c','-o',out/'nio-c-test.o','tests/nio_c_test.c'])
run(['cl65','-t','sim6502','-o',out/'nio-c-test',out/'nio-c-test.o',out/'nio.o',out/'wire.o'])
print(run(['sim65','-c',out/'nio-c-test']))

# Review artifact; excludes bulky generated byte arrays/maps/logs but preserves
# enough provenance and per-case figures to compare future compiler changes.
summary={'versions':versions, 'cpu':'NMOS 6502, no wait states, cc65 -O',
         'library_sha256':hashlib.sha256(args.nio_library.read_bytes()).hexdigest(),
         'maps':[]}
for r,f in zip(report,footprints):
    timings={}
    for line in r['output'].splitlines():
        match=re.fullmatch(r'(.+) cycles=(\d+) software=(\d+) hardware=(\d+) ok=1',line)
        if match and (match[1].startswith('snapshot-') or match[1] in
                      ('measurement-empty','EVENT-PLANNING_CLOSED','EVENT-largest-68',
                       'PAGE-full-240','SUBMIT-encode','PAGE-full-240-encode')):
            timings[match[1]]=dict(cycles=int(match[2]),software_stack=int(match[3]),hardware_stack=int(match[4]))
    summary['maps'].append({**{k:v for k,v in r.items() if k!='output'}, **f, 'measurements':timings})
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
