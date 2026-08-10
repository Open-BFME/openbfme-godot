from copy import deepcopy
import hashlib
import json

import pytest

from openbfme_importer.playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from importer.tests.test_playable_unit_compiler import _documents


def _structure_documents() -> dict[str, bytes]:
    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects += """
Object PorterBuilder
  CommandSet = PorterCommandSet
  KindOf = INFANTRY DOZER
End

Object TestKeep
  CommandSet = TestKeepCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = KEEP_BUILDCOST
  BuildTime = 45.0
  VisionRange = 200
  DisplayName = OBJECT:TestKeep
  SelectPortrait = UPTestKeep
  ButtonImage = BITestKeep
  SoundOnDamaged = KeepDamagedSound
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = Keep_SKN
    End
    IdleAnimationState
      Animation = Idle
        AnimationName = Keep_SKL.Keep_IDLA
      End
    End
    ModelConditionState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Model = Keep_CONS
    End
    AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Animation = Build
        AnimationName = Keep_SKL.Keep_CONSA
        AnimationMode = MANUAL
      End
    End
    ModelConditionState = DAMAGED
      Model = Keep_SKN
      ParticleSysBone = FireSmall01 FireBuildingMedium
      EnteringStateFX = FX_BuildingDamaged
    End
    ModelConditionState = REALLYDAMAGED
      Model = Keep_SKN
      EnteringStateFX = FX_BuildingReallyDamaged
    End
    ModelConditionState = RUBBLE
      Model = Keep_RUBBLE
      EnteringStateFX = FX_StructureMediumCollapse
    End
    AnimationState = RUBBLE
      Animation = Die
        AnimationName = Keep_SKL.Keep_LEVERA
        AnimationMode = ONCE
      End
    End
  End
  Draw = W3DFloorDraw ModuleTag_Bib
    ModelName = Keep_BIB
    HideIfModelConditions = AWAITING_CONSTRUCTION PARTIALLY_CONSTRUCTED
  End
  Body = StructureBody ModuleTag_Body
    MaxHealth = KEEP_HEALTH
    MaxHealthDamaged = KEEP_HEALTH_DAMAGED
    MaxHealthReallyDamaged = KEEP_HEALTH_REALLY_DAMAGED
  End
  Behavior = CommandSetUpgrade ModuleTag_Level2
    TriggeredBy = Upgrade_KeepLevel2
    CommandSet = TestKeepCommandSetLevel2
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse
    MinCollapseDelay = 0
    MaxCollapseDelay = 0
    CollapseDamping = 0.5
    MaxShudder = 0.6
    MinBurstDelay = 250
    MaxBurstDelay = 800
    BigBurstFrequency = 4
    FXList = INITIAL FX_StructureMediumCollapse
    FXList = ALMOST_FINAL FX_StructureAlmostCollapse
    DestroyObjectWhenDone = Yes
    CollapseHeight = 155
  End
End

Object TestCitadel
  CommandSet = TestKeepCommandSet
  KindOf = STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 500
  End
End

Object HollowKeep
  KindOf = STRUCTURE
End
"""
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet PorterCommandSet
  1 = Command_ConstructTestKeep
End
CommandSet TestKeepCommandSet
  1 = Command_BuildInfantry
End
CommandSet TestKeepCommandSetLevel2
  1 = Command_BuildRanged
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructTestKeep
  Command = PORTER_CONSTRUCT
  Object = TestKeep
  NeededUpgrade = Upgrade_StoneWork
  ButtonImage = BITestKeep
