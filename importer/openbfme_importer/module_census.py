"""Retail module-type (behaviour) census - the denominator for behaviour work.

WHY THIS EXISTS
===============
BFME2's extensibility surface is its module types (Behaviors / Updates / Bodies /
Contains / Dies / Draws / ...) bolted onto Object definitions in INI.  "N of 364
vocabulary entries implemented" answers a question nobody has: retail does not
use that vocabulary uniformly.  This census commits the denominator the same way
``game/data/retail_ai_call_census.json`` did for AI call sites: every module
declaration the retail INI corpus actually authors, per tree, so coverage can be
weighted by real usage instead of by vocabulary size.

THE AMBIGUITY OF "SUPPORTED", AND WHY EVERY MEMBER IS CLASSIFIED
================================================================
The importer does not hand behaviours to the runtime.  It interprets each
behaviour and flattens it into a normalised static description; the runtime
adapter consumes that description and names almost no behaviours itself.  So a
naive "supported" count would let a healing behaviour be "implemented" as a
field nothing reads.  Every member therefore carries one of five classes:

  A  STATIC-FLATTENED    effect fully expressible in the existing normalised
                         schema. Importer-only work.
  B  NEEDS-NEW-FIELD     flattenable, but the schema has no key for it yet.
                         Importer work plus a runtime read of the new key.
  C  NEEDS-RUNTIME-SYSTEM effect is behavioural over time or conditional; it
                         CANNOT be flattened into a static value.  Real
                         simulation work.  The data contract of such a module
                         may still flatten (abilities do) - the EFFECT cannot.
  D  PRESENTATION-ONLY   audio/visual/UI; no simulation semantics.
  E  INERT/BROKEN        evidence says the authored declaration is dead.  Only
                         asserted where we hold concrete evidence; the modding
                         community reports ~10% of retail module types are
                         broken, so E is knowingly under-populated.

WHAT "consumed" MEANS - READ BEFORE QUOTING
===========================================
A member's status is measured, not guessed: an AST scan over the import closure
of the shipped faction-import pipeline collects every string literal that
casefold-equals a census member name (docstrings excluded; entries of
explicitly-unsupported containers counted as refusals, never consumption).

  consumed   the pipeline names the module type somewhere and extracts from it.
             NOT the same as "the behaviour runs" - for class C members the
             runtime half may not exist at all.
  refused    named ONLY inside an explicitly-unsupported map; the importer
             deliberately reports it as unimplemented.  Never coverage.
  unhandled  never named.  Carrier-level skimming still applies: e.g. MaxHealth
             is read from EVERY Body module regardless of its kind, so an
             unhandled Body kind still yields health - and nothing else.

Regenerate ``game/data/retail_module_census.json`` with:

    python -m openbfme_importer.module_census

Regeneration is deterministic: byte-identical output while the retail corpus
and the importer source are unchanged.
"""

from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, Iterator, Mapping

from .catalog import CatalogEntry, InstallCatalog
from .paths import default_state_root, repo_root_from_module
from .sage_ini import MAX_INI_BYTES, parse_object_definitions


class ModuleCensusError(ValueError):
    """A module census input or invariant failed."""


#: Assignment keys that declare a module on an Object.  Must stay identical to
#: the syntactic carrier list in ``sage_cst`` (a test asserts this).
MODULE_CARRIER_KEYS = frozenset(
    {"behavior", "body", "clientbehavior", "clientupdate", "draw", "flasher"}
)

_MODULE_TAG = re.compile(r"^ModuleTag(?:_|$)", re.IGNORECASE)

MAX_CENSUS_DOCUMENTS = 8192
MAX_TOTAL_CENSUS_INI_BYTES = 256 * 1024 * 1024

#: Census tree labels keyed by retail game id (catalog file stem).
CENSUS_TREES = {"bfme2": "bfme2-retail", "rotwk": "rotwk-retail"}

DEFAULT_CENSUS_PATH = (
    repo_root_from_module() / "game" / "data" / "retail_module_census.json"
)

#: Roots of the shipped conversion pipeline.  The measured consumer set is the
#: package import closure of these modules - not the whole package, so oracle
#: and profile modules that merely mention a kind do not inflate support.
PIPELINE_ROOT_MODULES = ("faction_import", "m3_pack_expansion", "pipeline")

_REFUSAL_NAME_MARKERS = ("UNSUPPORTED", "UNIMPLEMENTED")


# ---------------------------------------------------------------------------
# Corpus scan
# ---------------------------------------------------------------------------


def module_kind_from_assignment(key: str, value: str) -> str | None:
    """The module class declared by one Object-body assignment, if any.

    Recognises the six carrier keys plus the generic ``<Kind> ModuleTag_*``
    shape (parity with the CST parser).  Tolerates the retail double-equals
    typo (``Draw = = W3DScriptedModelDraw ModuleTag_01`` in cinematic props).
    """

    tokens = value.split()
    while tokens and tokens[0] == "=":
        tokens.pop(0)
    if not tokens:
        return None
    if key.casefold() in MODULE_CARRIER_KEYS:
        return tokens[0]
    if len(tokens) >= 2 and _MODULE_TAG.match(tokens[1]):
        return tokens[0]
    return None


