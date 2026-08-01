"""Human-readable provenance reports for built content packs.

A pack's provenance manifest is authoritative JSON that, until now, only
``audit_pack`` read. This module answers the same questions for a person:
where did this pack come from, what produced it, what went into it, what
is in it, and is it healthy.

Two hard rules govern everything here:

* **Report, never repair.** ``describe_pack`` reads the pack and consumes
  the audit verdict. It never writes into the pack and never recomputes a
  provenance value to substitute for a recorded one.
* **Never invent or infer a provenance value.** A field the manifest does
  not carry is reported as absent, in words. The ``not_known`` section
  exists precisely so that unanswerable questions are named instead of
  silently omitted.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from .pipeline import (
    PROVENANCE_SOURCE_GIT_EXACT_ROOT,
    PROVENANCE_SOURCE_RELEASE_IDENTITY,
    PROVENANCE_SOURCES,
    audit_pack,
)

REPORT_SCHEMA = "openbfme.pack-provenance-report"
REPORT_SCHEMA_VERSION = 1

_NOT_RECORDED = "not recorded"

_SOURCE_GAME_DESCRIPTIONS = {
    "bfme2-retail-user-owned": (
        "The Battle for Middle-earth II (retail), imported from the user's "
        "own install"
    ),
    "rotwk-retail-user-owned": (
        "The Rise of the Witch-king expansion (retail), imported from the "
        "user's own install"
    ),
}

_PROVENANCE_SOURCE_DESCRIPTIONS = {
    PROVENANCE_SOURCE_RELEASE_IDENTITY: (
        "The importer's commit was stamped into the build when the release "
        "bundle was created, and read back from the bundled release-identity "
        "file. This pack was produced by a shipped release build, and the "
        "stamp itself is covered by the hashed importer inventory."
    ),
    PROVENANCE_SOURCE_GIT_EXACT_ROOT: (
        "The importer's commit was read from a git checkout whose repository "
        "root is exactly the importer's own tree; an enclosing repository's "
        "identity is refused. This pack was produced by a development "
        "checkout, not a stamped release build."
    ),
}

_PRE_PROVENANCE_EXPLANATION = (
    "This pack was built before packs recorded how their own origin was "
    "established. Its recorded commit cannot be verified and may even belong "
    "to an unrelated repository that happened to enclose the importer at "
    "build time. The fix is to re-import the pack with the current importer; "
    "editing the manifest cannot repair provenance."
)

# Substrings audit_pack emits when a pinned tool attestation fails. Used only
# to attribute the audit's own verdict to a tool; never to re-derive it.
_TOOL_PIN_FAILURE_MARKERS = {
    "blender": "Blender attestation does not match the pin",
    "ffmpeg": "FFmpeg attestation does not match the pin",
    "opensage_w3d_plugin": "OpenSAGE plugin attestation does not match the pin",
    "pillow": "Pillow attestation does not match the pin",
    "python": "Python runtime attestation does not match the pin",
}

_MISSING_TOOLS_MARKER = "retail provenance is missing tool attestations: "


def _is_sha256_text(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value.casefold())
    )


def _short_hash(value: Any) -> str:
    if isinstance(value, str) and len(value) >= 12:
        return value[:12] + "..."
    if isinstance(value, str) and value:
        return value
    return _NOT_RECORDED


def _format_bytes(value: Any) -> str:
    if not isinstance(value, int) or value < 0:
        return _NOT_RECORDED
    if value < 1024:
        return f"{value} bytes"
    for unit, scale in (("GB", 1024**3), ("MB", 1024**2), ("KB", 1024)):
        if value >= scale:
            return f"{value / scale:.1f} {unit}"
    return f"{value} bytes"


def _load_json_object(path: Path) -> tuple[dict[str, Any] | None, str | None]:
    """Load a JSON object, returning (value, error) and never raising."""

    if not path.is_file():
        return None, f"{path.name} is missing"
    try:
        with path.open("r", encoding="utf-8") as stream:
            value = json.load(stream)
    except (OSError, ValueError) as exc:
        return None, f"{path.name} could not be read as JSON: {exc}"
    if not isinstance(value, dict):
        return None, f"{path.name} is not a JSON object"
    return value, None


def _declared_inventory(
    manifest: dict[str, Any] | None,
) -> tuple[int | None, int | None, str | None]:
    """Summarize the manifest's own recorded file list.

    The digest is a fingerprint of what the manifest *declares* — path, size
    and sha256 per file — computed without re-reading any pack file. It is
    None whenever the declared inventory is absent or malformed.
    """

    if manifest is None:
        return None, None, None
    inventory = manifest.get("bundle_files")
    if not isinstance(inventory, list) or not inventory:
        return None, None, None
    digest = hashlib.sha256()
    total_bytes = 0
    entries: list[tuple[str, int, str]] = []
    for item in inventory:
        if (
            not isinstance(item, dict)
            or not isinstance(item.get("path"), str)
            or not isinstance(item.get("size"), int)
            or item.get("size", -1) < 0
            or not _is_sha256_text(item.get("sha256"))
        ):
            return len(inventory), None, None
        entries.append((item["path"], item["size"], item["sha256"]))
        total_bytes += item["size"]
    for path, size, sha256 in sorted(entries):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256.encode("ascii"))
        digest.update(b"\n")
    return len(entries), total_bytes, digest.hexdigest()


def _describe_provenance_source(raw: Any) -> tuple[str, bool]:
    """Return (description, pre_provenance) for a recipe's provenance_source."""

    if isinstance(raw, str) and raw in PROVENANCE_SOURCES:
        return _PROVENANCE_SOURCE_DESCRIPTIONS[raw], False
    if isinstance(raw, str) and raw:
        return (
            f"Declared as '{raw}', which this importer does not recognize. "
            "The commit's origin cannot be interpreted and must be treated "
            "as unverified.",
            True,
        )
    return ("Not declared. " + _PRE_PROVENANCE_EXPLANATION, True)


