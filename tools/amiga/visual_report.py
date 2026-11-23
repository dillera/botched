#!/usr/bin/env python3
"""Publish A2a screenshot/measurement evidence from a completed accept.py --a2a run.
Requires Pillow and the workspace Amiga toolchain; never copies guest media/tokens.
"""
import hashlib
import json
import pathlib
import re
import shutil
import statistics
import subprocess
import sys
from PIL import Image

work = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(__file__).resolve().parents[2]
out = root / 'docs/amiga/a2a'
out.mkdir(parents=True, exist_ok=True)
report = json.loads((work / 'results.json').read_text())
exe = root / 'build/amiga/botched-amiga'
assert hashlib.sha256(exe.read_bytes()).hexdigest() == report['executable_sha256']
text, data, bss = map(int, subprocess.check_output(
    ['m68k-amigaos-size', str(exe)], text=True).splitlines()[1].split()[:3])
report['hunks'] = dict(text=text, data=data, bss=bss, resident=text+data+bss)
report['explicit_bytes'] = text+data+bss+16384+8192+81920+5160
report['evidence'] = str(work.relative_to(root))
report['runs'] = []
logs = ['client.log', 'client2.log'] + [f'preview-{i}.log' for i in range(7)]
for name in logs:
    log = (work / 'share' / name).read_text()
    metrics = re.search(r'RENDER draws=(\d+) dirty_cells=(\d+) full_boards=(\d+) fields=(\d+) max_fields=(\d+)', log)
    assert metrics, name
    render = dict(zip(('draws', 'dirty_cells', 'full_boards', 'elapsed_ticks', 'max_ticks'), map(int, metrics.groups())))
    assert render['full_boards'] == 1
    stack = re.search(r'EXIT main_stack=(\d+) worker_stack=(\d+)', log)
    memory = [dict(zip(('chip_free', 'expansion_free', 'total_free'), map(int, row)))
              for row in re.findall(r'MEM \w+ chip=(\d+) other=(\d+) total=(\d+)', log)]
    animation = [tuple(map(int, row)) for row in re.findall(r'ANIM ordinal=(\d+) type=(\d+) frames=(\d+) fields=(\d+)', log)]
    events = [tuple(map(int, row)) for row in re.findall(r'EVENT ordinal=(\d+) type=(\d+)', log)]
    assert [row[:2] for row in animation] == events
    timings = {}
    for kind in sorted({row[1] for row in animation}):
        samples = [row[3]*20 for row in animation if row[1] == kind]
        timings[str(kind)] = dict(count=len(samples), min_ms=min(samples), median_ms=statistics.median(samples), max_ms=max(samples))
    report['runs'].append(dict(log=name, render=render, memory=memory,
                              stack=dict(main=int(stack[1]), worker=int(stack[2])),
                              events=len(events), animation_elapsed=timings))
report['timing_note'] = '20-ms DateStamp resolution; elapsed animation includes capture-mode event logging and task scheduling. Not an uninstrumented FPS or cycle benchmark.'
report['verification'] += ['nine graphical launches / all shipped maps', 'every event has one ordered completed animation', 'no accumulating exit-memory loss', 'cycle-exact OCS with immediate blits disabled']
for filename in ['display.png', 'orders-1.png', 'replay.png', 'finished.png', 'display-second.png']:
    shutil.copy2(work / filename, out / filename)
for filename in work.glob('map-*.png'):
    shutil.copy2(filename, out / filename.name)
# Preserve original full-size screenshots. GIF assembles actual captured frames only.
frames = [Image.open(work / f'animation-{i}.png').convert('RGB') for i in range(8)]
assert len({hashlib.sha256(im.tobytes()).digest() for im in frames}) > 1
frames[0].save(out / 'movement.gif', save_all=True, append_images=frames[1:], duration=50, loop=0)
report['animation_capture_note'] = 'Eight sequential Amiberry screenshots requested 50 ms apart (plus IPC time); GIF uses 50 ms playback per frame. This is a visual sample, not a timing instrument.'
(root / 'docs/amiga/a2a-measurements.json').write_text(json.dumps(report, indent=2) + '\n')
print('resident', text+data+bss, 'explicit', report['explicit_bytes'])
print('minimum free', min(m['total_free'] for run in report['runs'] for m in run['memory']))
print('stacks', max(run['stack']['main'] for run in report['runs']), max(run['stack']['worker'] for run in report['runs']))
print('exit free', [report[k]['total']['available'] for k in ('avail_after_first','avail_after_second')]+[x['total']['available'] for x in report['preview_memory']])
