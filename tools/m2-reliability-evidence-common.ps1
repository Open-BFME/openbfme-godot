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
    $sampleIntervalMsec = [int]$Soak.memorySampleIntervalMsec
    $samples = @($Soak.memorySamplesBytes)
    $frameSamplesMsec = [double[]]@($Soak.frameSamplesMsec | ForEach-Object { [double]$_ })

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
