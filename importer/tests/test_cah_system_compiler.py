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
    # The per-level grants the ExperienceLevel chain names. Authored as #define
    # references, exactly as retail does, so the test proves they are resolved
    # rather than passed through as names.
    for level in range(1, 4):
        lines.append(
            f"ModifierList HeroLevelUpDamage{level}\n"
            f"  Category = LEVEL\n"
            f"  Modifier = DAMAGE_ADD HERO_LVL{level + 1}_DAM_ADD\n"
            f"  Modifier = HEALTH HERO_LVL{level + 1}_HP_ADD\n"
            f"  Duration = 0\n"
            f"End"
        )
    # A list nothing in the Create-a-Hero ladder references, authored in a value
    # form this module has no reason to understand. Resolving the whole file
    # eagerly would make THIS line fail a Create-a-Hero compile.
    lines.append(
        "ModifierList StandardDebuff\n"
        "  Category = DEBUFF\n"
        "  Modifier = DAMAGE_MULT 80%\n"
        "End"
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
    # One APPEARANCE binder, compiled into appearanceGroups (not attributes).
    out.append(
        "\tCreateAHeroBlingBinder\n"
        "\t\tGroupName = CreateAHero_Helmet\n"
        "\t\tLabelTag = CAH:HelmetMenuLabel\n"
        "\t\tUISlot = 0\n"
        "\t\tBlingType = APPEARANCE\n"
        "\tEnd"
    )
    return "\n".join(out)


def _sub_class(
    name: str,
    budget: int,
    steps: dict[str, tuple[int, int, int]],
    *,
    sub_index: int = 0,
) -> str:
    blocks = [
        f"\t\tNameTag = CreateAHero:SubClassName_{name}",
        f"\t\tDescriptionTag = CreateAHero:SubClassDesc_{name}",
        f"\t\tIconImage = CP{name}",
        f"\t\tButtonImage = HICAH{name}",
        "\t\tUsableFactions = Men Elves Dwarves",
        f"\t\tSpendableAttributePoints = {budget}",
        f"\t\tUpgradeName = Upgrade_CreateAHero_SubClass_{sub_index}",
        # Retail frames every subclass individually so a Great Troll and a
        # Wanderer both fit the same viewport.
        "\t\tViewInfo\n"
        "\t\t\tFarPitch = -0.066\n"
        "\t\t\tFarDist = 20.500\n"
        "\t\t\tPortraitDist = 55.00\n"
        "\t\t\tMapLocation = 6\n"
        "\t\tEnd",
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


#: Two chains off one class, shaped exactly like retail's: a three-step
#: prerequisite ladder rising through the authored level columns, and a
#: standalone level-1 power with no successor.  ``None`` is retail's spelling
#: for "starts a chain", and the cost is a define name rather than a literal
#: because every real row is.
CAH_POWER_BUTTONS = (
    ("Command_CreateAHeroCallReinforcements", 1, "None", "CAH_REINFORCE1_COST"),
    (
        "Command_CreateAHeroImprovedCallReinforcements",
        3,
        "Command_CreateAHeroCallReinforcements",
        "CAH_REINFORCE2_COST",
    ),
    (
        "Command_CreateAHeroGreatCallReinforcements",
        7,
        "Command_CreateAHeroImprovedCallReinforcements",
        "CAH_REINFORCE3_COST",
    ),
    ("Command_CreateAHeroSpearThrow", 1, "None", "CAH_SPEARTHROW_COST"),
)


def _command_buttons() -> bytes:
    out = [
        # A button with no CreateAHeroUI* fields at all: the overwhelming
        # majority of commandbutton.ini, and none of it belongs on the POWERS
        # screen. Present so the test proves the compiler selects rather than
        # sweeps.
        "CommandButton Command_AttackMove\n"
        "\tCommand = ATTACK_MOVE\n"
        "\tTextLabel = CONTROLBAR:AttackMove\n"
        "End",
    ]
    for name, level, prerequisite, cost in CAH_POWER_BUTTONS:
        out.append(
            f"CommandButton {name}\n"
            f"\tCommand = SPECIAL_POWER\n"
            f"\tSpecialPower = SpecialAbility{name.split('Command_CreateAHero')[-1]}\n"
            f"\tOptions = NEED_TARGET_POS CONTEXTMODE_COMMAND\n"
            f"\tTextLabel = CONTROLBAR:{name}\n"
            f"\tDescriptLabel = CONTROLBAR:ToolTip{name}\n"
            f"\tButtonImage = HI{name}\n"
            f"\tCreateAHeroUIAllowableUpgrades = "
            f"Upgrade_CreateAHero_ClassHeroOfTheWest\n"
            f"\tCreateAHeroUIMinimumLevel = {level}\n"
            f"\tCreateAHeroUIPrerequisiteButtonName = {prerequisite}\n"
            f"\tCreateAHeroUICostIfSelected = {cost}\n"
            f"End"
        )
    return "\n".join(out).encode("cp1252")


#: The two model-condition flags Captain of Gondor claims: one for the
#: battlefield and one for the creation screen, which is the pattern every real
#: subclass follows.
def _model_conditions() -> bytes:
    return (
        "\tBehavior = ModelConditionUpgrade ModuleTag_HotW_SubClass_0\n"
        "\t\tTriggeredBy = Upgrade_CreateAHero_ClassHeroOfTheWest "
        "Upgrade_CreateAHero_SubClass_0\n"
        "\t\tConflictsWith = Upgrade_CreateAHeroMapMode\n"
        "\t\tRequiresAllTriggers = Yes\n"
        "\t\tRemoveConditionFlagsInRange = CREATE_A_HERO_00 CREATE_A_HERO_65\n"
        "\t\tAddConditionFlags = CREATE_A_HERO_00\n"
        "\tEnd\n"
        "\tBehavior = ModelConditionUpgrade ModuleTag_HotW_SubClass_0_MM\n"
        "\t\tTriggeredBy = Upgrade_CreateAHeroMapMode "
        "Upgrade_CreateAHero_ClassHeroOfTheWest Upgrade_CreateAHero_SubClass_0\n"
        "\t\tRequiresAllTriggers = Yes\n"
        "\t\tAddConditionFlags = CREATE_A_HERO_01\n"
        "\tEnd\n"
    ).encode("cp1252")


def _models() -> bytes:
    return (
        # The mounted state shares CREATE_A_HERO_00 with the on-foot one.
        "ModelConditionState = MOUNTED CREATE_A_HERO_00\n"
        "\tModel = CHHW_MW_M_SKN\n"
        "\tSkeleton = CHHW_MW_M_SKL\n"
        "\tModelAnimationPrefix = CHHW_MW\n"
        "\tPortraitImageName = CPCaptainofGondor\n"
        "\tButtonImageName = HICAHCaptainGondor\n"
        "\tWeaponLaunchBone = PRIMARY ARROW\n"
        "End\n"
        "ModelConditionState = CREATE_A_HERO_00\n"
        "\tModel = CHHW_CG_U_SKN\n"
        "\tSkeleton = CHHW_CG_U_SKL\n"
        "\tModelAnimationPrefix = CHHW_CG\n"
        "\tPortraitImageName = CPCaptainofGondor\n"
        "\tButtonImageName = HICAHCaptainGondor\n"
        "\tWeaponLaunchBone = PRIMARY SPEAR\n"
        "End\n"
        # A conditional restatement, as the Elf Archer authors for stealth. It
        # must not be mistaken for the base mesh.
        "ModelConditionState = CREATE_A_HERO_00 INVISIBLE_STEALTH\n"
        "\tModel = CHHW_CG_S_SKN\n"
        "\tSkeleton = CHHW_CG_U_SKL\n"
        "\tModelAnimationPrefix = CHHW_CG\n"
        "End\n"
        "ModelConditionState = CREATE_A_HERO_01\n"
        "\tModel = CHHW_CG_C_SKN\n"
        "\tSkeleton = CHHW_CG_C_SKL\n"
        "\tModelAnimationPrefix = CHHW_CG\n"
        "\tPortraitImageName = CPCaptainofGondor\n"
        "\tButtonImageName = HICAHCaptainGondor\n"
        "End\n"
    ).encode("cp1252")


def _experience_levels() -> bytes:
    out = []
    for rank in range(1, 5):
        required = "1" if rank == 1 else f"CREATE_A_HERO_LVL{rank}_EXP_NEEDED"
        modifiers = (
            "" if rank == 1 else f"\tAttributeModifiers = HeroLevelUpDamage{rank - 1}\n"
        )
        out.append(
            f"ExperienceLevel CreateAHeroLevel{rank}\n"
            f"\tTargetNames = CreateAHero\n"
            f"\tRequiredExperience = {required}\n"
            f"\tExperienceAward = CREATE_A_HERO_LVL{rank}_EXP_AWARD\n"
            f"\tRank = {rank}\n"
            f"{modifiers}"
            f"\tSelectionDecal\n"
            f"\t\tTexture = decal_hero_good\n"
            f"\t\tStyle = SHADOW_ALPHA_DECAL\n"
            f"\tEnd\n"
            f"End"
        )
    return "\n".join(out).encode("cp1252")


def _documents(
    *,
    budget: int = 30,
    steps: dict[str, tuple[int, int, int]] | None = None,
    build_cost_expression: str = "CAH_BUILDCOST",
    command_buttons: bytes | None = None,
    model_conditions: bytes | None = None,
    models: bytes | None = None,
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
            b"#define CAH_REINFORCE1_COST 300\n"
            b"#define CAH_REINFORCE2_COST 200\n"
            b"#define CAH_REINFORCE3_COST 400\n"
            b"#define CAH_SPEARTHROW_COST 150\n"
            b"#define CREATE_A_HERO_LVL1_EXP_AWARD 100\n"
            b"#define CREATE_A_HERO_LVL2_EXP_NEEDED 125\n"
            b"#define CREATE_A_HERO_LVL2_EXP_AWARD 110\n"
            b"#define CREATE_A_HERO_LVL3_EXP_NEEDED 250\n"
            b"#define CREATE_A_HERO_LVL3_EXP_AWARD 120\n"
            b"#define CREATE_A_HERO_LVL4_EXP_NEEDED 375\n"
            b"#define CREATE_A_HERO_LVL4_EXP_AWARD 130\n"
            b"#define HERO_LVL2_DAM_ADD 10\n"
            b"#define HERO_LVL2_HP_ADD 60\n"
            b"#define HERO_LVL3_DAM_ADD 10\n"
            b"#define HERO_LVL3_HP_ADD 60\n"
            b"#define HERO_LVL4_DAM_ADD 12\n"
            b"#define HERO_LVL4_HP_ADD 80\n"
        ),
        "data/ini/commandbutton.ini": (
            _command_buttons() if command_buttons is None else command_buttons
        ),
        "data/ini/object/createahero/createaheromodels.inc": (
            _models() if models is None else models
        ),
        "data/ini/object/createahero/createaheromodelconditionupgrades.inc": (
            _model_conditions() if model_conditions is None else model_conditions
        ),
        "data/ini/experiencelevels_createahero.inc": _experience_levels(),
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
        # Five ATTRIBUTE groups; the APPEARANCE binder is compiled separately.
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


class CahPowerScreenTests(unittest.TestCase):
    """The CUSTOMIZE HERO POWERS screen, which is authored on CommandButtons."""

    def _catalog(self, **kwargs: object) -> list[dict]:
        descriptor = compile_cah_system_descriptor(_documents(**kwargs))  # type: ignore[arg-type]
        return descriptor["powerCatalog"]

    def test_only_buttons_carrying_the_cah_ui_fields_are_powers(self) -> None:
        # commandbutton.ini is overwhelmingly NOT Create-a-Hero content. What
        # makes a button a power is CreateAHeroUIMinimumLevel and nothing else,
        # so a plain ATTACK_MOVE button must not reach the screen.
        selected = {
            row["powerId"] for tree in self._catalog() for row in tree["levels"]
        }
        self.assertEqual(selected, {name for name, _, _, _ in CAH_POWER_BUTTONS})
        self.assertNotIn("Command_AttackMove", selected)

    def test_a_tree_is_the_recovered_prerequisite_chain(self) -> None:
        # Retail names no power families; it only links each power to its
        # predecessor. A grid ROW is that chain, and recovering it is the only
        # way "Call Reinforcements -> Improved -> Great" becomes one row.
        catalog = self._catalog()
        self.assertEqual(len(catalog), 2)
        by_root = {tree["rootPowerId"]: tree for tree in catalog}

        reinforcements = by_root["Command_CreateAHeroCallReinforcements"]
        self.assertEqual(
            [row["powerId"] for row in reinforcements["levels"]],
            [
                "Command_CreateAHeroCallReinforcements",
                "Command_CreateAHeroImprovedCallReinforcements",
                "Command_CreateAHeroGreatCallReinforcements",
            ],
        )
        # Ordered by the required hero level, which is what puts each power in
        # its grid column.
        self.assertEqual(
            [row["requiredHeroLevel"] for row in reinforcements["levels"]], [1, 3, 7]
        )
        self.assertEqual([row["tier"] for row in reinforcements["levels"]], [1, 2, 3])
        # The row is labelled by its first power, as the screen shows it.
        self.assertEqual(
            reinforcements["labelStringId"],
            "CONTROLBAR:Command_CreateAHeroCallReinforcements",
        )

        # A power with no successor is still a tree, of one.
        self.assertEqual(len(by_root["Command_CreateAHeroSpearThrow"]["levels"]), 1)

    def test_none_is_the_spelling_for_no_prerequisite(self) -> None:
        roots = [
            row
            for tree in self._catalog()
            for row in tree["levels"]
            if not row["prerequisitePowerId"]
        ]
        self.assertEqual(
            sorted(row["powerId"] for row in roots),
            [
                "Command_CreateAHeroCallReinforcements",
                "Command_CreateAHeroSpearThrow",
            ],
        )

    def test_costs_resolve_through_the_defines_not_as_literals(self) -> None:
        # CreateAHeroUICostIfSelected is authored as a #define name in every
        # real row. The screen's Build Cost is the base object cost plus the
        # sum of these, so a cost left unresolved would price every hero wrong.
        by_id = {
            row["powerId"]: row
            for tree in self._catalog()
            for row in tree["levels"]
        }
        self.assertEqual(
            by_id["Command_CreateAHeroCallReinforcements"]["costIfSelected"], 300
        )
        self.assertEqual(
            by_id["Command_CreateAHeroCallReinforcements"]["costExpression"],
            "CAH_REINFORCE1_COST",
        )
        self.assertEqual(by_id["Command_CreateAHeroSpearThrow"]["costIfSelected"], 150)

    def test_class_binding_is_checked_against_the_declared_class_upgrades(self) -> None:
        by_id = {
            row["powerId"]: row
            for tree in self._catalog()
            for row in tree["levels"]
        }
        self.assertEqual(
            by_id["Command_CreateAHeroSpearThrow"]["allowedClassUpgrades"],
            ["Upgrade_CreateAHero_ClassHeroOfTheWest"],
        )

    def test_rejects_a_power_bound_to_a_class_that_does_not_exist(self) -> None:
        buttons = _command_buttons().replace(
            b"Upgrade_CreateAHero_ClassHeroOfTheWest",
            b"Upgrade_CreateAHero_ClassNeverDeclared",
            1,
        )
        with self.assertRaisesRegex(CahSystemCompilerError, "which no CreateAHeroClass"):
            compile_cah_system_descriptor(_documents(command_buttons=buttons))

    def test_rejects_a_prerequisite_that_names_no_power_button(self) -> None:
        buttons = _command_buttons().replace(
            b"CreateAHeroUIPrerequisiteButtonName = "
            b"Command_CreateAHeroCallReinforcements",
            b"CreateAHeroUIPrerequisiteButtonName = Command_ThisIsNotAPower",
        )
        with self.assertRaisesRegex(
            CahSystemCompilerError, "which is not a Create-a-Hero power button"
        ):
            compile_cah_system_descriptor(_documents(command_buttons=buttons))

    def test_rejects_a_power_no_class_can_select(self) -> None:
        buttons = _command_buttons().replace(
            b"\tCreateAHeroUIAllowableUpgrades = "
            b"Upgrade_CreateAHero_ClassHeroOfTheWest\n",
            b"",
            1,
        )
        with self.assertRaisesRegex(
            CahSystemCompilerError, "no class could ever select it"
        ):
            compile_cah_system_descriptor(_documents(command_buttons=buttons))

    def test_rejects_a_cost_that_resolves_to_nothing(self) -> None:
        buttons = _command_buttons().replace(
            b"CAH_SPEARTHROW_COST", b"CAH_A_DEFINE_NOBODY_WROTE"
        )
        with self.assertRaisesRegex(CahSystemCompilerError, "neither a number nor"):
            compile_cah_system_descriptor(_documents(command_buttons=buttons))

    def test_the_catalog_reaches_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        validate_cah_system_runtime(runtime)
        self.assertEqual(
            runtime["registration"]["powerCatalog"], descriptor["powerCatalog"]
        )


class CahModelBindingTests(unittest.TestCase):
    """The 3D binding, which is a join across two documents and not a lookup."""

    def _captain(self) -> dict:
        descriptor = compile_cah_system_descriptor(_documents())
        return descriptor["classes"][0]["subClasses"][0]

    def test_the_flag_is_recovered_from_the_trigger_pair(self) -> None:
        # createaheromodels.inc keys art by CREATE_A_HERO_NN, an ordinal that
        # appears nowhere in the system file. Only the ModelConditionUpgrade
        # trigger pair says which class/subclass raises which flag.
        models = self._captain()["models"]
        self.assertEqual(models["battlefield"]["conditionFlag"], "CREATE_A_HERO_00")
        self.assertEqual(models["creationScreen"]["conditionFlag"], "CREATE_A_HERO_01")

    def test_the_battlefield_and_creation_screen_meshes_differ(self) -> None:
        # A hero poses in a different mesh in the Create-a-Hero screens than the
        # one it fights in. Showing the battlefield mesh in the menu (or vice
        # versa) is exactly the bug this split exists to prevent.
        models = self._captain()["models"]
        self.assertEqual(models["battlefield"]["model"], "CHHW_CG_U_SKN")
        self.assertEqual(models["creationScreen"]["model"], "CHHW_CG_C_SKN")

    def test_the_base_mesh_is_hoisted_and_the_rig_travels_with_it(self) -> None:
        battlefield = self._captain()["models"]["battlefield"]
        self.assertEqual(battlefield["skeleton"], "CHHW_CG_U_SKL")
        self.assertEqual(battlefield["animationPrefix"], "CHHW_CG")
        self.assertEqual(battlefield["portraitImageId"], "CPCaptainofGondor")
        self.assertEqual(battlefield["weaponLaunchBones"], ["PRIMARY SPEAR"])

    def test_mounted_shares_the_flag_without_replacing_the_base(self) -> None:
        battlefield = self._captain()["models"]["battlefield"]
        self.assertEqual(battlefield["mounted"]["model"], "CHHW_MW_M_SKN")
        # The on-foot mesh is still the one hoisted to the top.
        self.assertEqual(battlefield["model"], "CHHW_CG_U_SKN")

    def test_a_conditional_state_never_becomes_the_base_mesh(self) -> None:
        # The Elf Archer authors `CREATE_A_HERO_NN INVISIBLE_STEALTH`. Letting
        # it win would dress every one of them in its stealth state.
        battlefield = self._captain()["models"]["battlefield"]
        self.assertEqual(
            [row["model"] for row in battlefield["conditionalStates"]],
            ["CHHW_CG_S_SKN"],
        )
        self.assertEqual(battlefield["model"], "CHHW_CG_U_SKN")

    def test_rejects_a_subclass_with_no_mesh(self) -> None:
        conditions = _model_conditions().replace(
            b"Upgrade_CreateAHero_SubClass_0\n"
            b"\t\tConflictsWith = Upgrade_CreateAHeroMapMode",
            b"Upgrade_CreateAHero_SubClass_9\n"
            b"\t\tConflictsWith = Upgrade_CreateAHeroMapMode",
        )
        with self.assertRaisesRegex(
            CahSystemCompilerError, "has a creation-screen model but no battlefield one"
        ):
            compile_cah_system_descriptor(_documents(model_conditions=conditions))

    def test_rejects_a_flag_raised_with_no_art_behind_it(self) -> None:
        models = _models().replace(
            b"ModelConditionState = CREATE_A_HERO_00\n", b"ModelConditionState = ZZZ\n"
        )
        with self.assertRaisesRegex(CahSystemCompilerError, "has no base mesh to stand in"):
            compile_cah_system_descriptor(_documents(models=models))

    def test_view_info_is_compiled_as_numbers(self) -> None:
        # Framing is per subclass so a Great Troll and a Wanderer both fit the
        # viewport; a screen that ignored it would crop the tall ones.
        view = self._captain()["viewInfo"]
        self.assertAlmostEqual(view["farPitch"], -0.066)
        self.assertAlmostEqual(view["farDist"], 20.5)
        self.assertEqual(view["mapLocation"], 6)


class CahExperienceLadderTests(unittest.TestCase):
    """The level chain a created hero climbs, shared across every class."""

    def _ladder(self) -> dict:
        return compile_cah_system_descriptor(_documents())["experience"]

    def test_ranks_are_a_contiguous_chain(self) -> None:
        ladder = self._ladder()
        self.assertEqual(ladder["maxLevel"], 4)
        self.assertEqual([row["rank"] for row in ladder["levels"]], [1, 2, 3, 4])

    def test_thresholds_resolve_through_the_defines(self) -> None:
        # RequiredExperience is a #define name in every level past the first.
        # The name is not the number, and a hero that never levels is the bug.
        levels = self._ladder()["levels"]
        self.assertEqual([row["requiredExperience"] for row in levels], [1, 125, 250, 375])
        self.assertEqual(
            levels[1]["requiredExperienceExpression"], "CREATE_A_HERO_LVL2_EXP_NEEDED"
        )
        self.assertEqual([row["experienceAward"] for row in levels], [100, 110, 120, 130])

    def test_per_level_effects_are_resolved_not_merely_named(self) -> None:
        # The runtime's ExperienceLevel contract wants {kind, value} rows. A
        # bare "HeroLevelUpDamage3" would be rejected by it, and a rejected
        # ladder is a hero that never levels at all.
        levels = self._ladder()["levels"]
        self.assertEqual(levels[0]["attributeModifiers"], [])
        granted = levels[3]["attributeModifiers"]
        self.assertEqual([row["name"] for row in granted], ["HeroLevelUpDamage3"])
        self.assertEqual(
            granted[0]["modifiers"],
            [{"kind": "DAMAGE_ADD", "value": 12}, {"kind": "HEALTH", "value": 80}],
        )

    def test_a_kind_the_runtime_cannot_apply_is_evidence_not_a_modifier(self) -> None:
        # An unknown kind makes the consumer reject the WHOLE ladder, so it is
        # carried as evidence instead of emitted as a modifier.
        documents = _documents()
        documents["data/ini/attributemodifier.ini"] += (
            b"\nModifierList HeroLevelUpDamage3\n"
            b"  Category = LEVEL\n"
            b"  Modifier = DAMAGE_ADD 12\n"
            b"  Modifier = RESIST_KNOCKBACK 100\n"
            b"End\n"
        )
        granted = compile_cah_system_descriptor(documents)["experience"]["levels"][3][
            "attributeModifiers"
        ][0]
        self.assertEqual(granted["modifiers"], [{"kind": "DAMAGE_ADD", "value": 12}])
        self.assertEqual(granted["unsupportedModifiers"], ["RESIST_KNOCKBACK 100"])

    def test_rejects_a_level_naming_a_modifier_list_that_does_not_exist(self) -> None:
        documents = _documents()
        documents["data/ini/experiencelevels_createahero.inc"] = documents[
            "data/ini/experiencelevels_createahero.inc"
        ].replace(b"HeroLevelUpDamage3", b"HeroLevelUpNobodyWrote")
        with self.assertRaisesRegex(CahSystemCompilerError, "the level would grant nothing"):
            compile_cah_system_descriptor(documents)

    def test_rejects_a_gap_that_would_strand_a_level(self) -> None:
        documents = _documents()
        documents["data/ini/experiencelevels_createahero.inc"] = documents[
            "data/ini/experiencelevels_createahero.inc"
        ].replace(b"\tRank = 3\n", b"\tRank = 5\n")
        with self.assertRaisesRegex(CahSystemCompilerError, "not a contiguous"):
            compile_cah_system_descriptor(documents)

    def test_the_ladder_reaches_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        self.assertEqual(
            runtime["registration"]["experience"], descriptor["experience"]
        )


if __name__ == "__main__":
    unittest.main()
