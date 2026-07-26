[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:FailureOutput = ""
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$bundleRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "content\openbfme-test"))
$projectPath = [IO.Path]::GetFullPath((Join-Path $repoRoot "engine\OpenBfme.Stage1\OpenBfme.Stage1.csproj"))
$gameRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "game"))
$godotRunner = [IO.Path]::GetFullPath((Join-Path $gameRoot "tests\stage1_proof_runner.gd"))
$godotVisualRunner = [IO.Path]::GetFullPath((Join-Path $gameRoot "tests\stage1_visual_runner.gd"))

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Get-RequiredProperty {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Context
    )

    Assert-True ($null -ne $Object) "$Context must be an object."
    $property = $Object.PSObject.Properties[$Name]
    Assert-True ($null -ne $property) "$Context is missing required property '$Name'."
    return $property.Value
}

function Assert-OnlyProperties {
    param(
        [object]$Object,
        [string[]]$Required,
        [string[]]$Allowed,
        [string]$Context
    )

    foreach ($name in $Required) {
        [void](Get-RequiredProperty $Object $name $Context)
    }

    foreach ($property in $Object.PSObject.Properties) {
        Assert-True ($Allowed -contains $property.Name) "$Context contains unknown property '$($property.Name)'."
    }
}

function Read-JsonFile {
    param([string]$Path)

    try {
        return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
    }
    catch {
        throw "Invalid JSON in '$Path': $($_.Exception.Message)"
    }
}

function Assert-Schema {
    param(
        [object]$Document,
        [string]$Schema,
        [string]$Context
    )

    $schemaValue = Get-RequiredProperty $Document "schema" $Context
    $versionValue = Get-RequiredProperty $Document "schemaVersion" $Context
    Assert-True ($schemaValue -is [string]) "$Context schema must be a string."
    Assert-True ($null -ne $versionValue -and ($versionValue -is [int] -or $versionValue -is [long])) "$Context schemaVersion must be an integer."
    $actualSchema = [string]$schemaValue
    $version = [int]$versionValue
    Assert-True ($actualSchema -ceq $Schema) "$Context schema must be '$Schema', found '$actualSchema'."
    Assert-True ($version -eq 0) "$Context schemaVersion must be 0, found $version."
}

function Assert-StableId {
    param(
        [string]$Id,
        [string]$Context
    )

    Assert-True ($Id -cmatch '^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)+$') "$Context has invalid stable id '$Id'."
}

function Get-UniqueIds {
    param(
        [object[]]$Items,
        [string]$Context
    )

    $ids = @{}
    foreach ($item in @($Items)) {
        $id = [string](Get-RequiredProperty $item "id" $Context)
        Assert-StableId $id $Context
        Assert-True (-not $ids.ContainsKey($id)) "$Context contains duplicate id '$id'."
        $ids[$id] = $true
    }

    return $ids
}

