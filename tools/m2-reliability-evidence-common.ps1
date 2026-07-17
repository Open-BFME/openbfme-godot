Set-StrictMode -Version Latest

function Assert-M2ReliabilitySoakEvidence {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Soak,
        [Parameter(Mandatory = $true)]
        [double]$MinimumDurationSeconds,
        [Parameter(Mandatory = $true)]
        [long]$MaximumMemoryGrowthBytes,
        [Parameter(Mandatory = $true)]
        [long]$MaximumLateWindowMemoryGrowthBytes
    )

    function Assert-SoakEvidence {
        param([bool]$Condition, [string]$Message)
        if (-not $Condition) { throw $Message }
    }

    $actualDurationSeconds = [double]$Soak.actualDurationSeconds
    $requestedDurationSeconds = [double]$Soak.requestedDurationSeconds
    $sampleIntervalMsec = [int]$Soak.memorySampleIntervalMsec
    $samples = @($Soak.memorySamplesBytes)
    $frameSamplesMsec = [double[]]@($Soak.frameSamplesMsec | ForEach-Object { [double]$_ })

    Assert-SoakEvidence (
        [string]$Soak.schema -eq 'openbfme.m2-men-fords-live-soak' -and
        [int]$Soak.schemaVersion -eq 1
    ) "Live soak evidence schema is invalid."
    Assert-SoakEvidence ($requestedDurationSeconds -ge 5.0 -and $requestedDurationSeconds -le 3600.0) "Live soak requested duration escaped the bounded evidence-storage contract."
    $frameStorage = $Soak.frameSampleStorage
    $expectedFrameCapacity = [int][Math]::Ceiling($requestedDurationSeconds * 1000.0) + 1
    Assert-SoakEvidence (
        [string]$frameStorage.format -eq 'packed-float64-preallocated' -and
        [int]$frameStorage.maximumFramesPerSecond -eq 1000 -and
        [int]$frameStorage.capacity -eq $expectedFrameCapacity -and
        [int]$frameStorage.usedCount -eq [int]$Soak.frameCount -and
        $frameStorage.allocationCompleteBeforeMemoryBaseline -is [bool] -and
        $frameStorage.allocationCompleteBeforeMemoryBaseline -eq $true
    ) "Live soak frame evidence storage was not fully allocated before the memory baseline."
    $memoryStorage = $Soak.memorySampleStorage
    $expectedMemoryCapacity = [int][Math]::Ceiling($requestedDurationSeconds * 1000.0 / [double]$sampleIntervalMsec) + 3
    Assert-SoakEvidence (
        [string]$memoryStorage.format -eq 'packed-int64-preallocated' -and
        [int]$memoryStorage.capacity -eq $expectedMemoryCapacity -and
        [int]$memoryStorage.usedCount -eq [int]$Soak.memorySampleCount -and
        $memoryStorage.allocationCompleteBeforeMemoryBaseline -is [bool] -and
        $memoryStorage.allocationCompleteBeforeMemoryBaseline -eq $true
    ) "Live soak memory evidence storage was not fully allocated before the memory baseline."

    Assert-SoakEvidence ($actualDurationSeconds -ge $MinimumDurationSeconds) "Live soak ended before the requested active duration."
    Assert-SoakEvidence (
        [string]$Soak.viewport -eq '1920x1080' -and
        -not [string]::IsNullOrWhiteSpace([string]$Soak.renderingDriver) -and
        -not [string]::IsNullOrWhiteSpace([string]$Soak.videoAdapter)
    ) "Live soak renderer evidence is incomplete."
    Assert-SoakEvidence (
        [double]$Soak.multiSecondStallThresholdMsec -eq 2000.0 -and
        [int]$Soak.multiSecondStallCount -eq 0
    ) "Live soak recorded a multi-second gameplay stall."
    Assert-SoakEvidence ($frameSamplesMsec.Count -eq [int]$Soak.frameCount -and $frameSamplesMsec.Count -gt 0) "Live soak frame sample count is inconsistent."
    Assert-SoakEvidence (@($frameSamplesMsec | Where-Object { $_ -le 0.0 }).Count -eq 0) "Live soak contains a non-positive frame sample."

    $maximumFrameMsec = [double](($frameSamplesMsec | Measure-Object -Maximum).Maximum)
    $frameDurationMsec = [double](($frameSamplesMsec | Measure-Object -Sum).Sum)
    $multiSecondStallCount = @($frameSamplesMsec | Where-Object { $_ -ge 2000.0 }).Count
    $averageFps = [double]$frameSamplesMsec.Count / $actualDurationSeconds
    $orderedFrameMsec = [double[]]$frameSamplesMsec.Clone()
    [Array]::Sort($orderedFrameMsec)
    [Array]::Reverse($orderedFrameMsec)
    $worstOnePercentCount = [Math]::Max(1, [int][Math]::Ceiling([double]$orderedFrameMsec.Count * 0.01))
    $worstOnePercentTotalMsec = 0.0
    for ($index = 0; $index -lt $worstOnePercentCount; $index++) {
        $worstOnePercentTotalMsec += $orderedFrameMsec[$index]
    }
    $onePercentLowFps = 1000.0 / ($worstOnePercentTotalMsec / [double]$worstOnePercentCount)
    Assert-SoakEvidence ([Math]::Abs([double]$Soak.maximumFrameMsec - $maximumFrameMsec) -le 0.000001) "Live soak maximum-frame value does not match its samples."
    Assert-SoakEvidence ([Math]::Abs(($actualDurationSeconds * 1000.0) - $frameDurationMsec) -le 0.001) "Live soak frame samples do not cover its declared active duration."
    Assert-SoakEvidence ([int]$Soak.multiSecondStallCount -eq $multiSecondStallCount) "Live soak stall count does not match its samples."
    Assert-SoakEvidence ([Math]::Abs([double]$Soak.averageFps - $averageFps) -le 0.000001) "Live soak average FPS does not match its frame count and duration."
    Assert-SoakEvidence ([Math]::Abs([double]$Soak.onePercentLowFps - $onePercentLowFps) -le 0.000001) "Live soak one-percent-low FPS does not match its samples."
    Assert-SoakEvidence ([string]$Soak.memoryMetric -eq 'godot-static-memory-usage' -and $sampleIntervalMsec -eq 1000) "Live soak memory metric or cadence changed."
    Assert-SoakEvidence ($samples.Count -eq [int]$Soak.memorySampleCount -and $samples.Count -gt 0) "Live soak memory sample count is inconsistent."

    $expectedSampleCount = [int][Math]::Floor(($actualDurationSeconds * 1000.0) / [double]$sampleIntervalMsec) + 1
    Assert-SoakEvidence ([Math]::Abs($samples.Count - $expectedSampleCount) -le 2) "Live soak memory samples are incomplete for the recorded duration."

    $sampleValues = [long[]]@($samples | ForEach-Object { [long]$_ })
    Assert-SoakEvidence (@($sampleValues | Where-Object { $_ -lt 0 }).Count -eq 0) "Live soak contains a negative memory sample."

    $firstMemoryBytes = $sampleValues[0]
    $finalMemoryBytes = $sampleValues[-1]
    $sampledPeakMemoryBytes = [long](($sampleValues | Measure-Object -Maximum).Maximum)
    $memoryGrowthBytes = $finalMemoryBytes - $firstMemoryBytes
    Assert-SoakEvidence ([long]$Soak.firstMemoryBytes -eq $firstMemoryBytes) "Live soak first-memory value does not match its samples."
    Assert-SoakEvidence ([long]$Soak.finalMemoryBytes -eq $finalMemoryBytes) "Live soak final-memory value does not match its samples."
    Assert-SoakEvidence ([long]$Soak.peakMemoryBytes -ge $sampledPeakMemoryBytes) "Live soak peak memory is below its sampled maximum."
    Assert-SoakEvidence ([long]$Soak.memoryGrowthBytes -eq $memoryGrowthBytes) "Live soak total memory growth does not match its samples."

    $expectedLateSampleCount = [Math]::Min(300, $sampleValues.Count)
    Assert-SoakEvidence ([int]$Soak.lateWindowMemorySampleCount -eq $expectedLateSampleCount) "Live soak late-window sample count is inconsistent."
    if ($MinimumDurationSeconds -ge 1800.0) {
        Assert-SoakEvidence ($expectedLateSampleCount -eq 300) "The final live soak lacks the full five-minute late-memory window."
    }
    $lateStartIndex = $sampleValues.Count - $expectedLateSampleCount
    $lateMemoryGrowthBytes = $finalMemoryBytes - $sampleValues[$lateStartIndex]
    Assert-SoakEvidence ([long]$Soak.lateWindowMemoryGrowthBytes -eq $lateMemoryGrowthBytes) "Live soak late-window memory growth does not match its samples."
    Assert-SoakEvidence ($memoryGrowthBytes -le $MaximumMemoryGrowthBytes) "Live soak memory growth exceeded the pre-frozen threshold."
    Assert-SoakEvidence ($lateMemoryGrowthBytes -le $MaximumLateWindowMemoryGrowthBytes) "Live soak late-window memory growth exceeded the separately frozen stabilization threshold."
}

