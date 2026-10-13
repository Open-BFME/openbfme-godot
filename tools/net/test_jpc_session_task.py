# Lane INFRA-2: the Windows half of services/online/deploy/jpc_session_run.sh (jpc_session_task.py), run here with a stand-in launcher and a stand-in
# cloudflare_api.py: each step goes through the launcher with its stdin file, the first failure ends the run with its exit code, only the allowed
# launcher kinds / scripts run, and the bundle folder (secrets.pack with it) is deleted whatever happens, before the "=== done" line.
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "services/online/deploy"))
import jpc_session_task  # noqa: E402

LAUNCHER = """import subprocess, sys
assert sys.argv[1] in ("cloudflare-workers", "cloudflare-dns") and sys.argv[2] == "--"
print("launcher:", sys.argv[1])
sys.exit(subprocess.run(sys.argv[3:]).returncode)
"""
FAKE_API = """import sys
data = sys.stdin.buffer.read()
print("api:", " ".join(sys.argv[1:]), "stdin", len(data))
sys.exit(int(sys.argv[2]) if sys.argv[1] == "fail" else 0)
"""


def bundle(tmp_path, steps):
    b = tmp_path / "deploy-tmp"
    b.mkdir()
    (b / "cloudflare_api.py").write_text(FAKE_API)
    (b / "secrets.pack").write_bytes(b"TICKET_KEYS eA==\n")
    out = tmp_path / "deploy-out.txt"
    (b / "job.json").write_text(steps if isinstance(steps, str) else json.dumps({"steps": steps}))
    launcher = tmp_path / "infra-run.py"
    launcher.write_text(LAUNCHER)
    return b, out, launcher


def test_steps_run_through_the_launcher_and_the_bundle_is_deleted(tmp_path):
    b, out, launcher = bundle(tmp_path, [
        {"kind": "cloudflare-workers", "script": "cloudflare_api.py", "stdin": "secrets.pack", "args": ["deploy", "--src", "{bundle}"]},
        {"kind": "cloudflare-dns", "script": "cloudflare_api.py", "args": ["check"]},
    ])
    assert jpc_session_task.run(b, launcher, out) == 0
    text = out.read_text()
    assert f"api: deploy --src {b} stdin 17" in text and "api: check stdin 0" in text and "launcher: cloudflare-dns" in text
    assert text.splitlines()[-2:] == ["deploy-tmp deleted", "=== done exit 0"]
    assert not b.exists()


def test_a_failing_step_stops_the_run_and_still_deletes_the_bundle(tmp_path):
    b, out, launcher = bundle(tmp_path, [
        {"kind": "cloudflare-workers", "script": "cloudflare_api.py", "args": ["fail", "7"]},
        {"kind": "cloudflare-workers", "script": "cloudflare_api.py", "args": ["check"]},
    ])
    assert jpc_session_task.run(b, launcher, out) == 7
    text = out.read_text()
    assert "step 0 exit 7" in text and "api: check" not in text
    assert text.splitlines()[-2:] == ["deploy-tmp deleted", "=== done exit 7"] and not b.exists()


def test_only_the_cloudflare_launcher_kinds_and_cloudflare_api_run(tmp_path):
    for step in ({"kind": "aws", "script": "cloudflare_api.py", "args": []}, {"kind": "cloudflare-workers", "script": "other.py", "args": []}):
        t = tmp_path / step["kind"] / step["script"]
        t.mkdir(parents=True)
        b, out, launcher = bundle(t, [step])
        assert jpc_session_task.run(b, launcher, out) == 2
        text = out.read_text()
        assert "is not allowed" in text and "launcher:" not in text and text.splitlines()[-1] == "=== done exit 2" and not b.exists()


def test_a_malformed_job_still_deletes_the_bundle(tmp_path):
    # INFRA-2 r2 (Sol r1): job.json was parsed before the cleanup guard
    for i, bad in enumerate(["{not json", json.dumps({"steps": []}), json.dumps({"other": 1}), json.dumps({"steps": [{"kind": "cloudflare-workers"}]})]):
        t = tmp_path / str(i)
        t.mkdir()
        b, out, launcher = bundle(t, bad)
        assert jpc_session_task.run(b, launcher, out) == 2
        text = out.read_text()
        assert "session_task:" in text and text.splitlines()[-2:] == ["deploy-tmp deleted", "=== done exit 2"] and not b.exists()


def test_an_unwritable_output_still_deletes_the_bundle(tmp_path):
    b, out, launcher = bundle(tmp_path, [{"kind": "cloudflare-workers", "script": "cloudflare_api.py", "args": ["check"]}])
    assert jpc_session_task.run(b, launcher, tmp_path / "no-such-dir" / "out.txt") == 2 and not b.exists()