def _explain_audit_error(error: str) -> str | None:
    """Translate one audit error into plain language, or None to pass through."""

    if (
        "does not declare how its commit was established" in error
        or "does not name the requirements pin" in error
    ):
        return _PRE_PROVENANCE_EXPLANATION
    if error == "missing pack.json":
        return (
            "The pack has no pack.json, so the game cannot even identify it. "
            "This is not a complete pack."
        )
    if error == "missing provenance/manifest.json":
        return (
            "The pack has no provenance manifest at all. Nothing about its "
            "origin, sources, tools, or contents can be verified. Re-import "
            "the pack to produce one."
        )
    if error.startswith("invalid provenance manifest"):
        return (
            "The provenance manifest exists but could not be read as JSON — "
            "it is corrupt or truncated. Nothing it claimed can be trusted; "
            "re-import the pack."
        )
    if error.startswith("hash mismatch: "):
        path = error.split(": ", 1)[1]
        return (
            f"The file '{path}' no longer matches the fingerprint recorded "
            "when the pack was built. It was modified, corrupted, or "
            "replaced after the build."
        )
    if error.startswith("size mismatch: "):
        path = error.split(": ", 1)[1]
        return (
            f"The file '{path}' has a different size than the manifest "
            "recorded, so it changed after the pack was built."
        )
    if error.startswith("missing bundle file: "):
        path = error.split(": ", 1)[1]
        return (
            f"The manifest says the pack contains '{path}', but that file "
            "is not on disk."
        )
    if error.startswith("missing or linked bundle file: "):
        path = error.split(": ", 1)[1]
        return (
            f"The file '{path}' is either missing or replaced by a link, "
            "which the pack format refuses."
        )
    if error.startswith("unexpected bundle file: "):
        path = error.split(": ", 1)[1]
        return (
            f"The file '{path}' is present on disk but was not part of the "
            "pack when it was built. Someone or something added it later."
        )
    if _MISSING_TOOLS_MARKER in error:
        names = error.split(": ", 1)[1] if ": " in error else ""
        return (
            "The manifest does not record which versions of these tools "
            f"produced the pack: {names}. Without those attestations there "
            "is no evidence of what software converted the retail files."
        )
    if "attestation does not match the pin" in error:
        return (
            error
            + ". The recorded tool differs from the exact tool version this "
            "project pins, so the converted output may not match a trusted "
            "build."
        )
    if "claims a release identity it did not hash" in error:
        return (
            "The recipe says its commit came from a stamped release "
            "identity, but the identity file is not covered by the hashed "
            "importer inventory — an uncovered stamp attests to nothing."
        )
    if "tree digest disagrees with its file inventory" in error:
        return (
            "The importer code inventory's summary fingerprint does not "
            "match its own file list. The recipe is internally inconsistent "
            "and cannot be trusted."
        )
    return None