End
"""
    ).encode("utf-8")
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define KEEP_HEALTH 3000\n"
        + b"#define KEEP_HEALTH_DAMAGED 2000\n"
        + b"#define KEEP_HEALTH_REALLY_DAMAGED 1000\n"
    )
    return documents


def _resign_structure_descriptor(descriptor: dict[str, object]) -> None:
    unsigned = dict(descriptor)
    unsigned.pop("descriptorSha256", None)
    descriptor["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def test_constructed_structure_compiles_deterministically() -> None:
    documents = _structure_documents()

    first = compile_playable_structure_descriptor("TestKeep", documents)
    second = compile_playable_structure_descriptor(
        "TestKeep", dict(reversed(documents.items()))
    )

    validate_playable_structure_descriptor(first)
    assert first == second
    assert first["category"] == "structure"
    assert first["production"]["evidence"] == "authored-construct-command"
    route = first["production"]["routes"][0]
    assert route["builderObjectId"] == "PorterBuilder"
    assert route["commandKind"] == "porter_construct"
    assert route["prerequisites"] == ["Upgrade_StoneWork"]
    assert route["buttonImageId"] == "BITestKeep"
    health = first["gameplay"]["health"]["primary"]
    assert health["module"] == "StructureBody"
    assert health["maxHealth"] == {"authored": "KEEP_HEALTH", "value": 3000}
    assert health["maxHealthDamaged"]["value"] == 2000
    assert health["maxHealthReallyDamaged"]["value"] == 1000
    trained = {row["id"]: row for row in first["gameplay"]["trainedCommandSets"]}
    assert trained["TestKeepCommandSet"]["kind"] == "direct"
    assert trained["TestKeepCommandSetLevel2"]["kind"] == "upgraded"
    assert trained["TestKeepCommandSetLevel2"]["triggeredBy"] == [
        "Upgrade_KeepLevel2"
    ]
    assert first["presentation"]["ui"]["DisplayName"]
    assert len(first["descriptorSha256"]) == 64


def test_queue_production_exit_update_compiles_effective_deferred_contract() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents["data/ini/gamedata.ini"] += (
        b"#define TEST_QUEUE_EXIT_DELAY 50\n"
    )
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:1 Y:2 Z:3
    UnitCreatePoint = X:14.8616 Y:-0.1480 Z:0.0
    NaturalRallyPoint = X:56 Y:-0.148 Z:0
    ExitDelay = 25
    ExitDelay = TEST_QUEUE_EXIT_DELAY
    AllowAirborneCreation = Yes
    InitialBurst = 2
    PlacementViewAngle = 45
    PlacementViewAngle = 90
    UseReturnToFormation = No
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["gameplay"]["productionExitUpdates"] == [
        {
            "module": "QueueProductionExitUpdate",
            "unitCreatePoint": {
                "authored": "X:14.8616 Y:-0.1480 Z:0.0",
                "value": {"x": 14.8616, "y": -0.148, "z": 0.0},
                "sourceIni": path,
                "line": descriptor["gameplay"]["productionExitUpdates"][0][
                    "unitCreatePoint"
                ]["line"],
            },
            "naturalRallyPoint": {
                "authored": "X:56 Y:-0.148 Z:0",
                "value": {"x": 56.0, "y": -0.148, "z": 0.0},
                "sourceIni": path,
                "line": descriptor["gameplay"]["productionExitUpdates"][0][
                    "naturalRallyPoint"
                ]["line"],
            },
            "exitDelay": {
                "authored": "TEST_QUEUE_EXIT_DELAY",
                "value": 50,
                "unit": "milliseconds",
                "sourceIni": path,
                "line": descriptor["gameplay"]["productionExitUpdates"][0][
                    "exitDelay"
                ]["line"],
                "resolvedDefine": {
                    "name": "TEST_QUEUE_EXIT_DELAY",
                    "value": 50,
                },
            },
            "allowAirborneCreation": {
                "authored": "Yes",
                "value": True,
                "sourceIni": path,
                "line": descriptor["gameplay"]["productionExitUpdates"][0][
                    "allowAirborneCreation"
                ]["line"],
            },
            "initialBurst": {
                "authored": "2",
                "value": 2,
                "sourceIni": path,
                "line": descriptor["gameplay"]["productionExitUpdates"][0][
                    "initialBurst"
                ]["line"],
            },
            "deferredFields": [
                {
                    "name": "PlacementViewAngle",
                    "authored": "90",
                    "sourceIni": path,
                    "line": descriptor["gameplay"]["productionExitUpdates"][0][
                        "deferredFields"
                    ][0]["line"],
                    "reason": "bfme-field-without-local-runtime-oracle",
                },
                {
                    "name": "UseReturnToFormation",
                    "authored": "No",
                    "sourceIni": path,
                    "line": descriptor["gameplay"]["productionExitUpdates"][0][
                        "deferredFields"
                    ][1]["line"],
                    "reason": "bfme-field-without-local-runtime-oracle",
                },
            ],
            "runtimeStatus": "deferred",
            "sourceIni": path,
            "line": descriptor["gameplay"]["productionExitUpdates"][0]["line"],
        }
    ]


def test_queue_production_exit_update_defaults_and_unknown_field_refusal() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:0 Y:0 Z:0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    row = descriptor["gameplay"]["productionExitUpdates"][0]
    assert row["exitDelay"] == {
        "authored": "0",
        "value": 0,
        "defaulted": True,
        "unit": "milliseconds",
    }
    assert row["allowAirborneCreation"] == {
        "authored": "No",
        "value": False,
        "defaulted": True,
    }
    assert row["initialBurst"] == {
        "authored": "0",
        "value": 0,
        "defaulted": True,
    }

    documents[path] = documents[path].replace(
        b"    NaturalRallyPoint = X:0 Y:0 Z:0",
        b"    NaturalRallyPoint = X:0 Y:0 Z:0\n    InventedExitRule = Yes",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError, match="unsupported fields"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_queue_production_exit_update_descriptor_rejects_runtime_promotion() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    corrupted = compile_playable_structure_descriptor("TestKeep", documents)
    corrupted["gameplay"]["productionExitUpdates"][0][
        "runtimeStatus"
    ] = "simulation-backed"
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(
        PlayableStructureCompilerError,
        match="production exit update row",
    ):
        validate_playable_structure_descriptor(corrupted)


def test_queue_production_exit_update_rejects_coord_with_ignored_suffix() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0 UnmappedTail
    NaturalRallyPoint = X:10 Y:0 Z:0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match="not an exact X/Y/Z Coord3D",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_queue_production_exit_update_rejects_unsigned_int_overflow() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
    ExitDelay = 4294967296
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match=r"UnsignedInt in range 0\.\.4294967295",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


@pytest.mark.parametrize(
    ("field_name", "authored"),
    (("ExitDelay", "50.0"), ("InitialBurst", "2.0")),
)
def test_queue_production_exit_update_requires_exact_unsigned_decimal(
    field_name: str, authored: str
) -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        f"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
    {field_name} = {authored}
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""".encode(),
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match="exact unsigned decimal or resolved GameData constant",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_queue_production_exit_update_rejects_non_ini_boolean_alias() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
    AllowAirborneCreation = True
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match="must be Yes or No",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_queue_production_exit_update_rejects_out_of_order_coord_axes() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = Y:0 X:10 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match="not an exact X/Y/Z Coord3D",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_queue_production_exit_update_resigned_descriptor_rejects_drift() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:10 Y:20 Z:30
    NaturalRallyPoint = X:40 Y:50 Z:60
    ExitDelay = 50
    InitialBurst = 2
    AllowAirborneCreation = Yes
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    mixed_case = deepcopy(descriptor)
    mixed_case["gameplay"]["productionExitUpdates"][0][
        "allowAirborneCreation"
    ]["authored"] = "yEs"
    unsigned_mixed_case = dict(mixed_case)
    unsigned_mixed_case.pop("descriptorSha256")
    mixed_case["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned_mixed_case,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()
    validate_playable_structure_descriptor(mixed_case)

    for mutate in (
        "coord-order",
        "coord-value",
        "exit-authored",
        "exit-value",
        "burst-authored",
        "burst-value",
        "bool-vocabulary",
        "bool-value",
    ):
        corrupted = deepcopy(descriptor)
        row = corrupted["gameplay"]["productionExitUpdates"][0]
        if mutate == "coord-order":
            row["unitCreatePoint"]["authored"] = "Y:20 X:10 Z:30"
        elif mutate == "coord-value":
            row["unitCreatePoint"]["value"]["x"] = 11.0
        elif mutate == "exit-authored":
            row["exitDelay"]["authored"] = "1"
        elif mutate == "exit-value":
            row["exitDelay"]["value"] = 1
        elif mutate == "burst-authored":
            row["initialBurst"]["authored"] = "1"
        elif mutate == "burst-value":
            row["initialBurst"]["value"] = 1
        elif mutate == "bool-vocabulary":
            row["allowAirborneCreation"]["authored"] = "True"
        else:
            row["allowAirborneCreation"]["value"] = False
        unsigned = dict(corrupted)
        unsigned.pop("descriptorSha256")
        corrupted["descriptorSha256"] = hashlib.sha256(
            json.dumps(
                unsigned,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=False,
                allow_nan=False,
            ).encode("utf-8")
        ).hexdigest()
        with pytest.raises(
            PlayableStructureCompilerError,
            match="production exit",
        ):
            validate_playable_structure_descriptor(corrupted)


def test_queue_production_exit_update_resigned_source_paths_require_attestation() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:10 Y:20 Z:30
    NaturalRallyPoint = X:40 Y:50 Z:60
    ExitDelay = 50
    InitialBurst = 2
    AllowAirborneCreation = Yes
    PlacementViewAngle = 90
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    mixed_case = deepcopy(descriptor)
    row = mixed_case["gameplay"]["productionExitUpdates"][0]
    for source_row in (
        row,
        row["unitCreatePoint"],
        row["naturalRallyPoint"],
        row["exitDelay"],
        row["initialBurst"],
        row["allowAirborneCreation"],
        row["deferredFields"][0],
    ):
        source_row["sourceIni"] = path.upper().replace("/", "\\")
    unsigned = dict(mixed_case)
    unsigned.pop("descriptorSha256")
    mixed_case["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()
    validate_playable_structure_descriptor(mixed_case)

    source_locations = (
        (),
        ("unitCreatePoint",),
        ("naturalRallyPoint",),
        ("exitDelay",),
        ("initialBurst",),
        ("allowAirborneCreation",),
        ("deferredFields", 0),
    )
    for location in source_locations:
        corrupted = deepcopy(descriptor)
        target = corrupted["gameplay"]["productionExitUpdates"][0]
        for part in location:
            target = target[part]
        target["sourceIni"] = "data/ini/object/unattested.ini"
        unsigned = dict(corrupted)
        unsigned.pop("descriptorSha256")
        corrupted["descriptorSha256"] = hashlib.sha256(
            json.dumps(
                unsigned,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=False,
                allow_nan=False,
            ).encode("utf-8")
        ).hexdigest()
        with pytest.raises(
            PlayableStructureCompilerError,
            match="production exit",
        ):
            validate_playable_structure_descriptor(corrupted)


def test_queue_production_exit_update_resigned_closed_schema_refuses_smuggling() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents["data/ini/gamedata.ini"] += b"#define TEST_EXIT_DELAY 50\n"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:10 Y:20 Z:30
    NaturalRallyPoint = X:40 Y:50 Z:60
    ExitDelay = TEST_EXIT_DELAY
    InitialBurst = 2
    AllowAirborneCreation = Yes
    PlacementViewAngle = 90
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    def queue_row(candidate: dict[str, object]) -> dict[str, object]:
        return candidate["gameplay"]["productionExitUpdates"][0]

    mutations = [
        lambda candidate: candidate["sourceDocuments"][0].__setitem__(
            "ignored", True
        ),
        lambda candidate: candidate["sourceDocuments"][0].pop("sha256"),
        lambda candidate: candidate["sourceDocuments"][0].__setitem__(
            "sha256", "A" * 64
        ),
        lambda candidate: queue_row(candidate).__setitem__("ignored", True),
        lambda candidate: queue_row(candidate).pop("runtimeStatus"),
        lambda candidate: queue_row(candidate)["unitCreatePoint"].__setitem__(
            "ignored", True
        ),
        lambda candidate: queue_row(candidate)["unitCreatePoint"].pop("line"),
        lambda candidate: queue_row(candidate)["unitCreatePoint"][
            "value"
        ].__setitem__("w", 1.0),
        lambda candidate: queue_row(candidate)["exitDelay"].__setitem__(
            "ignored", True
        ),
        lambda candidate: queue_row(candidate)["exitDelay"].pop("unit"),
        lambda candidate: queue_row(candidate)["exitDelay"][
            "resolvedDefine"
        ].__setitem__("ignored", True),
        lambda candidate: queue_row(candidate)["exitDelay"][
            "resolvedDefine"
        ].pop("value"),
        lambda candidate: queue_row(candidate)[
            "allowAirborneCreation"
        ].__setitem__("ignored", True),
        lambda candidate: queue_row(candidate)[
            "allowAirborneCreation"
        ].pop("line"),
        lambda candidate: queue_row(candidate)["deferredFields"][
            0
        ].__setitem__("ignored", True),
        lambda candidate: queue_row(candidate)["deferredFields"][0].pop(
            "reason"
        ),
    ]
    for mutate in mutations:
        corrupted = deepcopy(descriptor)
        mutate(corrupted)
        _resign_structure_descriptor(corrupted)
        with pytest.raises(
            PlayableStructureCompilerError,
            match="production exit",
        ):
            validate_playable_structure_descriptor(corrupted)


def test_queue_production_exit_update_resigned_schema_requires_exact_types() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:10 Y:20 Z:30
    NaturalRallyPoint = X:40 Y:50 Z:60
    ExitDelay = 50
    InitialBurst = 2
    AllowAirborneCreation = Yes
    PlacementViewAngle = 90
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    mutations = (
        lambda row: row.__setitem__("line", True),
        lambda row: row["unitCreatePoint"].__setitem__("line", True),
        lambda row: row["unitCreatePoint"]["value"].__setitem__("x", 10),
        lambda row: row["exitDelay"].__setitem__("line", True),
        lambda row: row["initialBurst"].__setitem__("value", 2.0),
        lambda row: row["allowAirborneCreation"].__setitem__("line", True),
        lambda row: row["deferredFields"][0].__setitem__("line", True),
    )
    for mutate in mutations:
        corrupted = deepcopy(descriptor)
        mutate(corrupted["gameplay"]["productionExitUpdates"][0])
        _resign_structure_descriptor(corrupted)
        with pytest.raises(
            PlayableStructureCompilerError,
            match="production exit",
        ):
            validate_playable_structure_descriptor(corrupted)

    defaulted = deepcopy(descriptor)
    row = defaulted["gameplay"]["productionExitUpdates"][0]
    for field_name, replacement in (
        (
            "unitCreatePoint",
            {
                "authored": "",
                "value": {"x": 0.0, "y": 0.0, "z": 0.0},
                "defaulted": "true",
            },
        ),
        (
            "exitDelay",
            {
                "authored": "0",
                "value": 0,
                "defaulted": 1,
                "unit": "milliseconds",
            },
        ),
        (
            "initialBurst",
            {"authored": "0", "value": 0, "defaulted": False},
        ),
        (
            "allowAirborneCreation",
            {"authored": "No", "value": False, "defaulted": "yes"},
        ),
    ):
        mistyped = deepcopy(descriptor)
        mistyped["gameplay"]["productionExitUpdates"][0][field_name] = (
            replacement
        )
        _resign_structure_descriptor(mistyped)
        with pytest.raises(
            PlayableStructureCompilerError, match="production exit"
        ):
            validate_playable_structure_descriptor(mistyped)


@pytest.mark.parametrize("contradictory", [False, True])
def test_queue_production_exit_update_resigned_duplicate_sources_refused(
    contradictory: bool,
) -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:10 Y:20 Z:30
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    source = next(
        row
        for row in descriptor["sourceDocuments"]
        if row["virtualPath"].replace("\\", "/").casefold()
        == path.casefold()
    )
    duplicate = deepcopy(source)
    duplicate["virtualPath"] = path.upper().replace("/", "\\")
    if contradictory:
        duplicate["sha256"] = "0" * 64
    descriptor["sourceDocuments"].append(duplicate)
    _resign_structure_descriptor(descriptor)

    with pytest.raises(
        PlayableStructureCompilerError,
        match="contradict|duplicated",
    ):
        validate_playable_structure_descriptor(descriptor)


def test_queue_production_exit_update_repairs_the_retail_authored_coord_typo() -> None:
    """RotWK AngmarKennelExpansion authors ``X:70.0.0`` and must still compile.

    RE-PINNED 2026-08-04 (retail rebase). This test previously asserted the
    OPPOSITE - that the token fails closed - while the compiler had already
    been taught to repair it (`_queue_exit_coord_values`, "Known RotWK typo").
    Both halves of that contradiction were committed together in the public
    snapshot a1d207a, so this test has been red on `main` ever since; it was
    invisible because `tools/gate-retail.ps1` runs `unittest discover`, which
    cannot collect this module's module-level test functions.

    The repair is the CORRECT half. Pure RotWK 2.01 really does author the
    extra ``.0``:

        object/evilfaction/structures/angmar/angmarkennelexpansion.ini:246
            NaturalRallyPoint  = X:70.0.0 Y:0.0 Z:0.0

    Failing closed there would make the Angmar faction uncompilable. So the
    typo is accepted with the authored text preserved verbatim, and the
    repaired value must be exactly 70.0 - never a guess, never a default.
    """

    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0.0 Y:0.0 Z:0.0
    NaturalRallyPoint = X:70.0.0 Y:0.0 Z:0.0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    exit_rows = [
        row
        for row in descriptor["gameplay"]["productionExitUpdates"]
        if row.get("module") == "QueueProductionExitUpdate"
    ]
    assert len(exit_rows) == 1
    rally = exit_rows[0]["naturalRallyPoint"]
    # The authored text is retained verbatim - the repair must never rewrite
    # the record of what retail actually said.
    assert rally["authored"] == "X:70.0.0 Y:0.0 Z:0.0"
    assert rally["value"] == {"x": 70.0, "y": 0.0, "z": 0.0}


def test_queue_production_exit_update_still_rejects_a_genuinely_malformed_coord() -> None:
    """The typo repair must not become a general "parse anything" fallback."""

    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0.0 Y:0.0 Z:0.0
    NaturalRallyPoint = X:seventy Y:0.0 Z:0.0
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    with pytest.raises(
        PlayableStructureCompilerError,
        match="not an exact X/Y/Z Coord3D",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_inherit_upgrade_create_compiles_exact_creation_contract() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = InheritUpgradeCreate ModuleTag_InheritStonework
    Radius = KEEP_INHERIT_RADIUS
    Upgrade = Upgrade_StoneWork
    ObjectFilter = ANY +TestCitadel
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/gamedata.ini"] += b"#define KEEP_INHERIT_RADIUS 400\n"
    documents["data/ini/upgrade.ini"] = b"""Upgrade Upgrade_StoneWork
  Type = OBJECT
End
"""

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["gameplay"]["inheritUpgradesOnCreate"] == [
        {
            "radius": {"authored": "KEEP_INHERIT_RADIUS", "value": 400},
            "upgradeId": "Upgrade_StoneWork",
            "upgradeType": "OBJECT",
            "objectFilter": "ANY +TestCitadel",
            "sourceObjectId": "TestCitadel",
            "module": "InheritUpgradeCreate",
            "sourceIni": path,
            "line": descriptor["gameplay"]["inheritUpgradesOnCreate"][0]["line"],
        }
    ]


def test_inherit_upgrade_create_descriptor_rejects_resigned_non_object_upgrade() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = InheritUpgradeCreate ModuleTag_InheritStonework
    Radius = 400
    Upgrade = Upgrade_StoneWork
    ObjectFilter = ANY +TestCitadel
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""Upgrade Upgrade_StoneWork
  Type = OBJECT
End
"""
    corrupted = compile_playable_structure_descriptor("TestKeep", documents)
    corrupted["gameplay"]["inheritUpgradesOnCreate"][0]["upgradeType"] = "PLAYER"
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(
        PlayableStructureCompilerError, match="inherited upgrade row"
    ):
        validate_playable_structure_descriptor(corrupted)


def test_inherit_upgrade_create_rejects_unproven_filter_grammar() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = InheritUpgradeCreate ModuleTag_InheritStonework
    Radius = 400
    Upgrade = Upgrade_StoneWork
    ObjectFilter = ANY +TestCitadel -MONSTER
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""Upgrade Upgrade_StoneWork
  Type = OBJECT
End
"""

    with pytest.raises(
        PlayableStructureCompilerError, match="unsupported ObjectFilter"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


@pytest.mark.parametrize(
    "bad_row",
    [
        b"    Radius = 401\n",
        b"    UnknownField = Yes\n",
    ],
)
def test_inherit_upgrade_create_rejects_duplicate_or_unknown_fields(
    bad_row: bytes,
) -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = InheritUpgradeCreate ModuleTag_InheritStonework
    Radius = 400
    Upgrade = Upgrade_StoneWork
    ObjectFilter = ANY +TestCitadel
"""
        + bad_row
        + b"""  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""Upgrade Upgrade_StoneWork
  Type = OBJECT
End
"""

    with pytest.raises(
        PlayableStructureCompilerError, match="must author exactly one"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_inherit_upgrade_create_requires_object_upgrade_type() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = InheritUpgradeCreate ModuleTag_InheritStonework
    Radius = 400
    Upgrade = Upgrade_StoneWork
    ObjectFilter = ANY +TestCitadel
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""Upgrade Upgrade_StoneWork
  Type = PLAYER
End
"""

    with pytest.raises(PlayableStructureCompilerError, match="Type = OBJECT"):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_highlander_body_is_preserved_as_primary_structure_policy() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Body = StructureBody ModuleTag_Body",
        b"Body = HighlanderBody ModuleTag_Body",
        1,
    )

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    primary = descriptor["gameplay"]["health"]["primary"]
    assert primary["module"] == "HighlanderBody"
    assert primary["maxHealth"]["value"] == 3000
    assert descriptor["gameplay"]["health"]["highlanderBody"]["value"] is True

    corrupted = deepcopy(descriptor)
    corrupted["gameplay"]["health"]["highlanderBody"]["line"] = 0
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(
        PlayableStructureCompilerError, match="HighlanderBody policy"
    ):
        validate_playable_structure_descriptor(corrupted)


def test_grant_upgrade_create_compiles_exact_build_complete_contract() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = GrantUpgradeCreate ModuleTag_BuiltUpgrade
    UpgradeToGrant = Upgrade_TestKeepBuilt
    GiveOnBuildComplete = Yes
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_TestKeepBuilt
  Type = OBJECT
End
"""

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    grant = descriptor["gameplay"]["createGrants"][0]
    assert grant["upgradeId"] == "Upgrade_TestKeepBuilt"
    assert grant["upgradeType"] == "OBJECT"
    assert grant["onCreateWhenComplete"] is False
    assert grant["onBuildComplete"] is True
    assert grant["module"] == "GrantUpgradeCreate"
    assert "data/ini/upgrade.ini" in {
        row["virtualPath"] for row in descriptor["sourceDocuments"]
    }


def test_grant_upgrade_create_rejects_unsupported_status_mask() -> None:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        b"""  Behavior = GrantUpgradeCreate ModuleTag_BuiltUpgrade
    UpgradeToGrant = Upgrade_TestKeepBuilt
    ExemptStatus = RIDER1
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse""",
        1,
    )
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_TestKeepBuilt
  Type = OBJECT
End
"""

    with pytest.raises(
        PlayableStructureCompilerError, match="unsupported ExemptStatus"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_prepared_inputs_preserve_structure_identity() -> None:
    documents = _structure_documents()
    expected = compile_playable_structure_descriptor("TestKeep", documents)
    prepared = prepare_playable_unit_compiler(documents)

    actual = compile_playable_structure_descriptor(
        "TestKeep", documents, prepared=prepared
    )

    assert actual == expected


def test_engine_spawned_composite_requires_declared_policy() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="not a declared engine-spawned"
    ):
        compile_playable_structure_descriptor("TestCitadel", documents)

    descriptor = compile_playable_structure_descriptor(
        "TestCitadel",
        documents,
        engine_spawned_roots=("TestCitadel",),
        engine_spawned_roles={"testcitadel": "fortress-composite-citadel"},
    )
    assert descriptor["production"]["evidence"] == "engine-spawned-composite"
    assert descriptor["production"]["routes"] == []
    assert descriptor["compositeRole"] == "fortress-composite-citadel"

    with pytest.raises(
        PlayableStructureCompilerError, match="undeclared roots"
    ):
        compile_playable_structure_descriptor(
            "TestCitadel",
            documents,
            engine_spawned_roots=("TestCitadel",),
            engine_spawned_roles={"other": "fortress-composite-citadel"},
        )


def test_foundation_construct_command_is_an_authored_route() -> None:
    documents = _structure_documents()
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructTestCitadel
  Command = FOUNDATION_CONSTRUCT
  Object = TestCitadel
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet FoundationCommandSet
  1 = Command_ConstructTestCitadel
End
"""
    ).encode("utf-8")
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object FoundationPad
  CommandSet = FoundationCommandSet
  KindOf = STRUCTURE BASE_FOUNDATION
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestCitadel", documents)

    assert descriptor["production"]["evidence"] == "authored-construct-command"
    route = descriptor["production"]["routes"][0]
    assert route["commandKind"] == "foundation_construct"
    assert route["builderObjectId"] == "FoundationPad"
    # The fixture's foundation button authors no ButtonImage: the route keeps
    # the key absent so downstream image binding records an explicit gap.
    assert "buttonImageId" not in route


def test_wall_template_policy_admits_template_only_structures() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallSegment
  KindOf = STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 500
  End
End
"""
    ).encode("utf-8")

    with pytest.raises(PlayableStructureCompilerError, match="wall-template"):
        compile_playable_structure_descriptor("TestWallSegment", documents)

    descriptor = compile_playable_structure_descriptor(
        "TestWallSegment", documents, wall_template_roots=("TestWallSegment",)
    )
    assert descriptor["production"]["evidence"] == "wall-template"
    assert descriptor["production"]["routes"] == []


def test_foundation_without_health_is_admitted_with_evidence() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object BareFoundation
  KindOf = STRUCTURE BASE_FOUNDATION
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor(
        "BareFoundation", documents, engine_spawned_roots=("BareFoundation",)
    )
    validate_playable_structure_descriptor(descriptor)
    assert descriptor["gameplay"]["health"] is None


def test_non_structure_object_is_rejected() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="no structure KindOf"
    ):
        compile_playable_structure_descriptor("InfantryHorde", documents)


def test_structure_without_body_health_fails_closed() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="no authored body health"
    ):
        compile_playable_structure_descriptor(
            "HollowKeep", documents, engine_spawned_roots=("HollowKeep",)
        )


