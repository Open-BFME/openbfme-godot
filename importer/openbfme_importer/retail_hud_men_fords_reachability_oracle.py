"""Fail-closed BFME2 1.06 HUD reachability oracle for Men/Fords.

The oracle intentionally does not assign names to unidentified native flags.
It pins the retail executable and authored APT bytecode, then reports only the
control-flow facts those bytes prove for the declared Men-vs-Men Fords slice.
No retail payload is emitted.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA = "openbfme.private-hud-men-fords-reachability"
SCHEMA_VERSION = 0
GAME_DAT_SHA256 = "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
TEXT_VA_FILE_DELTA = 0x400A00
FRAME_CONTRACT_FILE_SHA256 = (
    "36d1a849cb2c24436ce7342d940fdcdeef7f11ebe1fbe216721669bbb5b307ed"
)
MAP_FILE_SHA256 = "b9932e95e0949a7faecea3e31500b356e4f7f695e40e8b9f6c37202439c4b11b"
SETUP_FILE_SHA256 = "b6ab4ed3493e3e3e4b286b0a4b7e903d1bf508a09f2bc4393d112c77ea3d0df6"
SIDE_APT_SHA256 = "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"
SIM_SHA256 = "a63afe6ca3b570a7d4227c4014b2810b5b7c28826f322eee5ed1c6af9156b71a"
SLICE_SHA256 = "0663046e040e6283bb327de352931b2b7b9d5df02abcc6c68474ca0ab5444e4e"

RETAIL_INI_SOURCES: dict[str, str] = {
    "data/ini/commandset.ini": "3d57ff841b93428ce2118d4bff1871684003bb9eacd8d48865f03ce23e4c5300",
    "data/ini/commandbutton.ini": "bd1af6bedd22acd39bd7571011ac153bd6c5e93543e5f487866e81652c9899c0",
    "data/ini/object/goodfaction/hordes/men/menhordes.ini": (
        "5f73cdc4627d9a745fdfcf79be2d3d8379e3e9180595e98b0df62363645516b9"
    ),
    "data/ini/object/goodfaction/structures/men/fortress.ini": (
        "6d8030714f46bc147fe55adb9a3f101aabe5a773e6b2fa50c479783b7bdb18a0"
    ),
    "data/ini/object/goodfaction/structures/men/farm.ini": (
        "b3a243f0eb887d4127f9596e90a7b2a41e4f482cafe5ec2e538a4fd99d1c941c"
    ),
    "data/ini/object/goodfaction/structures/men/barracks.ini": (
        "e91c4d73a80f51e77c9b6fdce063899fc2195a6abd94eff72f9d6f94531158a7"
    ),
    "data/ini/object/goodfaction/structures/men/archerrange.ini": (
        "fc0caf596dfd74dcff21bbf645b24f92027d33e91c3185c577f78dcacddd99d6"
    ),
    "data/ini/object/goodfaction/structures/men/stable.ini": (
        "d9a04b56739d02fb51545a1bcaac9e8b615bfa0650ef8926cbb1f9c7665ff506"
    ),
}

ROSTER_COMMAND_SETS: tuple[dict[str, str], ...] = (
    {
        "selectionKind": "battalion",
        "selectorField": "unit_type",
        "selectorValue": "bfme2.object.gondor-fighter-horde",
        "retailObject": "GondorFighterHorde",
        "commandSet": "GondorFighterHordeCommandSet",
        "objectSource": "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    },
    {
        "selectionKind": "battalion",
        "selectorField": "unit_type",
        "selectorValue": "bfme2.object.gondor-tower-guard",
        "retailObject": "GondorTowerShieldGuardHorde",
        "commandSet": "GondorTowerShieldGuardCommandSet",
        "objectSource": "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    },
    {
        "selectionKind": "battalion",
        "selectorField": "unit_type",
        "selectorValue": "bfme2.object.gondor-archer",
        "retailObject": "GondorArcherHorde",
        "commandSet": "GondorArcherHordeCommandSet",
        "objectSource": "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    },
    {
        "selectionKind": "battalion",
        "selectorField": "unit_type",
        "selectorValue": "bfme2.object.gondor-knight",
        "retailObject": "GondorKnightHorde",
        "commandSet": "GondorKnightHordeCommandSet",
        "objectSource": "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    },
    {
        "selectionKind": "structure",
        "selectorField": "structure_kind",
        "selectorValue": "fortress",
        "retailObject": "MenFortressCitadel",
        "commandSet": "MenFortressCommandSet",
        "objectSource": "data/ini/object/goodfaction/structures/men/fortress.ini",
    },
    {
        "selectionKind": "structure",
        "selectorField": "structure_kind",
        "selectorValue": "farm",
        "retailObject": "GondorFarm",
        "commandSet": "SellableCommandSet",
        "objectSource": "data/ini/object/goodfaction/structures/men/farm.ini",
    },
    {
        "selectionKind": "structure",
        "selectorField": "structure_kind",
        "selectorValue": "barracks",
        "retailObject": "GondorBarracks",
        "commandSet": "GondorBarracksCommandSet",
        "objectSource": "data/ini/object/goodfaction/structures/men/barracks.ini",
    },
    {
        "selectionKind": "structure",
        "selectorField": "structure_kind",
        "selectorValue": "archery_range",
        "retailObject": "GondorArcherRange",
        "commandSet": "GondorArcheryCommandSet",
        "objectSource": "data/ini/object/goodfaction/structures/men/archerrange.ini",
    },
    {
        "selectionKind": "structure",
        "selectorField": "structure_kind",
        "selectorValue": "stable",
        "retailObject": "GondorStable",
        "commandSet": "GondorStablesCommandSet",
        "objectSource": "data/ini/object/goodfaction/structures/men/stable.ini",
    },
)


NATIVE_RANGES: tuple[dict[str, Any], ...] = (
    {
        "id": "palantir-raw-dual-flag-predicate",
        "startVa": 0x44253A,
        "endVa": 0x44254E,
        "sha256": "b0f8a53aadc0ad21dc1fbf366ccb6c9bbb656aa8a639c53e449c5a8739a0d99c",
    },
    {
        "id": "palantir-raw-mode-predicate",
        "startVa": 0x442219,
        "endVa": 0x442235,
        "sha256": "492949d529b0a09750f2f58b82a391de314cbc119e223175f910fe088e4ade59",
    },
    {
        "id": "palantir-state-chooser",
        "startVa": 0x6D2E57,
        "endVa": 0x6D2EBC,
        "sha256": "208b038cd8c1c4d02ad9a78c0f282ef1ab318327ae5d8d25082d1a0ee45ffd3d",
    },
    {
        "id": "palantir-chooser-callsite",
        "startVa": 0x6D666A,
        "endVa": 0x6D66A7,
        "sha256": "1be99a5aee6ceeed0766a6ab6509f6822d8e6ff5ee097e4a0d2b697f95c95ea4",
    },
    {
        "id": "selected-object-fanout",
        "startVa": 0x6D363E,
        "endVa": 0x6D3680,
        "sha256": "f4487cf26fd2ec3c3e332c7f3ebca1014d60f9d443768919511b4fff08d90cb3",
    },
    {
        "id": "selected-id-object-lookup",
        "startVa": 0x449DC5,
        "endVa": 0x449DEA,
        "sha256": "ef513759e47c5560c5df8531db25f244cef8b95d1d8d5cdb495f2ffd798b694b",
    },
    {
        "id": "selected-object-owner-lookup",
        "startVa": 0x68AFA9,
        "endVa": 0x68AFBB,
        "sha256": "f11813149fd35ca5b296e6c9d35e9efa61e5f0e9c7c8882296b7e0558c71a80f",
    },
    {
        "id": "command-slot-row-resolve",
        "startVa": 0x727D56,
        "endVa": 0x727D6E,
        "sha256": "8737bd0d7ec61e0b342db32c8dd3c77c9592408590a9339150109c80d2af2916",
    },
    {
        "id": "palantir-set-state-bridge",
        "startVa": 0x7FEA18,
        "endVa": 0x7FEA42,
        "sha256": "927d7f5054d90352026f56bbfc7d952a771d08196826f67568ea729e692ff3fc",
    },
    {
        "id": "side-command-fade-in-complete",
        "startVa": 0x928240,
        "endVa": 0x928250,
        "sha256": "a101c33ff15fc8df5c5a54aeb6aa7a86d15f031d4e595aed92529b4adafc80fb",
    },
    {
        "id": "side-command-fade-out-complete",
        "startVa": 0x928250,
        "endVa": 0x928264,
        "sha256": "ad8145e48a96b07e2e1c64e8d68c5fd7b9813472a5c2e5fbc952c1f48e78e5d6",
    },
    {
        "id": "side-command-fade-in-dispatch",
        "startVa": 0x928349,
        "endVa": 0x928389,
        "sha256": "35c7de3900fbcb23bc067a5c3ae856a65af030c80facd4d3c1aa9ec8ced676e8",
    },
    {
        "id": "side-command-loaded",
        "startVa": 0x9283C9,
        "endVa": 0x9283E3,
        "sha256": "108e9be5ad44b786e35d753c10999608a5de8aa7fa6a9de3a1b84268fb65253e",
    },
    {
        "id": "side-command-selected-id-extractor",
        "startVa": 0x92854C,
        "endVa": 0x928566,
        "sha256": "6ebdb4740ebd5d5522dd6900e83b1548f41c4e7f9bf6a8bd2ddb6ff30a1733b5",
    },
    {
        "id": "side-command-eligibility-and-button-build",
        "startVa": 0x9285EF,
        "endVa": 0x928738,
        "sha256": "841ee50a38086d2a0818cffe65fa4baa7600b47d25a76986a6cde1c82c118f23",
    },
    {
        "id": "side-command-update",
        "startVa": 0x9287EA,
        "endVa": 0x9288B8,
        "sha256": "2888ca0c896ab9ee575fcdffbf71426fc36ea6e969e59d83d5daaa674c820ea8",
    },
    {
        "id": "side-command-callback-registration",
        "startVa": 0x9288C4,
        "endVa": 0x928AC3,
        "sha256": "e150d50d97600b4fe9e77cb9d2bff5a74130c6160b3e32196b81dc50bb816c05",
    },
)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private_file(path: Path | str, label: str) -> Path:
    resolved = Path(path).resolve()
    if ".private" not in {part.casefold() for part in resolved.parts}:
        raise ValueError(f"{label} must stay under .private")
    if not resolved.is_file():
        raise ValueError(f"{label} is missing")
    return resolved


def _read_exact_json(path: Path, expected_sha: str, label: str) -> Mapping[str, Any]:
    payload = path.read_bytes()
    if _sha(payload) != expected_sha:
        raise ValueError(f"unexpected {label} SHA-256")
    value = json.loads(payload)
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be a JSON object")
    return value


def _validate_native(game_dat: bytes) -> list[dict[str, Any]]:
    if _sha(game_dat) != GAME_DAT_SHA256:
        raise ValueError("game.dat is not the pinned BFME2 1.06 executable")
    result: list[dict[str, Any]] = []
    for expected in NATIVE_RANGES:
        start = int(expected["startVa"])
        end = int(expected["endVa"])
        payload = game_dat[start - TEXT_VA_FILE_DELTA : end - TEXT_VA_FILE_DELTA]
        if len(payload) != end - start or _sha(payload) != expected["sha256"]:
            raise ValueError(f"native range changed: {expected['id']}")
        result.append(
            {
                **expected,
                "byteLength": len(payload),
                "startVaHex": f"0x{start:08x}",
                "endVaHex": f"0x{end:08x}",
            }
        )
    return result


def _validate_frame_contract(frame: Mapping[str, Any]) -> None:
    if frame.get("aggregateSha256") != (
        "ce06070cdf9355245421efa7a449257a71c17aca6d83d0dd0d47dd5a3cc45e33"
    ):
        raise ValueError("unexpected frame-selection aggregate")
    states = frame.get("palantir", {}).get("palantirFrame", {}).get("states", [])
    actual = {
        row.get("state"): (row.get("frameIndex"), row.get("importSymbol"))
        for row in states
    }
    expected = {
        "_good": (19, "PalantirFrame_GoodDouble"),
        "_goodSingle": (9, "PalantirFrame_GoodSingle"),
        "_evil": (39, "PalantirFrame_EvilDouble"),
        "_evilSingle": (29, "PalantirFrame_EvilSingle"),
    }
    if actual != expected:
        raise ValueError("Palantir APT state table changed")
    labels = frame.get("inGameSideCommandBar", {}).get("labels")
    if labels != {"_fadeIn": 11, "_fadeOut": 31, "_hide": 1}:
        raise ValueError("side-command APT labels changed")


def _validate_map(map_data: Mapping[str, Any], setup: Mapping[str, Any]) -> None:
    if (
        map_data.get("schema") != "openbfme.map"
        or map_data.get("id") != "bfme2.map.fords-of-isen-ii"
        or map_data.get("displayName") != "Fords of Isen II"
        or map_data.get("source", {}).get("sha256")
        != "fa3b460c23f72821c7e19127bb90f7d97c27a7a2c485a44936b9c291173c0800"
    ):
        raise ValueError("unexpected Fords map contract")
    slots = setup.get("lobbySlots", [])
    if (
        setup.get("schema") != "openbfme.sage-multiplayer-setup"
        or setup.get("declaredPlayerCount") != 2
        or setup.get("nonemptyScriptListCount") != 0
        or not isinstance(slots, list)
        or any(row.get("sideRestrictions") != [] for row in slots)
    ):
        raise ValueError("unexpected Fords setup contract")


def _validate_side_apt(apt: bytes) -> None:
    if _sha(apt) != SIDE_APT_SHA256:
        raise ValueError("unexpected InGameSideCommandBar.apt SHA-256")
    ranges = (
        ("root-program", 8136, 9346, "5f819773cdf0b9105bd0f0c0978da26d2d8f8ecebf63668116c1a00041e24fb5"),
        ("FadeIn-body", 8836, 9009, "e360a3640690bda116ca9437e11bb4ece5f5afbe5f2f46f463facc5540a8939a"),
        (
            "FadeIn-completion-program",
            9404,
            10086,
            "47b0231d9b4f7952f3dba37fd2ba6f3f07914edb3a546ba63a0f873e51ef1a9c",
        ),
        (
            "FadeIn-settled-stop",
            10088,
            10090,
            "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd",
        ),
    )
    for label, start, end, expected_sha in ranges:
        if _sha(apt[start:end]) != expected_sha:
            raise ValueError(f"side-command APT {label} changed")


def _ini_block(text: str, kind: str, name: str) -> str:
    match = re.search(
        rf"(?ims)^\s*{re.escape(kind)}\s+{re.escape(name)}\s*$"
        rf"(.*?)^\s*End\s*$",
        text,
    )
    if match is None:
        raise ValueError(f"retail INI block is missing: {kind} {name}")
    return match.group(1)


def _command_set_rows(text: str, name: str) -> list[dict[str, Any]]:
    block = _ini_block(text, "CommandSet", name)
    rows: list[dict[str, Any]] = []
    for raw_line in block.splitlines():
        line = raw_line.split(";", 1)[0].strip()
        match = re.match(r"(\d+)\s*=\s*([^\s]+)", line)
        if match is not None:
            rows.append({"slot": int(match.group(1)), "commandButton": match.group(2)})
    if not rows:
        raise ValueError(f"retail command set is empty: {name}")
    return rows


def _command_button_fields(text: str, name: str) -> dict[str, str]:
    block = _ini_block(text, "CommandButton", name)
    fields: dict[str, str] = {}
    for raw_line in block.splitlines():
        line = raw_line.split(";", 1)[0].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        fields[key.strip().casefold()] = value.strip()
    return fields


def _object_declaration_body(text: str, name: str) -> str:
    declaration = re.search(
        rf"(?im)^\s*(?:Object|ChildObject)\s+{re.escape(name)}(?:\s|$)", text
    )
    if declaration is None:
        raise ValueError(f"retail roster object is missing: {name}")
    next_declaration = re.search(
        r"(?im)^\s*(?:Object|ChildObject)\s+[^\s;]+", text[declaration.end() :]
    )
    end = (
        declaration.end() + next_declaration.start()
        if next_declaration is not None
        else len(text)
    )
    return text[declaration.end() : end]


def _validate_roster_sources(asset_root: Path) -> tuple[list[dict[str, Any]], dict[str, str]]:
    sources: dict[str, bytes] = {}
    for relative, expected_sha in RETAIL_INI_SOURCES.items():
        path = asset_root.joinpath(*relative.split("/"))
        if not path.is_file():
            raise ValueError(f"retail INI source is missing: {relative}")
        payload = path.read_bytes()
        if _sha(payload) != expected_sha:
            raise ValueError(f"retail INI source changed: {relative}")
        sources[relative] = payload

    command_sets = sources["data/ini/commandset.ini"].decode(
        "utf-8", errors="replace"
    )
    command_buttons = sources["data/ini/commandbutton.ini"].decode(
        "utf-8", errors="replace"
    )
    result: list[dict[str, Any]] = []
    for spec in ROSTER_COMMAND_SETS:
        object_name = spec["retailObject"]
        object_source = spec["objectSource"]
        object_text = sources[object_source].decode("utf-8", errors="replace")
        object_body = _object_declaration_body(object_text, object_name)
        declared_command_sets = re.findall(
            r"(?im)^\s*CommandSet\s*=\s*([^\s;]+)", object_body
        )
        if spec["commandSet"] not in declared_command_sets:
            raise ValueError(
                f"retail roster object {object_name} does not declare "
                f"{spec['commandSet']}"
            )
        rows = _command_set_rows(command_sets, spec["commandSet"])
        eligible: list[str] = []
        multi_select: list[str] = []
        for row in rows:
            name = str(row["commandButton"])
            fields = _command_button_fields(command_buttons, name)
            if fields.get("inpalantir", "").casefold() != "yes":
                continue
            eligible.append(name)
            options = fields.get("options", "").upper().split()
            if "OK_FOR_MULTI_SELECT" in options:
                multi_select.append(name)
        if not eligible:
            raise ValueError(f"retail command set has no InPalantir row: {spec['commandSet']}")
        result.append(
            {
                **spec,
                "commandRows": rows,
                "commandRowCount": len(rows),
                "inPalantirYesCommands": eligible,
                "inPalantirYesCount": len(eligible),
                "multiSelectCommands": multi_select,
            }
        )

    battalions = [row for row in result if row["selectionKind"] == "battalion"]
    common_multi = set(battalions[0]["multiSelectCommands"])
    for row in battalions[1:]:
        common_multi.intersection_update(row["multiSelectCommands"])
    expected_common = {"Command_ToggleStance", "Command_AttackMove", "Command_Stop"}
    if not expected_common.issubset(common_multi):
        raise ValueError("Men battalion multi-selection command intersection changed")
    return result, {
        relative: expected_sha for relative, expected_sha in RETAIL_INI_SOURCES.items()
    }


def _validate_sim_selection_sources(repo_root: Path) -> dict[str, Any]:
    sim_path = repo_root / "game" / "src" / "retail_slice" / "retail_slice_sim.gd"
    slice_path = (
        repo_root / "game" / "src" / "retail_slice" / "retail_vertical_slice.gd"
    )
    sim = sim_path.read_bytes()
    vertical = slice_path.read_bytes()
    if _sha(sim) != SIM_SHA256 or _sha(vertical) != SLICE_SHA256:
        raise ValueError("existing Men/Fords selection source changed")
    sim_text = sim.decode("utf-8")
    slice_text = vertical.decode("utf-8")
    required_sim = (
        "var selected_ids: Array[int] = []",
        '"team": team,',
        '"health": maximum_health,',
        '"unit_type": unit_type,',
        '"structure_kind": kind,',
        "selected_ids.sort()",
        "winner == -1",
    )
    required_slice = (
        "var selected_structure_id := 0",
        "simulation.clear_selection()",
        "selected_structure_id = structure_id",
        "selected_structure_id = 0",
    )
    if any(marker not in sim_text for marker in required_sim) or any(
        marker not in slice_text for marker in required_slice
    ):
        raise ValueError("existing Men/Fords selection fields changed")
    return {
        "retailSliceSimSha256": SIM_SHA256,
        "retailVerticalSliceSha256": SLICE_SHA256,
        "observedFields": {
            "battalionSelection": "selected_ids sorted ascending",
            "battalionRow": ["team", "health", "unit_type"],
            "structureSelection": "selected_structure_id",
            "structureRow": ["team", "health", "structure_kind", "production"],
            "matchState": "winner",
        },
        "selectionExclusivity": (
            "battalion click clears selected_structure_id; structure click clears selected_ids"
        ),
    }


def build_contract(
    frame_contract_path: Path | str,
    map_path: Path | str,
    setup_path: Path | str,
    game_dat_path: Path | str,
    side_apt_path: Path | str,
) -> dict[str, Any]:
    """Build the deterministic, payload-free Men/Fords reachability contract."""

    frame_path = _private_file(frame_contract_path, "frame contract")
    private_map = _private_file(map_path, "map contract")
    private_setup = _private_file(setup_path, "setup contract")
    private_apt = _private_file(side_apt_path, "side-command APT")
    game_path = Path(game_dat_path).resolve()
    if not game_path.is_file():
        raise ValueError("game.dat is missing")

    frame = _read_exact_json(
        frame_path, FRAME_CONTRACT_FILE_SHA256, "frame-selection contract"
    )
    map_data = _read_exact_json(private_map, MAP_FILE_SHA256, "Fords map contract")
    setup = _read_exact_json(private_setup, SETUP_FILE_SHA256, "Fords setup contract")
    _validate_frame_contract(frame)
    _validate_map(map_data, setup)
    apt = private_apt.read_bytes()
    _validate_side_apt(apt)
    roster, retail_ini_hashes = _validate_roster_sources(private_apt.parent)
    repo_root = Path(__file__).resolve().parents[2]
    sim_selection = _validate_sim_selection_sources(repo_root)
    native = _validate_native(game_path.read_bytes())

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "gameDatSha256": GAME_DAT_SHA256,
            "frameContractSha256": FRAME_CONTRACT_FILE_SHA256,
            "mapContractSha256": MAP_FILE_SHA256,
            "setupContractSha256": SETUP_FILE_SHA256,
            "sideCommandAptSha256": SIDE_APT_SHA256,
            "retailIniSha256": retail_ini_hashes,
            "existingSelectionSources": sim_selection,
            "nativeRanges": native,
        },
        "declaredSlice": {
            "mapId": "bfme2.map.fords-of-isen-ii",
            "mapDisplayName": "Fords of Isen II",
            "declaredPlayerCount": 2,
            "matchKind": "skirmish-human-men-vs-ai-men",
            "networkMultiplayerInScope": False,
            "mapScriptsSelectingHudVariant": 0,
            "lobbySideRestrictions": [],
            "criticalBoundary": (
                "Men-vs-Men and human-vs-AI are slice declarations; the map/setup "
                "records do not bind either declaration to the chooser's raw bytes"
            ),
        },
        "palantir": {
            "aptMethod": "SetPalantirFrameState(state)",
            "nativeCallOrder": [
                "0x006d666b calls chooser 0x006d2e57",
                "0x006d6678 compares result with cached owner+0xe8 state",
                "0x006d667d passes a changed index to bridge 0x007fea18",
                "0x007fea28 indexes the state-string table",
                "0x007fea3c invokes APT SetPalantirFrameState",
            ],
            "stateTable": [
                {"index": 0, "state": "_hide", "chooserReturns": False},
                {"index": 1, "state": "_good", "variant": "good-double"},
                {"index": 2, "state": "_goodSingle", "variant": "good-single"},
                {"index": 3, "state": "_evil", "variant": "evil-double"},
                {"index": 4, "state": "_evilSingle", "variant": "evil-single"},
            ],
            "orderedRoutes": [
                {
                    "priority": 1,
                    "rawCondition": (
                        "[0x00dfef10] exists and predicate 0x0044253a sees both "
                        "object bytes +0xb4 and +0xb5 nonzero"
                    ),
                    "selector": "byte [[0x00e02d6c]+0x2c]",
                    "zeroResult": {"index": 2, "state": "_goodSingle"},
                    "nonzeroResult": {"index": 4, "state": "_evilSingle"},
                    "semanticNameProved": False,
                },
                {
                    "priority": 2,
                    "rawCondition": (
                        "[0x00dfe78c] exists and predicate 0x00442219 returns true "
                        "when object dword +0x110 is not 9, 4, or 7"
                    ),
                    "selector": (
                        "byte at ((0x006a7e14([0x00dfeee8]))+0x34)+0x1bc"
                    ),
                    "zeroResult": {"index": 1, "state": "_good"},
                    "nonzeroResult": {"index": 3, "state": "_evil"},
                    "semanticNameProved": False,
                },
                {
                    "priority": 3,
                    "rawCondition": "fallback",
                    "result": {"index": 1, "state": "_good"},
                },
            ],
            "sliceReachability": {
                "_good": "proved native fallback and raw-route-2 zero result",
                "_goodSingle": "proved conditional native route; not eliminated by slice inputs",
                "_evil": "proved conditional native route; Men-to-zero-byte binding absent",
                "_evilSingle": "proved conditional native route; Men-to-zero-byte binding absent",
            },
            "blockerDecision": "retain",
            "decisionReason": (
                "Deleting nondefault selection would silently force _good. The exact "
                "native chooser can return all four authored variants, while the Fords "
                "setup has no side restriction or mode/alignment binding that proves the "
                "three nondefault branches unreachable for this configured skirmish."
            ),
            "minimumRequiredImplementation": (
                "one typed four-result chooser binding plus a fail-closed trace/assertion "
                "for the two raw selectors; no generic APT dispatcher"
            ),
        },
        "sideCommandBar": {
            "aptTimeline": {
                "labelsZeroBased": {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31},
                "frameNumbersOneBased": {
                    "fadeInStartFrameNum": 12,
                    "fadeInEndFrameNum": 22,
                    "fadeOutStartFrameNum": 32,
                    "fadeOutEndFrameNum": 42,
                },
                "rootProgramRange": [8136, 9346],
                "fadeInFunctionHeaderOffset": 8809,
                "fadeInBodyRange": [8836, 9009],
                "fadeInTarget": (
                    "if currentframe is outside [32,42), this.gotoAndPlay('_fadeIn') "
                    "at one-based frame 12; if currentframe is inside [32,42), "
                    "this.gotoAndPlay(12 + 42 - currentframe), reversing FadeOut"
                ),
                "fadeInTargetExamples": {
                    "currentframe31": 12,
                    "currentframe32": 22,
                    "currentframe37": 17,
                    "currentframe41": 13,
                    "currentframe42": 12,
                },
                "fadeInCompletion": {
                    "triggerFrameZeroBased": 21,
                    "triggerFrameOneBased": 22,
                    "programRange": [9404, 10086],
                    "programSha256": (
                        "47b0231d9b4f7952f3dba37fd2ba6f3f07914edb3a546ba63a0f873e51ef1a9c"
                    ),
                    "action": (
                        "GetURL2('FSCommand:OnAptInGameSideCommandBarFadeInComplete', "
                        "GetFullName(this))"
                    ),
                    "nativeCondition": "callback changes state 2 to state 3 only",
                    "settledStopFrameZeroBased": 30,
                    "settledStopFrameOneBased": 31,
                    "settledStopProgramRange": [10088, 10090],
                },
                "normalInGameLoadGuard": (
                    "root script tests _global.InGame; truthy branches to End, while "
                    "falsey executes goto-label _fadeIn then Play"
                ),
            },
            "nativeStateMachine": {
                "loaded": (
                    "OnAptInGameSideCommandBarLoaded callback 0x009283c9 stores the loaded "
                    "movie root at owner+0x18, then writes state 1"
                ),
                "selectionGate": (
                    "update 0x009287ea requires a nonzero selected id, a resolved selected "
                    "object whose definition byte +0x109 has bit 0x40, the selected owner "
                    "to equal the local player, local-player dword +0x750 to equal zero, "
                    "and 0x009285ef to build at least one accepted command row"
                ),
                "selectedIdPath": [
                    "selection fanout 0x006d363e receives the selected retail object",
                    "0x0092854c reads selected-object dword +0x74 as the selected id",
                    "the id is stored at side-command owner+0x1c",
                    "0x00449dc5 resolves it through [0x00dfe78c]+0xb4",
                    "0x0068afa9 follows selected-object +0x304 then 0x0079d7cf to owner",
                    "owner must equal 0x006a7e14([0x00dfeee8])",
                ],
                "eligibilityLoop": {
                    "entryVa": "0x009285ef",
                    "candidateArray": "[0x00e01cfc] dwords +0xdc through +0x158",
                    "candidateCapacity": 32,
                    "acceptedRowCapacity": 15,
                    "rowResolve": "candidate +0x2c pointer, then resolved pointer +0x14",
                    "rowAcceptTest": "resolved-row byte +0x101 is nonzero",
                    "rowAction": "materialize/update APT button and SetButtonState('_show')",
                    "returnCondition": "accepted-row count > 0",
                },
                "fadeInDispatch": (
                    "0x00928349 invokes method string FadeIn on the loaded movie root, "
                    "then writes state 2"
                ),
                "fadeInComplete": (
                    "callback 0x00928240 changes state 2 to settled-visible state 3"
                ),
                "exactOrder": [
                    "load root -> state 1",
                    "update resolves and validates selected local object",
                    "state not 2/3 -> dispatch root.FadeIn()",
                    "native writes state 2",
                    "APT FadeIn calls root.gotoAndPlay(target)",
                    "OnAptInGameSideCommandBarFadeInComplete -> state 3",
                ],
            },
            "declaredRosterCommandSets": roster,
            "godotTypedInputContract": {
                "inputType": "MenFordsSelectionCommandContext",
                "fields": {
                    "selected_ids": "Array[int] from RetailSliceSim, sorted",
                    "selected_structure_id": "int from RetailVerticalSlice",
                    "entities": "Dictionary rows containing team, health, unit_type",
                    "structures": (
                        "Dictionary rows containing team, health, structure_kind, production"
                    ),
                    "winner": "int from RetailSliceSim",
                    "local_team": "constant 0 for the declared slice",
                },
                "resolver": [
                    "assert selected_structure_id == 0 or selected_ids is empty",
                    "resolve selected_structure_id through structures, otherwise every selected_id through entities",
                    "require every row team == local_team, health > 0, and winner == -1",
                    "map structure_kind or unit_type through declaredRosterCommandSets",
                    "single selection is eligible when its authored command set has an InPalantir=Yes row",
                    "multi-battalion selection is eligible because all four authored sets share InPalantir=Yes and OK_FOR_MULTI_SELECT ToggleStance, AttackMove, and Stop rows",
                    "FadeIn eligibility is exactly resolved eligible-command count > 0",
                ],
                "noSelectionResult": False,
                "allNineRosterSelectionsResult": True,
                "mixedBattalionSelectionResult": True,
                "enemyDeadOrPostMatchSelectionResult": False,
                "genericAptDispatchRequired": False,
            },
            "narrowNativeAliasGate": {
                "code": "side-command-native-row-alias-trace",
                "scope": (
                    "one retail trace for a local living roster selection must confirm "
                    "definition bit ([selected+4]+0x109)&0x40, local-player +0x750==0, "
                    "and accepted row byte +0x101 for an authored InPalantir=Yes command"
                ),
                "whyNarrow": (
                    "selection identity, ownership, roster command sets, nonempty rows, "
                    "FadeIn target math, and completion are already closed"
                ),
                "blocksGodotTypedImplementation": False,
                "blocksClaimOfExactNativeAliasParity": True,
            },
            "normalMatchStartDecision": {
                "unconditionalAtMovieLoad": False,
                "mandatoryWithoutSelection": False,
                "reachableWithEligibleSelection": True,
                "firstTickAutoSelectionProved": False,
                "finding": (
                    "Delete the overbroad requirement that FadeIn must run at movie load or "
                    "unconditionally on the first match tick. Keep a narrow first-selection "
                    "trace if exact initial auto-selection timing is later claimed."
                ),
            },
            "blockerDecision": "delete-broad-research-blocker-after-binding",
            "decisionReason": (
                "The implementation predicate is now typed from existing simulation fields, "
                "all nine declared roster selections resolve a nonempty authored command set, "
                "and FadeIn target/completion behavior is exact. Keep only the one native-alias "
                "trace gate; this oracle task does not edit the runtime binding."
            ),
            "minimumRequiredImplementation": (
                "implement MenFordsSelectionCommandContext and drive the existing authored "
                "APT FadeIn/FadeOut state machine from eligible-command count > 0"
            ),
        },
        "summary": {
            "broadResearchBlockersDeleted": 1,
            "blockersRetained": 1,
            "narrowNativeAliasGatesRetained": 1,
            "overbroadRequirementsDeleted": 1,
            "deletedRequirement": "unconditional side-command FadeIn at match load/start",
            "retainedBlockers": [
                "palantir-nondefault-selection-not-bound",
            ],
            "implementationReadyBlocker": "side-command-bar-fade-in-not-bound",
            "runtimeBindingImplementedByThisOracle": False,
            "retailPayloadEmitted": False,
            "genericDispatchAllowed": False,
        },
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame-contract", required=True, type=Path)
    parser.add_argument("--map", required=True, type=Path)
    parser.add_argument("--setup", required=True, type=Path)
    parser.add_argument("--game-dat", required=True, type=Path)
    parser.add_argument("--side-apt", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = build_contract(
        args.frame_contract, args.map, args.setup, args.game_dat, args.side_apt
    )
    payload = json.dumps(contract, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(payload, end="")
    else:
        output = args.output.resolve()
        if ".private" not in {part.casefold() for part in output.parts}:
            raise ValueError("oracle output must stay under .private")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
