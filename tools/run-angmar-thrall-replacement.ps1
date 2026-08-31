[CmdletBinding()]
param([string]$GodotPath = '')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$gate = 'ANGMAR_THRALL_REPLACEMENT'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'proof-gate-common.ps1')

try {
    $commonGitDir = (& git -C $repo rev-parse --path-format=absolute --git-common-dir).Trim()
    Assert-ProofTrue ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($commonGitDir)) 'Shared Git directory is unavailable.'
    $main = Split-Path -Parent $commonGitDir
    $python = Join-Path $main 'workspace\retail-work\tools\python-3.12-env\Scripts\python.exe'
    $sourceRoot = Join-Path $main 'workspace\retail-work\editions\rotwk\cache\layered-effective-assets'
    $manifest = Join-Path $sourceRoot '.openbfme\manifest.json'
    $basePack = Join-Path $main 'workspace\content-packs\rotwk-angmar-vslice\64a03976781f6b034ed3cb969d4b7a40570167551c73291d346091c5e8587109'
    foreach ($path in @($python, $manifest, (Join-Path $basePack 'pack.json'))) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Pinned private prerequisite is unavailable: $path"
    }

    $pytestArgs = @(
        '-B', '-m', 'pytest',
        (Join-Path $repo 'importer\tests\test_module_contracts_batch.py'),
        (Join-Path $repo 'importer\tests\test_playable_unit_compiler.py'),
        '-q', '-k', 'thrall or do_command_upgrade_fails_closed or summon_replacement_fails_closed'
    )
    [void](Invoke-ProofChecked $gate 'focused-conversion' $python $pytestArgs '(?m)[1-9][0-9]* passed' '(?i)failed|error')

    $logRoot = Join-Path $repo 'workspace\logs\P1-ANGMAR-THRALL-002'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $cookResult = Join-Path $logRoot 'cook-result.json'
    $cookCode = @'
import copy, hashlib, json, os, pathlib, sys, uuid

repo, source_root, manifest_path, base_pack, log_root, result_path = map(pathlib.Path, sys.argv[1:7])
sys.path.insert(0, str(repo / "importer"))
from openbfme_importer.playable_unit_compiler import compile_angmar_thrall_replacement_graph

mandatory = {
 "data/ini/object/evilfaction/units/angmar/angmarthrallmaster.ini":"a3702118d838392e71e487dcaebe4d3b52a8a5e2a8c0f79582ce1fd048668922",
 "data/ini/object/evilfaction/hordes/angmar/angmarhordes.ini":"b1c60557d2dfab885013e06edce3ef56c29cc53834975dbb30572848b93cb829",
 "data/ini/object/evilfaction/structures/angmar/angmarden.ini":"bbcf56d7e0828397bdec9cda5169870d73960f11489b97b98a9c081181f87be6",
 "data/ini/commandbutton.ini":"225653f2e379b50c6f8684cc8f7248e87a7719c7e5d6717c3ed5b96055e2a670",
 "data/ini/commandset.ini":"3b1fc73d2e6658c1886c6e59cc47a5e071cfedc67ebaae01fdb96480ca4b9658",
 "data/ini/specialpower.ini":"ba9f33063fc7a4476e4ed325c24d4bda521fa2f5aa63a2d765631d2380bd4571",
 "data/ini/upgrade.ini":"98befac94217865cc8eaf37cde71737434bf14fbf7b7b4dcea74ef83049e7c2e",
 "data/ini/attributemodifier.ini":"07d1c80b84acdb23d2bbd085a73c555510f7e0f14e81ed91781b75d9ab4c0379",
}
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
winner = {str(r["path"]).lower(): r for r in manifest["files"]}
for rel, expected in mandatory.items():
    row = winner.get(rel.lower())
    if not row or row.get("archive") != "layer-expansion/__patch202.big" or row.get("precedence") != 0 or row.get("sha256") != expected:
        raise SystemExit(f"mandatory winner drift: {rel}")