def object_family(virtual_path: str) -> str:
    """Coarse object-family label derived from the document's virtual path.

    ``data/ini/object/<side>/...`` yields ``object/<side>`` (goodfaction,
    evilfaction, neutral, civilian, ...); other locations yield the directory
    (or file) name directly under ``data/ini``.
    """

    parts = virtual_path.replace("\\", "/").casefold().split("/")
    if len(parts) < 3 or parts[0] != "data" or parts[1] != "ini":
        return "other"
    if parts[2] == "object" and len(parts) > 4:
        return f"object/{parts[3]}"
    return parts[2]


@dataclass
class ModuleUsage:
    """Aggregated usage of one module kind (casefold identity) in one tree."""

    spellings: Counter = field(default_factory=Counter)
    sites: int = 0
    objects: set[str] = field(default_factory=set)
    families: set[str] = field(default_factory=set)
    carriers: set[str] = field(default_factory=set)

    @property
    def display_name(self) -> str:
        # Deterministic: most frequent authored spelling, ties lexicographic.
        return min(self.spellings, key=lambda name: (-self.spellings[name], name))


@dataclass
class TreeUsage:
    """One tree's module census: documents scanned and per-kind usage."""

    document_count: int = 0
    object_count: int = 0
    members: dict[str, ModuleUsage] = field(default_factory=dict)

    @property
    def declaration_sites(self) -> int:
        return sum(usage.sites for usage in self.members.values())


def collect_tree_usage(documents: Iterable[tuple[str, bytes]]) -> TreeUsage:
    """Aggregate module declarations across ``(virtual_path, source)`` docs.

    Raises on unparsable input instead of skipping it: a silently dropped
    document is how a denominator understates reality.
    """

    tree = TreeUsage()
    total_bytes = 0
    for virtual_path, source in documents:
        tree.document_count += 1
        if tree.document_count > MAX_CENSUS_DOCUMENTS:
            raise ModuleCensusError("census document count exceeds limit")
        total_bytes += len(source)
        if total_bytes > MAX_TOTAL_CENSUS_INI_BYTES:
            raise ModuleCensusError("census document bytes exceed limit")
        try:
            blocks = parse_object_definitions(source)
        except ValueError as exc:
            raise ModuleCensusError(f"unparsable INI document {virtual_path}: {exc}")
        family = object_family(virtual_path)
        for block in blocks:
            if block.name.startswith("="):
                # ``Object = X`` lines inside unrelated flat blocks (retail
                # commandbutton.ini authors them) parse as a pseudo-object
                # named "=".  Real object names never start with "=" - but
                # they do contain apostrophes (Helm'sDeep_MainWall), so this
                # check must stay this narrow.
                continue
            tree.object_count += 1
            for key, value in block.assignments:
                kind = module_kind_from_assignment(key, value)
                if kind is None:
                    continue
                folded = kind.casefold()
                usage = tree.members.get(folded)
                if usage is None:
                    usage = tree.members[folded] = ModuleUsage()
                usage.spellings[kind] += 1
                usage.sites += 1
                usage.objects.add(block.name.casefold())
                usage.families.add(family)
                usage.carriers.add(key.casefold())
    return tree


def effective_ini_entries(entries: Iterable[CatalogEntry]) -> list[CatalogEntry]:
    """Precedence winners for every effective ``data/ini/**`` INI/INC document."""

    winners: dict[str, CatalogEntry] = {}
    for entry in sorted(
        entries,
        key=lambda item: (item.precedence, item.archive.casefold(), item.name.casefold()),
    ):
        winners.setdefault(entry.key, entry)
    selected = [
        entry
        for entry in winners.values()
        if entry.name.casefold().startswith("data/ini/")
        and entry.name.casefold().endswith((".ini", ".inc"))
    ]
    selected.sort(key=lambda item: (item.name.casefold(), item.name))
    return selected


def read_catalog_documents(catalog: InstallCatalog) -> Iterator[tuple[str, bytes]]:
    for entry in effective_ini_entries(catalog.entries):
        archive = catalog.open_archive_for(entry)
        yield entry.name, archive.read_entry(
            catalog.as_entry(entry), max_bytes=MAX_INI_BYTES
        )


def collect_catalog_tree_usage(catalog: InstallCatalog) -> TreeUsage:
    return collect_tree_usage(read_catalog_documents(catalog))


# ---------------------------------------------------------------------------
# Measured support: what the shipped pipeline actually names
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ModuleSupport:
    """Result of the AST scan over the pipeline import closure."""

    consumer_files: tuple[str, ...]
    consumed: Mapping[str, tuple[str, ...]]  # folded kind -> sorted file names
    refused: Mapping[str, tuple[str, ...]]  # folded kind -> sorted file names
    refusal_reasons: Mapping[str, tuple[str, ...]]  # folded kind -> reasons

    def status(self, folded: str) -> str:
        if folded in self.consumed:
            return "consumed"
        if folded in self.refused:
            return "refused"
        return "unhandled"