def _tool_entries(
    manifest: dict[str, Any] | None,
    pack_data: dict[str, Any] | None,
    audit_errors: list[str],
) -> list[dict[str, Any]]:
    tools_raw = manifest.get("tools") if isinstance(manifest, dict) else None
    if not isinstance(tools_raw, dict):
        tools_raw = {}
    profile = manifest.get("profile") if isinstance(manifest, dict) else None
    is_retail = bool(
        isinstance(pack_data, dict) and pack_data.get("local_retail_import") is True
    )
    pin_checked_profile = profile == "men-fords-v0"

    missing_names: list[str] = []
    for error in audit_errors:
        if _MISSING_TOOLS_MARKER in error:
            missing_names = [
                name.strip()
                for name in error.split(_MISSING_TOOLS_MARKER, 1)[1].split(",")
                if name.strip()
            ]

    entries: list[dict[str, Any]] = []
    for name in sorted(tools_raw):
        recorded = tools_raw[name]
        if not is_retail:
            status, detail = "not-checked", (
                "the audit pin-checks tools only for importer-generated "
                "retail packs"
            )
        elif not pin_checked_profile:
            status, detail = "recorded-not-pinned", (
                "attestation is recorded, but the audit pin-checks tools "
                "only for the men-fords-v0 profile"
            )
        elif any(
            _TOOL_PIN_FAILURE_MARKERS.get(name, "\0") in error
            for error in audit_errors
        ):
            status, detail = "failed-pin-check", (
                "the recorded attestation does not match this project's "
                "pinned tool"
            )
        else:
            status, detail = "verified", (
                "the recorded attestation matches this project's pinned tool"
            )
        entries.append(
            {
                "name": name,
                "recorded": recorded if isinstance(recorded, dict) else None,
                "verification": {"status": status, "detail": detail},
            }
        )
    for name in missing_names:
        if name in tools_raw:
            continue
        entries.append(
            {
                "name": name,
                "recorded": None,
                "verification": {
                    "status": "missing-attestation",
                    "detail": (
                        "required for this pack, but the manifest records "
                        "no attestation for it"
                    ),
                },
            }
        )
    return sorted(entries, key=lambda item: item["name"])


