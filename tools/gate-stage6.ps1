[CmdletBinding()]
param(
    [string]$GodotPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "STAGE6_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$rostersPath = Join-Path $repoRoot "content\openbfme-test\data\faction_rosters.json"
$priorGate = Join-Path $PSScriptRoot "gate-stage5.ps1"
$proofRunner = Join-Path $gameRoot "tests\stage6_proof_runner.gd"
$visualRunner = Join-Path $gameRoot "tests\stage6_visual_runner.gd"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Assert-FactionRosterDocument {
    $root = Read-ProofJson $rostersPath
    $rootFields = @("schema", "schemaVersion", "rulesVersion", "damageTypes", "armorClasses", "upgrades", "factions")
    Assert-ProofProperties $root $rootFields $rootFields "faction_rosters.json"
    Assert-ProofTrue ([string]$root.schema -ceq "openbfme.faction-rosters") "faction_rosters.json schema is unsupported."
    Assert-ProofTrue ((Test-ProofInteger $root.schemaVersion) -and [int]$root.schemaVersion -eq 0) "faction_rosters.json schemaVersion must be 0."
    Assert-ProofTrue ((Test-ProofInteger $root.rulesVersion) -and [int]$root.rulesVersion -eq 1) "faction_rosters.json rulesVersion must be 1."

    $damageTypes = @($root.damageTypes)
    Assert-ProofTrue ($damageTypes.Count -eq 3 -and @($damageTypes | Select-Object -Unique).Count -eq 3) "Stage 6 must define exactly three unique damage types."
    foreach ($damageType in $damageTypes) {
        Assert-ProofTrue ([string]$damageType -cmatch '^[a-z][a-z0-9_-]+$') "Damage type identifiers must be stable lowercase values."
    }

    $armorFields = @("id", "multipliers")
    $ratioFields = @("numerator", "denominator")
    $armorIds = @{}
    $armorClasses = @($root.armorClasses)
    Assert-ProofTrue ($armorClasses.Count -eq 3) "Stage 6 must define exactly three armor classes."
    foreach ($armor in $armorClasses) {
        Assert-ProofProperties $armor $armorFields $armorFields "armor class"
        $armorId = [string]$armor.id
        Assert-ProofTrue ($armorId -cmatch '^[a-z][a-z0-9_-]+$' -and -not $armorIds.ContainsKey($armorId)) "Armor ids must be unique stable identifiers."
        $armorIds[$armorId] = $true
        Assert-ProofProperties $armor.multipliers $damageTypes $damageTypes "armor multipliers"
        foreach ($damageType in $damageTypes) {
            $ratio = $armor.multipliers.PSObject.Properties[[string]$damageType].Value
            Assert-ProofProperties $ratio $ratioFields $ratioFields "damage multiplier"
            Assert-ProofTrue ((Test-ProofInteger $ratio.numerator) -and [int]$ratio.numerator -gt 0) "Damage multiplier numerator must be positive."
            Assert-ProofTrue ((Test-ProofInteger $ratio.denominator) -and [int]$ratio.denominator -gt 0) "Damage multiplier denominator must be positive."
        }
    }

    $upgradeFields = @("id", "displayName", "cost", "researchTicks", "damageType", "damageBonusPermille", "armorBonusPermille")
    $upgradeIds = @{}
    $upgrades = @($root.upgrades)
    Assert-ProofTrue ($upgrades.Count -eq 3) "Stage 6 must define exactly three research upgrades."
    foreach ($upgrade in $upgrades) {
        Assert-ProofProperties $upgrade $upgradeFields $upgradeFields "upgrade"
        $upgradeId = [string]$upgrade.id
        Assert-ProofTrue ($upgradeId -cmatch '^[a-z][a-z0-9_]+$' -and -not $upgradeIds.ContainsKey($upgradeId)) "Upgrade ids must be unique stable identifiers."
        $upgradeIds[$upgradeId] = $true
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$upgrade.displayName)) "Upgrade displayName must not be empty."
        Assert-ProofTrue ($damageTypes -contains [string]$upgrade.damageType) "Upgrade damageType must reference the declared matrix."
        foreach ($field in @("cost", "researchTicks", "damageBonusPermille", "armorBonusPermille")) {
            Assert-ProofTrue ((Test-ProofInteger $upgrade.$field) -and [int]$upgrade.$field -ge 0) "Upgrade $field must be a nonnegative integer."
        }
        Assert-ProofTrue ([int]$upgrade.researchTicks -gt 0) "Upgrade researchTicks must be positive."
    }

    $factionFields = @("id", "displayName", "teamColor", "startingResources", "upgradeIds", "roster")
    $unitFields = @("unitId", "displayName", "armorClass", "damageType", "baseDamage", "maximumHealth", "rangeCells", "cooldownTicks", "art")
    $artFields = @("source", "shape", "material")
    $allowedShapes = @("box", "capsule", "cylinder", "diamond", "hexagon", "sphere")
    $factionIds = @{}
    $unitIds = @{}
    $rosterCount = 0
    $factions = @($root.factions)
    Assert-ProofTrue ($factions.Count -eq 4) "Stage 6 must define exactly four selectable legal-safe factions."
    foreach ($faction in $factions) {
        Assert-ProofProperties $faction $factionFields $factionFields "faction"
        $factionId = [string]$faction.id
        Assert-ProofTrue ($factionId -cmatch '^[a-z][a-z0-9_]+$' -and -not $factionIds.ContainsKey($factionId)) "Faction ids must be unique stable identifiers."
        $factionIds[$factionId] = $true
        Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$faction.displayName)) "Faction displayName must not be empty."
        Assert-ProofTrue ([string]$faction.teamColor -cmatch '^#[0-9A-Fa-f]{6}$') "Faction teamColor must be a six-digit HTML color."
        Assert-ProofTrue ((Test-ProofInteger $faction.startingResources) -and [int]$faction.startingResources -ge 0) "Faction startingResources must be nonnegative."
        $availableUpgrades = @($faction.upgradeIds)
        Assert-ProofTrue ($availableUpgrades.Count -eq 2 -and @($availableUpgrades | Select-Object -Unique).Count -eq 2) "Each faction must expose exactly two distinct upgrades."
        foreach ($upgradeId in $availableUpgrades) {
            Assert-ProofTrue $upgradeIds.ContainsKey([string]$upgradeId) "Faction upgrade references must resolve."
        }
        $roster = @($faction.roster)
        Assert-ProofTrue ($roster.Count -eq 2) "Each faction must expose exactly two selectable roster entries."
        foreach ($unit in $roster) {
            Assert-ProofProperties $unit $unitFields $unitFields "roster unit"
            $unitId = [string]$unit.unitId
            Assert-ProofTrue ($unitId -cmatch '^[a-z][a-z0-9_]+$' -and -not $unitIds.ContainsKey($unitId)) "Unit ids must be globally unique stable identifiers."
            $unitIds[$unitId] = $true
            $rosterCount += 1
            Assert-ProofTrue (-not [string]::IsNullOrWhiteSpace([string]$unit.displayName)) "Unit displayName must not be empty."
            Assert-ProofTrue $armorIds.ContainsKey([string]$unit.armorClass) "Roster armorClass must resolve."
            Assert-ProofTrue ($damageTypes -contains [string]$unit.damageType) "Roster damageType must resolve."
            foreach ($field in @("baseDamage", "maximumHealth", "rangeCells", "cooldownTicks")) {
                Assert-ProofTrue ((Test-ProofInteger $unit.$field) -and [int]$unit.$field -gt 0) "Roster $field must be a positive integer."
            }
            Assert-ProofProperties $unit.art $artFields $artFields "roster art"
            Assert-ProofTrue ([string]$unit.art.source -ceq "generated-primitive") "Stage 6 art must remain generated-primitive and legal-safe."
            Assert-ProofTrue ($allowedShapes -contains [string]$unit.art.shape) "Roster primitive shape is unsupported."
            Assert-ProofTrue ([string]$unit.art.material -cmatch '^team-color-[a-z]+$') "Roster material must be a team-color primitive recipe."
        }
    }
    Assert-ProofTrue ($rosterCount -eq 8 -and $unitIds.Count -eq 8) "Stage 6 must cover exactly eight roster entries."
}

