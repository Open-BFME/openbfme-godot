from __future__ import annotations

import unittest

from openbfme_importer.cah_system_compiler import (
    ATTRIBUTE_STEP_COUNT,
    CahSystemCompilerError,
    RUNTIME_SCHEMA,
    RUNTIME_SCHEMA_VERSION,
    SCHEMA,
    SCHEMA_VERSION,
    attribute_spend,
    build_cah_system_runtime,
    compile_cah_system_descriptor,
    validate_cah_system_descriptor,
    validate_cah_system_runtime,
)

# The five attribute families, and the modifier kind each one emits.  Kept here
# rather than imported so the test states retail's shape independently of the
# module under test: if the compiler ever quietly renamed a group, these
# fixtures would stop matching it.
FAMILIES = (
    ("ArmorAttribute", "INNATE_ARMOR", ("ARMOR",)),
    ("DamageMultAttribute", "INNATE_DAMAGE", ("DAMAGE_MULT",)),
    ("HealthMultAttribute", "INNATE_HEALTH", ("HEALTH_MULT",)),
    ("AutoHealAttribute", "INNATE_AUTO_HEAL", ("AUTO_HEAL",)),
    # Vision authors TWO modifiers per step, on different curves; the fixture
    # keeps that shape because it is the only group that does.
    ("VisionAttribute", "INNATE_VISION", ("SHROUD_CLEARING", "VISION")),
)


def _upgrades() -> bytes:
    lines = []
    for family, _category, _kinds in FAMILIES:
        for step in range(1, ATTRIBUTE_STEP_COUNT + 1):
            lines.append(
                f"Upgrade Upgrade_{family}{step:02d}\n"
                f"  Type = OBJECT\n"
                f"  GroupName = CreateAHero_{family}\n"
                f"  GroupOrder = {step - 1}\n"
                f"End"
            )
    return "\n".join(lines).encode("cp1252")


def _modifiers() -> bytes:
    lines = []
    for family, category, kinds in FAMILIES:
        for step in range(1, ATTRIBUTE_STEP_COUNT + 1):
            body = "\n".join(
                f"  Modifier = {kind} "
                f"#MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER {step / 10:.2f} )"
                for kind in kinds
            )
            lines.append(
                f"ModifierList {family}{step:02d}\n"
                f"  Category = {category}\n"
                f"{body}\n"
                f"  Duration = 0\n"
                f"End"
            )
    return "\n".join(lines).encode("cp1252")


def _binders() -> str:
    out = []
    for slot, (family, _category, _kinds) in enumerate(FAMILIES):
        out.append(
            f"\tCreateAHeroBlingBinder\n"
            f"\t\tGroupName = CreateAHero_{family}\n"
            f"\t\tLabelTag = CAH:Label{slot}\n"
            f"\t\tUISlot = {slot}\n"
            f"\t\tBlingType = ATTRIBUTE\n"
            f"\tEnd"
        )
    # One APPEARANCE binder, which must be ignored rather than compiled.
    out.append(
        "\tCreateAHeroBlingBinder\n"
        "\t\tGroupName = CreateAHero_Helmet\n"
        "\t\tLabelTag = CAH:HelmetMenuLabel\n"
        "\t\tUISlot = 0\n"
        "\t\tBlingType = APPEARANCE\n"
        "\tEnd"
    )
    return "\n".join(out)


