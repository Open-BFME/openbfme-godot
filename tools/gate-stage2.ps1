[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$bundleRoot = Join-Path $repoRoot 'content\openbfme-test'
$packPath = Join-Path $bundleRoot 'pack.json'
$projectPath = Join-Path $repoRoot 'engine\OpenBfme.Stage1\OpenBfme.Stage1.csproj'
$gameRoot = Join-Path $repoRoot 'game'
$stage1Gate = Join-Path $PSScriptRoot 'gate-stage1.ps1'
$proofRunner = Join-Path $gameRoot 'tests\stage2_proof_runner.gd'
$visualRunner = Join-Path $gameRoot 'tests\stage2_visual_runner.gd'
$script:FailureOutput = ''

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Read-Json {
    param([string]$Path)
    try { return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json) }
    catch { throw "Invalid JSON in '$Path': $($_.Exception.Message)" }
}

function Get-Property {
    param([object]$Object, [string]$Name, [string]$Context)
    Assert-True ($null -ne $Object) "$Context must be an object."
    $property = $Object.PSObject.Properties[$Name]
    Assert-True ($null -ne $property) "$Context is missing '$Name'."
    return $property.Value
}

function Assert-Properties {
    param([object]$Object, [string[]]$Required, [string[]]$Allowed, [string]$Context)
    foreach ($name in $Required) { [void](Get-Property $Object $name $Context) }
    foreach ($property in $Object.PSObject.Properties) {
        Assert-True ($Allowed -contains $property.Name) "$Context contains unknown property '$($property.Name)'."
    }
}

function Get-Integer {
    param([object]$Value, [string]$Context)
    Assert-True ($null -ne $Value -and ($Value -is [int] -or $Value -is [long])) "$Context must be an integer."
    return [int]$Value
}

function Assert-StableId {
    param([string]$Id, [string]$Context)
    Assert-True ($Id -cmatch '^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)+$') "$Context has invalid stable id '$Id'."
}

function Assert-Position {
    param([object]$Position, [int]$Width, [int]$Height, [bool]$RequireCellCenter, [string]$Context)
    Assert-Properties $Position @('xSubcells', 'ySubcells') @('xSubcells', 'ySubcells') $Context
    $x = Get-Integer $Position.xSubcells "$Context xSubcells"
    $y = Get-Integer $Position.ySubcells "$Context ySubcells"
    Assert-True ($x -ge 0 -and $x -lt $Width -and $y -ge 0 -and $y -lt $Height) "$Context is outside the map."
    if ($RequireCellCenter) {
        Assert-True (($x % 1000) -eq 500 -and ($y % 1000) -eq 500) "$Context must be at a grid-cell center."
    }
}

function Resolve-Godot {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $candidates += $GodotPath }
    foreach ($name in @('OPENBFME_GODOT', 'GODOT_CONSOLE', 'GODOT_EXE', 'GODOT')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($value)) { $candidates += $value }
    }
    $candidates += @(
        'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe',
        'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe'
    )
    foreach ($candidateValue in $candidates) {
        $candidate = ([string]$candidateValue).Trim().Trim('"')
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    throw 'Godot 4.7 was not found. Set OPENBFME_GODOT or pass -GodotPath.'
}

function Invoke-Checked {
    param([string]$Step, [string]$FilePath, [string[]]$Arguments, [string]$Marker, [string]$Forbidden = '')
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $oldPreference }
    $output = $lines -join [Environment]::NewLine
    if ($exitCode -ne 0 -or (-not [string]::IsNullOrWhiteSpace($Marker) -and $output -cnotmatch $Marker) -or (-not [string]::IsNullOrWhiteSpace($Forbidden) -and $output -match $Forbidden)) {
        $script:FailureOutput = $output
        throw "$Step failed."
    }
    Write-Host "STAGE2_GATE $Step PASS"
    return $output
}