def default_package_dir() -> Path:
    return Path(__file__).resolve().parent


def _relative_import_targets(tree: ast.AST, package_name: str) -> set[str]:
    targets: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom):
            if node.level and node.level > 0:
                if node.module:
                    targets.add(node.module.split(".")[0])
                else:
                    targets.update(alias.name.split(".")[0] for alias in node.names)
            elif node.module and node.module.split(".")[0] == package_name:
                pieces = node.module.split(".")
                if len(pieces) > 1:
                    targets.add(pieces[1])
                else:
                    targets.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.Import):
            for alias in node.names:
                pieces = alias.name.split(".")
                if pieces[0] == package_name and len(pieces) > 1:
                    targets.add(pieces[1])
    return targets


def package_import_closure(
    package_dir: Path, roots: Iterable[str] = PIPELINE_ROOT_MODULES
) -> tuple[str, ...]:
    """Module names reachable by import from ``roots`` inside one package.

    Lazy (function-body) imports count: the scan walks the whole AST.  Roots
    missing from the package are reported instead of silently ignored.
    """

    package_name = package_dir.name
    modules = {
        path.stem: path
        for path in package_dir.glob("*.py")
        if path.stem != "__init__"
    }
    pending = list(dict.fromkeys(roots))
    missing = [name for name in pending if name not in modules]
    if missing:
        raise ModuleCensusError(
            f"pipeline root modules missing from {package_dir}: {missing}"
        )
    closure: set[str] = set()
    while pending:
        name = pending.pop()
        if name in closure or name not in modules:
            continue
        closure.add(name)
        tree = ast.parse(modules[name].read_text(encoding="utf-8"))
        pending.extend(_relative_import_targets(tree, package_name) - closure)
    return tuple(sorted(closure))


def _docstring_constant_ids(tree: ast.AST) -> set[int]:
    ids: set[int] = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            body = getattr(node, "body", [])
            if (
                body
                and isinstance(body[0], ast.Expr)
                and isinstance(body[0].value, ast.Constant)
                and isinstance(body[0].value.value, str)
            ):
                ids.add(id(body[0].value))
    return ids


def _refusal_container_values(tree: ast.AST) -> list[ast.AST]:
    values: list[ast.AST] = []
    for node in ast.walk(tree):
        targets: list[ast.expr] = []
        if isinstance(node, ast.Assign):
            targets = node.targets
        elif isinstance(node, ast.AnnAssign) and node.value is not None:
            targets = [node.target]
        else:
            continue
        names = [target.id for target in targets if isinstance(target, ast.Name)]
        if any(
            marker in name.upper()
            for name in names
            for marker in _REFUSAL_NAME_MARKERS
        ):
            value = node.value if isinstance(node, ast.Assign) else node.value
            if value is not None:
                values.append(value)
    return values


def scan_module_support(
    vocabulary: Iterable[str],
    package_dir: Path | None = None,
    roots: Iterable[str] = PIPELINE_ROOT_MODULES,
) -> ModuleSupport:
    """Measure which module kinds the pipeline names, and which it refuses.

    A literal counts as consumption only when it casefold-equals a vocabulary
    member, is not a docstring, and does not sit inside a container whose name
    carries an UNSUPPORTED/UNIMPLEMENTED marker; those containers count as
    refusals, with their dict-value reason strings captured.
    """

    directory = package_dir if package_dir is not None else default_package_dir()
    vocab = {name.casefold() for name in vocabulary}
    consumer_files = package_import_closure(directory, roots)
    consumed: dict[str, set[str]] = {}
    refused: dict[str, set[str]] = {}
    reasons: dict[str, set[str]] = {}
    for module_name in consumer_files:
        path = directory / f"{module_name}.py"
        tree = ast.parse(path.read_text(encoding="utf-8"))
        docstrings = _docstring_constant_ids(tree)
        refusal_ids: set[int] = set()
        for container in _refusal_container_values(tree):
            for sub in ast.walk(container):
                if isinstance(sub, ast.Constant) and isinstance(sub.value, str):
                    refusal_ids.add(id(sub))
            if isinstance(container, ast.Dict):
                for key_node, value_node in zip(container.keys, container.values):
                    if (
                        isinstance(key_node, ast.Constant)
                        and isinstance(key_node.value, str)
                        and key_node.value.casefold() in vocab
                    ):
                        folded = key_node.value.casefold()
                        refused.setdefault(folded, set()).add(path.name)
                        if isinstance(value_node, ast.Constant) and isinstance(
                            value_node.value, str
                        ):
                            reasons.setdefault(folded, set()).add(value_node.value)
            else:
                for sub in ast.walk(container):
                    if (
                        isinstance(sub, ast.Constant)
                        and isinstance(sub.value, str)
                        and sub.value.casefold() in vocab
                    ):
                        refused.setdefault(sub.value.casefold(), set()).add(path.name)
        for node in ast.walk(tree):
            if not (isinstance(node, ast.Constant) and isinstance(node.value, str)):
                continue
            if id(node) in docstrings or id(node) in refusal_ids:
                continue
            folded = node.value.casefold()
            if folded in vocab:
                consumed.setdefault(folded, set()).add(path.name)
    # An explicit refusal is authoritative.  A module may contain prerequisite
    # or diagnostic literals without admitting the module kind to the shipping
    # compiler; do not let those incidental names turn a refused kind into a
    # false consumed result.
    for folded in refused:
        consumed.pop(folded, None)
    return ModuleSupport(
        consumer_files=consumer_files,
        consumed={key: tuple(sorted(value)) for key, value in sorted(consumed.items())},
        refused={key: tuple(sorted(value)) for key, value in sorted(refused.items())},
        refusal_reasons={
            key: tuple(sorted(value)) for key, value in sorted(reasons.items())
        },
    )


# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------


MODULE_CLASS_MEANINGS = {
    "A": (
        "STATIC-FLATTENED: effect fully expressible in the existing normalised "
        "schema; importer-only work"
    ),
    "B": (
        "NEEDS-NEW-FIELD: flattenable, but the normalised schema has no key; "
        "importer work plus a runtime read"
    ),
    "C": (
        "NEEDS-RUNTIME-SYSTEM: behavioural over time or conditional; cannot be "
        "flattened into a static value; real simulation work"
    ),
    "D": "PRESENTATION-ONLY: audio/visual/UI; no simulation semantics",
    "E": "INERT/BROKEN: evidence says the authored declaration is dead",
}


def _group(letter: str, note: str, names: Iterable[str]) -> dict[str, tuple[str, str]]:
    return {name.casefold(): (letter, note) for name in names}


_CLASSIFICATION_GROUPS: tuple[dict[str, tuple[str, str]], ...] = (
    # -- A: already flattened into the normalised description ---------------
    _group(
        "A",
        "MaxHealth is flattened into the descriptor health contract",
        ("ActiveBody", "StructureBody"),
    ),
    _group(
        "A",
        "flattened by the armor/upgrade lane into upgrade-gated static values",
        ("ArmorUpgrade", "WeaponSetUpgrade", "StatusBitsUpgrade"),
    ),
    _group(
        "A",
        "flattened into upgrade-gated command-set / level / cost values",
        ("CommandSetUpgrade", "LevelUpUpgrade", "CostModifierUpgrade"),
    ),
    _group(
        "A",
        "flattened into ability gating inside the abilities contract",
        ("UnpauseSpecialPowerUpgrade",),
    ),
    # -- B: flattenable, schema key missing ----------------------------------
    _group(
        "B",
        "static per-upgrade stat modifier; the normalised schema has no key yet",
        ("AttributeModifierUpgrade",),
    ),
    _group(
        "B",
        "static upgrade-gated value with no schema key yet",
        (
            "GeometryUpgrade",
            "CommandPointsUpgrade",
            "SpellRechargeModifierUpgrade",
            "AllowBannerSpawnUpgrade",
            "BuildableHeroListUpgrade",
        ),
    ),
    _group(
        "B",
        "static damage-resolution rule; needs a flag key honoured by combat",
        ("ImmortalBody", "HighlanderBody", "InactiveBody"),
    ),
    _group(
        "B",
        "static spawn-time initial state; needs a key read at unit creation",
        (
            "ExperienceLevelCreate",
            "GrantUpgradeCreate",
            "InheritUpgradeCreate",
            "LockWeaponCreate",
            "SupplyCenterCreate",
        ),
    ),
    _group(
        "B",
        "static production exit/rally geometry for the existing production lane",
        (
            "QueueProductionExitUpdate",
            "SpawnPointProductionExitUpdate",
            "SupplyCenterProductionExitUpdate",
        ),
    ),
    # -- C: needs a runtime system -------------------------------------------
    _group(
        "C",
        "special-power effect; the ability contract flattens, the effect needs runtime",
        (
            "SpecialPowerModule",
            "AISpecialPowerUpdate",
            "OCLSpecialPower",
            "PlayerHealSpecialPower",
            "LevelGrantSpecialPower",
            "InvisibilitySpecialPower",
            "SpecialPowerTimerRefreshSpecialPower",
            "ActivateModuleSpecialPower",
            "CloudBreakSpecialPower",
            "CurseSpecialPower",
            "DarknessSpecialPower",
            "DeflectSpecialPower",
            "DevastateSpecialPower",
            "DominateEnemySpecialPower",
            "ElvenWoodSpecialPower",
            "FellBeastSwoopPower",
            "FreezingRainSpecialPower",
            "GrabPassengerSpecialPower",
            "HordeDispatchSpecialPower",
            "PlayerUpgradeSpecialPower",
            "RepairSpecialPower",
            "ScavengerSpecialPower",
            "SiegeDeployHordeSpecialPower",
            "SiegeDeploySpecialPower",
            "SplitHordeSpecialPower",
            "StopSpecialPower",
            "TaintSpecialPower",
            "UnleashSpecialPower",
            "UntamedAllegianceSpecialPower",
            "WeaponChangeSpecialPowerModule",
        ),
    ),
    _group(
        "C",
        "special-ability update; timed/conditional effect needs runtime",
        (
            "SpecialAbilityUpdate",
            "WeaponFireSpecialAbilityUpdate",
            "HeroModeSpecialAbilityUpdate",
            "ToggleMountedSpecialAbilityUpdate",
            "ToggleHiddenSpecialAbilityUpdate",
            "ToggleDeploySpecialAbilityUpdate",
            "TeleportSpecialAbilityUpdate",
            "ModelConditionSpecialAbilityUpdate",
            "FlingPassengerSpecialAbilityUpdate",
            "SummonReplacementSpecialAbilityUpdate",
            "ArrowStormUpdate",
            "WeaponModeSpecialPowerUpdate",
        ),
    ),
    _group(
        "C",
        "AI/movement runtime system (movement is engine-baked in retail)",
        (
            "AIUpdateInterface",
            "HordeAIUpdate",
            "AnimalAIUpdate",
            "WorkerAIUpdate",
            "HordeWorkerAIUpdate",
            "DozerAIUpdate",
            "FoundationAIUpdate",
            "GiantBirdAIUpdate",
            "SiegeAIUpdate",
            "DeployStyleAIUpdate",
            "AIGateUpdate",
            "CommandButtonHuntUpdate",
        ),
    ),
    _group(
        "C",
        "container/garrison/transport runtime system",
        (
            "HordeContain",
            "HorseHordeContain",
            "GarrisonContain",
            "HordeGarrisonContain",
            "TransportContain",
            "HordeTransportContain",
            "TunnelContain",
            "SiegeEngineContain",
            "HordeSiegeEngineContain",
            "CitadelSlaughterHordeContain",
            "SlaughterHordeContain",
            "ProductionQueueHordeContain",
            "AODHordeContain",
        ),
    ),
    _group(
        "C",
        "death-event runtime behaviour",
        (
            "SlowDeathBehavior",
            "ShipSlowDeathBehavior",
            "ClearanceTestingSlowDeathBehavior",
            "DestroyDie",
            "KeepObjectDie",
            "CreateObjectDie",
            "DamageFilteredCreateObjectDie",
            "RebuildHoleExposeDie",
            "RefundDie",
            "UpgradeDie",
            "HeroDie",
            "FireWeaponWhenDeadBehavior",
            "DelayedDeathBody",
            "OathbreakersFadeAwayBehavior",
        ),
    ),
    _group(
        "C",
        "hero respawn runtime system",
        ("RespawnBody", "RespawnUpdate"),
    ),
    _group(
        "C",
        "body with conditional runtime semantics beyond static health",
        (
            "PorcupineFormationBodyModule",
            "SymbioticStructuresBody",
            "DetachableRiderBody",
        ),
    ),
    _group(
        "C",
        "upgrade trigger with world side effects at runtime",
        (
            "ObjectCreationUpgrade",
            "ReplaceSelfUpgrade",
            "RemoveUpgradeUpgrade",
            "CastleUpgrade",
            "BaseUpgrade",
            "DoCommandUpgrade",
        ),
    ),
    _group(
        "C",
        "structure lifecycle runtime (build/collapse/rebuild/topple)",
        (
            "GettingBuiltBehavior",
            "BuildingBehavior",
            "StructureCollapseUpdate",
            "StructureToppleUpdate",
            "ToppleUpdate",
            "RubbleRiseUpdate",
            "RebuildHoleBehavior",
            "CastleBehavior",
            "CastleMemberBehavior",
            "WallHubBehavior",
            "WallUpgradeUpdate",
            "BridgeBehavior",
            "GateOpenAndCloseBehavior",
            "GateProxyBehavior",
            "DynamicPortalBehaviour",
            "FakePathfindPortalBehaviour",
            "EvacuateDamage",
        ),
    ),
    _group(
        "C",
        "resource-economy runtime behaviour",
        (
            "TerrainResourceBehavior",
            "AutoDepositUpdate",
            "SupplyCenterDockUpdate",
            "PickupStuffUpdate",
            "AutoPickUpUpdate",
            "PillageModule",
            "ProductionSpeedBonus",
        ),
    ),
    _group(
        "C",
        "production runtime system",
        ("ProductionUpdate",),
    ),
    _group(
        "C",
        "aura/modifier ticking runtime",
        (
            "AttributeModifierAuraUpdate",
            "AttributeModifierPoolUpdate",
            "LargeGroupBonusUpdate",
            "PassiveAreaEffectBehavior",
            "RadiateFearUpdate",
            "RousingSpeechUpdate",
            "BloodthirstyUpdate",
            "ShareExperienceBehavior",
        ),
    ),
    _group(
        "C",
        "per-tick or timer-driven simulation behaviour",
        (
            "LifetimeUpdate",
            "DeletionUpdate",
            "AutoHealBehavior",
            "PoisonedBehavior",
            "FlammableUpdate",
            "FireSpreadUpdate",
            "OilSpillUpdate",
            "DamageFieldUpdate",
            "FloodUpdate",
            "PartTheHeavensUpdate",
            "DestroyEnvironmentUpdate",
            "DynamicShroudClearingRangeUpdate",
            "StrafeAreaUpdate",
            "OCLUpdate",
            "FireWeaponUpdate",
            "DelayedLuaEventUpdate",
            "MonitorConditionUpdate",
            "BoredUpdate",
            "EmotionTrackerUpdate",
        ),
    ),
    _group(
        "C",
        "spawn/slave runtime system",
        (
            "SpawnBehavior",
            "SpawnUnitBehavior",
            "SlavedUpdate",
            "SlaveWatcherBehavior",
            "CivilianSpawnUpdate",
            "CivilianSpawnCollide",
            "CritterEmitterUpdate",
            "BannerCarrierUpdate",
            "DetachableRiderUpdate",
        ),
    ),
    _group(
        "C",
        "stealth/detection runtime system",
        (
            "StealthUpdate",
            "StealthDetectorUpdate",
            "InvisibilityUpdate",
            "SpecialDisguiseUpdate",
            "SpecialEnemySenseUpdate",
        ),
    ),
    _group(
        "C",
        "physics/collision/projectile runtime",
        (
            "PhysicsBehavior",
            "BezierProjectileBehavior",
            "ProjectileStreamUpdate",
            "SquishCollide",
            "HordeMemberCollide",
            "AODCrushCollide",
            "NotifyTargetsOfImminentProbableCrushingUpdate",
            "HordeNotifyTargetsOfImminentProbableCrushingUpdate",
        ),
    ),
    _group(
        "C",
        "crate pickup runtime behaviour",
        ("SalvageCrateCollide", "VeterancyCrateCollide"),
    ),
    _group(
        "C",
        "combat-stance/weapon-choice runtime behaviour",
        (
            "StancesBehavior",
            "DualWeaponBehavior",
            "AimWeaponBehavior",
            "AutoAbilityBehavior",
        ),
    ),
    _group(
        "C",
        "conditional runtime behaviour",
        (
            "SiegeDockingBehavior",
            "RunOffMapBehavior",
            "TemporarilyDefectUpdate",
            "ThreatFinderUpdate",
            "GiveUpgradeUpdate",
            "AttachUpdate",
        ),
    ),
    # -- D: presentation only -------------------------------------------------
    _group(
        "D",
        "draw module: model/effect presentation only",
        (
            "DefaultDraw",
            "W3DBuffDraw",
            "W3DDebrisDraw",
            "W3DDefaultDraw",
            "W3DFloorDraw",
            "W3DHordeModelDraw",
            "W3DLaserDraw",
            "W3DLightDraw",
            "W3DProjectileStreamDraw",
            "W3DPropDraw",
            "W3DQuadrupedDraw",
            "W3DSailModelDraw",
            "W3DScriptedModelDraw",
            "W3DStreakDraw",
            "W3DTornadoDraw",
            "W3DTreeDraw",
            "W3DTruckDraw",
        ),
    ),
    _group(
        "D",
        "client-side audio; no simulation semantics",
        (
            "AnimationSoundClientBehavior",
            "ModelConditionAudioLoopClientBehavior",
            "ModelConditionSoundSelectorClientBehavior",
            "RandomSoundSelectorClientBehavior",
            "UpgradeSoundSelectorClientBehavior",
            "TerrainResourceClientBehavior",
            "LargeGroupAudioUpdate",
            "AudioLoopUpgrade",
            "AnnounceBirthAndDeathBehavior",
            "EvaAnnounceClientCreate",
        ),
    ),
    _group(
        "D",
        "client-side visual/UI update; no simulation semantics",
        (
            "BeaconClientUpdate",
            "RadarMarkerClientUpdate",
            "SwayClientUpdate",
            "LaserUpdate",
            "TransitionDamageFX",
            "FXListDie",
            "HitReactionBehavior",
            "ClickReactionBehavior",
            "FadeAndDieOrnamentUpdate",
        ),
    ),
    _group(
        "D",
        "visual sub-object/model-condition toggle per upgrade",
        ("SubObjectsUpgrade", "ModelConditionUpgrade"),
    ),
    _group(
        "D",
        "UI text only",
        ("TooltipUpgrade",),
    ),
    # -- E: evidence of dead authoring ---------------------------------------
    _group(
        "E",
        "authored once in RotWK with a misspelled ModuleTag; class absent from "
        "the BFME2 tree; suspected dead authoring artifact - verify against the "
        "engine before scheduling any work",
        ("HordeTransportContainDamage",),
    ),
    _group(
        "E",
        "legacy GreatKeep authoring is inert in both retail trees: the carrier "
        "references absent BattleTowerCommandSet, the only command button is "
        "commented out, and garrison KindOf flags are commented out",
        ("ManTheWallsSpecialPower",),
    ),
)


