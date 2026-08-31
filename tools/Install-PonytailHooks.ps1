[CmdletBinding(DefaultParameterSetName = 'Verify')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')]
    [switch]$Install,
    [Parameter(Mandatory = $true, ParameterSetName = 'Verify')]
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $output = @(& git @Arguments 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed: $($output -join [Environment]::NewLine)"
    }
    return ($output -join "`n").Trim()
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Utf8LfAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    $normalized = $Text.Replace("`r`n", "`n").Replace("`r", "`n")
    $temporary = "$Path.tmp-$PID"
    [IO.File]::WriteAllText($temporary, $normalized, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Write-BytesAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][byte[]]$Bytes
    )
    $temporary = "$Path.tmp-$PID"
    [IO.File]::WriteAllBytes($temporary, $Bytes)
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

$commonGitDir = [IO.Path]::GetFullPath((Invoke-Git -Arguments @('rev-parse', '--path-format=absolute', '--git-common-dir')))
$mainRoot = Split-Path -Parent $commonGitDir
$hooksDir = Join-Path $commonGitDir 'hooks'
$manifestPath = Join-Path $hooksDir 'openbfme-ponytail.json'
$gatePath = Join-Path $mainRoot 'tools\ponytail-git-gate.py'
$pythonCandidates = @(
    (Join-Path $mainRoot 'workspace\retail-work\tools\python-3.12-env\Scripts\python.exe'),
    (Join-Path $mainRoot 'workspace\retail-work\tools\cpython-3.12.13\python.exe')
)
$pythonPath = $pythonCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$grokCommand = Get-Command grok.exe -CommandType Application -ErrorAction Stop
$grokPath = $grokCommand.Source

$configuredHooksPath = @(& git config --get core.hooksPath 2>$null | ForEach-Object { [string]$_ }) -join "`n"
if ($LASTEXITCODE -notin @(0, 1)) { throw 'could not read core.hooksPath' }
if ($configuredHooksPath) {
    throw 'core.hooksPath must remain unset; OpenBFME installs into the shared common Git hooks directory.'
}
if (-not (Test-Path -LiteralPath $gatePath -PathType Leaf)) { throw "missing gate: $gatePath" }
if (-not $pythonPath) { throw 'repository-pinned private Python runtime is missing' }
if (-not (Test-Path -LiteralPath $hooksDir -PathType Container)) { throw "missing Git hooks directory: $hooksDir" }
$gateIndex = @(& git -C $mainRoot ls-files -s -- tools/ponytail-git-gate.py 2>$null | ForEach-Object { [string]$_ })
if ($LASTEXITCODE -ne 0 -or $gateIndex.Count -ne 1) { throw 'Ponytail gate must be staged or tracked exactly once' }
$gateBlob = ($gateIndex[0] -split '\s+')[1]
$worktreeBlob = @(& git -C $mainRoot hash-object -- tools/ponytail-git-gate.py 2>$null | ForEach-Object { [string]$_ }) -join ''
if ($LASTEXITCODE -ne 0 -or $gateBlob -ne $worktreeBlob) { throw 'Ponytail gate worktree bytes differ from the staged/tracked blob' }

& $pythonPath -I -S -B $gatePath --verify-approved-plugin --grok-path $grokPath
if ($LASTEXITCODE -ne 0) { throw 'installed Ponytail plugin is not the tracked approved release' }

