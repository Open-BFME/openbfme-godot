#!/usr/bin/env python3
"""Sixteen independent simulation processes over loopback UDP, not a relay load test.

Requires private local RotWK/BFME2 installations in ROTWK_INSTALL/BFME2_INSTALL.
The map is generated in memory by P16TestUtil.h. No retail files are copied.
Default: 1500 completed ticks (five game minutes at 5 Hz), accelerated, fixed
run-ahead 2, CRC interval 10, scripted humans. This is not real-time/cross-host
performance qualification, nor lobby discovery/JOIN or drop/rejoin coverage.
--relay starts the real relay with ephemeral local tickets, default rate limits
and logging enabled. --paced advances at five ticks per wall-clock second.
ONLINE16-FIX (relay only): --auth gives every slot the lobby's per-slot macKey
(authenticated datagrams, AUTH-1); --drop SLOT:FRAME crashes that slot's process
at the frame and starts its next incarnation with a rejoin ticket (catch-up from
the relay's log, then lockstep); --replay-log replays the relay's log of the game
from frame 0 and compares every frame's hash with peer 0's live ones.
"""

import argparse
import csv
import hashlib
import hmac
import json
import os
from pathlib import Path
import socket
import secrets
import struct
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--ticks", type=int, default=1500)
    parser.add_argument("--timeout", type=int, default=1200, help="total wall-clock seconds")
    parser.add_argument("--relay", type=Path, help="openbfme_relay executable; omit for direct UDP")
    parser.add_argument("--paced", action="store_true", help="pace gameplay at five ticks per second")
    parser.add_argument("--auth", action="store_true", help="authenticated relay datagrams (each slot's macKey)")
    parser.add_argument("--drop", help="SLOT:FRAME: that slot crashes at FRAME and rejoins")
    parser.add_argument("--replay-log", action="store_true", help="replay the relay's log and compare every frame")
    parser.add_argument("--disconnect-ms", type=int, default=3000)
    parser.add_argument("--timeout-ms", type=int, default=6000)
    parser.add_argument("--load-s", type=int, default=180, help="each participant's load deadline")
    parser.add_argument("--allow-registered-stops", action="store_true",
                        help="a game error citing a stop registered in docs/STOPS.md is reported (peer-N.stops), not a failure")
    args = parser.parse_args()
    drop_slot, drop_frame = (-1, -1)
    if args.drop:
        drop_slot, drop_frame = (int(x) for x in args.drop.split(":"))
        if not 0 <= drop_slot <= 15 or not 3 <= drop_frame < args.ticks:
            parser.error("--drop SLOT:FRAME needs a slot 0..15 and a frame inside the game")
    if (args.auth or args.drop or args.replay_log) and not args.relay:
        parser.error("--auth, --drop and --replay-log need --relay")
    if not 30 <= args.ticks <= 18000 or args.timeout <= 0:
        parser.error("ticks must be 30..18000 and timeout must be positive")
    tests = args.tests.resolve(strict=True)
    relay_exe = args.relay.resolve(strict=True) if args.relay else None
    for key in ("ROTWK_INSTALL", "BFME2_INSTALL"):
        if not os.environ.get(key):
            parser.error(f"{key} is required")
    output = args.output.resolve()
    output.mkdir(parents=False, exist_ok=False)
    processes, logs, reservations = [], [], []
    relay = None
    credentials = output / ".private"
    result = {"transport": "real relay" if relay_exe else "loopback UDP", "processes": 16, "ticks": args.ticks,
              "game_seconds": args.ticks / 5, "paced": args.paced,
              "run_ahead": 2, "crc_interval": 10, "auth": args.auth, "drop": args.drop, "passed": False}
    try:
        # Reserve all ports together so none repeats. A later bind race fails visibly.
        for _ in range(17 if relay_exe else 16):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("127.0.0.1", 0))
            reservations.append(sock)
        ports = [sock.getsockname()[1] for sock in reservations]
        port_file = output / "ports.txt"
        port_file.write_text("\n".join(map(str, ports[:16])) + "\n", encoding="ascii")
        for sock in reservations:
            sock.close()
        began = time.monotonic()
        if relay_exe:
            credentials.mkdir(mode=0o700)
            key = secrets.token_bytes(32)
            (credentials / "relay.key").write_text("0:" + key.hex() + "\n", encoding="ascii")
            game_id = secrets.randbits(64)
            expires = int(time.time()) + args.timeout + 180

            def write_ticket(name, body):
                ticket = body + hmac.digest(key, body, "sha256")[:16]
                (credentials / f"{name}.ticket").write_bytes(ticket)
                # the lobby's session_key (ONLINE16-FIX, one scheme with ANTICHEAT-1): HMAC-SHA256(ticket key, "OBFME-SESSION-1" || ticket)[0..16)
                (credentials / f"{name}.mackey").write_bytes(hmac.digest(key, b"OBFME-SESSION-1" + ticket, "sha256")[:16])

            for slot in range(16):
                write_ticket(str(slot), struct.pack("<BBQBHI", 4, 0, game_id, slot, 0xffff, expires))
            if drop_slot >= 0:  # the rejoin ticket (version 5: the wide mask, generation 1)
                write_ticket(f"{drop_slot}-rejoin", struct.pack("<BBQBHIH", 5, 0, game_id, drop_slot, 0xffff, expires, 1))
            (output / "relay-logs").mkdir()
            relay_log = (output / "relay.log").open("wb")
            logs.append(relay_log)
            relay = subprocess.Popen([
                str(relay_exe), "--bind", f"127.0.0.1:{ports[16]}",
                "--keys", str(credentials / "relay.key"), "--log-dir", str(output / "relay-logs"),
                "--status", str(output / "relay-status.json"), "--stats-s", "10"],
                cwd=output, stdout=relay_log, stderr=subprocess.STDOUT)
            while b"listening on" not in (output / "relay.log").read_bytes():
                if relay.poll() is not None or time.monotonic() - began > 10:
                    raise RuntimeError("relay failed to become ready; see relay.log")
                time.sleep(0.05)
        def participant(slot, rejoin=False):
            env = dict(os.environ, OPENBFME_P16_SLOT=str(slot),
                       OPENBFME_P16_TICKS=str(args.ticks),
                       OPENBFME_P16_OUTPUT=str(output), OPENBFME_P16_PORTS=str(port_file),
                       OPENBFME_LOGIC_THREADS="1", OPENBFME_P16_MODE="relay" if relay_exe else "lan",
                       OPENBFME_P16_PACED=str(int(args.paced)), OPENBFME_P16_LOAD_S=str(args.load_s),
                       OPENBFME_P16_ALLOW_STOPS=str(int(args.allow_registered_stops)))
            name = f"{slot}-rejoin" if rejoin else str(slot)
            if relay_exe:
                env.update(OPENBFME_P16_RELAY_PORT=str(ports[16]),
                           OPENBFME_P16_TICKET=str(credentials / f"{name}.ticket"))
                if args.auth:
                    env.update(OPENBFME_P16_MACKEY=str(credentials / f"{name}.mackey"))
                if drop_slot >= 0:
                    env.update(OPENBFME_P16_DROP_SLOT=str(drop_slot), OPENBFME_P16_DISCONNECT_MS=str(args.disconnect_ms),
                               OPENBFME_P16_TIMEOUT_MS=str(args.timeout_ms))
                    if slot == drop_slot and not rejoin:
                        env.update(OPENBFME_P16_VANISH_AT=str(drop_frame))
                    if rejoin:
                        env.update(OPENBFME_P16_REJOIN="1")
            log = (output / f"peer-{name}.log").open("wb")
            logs.append(log)
            return subprocess.Popen(
                [str(tests), "--test-case=p16 net soak: one real UDP participant", "--no-skip=true"],
                cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)

        for slot in range(16):
            processes.append(participant(slot))
        rejoin_started = None
        while True:
            if drop_slot >= 0 and rejoin_started is None and (output / f"peer-{drop_slot}.vanished").exists():
                # the next incarnation starts once the others had time to drop the slot (their disconnect screen, then its timeout)
                vanished = (output / f"peer-{drop_slot}.vanished").stat().st_mtime
                if time.time() - vanished > (args.disconnect_ms + args.timeout_ms) / 1000 + 2:
                    processes.append(participant(drop_slot, rejoin=True))
                    rejoin_started = time.monotonic()
                    result["rejoin_started_after_vanish_s"] = round(time.time() - vanished, 1)
            if relay is not None and relay.poll() is not None:
                raise RuntimeError("relay exited during gameplay; see relay.log")
            statuses = [p.poll() for p in processes]
            failed = [(i, code) for i, code in enumerate(statuses)
                      if code is not None and code != 0]
            if failed:
                raise RuntimeError(f"participant failure(s): {failed}; see peer logs")
            if all(code is not None for code in statuses):
                break
            if time.monotonic() - began > args.timeout:
                raise TimeoutError("total wall-clock deadline exceeded; see peer logs")
            time.sleep(0.1)
        result["exit_codes"] = [p.returncode for p in processes]
        if any(result["exit_codes"]):
            raise RuntimeError(f"participant failure(s): {result['exit_codes']}")
        if drop_slot >= 0 and rejoin_started is None:
            raise RuntimeError("the dropped slot never vanished")
        expected_frames = list(range(2, args.ticks + 2))

        def read_rows(name):
            with (output / f"peer-{name}.csv").open(newline="", encoding="ascii") as stream:
                reader = csv.DictReader(stream)
                if reader.fieldnames != ["frame", "state_hash"]:
                    raise RuntimeError(f"peer {name}: unexpected hash schema")
                rows = [(int(r["frame"]), int(r["state_hash"])) for r in reader]
            if any(not 0 <= h <= 0xFFFFFFFF for _, h in rows):
                raise RuntimeError(f"peer {name}: invalid state hash")
            return rows

        reference = None
        reports = []
        for slot in range(16):
            if slot == drop_slot:
                continue
            rows = read_rows(str(slot))
            if [f for f, _ in rows] != expected_frames:
                raise RuntimeError(f"peer {slot}: missing, duplicated or out-of-order frame")
            if reference is None:
                reference = rows
            elif rows != reference:
                first = next(f for (f, h), (_, other) in zip(rows, reference) if h != other)
                raise RuntimeError(f"peer {slot}: first hash disagreement at frame {first}")
        by_frame = dict(reference)
        if drop_slot >= 0:
            # the dropped slot's two incarnations: frames 2 .. its crash, then its first lockstep frame .. the end, each equal to everyone's
            before, after = read_rows(str(drop_slot)), read_rows(f"{drop_slot}-rejoin")
            if [f for f, _ in before] != list(range(2, before[-1][0] + 1)) or before[-1][0] < drop_frame - 1:
                raise RuntimeError(f"peer {drop_slot}: its frames before the crash are not 2 .. {drop_frame}")
            if [f for f, _ in after] != list(range(after[0][0], args.ticks + 2)):
                raise RuntimeError(f"peer {drop_slot}-rejoin: its lockstep frames do not run on to the end")
            for f, h in before + after:
                if by_frame[f] != h:
                    raise RuntimeError(f"peer {drop_slot}: hash disagreement at frame {f}")
            result["dropped"] = {"slot": drop_slot, "crashed_after_frame": before[-1][0], "rejoined_from_frame": after[0][0],
                                 "frames_compared": len(before) + len(after)}
        for name in [str(s) if s != drop_slot else f"{s}-rejoin" for s in range(16)]:
            report = dict(line.split() for line in (output / f"peer-{name}.txt").read_text().splitlines())
            values = {key: int(value) for key, value in report.items()}
            slot = int(name.split("-")[0])
            crc_ok = values["crc_checks"] == (args.ticks - 2) // 10 if drop_slot < 0 else values["crc_checks"] > 0
            if (values["slot"] != slot or not crc_ok or values["scripted"] <= 0
                    or values["ticks"] != args.ticks + 2 - values["first_frame"]):
                raise RuntimeError(f"peer {name}: incomplete workload/CRC evidence: {values}")
            if bool(values["relay_welcomes"]) != bool(relay_exe):
                raise RuntimeError(f"peer {name}: wrong transport path")
            # a rejoined incarnation refuses what the relay still sealed for its crashed predecessor at the same address until it binds; nothing
            # after its bind may drop, and nobody else drops anything
            after_bind = values["relay_auth_dropped"] - values["relay_auth_dropped_before_bind"]
            if values["relay_auth"] != int(args.auth) or after_bind != 0 or (not name.endswith("rejoin") and values["relay_auth_dropped"] != 0):
                raise RuntimeError(f"peer {name}: authentication {values['relay_auth']} / dropped {values['relay_auth_dropped']}"
                                   f" ({values['relay_auth_dropped_before_bind']} before its bind)")
            if name.endswith("rejoin"):
                result["rejoin_relay_auth_dropped_before_bind"] = values["relay_auth_dropped_before_bind"]
            reports.append(values)
        canonical = "".join(f"{frame},{value}\n" for frame, value in reference).encode("ascii")
        result["registered_stops"] = sorted({line for p in output.glob("peer-*.stops") for line in p.read_text().splitlines()})
        result.update(identical_frames=len(reference), peers=reports,
                      canonical_sha256=hashlib.sha256(canonical).hexdigest(),
                      wall_seconds=time.monotonic() - began)
        if relay is not None:
            relay.terminate()
            result["relay_exit"] = relay.wait(timeout=10)
            if result["relay_exit"] != 0:
                raise RuntimeError("relay shutdown failed")
            status = json.loads((output / "relay-status.json").read_text())
            result["relay_status"] = status
            for key in ("refused", "bad", "slotMismatches", "rateLimited", "logGaps", "logErrors", "invalidPackets", "accountingErrors"):
                if status[key] != 0:
                    raise RuntimeError(f"relay {key}: {status[key]}")
            if status["gamesOpened"] != 1 or status["gamesClosed"] != 1 or status["commandsLogged"] <= 0:
                raise RuntimeError("relay did not record exactly one complete game")
            if status["authDropped"] != 0:
                raise RuntimeError(f"relay authDropped: {status['authDropped']}")
            if drop_slot >= 0 and status["rejoins"] != 1:
                raise RuntimeError(f"relay rejoins: {status['rejoins']}")
            if args.replay_log:
                logs_found = sorted((output / "relay-logs").glob("*.obfmerlog"))
                if len(logs_found) != 1:
                    raise RuntimeError(f"expected one relay log, found {len(logs_found)}")
                env = dict(os.environ, OPENBFME_P16_OUTPUT=str(output), OPENBFME_P16_RELAY_LOG=str(logs_found[0]),
                           OPENBFME_P16_TICKS=str(args.ticks), OPENBFME_LOGIC_THREADS="1")
                with (output / "replay.log").open("wb") as replay_log:
                    code = subprocess.run([str(tests), "--test-case=p16 net soak: the relay log replays with every CRC equal", "--no-skip=true"],
                                          cwd=output, env=env, stdout=replay_log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
                result["replay"] = dict(line.split() for line in (output / "replay.txt").read_text().splitlines()) \
                    if (output / "replay.txt").exists() else None
                if code != 0:
                    raise RuntimeError(f"the relay log replay failed ({code}); see replay.log")
        result["passed"] = True
    except Exception as exc:
        result["error"] = str(exc)
        raise
    finally:
        for sock in reservations:
            sock.close()
        for process in processes:
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        if relay is not None and relay.poll() is None:
            relay.terminate()
            try:
                relay.wait(timeout=10)
            except subprocess.TimeoutExpired:
                relay.kill()
                relay.wait()
        if credentials.exists():
            for path in credentials.iterdir():
                path.unlink()
            credentials.rmdir()
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
