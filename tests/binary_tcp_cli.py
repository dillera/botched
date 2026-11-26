"""B3: installed CLI paths, binary SDL playback and a JSON peer on real sockets."""
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile

server_exe, client_exe = sys.argv[1:]
children = []
try:
    server = subprocess.Popen(
        [server_exe, "--port", "0", "--binary-port", "0"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    children.append(server)
    first, second = server.stdout.readline(), server.stdout.readline()
    json_port = int(re.search(r"127\.0\.0\.1:(\d+)", first)[1])
    binary_port = int(re.search(r"127\.0\.0\.1:(\d+)", second)[1])
    with socket.create_connection(("127.0.0.1", json_port), timeout=10) as sock:
        stream = sock.makefile("rwb")

        def ask(request):
            stream.write(json.dumps(request).encode() + b"\n")
            stream.flush()
            return json.loads(stream.readline())

        # Exercise default JSON as well as explicit --wire json and binary map-list mode.
        for wire in ([], ["--wire", "json"], ["--wire", "binary"]):
            port = binary_port if "binary" in wire else json_port
            result = subprocess.run([client_exe, *wire, "--port", str(port), "--list-maps"],
                                    capture_output=True, text=True, timeout=15)
            assert result.returncode == 0, result.stderr
            assert "foundry-01" in result.stdout

        made = ask({"type": "CREATE_MATCH", "player_count": 3, "bots": 1,
                    "map_id": "foundry-01", "rounds": 2, "planning_seconds": 5})
        assert made["ok"], made
        match = made["match_id"]
        with tempfile.TemporaryDirectory(prefix="botched-b3-") as temp:
            screenshot = pathlib.Path(temp) / "binary-sdl.bmp"
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            binary = subprocess.Popen(
                [client_exe, "--wire", "binary", "--port", str(binary_port), "--join", match,
                 "--ui", "--bot", "--frames", "300", "--screenshot", str(screenshot)],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            children.append(binary)
            peer = subprocess.Popen(
                [client_exe, "--wire", "json", "--port", str(json_port), "--join", match,
                 "--bot", "--plain"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            children.append(peer)
            peer_out, peer_err = peer.communicate(timeout=25)
            binary_out, binary_err = binary.communicate(timeout=25)
            assert peer.returncode == 0, peer_err
            assert binary.returncode == 0, binary_err
            assert "RESULT" in peer_out
            status = ask({"type": "GET_MATCH_STATUS", "match_id": match})["status"]
            assert status["phase"] == "FINISHED", status
            assert status["winners"], status
            assert screenshot.exists() and screenshot.stat().st_size > 10000
            assert screenshot.read_bytes()[:2] == b"BM"
            print("B3 CLI: default/explicit JSON and binary map lists; binary SDL + JSON terminal + bot finished foundry-01; SDL screenshot produced")
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