function Assert-M2MatchLifecycleEvidence {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Lifecycle,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedProfileSha256,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedBundleSha256,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedGitRevision,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedDirtyStateDigest
    )

    function Assert-LifecycleEvidence {
        param([bool]$Condition, [string]$Message)
        if (-not $Condition) { throw $Message }
    }

    Assert-LifecycleEvidence (
        [string]$Lifecycle.schema -eq 'openbfme.m2-match-lifecycle' -and
        [int]$Lifecycle.schemaVersion -eq 0
    ) 'Match lifecycle evidence schema is invalid.'
    Assert-LifecycleEvidence (
        [string]$Lifecycle.profileSha256 -eq $ExpectedProfileSha256 -and
        [string]$Lifecycle.bundleSha256 -eq $ExpectedBundleSha256 -and
        [string]$Lifecycle.gitRevision -eq $ExpectedGitRevision -and
        [string]$Lifecycle.dirtyStateDigest -eq $ExpectedDirtyStateDigest
    ) 'Match lifecycle evidence targets another identity.'
    Assert-LifecycleEvidence ([int]$Lifecycle.expectedMatches -eq 3) 'Match lifecycle expected-match count changed.'
    Assert-LifecycleEvidence (
        [int]$Lifecycle.completedMatches -eq 3 -and
        [int]$Lifecycle.readyStarts -eq 3 -and
        [int]$Lifecycle.teardownsConfirmed -eq 3
    ) 'Match lifecycle did not complete three starts, matches, and teardowns.'
    Assert-LifecycleEvidence ([int]$Lifecycle.diagnosticCount -eq 0) 'Match lifecycle recorded a forbidden diagnostic.'
    $rows = @($Lifecycle.matches)
    Assert-LifecycleEvidence ($rows.Count -eq 3) 'Match lifecycle does not contain exactly three match rows.'
    $signatures = @($rows | ForEach-Object { [string]$_.stateSignature } | Select-Object -Unique)
    Assert-LifecycleEvidence (
        $signatures.Count -eq 1 -and
        $signatures[0] -match '^[0-9A-F]{8}$' -and
        [string]$Lifecycle.deterministicSignature -eq $signatures[0]
    ) 'Match lifecycle signatures are absent or non-deterministic.'
    for ($index = 0; $index -lt $rows.Count; $index++) {
        $row = $rows[$index]
        Assert-LifecycleEvidence ([int]$row.index -eq $index + 1) "Match lifecycle row $($index + 1) is out of order."
        Assert-LifecycleEvidence ([string]$row.bundleSha256 -eq $ExpectedBundleSha256) "Match lifecycle row $($index + 1) mounted another bundle."
        Assert-LifecycleEvidence (
            [int]$row.winner -eq 1 -and
            [int]$row.terminalTick -gt 0 -and
            [int]$row.playerFortressHealth -le 0
        ) "Match lifecycle row $($index + 1) is not a causal enemy victory."
        Assert-LifecycleEvidence (
            [int]$row.constructionStartedSequence -gt 0 -and
            [int]$row.constructionCompletedSequence -gt [int]$row.constructionStartedSequence -and
            [int]$row.productionCompletedSequence -gt [int]$row.constructionCompletedSequence -and
            [int]$row.fortressHitSequence -gt [int]$row.productionCompletedSequence -and
            [int]$row.fortressDestroyedSequence -gt [int]$row.fortressHitSequence -and
            [int]$row.defeatSequence -gt [int]$row.fortressDestroyedSequence -and
            [int]$row.defeatEventCount -eq 1
        ) "Match lifecycle row $($index + 1) lacks construction-to-production-to-attack-to-defeat evidence."
        Assert-LifecycleEvidence (
            $row.outcomePresented -is [bool] -and
            $row.outcomePresented -eq $true -and
            [string]$row.musicState -eq 'defeat'
        ) "Match lifecycle row $($index + 1) lacks the imported defeat presentation."
        Assert-LifecycleEvidence (
            $row.sceneFreed -is [bool] -and
            $row.sceneFreed -eq $true -and
            $row.meshCacheCleared -is [bool] -and
            $row.meshCacheCleared -eq $true -and
            [int]$row.readyDurationMsec -gt 0
        ) "Match lifecycle row $($index + 1) did not prove teardown and reload readiness."
    }
}