$details = @(& $grokPath plugin details ponytail 2>&1 | ForEach-Object { [string]$_ })
if ($LASTEXITCODE -ne 0) { throw "Grok Ponytail plugin is unavailable: $($details -join [Environment]::NewLine)" }
$pluginPathLine = $details | Where-Object { $_ -match '^\s*path:\s*(.+?)\s*$' } | Select-Object -First 1
if (-not $pluginPathLine) { throw 'Grok Ponytail plugin details have no installed path' }
$pluginPath = [IO.Path]::GetFullPath(([regex]::Match($pluginPathLine, '^\s*path:\s*(.+?)\s*$').Groups[1].Value))
$skillPath = Join-Path $pluginPath 'skills\ponytail-review\SKILL.md'
$commandPath = Join-Path $pluginPath 'commands\ponytail-review.toml'
if (-not (Test-Path -LiteralPath $skillPath -PathType Leaf) -or -not (Test-Path -LiteralPath $commandPath -PathType Leaf)) {
    throw 'missing official Ponytail review skill or command'
}
$inspection = @(& $grokPath inspect 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
if ($LASTEXITCODE -ne 0 -or $inspection -notmatch 'ponytail \(user, enabled\)' -or $inspection -notmatch '/ponytail:ponytail-review') {
    throw 'official Ponytail plugin and qualified review skill are not enabled in Grok'
}
& git lfs version | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Git LFS is unavailable' }
$ponytailCommit = @(& git -C $pluginPath rev-parse HEAD 2>$null | ForEach-Object { [string]$_ }) -join ''
if ($LASTEXITCODE -ne 0 -or $ponytailCommit -notmatch '^[0-9a-f]{40}$') { throw 'cannot bind the installed Ponytail Git revision' }
$ponytailOrigin = @(& git -C $pluginPath remote get-url origin 2>$null | ForEach-Object { [string]$_ }) -join ''
if ($LASTEXITCODE -ne 0 -or $ponytailOrigin -notin @('https://github.com/DietrichGebert/ponytail', 'https://github.com/DietrichGebert/ponytail.git')) {
    throw "unexpected Ponytail origin: $ponytailOrigin"
}
$ponytailStatus = @(& git -C $pluginPath status --porcelain=v1 --untracked-files=all 2>$null | ForEach-Object { [string]$_ })
if ($LASTEXITCODE -ne 0 -or $ponytailStatus.Count -ne 0) { throw 'installed Ponytail plugin checkout is dirty' }
$ponytailVersion = (Get-Content -LiteralPath (Join-Path $pluginPath 'package.json') -Raw | ConvertFrom-Json).version
if (-not $ponytailVersion) { throw 'cannot bind the installed Ponytail version' }

if ($Install) {
    $pythonHook = $pythonPath.Replace('\', '/')
    $gateHook = $gatePath.Replace('\', '/')
    $preCommit = @'
#!/bin/sh
exec "__PYTHON__" -I -S -B "__GATE__" --event pre-commit "$@"
'@.Replace('__PYTHON__', $pythonHook).Replace('__GATE__', $gateHook) + "`n"
    $prePush = @'
#!/bin/sh
exec "__PYTHON__" -I -S -B "__GATE__" --event pre-push "$@"
'@.Replace('__PYTHON__', $pythonHook).Replace('__GATE__', $gateHook) + "`n"
    $expected = @{
        'pre-commit' = $preCommit
        'pre-merge-commit' = $preCommit.Replace('--event pre-commit', '--event pre-merge-commit')
        'pre-applypatch' = $preCommit.Replace('--event pre-commit', '--event pre-applypatch')
        'pre-push' = $prePush
    }
    $oldManifest = $null
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        $oldManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    }
    $archiveRoot = Join-Path $mainRoot ('workspace\archive\git-hooks\' + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ'))
    $stockLfsGuard = @'
command -v git-lfs >/dev/null 2>&1 || { printf >&2 "\n%s\n\n" "This repository is configured for Git LFS but 'git-lfs' was not found on your path. If you no longer wish to use Git LFS, remove this hook by deleting the 'pre-push' file in the hooks directory (set by 'core.hookspath'; usually '.git/hooks')."; exit 2; }
'@.Trim()
    foreach ($name in $expected.Keys) {
        $path = Join-Path $hooksDir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $currentHash = Get-Sha256 -Path $path
        $knownInstalled = $oldManifest -and $oldManifest.hooks.$name -eq $currentHash
        $hookLines = @((Get-Content -LiteralPath $path -Raw).Replace("`r`n", "`n").TrimEnd("`n").Split("`n"))
        $stockLfs = $name -eq 'pre-push' -and $hookLines.Count -eq 3 -and
            $hookLines[0] -eq '#!/bin/sh' -and
            $hookLines[1] -eq $stockLfsGuard -and
            $hookLines[2] -eq 'git lfs pre-push "$@"'
        if (-not $knownInstalled -and -not $stockLfs) {
            throw "refusing to replace unknown Git hook: $path"
        }
        if (-not $knownInstalled) {
            [IO.Directory]::CreateDirectory($archiveRoot) | Out-Null
            [IO.File]::WriteAllBytes((Join-Path $archiveRoot $name), [IO.File]::ReadAllBytes($path))
        }
    }
    $snapshots = @{}
    foreach ($path in @($expected.Keys | ForEach-Object { Join-Path $hooksDir $_ }) + @($manifestPath)) {
        $snapshots[$path] = if (Test-Path -LiteralPath $path -PathType Leaf) {
            [IO.File]::ReadAllBytes($path)
        } else {
            $null
        }
    }
    try {
        foreach ($entry in $expected.GetEnumerator()) {
            Write-Utf8LfAtomic -Path (Join-Path $hooksDir $entry.Key) -Text $entry.Value
        }
        $manifest = [ordered]@{
        schema = 'openbfme.ponytail-hooks'
        schemaVersion = 1
        installedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        mainRoot = $mainRoot
        gatePath = $gatePath
        gateSha256 = Get-Sha256 -Path $gatePath
        pythonPath = $pythonPath
        pythonSha256 = Get-Sha256 -Path $pythonPath
        grokPath = $grokPath
        grokSha256 = Get-Sha256 -Path $grokPath
        ponytailSkillPath = $skillPath
        ponytailSkillSha256 = Get-Sha256 -Path $skillPath
        ponytailCommandPath = $commandPath
        ponytailCommandSha256 = Get-Sha256 -Path $commandPath
        ponytailCommit = $ponytailCommit
        ponytailOrigin = $ponytailOrigin
        ponytailVersion = $ponytailVersion
        hooks = [ordered]@{
            'pre-commit' = Get-Sha256 -Path (Join-Path $hooksDir 'pre-commit')
            'pre-merge-commit' = Get-Sha256 -Path (Join-Path $hooksDir 'pre-merge-commit')
            'pre-applypatch' = Get-Sha256 -Path (Join-Path $hooksDir 'pre-applypatch')
            'pre-push' = Get-Sha256 -Path (Join-Path $hooksDir 'pre-push')
        }
        }
        Write-Utf8LfAtomic -Path $manifestPath -Text (($manifest | ConvertTo-Json -Depth 5) + "`n")
        & $pythonPath -I -S -B $gatePath --verify-installation
        if ($LASTEXITCODE -ne 0) { throw 'Ponytail hook verification failed' }
    } catch {
        foreach ($entry in $snapshots.GetEnumerator()) {
            if ($null -eq $entry.Value) {
                if (Test-Path -LiteralPath $entry.Key) { Remove-Item -LiteralPath $entry.Key -Force }
            } else {
                Write-BytesAtomic -Path $entry.Key -Bytes $entry.Value
            }
        }
        throw
    }
} else {
    & $pythonPath -I -S -B $gatePath --verify-installation
    if ($LASTEXITCODE -ne 0) { throw 'Ponytail hook verification failed' }
}
Write-Host "PONYTAIL_HOOK_INSTALL PASS mode=$($PSCmdlet.ParameterSetName) common_git_dir=$commonGitDir"
