[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Godot,
    [Parameter(Mandatory)][string]$Project,
    [Parameter(Mandatory)][string]$Output,
    [string]$Preset = "windows",
    [Parameter(Mandatory)][string]$LogRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$godotPath = [IO.Path]::GetFullPath($Godot)
$projectPath = [IO.Path]::GetFullPath($Project)
$outputPath = [IO.Path]::GetFullPath($Output)
$logRootPath = [IO.Path]::GetFullPath($LogRoot)
if (-not (Test-Path -LiteralPath $godotPath -PathType Leaf)) { throw "Godot executable is missing." }
if (-not (Test-Path -LiteralPath (Join-Path $projectPath "project.godot") -PathType Leaf)) {
    throw "Godot project is missing."
}
New-Item -ItemType Directory -Path $logRootPath -Force | Out-Null
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($outputPath)) -Force | Out-Null

function Invoke-CheckedGodot {
    param([string]$Name, [string[]]$Arguments)
    $log = Join-Path $logRootPath "$Name.log"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $lines = @(& $godotPath @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    $text = ($lines | ForEach-Object { $_.ToString() }) -join "`n"
    [IO.File]::WriteAllText($log, $text + "`n", [Text.UTF8Encoding]::new($false))
    if ($exitCode -ne 0) { throw "Godot $Name failed with exit code $exitCode. See $log" }
    if ($text -match '(?im)^(ERROR|WARNING):') {
        $first = [regex]::Match($text, '(?im)^(ERROR|WARNING):[^\r\n]*').Value
        throw "Godot $Name reported $first. See $log"
    }
}

Invoke-CheckedGodot -Name "import" -Arguments @("--headless", "--path", $projectPath, "--import")
Invoke-CheckedGodot -Name "export" -Arguments @(
    "--headless", "--path", $projectPath, "--export-release", $Preset, $outputPath
)

$pck = [IO.Path]::ChangeExtension($outputPath, ".pck")
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf) -or
    (Get-Item -LiteralPath $outputPath).Length -lt 64KB) {
    throw "Exported executable is missing or truncated."
}
if (-not (Test-Path -LiteralPath $pck -PathType Leaf) -or
    (Get-Item -LiteralPath $pck).Length -eq 0) {
    throw "Exported PCK is missing or empty."
}
Write-Host "GODOT_WINDOWS_EXPORT_PASS exe=$outputPath pck=$pck"