def _build_classification() -> dict[str, tuple[str, str]]:
    table: dict[str, tuple[str, str]] = {}
    for group in _CLASSIFICATION_GROUPS:
        for folded, row in group.items():
            if folded in table:
                raise ModuleCensusError(f"duplicate classification entry: {folded}")
            table[folded] = row
    return table


MODULE_CLASSIFICATION: dict[str, tuple[str, str]] = _build_classification()


def classification_for(name: str) -> tuple[str, str] | None:
    return MODULE_CLASSIFICATION.get(name.casefold())


# ---------------------------------------------------------------------------
# Census assembly and serialization
# ---------------------------------------------------------------------------


CENSUS_COMMENT = (
    "Retail module-type census. Regenerate with `python -m "
    "openbfme_importer.module_census`; do not hand-edit. The denominator for "
    "usage-weighted behaviour coverage. Module type NAMES and COUNTS only - no "
    "retail INI content or stat values. CAVEAT: status 'consumed' means the "
    "importer pipeline names the type and extracts from it, NOT that the "
    "behaviour runs; the runtime adapter reads the flattened description and "
    "names almost no behaviours itself. Class C members need simulation work "
    "no importer change can provide."
)

CENSUS_CORPUS = (
    "Every effective data/ini/**.ini|.inc document (BIG-archive precedence "
    "winners) per tree; a module declaration is a carrier-key assignment "
    "(Behavior/Body/Draw/ClientUpdate/ClientBehavior/Flasher) or a "
    "ModuleTag-shaped assignment inside Object/ChildObject/ObjectReskin "
    "definitions."
)


