[CmdletBinding()]
param(
    [string]$GodotPath = "",
    [int]$TimeoutSeconds = 180,
    [string]$Runner = "",
    [string]$ReportPath = "",
    [switch]$ListOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "MODULE_RUNTIME_EVIDENCE_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$manifestTool = Join-Path $PSScriptRoot "module-runtime-evidence-manifest.py"
$stateRoot = if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
    [IO.Path]::GetFullPath($env:OPENBFME_IMPORT_ROOT)
} else { Join-Path $repoRoot "workspace\retail-work" }
$python = Join-Path $stateRoot "tools\python-3.12-env\Scripts\python.exe"
$resolvedReportPath = if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    Join-Path $repoRoot "workspace\retail-work\reports\module-runtime-evidence-gate.json"
} else { [IO.Path]::GetFullPath($ReportPath) }
$forbiddenDiagnostics = '(?i)(?:SCRIPT ERROR|Parse Error|ObjectDB instances leaked|RID allocations|watchdog (?:abort|timeout)|Parameter [^\r\n]+ is null)'

function Invoke-EvidenceRunner {
    param([string]$RunnerPath, [string]$Godot)
    $name = Split-Path -Leaf $RunnerPath
    $scratch = Join-Path $repoRoot "workspace\scratch\module-runtime-evidence"
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    $token = [Guid]::NewGuid().ToString("N")
    $stdoutPath = Join-Path $scratch "$token.stdout.log"
    $stderrPath = Join-Path $scratch "$token.stderr.log"
    $commandLine = '""{0}" --headless --path "{1}" --script "res://tests/{2}" 1>"{3}" 2>"{4}""' -f `
        $Godot, $gameRoot, $name, $stdoutPath, $stderrPath
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = "$env:SystemRoot\System32\cmd.exe"
    $startInfo.Arguments = "/d /s /c $commandLine"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    Assert-ProofTrue ($process.Start()) "Could not start evidence runner $name."
    try {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            # Kill this exact owned cmd/Godot process tree.  The user may have
            # another visible Godot instance which must never be touched.
            & "$env:SystemRoot\System32\taskkill.exe" /PID $process.Id /T /F 2>$null | Out-Null
            throw "$name timed out after $TimeoutSeconds seconds."
        }
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw -Encoding UTF8 } else { "" }
        $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw -Encoding UTF8 } else { "" }
        $output = ($stdout + [Environment]::NewLine + $stderr).Trim()
        $marker = [regex]::Match($output, '(?m)^[A-Z0-9_]*RESULT passed=([1-9][0-9]*) failed=0[^\r\n]*\r?$')
        $forbiddenMatch = [regex]::Match($output, $forbiddenDiagnostics)
        if ($exitCode -ne 0 -or -not $marker.Success -or $forbiddenMatch.Success) {
            $script:ProofGateFailureOutput = $output
            throw "$name failed executable-evidence acceptance (exit=$exitCode marker=$($marker.Success) forbidden='$($forbiddenMatch.Value)')."
        }
        Write-Host "$gate runner=$name PASS passed=$($marker.Groups[1].Value)"
        return [int]$marker.Groups[1].Value
    }
    finally {
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
        if ($null -ne $process) { $process.Dispose() }
    }
}

try {
    # Pin the tree before deriving the dynamic manifest. This closes the small
    # race where a registry edit between generation and identity capture could
    # otherwise leave the run exercising a stale runner set.
    $initialIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (Test-Path -LiteralPath $manifestTool -PathType Leaf) "Missing evidence manifest tool."
    Assert-ProofTrue (Test-Path -LiteralPath $python -PathType Leaf) "Pinned importer Python is missing."
    Assert-ProofTrue ($TimeoutSeconds -gt 0) "TimeoutSeconds must be positive."
    $manifestText = (& $python $manifestTool --require-count 58)
    Assert-ProofTrue ($LASTEXITCODE -eq 0) "Evidence manifest generation failed."
    $manifest = ($manifestText | Out-String | ConvertFrom-Json)
    Assert-ProofTrue ([string]$manifest.schema -eq "openbfme.module-runtime-evidence-gate") "Evidence manifest schema changed."
    Assert-ProofTrue ([int]$manifest.runnerCount -eq 58) "Executable evidence must name exactly 58 unique runners at this baseline."
    $runners = @($manifest.runners)
    if (-not [string]::IsNullOrWhiteSpace($Runner)) {
        $runners = @($runners | Where-Object { (Split-Path -Leaf ([string]$_.runner)) -eq $Runner })
        Assert-ProofTrue ($runners.Count -eq 1) "Requested runner '$Runner' is not registered exactly once."
    }
    if ($ListOnly) {
        foreach ($row in $runners) { Write-Host ([string]$row.runner) }
        Write-Host "$gate PASS listed=$($runners.Count) registered=$($manifest.runnerCount)"
        exit 0
    }
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $checks = 0
    $resultRows = [Collections.Generic.List[object]]::new()
    foreach ($row in $runners) {
        $runnerPath = Join-Path $repoRoot ([string]$row.runner)
        Assert-ProofTrue (Test-Path -LiteralPath $runnerPath -PathType Leaf) "Registered runner disappeared: $runnerPath"
        $actualSha = (Get-FileHash -LiteralPath $runnerPath -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-ProofTrue ($actualSha -eq [string]$row.runnerSha256) "Runner changed after manifest generation: $runnerPath"
        $passed = Invoke-EvidenceRunner ([string]$row.runner) $godot
        $checks += $passed
        $resultRows.Add([ordered]@{
            runner = [string]$row.runner
            runnerSha256 = [string]$row.runnerSha256
            modules = @($row.modules)
            scopes = @($row.scopes)
            passed = $passed
        })
    }
    $finalIdentity = Get-ProofWorkingTreeIdentity $repoRoot
    Assert-ProofTrue (
        [string]$finalIdentity.revision -eq [string]$initialIdentity.revision -and
        [string]$finalIdentity.dirtyStateDigest -eq [string]$initialIdentity.dirtyStateDigest
    ) "Working-tree identity changed while executable evidence ran."
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedReportPath) | Out-Null
    [ordered]@{
        schema = "openbfme.module-runtime-evidence-gate-result"
        version = 1
        gitRevision = [string]$finalIdentity.revision
        dirtyStateDigest = [string]$finalIdentity.dirtyStateDigest
        registeredRunnerCount = [int]$manifest.runnerCount
        executedRunnerCount = $runners.Count
        passedCheckCount = $checks
        runners = @($resultRows)
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedReportPath -Encoding UTF8
    Write-Host "$gate PASS runners=$($runners.Count) checks=$checks registered=$($manifest.runnerCount)"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
