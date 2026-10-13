#!/usr/bin/env python3
# The Windows half of jpc_session_run.sh (lane INFRA-2): runs in the owner's own logon session on JonathanPC, started by a one-off scheduled task, because
# the owner's Cloudflare tokens live in Windows Credential Manager and an ssh logon cannot read it (CredRead 1312). It sits in the bundle folder
# (deploy-tmp, ACL: the owner only) beside job.json, cloudflare_api.py, the lobby's modules and secrets.pack, and:
#   1. runs each step of job.json: <python> infra-run.py <kind> -- <python> <bundle>\<script> <args...> (stdin: a bundle file or nothing), output to
#      the output file (%USERPROFILE%\.openbfme\infra\deploy-out.txt: a fixed place, not named by the job); the first failing step ends the run;
#   2. deletes the bundle folder (secrets.pack with it) and says whether it is gone;
#   3. writes "=== done exit <code>" last: the Deck side polls for that line.
# INFRA-2 r2 (Sol r1: a malformed job.json was read before the cleanup guard, so the bundle stayed behind): everything after the start, the job's parse
# included, runs inside the guard; the bundle is deleted whatever happens, and a bundle that cannot be deleted is exit 3 and said so.
# Python 3 standard library only. Nothing secret is printed: cloudflare_api.py redacts its own output and secrets.pack only ever goes to its stdin.
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

KINDS = ("cloudflare-workers", "cloudflare-dns")
SCRIPTS = ("cloudflare_api.py",)


def run(bundle, launcher, out, python=sys.executable):
    bundle = Path(bundle).resolve()
    rc = 2
    f = None
    try:
        f = open(out, "w", encoding="utf-8", newline="\n")

        def say(line):
            f.write(line + "\n")
            f.flush()
        job = json.loads((bundle / "job.json").read_text())
        steps = job["steps"]
        if not isinstance(steps, list) or not steps:
            raise ValueError("job.json has no steps")
        rc = 0
        for i, step in enumerate(steps):
            if step["kind"] not in KINDS or step["script"] not in SCRIPTS:
                raise ValueError(f"step {i}: kind {step['kind']!r} / script {step['script']!r} is not allowed")
            argv = [python, str(launcher), step["kind"], "--", python, str(bundle / step["script"])]
            argv += [str(a).replace("{bundle}", str(bundle)) for a in step["args"]]
            say(f"=== step {i}: {step['kind']} {step['script']} {step['args'][0] if step['args'] else ''}")
            stdin = open(bundle / step["stdin"], "rb") if step.get("stdin") else subprocess.DEVNULL
            try:
                r = subprocess.run(argv, stdin=stdin, stdout=f, stderr=subprocess.STDOUT, cwd=str(bundle.parent))
            finally:
                if stdin is not subprocess.DEVNULL:
                    stdin.close()
            say(f"step {i} exit {r.returncode}")
            if r.returncode:
                rc = r.returncode
                break
    except Exception as e:  # noqa: BLE001  the Deck side reads the reason from the output file
        rc = 2
        if f:
            f.write(f"session_task: {type(e).__name__}: {e}\n")
    finally:
        os.chdir(str(bundle.parent))
        shutil.rmtree(bundle, ignore_errors=True)
        gone = not bundle.exists()
        if not gone:
            rc = 3
        if f:
            f.write("deploy-tmp deleted\n" if gone else "deploy-tmp STILL EXISTS\n")
            f.write(f"=== done exit {rc}\n")
            f.close()
    return rc


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    base = Path(os.environ["USERPROFILE"]) / ".openbfme"
    sys.exit(run(here, base / "infra" / "infra-run.py", base / "infra" / "deploy-out.txt"))