function Assert-Stage2Bundle {
    $pack = Read-Json $packPath
    $economyPath = Join-Path $bundleRoot ([string](Get-Property $pack.files 'economy' 'pack files'))
    $mapPath = Join-Path $bundleRoot ([string](Get-Property $pack.files 'stage2Map' 'pack files'))
    $objectsPath = Join-Path $bundleRoot ([string](Get-Property $pack.files 'objects' 'pack files'))
    $economy = Read-Json $economyPath
    $map = Read-Json $mapPath
    $objects = Read-Json $objectsPath

    Assert-Properties $economy @('schema', 'schemaVersion', 'rulesVersion', 'currency', 'rules', 'farmEfficiency', 'sides', 'buildings', 'hordeBlueprints') @('schema', 'schemaVersion', 'rulesVersion', 'currency', 'rules', 'farmEfficiency', 'sides', 'buildings', 'hordeBlueprints') 'economy.json'
    Assert-True ([string]$economy.schema -ceq 'openbfme.economy') 'economy.json schema is unsupported.'
    Assert-True ((Get-Integer $economy.schemaVersion 'economy schemaVersion') -eq 0) 'economy schemaVersion must be 0.'
    Assert-True ((Get-Integer $economy.rulesVersion 'economy rulesVersion') -gt 0) 'economy rulesVersion must be positive.'
    Assert-Properties $economy.currency @('id', 'displayName') @('id', 'displayName') 'economy currency'
    Assert-StableId ([string]$economy.currency.id) 'economy currency'
    Assert-Properties $economy.rules @('maximumTrainQueue', 'constructionHealthRamp', 'buildingBlocksNavigationAt', 'queuedBattalionsCountTowardPopulation', 'spawnSearchMaximumRadiusCells', 'spawnSearchOrder') @('maximumTrainQueue', 'constructionHealthRamp', 'buildingBlocksNavigationAt', 'queuedBattalionsCountTowardPopulation', 'spawnSearchMaximumRadiusCells', 'spawnSearchOrder') 'economy rules'
    $maximumQueue = Get-Integer $economy.rules.maximumTrainQueue 'maximumTrainQueue'
    Assert-True ($maximumQueue -ge 1 -and $maximumQueue -le 16) 'maximumTrainQueue is outside safety limits.'
    Assert-True ([string]$economy.rules.constructionHealthRamp -ceq 'linear-floor-minimum-one') 'Unsupported construction health ramp.'
    Assert-True ([string]$economy.rules.buildingBlocksNavigationAt -ceq 'placement') 'Buildings must block navigation at placement.'
    Assert-True ($economy.rules.queuedBattalionsCountTowardPopulation -eq $true) 'Queued battalions must count toward population.'
    $spawnRadius = Get-Integer $economy.rules.spawnSearchMaximumRadiusCells 'spawnSearchMaximumRadiusCells'
    Assert-True ($spawnRadius -ge 1 -and $spawnRadius -le 32) 'spawn search radius is outside safety limits.'
    Assert-True ((@($economy.rules.spawnSearchOrder) -join ',') -ceq 'north,east,south,west') 'Spawn search order must be north,east,south,west.'

    Assert-Properties $economy.farmEfficiency @('radiusSubcells', 'basePermille', 'penaltyPerNeighborPermille', 'minimumPermille') @('radiusSubcells', 'basePermille', 'penaltyPerNeighborPermille', 'minimumPermille') 'farmEfficiency'
    $efficiencyRadius = Get-Integer $economy.farmEfficiency.radiusSubcells 'farm efficiency radius'
    $basePermille = Get-Integer $economy.farmEfficiency.basePermille 'farm basePermille'
    $penaltyPermille = Get-Integer $economy.farmEfficiency.penaltyPerNeighborPermille 'farm penaltyPermille'
    $minimumPermille = Get-Integer $economy.farmEfficiency.minimumPermille 'farm minimumPermille'
    Assert-True ($efficiencyRadius -gt 0 -and $basePermille -eq 1000 -and $penaltyPermille -gt 0 -and $minimumPermille -gt 0 -and $minimumPermille -le $basePermille) 'Farm efficiency values are invalid.'

    $teams = @{}
    foreach ($side in @($economy.sides)) {
        Assert-Properties $side @('team', 'startingResources', 'populationCap') @('team', 'startingResources', 'populationCap') 'economy side'
        $team = Get-Integer $side.team 'economy side team'
        Assert-True (($team -eq 0 -or $team -eq 1) -and -not $teams.ContainsKey($team)) "Invalid or duplicate economy team '$team'."
        $teams[$team] = $true
        Assert-True ((Get-Integer $side.startingResources 'startingResources') -ge 0) 'startingResources may not be negative.'
        $cap = Get-Integer $side.populationCap 'populationCap'
        Assert-True ($cap -ge 1 -and $cap -le 64) 'populationCap is outside safety limits.'
    }
    Assert-True ($teams.Count -eq 2) 'Economy must define teams 0 and 1.'

    $objectIds = @{}
    foreach ($object in @($objects.objects)) { $objectIds[[string]$object.id] = $true }
    $buildingCodes = @{}
    $menuSlots = @{}
    $buildingRows = @($economy.buildings)
    foreach ($building in $buildingRows) {
        Assert-Properties $building @('typeCode', 'objectId', 'role', 'cost', 'constructionTicks', 'maximumHealth', 'footprint', 'buildMenuSlot', 'income', 'trains') @('typeCode', 'objectId', 'role', 'cost', 'constructionTicks', 'maximumHealth', 'footprint', 'buildMenuSlot', 'income', 'trains') 'economy building'
        $code = Get-Integer $building.typeCode 'building typeCode'
        Assert-True ($code -gt 0 -and -not $buildingCodes.ContainsKey($code)) "Invalid or duplicate building typeCode '$code'."
        $buildingCodes[$code] = $building
        Assert-True ($objectIds.ContainsKey([string]$building.objectId)) "Building typeCode '$code' references an unknown object."
        $role = [string]$building.role
        Assert-True (@('fortress', 'resource', 'production') -contains $role) "Building typeCode '$code' has an invalid role."
        Assert-True ((Get-Integer $building.cost "building '$code' cost") -ge 0) "Building '$code' has a negative cost."
        $constructionTicks = Get-Integer $building.constructionTicks "building '$code' constructionTicks"
        Assert-True (($role -eq 'fortress' -and $constructionTicks -eq 0) -or ($role -ne 'fortress' -and $constructionTicks -gt 0)) "Building '$code' constructionTicks must be zero only for the fortress and positive otherwise."
        Assert-True ((Get-Integer $building.maximumHealth "building '$code' maximumHealth") -gt 0) "Building '$code' has invalid health."
        Assert-Properties $building.footprint @('widthCells', 'heightCells') @('widthCells', 'heightCells') "building '$code' footprint"
        $footWidth = Get-Integer $building.footprint.widthCells "building '$code' footprint width"
        $footHeight = Get-Integer $building.footprint.heightCells "building '$code' footprint height"
        Assert-True ($footWidth -gt 0 -and $footHeight -gt 0 -and $footWidth -le 9 -and $footHeight -le 9 -and ($footWidth % 2) -eq 1 -and ($footHeight % 2) -eq 1) "Building '$code' footprint must be positive, odd, and at most 9x9."
        $menuSlot = Get-Integer $building.buildMenuSlot "building '$code' buildMenuSlot"
        if ($menuSlot -ge 0) {
            Assert-True (-not $menuSlots.ContainsKey($menuSlot)) "Duplicate build menu slot '$menuSlot'."
            $menuSlots[$menuSlot] = $true
        }
        Assert-Properties $building.income @('amount', 'intervalTicks') @('amount', 'intervalTicks') "building '$code' income"
        $incomeAmount = Get-Integer $building.income.amount "building '$code' income amount"
        $incomeInterval = Get-Integer $building.income.intervalTicks "building '$code' income interval"
        Assert-True (($incomeAmount -eq 0 -and $incomeInterval -eq 0) -or ($incomeAmount -gt 0 -and $incomeInterval -gt 0)) "Building '$code' income must use two zeros or two positive values."
    }

    $blueprintCodes = @{}
    $blueprintSlots = @{}
    foreach ($blueprint in @($economy.hordeBlueprints)) {
        Assert-Properties $blueprint @('typeCode', 'id', 'displayName', 'memberCount', 'rangedCount', 'cost', 'productionTicks', 'population', 'trainMenuSlot') @('typeCode', 'id', 'displayName', 'memberCount', 'rangedCount', 'cost', 'productionTicks', 'population', 'trainMenuSlot') 'horde blueprint'
        $code = Get-Integer $blueprint.typeCode 'blueprint typeCode'
        Assert-True ($code -ge 100 -and -not $blueprintCodes.ContainsKey($code)) "Invalid or duplicate blueprint typeCode '$code'."
        $blueprintCodes[$code] = $true
        Assert-StableId ([string]$blueprint.id) "blueprint '$code'"
        $members = Get-Integer $blueprint.memberCount "blueprint '$code' memberCount"
        $ranged = Get-Integer $blueprint.rangedCount "blueprint '$code' rangedCount"
        Assert-True ($members -eq 15 -and $ranged -ge 0 -and $ranged -le $members) "Blueprint '$code' must describe a 15-member horde."
        Assert-True ((Get-Integer $blueprint.cost "blueprint '$code' cost") -gt 0 -and (Get-Integer $blueprint.productionTicks "blueprint '$code' productionTicks") -gt 0 -and (Get-Integer $blueprint.population "blueprint '$code' population") -gt 0) "Blueprint '$code' has invalid production values."
        $slot = Get-Integer $blueprint.trainMenuSlot "blueprint '$code' trainMenuSlot"
        Assert-True ($slot -ge 0) "Blueprint '$code' trainMenuSlot must be nonnegative."
        $blueprintSlots["$code/$slot"] = $true
    }
    foreach ($building in $buildingRows) {
        foreach ($trainCodeValue in @($building.trains)) {
            $trainCode = Get-Integer $trainCodeValue "building '$($building.typeCode)' train code"
            Assert-True ($blueprintCodes.ContainsKey($trainCode)) "Building '$($building.typeCode)' references unknown blueprint '$trainCode'."
        }
    }

    Assert-Properties $map @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') 'stage2 map'
    Assert-True ([string]$map.schema -ceq 'openbfme.map' -and [string]$map.id -ceq 'test.map.primitive-economy') 'Unexpected Stage 2 map schema or id.'
    Assert-Properties $map.grid @('widthCells', 'heightCells', 'cellSizeSubcells', 'defaultTerrainCost', 'neighborOrder') @('widthCells', 'heightCells', 'cellSizeSubcells', 'defaultTerrainCost', 'neighborOrder') 'stage2 grid'
    $widthCells = Get-Integer $map.grid.widthCells 'stage2 grid width'
    $heightCells = Get-Integer $map.grid.heightCells 'stage2 grid height'
    Assert-True ($widthCells -ge 3 -and $heightCells -ge 3 -and ($widthCells * $heightCells) -le 4096 -and (Get-Integer $map.grid.cellSizeSubcells 'cellSizeSubcells') -eq 1000) 'Stage 2 grid is invalid.'
    Assert-True ((@($map.grid.neighborOrder) -join ',') -ceq 'north,east,south,west') 'Stage 2 neighbor order is invalid.'
    $widthSubcells = $widthCells * 1000
    $heightSubcells = $heightCells * 1000
    Assert-Properties $map.terrain @('source', 'base', 'heightSubcells', 'buildable') @('source', 'base', 'heightSubcells', 'buildable') 'stage2 terrain'
    Assert-True ($map.terrain.buildable -eq $true) 'Stage 2 base terrain must be buildable.'
    foreach ($blocker in @($map.staticBlockers)) {
        Assert-Properties $blocker @('id', 'shape', 'xCell', 'yCell', 'widthCells', 'heightCells', 'presentationShape') @('id', 'shape', 'xCell', 'yCell', 'widthCells', 'heightCells', 'presentationShape') 'stage2 blocker'
        Assert-StableId ([string]$blocker.id) 'stage2 blocker'
        $x = Get-Integer $blocker.xCell 'blocker xCell'; $y = Get-Integer $blocker.yCell 'blocker yCell'
        $w = Get-Integer $blocker.widthCells 'blocker width'; $h = Get-Integer $blocker.heightCells 'blocker height'
        Assert-True ($x -ge 0 -and $y -ge 0 -and $w -gt 0 -and $h -gt 0 -and ($x + $w) -le $widthCells -and ($y + $h) -le $heightCells) 'Stage 2 blocker is outside the map.'
    }
    Assert-Properties $map.formation @('id', 'rows', 'columns', 'rowSpacingSubcells', 'columnSpacingSubcells', 'slotAssignment', 'reassignOnlyOn') @('id', 'rows', 'columns', 'rowSpacingSubcells', 'columnSpacingSubcells', 'slotAssignment', 'reassignOnlyOn') 'stage2 formation'
    Assert-True ((Get-Integer $map.formation.rows 'formation rows') * (Get-Integer $map.formation.columns 'formation columns') -eq 15) 'Stage 2 formation must have 15 slots.'
    foreach ($side in @($map.sides)) {
        Assert-Properties $side @('id', 'team', 'teamColor', 'fortress', 'horde') @('id', 'team', 'teamColor', 'fortress', 'horde') 'stage2 side'
        Assert-Properties $side.fortress @('entityId', 'objectId', 'position') @('entityId', 'objectId', 'position') 'stage2 fortress'
        Assert-Properties $side.horde @('entityId', 'firstMemberEntityId', 'anchor', 'formationId', 'composition') @('entityId', 'firstMemberEntityId', 'anchor', 'formationId', 'composition') 'stage2 horde'
        Assert-Position $side.fortress.position $widthSubcells $heightSubcells $true 'stage2 fortress position'
        Assert-Position $side.horde.anchor $widthSubcells $heightSubcells $true 'stage2 horde anchor'
        foreach ($entry in @($side.horde.composition)) { Assert-Properties $entry @('objectId', 'count') @('objectId', 'count') 'stage2 composition' }
    }
    Assert-Properties $map.scenario @('id', 'randomSeed', 'simulationTicks', 'commands', 'requiredObservations') @('id', 'randomSeed', 'simulationTicks', 'commands', 'requiredObservations') 'stage2 scenario'
    $simulationTicks = Get-Integer $map.scenario.simulationTicks 'stage2 simulationTicks'
    Assert-True ($simulationTicks -gt 0 -and $simulationTicks -le 10000) 'Stage 2 simulationTicks is outside safety limits.'
    $sequences = @{}
    foreach ($command in @($map.scenario.commands)) {
        $order = [string](Get-Property $command 'order' 'stage2 command')
        $tick = Get-Integer (Get-Property $command 'executeTick' 'stage2 command') 'stage2 command executeTick'
        $sequence = Get-Integer (Get-Property $command 'sequence' 'stage2 command') 'stage2 command sequence'
        Assert-True ($tick -ge 0 -and $tick -lt $simulationTicks -and -not $sequences.ContainsKey($sequence)) "Invalid Stage 2 command tick/sequence '$sequence'."
        $sequences[$sequence] = $true
        switch ($order) {
            'place-building' {
                Assert-Properties $command @('executeTick', 'sequence', 'order', 'team', 'typeCode', 'destination') @('executeTick', 'sequence', 'order', 'team', 'typeCode', 'destination') "place command '$sequence'"
                $code = Get-Integer $command.typeCode "place command '$sequence' typeCode"
                Assert-True ($buildingCodes.ContainsKey($code) -and (Get-Integer $buildingCodes[$code].buildMenuSlot 'build menu slot') -ge 0) "Place command '$sequence' uses a non-buildable type."
                Assert-Position $command.destination $widthSubcells $heightSubcells $true "place command '$sequence' destination"
            }
            'set-rally' {
                Assert-Properties $command @('executeTick', 'sequence', 'order', 'buildingEntityId', 'destination') @('executeTick', 'sequence', 'order', 'buildingEntityId', 'destination') "rally command '$sequence'"
                Assert-True ((Get-Integer $command.buildingEntityId "rally command '$sequence' building") -ge 200) "Rally command '$sequence' has invalid producer id."
                Assert-Position $command.destination $widthSubcells $heightSubcells $true "rally command '$sequence' destination"
            }
            'train' {
                Assert-Properties $command @('executeTick', 'sequence', 'order', 'buildingEntityId', 'typeCode') @('executeTick', 'sequence', 'order', 'buildingEntityId', 'typeCode') "train command '$sequence'"
                Assert-True ((Get-Integer $command.buildingEntityId "train command '$sequence' building") -ge 200) "Train command '$sequence' has invalid producer id."
                Assert-True ($blueprintCodes.ContainsKey((Get-Integer $command.typeCode "train command '$sequence' typeCode"))) "Train command '$sequence' uses an unknown blueprint."
            }
            'attack-target' {
                Assert-Properties $command @('executeTick', 'sequence', 'order', 'hordeEntityId', 'targetEntityId', 'destination') @('executeTick', 'sequence', 'order', 'hordeEntityId', 'targetEntityId', 'destination') "attack command '$sequence'"
                Assert-True ((Get-Integer $command.hordeEntityId "attack command '$sequence' horde") -ge 100 -and (Get-Integer $command.targetEntityId "attack command '$sequence' target") -gt 0) "Attack command '$sequence' has invalid ids."
                Assert-Position $command.destination $widthSubcells $heightSubcells $true "attack command '$sequence' destination"
            }
            default { throw "Stage 2 scenario contains unsupported order '$order'." }
        }
    }
}