try {
    [void](Invoke-ProofPriorGate $gate "stage5_regression" $priorGate $GodotPath '(?m)^STAGE5_GATE PASS\s*$')
    Assert-FactionRosterDocument
    Write-Host "$gate bundle PASS"
    Assert-ProofTrue (Test-Path -LiteralPath $proofRunner -PathType Leaf) "Missing Stage 6 proof runner."
    Assert-ProofTrue (Test-Path -LiteralPath $visualRunner -PathType Leaf) "Missing Stage 6 visual runner."
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    $proofOutput = Invoke-ProofChecked $gate "godot_stage6_proof" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage6_proof_runner.gd") '(?m)^STAGE6_GODOT_PROOF PASS authority=gdscript-proof assertions=67\s*$' $forbiddenDiagnostics
    $visualOutput = Invoke-ProofChecked $gate "godot_stage6_visual" $godot @("--headless", "--path", $gameRoot, "-s", "res://tests/stage6_visual_runner.gd") '(?m)^STAGE6_VISUAL_PROOF PASS assertions=35\s*$' $forbiddenDiagnostics
    $proofMatch = [regex]::Match($proofOutput, '(?m)^STAGE6_METRICS repeat_hash=([0-9A-F]{8}) assertions=(\d+) factions=(\d+) roster=(\d+) art_resolved=(\d+)\s*$')
    $visualMatch = [regex]::Match($visualOutput, '(?m)^STAGE6_VISUAL_METRICS battalions=(\d+) presentation_ms=([0-9]+(?:\.[0-9]+)?) note=legal-safe-proof-not-bfme-parity\s*$')
    Assert-ProofTrue ($proofMatch.Success -and $proofMatch.Groups[1].Value -ceq "E383C87F") "Stage 6 deterministic replay hash changed."
    Assert-ProofTrue ([int]$proofMatch.Groups[2].Value -eq 67 -and [int]$proofMatch.Groups[3].Value -eq 4 -and [int]$proofMatch.Groups[4].Value -eq 8 -and [int]$proofMatch.Groups[5].Value -eq 8) "Stage 6 proof coverage metrics changed."
    Assert-ProofTrue ($visualMatch.Success -and [int]$visualMatch.Groups[1].Value -eq 80) "Stage 6 did not render the declared 80-battalion probe."
    Assert-ProofTrue ([double]$visualMatch.Groups[2].Value -lt 500.0) "Stage 6 bounded primitive presentation probe exceeded 500 ms."
    Write-Host "$gate deterministic_replay PASS hash=$($proofMatch.Groups[1].Value)"
    Write-Host "$gate presentation_probe PASS battalions=80 ms=$($visualMatch.Groups[2].Value) scope=legal-safe-proof"
    Write-Host "$gate PASS"
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
