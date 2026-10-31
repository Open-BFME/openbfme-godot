# OpenBFME tests (lane ANTICHEAT-1): tools/integrity/fog_check.py turns detector D1's report into review findings: only players in the review / alert
# bands, never from an abstained game or one whose re-simulation did not reproduce the players' hashes, in neutral wording, signed as the lobby's private routes are.
from __future__ import annotations

import hashlib
import hmac
import io
import json
import sys
import threading
import unittest
from contextlib import redirect_stdout
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog_check as fc  # noqa: E402
import render_evidence as rev  # noqa: E402


def event(frame: int, reason: str, encounter: int) -> dict:
    return {"frame": frame, "time_s": frame / 5, "command": "MSG_DO_ATTACK_OBJECT", "target_id": 100 + frame, "target_template": "MordorFortress",
            "target_owner_index": 3, "reason": reason, "last_visible_frame": None, "x": 1.0, "y": 2.0, "confidence": 0.97, "encounter": encounter}


def report(**over) -> dict:
    r = {"v": 1, "detector": "D1-orders-into-fog", "detector_version": 1, "game_id": "0123456789abcdef", "status": "ok", "abstain_reason": None,
         "frames": 9000, "crc_parity": "match", "settings": {},
         "players": [
             {"player_index": 2, "slot": 0, "name": "Ana", "band": "alert", "score": 0.9999, "encounters": 3, "strong_encounters": 3,
              "orders_judged": 41, "orders_on_remembered": 2, "orders_stale": 0, "orders_abstained": 0,
              "events": [event(500, "never_seen", 1), event(510, "hidden_unit", 1), event(1500, "never_seen", 2), event(2500, "never_seen", 3)]},
             {"player_index": 3, "slot": 1, "name": "Bo", "band": "review", "score": 0.6, "encounters": 1, "strong_encounters": 0,
              "orders_judged": 12, "orders_on_remembered": 0, "orders_stale": 1, "orders_abstained": 0, "events": [event(700, "hidden_unit", 1)]},
             {"player_index": 4, "slot": 2, "name": "Cy", "band": "none", "score": 0.0, "encounters": 0, "strong_encounters": 0,
              "orders_judged": 30, "orders_on_remembered": 0, "orders_stale": 0, "orders_abstained": 0, "events": []},
         ]}
    r.update(over)
    return r


class FindingsTest(unittest.TestCase):
    def test_review_and_alert_players_become_findings(self):
        items = fc.findings(report())
        self.assertEqual([(i["name"], i["band"], i["slot"]) for i in items], [("Ana", "alert", 0), ("Bo", "review", 1)])
        self.assertEqual(items[0]["game_id"], "0123456789abcdef")
        self.assertEqual(items[0]["evidence"]["events"][0]["frame"], 500)

    def test_summary_is_neutral_and_counts(self):
        s = fc.findings(report())[0]["summary"]
        self.assertEqual(s, "Automated check D1: 4 orders on objects this player could not see (1 hidden unit, 3 never seen) in 3 separate moments "
                            "of the game; 41 orders on enemy objects checked")
        for word in ("cheat", "hack", "guilty", "banned"):
            self.assertNotIn(word, s.lower())

    def test_d7_anomaly_summary(self):
        # online batch r4: a version-2 relay log with authenticated players: the slot whose HELLO opened the game is the finding
        p = {"slot": 0, "band": "review", "encounters": 1, "input": "signed", "signed_datagrams": 1, "plain_datagrams": 0, "bad_tag": 0,
             "anomaly": "a version-2 (previous-release) relay log, yet 2 authenticated player HELLO(s)", "events": []}
        self.assertEqual(fc.summary(p, fc.PACKET_INTEGRITY), "Automated check D7: a version-2 (previous-release) relay log, yet 2 authenticated player "
                         "HELLO(s); the relay refused 0 datagrams sent as this slot; its input was signed (1 signed, 0 plain datagrams)")

    def test_abstained_or_unmatched_games_give_no_finding(self):
        self.assertEqual(fc.findings(report(status="abstained", abstain_reason="crc")), [])
        self.assertEqual(fc.findings(report(crc_parity="mismatch")), [])
        self.assertEqual(fc.findings(report(crc_parity="none")), [])

    def test_other_reports_are_refused(self):
        with self.assertRaises(ValueError):
            fc.findings(report(detector="D9"))
        with self.assertRaises(ValueError):
            fc.findings(report(v=2))


