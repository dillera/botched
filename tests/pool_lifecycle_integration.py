#!/usr/bin/env python3
"""S2 real listeners: repeated mixed JSON/Linux-binary games at full pool capacity."""
import json
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time

SERVER, CLIENT = map(pathlib.Path, sys.argv[1:3])
ROOT = pathlib.Path(__file__).resolve().parents[1]


def wait(fn, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = fn()
        if result:
            return result
        time.sleep(.02)
    raise AssertionError('timed out')


class Peer:
    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port))
        self.sock.settimeout(5)
        self.stream = self.sock.makefile('rwb')

    def call(self, kind, **fields):
        self.stream.write(json.dumps(dict(type=kind, **fields)).encode() + b'\n')
        self.stream.flush()
        return json.loads(self.stream.readline())

    def close(self):
        self.stream.close()
        self.sock.close()


with tempfile.TemporaryDirectory(prefix='botched-s2-') as td:
    root = pathlib.Path(td)
    config = json.loads((ROOT / 'docs/examples/server-pool.json').read_text())
    config['server'].update(max_concurrent_matches=3, finished_retention_seconds=2)
    named, duel = config['offerings']
    named.update(instances=1, rounds=30, target_score=0, planning_seconds=0)
    duel.update(instances=2, rounds=1, target_score=0, planning_seconds=0)
    config['offerings'] = [duel, named]  # m1 recycling, m2 waiting, m3 active
    path = root / 'pool.json'
    path.write_text(json.dumps(config))
    with open(root / 'server.log', 'w+') as log:
        proc = subprocess.Popen([SERVER, '--config', path, '--port', '0', '--binary-port', '0'],
                                stdout=log, stderr=log)
        peer = None
        children = []
        try:
            def output():
                return (root / 'server.log').read_text()

            def records():
                return [json.loads(line[5:]) for line in output().splitlines()
                        if line.startswith('pool {')]

            wait(lambda: 'binary wire v1 listening' in output())
            jp = int(re.search(r'listening on 127.0.0.1:(\d+)', output())[1])
            bp = int(re.search(r'binary wire v1 listening on 127.0.0.1:(\d+)', output())[1])
            assert len(records()) == 3
            assert 'finished_retention_seconds=2' in output()
            peer = Peer(jp)
            assert not peer.call('JOIN_MATCH', match_id='m2')['started']
            assert peer.call('JOIN_MATCH', match_id='m3')['started']
            active = peer.call('GET_SNAPSHOT', match_id='m3')
            current = 'm1'
            seen_ids = {current, 'm2', 'm3'}
            tokens = set()
            for cycle in range(3):
                human = peer.call('JOIN_MATCH', match_id=current)
                assert human['ok'] and not human['started']
                assert human['session'] not in tokens
                tokens.add(human['session'])
                with open(root / f'client-{cycle}.log', 'w+') as client_log:
                    client = subprocess.Popen([CLIENT, '--wire', 'binary', '--port', str(bp),
                                               '--join', current, '--bot', '--think-ms', '0', '--plain'],
                                              stdout=client_log, stderr=client_log)
                    children.append(client)
                    wait(lambda: peer.call('GET_SNAPSHOT', match_id=current).get('ok'))
                    fields = dict(match_id=current, player_id=human['player_id'], session=human['session'],
                                  round=1, revision=1, orders=['WAIT'] * 3, ready=True, request_id='r1')
                    assert peer.call('SUBMIT_PROGRAM', **fields)['ok']
                    wait(lambda: peer.call('GET_MATCH_STATUS', match_id=current)['status']['phase'] == 'FINISHED')
                    assert client.wait(timeout=10) == 0
                final = peer.call('GET_SNAPSHOT', match_id=current)
                assert final['snapshot']['phase'] == 'FINISHED'
                assert peer.call('GET_EVENTS', match_id=current, since_version=0)['ok']
                assert peer.call('CREATE_MATCH')['code'] == 'TOO_MANY_MATCHES'
                # All sockets for the finished game close, then a new socket can still read results.
                peer.close()
                peer = Peer(jp)
                assert peer.call('GET_SNAPSHOT', match_id=current) == final
                retired = wait(lambda: next((r for r in records() if r.get('retired_match_id') == current), None))
                assert retired['action'] == 'replaced' and retired['offering'] == duel['id']
                assert retired['instance'] == 1 and retired['match_id'] not in seen_ids
                assert peer.call('GET_MATCH_STATUS', match_id=current)['code'] == 'UNKNOWN_MATCH'
                assert peer.call('SUBMIT_PROGRAM', **fields)['code'] == 'UNKNOWN_MATCH'
                current = retired['match_id']
                seen_ids.add(current)
                assert peer.call('GET_MATCH_STATUS', match_id=current)['status']['phase'] == 'LOBBY'
                assert peer.call('CREATE_MATCH')['code'] == 'TOO_MANY_MATCHES'
                assert peer.call('GET_SNAPSHOT', match_id='m3') == active
                assert peer.call('GET_MATCH_STATUS', match_id='m2')['status']['joined'] == 1
            assert len([r for r in records() if r.get('action') == 'replaced']) == 3
        finally:
            if peer:
                peer.close()
            for child in children:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
            proc.terminate()
            proc.wait(timeout=5)
print('S2 real TCP: three completed mixed JSON/binary games, disconnect retention, new IDs/tokens, concurrent isolation and full capacity PASS')