def test_unresolved_health_constant_fails_closed() -> None:
    documents = _structure_documents()
    documents["data/ini/gamedata.ini"] = documents[
        "data/ini/gamedata.ini"
    ].replace(b"#define KEEP_HEALTH 3000\n", b"")

    with pytest.raises(
        PlayableStructureCompilerError, match="unresolved GameData constant"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_tampered_descriptor_digest_is_rejected() -> None:
    documents = _structure_documents()
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    descriptor["descriptorSha256"] = "0" * 64

    with pytest.raises(
        PlayableStructureCompilerError, match="digest is invalid"
    ):
        validate_playable_structure_descriptor(descriptor)


def test_wall_upgrade_command_is_authored_production_evidence() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallHub
  CommandSet = TestWallHubCommandSet
  KindOf = SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 900
  End
End

Object TestWallGate
  KindOf = SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1200
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet TestWallHubCommandSet
  2 = Command_WallUpgradeToGate
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_WallUpgradeToGate
  Command = OBJECT_UPGRADE
  Options = CANCELABLE NOT_QUEUEABLE
  Object = TestWallGate
  Upgrade = Upgrade_TestWallGate
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestWallGate", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["production"]["evidence"] == "authored-wall-upgrade-command"
    route = descriptor["production"]["routes"][0]
    assert route["surface"] == "wall-upgrade"
    assert route["commandKind"] == "object_upgrade"
    assert route["builderObjectId"] == "TestWallHub"
    assert route["commandSetId"] == "TestWallHubCommandSet"
    assert route["slot"] == 2
    assert route["upgrade"] == ["Upgrade_TestWallGate"]


def test_construct_route_wins_over_wall_upgrade_for_one_structure() -> None:
    documents = _structure_documents()
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_WallUpgradeToKeep
  Command = OBJECT_UPGRADE
  Object = TestKeep
  Upgrade = Upgrade_TestKeep
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet TestWallUpgradeCommandSet
  1 = Command_WallUpgradeToKeep
End
"""
    ).encode("utf-8")
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallUpgradeHub
  CommandSet = TestWallUpgradeCommandSet
  KindOf = STRUCTURE
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["production"]["evidence"] == "authored-construct-command"
    surfaces = {route["surface"] for route in descriptor["production"]["routes"]}
    assert surfaces == {"construct", "wall-upgrade"}


# ---------------------------------------------------------------------------
# Purchased structure level chain tests.
# ---------------------------------------------------------------------------


def _upgradeable_documents() -> dict[str, bytes]:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object UpgradeableKeep
  CommandSet = UpgradeableKeepCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = 800
  BuildTime = 45.0
  VisionRange = 200
  DisplayName = OBJECT:UpgradeableKeep
  Body = StructureBody ModuleTag_Body
    MaxHealth = 3000
  End
  Behavior = SubObjectsUpgrade ModuleTag_HideAll
    TriggeredBy = Upgrade_StructureLevel1
    HideSubObjects = V1 V1FLAG V2 V2A V1_PIECE* V2_PIECE*
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowWallsAndFlag
    TriggeredBy = Upgrade_KeepLevel2
    ShowSubObjects = V1 V1FLAG V1_PIECE*
    HideSubObjects = V2 V2A V2_PIECE*
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowTowers
    TriggeredBy = Upgrade_KeepLevel3
    ShowSubObjects = V1 V2 V2A V1_PIECE* V2_PIECE*
    HideSubObjects = V1FLAG
  End
  Behavior = LevelUpUpgrade ModuleTag_KeepLevel2
    TriggeredBy = Upgrade_KeepLevel2
    LevelsToGain = 1
    LevelCap = 3
  End
  Behavior = LevelUpUpgrade ModuleTag_KeepLevel3
    TriggeredBy = Upgrade_KeepLevel3
    LevelsToGain = 1
    LevelCap = 3
  End
  Behavior = CommandSetUpgrade ModuleTag_KeepLevel2Set
    TriggeredBy = Upgrade_KeepLevel2
    CommandSet = UpgradeableKeepCommandSetLevel2
  End
  Behavior = CommandSetUpgrade ModuleTag_KeepLevel3Set
    TriggeredBy = Upgrade_KeepLevel3
    CommandSet = UpgradeableKeepCommandSetLevel3
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet UpgradeableKeepCommandSet
  1 = Command_BuildInfantry
  5 = Command_PurchaseUpgradeKeepLevel2
End
CommandSet UpgradeableKeepCommandSetLevel2
  1 = Command_BuildRanged
  5 = Command_PurchaseUpgradeKeepLevel3
End
CommandSet UpgradeableKeepCommandSetLevel3
  1 = Command_BuildRanged
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructUpgradeableKeep
  Command = PORTER_CONSTRUCT
  Object = UpgradeableKeep
End
CommandButton Command_PurchaseUpgradeKeepLevel2
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_KeepLevel2
  Options = CANCELABLE
  TextLabel = CONTROLBAR:KeepLevel2
  DescriptLabel = CONTROLBAR:ToolTipKeepLevel2
  ButtonImage = UCCommon_UpgradeStructureNew
End
CommandButton Command_PurchaseUpgradeKeepLevel3
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_KeepLevel3
  TextLabel = CONTROLBAR:KeepLevel3
  DescriptLabel = CONTROLBAR:ToolTipKeepLevel3
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8").replace(
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\nEnd",
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\n  2 = Command_ConstructUpgradeableKeep\nEnd",
            1,
        )
    ).encode("utf-8")
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_KeepLevel2
  Type = OBJECT
  BuildCost = KEEP_LEVEL2_COST
  BuildTime = KEEP_LEVEL2_BUILDTIME
  DisplayName = Upgrade:KeepLevel2
End
Upgrade Upgrade_KeepLevel3
  Type = OBJECT
  BuildCost = KEEP_LEVEL3_COST
  BuildTime = KEEP_LEVEL3_BUILDTIME
  DisplayName = Upgrade:KeepLevel3
End
"""
    documents["data/ini/experiencelevels.ini"] = b"""
ExperienceLevel KeepLevel1
  TargetNames = UpgradeableKeep
  RequiredExperience = 1
  ExperienceAward = 50
  Rank = 1
End
ExperienceLevel KeepLevel2
  TargetNames = UpgradeableKeep
  RequiredExperience = 100
  ExperienceAward = 60
  Rank = 2
  AttributeModifiers = KeepBuildSpeedModLvl2
  Upgrades = Upgrade_KeepLevel2
End
ExperienceLevel KeepLevel3
  TargetNames = UpgradeableKeep
  RequiredExperience = 1000
  ExperienceAward = 70
  Rank = 3
  AttributeModifiers = KeepBuildSpeedModLvl3
  Upgrades = Upgrade_KeepLevel3
End
"""
    documents["data/ini/attributemodifier.ini"] = b"""
ModifierList KeepBuildSpeedModLvl2
  Category = STRUCTURE
  Modifier = PRODUCTION KEEP_LVL2_BUILD_SPEED
  Modifier = HEALTH KEEP_LVL2_HP_ADD
  Duration = 0
End
ModifierList KeepBuildSpeedModLvl3
  Category = STRUCTURE
  Modifier = PRODUCTION 1.25
  Modifier = HEALTH 1500
  Duration = 0
End
"""
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define KEEP_LEVEL2_COST 500\n"
        + b"#define KEEP_LEVEL2_BUILDTIME 30\n"
        + b"#define KEEP_LEVEL3_COST 650\n"
        + b"#define KEEP_LEVEL3_BUILDTIME 60\n"
        + b"#define KEEP_LVL2_BUILD_SPEED 1.10\n"
        + b"#define KEEP_LVL2_HP_ADD 1500\n"
    )
    return documents