class FixtureTest(unittest.TestCase):
    """The reports openbfme_peer --fog-check wrote for the two recorded Evendim games (engine/tests/test_orders_into_fog_retail.cpp): the lobby
    intake's test data."""

    def load(self, name):
        return json.loads((Path(__file__).resolve().parent / "fixtures" / name).read_text())

    def test_the_cheater_like_game_gives_one_alert_finding(self):
        items = fc.findings(self.load("d1-evendim-alert.json"))
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["band"], "alert")
        self.assertEqual(items[0]["slot"], 0)
        self.assertEqual(items[0]["summary"], "Automated check D1: 6 orders on objects this player could not see (6 never seen) in 3 separate "
                                              "moments of the game; 42 orders on enemy objects checked")
        self.assertEqual({e["target_template"] for e in items[0]["evidence"]["events"]}, {"MordorFortress", "MordorFortressExpansionPadCorner"})

    def test_the_derived_fixtures(self):
        review = fc.findings(self.load("d1-derived-review.json"))
        self.assertEqual([(i["band"], i["evidence"]["events_total"]) for i in review], [("review", 2)])
        for name, why in (("d1-derived-abstained.json", "relay log"), ("d1-derived-crc-mismatch.json", "state hashes")):
            r = self.load(name)
            self.assertEqual(fc.findings(r), [])
            ab = fc.abstentions(r)
            self.assertEqual([(i["band"], i["evidence"], i["summary"]) for i in ab], [("abstained", None, None)])
            self.assertIn(why, ab[0]["abstain_reason"])
        self.assertEqual(fc.abstentions(self.load("d1-evendim-alert.json")), [])

    def test_events_are_capped_and_a_missing_game_id_posts_nothing(self):
        r = report()
        r["players"][0]["events"] = [event(500 + i, "never_seen", 1) for i in range(60)]
        item = fc.findings(r)[0]
        self.assertEqual((len(item["evidence"]["events"]), item["evidence"]["events_total"]), (50, 60))
        self.assertIn("60 orders", item["summary"])
        for e in r["players"][0]["events"]:
            e["target_template"] = "T" * 120
        item = fc.findings(r)[0]
        self.assertLessEqual(len(fc.body_of(item)), 16 * 1024)
        self.assertLess(len(item["evidence"]["events"]), 50)
        self.assertEqual(item["evidence"]["events_total"], 60)
        self.assertEqual(fc.findings(report(game_id=None)), [])
        self.assertEqual(fc.abstentions(report(game_id=None, status="abstained")), [])

    def test_the_d5_and_d6_fixtures(self):
        d5 = fc.findings(self.load("d5-evendim-review.json"))
        self.assertEqual([(i["detector"], i["band"]) for i in d5], [("D5-input-rate", "review")])
        self.assertIn("peak 155 commands", d5[0]["summary"])
        self.assertEqual(set(d5[0]["evidence"]["events"][0]), {"frame", "time_s", "command", "count", "encounter"})
        self.assertLessEqual(set(d5[0]["evidence"]), set(fc.EVIDENCE_ALLOWED))
        d6 = fc.findings(self.load("d6-derived-review.json"))
        self.assertEqual([(i["detector"], i["band"]) for i in d6], [("D6-command-integrity", "review")])
        self.assertEqual(set(d6[0]["evidence"]["events"][0]), {"frame", "time_s", "command", "reason", "target_id", "encounter"})
        self.assertLessEqual(set(d6[0]["evidence"]), set(fc.EVIDENCE_ALLOWED))
        self.assertEqual(d6[0]["summary"], "Automated check D6: 1 command an unmodified client does not send (1 foreign object) in 1 separate moment "
                                           "of the game; 343 commands checked")

    def test_the_clean_game_gives_none(self):
        r = self.load("d1-evendim-clean.json")
        self.assertEqual(r["status"], "ok")
        self.assertEqual(r["crc_parity"], "match")
        self.assertEqual(fc.findings(r), [])