try {
    $stage1Arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $stage1Gate)
    if (-not [string]::IsNullOrWhiteSpace($GodotPath)) { $stage1Arguments += @('-GodotPath', $GodotPath) }
    $stage1Output = Invoke-Checked 'stage1_regression' 'powershell.exe' $stage1Arguments '(?m)^STAGE1_GATE PASS\s*$'
    Assert-Stage2Bundle
    Write-Host 'STAGE2_GATE bundle PASS'

    $dotnet = (Get-Command dotnet -ErrorAction Stop | Select-Object -First 1).Source
    $csharpSelfOutput = Invoke-Checked 'csharp_stage2_self_test' $dotnet @('run', '--project', $projectPath, '--configuration', 'Release', '--no-build', '--', 'stage2-self-test') '(?m)^SUMMARY command=stage2-self-test passed=\d+ failed=0 status=PASS\s*$'
    $csharpOutput = Invoke-Checked 'csharp_stage2_bundle' $dotnet @('run', '--project', $projectPath, '--configuration', 'Release', '--no-build', '--', 'stage2-bundle-test', $bundleRoot) '(?m)^BUNDLE_STAGE2_CSHARP .+\bstatus=PASS\s*$'

    Assert-True (Test-Path -LiteralPath $proofRunner -PathType Leaf) 'Missing Stage 2 Godot proof runner.'
    Assert-True (Test-Path -LiteralPath $visualRunner -PathType Leaf) 'Missing Stage 2 Godot visual runner.'
    $godot = Resolve-Godot
    $forbidden = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'
    $godotOutput = Invoke-Checked 'godot_stage2_proof' $godot @('--headless', '--path', $gameRoot, '-s', 'res://tests/stage2_proof_runner.gd') '(?m)^STAGE2_GODOT_PROOF PASS(?:\s|$)' $forbidden
    $visualOutput = Invoke-Checked 'godot_stage2_visual' $godot @('--headless', '--path', $gameRoot, '-s', 'res://tests/stage2_visual_runner.gd') '(?m)^STAGE2_VISUAL_PROOF PASS(?:\s|$)' $forbidden

    $csharpSelfCount = [regex]::Match($csharpSelfOutput, '(?m)^SUMMARY command=stage2-self-test passed=(\d+) failed=0 status=PASS\s*$')
    $godotProofCount = [regex]::Match($godotOutput, '(?m)^STAGE2_GODOT_PROOF PASS assertions=(\d+)\s*$')
    $visualCount = [regex]::Match($visualOutput, '(?m)^STAGE2_VISUAL_PROOF PASS assertions=(\d+)\s*$')
    Assert-True ($csharpSelfCount.Success -and [int]$csharpSelfCount.Groups[1].Value -ge 7) 'Stage 2 C# assertion coverage regressed below 7.'
    Assert-True ($godotProofCount.Success -and [int]$godotProofCount.Groups[1].Value -ge 30) 'Stage 2 Godot assertion coverage regressed below 30.'
    Assert-True ($visualCount.Success -and [int]$visualCount.Groups[1].Value -ge 38) 'Stage 2 visual assertion coverage regressed below 38.'

    $csharpHash = [regex]::Match($csharpOutput, '(?m)^BUNDLE_STAGE2_CSHARP .+\bhash=([0-9A-F]{8})\b')
    $godotHash = [regex]::Match($godotOutput, '(?m)^BUNDLE_STAGE2_GODOT .+\bhash=([0-9A-F]{8})\b')
    Assert-True ($csharpHash.Success -and $godotHash.Success) 'Stage 2 candidates did not emit comparable hashes.'
    Assert-True ($csharpHash.Groups[1].Value -ceq $godotHash.Groups[1].Value) "Stage 2 hash mismatch: C#=$($csharpHash.Groups[1].Value) Godot=$($godotHash.Groups[1].Value)"
    Write-Host "STAGE2_GATE cross_language_hash PASS hash=$($csharpHash.Groups[1].Value)"
    Write-Host 'STAGE2_GATE PASS'
    exit 0
}
catch {
    Write-Host "STAGE2_GATE FAIL $($_.Exception.Message)" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($script:FailureOutput)) {
        Write-Host 'STAGE2_GATE child-output-begin'
        Write-Host (($script:FailureOutput -split "`r?`n" | Select-Object -Last 80) -join [Environment]::NewLine)
        Write-Host 'STAGE2_GATE child-output-end'
    }
    exit 1
}