def describe_pack(pack_root: Path | str) -> dict[str, Any]:
    """Build the machine-readable provenance report for one pack.

    Never raises for pack problems: a missing, corrupt, or partial pack
    produces a report whose ``report_errors`` and ``health`` sections say
    exactly what could not be read.
    """

    root = Path(pack_root).expanduser().resolve()
    report_errors: list[str] = []

    if not root.is_dir():
        report_errors.append(f"pack root is not a directory: {root}")

    pack_data, pack_error = _load_json_object(root / "pack.json")
    if pack_error is not None:
        report_errors.append(pack_error)
    manifest, manifest_error = _load_json_object(
        root / "provenance" / "manifest.json"
    )
    if manifest_error is not None:
        report_errors.append(manifest_error)

    try:
        audit = audit_pack(root)
    except Exception as exc:  # report, never traceback
        audit = {
            "valid": False,
            "checked_files": 0,
            "checked_outputs": 0,
            "errors": [f"audit could not run: {exc}"],
        }
        report_errors.append(f"audit could not run: {exc}")
    audit_errors = [
        error for error in audit.get("errors", []) if isinstance(error, str)
    ]

    def manifest_str(key: str) -> str | None:
        if manifest is None:
            return None
        value = manifest.get(key)
        return value if isinstance(value, str) and value else None

    def pack_str(key: str) -> str | None:
        if pack_data is None:
            return None
        value = pack_data.get(key)
        return value if isinstance(value, str) and value else None

    # ---- identity -------------------------------------------------------
    source_game = manifest_str("source_game")
    file_count, total_bytes, content_digest = _declared_inventory(manifest)
    redistributable = (
        pack_data.get("redistributable") if isinstance(pack_data, dict) else None
    )
    identity = {
        "pack_id": pack_str("id"),
        "pack_title": pack_str("title"),
        "pack_version": pack_str("version"),
        "profile": manifest_str("profile"),
        "profile_sha256": manifest_str("profile_sha256"),
        "importer_version": manifest_str("importer_version"),
        "source_game": source_game,
        "source_game_description": _SOURCE_GAME_DESCRIPTIONS.get(source_game or ""),
        "redistributable": (
            redistributable if isinstance(redistributable, bool) else None
        ),
        # The manifest records no build timestamp; this is always null and
        # the gap is named in not_known rather than guessed from file times.
        "built_at": None,
        "declared_file_count": file_count,
        "declared_total_bytes": total_bytes,
        "declared_content_digest_sha256": content_digest,
    }

    # ---- origin ---------------------------------------------------------
    recipe = manifest.get("importer_recipe") if isinstance(manifest, dict) else None
    if not isinstance(recipe, dict):
        recipe = None
    raw_source = recipe.get("provenance_source") if recipe else None
    if recipe is not None:
        source_description, pre_provenance = _describe_provenance_source(raw_source)
        requirements = recipe.get("requirements_files")
        if not (
            isinstance(requirements, list)
            and requirements
            and all(isinstance(item, str) and item for item in requirements)
        ):
            requirements = None
            pre_provenance = True
        recipe_files = recipe.get("files")
        git_commit = recipe.get("git_commit")
        clean = recipe.get("git_worktree_clean")
        origin = {
            "recorded": True,
            "git_commit": git_commit if isinstance(git_commit, str) else None,
            "git_worktree_clean": clean if isinstance(clean, bool) else None,
            "provenance_source": raw_source if isinstance(raw_source, str) else None,
            "provenance_source_description": source_description,
            "pre_provenance_recipe": pre_provenance,
            "requirements_files": requirements,
            "recipe_file_count": (
                len(recipe_files) if isinstance(recipe_files, list) else None
            ),
            "recipe_tree_sha256": (
                recipe.get("tree_sha256")
                if _is_sha256_text(recipe.get("tree_sha256"))
                else None
            ),
        }
    else:
        origin = {
            "recorded": False,
            "git_commit": None,
            "git_worktree_clean": None,
            "provenance_source": None,
            "provenance_source_description": (
                "No importer recipe is recorded, so nothing is known about "
                "the importer source tree that built this pack."
            ),
            "pre_provenance_recipe": False,
            "requirements_files": None,
            "recipe_file_count": None,
            "recipe_tree_sha256": None,
        }

    # ---- tools ----------------------------------------------------------
    tools = _tool_entries(manifest, pack_data, audit_errors)

    # ---- source archives ------------------------------------------------
    archives_raw = manifest.get("source_archives") if manifest else None
    archives: list[dict[str, Any]] = []
    archive_bytes = 0
    archive_bytes_known = True
    if isinstance(archives_raw, list):
        for item in archives_raw:
            if not isinstance(item, dict):
                report_errors.append(
                    "manifest source_archives contains a non-object entry"
                )
                archive_bytes_known = False
                continue
            size = item.get("size")
            if isinstance(size, int) and size >= 0:
                archive_bytes += size
            else:
                archive_bytes_known = False
            matches = item.get("matches_reference")
            archives.append(
                {
                    "relative_path": (
                        item["relative_path"]
                        if isinstance(item.get("relative_path"), str)
                        else None
                    ),
                    "size": size if isinstance(size, int) else None,
                    "sha256": (
                        item["sha256"]
                        if _is_sha256_text(item.get("sha256"))
                        else None
                    ),
                    "matches_reference": (
                        matches if isinstance(matches, bool) else None
                    ),
                }
            )
    elif manifest is not None and archives_raw is not None:
        report_errors.append("manifest source_archives is not an array")
    source_archives = {
        "count": len(archives),
        "total_bytes": archive_bytes if archive_bytes_known else None,
        "archives": archives,
    }

    # ---- contents -------------------------------------------------------
    entries_raw = manifest.get("entries") if manifest else None
    by_kind: dict[str, dict[str, Any]] = {}
    declared_output_paths: set[str] = set()
    entry_count = 0
    if isinstance(entries_raw, list):
        for entry in entries_raw:
            if not isinstance(entry, dict):
                continue
            entry_count += 1
            kind = entry.get("kind")
            kind_key = kind if isinstance(kind, str) and kind else "(unlabeled)"
            bucket = by_kind.setdefault(
                kind_key,
                {
                    "kind": kind_key,
                    "source_count": 0,
                    "output_count": 0,
                    "output_bytes": 0,
                    "converters": set(),
                },
            )
            bucket["source_count"] += 1
            converter = entry.get("converter")
            if isinstance(converter, str) and converter:
                bucket["converters"].add(converter)
            outputs = entry.get("outputs")
            if isinstance(outputs, list):
                for output in outputs:
                    if not isinstance(output, dict):
                        continue
                    bucket["output_count"] += 1
                    size = output.get("size")
                    if isinstance(size, int) and size >= 0:
                        bucket["output_bytes"] += size
                    path = output.get("path")
                    if isinstance(path, str):
                        declared_output_paths.add(path)
    inventory_paths: set[str] = set()
    inventory_list = manifest.get("bundle_files") if manifest else None
    if isinstance(inventory_list, list):
        for item in inventory_list:
            if isinstance(item, dict) and isinstance(item.get("path"), str):
                inventory_paths.add(item["path"])
    importer_written = sorted(inventory_paths - declared_output_paths)
    incomplete_raw = manifest.get("incomplete") if manifest else None
    incomplete: list[dict[str, Any]] = []
    if isinstance(incomplete_raw, list):
        for item in incomplete_raw:
            if isinstance(item, dict):
                incomplete.append(
                    {
                        "resource": (
                            item["resource"]
                            if isinstance(item.get("resource"), str)
                            else None
                        ),
                        "reason": (
                            item["reason"]
                            if isinstance(item.get("reason"), str)
                            else None
                        ),
                    }
                )
    contents = {
        "conversion_entry_count": entry_count,
        "by_kind": [
            {
                "kind": bucket["kind"],
                "source_count": bucket["source_count"],
                "output_count": bucket["output_count"],
                "output_bytes": bucket["output_bytes"],
                "converters": sorted(bucket["converters"]),
            }
            for bucket in sorted(by_kind.values(), key=lambda item: item["kind"])
        ],
        "declared_output_file_count": len(declared_output_paths),
        "importer_written_file_count": len(importer_written),
        "importer_written_files": importer_written,
        "incomplete": incomplete,
    }

    # ---- health ---------------------------------------------------------
    health = {
        "valid": bool(audit.get("valid", False)),
        "audit": audit,
        "errors_explained": [
            {"error": error, "explanation": _explain_audit_error(error)}
            for error in audit_errors
        ],
    }

    # ---- what is not known ---------------------------------------------
    not_known: list[str] = []
    if manifest is None:
        not_known.append(
            "Almost everything. Without a readable provenance manifest the "
            "pack cannot describe its own origin, sources, tools, or "
            "contents."
        )
    not_known.append(
        "When the pack was built. The manifest records no timestamp, and "
        "file times on disk reflect copying, not the build."
    )
    not_known.append(
        "What machine or operating system ran the import. Only the "
        "conversion tools themselves are attested."
    )
    if source_game is not None:
        not_known.append(
            "Whether the retail install the sources came from was "
            "legitimately owned. The manifest proves what bytes went in, "
            "by hash, not who owned them."
        )
    if origin["provenance_source"] == PROVENANCE_SOURCE_GIT_EXACT_ROOT:
        not_known.append(
            "Whether the importer commit used here was ever published as a "
            "release. The identity was read from a development checkout, "
            "not stamped by a release build."
        )
    if origin["git_worktree_clean"] is False:
        not_known.append(
            "What the uncommitted changes in the importer checkout were. "
            "The recipe records only that the source tree was not clean."
        )
    if origin["recorded"] and origin["pre_provenance_recipe"]:
        not_known.append(
            "How the recorded commit was established. Pre-provenance "
            "recipes may carry a commit inherited from an unrelated "
            "enclosing repository."
        )

    return {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "pack_root": str(root),
        "identity": identity,
        "origin": origin,
        "tools": tools,
        "source_archives": source_archives,
        "contents": contents,
        "health": health,
        "not_known": not_known,
        "report_errors": report_errors,
    }


