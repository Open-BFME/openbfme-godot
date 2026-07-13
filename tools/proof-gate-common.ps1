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
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) { $candidates += $RequestedPath }
    foreach ($name in @("OPENBFME_GODOT", "GODOT_CONSOLE", "GODOT_EXE", "GODOT")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($value)) { $candidates += $value }
    }
    $candidates += @(
        (Join-Path $RepoRoot ".tools\godot\Godot_v4.7-stable_win64_console.exe"),
        (Join-Path $RepoRoot ".tools\godot\Godot_v4.7-stable_win64.exe"),
        "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe",
        "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"
    )
    foreach ($value in $candidates) {
        $candidate = ([string]$value).Trim().Trim('"')
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw "Godot 4.7 was not found. Set OPENBFME_GODOT or pass -GodotPath."
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