def _sub_class(name: str, budget: int, steps: dict[str, tuple[int, int, int]]) -> str:
    blocks = [
        f"\t\tNameTag = CreateAHero:SubClassName_{name}",
        f"\t\tDescriptionTag = CreateAHero:SubClassDesc_{name}",
        f"\t\tIconImage = CP{name}",
        f"\t\tButtonImage = HICAH{name}",
        "\t\tUsableFactions = Men Elves Dwarves",
        f"\t\tSpendableAttributePoints = {budget}",
    ]
    for family, (low, high, default) in steps.items():
        blocks.append(
            f"\t\tAttribute\n"
            f"\t\t\tGroupName = CreateAHero_{family}\n"
            f"\t\t\tMinValueUpgrade = Upgrade_{family}{low:02d}\n"
            f"\t\t\tMaxValueUpgrade = Upgrade_{family}{high:02d}\n"
            f"\t\t\tDefaultValueUpgrade = Upgrade_{family}{default:02d}\n"
            f"\t\tEnd"
        )
    return "\tSubClass\n" + "\n".join(blocks) + "\n\tEnd"


#: Captain of Gondor's real authored numbers, which spend exactly 30 points:
#: 11 + 8 + 6 + 1 + 4.
CAPTAIN_STEPS = {
    "ArmorAttribute": (5, 20, 16),
    "DamageMultAttribute": (4, 17, 12),
    "HealthMultAttribute": (4, 15, 10),
    "AutoHealAttribute": (5, 18, 6),
    "VisionAttribute": (4, 14, 8),
}


def _documents(
    *,
    budget: int = 30,
    steps: dict[str, tuple[int, int, int]] | None = None,
    build_cost_expression: str = "CAH_BUILDCOST",
) -> dict[str, bytes]:
    sub = _sub_class("CaptainOfGondor", budget, steps or CAPTAIN_STEPS)
    system = (
        "CreateAHeroSystem\n"
        "\tWeaponGroupName = CreateAHero_Weapon\n"
        "\tCommandSetTemplate = CreateAHeroCommandSetTemplate\n"
        f"{_binders()}\n"
        '#include "CreateAHeroSystemMenOfTheWest.inc"\n'
        "End\n"
    )
    men_of_the_west = (
        "CreateAHeroClass\n"
        "\tNameTag = CreateAHero:ClassName_HeroesOfTheWest\n"
        "\tDescriptionTag = CreateAHero:ClassDesc_HeroesOfTheWest\n"
        "\tUpgradeName = Upgrade_CreateAHero_ClassHeroOfTheWest\n"
        "\tIconImage = Archetype_HerooftheWest\n"
        f"{sub}\n"
        "End\n"
    )
    return {
        "data/ini/createaherosystem.ini": system.encode("cp1252"),
        "data/ini/CreateAHeroSystemMenOfTheWest.inc": men_of_the_west.encode("cp1252"),
        "data/ini/createaheroupgrades.inc": _upgrades(),
        "data/ini/attributemodifier.ini": _modifiers(),
        "data/ini/createaherogamedata.inc": (
            b"#define CREATE_A_HERO_ATTRIBUTE_MULTIPLIER 1\n"
            b"#define CREATE_A_HERO_VISION_RANGE 150\n"
            b"#define SHROUD_CLEAR_CREATE_A_HERO 100\n"
            b"#define CREATE_A_HERO_BUILDCOST 2000\n"
            b"#define CREATE_A_HERO_COMMAND_POINT_COST 50\n"
        ),
        "data/ini/gamedata.ini": (
            b"#define CAH_BUILDCOST 500\n"
            b"#define CAH_BUILDTIME 30\n"
            b"#define FARAMIR_HEALTH 2000\n"
            b"#define GONDOR_FARAMIR_BOUNTY_VALUE 150\n"
        ),
        "data/ini/object/createahero/createaherodesign.inc": (
            f"\tSide = Men\n"
            f"\tBuildCost = {build_cost_expression}\n"
            f"\tBuildTime = CAH_BUILDTIME\n"
            f"\tVisionRange = CREATE_A_HERO_VISION_RANGE\n"
            f"\tShroudClearingRange = SHROUD_CLEAR_CREATE_A_HERO\n"
            f"\tCommandPoints = CREATE_A_HERO_COMMAND_POINT_COST\n"
            f"\tBountyValue = GONDOR_FARAMIR_BOUNTY_VALUE\n"
            f"\tDisplayName = OBJECT:CreateAHero\n"
            f"\tCommandSet = CreateAHeroCommandSet\n"
        ).encode("cp1252"),
        "data/ini/object/createahero/createaherorespawn.inc": (
            b"\tBody = RespawnBody ModuleTag_RespawnBody\n"
            b"\t\tMaxHealth = FARAMIR_HEALTH\n"
            b"\t\tDodgePercent = HERO_DODGE_PERCENT\n"
            b"\tEnd\n"
            b"\tBehavior = RespawnUpdate ModuleTag_RespawnUpdate\n"
            b"\t\tRespawnRules = AutoSpawn:No Cost:1500 Time:60000 Health:100%\n"
            b"\tEnd\n"
        ),
    }