def test_upgrade_chain_compiles_costs_sets_and_level_effects() -> None:
    documents = _upgradeable_documents()

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    chain = descriptor["gameplay"]["upgradeChain"]
    assert chain["levelCap"] == 3
    assert [step["toLevel"] for step in chain["steps"]] == [2, 3]
    level_two, level_three = chain["steps"]
    assert level_two["upgradeId"] == "Upgrade_KeepLevel2"
    assert level_two["commandId"] == "Command_PurchaseUpgradeKeepLevel2"
    assert level_two["slot"] == 5
    assert level_two["cost"] == 500
    assert level_two["buildTimeSeconds"] == 30
    assert level_two["cancelable"] is True
    assert level_two["fromCommandSet"] == "UpgradeableKeepCommandSet"
    assert level_two["toCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert "requiresUpgradeId" not in level_two
    assert level_two["effects"] == [
        {
            "id": "KeepBuildSpeedModLvl2",
            "modifiers": [
                {"kind": "PRODUCTION", "value": 1.1, "application": "multiplicative"},
                {"kind": "HEALTH", "value": 1500, "application": "additive"},
            ],
            "sourceIni": "data/ini/attributemodifier.ini",
            "category": "STRUCTURE",
        }
    ]
    assert level_two["buttonLabels"] == [
        "CONTROLBAR:KeepLevel2",
        "CONTROLBAR:ToolTipKeepLevel2",
        "UCCommon_UpgradeStructureNew",
    ]
    assert level_two["labelId"] == "CONTROLBAR:KeepLevel2"
    assert level_two["tooltipId"] == "CONTROLBAR:ToolTipKeepLevel2"
    assert level_two["buttonImageId"] == "UCCommon_UpgradeStructureNew"
    # Per-level model variants: the authored SubObjectsUpgrade directives ride
    # each step with the cumulative visibility resolved from the level-one
    # base state.
    level_one = chain["levelOne"]
    assert level_one["hiddenSubObjects"] == [
        "V1", "V1_PIECE*", "V1FLAG", "V2", "V2_PIECE*", "V2A"
    ]
    assert level_one["visibleSubObjects"] == []
    presentation_two = level_two["presentation"]
    assert presentation_two["subObjects"] == [
        {
            "show": ["V1", "V1FLAG", "V1_PIECE*"],
            "hide": ["V2", "V2A", "V2_PIECE*"],
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": presentation_two["subObjects"][0]["line"],
        }
    ]
    assert presentation_two["visibleSubObjects"] == ["V1", "V1_PIECE*", "V1FLAG"]
    assert presentation_two["hiddenSubObjects"] == ["V2", "V2_PIECE*", "V2A"]
    presentation_three = level_three["presentation"]
    assert presentation_three["visibleSubObjects"] == [
        "V1", "V1_PIECE*", "V2", "V2_PIECE*", "V2A"
    ]
    assert presentation_three["hiddenSubObjects"] == ["V1FLAG"]
    assert level_three["requiresUpgradeId"] == "Upgrade_KeepLevel2"
    assert level_three["fromCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert level_three["toCommandSet"] == "UpgradeableKeepCommandSetLevel3"
    assert level_three["cancelable"] is False
    assert level_three["cost"] == 650
    sources = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/upgrade.ini" in sources
    assert "data/ini/experiencelevels.ini" in sources
    assert "data/ini/attributemodifier.ini" in sources


def test_structure_without_level_upgrades_has_no_chain_key() -> None:
    documents = _upgradeable_documents()

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert "upgradeChain" not in descriptor["gameplay"]


def test_upgrade_chain_missing_purchase_button_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8").replace(
            "  5 = Command_PurchaseUpgradeKeepLevel3\n", "", 1
        )
    ).encode("utf-8")

    with pytest.raises(PlayableStructureCompilerError, match="not purchasable"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_upgrade_chain_missing_upgrade_block_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_KeepLevel2
  Type = OBJECT
  BuildCost = 500
  BuildTime = 30
End
"""

    with pytest.raises(PlayableStructureCompilerError, match="no .*Upgrade.* block|Upgrade_KeepLevel3"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_upgrade_chain_missing_experience_chain_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/experiencelevels.ini"] = b"""
ExperienceLevel UnrelatedLevel1
  TargetNames = UnrelatedKeep
  RequiredExperience = 1
  ExperienceAward = 50
  Rank = 1
End
"""

    with pytest.raises(PlayableStructureCompilerError, match="no ExperienceLevel chain"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def _structure_level_alias_documents() -> dict[str, bytes]:
    """Rewrite the keep to retail's aliased authoring style.

    IsengardSiegeWorks, GoblinCave, GoblinFissure, WildSpiderPit, and
    DwarvenArcheryRange key their CommandSetUpgrade and per-level
    SubObjectsUpgrade modules to the engine-granted
    Upgrade_StructureLevel<N> ids instead of the purchased upgrade ids.
    """

    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    text = documents[objects_path].decode("utf-8")
    text = text.replace(
        "  Behavior = SubObjectsUpgrade ModuleTag_ShowWallsAndFlag\n"
        "    TriggeredBy = Upgrade_KeepLevel2\n",
        "  Behavior = SubObjectsUpgrade ModuleTag_ShowWallsAndFlag\n"
        "    TriggeredBy = Upgrade_StructureLevel2\n",
        1,
    )
    text = text.replace(
        "  Behavior = SubObjectsUpgrade ModuleTag_ShowTowers\n"
        "    TriggeredBy = Upgrade_KeepLevel3\n",
        "  Behavior = SubObjectsUpgrade ModuleTag_ShowTowers\n"
        "    TriggeredBy = Upgrade_StructureLevel3\n",
        1,
    )
    text = text.replace(
        "  Behavior = CommandSetUpgrade ModuleTag_KeepLevel2Set\n"
        "    TriggeredBy = Upgrade_KeepLevel2\n",
        "  Behavior = CommandSetUpgrade ModuleTag_KeepLevel2Set\n"
        "    TriggeredBy = Upgrade_StructureLevel2\n",
        1,
    )
    text = text.replace(
        "  Behavior = CommandSetUpgrade ModuleTag_KeepLevel3Set\n"
        "    TriggeredBy = Upgrade_KeepLevel3\n",
        "  Behavior = CommandSetUpgrade ModuleTag_KeepLevel3Set\n"
        "    TriggeredBy = Upgrade_StructureLevel3\n",
        1,
    )
    documents[objects_path] = text.encode("utf-8")
    return documents


def test_upgrade_chain_resolves_structure_level_alias_transitions() -> None:
    # Measured retail evidence: the five aliased structures swap command sets
    # through Upgrade_StructureLevel<N>; the compiled chain must carry the
    # real swap, marked so downstream can tell it from the purchased-trigger
    # authoring style.
    documents = _structure_level_alias_documents()

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    chain = descriptor["gameplay"]["upgradeChain"]
    level_two, level_three = chain["steps"]
    assert level_two["commandSetTransition"] == "structure-level"
    assert level_two["fromCommandSet"] == "UpgradeableKeepCommandSet"
    assert level_two["toCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert level_three["commandSetTransition"] == "structure-level"
    assert level_three["fromCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert level_three["toCommandSet"] == "UpgradeableKeepCommandSetLevel3"
    # The aliased SubObjectsUpgrade rows ride their steps instead of landing
    # in unconsumedSubObjectTriggers.
    assert level_two["presentation"]["visibleSubObjects"] == [
        "V1", "V1_PIECE*", "V1FLAG"
    ]
    assert level_three["presentation"]["hiddenSubObjects"] == ["V1FLAG"]
    assert "unconsumedSubObjectTriggers" not in chain


def test_upgrade_chain_authored_transitions_carry_no_marker() -> None:
    # The purchased-trigger family must stay byte-identical: no marker key.
    documents = _upgradeable_documents()

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    for step in descriptor["gameplay"]["upgradeChain"]["steps"]:
        assert "commandSetTransition" not in step


def _identity_documents() -> dict[str, bytes]:
    """Strip every CommandSetUpgrade: the command set legitimately stays put."""

    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    text = documents[objects_path].decode("utf-8")
    for tag, trigger, set_id in (
        ("ModuleTag_KeepLevel2Set", "Upgrade_KeepLevel2", "UpgradeableKeepCommandSetLevel2"),
        ("ModuleTag_KeepLevel3Set", "Upgrade_KeepLevel3", "UpgradeableKeepCommandSetLevel3"),
    ):
        text = text.replace(
            f"  Behavior = CommandSetUpgrade {tag}\n"
            f"    TriggeredBy = {trigger}\n"
            f"    CommandSet = {set_id}\n"
            "  End\n",
            "",
            1,
        )
    documents[objects_path] = text.encode("utf-8")
    # With no set swap, both purchase buttons live on the one direct set.
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8").replace(
            "CommandSet UpgradeableKeepCommandSet\n"
            "  1 = Command_BuildInfantry\n"
            "  5 = Command_PurchaseUpgradeKeepLevel2\n"
            "End",
            "CommandSet UpgradeableKeepCommandSet\n"
            "  1 = Command_BuildInfantry\n"
            "  5 = Command_PurchaseUpgradeKeepLevel2\n"
            "  6 = Command_PurchaseUpgradeKeepLevel3\n"
            "End",
            1,
        )
    ).encode("utf-8")
    return documents


def test_upgrade_chain_without_command_set_upgrade_compiles_identity() -> None:
    documents = _identity_documents()

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    chain = descriptor["gameplay"]["upgradeChain"]
    assert [step["toLevel"] for step in chain["steps"]] == [2, 3]
    for step in chain["steps"]:
        assert step["commandSetTransition"] == "identity"
        assert step["fromCommandSet"] == "UpgradeableKeepCommandSet"
        assert step["toCommandSet"] == "UpgradeableKeepCommandSet"
    # Stat/model evidence still rides the steps.
    assert chain["steps"][0]["cost"] == 500
    assert chain["steps"][0]["effects"]
    assert chain["steps"][0]["presentation"]["visibleSubObjects"] == [
        "V1", "V1_PIECE*", "V1FLAG"
    ]


def test_identity_step_missing_build_cost_still_fails_closed() -> None:
    documents = _identity_documents()
    documents["data/ini/upgrade.ini"] = (
        documents["data/ini/upgrade.ini"]
        .decode("utf-8")
        .replace("  BuildCost = KEEP_LEVEL2_COST\n", "", 1)
        .encode("utf-8")
    )

    with pytest.raises(
        PlayableStructureCompilerError, match="lacks authored BuildCost/BuildTime"
    ):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_level_cap_parses_leading_integer_like_retail() -> None:
    # GoblinFissure's Level3 module authors "LevelCap = 3w"
    # (data/ini/object/evilfaction/structures/wild/fissure.ini); the SAGE
    # atoi-style scanner reads 3 and the retail engine caps at level 3.
    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace(
            "    TriggeredBy = Upgrade_KeepLevel3\n"
            "    LevelsToGain = 1\n"
            "    LevelCap = 3\n",
            "    TriggeredBy = Upgrade_KeepLevel3\n"
            "    LevelsToGain = 1\n"
            "    LevelCap = 3w\n",
            1,
        )
        .encode("utf-8")
    )

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    chain = descriptor["gameplay"]["upgradeChain"]
    assert chain["levelCap"] == 3
    assert chain["steps"][1]["levelCap"] == 3
    assert chain["steps"][1]["toLevel"] == 3


def test_level_cap_without_leading_digit_fails_closed() -> None:
    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace("    LevelCap = 3\n", "    LevelCap = w3\n", 1)
        .encode("utf-8")
    )

    with pytest.raises(
        PlayableStructureCompilerError, match="no valid LevelCap"
    ):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_cost_modifier_variant_without_apply_list_is_unsupported_evidence() -> None:
    # Retail's other CostModifierUpgrade shapes (IsengardFortress Excavations
    # ObjectFilter discount, MenGarrisonTowerExpansion Slaughter modifier)
    # must not fail the structure closed; the PLAYER-bound ones surface as
    # unsupported-effect evidence.
    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace(
            "Object UpgradeableKeep\n",
            "Object UpgradeableKeep\n"
            "  Behavior = CostModifierUpgrade ModuleTag_WallDiscount\n"
            "    TriggeredBy = Upgrade_KeepExcavations\n"
            "    ObjectFilter = ANY +STRUCTURE\n"
            "    Percentage = -10%\n"
            "  End\n"
            "  Behavior = CostModifierUpgrade ModuleTag_Slaughter\n"
            "    StartsActive = Yes\n"
            "    Slaughter = Yes\n"
            "    Percentage = 25%\n"
            "  End\n",
            1,
        )
        .encode("utf-8")
    )
    documents["data/ini/upgrade.ini"] = (
        documents["data/ini/upgrade.ini"]
        + b"""
Upgrade Upgrade_KeepExcavations
  Type = PLAYER
  BuildCost = 300
  BuildTime = 30
End
"""
    )

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    effects = descriptor["gameplay"]["upgradeEffects"]
    unsupported = effects["unsupportedEffects"]
    assert any(
        row["upgradeId"] == "Upgrade_KeepExcavations"
        and row["module"] == "CostModifierUpgrade"
        for row in unsupported
    )
    # The Slaughter block binds no PLAYER upgrade: no invented evidence.
    assert all(row["upgradeId"] != "" for row in unsupported)


def test_cost_modifier_with_apply_list_missing_trigger_still_fails() -> None:
    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace(
            "Object UpgradeableKeep\n",
            "Object UpgradeableKeep\n"
            "  Behavior = CostModifierUpgrade ModuleTag_BadDiscount\n"
            "    ApplyToTheseUpgrades = Upgrade_KeepLevel2\n"
            "  End\n",
            1,
        )
        .encode("utf-8")
    )

    with pytest.raises(
        PlayableStructureCompilerError,
        match="CostModifierUpgrade lacks TriggeredBy",
    ):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_starts_active_cost_modifier_with_apply_list_is_declared_unsupported() -> None:
    documents = _upgradeable_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace(
            "Object UpgradeableKeep\n",
            "Object UpgradeableKeep\n"
            "  Behavior = CostModifierUpgrade ModuleTag_ActiveDiscount\n"
            "    StartsActive = Yes\n"
            "    ApplyToTheseUpgrades = Upgrade_KeepResearch\n"
            "    Percentage = -10%\n"
            "  End\n",
            1,
        )
        .encode("utf-8")
    )
    documents["data/ini/upgrade.ini"] += b"""
Upgrade Upgrade_KeepResearch
  Type = PLAYER
  BuildCost = 100
  BuildTime = 10
End
"""

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    unsupported = descriptor["gameplay"]["upgradeEffects"]["unsupportedEffects"]
    assert any(
        row["upgradeId"] == "Upgrade_KeepResearch"
        and "always-active" in row["reason"]
        for row in unsupported
    )


def test_upgrade_chain_unresolvable_cost_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/upgrade.ini"] = (
        documents["data/ini/upgrade.ini"]
        .decode("utf-8")
        .replace("KEEP_LEVEL2_COST", "UNDEFINED_KEEP_COST", 1)
        .encode("utf-8")
    )

    with pytest.raises(PlayableStructureCompilerError, match="GameData constant"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def _auto_deposit_documents(body: str) -> dict[str, bytes]:
    documents = _structure_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = (
        documents[path]
        .decode("utf-8")
        .replace(
            "Object TestKeep\n",
            "Object TestKeep\n"
            "  Behavior = AutoDepositUpdate ModuleTag_AutoDeposit\n"
            f"{body}"
            "  End\n",
            1,
        )
        .encode("utf-8")
    )
    return documents


def test_auto_deposit_compiles_exact_runtime_contract() -> None:
    documents = _auto_deposit_documents(
        "    DepositTiming = 5050\n"
        "    DepositAmount = KEEP_INCOME\n"
        "    InitialCaptureBonus = 45\n"
        "    ActualMoney = No\n"
        "    UpgradedBoost = UpgradeType:Upgrade_StoneWork Boost:25\n"
    )
    documents["data/ini/gamedata.ini"] += b"#define KEEP_INCOME 35\n"
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_StoneWork
  Type = PLAYER
End
"""

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    row = descriptor["gameplay"]["autoDepositUpdates"][0]
    assert row["runtimeStatus"] == "executable"
    assert row["depositTiming"]["value"] == 5050
    assert row["depositTiming"]["unit"] == "milliseconds"
    assert row["depositTiming"]["simulationTicks"] == 51
    assert row["depositAmount"]["resolvedDefine"] == {
        "name": "KEEP_INCOME",
        "value": 35,
    }
    assert row["initialCaptureBonus"]["value"] == 45
    assert row["actualMoney"]["value"] is False
    assert row["upgradedBoosts"][0]["upgradeId"] == "Upgrade_StoneWork"
    assert row["upgradedBoosts"][0]["upgradeType"] == "PLAYER"
    assert row["upgradedBoosts"][0]["boost"] == 25


def test_auto_deposit_defaults_and_bfme_only_fields_defer() -> None:
    documents = _auto_deposit_documents(
        "    DepositTiming = 5000\n"
        "    DepositAmount = 25\n"
        "    GiveNoXP = Yes\n"
        "    OnlyWhenGarrisoned = Yes\n"
    )

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    row = descriptor["gameplay"]["autoDepositUpdates"][0]
    assert row["runtimeStatus"] == "deferred"
    assert row["actualMoney"] == {
        "authored": "Yes",
        "value": True,
        "defaulted": True,
    }
    assert row["initialCaptureBonus"] == {
        "authored": "0",
        "value": 0,
        "defaulted": True,
    }
    assert [field["name"] for field in row["deferredFields"]] == [
        "GiveNoXP",
        "OnlyWhenGarrisoned",
    ]


def test_auto_deposit_unknown_field_fails_closed() -> None:
    documents = _auto_deposit_documents(
        "    DepositTiming = 5000\n"
        "    InventedIncome = 25\n"
    )
    with pytest.raises(
        PlayableStructureCompilerError, match="unsupported fields"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_auto_deposit_timing_projection_is_digest_validated() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep",
        _auto_deposit_documents(
            "    DepositTiming = 5000\n"
            "    DepositAmount = 25\n"
        ),
    )
    descriptor["gameplay"]["autoDepositUpdates"][0]["depositTiming"][
        "simulationTicks"
    ] = 49
    _resign_structure_descriptor(descriptor)
    with pytest.raises(
        PlayableStructureCompilerError, match="auto-deposit timing"
    ):
        validate_playable_structure_descriptor(descriptor)


@pytest.mark.parametrize(
    ("field_name", "mutated"),
    [
        ("depositTiming", {"authored": "1", "value": 1, "defaulted": True,
                           "unit": "milliseconds", "simulationTicks": 1}),
        ("depositAmount", {"authored": "1", "value": 1, "defaulted": True}),
        ("initialCaptureBonus", {"authored": "1", "value": 1, "defaulted": True}),
    ],
)
def test_auto_deposit_resigned_non_cpp_default_rejects(
    field_name: str, mutated: dict[str, object]
) -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _auto_deposit_documents("")
    )
    descriptor["gameplay"]["autoDepositUpdates"][0][field_name] = mutated
    _resign_structure_descriptor(descriptor)
    with pytest.raises(
        PlayableStructureCompilerError, match="auto-deposit default"
    ):
        validate_playable_structure_descriptor(descriptor)


