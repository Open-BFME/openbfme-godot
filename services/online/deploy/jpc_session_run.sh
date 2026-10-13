#!/bin/bash
# Runs services/online/deploy/cloudflare_api.py in the owner's own Windows session on JonathanPC, from the Deck, in one command (lane INFRA-2).
#   jpc_session_run.sh deploy --relays=us-east=3.13.126.208:7350 [--secrets DIR]   upload the lobby of THIS worktree (tests first: DEPLOY.md)
#   jpc_session_run.sh check                                                           the plan / Worker / domain / health check, no secrets sent
# Why a scheduled task: the owner's Cloudflare tokens live in Windows Credential Manager, which an ssh logon cannot read (CredRead 1312), so the
# launcher (infra-run.py) has to run in the owner's interactive logon session. The steps:
#   1. refuse if %USERPROFILE%\.openbfme\deploy-tmp exists (another run's bundle); create it with an ACL for the owner only;
#   2. scp the bundle in: worker.mjs, lobby.mjs, cloudflare_api.py, jpc_session_task.py, job.json; for deploy, pipe pack-secrets' output over ssh
#      stdin into deploy-tmp\secrets.pack (never a file on the Deck, never a command line) and compare its SHA-256 on both sides (not printed);
#   3. register a one-off task (Interactive logon, the owner, limited rights, hidden PowerShell window) that runs jpc_session_task.py, start it;
#   4. poll infra\deploy-out.txt for "=== done" (the task deletes deploy-tmp itself), print it;
#   5. always (an EXIT trap set before anything is touched): stop the task if it still runs (and the processes of the bundle), unregister it,
#      delete deploy-tmp if this run made it, and VERIFY both are gone; a cleanup that cannot be verified is exit 4 with what is left, loudly.
# INFRA-2 r2 (Sol r1): the trap came after the preparation, unregister failures were silenced and a timed-out task kept running.
# Tests: JPC_PS_RUNNER (a program given each PowerShell script instead of ssh), JPC_SCP, JPC_POLL_S (tools/net/test_jpc_session_run.py).
# Needs the owner logged in on JonathanPC (an Interactive task does not run otherwise: reported after JPC_SESSION_WAIT seconds, default 600).
# The secrets are ~/.openbfme/online-secrets (make_keys.sh's four files). Output is cloudflare_api.py's (redacted: no token, no key).
set -euo pipefail
HOST="${JPC_SSH_HOST:-jonathan-windows}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SECRETS="$HOME/.openbfme/online-secrets"
CMD="${1:-}"; shift || true
RELAYS=""; HAVE_RELAYS=0
while [ $# -gt 0 ]; do
  case "$1" in
    --relays=*) RELAYS="${1#--relays=}"; HAVE_RELAYS=1 ;;
    --secrets) SECRETS="$2"; shift ;;
    *) echo "jpc_session_run: unknown argument $1" >&2; exit 2 ;;
  esac
  shift
done
case "$CMD" in
  deploy) [ "$HAVE_RELAYS" = 1 ] || { echo "jpc_session_run: deploy needs --relays=id=a.b.c.d:port (or --relays= for none)" >&2; exit 2; } ;;
  check) ;;
  *) echo "usage: jpc_session_run.sh deploy --relays=... [--secrets DIR] | check" >&2; exit 2 ;;
esac
URL=https://play.openbfme.com
TASK="OpenBFME-Deploy-$(date -u +%Y%m%d%H%M%S)"
WAIT="${JPC_SESSION_WAIT:-600}"
POLL="${JPC_POLL_S:-5}"
SCP="${JPC_SCP:-scp}"
OWN=0   # 1 once this run created deploy-tmp (only then does the cleanup delete it)

