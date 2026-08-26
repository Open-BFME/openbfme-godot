# Runs the UI parity capture + gate (Q90): boots the game WINDOWED at 1280x720,
# photographs each major screen into workspace/review/ui-parity/, and asserts a
# structural per-screen checklist against reference/INDEX.md (REF ids).
# Reds are FINDINGS (missing retail elements), not harness errors.
#
# Usage:  powershell -File tools\run-ui-parity-gate.ps1
# Log:    workspace\logs\ui-parity-gate.log
# Exit:   0 = all checks green, 1 = at least one red (or watchdog kill)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$godot = "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe"
$log = Join-Path $repo "workspace\logs\ui-parity-gate.log"
$deadlineSeconds = 240

if (-not (Test-Path $godot)) {
    Write-Host "UI_PARITY_GATE DRIVER REFUSED: Godot exe not found at $godot"
    exit 2
}
New-Item -ItemType Directory -Force (Join-Path $repo "workspace\logs") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $repo "workspace\review\ui-parity") | Out-Null

# NO --headless: this runner photographs the presented window and refuses headless.
$processArgs = @(
    "--path", (Join-Path $repo "game"),
    "--script", "res://tests/ui_parity_gate_runner.gd"
)
$process = Start-Process -FilePath $godot -ArgumentList $processArgs `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
    -PassThru -WindowStyle Minimized

# External watchdog: the runner has its own 240 s deadline; this one catches a
# hang so hard the runner's own watchdog never fires.
if (-not $process.WaitForExit(($deadlineSeconds + 30) * 1000)) {
    Write-Host "UI_PARITY_GATE DRIVER: killing pid $($process.Id) after $($deadlineSeconds + 30)s"
    Stop-Process -Id $process.Id -Force -Confirm:$false
    Get-Content $log -Tail 40
    exit 1
}

Get-Content $log
if (Test-Path "$log.err") { Get-Content "$log.err" | Select-Object -First 20 }
exit $process.ExitCode
