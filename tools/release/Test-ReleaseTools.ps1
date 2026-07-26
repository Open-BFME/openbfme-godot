[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temp = [IO.Path]::GetFullPath((Join-Path $tempBase ("openbfme-release-test-" + [Guid]::NewGuid().ToString("N"))))
if (-not $temp.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe test root."
}

try {
    $releaseWorkflow = Get-Content -LiteralPath (
        Join-Path $PSScriptRoot "..\..\.github\workflows\release.yml"
    ) -Raw
    if ($releaseWorkflow -match '\$env:GODOT_RELEASE_(?:win64|export_templates)') {
        throw "Release workflow uses ambiguous PowerShell environment-variable interpolation."
    }
    $buildJob = [regex]::Match(
        $releaseWorkflow,
        '(?ms)^  build:\r?\n(?<body>.*?)(?=^  [a-z0-9_-]+:\r?$)'
    ).Groups["body"].Value
    $signJob = [regex]::Match(
        $releaseWorkflow,
        '(?ms)^  sign:\r?\n(?<body>.*?)(?=^  [a-z0-9_-]+:\r?$)'
    ).Groups["body"].Value
    if ([string]::IsNullOrWhiteSpace($buildJob) -or [string]::IsNullOrWhiteSpace($signJob)) {
        throw "Release workflow build/sign job boundaries could not be audited."
    }
    if ($buildJob -match 'OPENBFME_RELEASE_SIGNING_KEY|secrets\.OPENBFME_RELEASE_SIGNING_KEY') {
        throw "Ungated build job can access the release signing key."
    }
    if ($signJob -notmatch '(?m)^    environment: release-signing\s*$' -or
        $signJob -notmatch '(?m)^    if: .*github\.ref_protected.*$' -or
        $signJob -notmatch 'secrets\.OPENBFME_RELEASE_SIGNING_KEY') {
        throw "Signing job is missing its protected-ref guard, environment, or environment secret."
    }
    if ([regex]::Matches(
        $releaseWorkflow,
        'secrets\.OPENBFME_RELEASE_SIGNING_KEY'
    ).Count -ne 1) {
        throw "Release signing secret must appear exactly once in the signing job."
    }
    $releaseSurface = $releaseWorkflow + "`n" + (
        Get-ChildItem -LiteralPath $PSScriptRoot -File |
            Where-Object Extension -in @(".ps1", ".py") |
            ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
    ) -join "`n"
    foreach ($forbiddenRepository in @(
        ("Ancalgonn/" + "open-bfme-engine"),
        ("Open-BFME/" + "openbmfe-godot"),
        ("Open-BFME/" + "openbfme-godot")
    )) {
        if ($releaseSurface.IndexOf(
            $forbiddenRepository,
            [StringComparison]::OrdinalIgnoreCase
        ) -ge 0) {
            throw "Release code hardcodes publish/update target '$forbiddenRepository'."
        }
    }

    $bootstrapPath = Join-Path $PSScriptRoot "..\..\importer\openbfme_importer\bootstrap.py"
    $runtimeScriptPath = Join-Path $PSScriptRoot "New-PinnedPythonRuntime.ps1"
    $bootstrap = Get-Content -LiteralPath $bootstrapPath -Raw
    $runtimeScript = Get-Content -LiteralPath $runtimeScriptPath -Raw
    $pythonPins = @{}
    foreach ($name in @(
        "PYTHON_VERSION",
        "PYTHON_LAUNCHER_SHA256",
        "PYTHON_BASE_DLL_SHA256",
        "PYTHON_RUNTIME_TREE_SHA256"
    )) {
        $pinMatch = [regex]::Match(
            $bootstrap,
            '(?m)^' + [regex]::Escape($name) + ' = "([^"]+)"\s*$'
        )
        if (-not $pinMatch.Success) {
            throw "Importer bootstrap does not declare $name."
        }
        $pythonPins[$name] = $pinMatch.Groups[1].Value
        if ($runtimeScript.IndexOf(
            [string]$pythonPins[$name],
            [StringComparison]::Ordinal
        ) -lt 0) {
            throw "Bundled Python runtime does not enforce importer pin $name."
        }
    }
    if ($releaseWorkflow.IndexOf(
        "cpython-$([string]$pythonPins['PYTHON_VERSION'])",
        [StringComparison]::Ordinal
    ) -lt 0 -or $releaseWorkflow -match '\(Get-Command python\)\.Source') {
        throw "Release workflow does not provision the importer-pinned Python runtime."
    }

    $stage = Join-Path $temp "stage"
    & (Join-Path $PSScriptRoot "Build-CodeOnlyExport.ps1") `
        -RepositoryRoot (Resolve-Path (Join-Path $PSScriptRoot "../..")) `
        -Destination $stage

    $payload = Join-Path $temp "payload"
    [void](New-Item -ItemType Directory -Path $payload)
    [IO.File]::WriteAllBytes((Join-Path $payload "OpenBFME.exe"), [byte[]]::new(70000))
    [IO.File]::WriteAllBytes((Join-Path $payload "OpenBFME.pck"), [byte[]](1, 2, 3))

    $version = "0.0.0-test"
    $game = Join-Path $temp "OpenBFME-$version-windows-x64.zip"
    $launcher = Join-Path $temp "OpenBFME-Launcher-$version-windows-x64.zip"
    Compress-Archive -Path (Join-Path $payload "*") -DestinationPath $game
    Copy-Item -LiteralPath $game -Destination $launcher

    $releaseSource = Get-Content -LiteralPath (
        Join-Path $PSScriptRoot "..\..\config\release-source.json"
    ) -Raw | ConvertFrom-Json
    & (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $game
    & (Join-Path $PSScriptRoot "New-ReleaseManifest.ps1") `
        -ReleaseRoot $temp `
        -Version $version `
        -Commit "0123456789abcdef0123456789abcdef01234567" `
        -ReleaseHost ([string]$releaseSource.host) `
        -Repository ([string]$releaseSource.repository) `
        -Output (Join-Path $temp "release-manifest.json")

    $manifest = Get-Content -LiteralPath (Join-Path $temp "release-manifest.json") -Raw | ConvertFrom-Json
    $expectedUrlPrefix = "https://$([string]$releaseSource.host)/$([string]$releaseSource.repository)/releases/download/v$version/"
    if ($manifest.schema -ne "openbfme.release-manifest" -or
        $manifest.packages.Count -ne 2 -or
        $manifest.repository -cne [string]$releaseSource.repository -or
        @($manifest.packages | Where-Object {
            -not ([string]$_.url).StartsWith($expectedUrlPrefix, [StringComparison]::Ordinal)
        }).Count -ne 0) {
        throw "Generated release manifest is invalid."
    }

    $badPayload = Join-Path $temp "bad-payload"
    [void](New-Item -ItemType Directory -Path $badPayload)
    [IO.File]::WriteAllBytes((Join-Path $badPayload "retail.big"), [byte[]](1, 2, 3))
    $badArchive = Join-Path $temp "bad.zip"
    Compress-Archive -Path (Join-Path $badPayload "*") -DestinationPath $badArchive
    $output = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $badArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($output -join "`n") -notmatch "RELEASE_ARTIFACT_SCAN FAIL") {
        throw "Release artifact scanner accepted a retail extension."
    }

    $agentPayload = Join-Path $temp "agent-payload"
    [void](New-Item -ItemType Directory -Path $agentPayload)
    [IO.File]::WriteAllText((Join-Path $agentPayload "AGENTS.md"), "internal instructions", [Text.UTF8Encoding]::new($false))
    $agentArchive = Join-Path $temp "agent.zip"
    Compress-Archive -Path (Join-Path $agentPayload "*") -DestinationPath $agentArchive
    $agentOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $agentArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($agentOutput -join "`n") -notmatch "forbidden private or agent path") {
        throw "Release artifact scanner accepted AGENTS.md."
    }

    $privatePayload = Join-Path $temp "private-payload"
    [void](New-Item -ItemType Directory -Path (Join-Path $privatePayload ".private"))
    [IO.File]::WriteAllText(
        (Join-Path $privatePayload ".private\receipt.json"),
        "{}",
        [Text.UTF8Encoding]::new($false)
    )
    $privateArchive = Join-Path $temp "private.zip"
    Compress-Archive -Path (Join-Path $privatePayload "*") -DestinationPath $privateArchive
    $privateOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $privateArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($privateOutput -join "`n") -notmatch "forbidden private or agent path") {
        throw "Release artifact scanner accepted a .private payload."
    }

    $portablePayload = Join-Path $temp "portable-payload"
    [void](New-Item -ItemType Directory -Path $portablePayload)
    [IO.File]::WriteAllText(
        (Join-Path $portablePayload "README.txt"),
        "Runtime state is stored under .private/retail-work and is never packaged.",
        [Text.UTF8Encoding]::new($false)
    )
    $portableArchive = Join-Path $temp "portable.zip"
    Compress-Archive -Path (Join-Path $portablePayload "*") -DestinationPath $portableArchive
    & (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $portableArchive

    $secretPayload = Join-Path $temp "secret-payload"
    [void](New-Item -ItemType Directory -Path (Join-Path $secretPayload "python"))
    [IO.File]::WriteAllText(
        (Join-Path $secretPayload "python\metadata.txt"),
        "developer path C:\Users\SensitiveUser\project",
        [Text.UTF8Encoding]::new($false)
    )
    [IO.File]::WriteAllText(
        (Join-Path $secretPayload "release.pem"),
        ("-----BEGIN " + "PRIVATE KEY-----"),
        [Text.UTF8Encoding]::new($false)
    )
    $secretArchive = Join-Path $temp "secret.zip"
    Compress-Archive -Path (Join-Path $secretPayload "*") -DestinationPath $secretArchive
    $secretOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $secretArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($secretOutput -join "`n") -notmatch "RELEASE_ARTIFACT_SCAN FAIL") {
        throw "Release artifact scanner accepted personal paths or a private key."
    }

    $modifiedTrustedPayload = Join-Path $temp "modified-trusted-payload"
    [void](New-Item -ItemType Directory -Path $modifiedTrustedPayload)
    [IO.File]::WriteAllText(
        (Join-Path $modifiedTrustedPayload "OpenBFME.exe"),
        "developer path C:\Users\SensitiveUser\project",
        [Text.UTF8Encoding]::new($false)
    )
    $modifiedTrustedArchive = Join-Path $temp "modified-trusted.zip"
    Compress-Archive -Path (Join-Path $modifiedTrustedPayload "*") -DestinationPath $modifiedTrustedArchive
    $modifiedTrustedOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $modifiedTrustedArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($modifiedTrustedOutput -join "`n") -notmatch "RELEASE_ARTIFACT_SCAN FAIL") {
        throw "Release artifact scanner trusted a modified OpenBFME executable."
    }

    $oversizedPayload = Join-Path $temp "oversized-payload"
    [void](New-Item -ItemType Directory -Path $oversizedPayload)
    $oversizedFile = [IO.File]::Create((Join-Path $oversizedPayload "opaque.bin"))
    try { $oversizedFile.SetLength(128MB + 1) }
    finally { $oversizedFile.Dispose() }
    $oversizedArchive = Join-Path $temp "oversized.zip"
    Compress-Archive -Path (Join-Path $oversizedPayload "*") -DestinationPath $oversizedArchive
    $oversizedOutput = @(& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "Test-ReleaseArtifact.ps1") -Path $oversizedArchive 2>&1)
    if ($LASTEXITCODE -eq 0 -or ($oversizedOutput -join "`n") -notmatch "oversized unscannable archive entry") {
        throw "Release artifact scanner accepted an entry too large to inspect."
    }

    # The final scanner invocation is intentionally a negative test. GitHub
    # Actions dot-sources this script and otherwise inherits that expected
    # child exit code even though every release assertion passed.
    $global:LASTEXITCODE = 0
    Write-Host "RELEASE_PIPELINE_PASS"
}
finally {
    if ($temp.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $temp)) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