@pytest.mark.parametrize(
    ("field_name", "mutated"),
    [
        ("upgradeId", "Upgrade_Invented"),
        ("upgradeType", "OBJECT"),
        ("boost", 26),
    ],
)
def test_auto_deposit_resigned_boost_projection_mutation_rejects(
    field_name: str, mutated: object
) -> None:
    documents = _auto_deposit_documents(
        "    UpgradedBoost = UpgradeType:Upgrade_StoneWork Boost:25\n"
    )
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_StoneWork
  Type = PLAYER
End
"""
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    descriptor["gameplay"]["autoDepositUpdates"][0]["upgradedBoosts"][0][
        field_name
    ] = mutated
    _resign_structure_descriptor(descriptor)
    with pytest.raises(
        PlayableStructureCompilerError, match="auto-deposit boost"
    ):
        validate_playable_structure_descriptor(descriptor)


def test_auto_deposit_object_upgrade_type_fails_closed_at_compile() -> None:
    documents = _auto_deposit_documents(
        "    UpgradedBoost = UpgradeType:Upgrade_StoneWork Boost:25\n"
    )
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_StoneWork
  Type = OBJECT
End
"""
    with pytest.raises(
        PlayableStructureCompilerError,
        match="source-attested Type PLAYER",
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_auto_deposit_resigned_coordinated_object_type_mutation_rejects() -> None:
    documents = _auto_deposit_documents(
        "    UpgradedBoost = UpgradeType:Upgrade_StoneWork Boost:25\n"
    )
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_StoneWork
  Type = PLAYER
End
"""
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    boost = descriptor["gameplay"]["autoDepositUpdates"][0]["upgradedBoosts"][0]
    boost["upgradeType"] = "OBJECT"
    boost["upgradeAttestation"]["upgradeType"] = "OBJECT"
    _resign_structure_descriptor(descriptor)
    with pytest.raises(
        PlayableStructureCompilerError, match="auto-deposit boost"
    ):
        validate_playable_structure_descriptor(descriptor)
def _castle_upgrade_documents(
    *,
    include_button: bool = True,
    include_unmatched_button: bool = False,
    upgrade_type: str = "OBJECT",
    build_cost: str | None = "500",
    build_time: str | None = "30.0",
    second_upgrade: bool = False,
    plain_object_upgrade: bool = False,
    castle_upgrade_command: bool = False,
    radial_page_selectors: bool = False,
) -> dict[str, bytes]:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    object_source = documents[objects_path].decode("utf-8")
    castle_behavior = """
  Behavior = CastleUpgrade ModuleTag_TestCastleWalls
    TriggeredBy = Upgrade_TestIceWallsTrigger
    Upgrade = Upgrade_TestIceWalls
    WallUpgradeRadius = 400
  End
"""
    if second_upgrade:
        castle_behavior += """
  Behavior = CastleUpgrade ModuleTag_TestCastleAlpha
    TriggeredBy = Upgrade_AlphaWallsTrigger
    Upgrade = Upgrade_AlphaWalls
    WallUpgradeRadius = 400
  End
"""
    object_source = object_source.replace(
        "  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        castle_behavior + "  Behavior = StructureCollapseUpdate ModuleTag_Collapse",
        1,
    )
    documents[objects_path] = object_source.encode("utf-8")

    command_set = """
  12 = Command_PurchaseUpgradeTestIceWalls
""" if include_button else ""
    if second_upgrade:
        command_set += """
  11 = Command_PurchaseUpgradeAlphaWalls
"""
    if include_unmatched_button:
        command_set += """
  13 = Command_PurchaseUpgradeUnmatched
"""
    if plain_object_upgrade:
        command_set += """
  8 = Command_PurchaseUpgradeTestBanners
"""
    if castle_upgrade_command:
        command_set += """
  9 = Command_PurchaseUpgradeTestHouseOfHealing
"""
    if radial_page_selectors:
        # Retail's fortress command set opens its upgrades/heroes pages with
        # PUSH_VISIBLE_COMMAND_RANGE selectors and closes them with
        # Command_RadialBack, which occupies the LAST slot of every page
        # (DwarvenFortressCommandSet slots 5/14/24, commandset.ini:1931).
        command_set += """
  2 = Command_SelectUpgradesTestFortress
  14 = Command_RadialBack
  24 = Command_RadialBack
"""
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + f"""
CommandSet TestKeepCastleCommandSet
  1 = Command_BuildInfantry
{command_set}End
"""
    ).encode("utf-8")
    documents[objects_path] = documents[objects_path].replace(
        b"  CommandSet = TestKeepCommandSet\n",
        b"  CommandSet = TestKeepCastleCommandSet\n",
        1,
    )

    command_buttons = """
CommandButton Command_PurchaseUpgradeTestIceWalls
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_TestIceWallsTrigger
  Options = CANCELABLE
  NeededUpgrade = Upgrade_StoneWork
  TextLabel = CONTROLBAR:TestIceWalls
  DescriptLabel = CONTROLBAR:ToolTipTestIceWalls
  ButtonImage = UCCommon_UpgradeStructureNew
End
""" if include_button else ""
    if second_upgrade:
        command_buttons += """
CommandButton Command_PurchaseUpgradeAlphaWalls
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_AlphaWallsTrigger
  TextLabel = CONTROLBAR:AlphaWalls
End
"""
    if include_unmatched_button:
        command_buttons += """
CommandButton Command_PurchaseUpgradeUnmatched
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_NotACastleTrigger
End
"""
    if plain_object_upgrade:
        # Retail's Command_PurchaseUpgradeDwarvenFortressBanners shape
        # (commandbutton.ini:13695): a fortress improvement that is bought
        # outright and applies to the fortress itself, with no CastleUpgrade
        # pass-out module behind it.
        command_buttons += """
CommandButton Command_PurchaseUpgradeTestBanners
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_TestBanners
  Options = CANCELABLE
  TextLabel = CONTROLBAR:TestBanners
  DescriptLabel = CONTROLBAR:ToolTipTestBanners
  ButtonImage = BDFortress_Banners
End
"""
    if castle_upgrade_command:
        # Retail's Command_PurchaseUpgradeMenFortressHouseOfHealing
        # (commandbutton.ini:13283) is the one fortress-menu button authored as
        # `Command = CASTLE_UPGRADE` rather than OBJECT_UPGRADE.
        command_buttons += """
CommandButton Command_PurchaseUpgradeTestHouseOfHealing
  Command = CASTLE_UPGRADE
  Upgrade = Upgrade_TestHouseOfHealing
  Options = CANCELABLE
  TextLabel = CONTROLBAR:TestHouseOfHealing
  DescriptLabel = CONTROLBAR:ToolTipTestHouseOfHealing
  ButtonImage = BGFortress_HouseofHealing
End
"""
    if radial_page_selectors:
        command_buttons += """
CommandButton Command_SelectUpgradesTestFortress
  Command = PUSH_VISIBLE_COMMAND_RANGE
  TextLabel = CONTROLBAR:SelectUpgradesTestFortress
  ButtonImage = UCCommon_UpgradeStructureNew
  ButtonBorderType = SYSTEM
  DescriptLabel = CONTROLBAR:ToolTipCommandSelectUpgradesTestFortress
  Radial = Yes
	CommandRangeStart		= 7	//Starts its counting at 0, so command button 8 is 7
	CommandRangeCount		= 7
End
CommandButton Command_RadialBack
  Command = POP_VISIBLE_COMMAND_RANGE
  Options = OK_FOR_MULTI_SELECT
  TextLabel = CONTROLBAR:RadialBack
  ButtonImage = UCCommon_BackArrow
  ButtonBorderType = SYSTEM
  DescriptLabel = CONTROLBAR:ToolTipCommandRadialBack
  Radial = Yes
  InPalantir = No
End
"""
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + command_buttons
    ).encode("utf-8")

    upgrade_block = [
        "Upgrade Upgrade_TestIceWallsTrigger",
        f"  Type = {upgrade_type}",
    ]
    if build_cost is not None:
        upgrade_block.append(f"  BuildCost = {build_cost}")
    if build_time is not None:
        upgrade_block.append(f"  BuildTime = {build_time}")
    upgrade_block.append("End")
    if second_upgrade:
        upgrade_block.extend(
            [
                "Upgrade Upgrade_AlphaWallsTrigger",
                "  Type = OBJECT",
                "  BuildCost = 250",
                "  BuildTime = 15.0",
                "End",
            ]
        )
    if plain_object_upgrade:
        upgrade_block.extend(
            [
                "Upgrade Upgrade_TestBanners",
                "  Type = OBJECT",
                "  BuildCost = 500",
                "  BuildTime = 5.0",
                "End",
            ]
        )
    if castle_upgrade_command:
        upgrade_block.extend(
            [
                "Upgrade Upgrade_TestHouseOfHealing",
                "  Type = OBJECT",
                "  BuildCost = 1000",
                "  BuildTime = 30.0",
                "End",
            ]
        )
    documents["data/ini/upgrade.ini"] = (
        "\n".join(upgrade_block) + "\n"
    ).encode("utf-8")
    return documents


def _castle_upgrade_documents_duplicate_slots() -> dict[str, bytes]:
    """Same castle trigger sold from two different slots of one command set.

    Retail legitimately reaches one fortress improvement from several command
    sets (the per-level and `_ForMP` variants), so the compiler must emit the
    trigger ONCE - the runtime validator rejects the whole `castleUpgrades`
    surface the moment it sees a duplicate `upgradeId`
    (retail_faction_manifest.gd `_validate_structure_castle_upgrades`). Where
    the authored slot or button DISAGREES between sets, that is a data
    question and must fail closed rather than having a winner picked silently.
    """

    documents = _castle_upgrade_documents()
    source = documents["data/ini/commandset.ini"].decode("utf-8")
    documents["data/ini/commandset.ini"] = source.replace(
        "  12 = Command_PurchaseUpgradeTestIceWalls" + "\n",
        "  12 = Command_PurchaseUpgradeTestIceWalls" + "\n"
        + "  9 = Command_PurchaseUpgradeTestIceWalls" + "\n",
        1,
    ).encode("utf-8")
    return documents


def test_castle_upgrade_conflicting_slots_fail_closed() -> None:
    with pytest.raises(PlayableStructureCompilerError) as error:
        compile_playable_structure_descriptor(
            "TestKeep", _castle_upgrade_documents_duplicate_slots()
        )

    assert "Upgrade_TestIceWallsTrigger" in str(error.value)
    assert "conflicting" in str(error.value)


def test_castle_upgrade_button_emits_trigger_grant_and_authored_purchase_fields() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents()
    )

    row = descriptor["gameplay"]["castleUpgrades"]["upgrades"][0]
    assert row == {
        "upgradeId": "Upgrade_TestIceWallsTrigger",
        "grantsUpgradeId": "Upgrade_TestIceWalls",
        "cost": 500,
        "buildTimeSeconds": 30.0,
        "slot": 12,
        "commandId": "Command_PurchaseUpgradeTestIceWalls",
        "labelId": "CONTROLBAR:TestIceWalls",
        "tooltipId": "CONTROLBAR:ToolTipTestIceWalls",
        "buttonImageId": "UCCommon_UpgradeStructureNew",
        "neededUpgradeIds": ["Upgrade_StoneWork"],
        "cancelable": True,
    }


def test_castle_upgrade_surface_records_authored_radial_page_selectors() -> None:
    """The fortress's page selectors carry retail's OWN string ids.

    The HUD used to spell `CONTROLBAR:SelectUpgrades<Token>Fortress` itself from
    the faction token, which is wrong for two of the seven factions: Angmar's
    selectors are authored with the DWARVEN labels
    (commandbutton.ini:13532/13543) and Wild's heroes page points at
    `CONTROLBAR:SelectRevivablesGoblinFortress` (:13785). Nothing in the packs
    ever NAMED those ids, so the strings lane -- which publishes exactly the
    retail rows a runtime document references -- had no reason to ship them and
    all three buttons drew transcribed fallback text. Compiling the authored
    selectors puts the real ids in the document, which both feeds the strings
    scrape and gives the runtime the id retail actually authored.
    """

    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(radial_page_selectors=True)
    )

    assert descriptor["gameplay"]["castleUpgrades"]["pageSelectors"] == [
        {
            "commandId": "Command_RadialBack",
            "command": "POP_VISIBLE_COMMAND_RANGE",
            "slots": [14, 24],
            "labelId": "CONTROLBAR:RadialBack",
            "tooltipId": "CONTROLBAR:ToolTipCommandRadialBack",
            "buttonImageId": "UCCommon_BackArrow",
        },
        {
            "commandId": "Command_SelectUpgradesTestFortress",
            "command": "PUSH_VISIBLE_COMMAND_RANGE",
            "slots": [2],
            "labelId": "CONTROLBAR:SelectUpgradesTestFortress",
            "tooltipId": "CONTROLBAR:ToolTipCommandSelectUpgradesTestFortress",
            "buttonImageId": "UCCommon_UpgradeStructureNew",
            "commandRangeStart": 7,
            "commandRangeCount": 7,
        },
    ]


def test_castle_upgrade_surface_omits_page_selectors_when_unauthored() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents()
    )

    assert "pageSelectors" not in descriptor["gameplay"]["castleUpgrades"]


@pytest.mark.parametrize("missing_field", ["BuildCost", "BuildTime"])
def test_castle_upgrade_missing_authored_cost_or_time_fails_closed(
    missing_field: str,
) -> None:
    kwargs = {"build_cost": "500", "build_time": "30.0"}
    kwargs["build_cost" if missing_field == "BuildCost" else "build_time"] = None
    with pytest.raises(
        PlayableStructureCompilerError,
        match="Upgrade_TestIceWallsTrigger",
    ):
        compile_playable_structure_descriptor(
            "TestKeep", _castle_upgrade_documents(**kwargs)
        )


def test_castle_upgrade_non_object_trigger_fails_closed() -> None:
    with pytest.raises(
        PlayableStructureCompilerError,
        match="Upgrade_TestIceWallsTrigger",
    ):
        compile_playable_structure_descriptor(
            "TestKeep", _castle_upgrade_documents(upgrade_type="PLAYER")
        )


def test_castle_upgrade_without_selling_button_is_recorded() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(include_button=False)
    )

    gameplay = descriptor["gameplay"]
    assert "castleUpgrades" not in gameplay
    markers = gameplay["nonPurchasableCastleUpgrades"]["upgrades"]
    assert markers == [
        {
            "upgradeId": "Upgrade_TestIceWallsTrigger",
            "grantsUpgradeId": "Upgrade_TestIceWalls",
            "reason": "CastleUpgrade trigger has no selling OBJECT_UPGRADE button",
        }
    ]


def test_object_upgrade_without_castle_trigger_is_recorded_and_not_compiled() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(include_unmatched_button=True)
    )

    gameplay = descriptor["gameplay"]
    assert [
        row["upgradeId"] for row in gameplay["castleUpgrades"]["upgrades"]
    ] == ["Upgrade_TestIceWallsTrigger"]
    assert [
        row["upgradeId"]
        for row in gameplay["nonPurchasableCastleUpgrades"]["upgrades"]
    ] == ["Upgrade_NotACastleTrigger"]


def test_fortress_object_upgrade_without_a_trigger_module_is_still_purchasable() -> None:
    """A fortress improvement that applies to the fortress itself is a SALE.

    Four of the six buttons on `DwarvenFortressCommandSet`'s upgrades page
    (commandset.ini:4107 slots 8, 9, 11, 13 — Banners 500, Siege Kegs 1000, Oil
    Casks 1500, Mighty Catapult 2500) are ordinary `OBJECT_UPGRADE` buttons with
    no `CastleUpgrade` pass-out module behind them, so filing them as
    "non-purchasable" left two thirds of retail's fortress upgrade menu
    unbuyable. They carry authored `BuildCost`/`BuildTime` in upgrade.ini and
    grant nothing onward, which is exactly an empty `grantsUpgradeId`.
    """

    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(plain_object_upgrade=True)
    )

    gameplay = descriptor["gameplay"]
    rows = {row["upgradeId"]: row for row in gameplay["castleUpgrades"]["upgrades"]}
    assert rows["Upgrade_TestBanners"] == {
        "upgradeId": "Upgrade_TestBanners",
        "grantsUpgradeId": "",
        "cost": 500,
        "buildTimeSeconds": 5.0,
        "slot": 8,
        "commandId": "Command_PurchaseUpgradeTestBanners",
        "labelId": "CONTROLBAR:TestBanners",
        "tooltipId": "CONTROLBAR:ToolTipTestBanners",
        "buttonImageId": "BDFortress_Banners",
        "cancelable": True,
    }
    assert "Upgrade_TestBanners" not in {
        row["upgradeId"]
        for row in gameplay.get("nonPurchasableCastleUpgrades", {}).get("upgrades", [])
    }


def test_fortress_castle_upgrade_command_button_is_compiled() -> None:
    """`Command = CASTLE_UPGRADE` sells too.

    Men's House of Healing (commandbutton.ini:13283) is the one fortress-menu
    button retail authors with that command type; skipping it dropped a 1000
    resource purchase out of the Men fortress menu.
    """

    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(castle_upgrade_command=True)
    )

    rows = {
        row["upgradeId"]: row
        for row in descriptor["gameplay"]["castleUpgrades"]["upgrades"]
    }
    assert rows["Upgrade_TestHouseOfHealing"]["cost"] == 1000
    assert rows["Upgrade_TestHouseOfHealing"]["slot"] == 9


def test_structure_without_castle_upgrade_module_omits_castle_surface() -> None:
    descriptor = compile_playable_structure_descriptor("TestKeep", _structure_documents())

    assert "castleUpgrades" not in descriptor["gameplay"]


def test_castle_upgrade_rows_sort_by_casefolded_trigger_id() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _castle_upgrade_documents(second_upgrade=True)
    )

    assert [
        row["upgradeId"] for row in descriptor["gameplay"]["castleUpgrades"]["upgrades"]
    ] == ["Upgrade_AlphaWallsTrigger", "Upgrade_TestIceWallsTrigger"]