class CahSystemCompilerTests(unittest.TestCase):
    def test_compiles_classes_groups_and_ladders(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        validate_cah_system_descriptor(descriptor)

        self.assertEqual(descriptor["schema"], SCHEMA)
        self.assertEqual(descriptor["schemaVersion"], SCHEMA_VERSION)

        groups = descriptor["attributeGroups"]
        # Five ATTRIBUTE groups; the APPEARANCE binder is deliberately absent.
        self.assertEqual(len(groups), 5)
        self.assertEqual([g["uiSlot"] for g in groups], [0, 1, 2, 3, 4])
        self.assertNotIn(
            "CreateAHero_Helmet", [str(g["groupName"]) for g in groups]
        )
        for group in groups:
            self.assertEqual(len(group["steps"]), ATTRIBUTE_STEP_COUNT)
            # The 0-based GroupOrder a `.cah` stores, against the 1-based step.
            self.assertEqual(group["steps"][0]["groupOrder"], 0)
            self.assertEqual(group["steps"][15]["step"], 16)

        vision = next(g for g in groups if g["groupName"].endswith("VisionAttribute"))
        self.assertEqual(
            [m["kind"] for m in vision["steps"][7]["modifiers"]],
            ["SHROUD_CLEARING", "VISION"],
        )

        classes = descriptor["classes"]
        self.assertEqual(len(classes), 1)
        self.assertEqual(classes[0]["classIndex"], 0)
        captain = classes[0]["subClasses"][0]
        self.assertEqual(captain["subClassIndex"], 0)
        self.assertEqual(captain["spendableAttributePoints"], 30)
        self.assertEqual(captain["defaultAttributeSpend"], 30)
        self.assertEqual(captain["usableFactions"], ["Men", "Elves", "Dwarves"])

    def test_base_stats_follow_the_authored_reference_not_a_guessed_define(self) -> None:
        # PURE RETAIL authors `BuildCost = CAH_BUILDCOST` (500) even though
        # CREATE_A_HERO_BUILDCOST (2000) is also defined and unused. Reaching for
        # the define whose name merely looks right would report 2000 for an
        # object that costs 500, so the compiler follows the reference and
        # records which expression it followed.
        descriptor = compile_cah_system_descriptor(_documents())
        system = descriptor["system"]
        self.assertEqual(system["buildCost"], 500)
        self.assertEqual(system["buildCostExpression"], "CAH_BUILDCOST")
        self.assertEqual(system["maxHealth"], 2000)
        self.assertEqual(system["maxHealthExpression"], "FARAMIR_HEALTH")
        self.assertEqual(system["visionRange"], 150)
        self.assertEqual(system["reviveCost"], 1500)

        # A corpus that rewrote the same line reports the other number, and says so.
        patched = compile_cah_system_descriptor(
            _documents(build_cost_expression="CREATE_A_HERO_BUILDCOST")
        )
        self.assertEqual(patched["system"]["buildCost"], 2000)
        self.assertEqual(
            patched["system"]["buildCostExpression"], "CREATE_A_HERO_BUILDCOST"
        )

    def test_attribute_spend_is_one_point_per_step_above_the_minimum(self) -> None:
        attributes = [
            {"groupName": "a", "minStep": 5, "defaultStep": 16},
            {"groupName": "b", "minStep": 4, "defaultStep": 12},
            {"groupName": "c", "minStep": 4, "defaultStep": 10},
            {"groupName": "d", "minStep": 5, "defaultStep": 6},
            {"groupName": "e", "minStep": 4, "defaultStep": 8},
        ]
        self.assertEqual(attribute_spend(attributes), 11 + 8 + 6 + 1 + 4)

    def test_rejects_a_subclass_whose_default_does_not_spend_its_budget(self) -> None:
        # The rule holds for all sixteen live retail subclasses. A corpus where
        # it does not is one this module does not understand, so it refuses
        # rather than emitting a budget nobody can trust.
        with self.assertRaisesRegex(CahSystemCompilerError, "spends 30 points"):
            compile_cah_system_descriptor(_documents(budget=29))

    def test_rejects_unordered_min_default_max(self) -> None:
        steps = dict(CAPTAIN_STEPS)
        steps["ArmorAttribute"] = (5, 20, 3)
        with self.assertRaisesRegex(CahSystemCompilerError, "not ordered"):
            compile_cah_system_descriptor(_documents(steps=steps))

    def test_rejects_a_missing_document_by_name(self) -> None:
        documents = _documents()
        del documents["data/ini/attributemodifier.ini"]
        with self.assertRaisesRegex(
            CahSystemCompilerError, "data/ini/attributemodifier.ini"
        ):
            compile_cah_system_descriptor(documents)

    def test_rejects_an_unresolvable_include(self) -> None:
        documents = _documents()
        del documents["data/ini/CreateAHeroSystemMenOfTheWest.inc"]
        with self.assertRaisesRegex(CahSystemCompilerError, "resolves to nothing"):
            compile_cah_system_descriptor(documents)

    def test_rejects_a_group_order_that_disagrees_with_the_upgrade_name(self) -> None:
        documents = _documents()
        # A `.cah` stores GroupOrder and everything downstream adds one. If the
        # two ever disagreed that conversion would be silently wrong.
        documents["data/ini/createaheroupgrades.inc"] = documents[
            "data/ini/createaheroupgrades.inc"
        ].replace(
            b"Upgrade Upgrade_ArmorAttribute16\n  Type = OBJECT\n"
            b"  GroupName = CreateAHero_ArmorAttribute\n  GroupOrder = 15",
            b"Upgrade Upgrade_ArmorAttribute16\n  Type = OBJECT\n"
            b"  GroupName = CreateAHero_ArmorAttribute\n  GroupOrder = 7",
        )
        with self.assertRaisesRegex(CahSystemCompilerError, "requires GroupOrder 15"):
            compile_cah_system_descriptor(documents)

    def test_runtime_is_a_separate_identity_over_the_same_table(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        validate_cah_system_runtime(runtime)
        self.assertEqual(runtime["schema"], RUNTIME_SCHEMA)
        self.assertEqual(runtime["schemaVersion"], RUNTIME_SCHEMA_VERSION)
        self.assertEqual(runtime["descriptorSha256"], descriptor["descriptorSha256"])
        self.assertEqual(
            runtime["registration"]["classes"], descriptor["classes"]
        )

    def test_digests_reject_a_tampered_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        descriptor["classes"][0]["subClasses"][0]["spendableAttributePoints"] = 99
        with self.assertRaisesRegex(CahSystemCompilerError, "digest is invalid"):
            validate_cah_system_descriptor(descriptor)

    def test_compilation_is_deterministic(self) -> None:
        first = compile_cah_system_descriptor(_documents())
        second = compile_cah_system_descriptor(_documents())
        self.assertEqual(first["descriptorSha256"], second["descriptorSha256"])


if __name__ == "__main__":
    unittest.main()