function Test-SafeRelativeJsonPath {
    param(
        [string]$RelativePath,
        [string]$Root
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or $RelativePath.Length -gt 160) {
        return $false
    }

    if ($RelativePath.Contains('\') -or $RelativePath.StartsWith('/') -or $RelativePath.StartsWith('~')) {
        return $false
    }

    if ([IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '[:%]') {
        return $false
    }

    $parts = $RelativePath.Split('/')
    if ($parts.Count -eq 0) {
        return $false
    }

    foreach ($part in $parts) {
        if ([string]::IsNullOrWhiteSpace($part) -or $part -eq '.' -or $part -eq '..' -or $part -cnotmatch '^[A-Za-z0-9._-]+$') {
            return $false
        }
    }

    if ([IO.Path]::GetExtension($RelativePath) -cne '.json') {
        return $false
    }

    try {
        $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $candidate = [IO.Path]::GetFullPath((Join-Path $Root ($RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
        return $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)
    }
    catch {
        return $false
    }
}

function Assert-PathGuards {
    param([string]$Root)

    $maliciousPaths = @(
        '../outside.json',
        '..\outside.json',
        '/etc/passwd.json',
        'C:/outside.json',
        '\\server\share\outside.json',
        'data//objects.json',
        'data/./objects.json',
        'data/%2e%2e/outside.json',
        'data/payload.ps1'
    )

    foreach ($path in $maliciousPaths) {
        Assert-True (-not (Test-SafeRelativeJsonPath $path $Root)) "Path guard accepted malicious fixture '$path'."
    }

    Assert-True (Test-SafeRelativeJsonPath 'data/objects.json' $Root) "Path guard rejected a valid fixture path."
}

function Assert-Reference {
    param(
        [hashtable]$Ids,
        [string]$Id,
        [string]$Context
    )

    Assert-True ($Ids.ContainsKey($Id)) "$Context references unknown id '$Id'."
}

function Assert-PositionInMap {
    param(
        [object]$Position,
        [int]$WidthSubcells,
        [int]$HeightSubcells,
        [string]$Context
    )

    $x = [int](Get-RequiredProperty $Position "xSubcells" $Context)
    $y = [int](Get-RequiredProperty $Position "ySubcells" $Context)
    Assert-True ($x -ge 0 -and $x -lt $WidthSubcells) "$Context xSubcells is out of bounds."
    Assert-True ($y -ge 0 -and $y -lt $HeightSubcells) "$Context ySubcells is out of bounds."
}

function Get-CanonicalJsonDigest {
    param([string]$Path)

    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    try {
        $text = [IO.File]::ReadAllText($Path, $utf8)
    }
    catch {
        throw "Bundle JSON is not valid UTF-8: $Path"
    }
    if ($text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }
    $text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $bytes = $utf8.GetBytes($text)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $hashBytes = $sha256.ComputeHash($bytes)
    }
    finally {
        $sha256.Dispose()
    }
    $hash = ([BitConverter]::ToString($hashBytes)).Replace('-', '').ToLowerInvariant()
    return [pscustomobject]@{
        Bytes = [long]$bytes.Length
        Hash = $hash
    }
}

function Assert-Bundle {
    param([string]$Root)

    Assert-True (Test-Path -LiteralPath $Root -PathType Container) "Missing Stage 1 bundle: $Root"
    Assert-PathGuards $Root

    $rootItem = Get-Item -LiteralPath $Root -Force
    Assert-True (-not (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) "Bundle root may not be a reparse point."

    $packPath = Join-Path $Root 'pack.json'
    Assert-True (Test-Path -LiteralPath $packPath -PathType Leaf) "Bundle is missing pack.json."
    $pack = Read-JsonFile $packPath
    Assert-Schema $pack 'openbfme.content-pack' 'pack.json'
    Assert-OnlyProperties $pack `
        @('schema', 'schemaVersion', 'id', 'version', 'displayName', 'description', 'contentHashAlgorithm', 'dataPolicy', 'units', 'coordinateSystem', 'limits', 'files', 'requiredCapabilities') `
        @('schema', 'schemaVersion', 'id', 'version', 'displayName', 'description', 'contentHashAlgorithm', 'dataPolicy', 'units', 'coordinateSystem', 'limits', 'files', 'requiredCapabilities') `
        'pack.json'
    Assert-OnlyProperties $pack.dataPolicy @('mode', 'executableScriptsAllowed', 'externalPathsAllowed', 'unknownFields') @('mode', 'executableScriptsAllowed', 'externalPathsAllowed', 'unknownFields') 'pack.json dataPolicy'
    Assert-OnlyProperties $pack.units @('authoritativePosition', 'subcellsPerGridCell', 'time', 'angles') @('authoritativePosition', 'subcellsPerGridCell', 'time', 'angles') 'pack.json units'
    Assert-OnlyProperties $pack.coordinateSystem @('handedness', 'authoritativePlane', 'presentationMapping', 'origin', 'positiveX', 'positiveY') @('handedness', 'authoritativePlane', 'presentationMapping', 'origin', 'positiveX', 'positiveY') 'pack.json coordinateSystem'
    Assert-OnlyProperties $pack.limits @('maximumFiles', 'maximumFileBytes', 'maximumTotalBytes', 'maximumObjects', 'maximumMapCells', 'maximumSpawnedMembers') @('maximumFiles', 'maximumFileBytes', 'maximumTotalBytes', 'maximumObjects', 'maximumMapCells', 'maximumSpawnedMembers') 'pack.json limits'

    Assert-True ([string]$pack.id -ceq 'openbfme-test') "pack.json id must be 'openbfme-test'."
    Assert-True ([string]$pack.contentHashAlgorithm -ceq 'sha256') "pack.json must use sha256."
    Assert-True ([string]$pack.dataPolicy.mode -ceq 'data-only') "The test pack must be data-only."
    Assert-True ($pack.dataPolicy.executableScriptsAllowed -eq $false) "The test pack may not allow executable scripts."
    Assert-True ($pack.dataPolicy.externalPathsAllowed -eq $false) "The test pack may not allow external paths."
    Assert-True ([string]$pack.dataPolicy.unknownFields -ceq 'reject') "Unknown fields must be rejected."
    Assert-True ([int]$pack.units.subcellsPerGridCell -eq 1000) "Stage 1 requires 1,000 subcells per grid cell."

    $expectedFileKeys = @(
        'objects', 'weapons', 'armor', 'locomotion', 'behaviors',
        'animationCapabilities', 'economy', 'defenses', 'champions',
        'powers', 'factionRosters', 'aiStrategies', 'stage8Maps',
        'stage9RingRules', 'entryMap', 'stage2Map', 'provenance'
    )
    $declaredPaths = @('pack.json')
    foreach ($key in $expectedFileKeys) {
        $path = [string](Get-RequiredProperty $pack.files $key 'pack.json files')
        Assert-True (Test-SafeRelativeJsonPath $path $Root) "pack.json contains unsafe path '$path'."
        $declaredPaths += $path
    }

    foreach ($property in $pack.files.PSObject.Properties) {
        Assert-True ($expectedFileKeys -contains $property.Name) "pack.json files contains unknown key '$($property.Name)'."
    }

    Assert-True (($declaredPaths | Select-Object -Unique).Count -eq $declaredPaths.Count) "pack.json declares a duplicate path."
    foreach ($path in $declaredPaths) {
        $fullPath = Join-Path $Root ($path.Replace('/', [IO.Path]::DirectorySeparatorChar))
        Assert-True (Test-Path -LiteralPath $fullPath -PathType Leaf) "Declared bundle file is missing: $path"
    }

    $actualFiles = @(Get-ChildItem -LiteralPath $Root -Recurse -Force -File)
    Assert-True ($actualFiles.Count -le 48) "Bundle exceeds the hard 48-file safety limit."
    $totalBytes = [long]0
    $actualPaths = @()
    $rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    foreach ($file in $actualFiles) {
        Assert-True (-not (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) "Bundle file may not be a reparse point: $($file.FullName)"
        Assert-True ($file.Length -le 131072) "Bundle file exceeds the 128 KiB safety limit: $($file.Name)"
        $totalBytes += $file.Length
        $relativePath = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
        Assert-True (Test-SafeRelativeJsonPath $relativePath $Root) "Bundle contains an unsafe or non-JSON file: $relativePath"
        $actualPaths += $relativePath
    }
    Assert-True ($totalBytes -le 1048576) "Bundle exceeds the 1 MiB total safety limit."
    Assert-True ((($actualPaths | Sort-Object) -join "`n") -ceq (($declaredPaths | Sort-Object) -join "`n")) "Bundle contains undeclared files or omits a declared file."

    $objectsDocument = Read-JsonFile (Join-Path $Root $pack.files.objects)
    $weaponsDocument = Read-JsonFile (Join-Path $Root $pack.files.weapons)
    $armorDocument = Read-JsonFile (Join-Path $Root $pack.files.armor)
    $locomotionDocument = Read-JsonFile (Join-Path $Root $pack.files.locomotion)
    $behaviorsDocument = Read-JsonFile (Join-Path $Root $pack.files.behaviors)
    $animationDocument = Read-JsonFile (Join-Path $Root $pack.files.animationCapabilities)
    $economyDocument = Read-JsonFile (Join-Path $Root $pack.files.economy)
    $defensesDocument = Read-JsonFile (Join-Path $Root $pack.files.defenses)
    $championsDocument = Read-JsonFile (Join-Path $Root $pack.files.champions)
    $mapDocument = Read-JsonFile (Join-Path $Root $pack.files.entryMap)
    $stage2MapDocument = Read-JsonFile (Join-Path $Root $pack.files.stage2Map)
    $provenanceDocument = Read-JsonFile (Join-Path $Root $pack.files.provenance)

    Assert-Schema $objectsDocument 'openbfme.objects' 'objects.json'
    Assert-Schema $weaponsDocument 'openbfme.weapons' 'weapons.json'
    Assert-Schema $armorDocument 'openbfme.armor' 'armor.json'
    Assert-Schema $locomotionDocument 'openbfme.locomotion' 'locomotion.json'
    Assert-Schema $behaviorsDocument 'openbfme.behaviors' 'behaviors.json'
    Assert-Schema $animationDocument 'openbfme.animation-capabilities' 'animation_capabilities.json'
    Assert-Schema $economyDocument 'openbfme.economy' 'economy.json'
    Assert-Schema $defensesDocument 'openbfme.defenses' 'defenses.json'
    Assert-Schema $championsDocument 'openbfme.champions' 'champions.json'
    Assert-Schema $mapDocument 'openbfme.map' 'map.json'
    Assert-Schema $stage2MapDocument 'openbfme.map' 'stage2 map.json'
    Assert-Schema $provenanceDocument 'openbfme.provenance-manifest' 'provenance manifest'

    Assert-OnlyProperties $objectsDocument @('schema', 'schemaVersion', 'objects') @('schema', 'schemaVersion', 'objects') 'objects.json'
    Assert-OnlyProperties $weaponsDocument @('schema', 'schemaVersion', 'damageTypes', 'weapons') @('schema', 'schemaVersion', 'damageTypes', 'weapons') 'weapons.json'
    Assert-OnlyProperties $armorDocument @('schema', 'schemaVersion', 'armor') @('schema', 'schemaVersion', 'armor') 'armor.json'
    Assert-OnlyProperties $locomotionDocument @('schema', 'schemaVersion', 'locomotion') @('schema', 'schemaVersion', 'locomotion') 'locomotion.json'
    Assert-OnlyProperties $behaviorsDocument @('schema', 'schemaVersion', 'behaviorSets') @('schema', 'schemaVersion', 'behaviorSets') 'behaviors.json'
    Assert-OnlyProperties $animationDocument @('schema', 'schemaVersion', 'capabilities') @('schema', 'schemaVersion', 'capabilities') 'animation_capabilities.json'
    Assert-OnlyProperties $economyDocument @('schema', 'schemaVersion', 'rulesVersion', 'currency', 'rules', 'farmEfficiency', 'sides', 'buildings', 'hordeBlueprints') @('schema', 'schemaVersion', 'rulesVersion', 'currency', 'rules', 'farmEfficiency', 'sides', 'buildings', 'hordeBlueprints') 'economy.json'
    Assert-OnlyProperties $mapDocument @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') 'map.json'
    Assert-OnlyProperties $stage2MapDocument @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') @('schema', 'schemaVersion', 'id', 'displayName', 'grid', 'terrain', 'staticBlockers', 'formation', 'sides', 'scenario') 'stage2 map.json'
    Assert-OnlyProperties $provenanceDocument @('schema', 'schemaVersion', 'packId', 'createdUtc', 'origin', 'assetInventory', 'licenseDecision', 'hashAlgorithm', 'hashCanonicalization', 'files') @('schema', 'schemaVersion', 'packId', 'createdUtc', 'origin', 'assetInventory', 'licenseDecision', 'hashAlgorithm', 'hashCanonicalization', 'files') 'provenance manifest'

    $objectIds = Get-UniqueIds @($objectsDocument.objects) 'objects.json objects'
    $weaponIds = Get-UniqueIds @($weaponsDocument.weapons) 'weapons.json weapons'
    $armorIds = Get-UniqueIds @($armorDocument.armor) 'armor.json armor'
    $locomotionIds = Get-UniqueIds @($locomotionDocument.locomotion) 'locomotion.json locomotion'
    $behaviorIds = Get-UniqueIds @($behaviorsDocument.behaviorSets) 'behaviors.json behaviorSets'
    $animationIds = Get-UniqueIds @($animationDocument.capabilities) 'animation_capabilities.json capabilities'

    Assert-True ($objectIds.Count -le 96) "Bundle exceeds the 96-object limit."
    foreach ($object in @($objectsDocument.objects)) {
        $context = "object '$($object.id)'"
        Assert-OnlyProperties $object @('id', 'kind', 'displayName', 'maximumHealth', 'collisionRadiusSubcells', 'armorId', 'locomotionId', 'behaviorSetId', 'weaponIds', 'animationCapabilityId', 'presentation') @('id', 'kind', 'displayName', 'maximumHealth', 'collisionRadiusSubcells', 'armorId', 'locomotionId', 'behaviorSetId', 'weaponIds', 'animationCapabilityId', 'presentation') $context
        Assert-OnlyProperties $object.presentation @('source', 'shape', 'material') @('source', 'shape', 'radiusMillimeters', 'heightMillimeters', 'widthMillimeters', 'depthMillimeters', 'material') "$context presentation"
        Assert-Reference $armorIds ([string]$object.armorId) $context
        Assert-Reference $locomotionIds ([string]$object.locomotionId) $context
        Assert-Reference $behaviorIds ([string]$object.behaviorSetId) $context
        Assert-Reference $animationIds ([string]$object.animationCapabilityId) $context
        foreach ($weaponId in @($object.weaponIds)) {
            Assert-Reference $weaponIds ([string]$weaponId) $context
        }
        Assert-True ([string]$object.presentation.source -ceq 'generated-primitive') "$context must use a generated primitive."
        Assert-True ($null -eq $object.presentation.PSObject.Properties['path']) "$context may not declare an asset path."
    }

    $damageTypes = @{}
    foreach ($damageType in @($weaponsDocument.damageTypes)) {
        Assert-True ([string]$damageType -cmatch '^[a-z][a-z0-9-]+$') "Invalid damage type '$damageType'."
        Assert-True (-not $damageTypes.ContainsKey([string]$damageType)) "Duplicate damage type '$damageType'."
        $damageTypes[[string]$damageType] = $true
    }
    foreach ($weapon in @($weaponsDocument.weapons)) {
        Assert-OnlyProperties $weapon @('id', 'delivery', 'damageType', 'damage', 'minimumRangeSubcells', 'maximumRangeSubcells', 'cooldownTicks', 'windupTicks', 'validTargetKinds') @('id', 'delivery', 'damageType', 'damage', 'minimumRangeSubcells', 'maximumRangeSubcells', 'cooldownTicks', 'windupTicks', 'projectile', 'validTargetKinds') "weapon '$($weapon.id)'"
        if ($null -ne $weapon.PSObject.Properties['projectile']) {
            Assert-OnlyProperties $weapon.projectile @('speedSubcellsPerTick', 'collisionRadiusSubcells', 'tracking', 'maximumLifetimeTicks', 'presentationShape') @('speedSubcellsPerTick', 'collisionRadiusSubcells', 'tracking', 'maximumLifetimeTicks', 'presentationShape') "weapon '$($weapon.id)' projectile"
        }
        Assert-True ($damageTypes.ContainsKey([string]$weapon.damageType)) "Weapon '$($weapon.id)' references an unknown damage type."
        Assert-True ([int]$weapon.damage -gt 0 -and [int]$weapon.damage -le 10000) "Weapon '$($weapon.id)' damage is outside safety limits."
        Assert-True ([int]$weapon.cooldownTicks -gt 0) "Weapon '$($weapon.id)' cooldown must be positive."
    }

    foreach ($armor in @($armorDocument.armor)) {
        Assert-OnlyProperties $armor @('id', 'damageMultipliers') @('id', 'damageMultipliers') "armor '$($armor.id)'"
        foreach ($property in $armor.damageMultipliers.PSObject.Properties) {
            Assert-True ($damageTypes.ContainsKey($property.Name)) "Armor '$($armor.id)' has unknown damage type '$($property.Name)'."
            Assert-OnlyProperties $property.Value @('numerator', 'denominator') @('numerator', 'denominator') "armor '$($armor.id)' multiplier '$($property.Name)'"
        }
    }
    foreach ($locomotion in @($locomotionDocument.locomotion)) {
        Assert-OnlyProperties $locomotion @('id', 'mode', 'maximumSpeedSubcellsPerTick', 'catchUpSpeedSubcellsPerTick', 'maximumTurnMillidegreesPerTick', 'footprintClass', 'neighborOrder', 'pathTieBreakOrder') @('id', 'mode', 'maximumSpeedSubcellsPerTick', 'hordeAnchorSpeedSubcellsPerTick', 'catchUpSpeedSubcellsPerTick', 'maximumTurnMillidegreesPerTick', 'footprintClass', 'neighborOrder', 'pathTieBreakOrder') "locomotion '$($locomotion.id)'"
    }
    foreach ($behaviorSet in @($behaviorsDocument.behaviorSets)) {
        Assert-OnlyProperties $behaviorSet @('id', 'behaviors') @('id', 'behaviors') "behavior set '$($behaviorSet.id)'"
        foreach ($behavior in @($behaviorSet.behaviors)) {
            Assert-OnlyProperties $behavior @('kind') @('kind', 'assignmentOrder', 'softCohesionRadiusSubcells', 'hardCohesionRadiusSubcells', 'maximumSeparationCorrectionSubcellsPerTick', 'scanIntervalTicks', 'scanRadiusSubcells', 'tieBreakOrder', 'contactSlotsPerTarget', 'allocationOrder', 'retargetAfterLaunch', 'footprintClass', 'defeatWhenDestroyed', 'resolutionDelayTicks') "behavior '$($behavior.kind)'"
        }
    }
    foreach ($capability in @($animationDocument.capabilities)) {
        Assert-OnlyProperties $capability @('id', 'presentationMode', 'states', 'rootMotion', 'events') @('id', 'presentationMode', 'states', 'rootMotion', 'events') "animation '$($capability.id)'"
        foreach ($stateProperty in $capability.states.PSObject.Properties) {
            Assert-True (@('idle', 'move', 'attack', 'death') -contains $stateProperty.Name) "Animation '$($capability.id)' has unknown state '$($stateProperty.Name)'."
            Assert-OnlyProperties $stateProperty.Value @('required', 'loop', 'fallback') @('required', 'loop', 'fallback') "animation '$($capability.id)' state '$($stateProperty.Name)'"
        }
    }

    Assert-True ([string]$mapDocument.id -ceq 'test.map.primitive-arena') "Unexpected Stage 1 entry map id."
    Assert-OnlyProperties $mapDocument.grid @('widthCells', 'heightCells', 'cellSizeSubcells', 'defaultTerrainCost', 'neighborOrder') @('widthCells', 'heightCells', 'cellSizeSubcells', 'defaultTerrainCost', 'neighborOrder') 'map grid'
    Assert-OnlyProperties $mapDocument.terrain @('source', 'base', 'heightSubcells', 'buildable') @('source', 'base', 'heightSubcells', 'buildable') 'map terrain'
    $width = [int]$mapDocument.grid.widthCells
    $height = [int]$mapDocument.grid.heightCells
    $cellSize = [int]$mapDocument.grid.cellSizeSubcells
    Assert-True ($width -ge 3 -and $height -ge 3 -and ($width * $height) -le 4096) "Map dimensions are outside safety limits."
    Assert-True ($cellSize -eq 1000) "Map cell size must match pack units."
    $widthSubcells = $width * $cellSize
    $heightSubcells = $height * $cellSize

    [void](Get-UniqueIds @($mapDocument.staticBlockers) 'map staticBlockers')
    foreach ($blocker in @($mapDocument.staticBlockers)) {
        Assert-OnlyProperties $blocker @('id', 'shape', 'xCell', 'yCell', 'widthCells', 'heightCells', 'presentationShape') @('id', 'shape', 'xCell', 'yCell', 'widthCells', 'heightCells', 'presentationShape') "blocker '$($blocker.id)'"
        $x = [int]$blocker.xCell
        $y = [int]$blocker.yCell
        $blockWidth = [int]$blocker.widthCells
        $blockHeight = [int]$blocker.heightCells
        Assert-True ($blockWidth -gt 0 -and $blockHeight -gt 0) "Blocker '$($blocker.id)' has an empty rectangle."
        Assert-True ($x -ge 0 -and $y -ge 0 -and ($x + $blockWidth) -le $width -and ($y + $blockHeight) -le $height) "Blocker '$($blocker.id)' is outside the map."
    }

    Assert-StableId ([string]$mapDocument.formation.id) 'map formation'
    Assert-OnlyProperties $mapDocument.formation @('id', 'rows', 'columns', 'rowSpacingSubcells', 'columnSpacingSubcells', 'slotAssignment', 'reassignOnlyOn') @('id', 'rows', 'columns', 'rowSpacingSubcells', 'columnSpacingSubcells', 'slotAssignment', 'reassignOnlyOn') 'map formation'
    $formationSlots = [int]$mapDocument.formation.rows * [int]$mapDocument.formation.columns
    Assert-True ($formationSlots -eq 15) "Stage 1 formation must provide exactly 15 slots."

    $sides = @($mapDocument.sides)
    Assert-True ($sides.Count -eq 2) "Stage 1 map must contain exactly two sides."
    [void](Get-UniqueIds $sides 'map sides')
    $teams = @($sides | ForEach-Object { [int]$_.team } | Sort-Object)
    Assert-True (($teams -join ',') -ceq '0,1') "Stage 1 side teams must be exactly 0 and 1."

    $entityIds = @{}
    $hordeEntityIds = @{}
    $spawnedMembers = 0
    foreach ($side in $sides) {
        Assert-OnlyProperties $side @('id', 'team', 'teamColor', 'fortress', 'horde') @('id', 'team', 'teamColor', 'fortress', 'horde') "side '$($side.id)'"
        Assert-OnlyProperties $side.fortress @('entityId', 'objectId', 'position') @('entityId', 'objectId', 'position') "side '$($side.id)' fortress"
        Assert-OnlyProperties $side.horde @('entityId', 'firstMemberEntityId', 'anchor', 'formationId', 'composition') @('entityId', 'firstMemberEntityId', 'anchor', 'formationId', 'composition') "side '$($side.id)' horde"
        Assert-OnlyProperties $side.fortress.position @('xSubcells', 'ySubcells') @('xSubcells', 'ySubcells') "side '$($side.id)' fortress position"
        Assert-OnlyProperties $side.horde.anchor @('xSubcells', 'ySubcells') @('xSubcells', 'ySubcells') "side '$($side.id)' horde anchor"
        Assert-PositionInMap $side.fortress.position $widthSubcells $heightSubcells "side '$($side.id)' fortress"
        Assert-PositionInMap $side.horde.anchor $widthSubcells $heightSubcells "side '$($side.id)' horde"
        Assert-Reference $objectIds ([string]$side.fortress.objectId) "side '$($side.id)' fortress"
        Assert-True ([string]$side.horde.formationId -ceq [string]$mapDocument.formation.id) "side '$($side.id)' references an unknown formation."

        foreach ($entityId in @([int]$side.fortress.entityId, [int]$side.horde.entityId)) {
            Assert-True ($entityId -gt 0) "Entity ids must be positive."
            Assert-True (-not $entityIds.ContainsKey($entityId)) "Duplicate entity id '$entityId'."
            $entityIds[$entityId] = $true
        }
        $hordeEntityIds[[int]$side.horde.entityId] = $true

        $memberCount = 0
        foreach ($entry in @($side.horde.composition)) {
            Assert-OnlyProperties $entry @('objectId', 'count') @('objectId', 'count') "side '$($side.id)' composition"
            Assert-Reference $objectIds ([string]$entry.objectId) "side '$($side.id)' composition"
            $count = [int]$entry.count
            Assert-True ($count -gt 0) "Composition counts must be positive."
            $memberCount += $count
        }
        Assert-True ($memberCount -eq $formationSlots) "Each Stage 1 horde must fill all 15 stable slots."
        $spawnedMembers += $memberCount

        $firstMemberId = [int]$side.horde.firstMemberEntityId
        for ($offset = 0; $offset -lt $memberCount; $offset++) {
            $memberId = $firstMemberId + $offset
            Assert-True ($memberId -gt 0 -and -not $entityIds.ContainsKey($memberId)) "Duplicate or invalid member entity id '$memberId'."
            $entityIds[$memberId] = $true
        }
    }
    Assert-True ($spawnedMembers -le 256) "Map exceeds the 256-member spawn limit."

    $sequences = @{}
    Assert-OnlyProperties $mapDocument.scenario @('id', 'randomSeed', 'simulationTicks', 'commands', 'requiredObservations') @('id', 'randomSeed', 'simulationTicks', 'commands', 'requiredObservations') 'map scenario'
    foreach ($command in @($mapDocument.scenario.commands)) {
        Assert-OnlyProperties $command @('executeTick', 'sequence', 'hordeEntityId', 'order', 'destination') @('executeTick', 'sequence', 'hordeEntityId', 'order', 'destination', 'targetEntityId') "scenario command '$($command.sequence)'"
        Assert-OnlyProperties $command.destination @('xSubcells', 'ySubcells') @('xSubcells', 'ySubcells') "scenario command '$($command.sequence)' destination"
        $sequence = [int]$command.sequence
        Assert-True (-not $sequences.ContainsKey($sequence)) "Scenario contains duplicate command sequence '$sequence'."
        $sequences[$sequence] = $true
        Assert-True ($hordeEntityIds.ContainsKey([int]$command.hordeEntityId)) "Scenario command references an unknown horde."
        Assert-PositionInMap $command.destination $widthSubcells $heightSubcells "scenario command '$sequence'"
    }

    Assert-True ([string]$provenanceDocument.packId -ceq 'openbfme-test') "Provenance packId does not match pack.json."
    Assert-OnlyProperties $provenanceDocument.origin @('classification', 'description', 'containsRetailAssets', 'containsConvertedRetailAssets', 'containsDonorAssets', 'containsThirdPartyAssets', 'requiresExternalInstallation', 'referencesRetailPaths') @('classification', 'description', 'containsRetailAssets', 'containsConvertedRetailAssets', 'containsDonorAssets', 'containsThirdPartyAssets', 'requiresExternalInstallation', 'referencesRetailPaths') 'provenance origin'
    Assert-OnlyProperties $provenanceDocument.assetInventory @('models', 'textures', 'audio', 'video', 'fonts', 'scripts', 'note') @('models', 'textures', 'audio', 'video', 'fonts', 'scripts', 'note') 'provenance assetInventory'
    Assert-OnlyProperties $provenanceDocument.licenseDecision @('status', 'note') @('status', 'note') 'provenance licenseDecision'
    Assert-True ([string]$provenanceDocument.origin.classification -ceq 'repository-authored-original-test-data') "Provenance must classify the pack as repository-authored original test data."
    foreach ($flag in @('containsRetailAssets', 'containsConvertedRetailAssets', 'containsDonorAssets', 'containsThirdPartyAssets', 'requiresExternalInstallation', 'referencesRetailPaths')) {
        Assert-True ((Get-RequiredProperty $provenanceDocument.origin $flag 'provenance origin') -eq $false) "Provenance flag '$flag' must be false."
    }
    Assert-True ([int]$provenanceDocument.assetInventory.models -eq 0 -and [int]$provenanceDocument.assetInventory.textures -eq 0 -and [int]$provenanceDocument.assetInventory.audio -eq 0 -and [int]$provenanceDocument.assetInventory.scripts -eq 0) "The primitive pack may not inventory external assets or scripts."
    Assert-True ([string]$provenanceDocument.licenseDecision.status -ceq 'not-selected-by-proof-stages') "Proof stages may not choose an engine/content license."
    Assert-True ([string]$provenanceDocument.hashAlgorithm -ceq 'sha256') "Provenance hashes must use sha256."
    Assert-True ([string]$provenanceDocument.hashCanonicalization -ceq 'UTF-8 JSON text; optional BOM removed; CRLF and CR normalized to LF') "Provenance hash canonicalization is missing or unsupported."

    $hashedPaths = @{}
    foreach ($entry in @($provenanceDocument.files)) {
        Assert-OnlyProperties $entry @('path', 'canonicalBytes', 'sha256') @('path', 'canonicalBytes', 'sha256') 'provenance file entry'
        $path = [string](Get-RequiredProperty $entry 'path' 'provenance file entry')
        Assert-True (Test-SafeRelativeJsonPath $path $Root) "Provenance contains unsafe path '$path'."
        Assert-True ($path -cne [string]$pack.files.provenance) "Provenance manifest may not recursively hash itself."
        Assert-True (-not $hashedPaths.ContainsKey($path)) "Provenance contains duplicate path '$path'."
        $hashedPaths[$path] = $true
        $filePath = Join-Path $Root ($path.Replace('/', [IO.Path]::DirectorySeparatorChar))
        Assert-True (Test-Path -LiteralPath $filePath -PathType Leaf) "Provenance hash target is missing: $path"
        $digest = Get-CanonicalJsonDigest $filePath
        Assert-True ([long]$entry.canonicalBytes -eq $digest.Bytes) "Provenance canonical byte count does not match '$path'."
        Assert-True ([string]$entry.sha256 -ceq $digest.Hash) "Provenance hash does not match '$path'."
    }

    $expectedHashedPaths = @($declaredPaths | Where-Object { $_ -cne [string]$pack.files.provenance })
    Assert-True ((($hashedPaths.Keys | Sort-Object) -join "`n") -ceq (($expectedHashedPaths | Sort-Object) -join "`n")) "Provenance must hash every bundle file except itself."
}

function Resolve-GodotExecutable {
    param([string]$RequestedPath)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $candidates += $RequestedPath
    }
    foreach ($environmentName in @('OPENBFME_GODOT', 'GODOT_CONSOLE', 'GODOT_EXE', 'GODOT')) {
        $value = [Environment]::GetEnvironmentVariable($environmentName)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            $candidates += $value
        }
    }
    $candidates += @(
        (Join-Path $repoRoot '.tools\godot\Godot_v4.7-stable_win64_console.exe'),
        (Join-Path $repoRoot '.tools\godot\Godot_v4.7-stable_win64.exe'),
        "$env:USERPROFILE\Downloads\godot47\Godot_v4.7-stable_win64_console.exe",
        "$env:USERPROFILE\Downloads\godot47\Godot_v4.7-stable_win64.exe"
    )

    foreach ($candidateValue in $candidates) {
        $candidate = ([string]$candidateValue).Trim().Trim('"')
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    foreach ($commandName in @('godot4.7', 'godot4', 'godot')) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command) {
            return $command.Source
        }
    }

    throw "Godot 4.7 was not found. Set OPENBFME_GODOT or pass -GodotPath."
}

function Get-OutputTail {
    param(
        [string]$Text,
        [int]$MaximumLines = 60
    )

    $lines = @($Text -split "`r?`n")
    if ($lines.Count -le $MaximumLines) {
        return $Text.TrimEnd()
    }
    return (($lines | Select-Object -Last $MaximumLines) -join [Environment]::NewLine).TrimEnd()
}

function Invoke-CheckedNative {
    param(
        [string]$Step,
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$RequiredMarker = '',
        [string]$ForbiddenPattern = ''
    )

    $oldPreference = $ErrorActionPreference
    $lines = @()
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    $text = $lines -join [Environment]::NewLine
    if ($exitCode -ne 0) {
        $script:FailureOutput = $text
        throw "$Step exited with code $exitCode."
    }
    if (-not [string]::IsNullOrWhiteSpace($RequiredMarker) -and $text -cnotmatch $RequiredMarker) {
        $script:FailureOutput = $text
        throw "$Step did not emit its required PASS marker."
    }
    if (-not [string]::IsNullOrWhiteSpace($ForbiddenPattern) -and $text -match $ForbiddenPattern) {
        $script:FailureOutput = $text
        throw "$Step emitted a forbidden diagnostic: '$($Matches[0])'."
    }

    Write-Host "STAGE1_GATE $Step PASS"
    return $text
}

try {
    Assert-Bundle $bundleRoot
    Write-Host 'STAGE1_GATE bundle PASS'

    Assert-True (Test-Path -LiteralPath $projectPath -PathType Leaf) "Missing Stage 1 C# project: $projectPath"
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue | Select-Object -First 1
    Assert-True ($null -ne $dotnet) "dotnet was not found on PATH."

    [void](Invoke-CheckedNative 'dotnet_version' $dotnet.Source @('--version') '(?m)^10\.0\.100\s*$')
    [void](Invoke-CheckedNative 'csharp_build' $dotnet.Source @('build', $projectPath, '--configuration', 'Release', '--nologo'))
    [void](Invoke-CheckedNative 'csharp_self_test' $dotnet.Source @('run', '--project', $projectPath, '--configuration', 'Release', '--no-build', '--', 'self-test') '(?m)^SUMMARY command=self-test passed=\d+ failed=0 status=PASS\s*$')
    [void](Invoke-CheckedNative 'csharp_benchmark' $dotnet.Source @('run', '--project', $projectPath, '--configuration', 'Release', '--no-build', '--', 'benchmark') '(?m)^BENCHMARK .+\bstatus=PASS\s*$')
    $csharpBundleOutput = Invoke-CheckedNative 'csharp_bundle' $dotnet.Source @('run', '--project', $projectPath, '--configuration', 'Release', '--no-build', '--', 'bundle-test', $bundleRoot) '(?m)^BUNDLE_CSHARP .+\bstatus=PASS\s*$'

    Assert-True (Test-Path -LiteralPath $godotRunner -PathType Leaf) "Missing Godot Stage 1 runner: $godotRunner"
    Assert-True (Test-Path -LiteralPath $godotVisualRunner -PathType Leaf) "Missing Godot Stage 1 visual runner: $godotVisualRunner"
    $godot = Resolve-GodotExecutable $GodotPath
    $godotForbidden = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'
    [void](Invoke-CheckedNative 'godot_version' $godot @('--version') '(?m)^4\.7\.' $godotForbidden)
    $godotProofOutput = Invoke-CheckedNative 'godot_proof' $godot @('--headless', '--path', $gameRoot, '-s', 'res://tests/stage1_proof_runner.gd') '(?m)^STAGE1_GODOT_PROOF PASS(?:\s|$)' $godotForbidden
    [void](Invoke-CheckedNative 'godot_visual' $godot @('--headless', '--path', $gameRoot, '-s', 'res://tests/stage1_visual_runner.gd') '(?m)^STAGE1_VISUAL_PROOF PASS(?:\s|$)' $godotForbidden)

    $csharpHashMatch = [regex]::Match($csharpBundleOutput, '(?m)^BUNDLE_CSHARP .+\bhash=([0-9A-F]{8})\b')
    $godotHashMatch = [regex]::Match($godotProofOutput, '(?m)^BUNDLE_GODOT .+\bhash=([0-9A-F]{8})\b')
    Assert-True ($csharpHashMatch.Success -and $godotHashMatch.Success) 'Bundle candidates did not emit comparable hashes.'
    Assert-True ($csharpHashMatch.Groups[1].Value -ceq $godotHashMatch.Groups[1].Value) "Bundle candidate hash mismatch: C#=$($csharpHashMatch.Groups[1].Value) Godot=$($godotHashMatch.Groups[1].Value)"
    Write-Host "STAGE1_GATE cross_language_hash PASS hash=$($csharpHashMatch.Groups[1].Value)"

    Write-Host 'STAGE1_GATE PASS'
    exit 0
}
catch {
    $failureLocation = ''
    if ($_.InvocationInfo.ScriptLineNumber -gt 0) {
        $failureLocation = " line=$($_.InvocationInfo.ScriptLineNumber)"
    }
    Write-Host "STAGE1_GATE FAIL$failureLocation $($_.Exception.Message)" -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($script:FailureOutput)) {
        Write-Host 'STAGE1_GATE child-output-begin'
        Write-Host (Get-OutputTail $script:FailureOutput)
        Write-Host 'STAGE1_GATE child-output-end'
    }
    exit 1
}