class PostTest(unittest.TestCase):
    def serve(self, codes):
        got = []

        class H(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers["content-length"]))
                got.append((self.path, self.headers["x-openbfme-admin"], body))
                self.send_response(codes.pop(0) if codes else 201)
                self.end_headers()

            def log_message(self, *a):
                pass

        srv = HTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(srv.shutdown)
        return srv, got

    def test_post_signs_each_finding_as_the_lobby_private_routes(self):
        srv, got = self.serve([])
        with tempfile.TemporaryDirectory() as d:
            rp = Path(d) / "r.json"
            rp.write_text(json.dumps(report()))
            kp = Path(d) / "k"
            key = bytes(range(32))
            kp.write_text(key.hex())
            out = io.StringIO()
            with redirect_stdout(out):
                rc = fc.main(["findings", str(rp), "--post", f"http://127.0.0.1:{srv.server_port}/v1/internal/integrity/findings", "--key-file", str(kp)])
        self.assertEqual(rc, 0)
        self.assertEqual(len(got), 2)
        for path, sig, body in got:
            self.assertEqual(path, "/v1/internal/integrity/findings")
            ts, mac = sig.split(".")
            expected = hmac.new(key, f"POST /v1/internal/integrity/findings?\n{ts}\n".encode() + body, hashlib.sha256).hexdigest()
            self.assertEqual(mac, expected)
            self.assertEqual(json.loads(body)["detector"], "D1-orders-into-fog")
        self.assertEqual(len(out.getvalue().splitlines()), 2)

    def test_the_signature_form(self):
        sig = fc.admin_signature("POST", "https://x.example/v1/internal/integrity/findings", b"{}", b"k" * 32, 1700000000)
        mac = hmac.new(b"k" * 32, b"POST /v1/internal/integrity/findings?\n1700000000\n{}", hashlib.sha256).hexdigest()
        self.assertEqual(sig, f"1700000000.{mac}")

    def test_5xx_is_retried_4xx_is_final(self):
        srv, got = self.serve([503, 502, 201])
        url = f"http://127.0.0.1:{srv.server_port}/v1/internal/integrity/findings"
        self.assertEqual(fc.post(url, b"{}", b"k" * 32, sleep=lambda s: None), "")
        self.assertEqual(len(got), 3)
        srv2, got2 = self.serve([409])
        problem = fc.post(f"http://127.0.0.1:{srv2.server_port}/v1/internal/integrity/findings", b"{}", b"k" * 32, sleep=lambda s: None)
        self.assertTrue(problem.startswith("HTTP 409"))
        self.assertEqual(len(got2), 1)
        srv3, got3 = self.serve([200])  # an identical replay: the original receipt
        self.assertEqual(fc.post(f"http://127.0.0.1:{srv3.server_port}/v1/internal/integrity/findings", b"{}", b"k" * 32), "")

    def test_post_without_key_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            rp = Path(d) / "r.json"
            rp.write_text(json.dumps(report()))
            self.assertEqual(fc.main(["findings", str(rp), "--post", "http://127.0.0.1:9/x"]), 1)


FAKE_PEER = """#!/usr/bin/env python3
# a stand-in for openbfme_peer: --relay-log ... --record OUT exits with $FAKE_ASSEMBLE_RC after writing OUT; --replay ... --fog-check OUT writes REPORT
import json, os, sys
a = sys.argv[1:]
open(os.environ["FAKE_ARGS"], "a").write(" ".join(a) + "\\n")
if "--relay-log" in a:
    open(a[a.index("--record") + 1], "wb").write(b"replay")
    sys.exit(int(os.environ.get("FAKE_ASSEMBLE_RC", "0")))
open(a[a.index("--fog-check") + 1], "w").write(os.environ["FAKE_REPORT"])
sys.exit(0)
"""


class RunTest(unittest.TestCase):
    def run_worker(self, assemble_rc: int):
        import os
        import stat
        with tempfile.TemporaryDirectory() as d:
            peer = Path(d) / "peer.py"
            peer.write_text(FAKE_PEER)
            peer.chmod(peer.stat().st_mode | stat.S_IEXEC)
            args = Path(d) / "args.txt"
            out = Path(d) / "report.json"
            env = {"FAKE_ARGS": str(args), "FAKE_REPORT": json.dumps(report()), "FAKE_ASSEMBLE_RC": str(assemble_rc)}
            old = {k: os.environ.get(k) for k in env}
            os.environ.update(env)
            try:
                rc = fc.main(["run", "--peer", str(peer), "--rotwk", "R", "--bfme2", "B", "--relay-log", "g.obfmerlog", "--online-start", "s.json",
                              "--game-id", "0123456789abcdef", "--out", str(out)])
            finally:
                for k, v in old.items():
                    if v is None:
                        os.environ.pop(k, None)
                    else:
                        os.environ[k] = v
            calls = args.read_text().splitlines()
            return rc, (json.loads(out.read_text()) if out.exists() else None), calls

    def test_a_relay_log_is_assembled_then_checked(self):
        rc, rep, calls = self.run_worker(0)
        self.assertEqual(rc, 0)
        self.assertEqual(len(calls), 2)
        self.assertIn("--relay-log g.obfmerlog --online-start s.json --record", calls[0])
        self.assertIn("--fog-check", calls[1])
        self.assertIn("--game-id 0123456789abcdef", calls[1])
        self.assertEqual(rep["status"], "ok")
        self.assertEqual(len(fc.findings(rep)), 2)

    def test_a_desynced_or_incomplete_log_abstains(self):
        for code in (2, 4):
            rc, rep, _ = self.run_worker(code)
            self.assertEqual(rc, 0)
            self.assertEqual(rep["status"], "abstained")
            self.assertIn("relay log", rep["abstain_reason"])
            self.assertEqual({p["band"] for p in rep["players"]}, {"abstained"})
            self.assertEqual(fc.findings(rep), [])

    def test_a_failed_assembly_is_an_error(self):
        rc, rep, _ = self.run_worker(1)
        self.assertEqual(rc, 1)
        self.assertIsNone(rep)