def build_module_census(
    tree_usages: Mapping[str, TreeUsage], support: ModuleSupport
) -> dict[str, Any]:
    """Assemble the census document from per-tree usage and measured support.

    Raises when an observed module kind has no classification: an unclassified
    member would silently fall out of every A-E total.
    """

    tree_labels = sorted(tree_usages)
    all_folded: dict[str, str] = {}
    for label in tree_labels:
        for folded, usage in tree_usages[label].members.items():
            display = usage.display_name
            if folded not in all_folded or display < all_folded[folded]:
                all_folded[folded] = display
    unclassified = sorted(
        all_folded[folded] for folded in all_folded if folded not in MODULE_CLASSIFICATION
    )
    if unclassified:
        raise ModuleCensusError(
            "observed module kinds lack an A-E classification: "
            + ", ".join(unclassified)
        )

    members: list[dict[str, Any]] = []
    for folded, display in all_folded.items():
        letter, note = MODULE_CLASSIFICATION[folded]
        sites = {
            label: tree_usages[label].members[folded].sites
            if folded in tree_usages[label].members
            else 0
            for label in tree_labels
        }
        objects = {
            label: len(tree_usages[label].members[folded].objects)
            if folded in tree_usages[label].members
            else 0
            for label in tree_labels
        }
        families = sorted(
            {
                family
                for label in tree_labels
                if folded in tree_usages[label].members
                for family in tree_usages[label].members[folded].families
            }
        )
        carriers = sorted(
            {
                carrier
                for label in tree_labels
                if folded in tree_usages[label].members
                for carrier in tree_usages[label].members[folded].carriers
            }
        )
        member: dict[str, Any] = {
            "name": display,
            "carriers": carriers,
            "classification": letter,
            "classificationNote": note,
            "status": support.status(folded),
            "consumedBy": list(support.consumed.get(folded, ())),
            "refusedBy": list(support.refused.get(folded, ())),
            "refusalReasons": list(support.refusal_reasons.get(folded, ())),
            "declarationSites": sites,
            "objectCount": objects,
            "families": families,
        }
        if folded == "destroydie" and member["status"] == "consumed":
            member["compilerConsumedSubset"] = (
                "default ALL, explicit ALL, and ALL -TOPPLED declarations "
                "reached by playable-unit descriptor compilation"
            )
            member["runtimeExecutableSubset"] = (
                "default/explicit ALL on materialized playable-unit object "
                "and container owners"
            )
            member["runtimeDeferredSubset"] = (
                "primaryMember owners remain compiler evidence only because "
                "the simulation has no independently materialized member "
                "corpse/presentation lifecycle"
            )
        elif (
            folded == "queueproductionexitupdate"
            and member["status"] == "consumed"
        ):
            member["compilerConsumedSubset"] = (
                "syntactically valid declarations reached by "
                "playable-structure descriptor compilation"
            )
            member["knownFailClosedSubset"] = (
                "RotWK AngmarKennelExpansion malformed NaturalRallyPoint"
            )
            member["runtimeExecutableSubset"] = (
                "row-level only: UnitCreatePoint, NaturalRallyPoint, ExitDelay, "
                "PlacementViewAngle, and NoExitPath with valid numeric coordinates; "
                "UseReturnToFormation, AllowAirborneCreation, InitialBurst, "
                "CanRallyToSlaughter, and malformed coordinates remain deferred"
            )
        elif folded == "slavedupdate" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "all canonical typed gameplay fields; FadeOutRange, FadeTime, "
                "and UseSlaverAsControlForEvaObjectSightedEvents retain explicit "
                "presentation receipts"
            )
        elif folded == "spawnbehavior" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "row-level only: the binary-proven full-initial enabled-reclaim "
                "shape (SpawnNumber, matching InitialBurst, SpawnReplaceDelay, "
                "SpawnTemplateName, CanReclaimOrphans=Yes); 6 authored/8 effective "
                "BFME2 rows and 9 authored/13 effective RotWK rows"
            )
            member["runtimeDeferredSubset"] = (
                "all other row shapes; FadeInTime, "
                "KillSpawnsBasedOnModelConditionState, SpawnInsideBuilding, "
                "RespectCommandLimit, OneShot, SpawnedRequireSpawner, "
                "ShareUpgrades, and TriggeredBy keep the row deferred"
            )
        elif folded == "squishcollide" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "entire canonical fieldless marker schema; authored victim "
                "collision admission only"
            )
        elif folded == "rebuildholeexposeddie" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "all canonical rows; TransferAttackers is canonically No, while "
                "FadeInTimeSeconds retains an explicit presentation receipt"
            )
        elif folded == "rebuildholebehavior" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "all canonical rows, including WorkerObjectName completion evidence "
                "and exact per-frame health regeneration remainder"
            )
        elif folded == "salvagecratecollide" and member["status"] == "consumed":
            member["runtimeExecutableSubset"] = (
                "all canonical rows under the binary-oracle active/dead/parsed-ignored "
                "field partition"
            )
        elif folded == "autodepositupdate" and member["status"] == "consumed":
            member["compilerConsumedSubset"] = (
                "consumer recognition is implemented; 29 BFME2 and 37 RotWK "
                "are observed declaration sites, not measured descriptor "
                "outputs"
            )
            member["runtimeExecutableSubset"] = (
                "focused materialized descriptor fixture only; no real retail "
                "materialized-descriptor receipt is recorded by this census"
            )
            member["runtimeDeferredSubset"] = (
                "all unmeasured retail carriers, plus BFME-only "
                "GiveNoXP/OnlyWhenGarrisoned rows"
            )
        members.append(member)
    members.sort(
        key=lambda row: (
            -sum(row["declarationSites"].values()),
            str(row["name"]).casefold(),
        )
    )

    return {
        "$comment": CENSUS_COMMENT,
        "corpus": CENSUS_CORPUS,
        "classes": dict(sorted(MODULE_CLASS_MEANINGS.items())),
        "trees": {
            label: {
                "documentCount": tree_usages[label].document_count,
                "objectCount": tree_usages[label].object_count,
                "declarationSites": tree_usages[label].declaration_sites,
                "distinctModuleKinds": len(tree_usages[label].members),
            }
            for label in tree_labels
        },
        "supportedScan": {
            "method": (
                "AST literal scan over the package import closure of the "
                "shipped pipeline roots; docstrings excluded; entries of "
                "UNSUPPORTED/UNIMPLEMENTED containers recorded as refusals"
            ),
            "rootModules": list(PIPELINE_ROOT_MODULES),
            "consumerFileCount": len(support.consumer_files),
            "consumerFiles": list(support.consumer_files),
        },
        "members": members,
    }


