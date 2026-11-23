"""Run the real POSIX FujiNet-NIO NetworkDevice, not a mocked TCP service.

The Linux nio-lib serial channel uses a private PTY relayed to NIO's SLIP-over-TCP
channel. Optional reply loss is below NetworkDevice; game TCP remains unmodified.
"""
import argparse
import errno
import hashlib
import os
import pathlib
import pty
import select
import socket
import subprocess
import tempfile
import threading
import time
import tty

parser = argparse.ArgumentParser()
parser.add_argument("--nio", type=pathlib.Path, required=True)
parser.add_argument("--check", type=pathlib.Path, required=True)
parser.add_argument("--drop-read", action="store_true")
parser.add_argument("--drop-write", action="store_true")
args = parser.parse_args()
nio_exe, check_exe = args.nio.resolve(), args.check.resolve()
with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
with tempfile.TemporaryDirectory(prefix="botched-real-nio-") as temporary:
    root = pathlib.Path(temporary)
    (root / "fujinet-data").mkdir()
    (root / "fujinet-data/fujinet.yaml").write_text(f'''fujinet:
  device_name: "botched-b4-acceptance"
wifi:
  enabled: false
netsio:
  enabled: false
channel:
  tcp_host: "127.0.0.1"
  tcp_port: {port}
  uart:
    baud_rate: 115200
    data_bits: 8
    parity: none
    stop_bits: 1
    flow_control: none
''')
    log = (root / "nio.log").open("w+")
    nio = subprocess.Popen([str(nio_exe)], cwd=root, stdout=log, stderr=subprocess.STDOUT)
    master, slave = pty.openpty()
    tty.setraw(master)
    tty.setraw(slave)
    channel = None
    stop = threading.Event()
    stats = {"dropped": 0, "dropped_write": 0, "read_requests": 0, "max_read": 0, "max_write": 0,
             "duplicate_reads": 0, "not_ready": 0, "busy": 0, "max_raw_response": 0}
    failure = []
    try:
        for _ in range(100):
            if nio.poll() is not None:
                break
            try:
                channel = socket.create_connection(("127.0.0.1", port), timeout=.1)
                break
            except OSError:
                time.sleep(.05)
        if channel is None:
            log.seek(0)
            raise RuntimeError("NIO listener failed: " + log.read()[-6000:])
        channel.settimeout(None)
        last_read = None

        def relay():
            global last_read
            incoming, outgoing = bytearray(), bytearray()
            escaped_in = escaped_out = False
            drop_write_pending = False
            try:
                while not stop.is_set():
                    readable, _, _ = select.select([master, channel], [], [], .05)
                    if master in readable:
                        data = os.read(master, 4096)
                        # Observe exact service read request identities, without changing bytes.
                        for byte in data:
                            if byte == 0xC0:
                                if len(outgoing) == 15 and outgoing[:2] == b'\xfd\x02':
                                    identity = bytes(outgoing[7:15])
                                    stats["read_requests"] += 1
                                    stats["max_read"] = max(stats["max_read"], int.from_bytes(outgoing[13:15], "little"))
                                    if identity == last_read:
                                        stats["duplicate_reads"] += 1
                                    last_read = identity
                                if len(outgoing) >= 15 and outgoing[:2] == b'\xfd\x03':
                                    stats["max_write"] = max(stats["max_write"], int.from_bytes(outgoing[13:15], "little"))
                                    # A complete JOIN frame in one bounded Network Write.
                                    if (args.drop_write and not stats["dropped_write"] and len(outgoing) >= 18
                                            and outgoing[17] == 0x12
                                            and int.from_bytes(outgoing[15:17], "little") + 2 == len(outgoing) - 15):
                                        drop_write_pending = True
                                outgoing.clear()
                                escaped_out = False
                            elif escaped_out:
                                outgoing.append({0xDC: 0xC0, 0xDD: 0xDB}.get(byte, byte))
                                escaped_out = False
                            elif byte == 0xDB:
                                escaped_out = True
                            else:
                                outgoing.append(byte)
                        channel.sendall(data)
                    if channel in readable:
                        data = channel.recv(4096)
                        if not data:
                            return
                        for byte in data:
                            if byte == 0xC0:
                                if incoming:
                                    raw = bytes(incoming)
                                    stats["max_raw_response"] = max(stats["max_raw_response"], len(raw))
                                    if len(raw) >= 7 and raw[5] == 1:
                                        stats["not_ready"] += raw[6] == 4
                                        stats["busy"] += raw[6] == 3
                                    # Normal response descriptor 1: one status byte then 12-byte Read prefix.
                                    drop = (args.drop_read and not stats["dropped"] and len(raw) > 19
                                            and raw[0] == 0xFD and (raw[1] & 0x7F) == 2
                                            and raw[5] == 1 and raw[6] == 0
                                            and int.from_bytes(raw[17:19], "little") > 0)
                                    drop_write = (drop_write_pending and raw[0] == 0xFD
                                                  and (raw[1] & 0x7F) == 3)
                                    if drop_write:
                                        stats["dropped_write"] += 1
                                        drop_write_pending = False
                                    elif drop:
                                        stats["dropped"] += 1
                                    else:
                                        framed = b'\xc0' + raw.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc') + b'\xc0'
                                        # Force lower-channel byte fragmentation as well as game paging.
                                        for at in range(0, len(framed), 7):
                                            os.write(master, framed[at:at+7])
                                incoming.clear()
                                escaped_in = False
                            elif escaped_in:
                                incoming.append({0xDC: 0xC0, 0xDD: 0xDB}.get(byte, byte))
                                escaped_in = False
                            elif byte == 0xDB:
                                escaped_in = True
                            else:
                                incoming.append(byte)
            except OSError as error:
                if not stop.is_set() and error.errno != errno.EIO:
                    failure.append(str(error))

        thread = threading.Thread(target=relay)
        thread.start()
        env = dict(os.environ, FN_PORT=os.ttyname(slave), FN_BAUD="115200")
        result = subprocess.run([str(check_exe)], env=env, capture_output=True, text=True, timeout=240)
        print(result.stdout, end="")
        print(result.stderr, end="")
        print("Real NIO channel statistics:", stats)
        print("NIO executable sha256:", hashlib.sha256(nio_exe.read_bytes()).hexdigest())
        if result.returncode:
            log.flush()
            log.seek(0)
            print(log.read()[-12000:])
        assert result.returncode == 0, "real NIO semantic acceptance failed"
        assert not failure, failure
        assert stats["max_read"] <= 32 and stats["max_write"] <= 32, stats
        assert not args.drop_read or stats["dropped"] == 1, stats
        assert not args.drop_read or stats["duplicate_reads"] > 0, stats
        assert not args.drop_write or stats["dropped_write"] == 1, stats
    finally:
        stop.set()
        if "thread" in locals():
            thread.join(timeout=2)
        if channel:
            channel.close()
        os.close(master)
        os.close(slave)
        nio.terminate()
        try:
            nio.wait(timeout=5)
        except subprocess.TimeoutExpired:
            nio.kill()
            nio.wait()
        log.close()