documents = {}
for path in sorted((source_root / "data" / "ini").rglob("*.ini")):
    rel = path.relative_to(source_root).as_posix().lower()
    documents[rel] = path.read_bytes()
for rel, expected in mandatory.items():
    if hashlib.sha256(documents[rel]).hexdigest() != expected:
        raise SystemExit(f"mandatory effective bytes drift: {rel}")

graph = compile_angmar_thrall_replacement_graph(documents, game="rotwk")
if graph.get("graphStatus") != "executable" or len(graph.get("branches", [])) != 4:
    raise SystemExit("compiler did not close exact four-branch graph")

source_rows = []
for item in graph.get("sourceDocuments", []):
    rel = str(item.get("virtualPath", "")).lower()
    row = winner.get(rel)
    path = source_root / pathlib.PurePosixPath(rel)
    if not rel or row is None or not path.is_file():
        raise SystemExit(f"graph source is not an effective winner: {rel}")
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != row.get("sha256"):
        raise SystemExit(f"graph source hash drift: {rel}")
    source_rows.append({"virtualPath": rel, "sha256": actual, "archive": row.get("archive"), "precedence": row.get("precedence")})
if not set(mandatory).issubset({row["virtualPath"] for row in source_rows}):
    raise SystemExit("graph provenance omitted a mandatory source")

def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")

def clone_tree(src, dst):
    for path in sorted(src.rglob("*")):
        rel = path.relative_to(src)
        target = dst / rel
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            os.link(path, target)

def replace_json(path, value):
    path.unlink()
    path.write_bytes(canonical(value))

def tree_digest(root):
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        rel = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(rel); digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()

base_hashes = {
 "pack.json":"541dcff891f548483530441c153eb46e6f7b045d721bdac8e1e18d572e144f3f",
 "data/playable-units/angmarthrallmaster.json":"53d4e8fcbcb884a8e6a8ed5d079ab43f923aadd0a7acc06d8f48ea61dcf9689d",
 "data/playable-structures/angmarden.json":"712fae878fe7c61a5bca9ae46c2bcf895858cd7afd4e10e21e45aea142958a40",
}
if base_pack.name != "64a03976781f6b034ed3cb969d4b7a40570167551c73291d346091c5e8587109":
    raise SystemExit("base pack identity drift")
if tree_digest(base_pack) != "452b1310a47edffa19317a6310bb890dc9c20c5a3dfeec66d65f237e8c5e4dc3":
    raise SystemExit("base pack tree drift")
for rel, expected in base_hashes.items():
    if hashlib.sha256((base_pack / rel).read_bytes()).hexdigest() != expected:
        raise SystemExit(f"base pack file drift: {rel}")
base_thrall = json.loads((base_pack / "data/playable-units/angmarthrallmaster.json").read_text(encoding="utf-8"))
base_den = json.loads((base_pack / "data/playable-structures/angmarden.json").read_text(encoding="utf-8"))
if base_thrall.get("objectId") != "AngmarThrallMaster" or base_thrall.get("descriptorSha256") != "3062db8772c9b0e894fd949b81e729b1dccb15f677cd06925dfd2b50669eccf8":
    raise SystemExit("base Thrall descriptor identity drift")
if base_den.get("objectId") != "AngmarDen" or base_den.get("descriptorSha256") != "fba11ee21ce67947ea6f53c6380cdef39618b1c00324155e2f2b5fa88233a324":
    raise SystemExit("base Den descriptor identity drift")
aura_contract = graph.get("denAura", {}).get("moduleContract", {})
if (
    aura_contract.get("module") != "AttributeModifierAuraUpdate"
    or aura_contract.get("tag") != "ModuleTag_GrantWolfRiderSummon"
    or aura_contract.get("extraction") != "typed"
):
    raise SystemExit("graph-provenanced Den aura contract is invalid")