@unittest.skipUnless(__import__("shutil").which("ffmpeg"), "ffmpeg not installed")
class RenderTest(unittest.TestCase):
    def test_a_synthetic_dump_renders_a_picture_and_a_clip(self):
        with tempfile.TemporaryDirectory() as d:
            dump = Path(d) / "dump"
            dump.mkdir()
            w = h = 16
            (dump / "terrain.txt").write_text(f"grid {w} {h} cell 40 origin 0 0\n" + "\n".join(" ".join(str((x * y) % 9) for x in range(w)) for y in range(h)) + "\n")
            for f in range(20, 30):
                rows = ["".join("c" if x < 5 and y < 5 else "f" for x in range(w)) for y in range(h)]
                objs = ["o 1 own 100 100 1 u", "o 2 enemy 500 500 0 s", "o 3 enemy 120 140 1 u", "o 4 ally 60 60 1 u"]
                (dump / f"frame_{f:06d}.txt").write_text(f"frame {f} view 1\n" + "\n".join(rows) + "\n" + "\n".join(objs) + "\n")
            r = report()
            r["players"][0]["player_index"] = 1
            marker = Path(d) / "copied.txt"
            # a name crafted as an ffmpeg filter: it must stay a caption (no file written, no filter run)
            r["players"][0]["name"] = f"x',movie={d}/x.txt,drawtext=text='y'[out];[in]null,metadata=print:file={marker}'"
            r["players"][0]["events"] = [dict(event(25, "never_seen", 1), x=500.0, y=500.0)]
            rp = Path(d) / "r.json"
            rp.write_text(json.dumps(r))
            out = Path(d) / "out"
            self.assertEqual(rev.main(["--dump", str(dump), "--report", str(rp), "--player", "1", "--out-dir", str(out), "--name", "ev", "--scale", "6"]), 0)
            self.assertGreater((out / "ev.png").stat().st_size, 1000)
            self.assertGreater((out / "ev.mp4").stat().st_size, 1000)
            self.assertFalse(marker.exists())
            self.assertEqual(rev.safe_text("a'b;c[d]e"), "a_b_c_d_e")



class FfmpegVersionTest(unittest.TestCase):
    """-filter_script is gone in ffmpeg 9 ("Option not found"); 7+ takes -/filter:v FILE. An unread version is an error, never a guess."""

    def fake(self, d: str, out: str, rc: int = 0) -> str:
        f = Path(d) / "ffmpeg"
        f.write_text(f"#!/bin/sh\nprintf '%s\\n' {json.dumps(out)}\nexit {rc}\n")
        f.chmod(0o755)
        return str(f)

    def test_the_major_version_picks_the_option(self):
        with tempfile.TemporaryDirectory() as d:
            for text, major in (("ffmpeg version 6.1.1-3ubuntu5 Copyright (c) 2000-2023", 6), ("ffmpeg version n7.1 Copyright", 7),
                                ("ffmpeg version 9.0 Copyright (c) 2000-2026", 9)):
                self.assertEqual(rev.ffmpeg_major(self.fake(d, text)), major)
            for text, rc in (("ffmpeg version N-118000-gabcdef Copyright", 0), ("ffmpeg version 6.1.1", 1), ("", 0)):
                with self.assertRaises(RuntimeError):
                    rev.ffmpeg_major(self.fake(d, text, rc))
        self.assertEqual(rev.filter_script_args(6, Path("f.txt")), ["-filter_script:v", "f.txt"])
        self.assertEqual(rev.filter_script_args(7, Path("f.txt")), ["-/filter:v", "f.txt"])
        self.assertEqual(rev.filter_script_args(9, Path("f.txt")), ["-/filter:v", "f.txt"])

if __name__ == "__main__":
    unittest.main()