def census_json_bytes(census: Mapping[str, Any]) -> bytes:
    """Deterministic serialization: tab indent, LF line ends, trailing LF."""

    text = json.dumps(census, indent="\t", ensure_ascii=False)
    return text.replace("\r\n", "\n").encode("utf-8") + b"\n"


def write_module_census(path: Path, census: Mapping[str, Any]) -> None:
    data = census_json_bytes(census)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(data)


def load_module_census(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or "members" not in value:
        raise ModuleCensusError(f"not a module census document: {path}")
    return value


def census_catalog_paths(state_root: Path | None = None) -> dict[str, Path]:
    root = state_root if state_root is not None else default_state_root()
    return {
        label: root / "catalog" / f"{game_id}.json"
        for game_id, label in sorted(CENSUS_TREES.items())
    }


def generate_retail_module_census(
    state_root: Path | None = None, package_dir: Path | None = None
) -> dict[str, Any]:
    """Build the census from the real retail catalogs of both trees.

    Raises ``ModuleCensusError`` naming every missing catalog rather than
    producing numbers from a partial corpus.
    """

    paths = census_catalog_paths(state_root)
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise ModuleCensusError(
            "retail catalogs unavailable (run the importer catalog step "
            "first): " + ", ".join(missing)
        )
    usages = {
        label: collect_catalog_tree_usage(InstallCatalog.load(path))
        for label, path in paths.items()
    }
    vocabulary = {
        folded for usage in usages.values() for folded in usage.members
    }
    support = scan_module_support(vocabulary, package_dir=package_dir)
    return build_module_census(usages, support)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Regenerate the committed retail module-type census."
    )
    parser.add_argument(
        "--state-root",
        type=Path,
        default=None,
        help="importer state root holding catalog/{bfme2,rotwk}.json",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_CENSUS_PATH,
        help="census output path (default: game/data/retail_module_census.json)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed census is current instead of writing",
    )
    args = parser.parse_args(argv)
    try:
        census = generate_retail_module_census(args.state_root)
    except ModuleCensusError as exc:
        print(f"SKIP: {exc}", file=sys.stderr)
        return 3
    data = census_json_bytes(census)
    if args.check:
        current = args.out.read_bytes() if args.out.is_file() else b""
        if current != data:
            print(f"census is stale: {args.out}", file=sys.stderr)
            return 1
        print(f"census is current: {args.out}")
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as stream:
        stream.write(data)
    print(f"wrote {args.out} ({len(census['members'])} members)")
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