# a PowerShell script on JonathanPC (its ssh shell is PowerShell): sent -EncodedCommand (UTF-16LE base64), so no quoting survives the trip; @TASK@ /
# @OWN@ are replaced. Every script starts with $PRE; its first line names it ("# step: ...").
PRE='$ErrorActionPreference="Stop"; $ProgressPreference="SilentlyContinue"; $B=Join-Path $env:USERPROFILE ".openbfme"; $T=Join-Path $B "deploy-tmp"; $O=Join-Path $B "infra\deploy-out.txt"
'
ps() {
  local s="$PRE$1" own='$false'; [ "$OWN" = 1 ] && own='$true'
  s="${s//@TASK@/$TASK}"; s="${s//@OWN@/$own}"
  if [ -n "${JPC_PS_RUNNER:-}" ]; then "$JPC_PS_RUNNER" "$s"; return; fi
  ssh "$HOST" "powershell -NoProfile -NonInteractive -EncodedCommand $(printf '%s' "$s" | iconv -t UTF-16LE | base64 -w0)"
}

read -r -d '' PS_CLEANUP <<'EOF' || true
# step: cleanup
$errs = @()
try {
  $task = Get-ScheduledTask -TaskName "@TASK@" -ErrorAction SilentlyContinue
  if ($task) {
    if ($task.State -eq "Running") { Stop-ScheduledTask -TaskName "@TASK@"; "cleanup: stopped the running task" }
    Unregister-ScheduledTask -TaskName "@TASK@" -Confirm:$false
  }
} catch { $errs += "unregister: $($_.Exception.Message)" }
try {
  # the bundle's own processes (python jpc_session_task.py / cloudflare_api.py run from deploy-tmp), found by their command line, never by name
  Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($T) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "cleanup: stopped process $($_.ProcessId) of the bundle" }
} catch { $errs += "processes: $($_.Exception.Message)" }
try { if (@OWN@ -and (Test-Path $T)) { Remove-Item -Recurse -Force $T } } catch { $errs += "deploy-tmp: $($_.Exception.Message)" }
if (Get-ScheduledTask -TaskName "@TASK@" -ErrorAction SilentlyContinue) { $errs += "task @TASK@ is still registered" }
if (@OWN@ -and (Test-Path $T)) { $errs += "deploy-tmp still exists" }
if ($errs.Count) { "CLEANUP-FAILED: " + ($errs -join "; ") } else { "CLEANUP-OK: task @TASK@ absent" + $(if (@OWN@) { ", deploy-tmp absent" } else { "" }) }
EOF
read -r -d '' PS_PREPARE <<'EOF' || true
# step: prepare
if (Test-Path $T) { throw "deploy-tmp exists already (another run?): look at it before deleting it" }
New-Item -ItemType Directory $T | Out-Null
"CREATED"
icacls $T /inheritance:r /grant:r "$($env:USERNAME):(OI)(CI)F" | Out-Null
if ($LASTEXITCODE) { throw "icacls failed ($LASTEXITCODE)" }
if (Test-Path $O) { Remove-Item $O }
"PATH|$($T.Replace('\','/'))"
EOF
read -r -d '' PS_SECRETS <<'EOF' || true
# step: secrets
$i = [Console]::OpenStandardInput(); $f = [IO.File]::Create((Join-Path $T "secrets.pack")); $i.CopyTo($f); $f.Close()
(Get-FileHash -Algorithm SHA256 (Join-Path $T "secrets.pack")).Hash.ToLower()
EOF
read -r -d '' PS_START <<'EOF' || true
# step: start
$py = (Get-Command python).Source
$arg = "-NoProfile -NonInteractive -WindowStyle Hidden -Command & '$py' '$T\jpc_session_task.py'"
$a = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arg -WorkingDirectory $B
$p = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited
$s = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 15) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName "@TASK@" -Action $a -Principal $p -Settings $s | Out-Null
Start-ScheduledTask -TaskName "@TASK@"
"task @TASK@ started"
EOF
read -r -d '' PS_POLL <<'EOF' || true
# step: poll
if ((Test-Path $O) -and (Select-String -Path $O -Pattern "^=== done" -Quiet)) { "DONE" } else { "WAIT " + (Get-ScheduledTask -TaskName "@TASK@").State }
EOF
read -r -d '' PS_OUTPUT <<'EOF' || true
# step: output
Get-Content $O
EOF
read -r -d '' PS_STATE <<'EOF' || true
# step: state
$i = Get-ScheduledTaskInfo -TaskName "@TASK@"; "state " + (Get-ScheduledTask -TaskName "@TASK@").State + ", last result " + $i.LastTaskResult
EOF

