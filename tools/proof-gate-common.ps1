Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script:ProofGateFailureOutput = ""

function Assert-ProofTrue {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-ProofProperties {
    param([object]$Object, [string[]]$Required, [string[]]$Allowed, [string]$Context)
    Assert-ProofTrue ($null -ne $Object) "$Context must be an object."
    foreach ($name in $Required) {
        Assert-ProofTrue ($null -ne $Object.PSObject.Properties[$name]) "$Context is missing '$name'."
    }
    foreach ($property in $Object.PSObject.Properties) {
        Assert-ProofTrue ($Allowed -contains $property.Name) "$Context contains unknown property '$($property.Name)'."
    }
}

function Read-ProofJson {
    param([string]$Path)
    try { return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { throw "Invalid JSON in '$Path': $($_.Exception.Message)" }
}

function Test-ProofInteger {
    param([object]$Value)
    return $Value -is [int] -or $Value -is [long]
}

function Resolve-ProofGodot {
    param([string]$RequestedPath, [string]$RepoRoot)
    . (Join-Path $PSScriptRoot "resolve-godot.ps1")
    return Resolve-OpenBfmeGodot -RequestedPath $RequestedPath -RepoRoot $RepoRoot -PreferConsole
}

function Get-ProofWorkingTreeIdentity {
    param([string]$RepoRoot)
    # git discovers its repository by walking up, so a RepoRoot that is not
    # itself a repository root would silently report the identity of whatever
    # checkout encloses it. Refuse rather than inherit a wrong identity.
    $topLevel = ([string](& git -C $RepoRoot rev-parse --show-toplevel)).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($topLevel)) "Git repository root could not be resolved for '$RepoRoot'."
    $expectedRoot = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    $discoveredRoot = [IO.Path]::GetFullPath($topLevel).TrimEnd('\', '/')
    Assert-ProofTrue ([string]::Equals($discoveredRoot, $expectedRoot, [StringComparison]::OrdinalIgnoreCase)) "RepoRoot '$RepoRoot' is not itself a Git repository root (git top-level: '$topLevel'); its identity would be inherited from an enclosing checkout."
    $revision = (& git -C $RepoRoot rev-parse HEAD).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and $revision -match '^[0-9a-f]{40}$') "Git revision could not be resolved."
    $temporary = Join-Path $RepoRoot ".private\scratch\proof-working-tree.diff"
    New-Item -ItemType Directory -Force (Split-Path -Parent $temporary) | Out-Null
    & git -c core.safecrlf=false -C $RepoRoot diff --binary HEAD --output=$temporary -- . 2>$null
    Assert-ProofTrue ($LASTEXITCODE -eq 0) "Git working-tree diff could not be captured."
    $diffSha = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
    $untracked = @(& git -C $RepoRoot ls-files --others --exclude-standard)
    Assert-ProofTrue ($LASTEXITCODE -eq 0) "Git untracked-file list could not be captured."
    $rows = [Collections.Generic.List[string]]::new()
    $rows.Add("diff=$diffSha")
    foreach ($relative in @($untracked | Sort-Object)) {
        $path = Join-Path $RepoRoot $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $sha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            $rows.Add("untracked=$relative`0$sha")
        }
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $digest = ([BitConverter]::ToString($hasher.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant() }
    finally { $hasher.Dispose() }
    return [pscustomobject]@{ revision = $revision; dirtyStateDigest = $digest }
}

function Invoke-ProofChecked {
    param(
        [string]$Gate,
        [string]$Step,
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Marker,
        [string]$Forbidden = ""
    )
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $lines = @(& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $oldPreference }
    $output = $lines -join [Environment]::NewLine
    if ($exitCode -ne 0 -or $output -cnotmatch $Marker -or (-not [string]::IsNullOrWhiteSpace($Forbidden) -and $output -match $Forbidden)) {
        $script:ProofGateFailureOutput = $output
        throw "$Step failed."
    }
    Write-Host "$Gate $Step PASS"
    return $output
}

function Invoke-ProofPriorGate {
    param(
        [string]$Gate,
        [string]$Step,
        [string]$PriorScript,
        [string]$GodotPath,
        [string]$ExpectedMarker
    )
    $arguments = @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PriorScript)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $arguments += @("-GodotPath", $GodotPath) }
    return Invoke-ProofChecked $Gate $Step "powershell.exe" $arguments $ExpectedMarker
}

function Write-ProofGateFailure {
    param([string]$Gate, [object]$ErrorRecord)
    Write-Host "$Gate FAIL $($ErrorRecord.Exception.Message)" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($script:ProofGateFailureOutput)) {
        Write-Host "$Gate child-output-begin"
        Write-Host (($script:ProofGateFailureOutput -split "`r?`n" | Select-Object -Last 140) -join [Environment]::NewLine)
        Write-Host "$Gate child-output-end"
    }
}
