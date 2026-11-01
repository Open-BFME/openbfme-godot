#!/usr/bin/env python3
# AUTH-1: relay CPU per game for 2 and 16 authenticated players.
# Drives a real openbfme_relay with v5 datagrams (the same k/HKDF/tag the C++ side uses) and samples the relay
# process's CPU from /proc/<pid>/stat while each slot streams DATA at a fixed rate.
import hashlib
import hmac as hmac_mod
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

HMAC = lambda key, msg: hmac_mod.new(key, msg, hashlib.sha256).digest()
KEY = bytes(range(32))
GAME = 0x0123456789ABCDEF


def ticket(slot, mask=0xFFFF, mode=0):
    # v7: u8 ver, u8 keyId, u64 game, u8 slot, u8 maskLow, u8 maskHigh, u8 mode, u32 expiry, u16 frameMs, tag[16]
    body = bytes([7, 1]) + struct.pack("<Q", GAME) + bytes([slot, mask & 0xFF, mask >> 8, mode]) + \
        struct.pack("<IH", 2000000000, 0)
    return body + HMAC(KEY, body)[:16]


def k_of(ticket_bytes):
    return HMAC(KEY, b"OBFME-SESSION-1" + ticket_bytes)[:16]  # ONLINE16-FIX: the session key (RelayAuth::sessionKey)


def dir_key(k, label, slot, gen=0):
    info = label + struct.pack("<Q", GAME) + bytes([slot]) + struct.pack("<H", gen) + b"\x01"
    return HMAC(k, info)


def seal(payload_v4, key, counter):
    d = bytearray(b"BR\x05" + payload_v4[3:4] + struct.pack("<Q", counter) + payload_v4[4:])
    d += hashlib.blake2b(bytes(d), digest_size=16, key=key).digest()
    return bytes(d)


def relay_cpu(pid):
    with open(f"/proc/{pid}/stat") as f:
        parts = f.read().split()
    return (int(parts[13]) + int(parts[14])) / os.sysconf("SC_CLK_TCK")


def run(slots, seconds, pps, size):
    with tempfile.TemporaryDirectory() as td:
        kf = os.path.join(td, "keys.txt")
        with open(kf, "w") as f:
            f.write("1:" + KEY.hex() + "\n")
        st = os.path.join(td, "status.json")
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        relay = subprocess.Popen(
            ["./engine/build/relay/openbfme_relay", "--bind", f"127.0.0.1:{port}", "--keys", kf,
             "--log-dir", td, "--status", st, "--stats-s", "0"])
        time.sleep(0.3)
        clients = []
        for s in range(slots):
            u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            u.bind(("127.0.0.1", 0))
            t = ticket(s)
            c2r = dir_key(k_of(t), b"obfme-relay-c2r", s)
            hello = seal(b"BR\x04\x01" + t + b"\x03", c2r, 0)
            u.sendto(hello, ("127.0.0.1", port))
            u.settimeout(2.0)
            try:
                welcome = u.recv(2048)  # sealed; just needs to arrive
                assert len(welcome) > 40
            except socket.timeout:
                print(f"slot {s}: no WELCOME", file=sys.stderr)
                sys.exit(1)
            clients.append((u, c2r, 1))
        target = 1 % slots  # every slot floods the last one
        pkt = b"\x42" * size
        cpu0, t0 = relay_cpu(relay.pid), time.time()
        deadline = t0 + seconds
        sent = 0
        while time.time() < deadline:
            for i, (u, key, ctr) in enumerate(clients):
                data = seal(b"BR\x04\x03" + bytes([target]) + pkt, key, ctr)
                u.sendto(data, ("127.0.0.1", port))
                clients[i] = (u, key, ctr + 1)
                sent += 1
            time.sleep(1.0 / pps)
        # drain a moment, then read CPU
        time.sleep(0.5)
        cpu1, t1 = relay_cpu(relay.pid), time.time()
        pct = 100.0 * (cpu1 - cpu0) / (t1 - t0)
        relay.terminate()
        relay.wait()
        print(f"{slots} slots x {pps} pkt/s x {size}B payload: {sent} datagrams, relay CPU {pct:.2f}% "
              f"({(cpu1 - cpu0):.3f}s CPU / {(t1 - t0):.1f}s)")


if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.abspath(__file__)) + "/../..")
    for slots, seconds, pps, size in [(2, 8, 60, 128), (16, 8, 60, 128)]:
        run(slots, seconds, pps, size)
