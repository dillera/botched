#!/usr/bin/env python3
"""Summarise --capture logs, without sessions or NIO payloads. PAL fields are 20 ms.
Usage: python tools/amiga/replay_metrics.py build/amiga/evidence/a500-...
Rendering includes preemption; frame intervals include the foreground loop and
capture I/O. Round wall time includes page fetches and any open overlay.
"""
import json
import pathlib
import re
import statistics
import sys


def distribution(values):
    values = sorted(values)
    if not values:
        return {}
    return dict(count=len(values), min=min(values), median=statistics.median(values),
                p95=values[(len(values) * 95 + 99) // 100 - 1], max=max(values))


def measure(work):
    logs = sorted((work / 'share').glob('*.log'))
    frames, animations, rounds, stacks, memory = [], [], [], [], []
    for path in logs:
        log = path.read_text(errors='replace')
        events = {int(n): int(t) for n, t in re.findall(r'EVENT ordinal=(\d+) type=(\d+)', log)}
        for row in re.findall(r'FRAME ordinal=(\d+) step=(\d+) render_fields=(\d+) interval_fields=(\d+) px=(-?\d+) py=(-?\d+)', log):
            ordinal, step, render, interval, x, y = map(int, row)
            frames.append(dict(file=path.name, ordinal=ordinal, event=events[ordinal],
                               step=step, render_ms=render*20, interval_ms=interval*20, x=x, y=y))
        for row in re.findall(r'ANIM ordinal=(\d+) type=(\d+) frames=(\d+) fields=(\d+) draws=(\d+)', log):
            ordinal, event, intervals, fields, draws = map(int, row)
            animations.append(dict(file=path.name, ordinal=ordinal, event=event,
                                   intervals=intervals, wall_ms=fields*20, draws=draws))
        for rnd, fields in re.findall(r'ROUND round=(\d+) fields=(\d+)', log):
            rounds.append(dict(file=path.name, round=int(rnd), wall_ms=int(fields)*20))
        for main, worker in re.findall(r'EXIT main_stack=(\d+) worker_stack=(\d+)', log):
            stacks.append(dict(file=path.name, main=int(main), worker=int(worker)))
        for chip, other, total in re.findall(r'MEM \w+ chip=(\d+) other=(\d+) total=(\d+)', log):
            memory.append(tuple(map(int, (chip, other, total))))
    return dict(timing_resolution_ms=20, capture_enabled=True, events=len(animations),
                frames=len(frames), rounds=rounds, stacks=stacks,
                minimum_free=dict(zip(('chip','expansion','total'),map(min,zip(*memory)))) if memory else {},
                by_event={str(e):dict(
                    render_ms=distribution([f['render_ms'] for f in frames if f['event']==e]),
                    first_render_ms=distribution([f['render_ms'] for f in frames if f['event']==e and not f['step']]),
                    moving_render_ms=distribution([f['render_ms'] for f in frames if f['event']==e and f['step']]),
                    moving_interval_ms=distribution([f['interval_ms'] for f in frames if f['event']==e and f['step']]),
                    wall_ms=distribution([a['wall_ms'] for a in animations if a['event']==e]))
                    for e in sorted({a['event'] for a in animations})},
                frame_samples=frames, animation_samples=animations)


if __name__ == '__main__':
    print(json.dumps(measure(pathlib.Path(sys.argv[1])), indent=2))