monitor_contract = graph.get("monitor", {}).get("moduleContract", {})
if (
    monitor_contract.get("module") != "MonitorConditionUpdate"
    or monitor_contract.get("tag") != "ModuleTag_CommandSetSwapper"
    or monitor_contract.get("extraction") != "typed"
):
    raise SystemExit("graph-provenanced Thrall monitor contract is invalid")

def apply_exact_overlays(unit, den):
    original_unit = copy.deepcopy(unit)
    original_den = copy.deepcopy(den)
    unit["registration"]["gameplay"]["simulation"]["resolved"]["angmarThrallReplacement"] = copy.deepcopy(graph)
    original_commands = original_unit["registration"]["gameplay"].get("upgradeCommands", [])
    branches = graph.get("branches", [])
    branch_by_upgrade = {row.get("upgradeId"): row for row in branches}
    command_by_upgrade = {row.get("upgradeId"): row for row in original_commands}
    if len(branch_by_upgrade) != 4 or len(command_by_upgrade) != len(original_commands) or set(command_by_upgrade) != set(branch_by_upgrade):
        raise SystemExit("base Thrall purchase rows do not uniquely match the exact four branches")
    cooked_commands = []
    den_commands = []
    for original in original_commands:
        branch = branch_by_upgrade[original["upgradeId"]]
        purchase = branch["purchaseCommand"]
        cooked = copy.deepcopy(original)
        cooked["commandId"] = purchase["commandButtonId"]
        cooked["commandSetId"] = purchase["commandSetId"]
        cooked["slot"] = purchase["slot"]
        cooked["neededUpgradeIds"] = ([purchase["neededUpgradeId"]] if purchase.get("neededUpgradeId") else [])
        cooked["neededUpgradeAny"] = False
        cooked_commands.append(cooked)
        den_purchase = branch.get("denPurchaseCommand")
        if den_purchase:
            den_cooked = copy.deepcopy(original)
            den_cooked["commandId"] = den_purchase["commandButtonId"]
            den_cooked["commandSetId"] = den_purchase["commandSetId"]
            den_cooked["slot"] = den_purchase["slot"]
            den_cooked["neededUpgradeIds"] = []
            den_cooked["neededUpgradeAny"] = False
            den_commands.append(den_cooked)
    if len(den_commands) != 1:
        raise SystemExit("exact graph did not produce one Den Wolf purchase variant")
    cooked_commands.extend(den_commands)
    unit["registration"]["gameplay"]["upgradeCommands"] = cooked_commands
    unit_contracts = unit["registration"]["simulation"]["resolved"]["moduleContracts"]
    gameplay_contracts = unit["registration"]["gameplay"]["simulation"]["resolved"]["moduleContracts"]
    monitor_matches = [
        row for row in [*unit_contracts, *gameplay_contracts]
        if row.get("module") == "MonitorConditionUpdate"
        and row.get("tag") == "ModuleTag_CommandSetSwapper"
    ]
    if monitor_matches:
        raise SystemExit("stale Thrall unexpectedly already has the graph monitor contract")
    unit_contracts.append(copy.deepcopy(monitor_contract))
    den_contracts = den["registration"]["gameplay"]["moduleContracts"]
    if any(row.get("tag") == "ModuleTag_GrantWolfRiderSummon" for row in den_contracts):
        raise SystemExit("base Den unexpectedly already contains the graph aura")
    den_contracts.append(copy.deepcopy(aura_contract))
    expected_unit = copy.deepcopy(original_unit)
    expected_unit["registration"]["gameplay"]["simulation"]["resolved"]["angmarThrallReplacement"] = copy.deepcopy(graph)
    expected_unit["registration"]["gameplay"]["upgradeCommands"] = copy.deepcopy(cooked_commands)
    expected_unit["registration"]["simulation"]["resolved"]["moduleContracts"].append(copy.deepcopy(monitor_contract))
    expected_den = copy.deepcopy(original_den)
    expected_den["registration"]["gameplay"]["moduleContracts"].append(copy.deepcopy(aura_contract))
    if unit != expected_unit or den != expected_den:
        raise SystemExit("cook changed fields outside the exact graph/aura overlays")
    if unit.get("descriptorSha256") != original_unit.get("descriptorSha256") or den.get("descriptorSha256") != original_den.get("descriptorSha256"):
        raise SystemExit("cook rewrote a base descriptor identity")
    return {
        "thrallGraphSha256": hashlib.sha256(canonical(graph)).hexdigest(),
        "denAuraContractSha256": hashlib.sha256(canonical(aura_contract)).hexdigest(),
        "thrallMonitorContractSha256": hashlib.sha256(canonical(monitor_contract)).hexdigest(),
        "thrallOtherFieldsPreserved": True,
        "denOtherFieldsPreserved": True,
        "denExistingModuleContractCount": len(original_den["registration"]["gameplay"]["moduleContracts"]),
        "thrallPurchaseRowsBefore": len(original_commands),
        "thrallPurchaseRowsAfter": len(cooked_commands),
        "thrallPurchasePayloadFieldsPreserved": True,
        "thrallUnrelatedModuleContractsPreserved": True,
        "thrallExistingModuleContractCount": len(original_unit["registration"]["simulation"]["resolved"]["moduleContracts"]),
    }

