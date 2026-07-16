"""Plan the exact retail Gondor Archer projectile/impact presentation closure.

This module deliberately does not call the arrow streak a particle system.  In
BFME II 1.06 it is a ``W3DStreakDraw`` over ``EXArrowStreak01.tga``.  The
impact is the separate ``FX_GoodArrowHit`` attached ``g_arrow`` model plus the
``ImpactArrow`` event.  The planner binds those authored facts to exact
manifest bytes and emits only a payload-free ImportProfile fragment/runtime
document.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import tempfile
from typing import Any, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .profile import ImportProfile, resolve_profile
from .retail_visual_profile import (
    _canonical_sha256,
    _case_unique,
    _list,
    _mapping,
    _safe_virtual_path,
    _source_record,
    _validate_declared_digest,
    _validate_effective_manifest,
)
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


ARCHER_PROJECTILE_PLAN_SCHEMA = "openbfme.retail-archer-projectile-plan"
ARCHER_PROJECTILE_PLAN_SCHEMA_VERSION = 0
ARCHER_PROJECTILE_BINDING_SCHEMA = "openbfme.archer-projectile-binding"
ARCHER_PROJECTILE_BINDING_SCHEMA_VERSION = 0
RUNTIME_DATA_PATH = "combat/gondor-archer-projectile.json"

OBJECT_SOURCE = "data/ini/object/goodfaction/goodfactionsubobjects.ini"
WEAPON_SOURCE = "data/ini/weapon.ini"
DAMAGE_FX_SOURCE = "data/ini/damagefx.ini"
FX_LIST_SOURCE = "data/ini/fxlist.ini"
SOUND_SOURCE = "data/ini/soundeffects.ini"
G_ARROW_MODEL = "art/w3d/g_/g_arrow.w3d"
G_ARROW_TEXTURE = "art/compiledtextures/g_/g_arrow.dds"
STREAK_TEXTURE = "art/compiledtextures/ex/exarrowstreak01.dds"
STREAK_SNOW_TEXTURE = "art/compiledtextures/ex/exarrowstreak_snow.dds"

_DATA_ADDITIONS = (OBJECT_SOURCE, DAMAGE_FX_SOURCE, FX_LIST_SOURCE)
_REUSED_LEAVES = (WEAPON_SOURCE, SOUND_SOURCE, G_ARROW_TEXTURE)
_MAX_JSON_BYTES = 2 * 1024 * 1024 * 1024
_MAX_SOURCE_BYTES = 64 * 1024 * 1024
_BLOCK_HEADER = re.compile(
    r"^(Object|ObjectReskin|Weapon|DamageFX|FXList|AudioEvent)\s+(\S+)(?:\s+(\S+))?$",
    re.IGNORECASE,
)
_NESTED_BLOCK = re.compile(
    r"^(Draw|Body|Behavior|ClientBehavior|DefaultModelConditionState|"
    r"ModelConditionState|AnimationState|IdleAnimationState|Animation|ArmorSet|"
    r"WeaponSet|ProjectileNugget|DamageNugget|Sound|AttachedModel)\b",
    re.IGNORECASE,
)


def _strip_comment(line: str) -> str:
    positions = [position for marker in (";", "//") if (position := line.find(marker)) >= 0]
    if positions:
        line = line[: min(positions)]
    return line.strip()


def _source_span(payload: bytes, start_line: int, end_line: int) -> bytes:
    lines = payload.splitlines(keepends=True)
    if start_line < 1 or end_line < start_line or end_line > len(lines):
        raise ValueError("source block span is outside the source document")
    return b"".join(lines[start_line - 1 : end_line])


def _named_blocks(payload: bytes, kind: str, name: str) -> list[dict[str, Any]]:
    try:
        text = payload.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("retail INI is not CP1252") from exc
    lines = text.splitlines()
    matches: list[dict[str, Any]] = []
    target = (kind.casefold(), name.casefold())
    for start, raw in enumerate(lines, start=1):
        content = _strip_comment(raw)
        header = _BLOCK_HEADER.fullmatch(content)
        if header is None or (header.group(1).casefold(), header.group(2).casefold()) != target:
            continue
        depth = 1
        for end in range(start + 1, len(lines) + 1):
            active = _strip_comment(lines[end - 1])
            if not active:
                continue
            if active.casefold() == "end":
                depth -= 1
                if depth == 0:
                    span = _source_span(payload, start, end)
                    matches.append(
                        {
                            "kind": header.group(1),
                            "name": header.group(2),
                            "parent": header.group(3),
                            "startLine": start,
                            "endLine": end,
                            "sha256": hashlib.sha256(span).hexdigest(),
                            "byteLength": len(span),
                            "activeLines": [
                                _strip_comment(value)
                                for value in lines[start - 1 : end]
                                if _strip_comment(value)
                            ],
                        }
                    )
                    break
            elif _NESTED_BLOCK.match(active):
                # SAGE nested blocks (Draw, ArmorSet, Behavior, DamageNugget,
                # Sound, AttachedModel, etc.) are terminated by End.
                depth += 1
        else:
            raise ValueError(f"unterminated {kind} {name} block")
    return matches


def _one_block(payload: bytes, kind: str, name: str) -> dict[str, Any]:
    matches = _named_blocks(payload, kind, name)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {kind} {name}, found {len(matches)}")
    return matches[0]


def _active_text(block: Mapping[str, Any]) -> str:
    return "\n".join(str(value) for value in block["activeLines"])


def _require(pattern: str, text: str, label: str, *, count: int = 1) -> list[re.Match[str]]:
    matches = list(re.finditer(pattern, text, re.IGNORECASE | re.MULTILINE))
    if len(matches) != count:
        raise ValueError(f"{label} expected {count} authored matches, found {len(matches)}")
    return matches


def _assignments(block: Mapping[str, Any], field: str) -> list[str]:
    pattern = rf"^{re.escape(field)}\s*=\s*(.+?)\s*$"
    return [match.group(1).strip() for match in re.finditer(pattern, _active_text(block), re.I | re.M)]


def _one_assignment(block: Mapping[str, Any], field: str) -> str:
    values = _assignments(block, field)
    if len(values) != 1:
        raise ValueError(f"{block['kind']} {block['name']} field {field} is not unique")
    return values[0]


class _PrivateReader:
    def __init__(self, root: Path | str, sources: Mapping[str, Mapping[str, Any]]) -> None:
        self.root = Path(root).expanduser().resolve()
        if not self.root.is_dir():
            raise ValueError("effective-assets root is not a directory")
        self.sources = sources
        self.payloads: dict[str, bytes] = {}

    def read(self, path: str) -> bytes:
        canonical = _safe_virtual_path(path, "retail source path")
        if canonical in self.payloads:
            return self.payloads[canonical]
        record = self.sources.get(canonical)
        if record is None:
            raise ValueError(f"retail source is absent from manifest: {canonical}")
        if int(record["size"]) > _MAX_SOURCE_BYTES:
            raise ValueError(f"retail source exceeds byte limit: {canonical}")
        source = self.root.joinpath(*safe_relative_parts(canonical)).resolve()
        try:
            source.relative_to(self.root)
        except ValueError as exc:
            raise ValueError("retail source escaped effective-assets root") from exc
        if not source.is_file() or source.stat().st_size != int(record["size"]):
            raise ValueError(f"retail source size mismatch: {canonical}")
        payload = source.read_bytes()
        if hashlib.sha256(payload).hexdigest() != record["sha256"]:
            raise ValueError(f"retail source SHA-256 mismatch: {canonical}")
        self.payloads[canonical] = payload
        return payload

    def evidence(self) -> dict[str, Any]:
        rows = [
            {
                "virtualPath": path,
                "byteLength": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            for path, payload in sorted(self.payloads.items(), key=lambda item: item[0].casefold())
        ]
        return {
            "policy": "manifest-bound-files-below-private-effective-assets-root",
            "sourceCount": len(rows),
            "byteLength": sum(row["byteLength"] for row in rows),
            "sources": rows,
            "aggregateSha256": _canonical_sha256(rows),
        }


def _block_evidence(path: str, block: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "sourceVirtualPath": path,
        "kind": block["kind"],
        "name": block["name"],
        "parent": block["parent"],
        "startLine": block["startLine"],
        "endLine": block["endLine"],
        "byteLength": block["byteLength"],
        "sha256": block["sha256"],
    }


def _audio_event(block: Mapping[str, Any], expected_count: int) -> dict[str, Any]:
    tokens: list[str] = []
    for value in _assignments(block, "Sounds"):
        tokens.extend(value.split())
    if len(tokens) != expected_count or len({value.casefold() for value in tokens}) != len(tokens):
        raise ValueError(f"AudioEvent {block['name']} sound pool is not exact")
    settings = {}
    for field in (
        "Control",
        "Limit",
        "PitchShift",
        "VolumeShift",
        "Volume",
        "Priority",
        "Type",
        "SubmixSlider",
    ):
        settings[field] = _one_assignment(block, field)
    return {
        "eventId": block["name"],
        "soundTokens": tokens,
        "sourceVirtualPaths": [f"data/audio/sounds/{token.casefold()}.wav" for token in tokens],
        "settings": settings,
        "source": _block_evidence(SOUND_SOURCE, block),
    }


def _find_reuse_resources(
    profile_document: Mapping[str, Any], profile: ImportProfile
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    raw_resources = _list(profile_document.get("resources"), "base profile resources")
    owners: dict[str, dict[str, Any]] = {}
    requirements: list[dict[str, Any]] = []
    for path in _REUSED_LEAVES:
        matches = [
            resource
            for resource in raw_resources
            if path.casefold()
            in {str(pattern).casefold() for pattern in resource.get("patterns", [])}
        ]
        if len(matches) != 1:
            raise ValueError(f"base profile must have exactly one owner for reused leaf {path}")
        resource = deepcopy(matches[0])
        resource_id = str(resource["id"])
        owners[resource_id] = resource
        requirements.append(
            {
                "sourceVirtualPath": path,
                "existingResourceId": resource_id,
                "existingConverter": resource.get("converter", "copy"),
                "selection": "reuse-existing-resource-owner",
            }
        )
    known_ids = {resource.id for resource in profile.resources}
    if not set(owners) <= known_ids:
        raise ValueError("base profile raw resources disagree with ImportProfile")
    return [owners[key] for key in sorted(owners)], requirements


def _profile_document(
    resources: Sequence[Mapping[str, Any]],
    reused_resources: Sequence[Mapping[str, Any]],
    runtime: Mapping[str, Any],
) -> dict[str, Any]:
    return {
        "format": 1,
        "id": "men-fords-v0-archer-projectile-generated",
        "title": "Private BFME II exact Gondor Archer projectile closure",
        "pack": {
            "id": "bfme2-men-vslice-archer-projectile-private",
            "version": "1.06-plan-v0",
            "dataPolicy": {"externalPathsAllowed": False, "redistributable": False},
        },
        "resources": [deepcopy(dict(value)) for value in (*reused_resources, *resources)],
        "runtime_data": {RUNTIME_DATA_PATH: deepcopy(dict(runtime))},
    }


def _validate_and_resolve_profile(
    resources: Sequence[Mapping[str, Any]],
    reused_resources: Sequence[Mapping[str, Any]],
    runtime: Mapping[str, Any],
    catalog: InstallCatalog,
) -> dict[str, Any]:
    document = _profile_document(resources, reused_resources, runtime)
    with tempfile.TemporaryDirectory(prefix="openbfme-archer-projectile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        profile = ImportProfile.load(path)
    resolved = resolve_profile(profile, catalog)
    if resolved.missing_required:
        raise ValueError(
            "archer projectile profile does not resolve: "
            + ", ".join(resolved.missing_required)
        )
    return {
        "profileResourceCount": len(profile.resources),
        "fragmentResourceCount": len(resources),
        "reusedOwnerResourceCount": len(reused_resources),
        "selectedFileCount": len(resolved.selected_entries),
        "missingRequired": [],
    }


def build_retail_archer_projectile_plan(
    manifest: Mapping[str, Any],
    base_profile_document: Mapping[str, Any],
    combat_report_bytes: bytes,
    effective_assets_root: Path | str,
    catalog: InstallCatalog,
    *,
    catalog_sha256: str,
) -> dict[str, Any]:
    """Build and seal one exact, deterministic payload-free closure plan."""

    sources, manifest_evidence = _validate_effective_manifest(manifest)
    reader = _PrivateReader(effective_assets_root, sources)
    with tempfile.TemporaryDirectory(prefix="openbfme-archer-base-") as raw:
        base_path = Path(raw) / "base.json"
        base_path.write_text(json.dumps(base_profile_document), encoding="utf-8")
        base_profile = ImportProfile.load(base_path)
    reused_resources, reuse_requirements = _find_reuse_resources(
        base_profile_document, base_profile
    )

    report_text = combat_report_bytes.decode("utf-8")
    for required in (
        "Gondor Archer projectile closure",
        "reskins `GoodFactionArrow`",
        "FX_GoodArrowHit",
        "ImpactArrow",
    ):
        if required not in report_text:
            raise ValueError(f"combat visual report is missing required finding: {required}")

    object_payload = reader.read(OBJECT_SOURCE)
    weapon_payload = reader.read(WEAPON_SOURCE)
    damage_payload = reader.read(DAMAGE_FX_SOURCE)
    fx_payload = reader.read(FX_LIST_SOURCE)
    sound_payload = reader.read(SOUND_SOURCE)
    good = _one_block(object_payload, "Object", "GoodFactionArrow")
    gondor = _one_block(object_payload, "ObjectReskin", "GondorArcherArrow")
    if gondor["parent"] != "GoodFactionArrow":
        raise ValueError("GondorArcherArrow no longer reskins GoodFactionArrow")
    for block in (good, gondor):
        text = _active_text(block)
        _require(r"^Model\s*=\s*NONE\s*$", text, f"{block['name']} NONE model")
        expected = {
            "Length": "15",
            "Width": "2",
            "NumSegments": "1",
            "Color": "R:255 G:255 B:255",
            "Additive": "No",
            "Texture": "EXArrowStreak01.tga",
            "WeatherTexture": "SNOWY EXArrowStreak_Snow.tga",
        }
        for field, value in expected.items():
            if _one_assignment(block, field) != value:
                raise ValueError(f"{block['name']} {field} drifted from retail contract")
    good_text = _active_text(good)
    for pattern, label in (
        (r"^FirstHeight\s*=\s*9$", "FirstHeight"),
        (r"^SecondHeight\s*=\s*9$", "SecondHeight"),
        (r"^FirstPercentIndent\s*=\s*20%$", "FirstPercentIndent"),
        (r"^SecondPercentIndent\s*=\s*90%$", "SecondPercentIndent"),
        (r"^FlightPathAdjustDistPerSecond\s*=\s*50$", "tracking adjustment"),
        (r"^GroundHitFX\s*=\s*FX_GondorArrowDeath$", "ground hit FX"),
        (r"^CurveFlattenMinDist\s*=\s*100\.0$", "curve flatten distance"),
    ):
        _require(pattern, good_text, f"GoodFactionArrow {label}")

    bow = _one_block(weapon_payload, "Weapon", "GondorArcherBow")
    warhead = _one_block(weapon_payload, "Weapon", "GondorArcherBowWarhead")
    bow_text = _active_text(bow)
    _require(r"^ProjectileTemplateName\s*=\s*GondorArcherArrow$", bow_text, "normal projectile")
    _require(r"^WarheadTemplateName\s*=\s*GondorArcherBowWarhead$", bow_text, "normal warhead")
    _require(r"^FireFX\s*=\s*FX_RohanArcherBowWeapon$", bow_text, "archer fire FX")
    _require(r"^DamageFXType\s*=\s*GOOD_ARROW_PIERCE$", _active_text(warhead), "warhead damage FX")

    damage_rows: list[dict[str, Any]] = []
    for name in ("NormalDamageFX", "GwaihirDamageFX", "FellBeastDamageFX", "MumakilDamageFX"):
        block = _one_block(damage_payload, "DamageFX", name)
        _require(
            r"^MajorFX\s*=\s*GOOD_ARROW_PIERCE\s+FX_GoodArrowHit$",
            _active_text(block),
            f"{name} GOOD_ARROW_PIERCE mapping",
        )
        damage_rows.append(
            {"damageFxSetId": name, "damageFxType": "GOOD_ARROW_PIERCE", "majorFxListId": "FX_GoodArrowHit", "source": _block_evidence(DAMAGE_FX_SOURCE, block)}
        )
    hit_fx = _one_block(fx_payload, "FXList", "FX_GoodArrowHit")
    _require(r"^Name\s*=\s*ImpactArrow$", _active_text(hit_fx), "impact sound")
    _require(r"^Modelname\s*=\s*g_arrow$", _active_text(hit_fx), "impact attached model")
    fire_fx = _one_block(fx_payload, "FXList", "FX_RohanArcherBowWeapon")
    _require(r"^Name\s*=\s*ArcherWeapon$", _active_text(fire_fx), "fire sound")

    archer_audio = _audio_event(_one_block(sound_payload, "AudioEvent", "ArcherWeapon"), 32)
    impact_audio = _audio_event(_one_block(sound_payload, "AudioEvent", "ImpactArrow"), 68)
    audio_paths = archer_audio["sourceVirtualPaths"] + impact_audio["sourceVirtualPaths"]
    _case_unique(audio_paths, "archer projectile audio leaf")

    model_payload = reader.read(G_ARROW_MODEL)
    metadata = scan_w3d_metadata(model_payload, G_ARROW_MODEL)
    if set(value.casefold() for value in metadata.model_ids) != {"g_arrow", "g_arrow.line02"}:
        raise ValueError("g_arrow model headers drifted")
    if tuple(value.casefold() for value in metadata.hierarchy_ids) != ("g_arrow",):
        raise ValueError("g_arrow hierarchy header drifted")
    if tuple(value.casefold() for value in metadata.animation_ids) != ("g_arrow",):
        raise ValueError("g_arrow animation header drifted")
    textures = [value.identifier.casefold() for value in metadata.texture_references]
    if textures != ["g_arrow.tga"]:
        raise ValueError("g_arrow texture reference drifted")
    for path in (G_ARROW_TEXTURE, STREAK_TEXTURE, STREAK_SNOW_TEXTURE, *audio_paths):
        reader.read(path)

    runtime = {
        "schema": ARCHER_PROJECTILE_BINDING_SCHEMA,
        "schemaVersion": ARCHER_PROJECTILE_BINDING_SCHEMA_VERSION,
        "unitObjectId": "GondorArcher",
        "weapon": {
            "weaponTemplateId": "GondorArcherBow",
            "projectileTemplateId": "GondorArcherArrow",
            "inheritedProjectileObjectId": "GoodFactionArrow",
            "warheadTemplateId": "GondorArcherBowWarhead",
            "fireFxListId": "FX_RohanArcherBowWeapon",
            "speed": {"nominal": 321, "minimum": 241, "maximum": 481, "scaleWithRange": True},
            "hitPercentage": 100,
            "scatterRadius": 16.0,
            "source": _block_evidence(WEAPON_SOURCE, bow),
        },
        "projectilePresentation": {
            "kind": "W3DStreakDraw",
            "model": None,
            "length": 15,
            "width": 2,
            "numSegments": 1,
            "color": {"r": 255, "g": 255, "b": 255},
            "additive": False,
            "texture": "assets/textures/combat/archer/exarrowstreak01.png",
            "snowTexture": "assets/textures/combat/archer/exarrowstreak_snow.png",
            "trajectory": {
                "kind": "BezierProjectileBehavior",
                "firstHeight": 9,
                "secondHeight": 9,
                "firstPercentIndent": 20,
                "secondPercentIndent": 90,
                "flightPathAdjustDistPerSecond": 50,
                "curveFlattenMinDist": 100.0,
                "groundHitFxListId": "FX_GondorArrowDeath",
            },
            "source": _block_evidence(OBJECT_SOURCE, good),
            "reskinSource": _block_evidence(OBJECT_SOURCE, gondor),
        },
        "damage": {
            "damageType": "PIERCE",
            "damageFxType": "GOOD_ARROW_PIERCE",
            "majorFxMappings": damage_rows,
            "source": _block_evidence(WEAPON_SOURCE, warhead),
        },
        "impactPresentation": {
            "fxListId": "FX_GoodArrowHit",
            "soundEventId": "ImpactArrow",
            "attachedModelId": "g_arrow",
            "glb": "assets/models/combat/g-arrow.glb",
            "source": _block_evidence(FX_LIST_SOURCE, hit_fx),
        },
        "audioEvents": [archer_audio, impact_audio],
        "unresolvedEngineSemantics": [
            "Projectile spawn time must come from the authoritative Gondor Archer attack event; the INI closure does not prove the Godot animation event frame.",
            "GOOD_ARROW_PIERCE resolves through the struck object's DamageFX set; the four authored mappings are preserved and must not be treated as one global unconditional hit effect.",
            "The retail source does not specify deterministic random-pool selection seeds for ArcherWeapon or ImpactArrow.",
            "g_arrow is an impact AttachedModel with a W3D animation header; its attachment orientation, lifetime, and playback policy require an original-engine runtime oracle.",
            "FX_GondorArrowDeath is the ground-hit branch and is named here but lies outside the requested target-hit closure.",
        ],
    }

    resources = [
        {
            "id": "archer-projectile-definition-evidence",
            "kind": "data",
            "patterns": list(_DATA_ADDITIONS),
            "required": True,
            "converter": "hash-only",
            "limit": len(_DATA_ADDITIONS),
            "expected_count": len(_DATA_ADDITIONS),
        },
        {
            "id": "archer-projectile-streak-textures",
            "kind": "texture",
            "patterns": [STREAK_TEXTURE, STREAK_SNOW_TEXTURE],
            "required": True,
            "converter": "texture",
            "output": "assets/textures/combat/archer/{stem}.png",
            "limit": 2,
            "expected_count": 2,
        },
        {
            "id": "archer-projectile-impact-model",
            "kind": "model",
            "patterns": [G_ARROW_MODEL],
            "required": True,
            "converter": "w3d-bundle",
            "output": "assets/models/combat/g-arrow.glb",
            "limit": 1,
            "expected_count": 1,
            "options": {
                "model": "g_arrow.w3d",
                "animations": ["g_arrow.w3d"],
                "inputResourceIds": [next(row["existingResourceId"] for row in reuse_requirements if row["sourceVirtualPath"] == G_ARROW_TEXTURE)],
            },
        },
        {
            "id": "archer-projectile-audio-leaves",
            "kind": "audio",
            "patterns": audio_paths,
            "required": True,
            "converter": "audio",
            "output": "assets/audio/combat/archer/{stem}.wav",
            "limit": len(audio_paths),
            "expected_count": len(audio_paths),
            "options": {"force_pcm": True},
        },
    ]
    _case_unique([resource["id"] for resource in resources], "fragment resource id")
    resolution = _validate_and_resolve_profile(resources, reused_resources, runtime, catalog)
    closure_paths = [*_DATA_ADDITIONS, *_REUSED_LEAVES, G_ARROW_MODEL, STREAK_TEXTURE, STREAK_SNOW_TEXTURE, *audio_paths]
    _case_unique(closure_paths, "exact closure source")

    plan: dict[str, Any] = {
        "schema": ARCHER_PROJECTILE_PLAN_SCHEMA,
        "schemaVersion": ARCHER_PROJECTILE_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "effectiveAssets": manifest_evidence,
            "catalogSha256": catalog_sha256,
            "baseProfileSha256": base_profile.source_sha256,
            "combatVisualReportSha256": hashlib.sha256(combat_report_bytes).hexdigest(),
            "privateReadBoundary": reader.evidence(),
            "definitionBlocks": [
                _block_evidence(OBJECT_SOURCE, good),
                _block_evidence(OBJECT_SOURCE, gondor),
                _block_evidence(WEAPON_SOURCE, bow),
                _block_evidence(WEAPON_SOURCE, warhead),
                *[row["source"] for row in damage_rows],
                _block_evidence(FX_LIST_SOURCE, fire_fx),
                _block_evidence(FX_LIST_SOURCE, hit_fx),
                archer_audio["source"],
                impact_audio["source"],
            ],
            "impactModel": {
                **_source_record(G_ARROW_MODEL, sources),
                "headerIds": {"models": list(metadata.model_ids), "hierarchies": list(metadata.hierarchy_ids), "animations": list(metadata.animation_ids)},
                "textureReferences": [value.identifier for value in metadata.texture_references],
                "conversionClassification": "self-contained-embedded-animation",
            },
        },
        "policy": {
            "selection": "exact-normal-unupgraded-gondor-archer-projectile-and-target-impact",
            "substitutesAllowed": False,
            "genericFallbackAllowed": False,
            "streakClassification": "W3DStreakDraw-not-ParticleSystem",
            "runtimeMustUseAuthoritativeAttackEvent": True,
            "failClosedOnUnresolvedSemantics": True,
        },
        "dedupeContext": {
            "baseProfileId": base_profile.id,
            "reuseRequirements": reuse_requirements,
            "reusedResourceDefinitionsForStandaloneValidation": reused_resources,
        },
        "profileFragment": {
            "resources": resources,
            "runtimeDataPath": RUNTIME_DATA_PATH,
            "runtimeData": runtime,
        },
        "catalogResolution": resolution,
        "summary": {
            "exactClosureSourceFileCount": len(closure_paths),
            "newSelectedSourceFileCount": len(closure_paths) - len(_REUSED_LEAVES),
            "reusedExactLeafCount": len(_REUSED_LEAVES),
            "profileFragmentResourceCount": len(resources),
            "definitionEvidenceFileCount": len(_DATA_ADDITIONS),
            "streakTextureCount": 2,
            "impactModelCount": 1,
            "fireAudioLeafCount": len(archer_audio["soundTokens"]),
            "impactAudioLeafCount": len(impact_audio["soundTokens"]),
            "audioLeafCount": len(audio_paths),
            "damageFxSetMappingCount": len(damage_rows),
            "unresolvedEngineSemanticCount": len(runtime["unresolvedEngineSemantics"]),
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def generated_import_profile(plan: Mapping[str, Any]) -> dict[str, Any]:
    document = _mapping(plan, "archer projectile plan")
    if document.get("schema") != ARCHER_PROJECTILE_PLAN_SCHEMA:
        raise ValueError("unsupported archer projectile plan schema")
    _validate_declared_digest(document, "aggregateSha256", "archer projectile plan")
    fragment = _mapping(document.get("profileFragment"), "profileFragment")
    if fragment.get("runtimeDataPath") != RUNTIME_DATA_PATH:
        raise ValueError("archer projectile runtime path mismatch")
    dedupe = _mapping(document.get("dedupeContext"), "dedupeContext")
    result = _profile_document(
        _list(fragment.get("resources"), "profileFragment resources"),
        _list(dedupe.get("reusedResourceDefinitionsForStandaloneValidation"), "reused resource definitions"),
        _mapping(fragment.get("runtimeData"), "runtimeData"),
    )
    with tempfile.TemporaryDirectory(prefix="openbfme-archer-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(result), encoding="utf-8")
        ImportProfile.load(path)
    return result


def _load_json(path: Path | str, label: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{label} is missing or exceeds the byte limit")
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} root must be an object")
    return value


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--base-profile", required=True)
    parser.add_argument("--combat-report", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--generated-profile")
    args = parser.parse_args(argv)
    root = Path(args.effective_assets).expanduser().resolve()
    manifest_path = Path(args.manifest).expanduser().resolve() if args.manifest else root / ".openbfme" / "manifest.json"
    catalog_path = Path(args.catalog).expanduser().resolve()
    report_path = Path(args.combat_report).expanduser().resolve()
    plan = build_retail_archer_projectile_plan(
        _load_json(manifest_path, "effective-assets manifest"),
        _load_json(args.base_profile, "base profile"),
        report_path.read_bytes(),
        root,
        InstallCatalog.load(catalog_path),
        catalog_sha256=hashlib.sha256(catalog_path.read_bytes()).hexdigest(),
    )
    write_json_atomic(Path(args.output), plan)
    if args.generated_profile:
        write_json_atomic(Path(args.generated_profile), generated_import_profile(plan))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = [
    "ARCHER_PROJECTILE_BINDING_SCHEMA",
    "ARCHER_PROJECTILE_PLAN_SCHEMA",
    "RUNTIME_DATA_PATH",
    "build_retail_archer_projectile_plan",
    "generated_import_profile",
    "main",
]