W="$(mktemp -d)"
on_exit() {
  local rc=$? out
  rm -rf "$W"
  out="$(ps "$PS_CLEANUP" < /dev/null 2>&1 | tr -d '\r')" || true
  if grep -q '^CLEANUP-OK' <<< "$out"; then
    grep '^cleanup: \|^CLEANUP-OK' <<< "$out"
  else
    echo "jpc_session_run: CLEANUP NOT VERIFIED on $HOST: ${out:-no answer}" >&2
    echo "jpc_session_run: check by hand: the task $TASK and $( [ "$OWN" = 1 ] && echo '%USERPROFILE%\.openbfme\deploy-tmp' || echo 'no folder (this run made none)')" >&2
    rc=4
  fi
  exit "$rc"
}
trap on_exit EXIT

# 1. the bundle folder, owner-only (refused if one exists: never delete another run's bundle)
PREP="$(ps "$PS_PREPARE" < /dev/null | tr -d '\r')" || { grep -q '^CREATED' <<< "$PREP" && OWN=1; echo "jpc_session_run: preparing deploy-tmp failed" >&2; exit 1; }
OWN=1
TW="$(sed -n 's/^PATH|//p' <<< "$PREP")"
echo "bundle folder ready (owner-only ACL)"

# 2. the bundle and the job's steps (jpc_session_task.py runs them in the owner's session)
python3 - "$W/job.json" "$CMD" "$RELAYS" "$URL" <<'PY'
import json, sys
path, cmd, relays, url = sys.argv[1:]
if cmd == "deploy":
    step = {"kind": "cloudflare-workers", "script": "cloudflare_api.py", "stdin": "secrets.pack",
            "args": ["deploy", "--src", "{bundle}", "--hostname", url.split("//")[1], "--public-url", url, f"--relays={relays}"]}
else:
    step = {"kind": "cloudflare-workers", "script": "cloudflare_api.py", "args": ["check", "--public-url", url]}
json.dump({"steps": [step]}, open(path, "w"))
PY
$SCP -q "$ROOT/services/online/lobby/src/worker.mjs" "$ROOT/services/online/lobby/src/lobby.mjs" "$ROOT/services/online/deploy/cloudflare_api.py" \
  "$ROOT/services/online/deploy/jpc_session_task.py" "$W/job.json" "$HOST:$TW/" < /dev/null
if [ "$CMD" = deploy ]; then
  WANT="$(python3 "$ROOT/services/online/deploy/cloudflare_api.py" pack-secrets "$SECRETS" | sha256sum | cut -d' ' -f1)"
  GOT="$(python3 "$ROOT/services/online/deploy/cloudflare_api.py" pack-secrets "$SECRETS" | ps "$PS_SECRETS" 2> /dev/null | tr -d '\r')"
  [ -n "$WANT" ] && [ "$WANT" = "$GOT" ] || { echo "jpc_session_run: secrets.pack did not arrive intact (hash mismatch); nothing was run" >&2; exit 1; }
  echo "secrets.pack in place (its SHA-256 matches the Deck's)"
fi

# 3. the one-off task in the owner's session (Interactive: runs only while the owner is logged in; limited rights: no UAC)
ps "$PS_START" < /dev/null | tr -d '\r'

# 4. wait for the task's last line ("=== done exit N"; the task has deleted deploy-tmp by then); past WAIT the trap stops the task
deadline=$((SECONDS + WAIT))
while [ "$SECONDS" -lt "$deadline" ]; do
  sleep "$POLL"
  if ps "$PS_POLL" < /dev/null | tr -d '\r' | grep -q '^DONE'; then
    ps "$PS_OUTPUT" < /dev/null | tr -d '\r' | tee "$W/out.txt"
    rc="$(sed -n 's/^=== done exit \([0-9]*\)$/\1/p' "$W/out.txt")"
    exit "${rc:-1}"
  fi
done
echo "jpc_session_run: no result after $WAIT s ($(ps "$PS_STATE" < /dev/null | tr -d '\r')); is the owner logged in on JonathanPC? The task is stopped and removed." >&2
exit 1
