[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$scanner = Join-Path $PSScriptRoot "export-scan.ps1"
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase ("openbfme-export-scan-" + [Guid]::NewGuid().ToString("N"))))
$junctionPath = ""

if (-not $testRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create export-scan fixtures outside the system temp directory."
}

try {
    $cleanRoot = Join-Path $testRoot "clean"
    $badRoot = Join-Path $testRoot "bad"
    [void](New-Item -ItemType Directory -Path $cleanRoot -Force)
    [void](New-Item -ItemType Directory -Path $badRoot -Force)
    [IO.File]::WriteAllText((Join-Path $cleanRoot "project.godot"), "[application]`nconfig/name=`"Legal Safe Fixture`"`n", [Text.UTF8Encoding]::new($false))
    foreach ($name in @("authored.json", "authored.png", "authored.glb", "authored.wav")) {
        [IO.File]::WriteAllBytes((Join-Path $cleanRoot $name), [byte[]](1, 2, 3, 4))
    }
    [IO.File]::WriteAllText((Join-Path $badRoot "project.godot"), "[application]`nconfig/name=`"Bad Fixture`"`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllBytes((Join-Path $badRoot "retail.big"), [byte[]](1, 2, 3, 4))
    [IO.File]::WriteAllBytes((Join-Path $badRoot "retail.map"), [byte[]](1, 2, 3, 4))
    [IO.File]::WriteAllBytes((Join-Path $badRoot "retail.dds"), [byte[]](0x44, 0x44, 0x53, 0x20))
    [IO.File]::WriteAllText((Join-Path $badRoot "retail.ini"), "Object GondorFighter`n", [Text.UTF8Encoding]::new($false))
    $junctionTarget = Join-Path $testRoot "junction-target"
    [void](New-Item -ItemType Directory -Path $junctionTarget -Force)
    [IO.File]::WriteAllBytes((Join-Path $junctionTarget "outside-sentinel.big"), [byte[]](9, 9, 9, 9))
    $junctionPath = Join-Path $badRoot "linked-content"
    [void](New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget -ErrorAction Stop)

    $cleanOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $scanner -Root $cleanRoot 2>&1 | ForEach-Object { $_.ToString() })
    $cleanExit = $LASTEXITCODE
    if ($cleanExit -ne 0 -or ($cleanOutput -join "`n") -cnotmatch '(?m)^EXPORT_SCAN PASS ') {
        throw "The clean export fixture did not pass."
    }

    $badOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $scanner -Root $badRoot 2>&1 | ForEach-Object { $_.ToString() })
    $badExit = $LASTEXITCODE
    if ($badExit -eq 0 -or ($badOutput -join "`n") -cnotmatch '(?m)^EXPORT_SCAN FAIL ') {
        throw "The retail-format negative fixture was not rejected."
    }
    $badText = $badOutput -join "`n"
    foreach ($expected in @("retail.map", "retail.dds", "retail.ini", "directory reparse point")) {
        if ($badText -notmatch [regex]::Escape($expected)) {
            throw "The export scanner did not report '$expected'."
        }
    }
    if ($badText -match 'outside-sentinel') {
        throw "The export scanner traversed a directory reparse point."
    }

    Write-Host "EXPORT_SCAN_SELF_TEST PASS clean_authored_formats=true retail_raw_rejects=true reparse_rejects=true"
    exit 0
}
catch {
    Write-Host "EXPORT_SCAN_SELF_TEST FAIL $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not [string]::IsNullOrWhiteSpace($junctionPath) -and (Test-Path -LiteralPath $junctionPath)) {
        [IO.Directory]::Delete($junctionPath, $false)
    }
    if ($resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