def _value_or(value: Any, fallback: str = _NOT_RECORDED) -> str:
    if value is None:
        return fallback
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def render_pack_report(report: dict[str, Any]) -> str:
    """Render the report document as plain, sectioned text for a person."""

    lines: list[str] = []

    def section(title: str) -> None:
        if lines:
            lines.append("")
        lines.append(title)
        lines.append("-" * len(title))

    lines.append("PACK PROVENANCE REPORT")
    lines.append("=" * len("PACK PROVENANCE REPORT"))
    lines.append(f"Pack root: {report['pack_root']}")

    for error in report["report_errors"]:
        lines.append(f"PROBLEM READING PACK: {error}")

    identity = report["identity"]
    section("WHAT THIS PACK IS")
    lines.append(f"  Name:              {_value_or(identity['pack_title'])}")
    pack_id = _value_or(identity["pack_id"])
    version = identity["pack_version"]
    lines.append(
        f"  Pack id:           {pack_id}"
        + (f" (version {version})" if version else "")
    )
    game = identity["source_game_description"] or identity["source_game"]
    lines.append(f"  Source game:       {_value_or(game)}")
    profile = _value_or(identity["profile"])
    profile_digest = identity["profile_sha256"]
    lines.append(
        f"  Import profile:    {profile}"
        + (f" (digest {_short_hash(profile_digest)})" if profile_digest else "")
    )
    lines.append(f"  Importer version:  {_value_or(identity['importer_version'])}")
    if identity["redistributable"] is False:
        lines.append(
            "  Redistributable:   no - this pack contains converted retail "
            "content and must stay private"
        )
    else:
        lines.append(
            f"  Redistributable:   {_value_or(identity['redistributable'])}"
        )
    if identity["declared_file_count"] is not None:
        size_text = _format_bytes(identity["declared_total_bytes"])
        lines.append(
            f"  Declared contents: {identity['declared_file_count']} files, "
            f"{size_text} (per the manifest's own inventory)"
        )
    else:
        lines.append("  Declared contents: no usable file inventory in the manifest")
    if identity["declared_content_digest_sha256"]:
        lines.append(
            "  Content digest:    "
            f"{_short_hash(identity['declared_content_digest_sha256'])} "
            "(fingerprint of the manifest's recorded file list, not a "
            "re-hash of the pack)"
        )
    lines.append(
        "  Built:             not recorded (see WHAT THIS REPORT CANNOT "
        "TELL YOU)"
    )

    origin = report["origin"]
    section("WHERE IT CAME FROM")
    if not origin["recorded"]:
        lines.append("  " + origin["provenance_source_description"])
    else:
        commit_caveat = ""
        if origin["git_commit"] and origin["pre_provenance_recipe"]:
            commit_caveat = " (recorded, but unverifiable - see below)"
        elif origin["git_commit"]:
            commit_caveat = " (full value via --json)"
        lines.append(
            f"  Importer commit:   {_short_hash(origin['git_commit'])}"
            + commit_caveat
        )
        clean = origin["git_worktree_clean"]
        if clean is True:
            clean_text = "yes - no uncommitted changes when the importer ran"
        elif clean is False:
            clean_text = (
                "NO - the importer source tree had uncommitted changes, so "
                "the commit alone does not fully describe the code that ran"
            )
        else:
            clean_text = _NOT_RECORDED
        lines.append(f"  Source tree clean: {clean_text}")
        lines.append("  How the commit is known:")
        for wrapped in _wrap(origin["provenance_source_description"], 66):
            lines.append("      " + wrapped)
        requirements = origin["requirements_files"]
        lines.append(
            "  Requirements pins hashed: "
            + (", ".join(requirements) if requirements else _NOT_RECORDED)
        )
        if origin["recipe_file_count"] is not None:
            tree = origin["recipe_tree_sha256"]
            lines.append(
                f"  Importer code inventory:  {origin['recipe_file_count']} "
                "files hashed"
                + (f" (tree digest {_short_hash(tree)})" if tree else "")
            )

    tools = report["tools"]
    section("WHAT PRODUCED IT (TOOLS)")
    if not tools:
        lines.append("  No tool attestations are recorded in the manifest.")
    for tool in tools:
        recorded = tool["recorded"] or {}
        version = recorded.get("version")
        hash_bits: list[str] = []
        for key in sorted(recorded):
            value = recorded[key]
            if key.endswith("sha256") and isinstance(value, str):
                label = key[: -len("sha256")].rstrip("_") or "exe"
                hash_bits.append(f"{label} {_short_hash(value)}")
            elif key.endswith("commit") and isinstance(value, str):
                hash_bits.append(f"{key} {_short_hash(value)}")
        detail = ", ".join(hash_bits) if hash_bits else "no hashes recorded"
        status = tool["verification"]["status"]
        status_text = {
            "verified": "verified against the pinned tool",
            "failed-pin-check": "DOES NOT MATCH the pinned tool",
            "missing-attestation": "MISSING - required but not recorded",
            "recorded-not-pinned": "recorded (not pin-checked for this profile)",
            "not-checked": "recorded (not pin-checked for non-retail packs)",
        }.get(status, status)
        name_field = f"  {tool['name']:<22}"
        version_field = f"{version if isinstance(version, str) else '-':<10}"
        lines.append(f"{name_field}{version_field} {status_text}")
        lines.append(f"{'':<24}{detail}")

    archives = report["source_archives"]
    section("WHAT WENT INTO IT (RETAIL SOURCES)")
    if archives["count"] == 0:
        lines.append("  No source archives are attested in the manifest.")
    else:
        total = _format_bytes(archives["total_bytes"])
        lines.append(
            f"  {archives['count']} retail archives attested, {total} total"
        )
        for archive in archives["archives"]:
            path = _value_or(archive["relative_path"], "(unnamed archive)")
            size = _format_bytes(archive["size"])
            digest = _short_hash(archive["sha256"])
            if archive["matches_reference"] is True:
                verdict = "matches the known retail archive"
            elif archive["matches_reference"] is False:
                verdict = "DOES NOT match the known retail archive"
            else:
                verdict = "no reference fingerprint recorded"
            lines.append(f"    {path}  {size}  sha256 {digest}  {verdict}")

    contents = report["contents"]
    section("WHAT IS IN IT (CONVERTED CONTENT)")
    if contents["conversion_entry_count"] == 0:
        lines.append("  The manifest records no retail-source conversions.")
    else:
        sources = _plural(contents["conversion_entry_count"], "retail source")
        outputs = _plural(
            contents["declared_output_file_count"], "declared output file"
        )
        lines.append(f"  {sources} converted into {outputs}")
        for bucket in contents["by_kind"]:
            converters = ", ".join(bucket["converters"]) or "unknown converter"
            lines.append(
                f"    {bucket['kind']:<12} {bucket['source_count']} sources "
                f"-> {bucket['output_count']} outputs, "
                f"{_format_bytes(bucket['output_bytes'])} (via {converters})"
            )
    if contents["importer_written_file_count"]:
        written = _plural(contents["importer_written_file_count"], "file")
        verb = "is" if contents["importer_written_file_count"] == 1 else "are"
        lines.append(
            f"  {written} {verb} importer-written data (pack metadata, "
            "runtime data), not converted retail sources."
        )
    if contents["incomplete"]:
        lines.append(
            f"  INCOMPLETE: {len(contents['incomplete'])} resources failed "
            "during the build:"
        )
        for item in contents["incomplete"]:
            resource = _value_or(item["resource"], "(unnamed resource)")
            reason = _value_or(item["reason"], "no reason recorded")
            lines.append(f"    {resource}: {reason}")
    elif contents["conversion_entry_count"]:
        lines.append("  Nothing was reported incomplete during the build.")

    health = report["health"]
    section("HEALTH (AUDIT VERDICT)")
    if health["valid"]:
        lines.append(
            "  VALID - every file matches the hashes recorded when the pack "
            "was built, and the provenance chain is complete."
        )
    else:
        count = len(health["errors_explained"])
        lines.append(f"  INVALID - the audit found {_plural(count, 'problem')}:")
        # Several audit errors can share one plain-language cause (the
        # pre-provenance recipe raises two); say it once, not per error.
        seen_explanations: set[str] = set()
        for item in health["errors_explained"]:
            explanation = item["explanation"] or item["error"]
            if explanation in seen_explanations:
                continue
            seen_explanations.add(explanation)
            for index, wrapped in enumerate(_wrap(explanation, 68)):
                prefix = "  - " if index == 0 else "    "
                lines.append(prefix + wrapped)

    section("WHAT THIS REPORT CANNOT TELL YOU")
    for item in report["not_known"]:
        for index, wrapped in enumerate(_wrap(item, 68)):
            prefix = "  - " if index == 0 else "    "
            lines.append(prefix + wrapped)

    lines.append("")
    lines.append(
        "Full hashes and machine-readable detail: "
        "openbfme-import --json describe-pack <pack>"
    )
    return "\n".join(lines)


def _plural(count: int, noun: str) -> str:
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def _wrap(text: str, width: int) -> list[str]:
    words = text.split()
    if not words:
        return [""]
    lines: list[str] = []
    current = words[0]
    for word in words[1:]:
        if len(current) + 1 + len(word) <= width:
            current += " " + word
        else:
            lines.append(current)
            current = word
    lines.append(current)
    return lines