run_root = log_root / "cooks" / str(uuid.uuid4())
roots = []
identities = []
overlay_receipts = []
for label in ("a", "b"):
    content = run_root / f"{label}-content"
    staging = content / "rotwk-angmar-vslice" / "staging"
    clone_tree(base_pack, staging)
    unit_path = staging / "data" / "playable-units" / "angmarthrallmaster.json"
    unit = json.loads(unit_path.read_text(encoding="utf-8"))
    den_path = staging / "data" / "playable-structures" / "angmarden.json"
    den_runtime = json.loads(den_path.read_text(encoding="utf-8"))
    overlay_receipts.append(apply_exact_overlays(unit, den_runtime))
    replace_json(unit_path, unit)
    replace_json(den_path, den_runtime)
    identity = tree_digest(staging)
    final = staging.with_name(identity)
    staging.rename(final)
    selection = {"schema":"openbfme.pack-selection","schemaVersion":0,"activePack":f"rotwk-angmar-vslice/{identity}","supplementalPacks":[]}
    (content / "selection.json").write_bytes(canonical(selection))
    roots.append(str(content)); identities.append(identity)
if identities[0] != identities[1]:
    raise SystemExit("two independent pack cooks produced different identities")
if overlay_receipts[0] != overlay_receipts[1]:
    raise SystemExit("two independent cooks disagreed on overlay receipt")
