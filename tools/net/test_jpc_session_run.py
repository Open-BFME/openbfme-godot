# Lane INFRA-2 r2 (Sol r1): services/online/deploy/jpc_session_run.sh's control flow with a stand-in for JonathanPC (JPC_PS_RUNNER gets each
# PowerShell script; JPC_SCP stands in for scp). Every path runs the cleanup: a success, another run's bundle (never deleted: the cleanup is told it
# owns no folder), a preparation that fails after creating the folder, a damaged secrets copy, a failing step, a timeout (the cleanup stops the
# running task and the bundle's processes), and a cleanup that cannot be verified (exit 4, loudly, even after a successful deploy).
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "services/online/deploy/jpc_session_run.sh"

RUNNER = r'''
import hashlib, json, os, sys
script = sys.argv[1]
sc = json.loads(os.environ["FAKE_SCENARIO"])
state = os.environ["FAKE_STATE"]
step = next(l.split(":", 1)[1].strip() for l in script.splitlines() if l.startswith("# step:"))
with open(os.path.join(state, "calls.jsonl"), "a") as f:
    f.write(json.dumps({"step": step, "script": script}) + "\n")
def out(*lines):
    print("\n".join(lines))
if step == "prepare":
    p = sc.get("prepare", "ok")
    if p == "exists":
        print("deploy-tmp exists already", file=sys.stderr); sys.exit(1)
    out("CREATED")
    if p == "icacls-fail":
        sys.exit(1)
    out("PATH|C:/Users/x/.openbfme/deploy-tmp")
elif step == "secrets":
    data = sys.stdin.buffer.read()
    out("0" * 64 if sc.get("secrets") == "damaged" else hashlib.sha256(data).hexdigest())
elif step == "start":
    out("task started")
elif step == "poll":
    out("DONE" if sc.get("poll", "done") == "done" else "WAIT Running")
elif step == "output":
    out("=== step 0: cloudflare-workers cloudflare_api.py x", f"step 0 exit {sc.get('exit', 0)}", "deploy-tmp deleted", f"=== done exit {sc.get('exit', 0)}")
elif step == "state":
    out("state Running, last result 267009")
elif step == "cleanup":
    if sc.get("cleanup", "ok") == "ok":
        out("cleanup: stopped the running task" if sc.get("poll") == "never" else "", "CLEANUP-OK: task absent")
    else:
        out("CLEANUP-FAILED: task OpenBFME-Deploy-x is still registered")
'''


def run(tmp_path, scenario, cmd="check", extra=(), wait="30"):
    state = tmp_path / f"state-{len(list(tmp_path.glob('state-*')))}"
    state.mkdir()
    runner = tmp_path / "runner.py"
    runner.write_text(RUNNER)
    fake = tmp_path / "fake-ps"
    fake.write_text(f'#!/bin/bash\nexec {sys.executable} {runner} "$@"\n')
    fake.chmod(0o755)
    scp = tmp_path / "fake-scp"
    scp.write_text(f'#!/bin/bash\necho "$@" >> {state}/scp.txt\n')
    scp.chmod(0o755)
    sec = tmp_path / "secrets"
    sec.mkdir(exist_ok=True)
    for name, v in {"ticket.keys": "1:" + "11" * 32 + "\n", "session.key": "22" * 32, "upload.key": "33" * 32, "admin.key": "44" * 32}.items():
        (sec / name).write_text(v)
    env = {**os.environ, "JPC_PS_RUNNER": str(fake), "JPC_SCP": str(scp), "JPC_POLL_S": "0.1", "JPC_SESSION_WAIT": wait,
           "FAKE_SCENARIO": json.dumps(scenario), "FAKE_STATE": str(state)}
    args = [cmd, *extra] + (["--secrets", str(sec)] if cmd == "deploy" else [])
    r = subprocess.run(["bash", str(SCRIPT), *args], env=env, capture_output=True, text=True, timeout=60)
    calls = [json.loads(l) for l in (state / "calls.jsonl").read_text().splitlines()] if (state / "calls.jsonl").exists() else []
    return r, calls


def steps(calls):
    return [c["step"] for c in calls]


def cleanup_owns(calls):
    script = next(c["script"] for c in calls if c["step"] == "cleanup")
    assert "@OWN@" not in script and "@TASK@" not in script
    return "if ($true -and (Test-Path $T))" in script


def test_a_successful_check_cleans_up_and_verifies(tmp_path):
    r, calls = run(tmp_path, {})
    assert r.returncode == 0, r.stderr
    assert steps(calls) == ["prepare", "start", "poll", "output", "cleanup"]
    assert "=== done exit 0" in r.stdout and "CLEANUP-OK" in r.stdout and cleanup_owns(calls)


def test_another_runs_bundle_is_never_deleted(tmp_path):
    r, calls = run(tmp_path, {"prepare": "exists"})
    assert r.returncode == 1 and steps(calls) == ["prepare", "cleanup"]
    assert not cleanup_owns(calls)  # the cleanup removes only this run's task, not the folder it did not make


def test_a_preparation_that_failed_after_creating_the_folder_still_deletes_it(tmp_path):
    r, calls = run(tmp_path, {"prepare": "icacls-fail"})
    assert r.returncode == 1 and steps(calls) == ["prepare", "cleanup"] and cleanup_owns(calls)


def test_damaged_secrets_stop_before_the_task_and_clean_up(tmp_path):
    r, calls = run(tmp_path, {"secrets": "damaged"}, cmd="deploy", extra=["--relays=us-east=203.0.113.1:7350"])
    assert r.returncode == 1 and "did not arrive intact" in r.stderr
    assert steps(calls) == ["prepare", "secrets", "cleanup"] and cleanup_owns(calls)
    assert "33" * 32 not in r.stdout + r.stderr


def test_a_deploy_passes_the_secrets_through_stdin_and_the_steps_exit_code_out(tmp_path):
    r, calls = run(tmp_path, {"exit": 7}, cmd="deploy", extra=["--relays=us-east=203.0.113.1:7350"])
    assert r.returncode == 7 and steps(calls) == ["prepare", "secrets", "start", "poll", "output", "cleanup"]


def test_a_timeout_stops_the_task_and_the_bundles_processes(tmp_path):
    r, calls = run(tmp_path, {"poll": "never"}, wait="1")
    assert r.returncode == 1 and "no result after 1 s" in r.stderr and "The task is stopped and removed" in r.stderr
    assert steps(calls)[-2:] == ["state", "cleanup"]
    script = calls[-1]["script"]
    assert "Stop-ScheduledTask" in script and "Stop-Process -Id $_.ProcessId" in script and "CommandLine.Contains($T)" in script
    assert "stopped the running task" in r.stdout


def test_an_unverified_cleanup_fails_loudly_even_after_a_good_deploy(tmp_path):
    r, calls = run(tmp_path, {"cleanup": "fail"})
    assert r.returncode == 4
    assert "CLEANUP NOT VERIFIED" in r.stderr and "still registered" in r.stderr and "deploy-tmp" in r.stderr
    # the cleanup script itself reports an unregister failure instead of silencing it
    script = calls[-1]["script"]
    assert "SilentlyContinue" not in script.split("Unregister-ScheduledTask")[1].splitlines()[0]
    assert '$errs += "unregister: ' in script