result = {
 "schema":"openbfme.angmar-thrall-cook", "schemaVersion":1,
 "identity":identities[0], "contentRoots":roots,
 "packCount":1, "sourceManifestAggregateSha256":manifest["aggregate_sha256"],
 "sourceWinners":source_rows, "basePackIdentity":base_pack.name,
 "basePackTreeSha256":"452b1310a47edffa19317a6310bb890dc9c20c5a3dfeec66d65f237e8c5e4dc3",
 "baseFileSha256":base_hashes,
 "thrallDescriptorSha256":base_thrall["descriptorSha256"],
 "denDescriptorSha256":base_den["descriptorSha256"],
 "overlays":overlay_receipts[0], "branches":[b["targetHordeId"] for b in graph["branches"]],
}
result_path.write_bytes(canonical(result))
print("THRALL_COOK PASS identity=" + identities[0])
'@
    # Windows native argv quoting can strip JSON/Python quotes from a large
    # `-c` argument.  Materialize the private helper under ignored workspace
    # so the pinned interpreter receives exact bytes; it is not repo code.
    $cookScriptPath = Join-Path $logRoot 'cook-private-pack.py'
    Set-Content -LiteralPath $cookScriptPath -Value $cookCode -Encoding UTF8
    $cookOutput = & $python -B $cookScriptPath $repo $sourceRoot $manifest $basePack $logRoot $cookResult 2>&1
    if ($LASTEXITCODE -ne 0 -or ($cookOutput -join "`n") -cnotmatch 'THRALL_COOK PASS identity=[0-9a-f]{64}') {
        throw "Private deterministic cook failed:`n$($cookOutput -join "`n")"
    }
    $cook = Read-ProofJson $cookResult
    Assert-ProofTrue ($cook.packCount -eq 1 -and $cook.contentRoots.Count -eq 2) 'Cook did not produce two isolated one-pack roots.'
    Assert-ProofTrue ([string]$cook.identity -match '^[0-9a-f]{64}$') 'Cook identity is invalid.'

    if ([string]::IsNullOrWhiteSpace($GodotPath)) {
        $sharedGodot = Join-Path $main '.tools\godot\Godot_v4.7-stable_win64_console.exe'
        if (Test-Path -LiteralPath $sharedGodot -PathType Leaf) {
            $GodotPath = $sharedGodot
        }
    }
    $godot = Resolve-ProofGodot $GodotPath $repo
    $gameRoot = Join-Path $repo 'game'
    $classCache = Join-Path $gameRoot '.godot\global_script_class_cache.cfg'
    $sharedClassCache = Join-Path $main 'game\.godot\global_script_class_cache.cfg'
    if (-not (Test-Path -LiteralPath $classCache -PathType Leaf)) {
        Assert-ProofTrue (Test-Path -LiteralPath $sharedClassCache -PathType Leaf) 'Pinned Godot class cache is unavailable.'
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $classCache) | Out-Null
        Copy-Item -LiteralPath $sharedClassCache -Destination $classCache
    }
    $oldContent = $env:OPENBFME_CONTENT
    $oldIdentity = $env:OPENBFME_THRALL_PACK_IDENTITY
    try {
        $env:OPENBFME_CONTENT = [string]$cook.contentRoots[0]
        $env:OPENBFME_THRALL_PACK_IDENTITY = [string]$cook.identity
        $arguments = @('--headless', '--path', $gameRoot, '--script', 'res://tests/angmar_thrall_replacement_runner.gd')
        [void](Invoke-ProofChecked $gate 'production-behavior' $godot $arguments 'ANGMAR_THRALL_REPLACEMENT PASS branches=4 failed=0' '(?i)SCRIPT ERROR|Parse Error|Invalid call|ERROR:')
    }
    finally {
        $env:OPENBFME_CONTENT = $oldContent
        $env:OPENBFME_THRALL_PACK_IDENTITY = $oldIdentity
    }

    $receipt = [ordered]@{
        schema = 'openbfme.angmar-thrall-check'; schemaVersion = 2
        sourceManifestAggregateSha256 = $cook.sourceManifestAggregateSha256
        sourceWinners = $cook.sourceWinners
        conversion = [ordered]@{
            basePackIdentity = $cook.basePackIdentity; basePackTreeSha256 = $cook.basePackTreeSha256
            baseFileSha256 = $cook.baseFileSha256; thrallDescriptorSha256 = $cook.thrallDescriptorSha256
            denDescriptorSha256 = $cook.denDescriptorSha256; overlays = $cook.overlays
            cookIdentity = $cook.identity; independentCookCount = 2; mountedPackCount = 1; branches = $cook.branches
            descriptorRecompileScope = 'not-claimed; exact resolved graph and Den aura overlays only'
        }
        behavior = [ordered]@{ productionQueue = 'accepted'; denAuraMonitorTransition = 'accepted'; descriptorReplacement = 'accepted'; failed = 0 }
        openEvidence = @('L5 replacement-transfer observation', 'FX_ThrallSummon', 'BoromirHorn', 'VISUAL', 'AUDIO')
    }
    $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $logRoot 'angmar-thrall-check.json') -Encoding UTF8
    Write-Output 'ANGMAR_THRALL_REPLACEMENT PASS branches=4 failed=0'
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
