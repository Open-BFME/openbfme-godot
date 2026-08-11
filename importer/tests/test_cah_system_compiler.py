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
    # Two APPEARANCE binders, compiled into appearanceGroups (not attributes).
    out.append(
        "\tCreateAHeroBlingBinder\n"
        "\t\tGroupName = CreateAHero_Helmet\n"
        "\t\tLabelTag = CAH:HelmetMenuLabel\n"
        "\t\tUISlot = 0\n"
        "\t\tBlingType = APPEARANCE\n"
        "\tEnd"
    )
    out.append(
        "\tCreateAHeroBlingBinder\n"
        "\t\tGroupName = CreateAHero_Weapon\n"
        "\t\tLabelTag = CAH:WeaponMenuLabel\n"
        "\t\tUISlot = 1\n"
        "\t\tBlingType = APPEARANCE\n"
        "\tEnd"
    )
    out.append(_blings())
    return "\n".join(out)


#: The appearance options the fixture offers, shaped exactly like retail's:
#: two helmets in one group and two weapons in another.
APPEARANCE_BLINGS = (
    ("CreateAHero_Helmet", "Upgrade_CaptainOfGondor_CHH01"),
    ("CreateAHero_Helmet", "Upgrade_CaptainOfGondor_CHH02"),
    ("CreateAHero_Weapon", "Upgrade_CHW01"),
    ("CreateAHero_Weapon", "Upgrade_CHW02"),
)


def _blings() -> str:
    out = []
    for group, upgrade in APPEARANCE_BLINGS:
        out.append(
            f"\tCreateAHeroBling\n"
            f"\t\tNameTag = CreateAHero:BlingName_{upgrade}\n"
            f"\t\tDescriptionTag = CreateAHero:BlingDesc_{upgrade}\n"
            f"\t\tGroupName = {group}\n"
            f"\t\tBlingUpgradeName = {upgrade}\n"
            f"\tEnd"
        )
    return "\n".join(out)


def _garment() -> bytes:
    """Retail's shape: WeaponSets, WeaponSetUpgrades and SubObjectsUpgrades.

    ``Upgrade_CHW02`` carries TWO SubObjectsUpgrade modules (retail's
    Belthronding reveals the bow AND the archer's sword) and a toggle WeaponSet,
    because merging and alternate modes are the two behaviours a single-module
    fixture would never exercise.
    """

    return (
        "WeaponSet\n"
        "\tConditions = None\n"
        "End\n"
        "WeaponSet\n"
        "\tConditions = WEAPONSET_CREATE_A_HERO_WS_01\n"
        "\tWeapon = PRIMARY CreateAHeroBasicMeleeWeapon\n"
        "End\n"
        "Behavior = WeaponSetUpgrade Create_A_Hero_Weapon1\n"
        "\tTriggeredBy = Upgrade_CHW01\n"
        "\tWeaponCondition = WEAPONSET_CREATE_A_HERO_WS_01\n"
        "End\n"
        "Behavior = SubObjectsUpgrade Dwarf_Upgrade\n"
        "\tTriggeredBy = Upgrade_CHW01\n"
        "\tShowSubObjects = AXE_01\n"
        "\tHideSubObjectsOnRemove = Yes\n"
        "\tFadeTimeInSeconds = 0.0\n"
        "End\n"
        "WeaponSet\n"
        "\tConditions = WEAPONSET_CREATE_A_HERO_WS_02 WEAPONSET_TOGGLE_1\n"
        "\tWeapon = PRIMARY CreateAHeroBasicRangedWeapon\n"
        "End\n"
        "WeaponSet\n"
        "\tConditions = WEAPONSET_CREATE_A_HERO_WS_02\n"
        "\tWeapon = PRIMARY CreateAHeroBasicMeleeWeapon\n"
        "End\n"
        "Behavior = WeaponSetUpgrade Create_A_Hero_Weapon2\n"
        "\tTriggeredBy = Upgrade_CHW02\n"
        "\tWeaponCondition = WEAPONSET_CREATE_A_HERO_WS_02\n"
        "End\n"
        "Behavior = SubObjectsUpgrade Belthronding_Upgrade\n"
        "\tTriggeredBy = Upgrade_CHW02\n"
        "\tShowSubObjects = Belthronding\n"
        "\tHideSubObjectsOnRemove = Yes\n"
        "End\n"
        "Behavior = SubObjectsUpgrade Belthronding_ArcherSword_Upgrade\n"
        "\tTriggeredBy = Upgrade_CHW02\n"
        "\tShowSubObjects = WestronSword\n"
        "\tHideSubObjectsOnRemove = Yes\n"
        "End\n"
        "Behavior = SubObjectsUpgrade CaptainOfGondorHelmet_Upgrade01\n"
        "\tTriggeredBy = Upgrade_CaptainOfGondor_CHH01\n"
        "\tShowSubObjects = HAIR_00\n"
        "\tHideSubObjectsOnRemove = Yes\n"
        "End\n"
        "Behavior = SubObjectsUpgrade CaptainOfGondorHelmet_Upgrade02\n"
        "\tTriggeredBy = Upgrade_CaptainOfGondor_CHH02\n"
        "\tShowSubObjects = HLMT_01\n"
        "\tUpgradeTexture = CHCM_CM_01.tga 0 CHCM_CM.tga\n"
        "\tRecolorHouse = Yes\n"
        "\tHideSubObjectsOnRemove = Yes\n"
        "End\n"
    ).encode("cp1252")


def _weapons() -> bytes:
    return (
        "Weapon CreateAHeroBasicMeleeWeapon\n"
        "\tAttackRange = STANDARD_MELEE_ATTACK_RANGE\n"
        "\tMeleeWeapon = Yes\n"
        "\tDelayBetweenShots = FARAMIR_DELAYBETWEENSHOTS\n"
        "\tPreAttackDelay = FARAMIR_PREATTACKDELAY\n"
        "\tFiringDuration = FARAMIR_FIRINGDURATION\n"
        "\tDamageNugget\n"
        "\t\tDamage = CREATE_A_HERO_DAMAGE\n"
        "\t\tDamageType = HERO\n"
        "\tEnd\n"
        "\tDOTNugget\n"
        "\t\tDamage = DEFAULT_POISON_DAMAGE\n"
        "\t\tDamageType = POISON\n"
        "\tEnd\n"
        "End\n"
        "Weapon CreateAHeroBasicRangedWeapon\n"
        "\tAttackRange = 350\n"
        "\tDelayBetweenShots = 0\n"
        "\tPreAttackDelay = 1170\n"
        "\tFiringDuration = 0\n"
        "\tDamageNugget\n"
        "\t\tDamage = CREATE_A_HERO_DAMAGE\n"
        "\t\tDamageType = FLAME\n"
        "\tEnd\n"
        "End\n"
        # A weapon no Create-a-Hero set names: proof the reader selects rather
        # than sweeping the whole corpus into the descriptor.
        "Weapon SomeOtherUnitWeapon\n"
        "\tAttackRange = 40\n"
        "\tDamageNugget\n"
        "\t\tDamage = 12\n"
        "\tEnd\n"
        "End\n"
    ).encode("cp1252")


def _object() -> bytes:
    """The CreateAHero Object, complete with the include that cannot be read.

    Retail's ``createaheroanims.inc`` carries a stray ``End``; the fixture keeps
    an ``#include`` in the object file so the test proves the locomotor scan
    never tries to splice it.
    """

    return (
        "Object CreateAHero\n"
        '\t#include "CreateAHeroAnims.inc"\n'
        "\tLocomotorSet\n"
        "\t\tLocomotor = HeroHumanScalingLocomotor\n"
        "\t\tCondition = SET_NORMAL_UPGRADED\n"
        "\t\tSpeed = 50\n"
        "\tEnd\n"
        "\tLocomotorSet\n"
        "\t\tLocomotor = HeroHumanLocomotor\n"
        "\t\tCondition = SET_NORMAL\n"
        "\t\tSpeed = 50\n"
        "\tEnd\n"
        "End\n"
    ).encode("cp1252")


def _audio() -> bytes:
    return (
        "SoundUpgrade = Upgrade_CreateAHero_ClassHeroOfTheWest Upgrade_CreateAHero_SubClass_0\n"
        "\tVoiceAttack = HeroWestMaleVoiceAttack\n"
        "\tVoiceAttackStructure = HeroWestMaleVoiceAttackBuilding\n"
        "\tVoiceCreated = HeroWestMaleVoiceSalute\n"
        "\tVoiceFear = HeroWestMaleVoiceHelpMe\n"
        "\tVoiceGuard = HeroWestMaleVoiceMove\n"
        "\tVoiceMove = HeroWestMaleVoiceMove\n"
        "\tVoiceSelect = HeroWestMaleVoiceSelectMS\n"
        "\tVoiceSelectBattle = HeroWestMaleVoiceSelectBattle\n"
        "End\n"
    ).encode("cp1252")


def _award_system() -> bytes:
    return (
        "AwardSystem\n"
        "ObjectAward\n"
        "AwardName = Vanquisher\nImageName = CahAward_Vanquisher\n"
        "NameTag = Award:Vanquisher_Name\nDescriptionTag = Award:Vanquisher_Desc\n"
        "Trigger\nStat = ENEMIES_KILLED\nThreshold = 3000\nEnd\nEnd\n"
        "ThingStat\nStatName = ENEMIES_KILLED\nNameTag = Stat:ENEMIES_KILLED_Name\n"
        "DescriptionTag = Stat:ENEMIES_KILLED_Desc\nEnd\n"
        "End\n"
    ).encode("cp1252")


def _locomotors() -> bytes:
    return (
        "Locomotor HeroHumanLocomotor\n"
        "\tSurfaces = GROUND RUBBLE\n"
        "\tTurnTime = 500\n"
        "\tTurnTimeDamaged = 500\n"
        "\tFastTurnRadius = 9\n"
        "\tSlowTurnRadius = 1\n"
        "\tAcceleration = 210\n"
        "\tBraking = 210\n"
        "End\n"
        "Locomotor HeroHumanScalingLocomotor\n"
        "\tTurnTime = 500\n"
        "\tAcceleration = 510\n"
        "\tBraking = 510\n"
        "End\n"
    ).encode("cp1252")


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
        # Retail spells a subclass's offered blings as whitespace-separated
        # upgrade names, one of which may carry the `@` default marker.  Shaped
        # exactly like retail's own line: the marked option is not first.
        "\t\tBlingUpgrades = Upgrade_CaptainOfGondor_CHH01 "
        "@Upgrade_CaptainOfGondor_CHH02",
        "\t\tBlingUpgrades = Upgrade_CHW01 Upgrade_CHW02",
        f"\t\tSpendableAttributePoints = {budget}",
        f"\t\tUpgradeName = Upgrade_CreateAHero_SubClass_{sub_index}",
        "\t\tDefaultPrimaryColor = R:150 G:151\tB:152",
        "\t\tDefaultSecondaryColor = R:10 G:20 B:30",
        "\t\tDefaultTertiaryColor = R:255 G:0 B:128",
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



def _anims() -> bytes:
    """Retail's creation-screen animation states.

    Carries the stray ``End`` retail ships, because tolerating it is exactly
    why this document is scanned rather than parsed, and a battlefield state
    that must not contribute to the screen's idle vocabulary.
    """

    lines = [
        "//================== ANIMATIONS ====================",
        "AnimationState = CREATE_A_HERO_IN_CREATION_SCREEN "
        "CREATE_A_HERO_EXAMINE_SELF CREATE_A_HERO_SELECTED_CHEER",
        "	StateName = STATE_ExamineSelf",
        "	Animation = ExamineSelf",
        "		AnimationName = #(MODEL)_C_CLRA",
        "		AnimationMode = ONCE",
        "	End",
        "End",
        "AnimationState = CREATE_A_HERO_IN_CREATION_SCREEN "
        "CREATE_A_HERO_EXAMINE_WEAPON_RIGHT CREATE_A_HERO_SELECTED_CHEER",
        "	StateName = STATE_ExamineWeapon",
        "	Animation = WeaponSwap",
        "		AnimationName = #(MODEL)_C_WPNA",
        "	End",
        "End",
        "AnimationState = CREATE_A_HERO_IN_CREATION_SCREEN "
        "CREATE_A_HERO_SELECTED_CHEER",
        "	StateName = STATE_SelectedCheer",
        "	Animation = Foot_ATNB",
        "		AnimationName = #(MODEL)_C_ATNB #(MODEL)_C_ATND",
        "	End",
        "	Animation = Foot_ATNE",
        "		AnimationName = #(MODEL)_C_ATNE #(MODEL)_C_ATND",
        "	End",
        "End",
        "TransitionState = Trans_SelectedCheer",
        "	Animation = Transition",
        "		AnimationName = #(MODEL)_C_SLCA",
        "	End",
        "End",
        "// A battlefield state, which the screen never plays.",
        "AnimationState = MOVING",
        "	Animation = Run",
        "		AnimationName = #(MODEL)_U_RUNA",
        "	End",
        "End",
    ]
    return "\n".join(lines).encode("cp1252")


def _documents(
    *,
    budget: int = 30,
    steps: dict[str, tuple[int, int, int]] | None = None,
    build_cost_expression: str = "CAH_BUILDCOST",
    command_buttons: bytes | None = None,
    model_conditions: bytes | None = None,
    models: bytes | None = None,
    garment: bytes | None = None,
    weapons: bytes | None = None,
    obj: bytes | None = None,
    locomotors: bytes | None = None,
    anims: bytes | None = None,
) -> dict[str, bytes]:
    sub = _sub_class("CaptainOfGondor", budget, steps or CAPTAIN_STEPS)
    system = (
        "CreateAHeroSystem\n"
        "\tWeaponGroupName = CreateAHero_Weapon\n"
        # The creation screen's three special idles and how often one plays.
        # `Anin` is retail's own typo, kept verbatim.
        "\tSelectedCheerAninName = _C_SLCA\n"
        "\tExamineWeaponAninName = _C_WPNA\n"
        "\tExamineSelfAninName = _C_CLRA\n"
        "\tSpecialAnimPercentChance = 20.0\n"
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
            b"#define CREATE_A_HERO_DAMAGE 150\n"
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
            b"#define STANDARD_MELEE_ATTACK_RANGE 11.5\n"
            b"#define FARAMIR_DELAYBETWEENSHOTS 1400\n"
            b"#define FARAMIR_PREATTACKDELAY 800\n"
            b"#define FARAMIR_FIRINGDURATION 1200\n"
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
        "data/ini/object/createahero/createaheroweaponupgrades.inc": (
            _garment() if garment is None else garment
        ),
        "data/ini/object/createahero/createahero.ini": (
            _object() if obj is None else obj
        ),
        "data/ini/object/createahero/createaheroaudio.inc": _audio(),
        "data/ini/awardsystem.ini": _award_system(),
        "data/ini/weapon.ini": _weapons() if weapons is None else weapons,
        "data/ini/locomotor.ini": (
            _locomotors() if locomotors is None else locomotors
        ),
        "data/ini/object/createahero/createaheroanims.inc": (
            _anims() if anims is None else anims
        ),
    }


class CahSystemCompilerTests(unittest.TestCase):
    def test_award_and_stat_definitions_are_compiled(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        self.assertEqual(descriptor["awardDefinitions"][0]["awardId"], "Vanquisher")
        self.assertEqual(descriptor["awardDefinitions"][0]["imageId"], "CahAward_Vanquisher")
        self.assertEqual(descriptor["awardDefinitions"][0]["triggers"], [{"statIds": ["ENEMIES_KILLED"], "threshold": 3000}])
        self.assertEqual(descriptor["trackingStatDefinitions"][0]["statId"], "ENEMIES_KILLED")

    def test_subclass_fixed_voice_routes_are_compiled(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        voice = descriptor["classes"][0]["subClasses"][0]["voice"]
        self.assertEqual(voice["select"], ["HeroWestMaleVoiceSelectMS", "HeroWestMaleVoiceSelectBattle"])
        self.assertEqual(voice["move"], ["HeroWestMaleVoiceMove"])
        self.assertEqual(voice["attack"], ["HeroWestMaleVoiceAttack"])
        self.assertEqual(voice["attackStructure"], ["HeroWestMaleVoiceAttackBuilding"])
        self.assertEqual(voice["created"], ["HeroWestMaleVoiceSalute"])

    def test_voice_projection_limitations_are_named(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        voice_limitation = next(
            value
            for value in descriptor["limitations"]
            if "audioBindings" in value
        )
        self.assertIn("three conditional bow/mounted SoundUpgrade variants", voice_limitation)
        for retail_key in (
            "VoiceAttackCharge",
            "VoiceAttackAir",
            "VoiceAttackMachine",
            "VoiceMoveToCamp",
            "VoiceMoveWhileAttacking",
            "VoiceRetreatToCastle",
            "VoiceGarrison",
            "VoiceEnterUnit*",
            "VoiceInitiateCaptureBuilding",
            "VoicePriority",
            "SoundImpact",
        ):
            self.assertIn(retail_key, voice_limitation)
        self.assertIn("CAH heroes remain silent", voice_limitation)

    def test_subclass_default_colors_are_typed_rgb_triples(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertEqual(sub["defaultColors"], [[150, 151, 152], [10, 20, 30], [255, 0, 128]])

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


def _option(descriptor: dict, upgrade: str) -> dict:
    for row in descriptor["appearanceOptions"]:
        if row["upgradeName"] == upgrade:
            return row
    raise AssertionError(f"no appearance option {upgrade}")


class CahGarmentSubObjectTests(unittest.TestCase):
    """The mesh side of an appearance choice.

    The published gap this closes: system.json listed appearance options by
    upgrade name only, so a client could show the menu but not the hero -- every
    garment variant in the mesh stayed visible at once.
    """

    def test_an_option_carries_the_sub_objects_its_upgrade_reveals(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        helmet = _option(descriptor, "Upgrade_CaptainOfGondor_CHH02")
        self.assertEqual(helmet["subObjects"]["show"], ["HLMT_01"])
        self.assertEqual(helmet["subObjects"]["hide"], [])

    def test_sub_object_names_keep_the_authored_spelling(self) -> None:
        # They are matched against W3D sub-object names, which the conversion
        # preserves verbatim as GLB node names; casefolding them here would
        # silently stop every toggle from resolving.
        descriptor = compile_cah_system_descriptor(_documents())
        self.assertEqual(
            _option(descriptor, "Upgrade_CHW02")["subObjects"]["show"],
            ["Belthronding", "WestronSword"],
        )

    def test_two_modules_on_one_upgrade_merge_rather_than_overwrite(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        bow = _option(descriptor, "Upgrade_CHW02")["subObjects"]
        self.assertEqual(bow["moduleCount"], 2)
        self.assertIn("Belthronding", bow["show"])
        self.assertIn("WestronSword", bow["show"])

    def test_texture_swaps_carry_both_ends_and_the_recolour_flag(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        helmet = _option(descriptor, "Upgrade_CaptainOfGondor_CHH02")["subObjects"]
        self.assertEqual(
            helmet["textureSwaps"],
            [{"fromTexture": "CHCM_CM_01.tga", "index": 0, "texture": "CHCM_CM.tga"}],
        )
        self.assertTrue(helmet["recolorHouse"])

    def test_an_option_no_module_triggers_still_carries_the_shape(self) -> None:
        # A consumer never branches on key presence.
        garment = (
            "Behavior = SubObjectsUpgrade Only_Upgrade\n"
            "\tTriggeredBy = Upgrade_CaptainOfGondor_CHH01\n"
            "\tShowSubObjects = HAIR_00\n"
            "End\n"
        ).encode("cp1252")
        descriptor = compile_cah_system_descriptor(
            _documents(garment=garment + _garment())
        )
        quiet = _option(descriptor, "Upgrade_CaptainOfGondor_CHH02")["subObjects"]
        self.assertEqual(sorted(quiet), sorted(_option(
            descriptor, "Upgrade_CHW01"
        )["subObjects"]))

    def test_the_default_hidden_set_is_the_whole_corpus_not_one_subclass(self) -> None:
        # All 295 modules hang off the ONE CreateAHero Object, so a sub-object
        # any of them reveals is hidden on any mesh that carries it.  Scoping to
        # the subclass's own BlingUpgrades measurably breaks: the Snow Troll
        # mesh carries twenty-six garment sub-objects its own bling list never
        # names, and every one of them would render at once.
        descriptor = compile_cah_system_descriptor(_documents())
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertEqual(
            sub["defaultSubObjects"]["hide"],
            ["AXE_01", "Belthronding", "WestronSword", "HAIR_00", "HLMT_01"],
        )
        # …while the subclass's own slice is kept as evidence beside it.
        self.assertEqual(
            sub["defaultSubObjects"]["scopedToSubClass"],
            ["HAIR_00", "HLMT_01", "AXE_01", "Belthronding", "WestronSword"],
        )
        # Published under the model binding too, per the runtime contract.
        self.assertEqual(sub["models"]["defaultSubObjects"], sub["defaultSubObjects"])

    def test_a_malformed_texture_swap_refuses_the_compile(self) -> None:
        garment = _garment().replace(
            b"UpgradeTexture = CHCM_CM_01.tga 0 CHCM_CM.tga",
            b"UpgradeTexture = CHCM_CM_01.tga CHCM_CM.tga",
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(garment=garment))
        self.assertIn("UpgradeTexture", str(caught.exception))

    def test_a_module_triggered_by_nothing_refuses_the_compile(self) -> None:
        garment = _garment().replace(
            b"Behavior = SubObjectsUpgrade Dwarf_Upgrade\n"
            b"\tTriggeredBy = Upgrade_CHW01\n",
            b"Behavior = SubObjectsUpgrade Dwarf_Upgrade\n",
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(garment=garment))
        self.assertIn("TriggeredBy nothing", str(caught.exception))

    def test_the_garment_document_is_required(self) -> None:
        documents = _documents()
        del documents["data/ini/object/createahero/createaheroweaponupgrades.inc"]
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(documents)
        self.assertIn("createaheroweaponupgrades.inc", str(caught.exception))


class CahWeaponCombatTests(unittest.TestCase):
    """What a chosen weapon actually does, instead of a fabricated constant."""

    def test_a_weapon_option_carries_its_resolved_combat_numbers(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        combat = _option(descriptor, "Upgrade_CHW01")["combat"]
        self.assertEqual(combat["weaponTemplate"], "CreateAHeroBasicMeleeWeapon")
        self.assertEqual(combat["weaponSetFlag"], "WEAPONSET_CREATE_A_HERO_WS_01")
        self.assertEqual(combat["slot"], "PRIMARY")
        self.assertTrue(combat["melee"])
        self.assertEqual(combat["damage"], 150)
        self.assertEqual(combat["damageType"], "HERO")
        self.assertEqual(combat["attackRange"], 11.5)
        self.assertEqual(combat["delayBetweenShotsMs"], 1400)
        self.assertEqual(combat["preAttackMs"], 800)

    def test_only_the_first_damage_nugget_is_compiled(self) -> None:
        # The later ones are DOT riders gated behind poison-power upgrades.
        descriptor = compile_cah_system_descriptor(_documents())
        self.assertEqual(_option(descriptor, "Upgrade_CHW01")["combat"]["damage"], 150)

    def test_a_toggle_weapon_set_rides_beside_the_base_not_over_it(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        combat = _option(descriptor, "Upgrade_CHW02")["combat"]
        self.assertEqual(combat["weaponTemplate"], "CreateAHeroBasicMeleeWeapon")
        self.assertEqual(len(combat["alternateModes"]), 1)
        mode = combat["alternateModes"][0]
        self.assertEqual(mode["conditions"], ["WEAPONSET_TOGGLE_1"])
        self.assertEqual(mode["weaponTemplate"], "CreateAHeroBasicRangedWeapon")
        self.assertEqual(mode["attackRange"], 350)

    def test_a_non_weapon_option_carries_no_combat_block(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        self.assertNotIn("combat", _option(descriptor, "Upgrade_CaptainOfGondor_CHH01"))

    def test_an_unresolvable_weapon_is_named_never_zeroed(self) -> None:
        garment = _garment().replace(
            b"\tConditions = WEAPONSET_CREATE_A_HERO_WS_01\n",
            b"\tConditions = WEAPONSET_CREATE_A_HERO_WS_99\n",
        )
        descriptor = compile_cah_system_descriptor(_documents(garment=garment))
        self.assertNotIn("combat", _option(descriptor, "Upgrade_CHW01"))
        coverage = descriptor["combatCoverage"]
        self.assertEqual(coverage["unresolvedUpgrades"], ["Upgrade_CHW01"])
        self.assertEqual(coverage["weaponOptions"], 2)
        self.assertEqual(coverage["resolved"], 1)
        self.assertEqual(
            [row["upgradeName"] for row in coverage["gaps"]], ["Upgrade_CHW01"]
        )

    def test_a_weapon_template_the_corpus_lacks_refuses_the_compile(self) -> None:
        weapons = _weapons().replace(
            b"Weapon CreateAHeroBasicMeleeWeapon\n", b"Weapon SomethingElse\n"
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(weapons=weapons))
        self.assertIn("CreateAHeroBasicMeleeWeapon", str(caught.exception))

    def test_a_weapon_with_no_damage_nugget_refuses_the_compile(self) -> None:
        weapons = _weapons().replace(
            b"\tDamageNugget\n\t\tDamage = CREATE_A_HERO_DAMAGE\n\t\tDamageType = HERO\n\tEnd\n",
            b"",
            1,
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(weapons=weapons))
        self.assertIn("DamageNugget", str(caught.exception))

    def test_combat_coverage_reaches_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        self.assertEqual(
            runtime["registration"]["combatCoverage"], descriptor["combatCoverage"]
        )
        options = runtime["registration"]["appearanceOptions"]
        self.assertEqual(options, descriptor["appearanceOptions"])


class CahObjectBaselineTests(unittest.TestCase):
    """Movement and health, in retail units, instead of client guesses."""

    def test_the_baseline_is_compiled_from_the_object_and_its_locomotor(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        baseline = descriptor["objectBaseline"]
        self.assertEqual(baseline["objectId"], "CreateAHero")
        self.assertEqual(baseline["baseHealth"], 2000)
        self.assertEqual(baseline["speed"], 50)
        self.assertEqual(baseline["locomotor"], "HeroHumanLocomotor")
        self.assertEqual(baseline["turnTimeMs"], 500)
        self.assertEqual(baseline["accelerationMs"], 210)
        self.assertEqual(baseline["brakingMs"], 210)
        self.assertEqual(
            [row["condition"] for row in baseline["locomotorSets"]],
            ["SET_NORMAL_UPGRADED", "SET_NORMAL"],
        )

    def test_base_health_is_the_object_body_not_a_constant(self) -> None:
        documents = _documents()
        documents["data/ini/gamedata.ini"] = documents["data/ini/gamedata.ini"].replace(
            b"#define FARAMIR_HEALTH 2000", b"#define FARAMIR_HEALTH 2400"
        )
        descriptor = compile_cah_system_descriptor(documents)
        self.assertEqual(descriptor["objectBaseline"]["baseHealth"], 2400)
        self.assertEqual(descriptor["system"]["maxHealth"], 2400)

    def test_an_object_with_no_normal_locomotor_set_refuses(self) -> None:
        obj = _object().replace(b"\t\tCondition = SET_NORMAL\n", b"\t\tCondition = SET_SLUGGISH\n")
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(obj=obj))
        self.assertIn("SET_NORMAL", str(caught.exception))

    def test_a_locomotor_the_corpus_lacks_refuses_the_compile(self) -> None:
        locomotors = _locomotors().replace(
            b"Locomotor HeroHumanLocomotor\n", b"Locomotor SomethingElse\n"
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(_documents(locomotors=locomotors))
        self.assertIn("HeroHumanLocomotor", str(caught.exception))

    def test_the_baseline_reaches_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        self.assertEqual(
            runtime["registration"]["objectBaseline"], descriptor["objectBaseline"]
        )


if __name__ == "__main__":
    unittest.main()


class CahAuthoredGarmentDefaultTests(unittest.TestCase):
    """Retail's `@` marker: the option a garment group STARTS on.

    The published gap: the pack carried no default flag, so the client picked
    each group's first listed option -- which for most groups is retail's
    "wear nothing" entry.  Every newly created hero rendered bare-headed,
    bare-handed and barefoot.
    """

    def test_the_marked_option_is_the_subclass_group_default(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertEqual(
            sub["appearanceDefaults"],
            {"CreateAHero_Helmet": "Upgrade_CaptainOfGondor_CHH02"},
        )
        # The marker is not the first token; first-wins would pick the wrong one.
        self.assertEqual(
            sub["appearanceChoices"]["CreateAHero_Helmet"][0],
            "Upgrade_CaptainOfGondor_CHH01",
        )

    def test_the_default_outfit_reaches_the_default_show_set(self) -> None:
        # This is what dresses the preview hero: Godot matches options whose
        # parts appear in the show-set, so an empty show-set made the rule inert.
        descriptor = compile_cah_system_descriptor(_documents())
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertEqual(sub["defaultSubObjects"]["show"], ["HLMT_01"])
        self.assertEqual(sub["models"]["defaultSubObjects"]["show"], ["HLMT_01"])
        # APPLY ORDER IS HIDE THEN SHOW: the corpus-wide hide set necessarily
        # contains the very parts the outfit shows.
        self.assertIn("HLMT_01", sub["defaultSubObjects"]["hide"])

    def test_an_unmarked_group_contributes_no_default(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertNotIn("CreateAHero_Weapon", sub["appearanceDefaults"])
        self.assertNotIn("AXE_01", sub["defaultSubObjects"]["show"])

    def test_the_catalog_row_carries_the_default_flag(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        self.assertTrue(_option(descriptor, "Upgrade_CaptainOfGondor_CHH02")["isDefault"])
        self.assertFalse(
            _option(descriptor, "Upgrade_CaptainOfGondor_CHH01")["isDefault"]
        )
        # Every row carries the key; a consumer never branches on presence.
        self.assertTrue(
            all("isDefault" in row for row in descriptor["appearanceOptions"])
        )

    def test_a_marker_naming_an_undeclared_upgrade_refuses_the_compile(self) -> None:
        documents = _documents()
        key = "data/ini/CreateAHeroSystemMenOfTheWest.inc"
        documents[key] = documents[key].replace(
            b"@Upgrade_CaptainOfGondor_CHH02", b"@Upgrade_NoSuchThing"
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(documents)
        self.assertIn("Upgrade_NoSuchThing", str(caught.exception))

    def test_two_markers_in_one_group_refuse_the_compile(self) -> None:
        documents = _documents()
        key = "data/ini/CreateAHeroSystemMenOfTheWest.inc"
        documents[key] = documents[key].replace(
            b"Upgrade_CaptainOfGondor_CHH01 @Upgrade_CaptainOfGondor_CHH02",
            b"@Upgrade_CaptainOfGondor_CHH01 @Upgrade_CaptainOfGondor_CHH02",
        )
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(documents)
        self.assertIn("marks two defaults", str(caught.exception))

    def test_an_unspaced_marker_still_yields_both_names(self) -> None:
        # Tolerance, not a retail form: retail always spaces the marker, but
        # stripping rather than splitting would turn `A@B` into one dead name.
        documents = _documents()
        key = "data/ini/CreateAHeroSystemMenOfTheWest.inc"
        documents[key] = documents[key].replace(
            b"Upgrade_CaptainOfGondor_CHH01 @Upgrade_CaptainOfGondor_CHH02",
            b"Upgrade_CaptainOfGondor_CHH01@Upgrade_CaptainOfGondor_CHH02",
        )
        descriptor = compile_cah_system_descriptor(documents)
        sub = descriptor["classes"][0]["subClasses"][0]
        self.assertEqual(
            sub["appearanceChoices"]["CreateAHero_Helmet"],
            ["Upgrade_CaptainOfGondor_CHH01", "Upgrade_CaptainOfGondor_CHH02"],
        )
        self.assertEqual(
            sub["appearanceDefaults"]["CreateAHero_Helmet"],
            "Upgrade_CaptainOfGondor_CHH02",
        )

    def test_the_defaults_reach_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        sub = runtime["registration"]["classes"][0]["subClasses"][0]
        self.assertEqual(sub["defaultSubObjects"]["show"], ["HLMT_01"])
        self.assertEqual(
            sub["appearanceDefaults"],
            {"CreateAHero_Helmet": "Upgrade_CaptainOfGondor_CHH02"},
        )


class CahCreationIdleTests(unittest.TestCase):
    """The creation screen's idle loop.

    Retail plays a bored-idle cycle plus three special idles on the hero
    creation screen. None of it reached the pack, so the preview hero stood
    still in its bind pose.
    """

    def _creation(self, descriptor: dict) -> dict:
        return descriptor["classes"][0]["subClasses"][0]["models"]["creationScreen"]

    def test_the_idle_names_are_built_from_the_subclass_anim_prefix(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        idles = self._creation(descriptor)["creationIdles"]
        self.assertEqual(idles["animationPrefix"], "CHHW_CG")
        self.assertEqual(
            [row["sourceAnimation"] for row in idles["base"]],
            ["CHHW_CG_C_ATNB", "CHHW_CG_C_ATND", "CHHW_CG_C_ATNE"],
        )

    def test_the_three_specials_come_off_the_system_block(self) -> None:
        # Not from the anim file: the system block is where retail states them,
        # and it is the only place the roles are distinguishable.
        descriptor = compile_cah_system_descriptor(_documents())
        idles = self._creation(descriptor)["creationIdles"]
        self.assertEqual(
            [(row["role"], row["sourceAnimation"]) for row in idles["specials"]],
            [
                ("selectedCheer", "CHHW_CG_C_SLCA"),
                ("examineWeapon", "CHHW_CG_C_WPNA"),
                ("examineSelf", "CHHW_CG_C_CLRA"),
            ],
        )
        self.assertEqual(idles["specialChancePercent"], 20.0)

    def test_the_special_chance_is_a_percent_float_not_a_fraction(self) -> None:
        # The consumer clamps this to 0..100. A 0..1 fraction would fire a
        # special idle a fifth of one percent of the time; an int would hand a
        # typed reader the wrong type for a legitimately fractional field.
        descriptor = compile_cah_system_descriptor(_documents())
        chance = self._creation(descriptor)["creationIdles"]["specialChancePercent"]
        self.assertIsInstance(chance, float)
        self.assertEqual(chance, 20.0)
        self.assertEqual(
            descriptor["creationIdlePlan"]["specialChancePercent"], 20.0
        )

    def test_a_fractional_chance_survives_verbatim(self) -> None:
        documents = _documents()
        documents["data/ini/createaherosystem.ini"] = documents[
            "data/ini/createaherosystem.ini"
        ].replace(b"SpecialAnimPercentChance = 20.0", b"SpecialAnimPercentChance = 12.5")
        descriptor = compile_cah_system_descriptor(documents)
        self.assertEqual(
            self._creation(descriptor)["creationIdles"]["specialChancePercent"], 12.5
        )

    def test_each_entry_carries_the_converted_glb_animation_name(self) -> None:
        # The consumer asks the GLB by this name; it must not have to
        # re-implement the adapter's naming rule.
        descriptor = compile_cah_system_descriptor(_documents())
        idles = self._creation(descriptor)["creationIdles"]
        self.assertEqual(
            [row["animation"] for row in idles["base"]],
            ["chhw_cg_c_atnb", "chhw_cg_c_atnd", "chhw_cg_c_atne"],
        )
        self.assertEqual(idles["specials"][0]["animation"], "chhw_cg_c_slca")

    def test_the_glb_name_rule_matches_the_blender_adapter(self) -> None:
        # Pins the reproduced rule to the adapter's own `clean_name`, which is
        # the thing that actually names the exported clip.
        import re as _re

        from openbfme_importer.cah_system_compiler import glb_animation_name

        def adapter_clean_name(value: str) -> str:
            return _re.sub(r"[^a-z0-9_]+", "_", value.casefold()).strip("_")

        for probe in ("CHHW_CG_C_ATNB", "CHAR_AR_C_SLCA", "A B-C.D", "_X_"):
            self.assertEqual(glb_animation_name(probe), adapter_clean_name(probe))

    def test_a_battlefield_state_contributes_no_creation_idle(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        idles = self._creation(descriptor)["creationIdles"]
        every = [row["sourceAnimation"] for row in idles["base"]]
        self.assertNotIn("CHHW_CG_U_RUNA", every)

    def test_the_specials_are_not_repeated_in_the_base_loop(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        idles = self._creation(descriptor)["creationIdles"]
        base = {row["sourceAnimation"] for row in idles["base"]}
        for row in idles["specials"]:
            self.assertNotIn(row["sourceAnimation"], base)

    def test_a_system_with_no_special_anim_field_refuses(self) -> None:
        documents = _documents()
        documents["data/ini/createaherosystem.ini"] = documents[
            "data/ini/createaherosystem.ini"
        ].replace(b"\tSelectedCheerAninName = _C_SLCA\n", b"")
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(documents)
        self.assertIn("SelectedCheerAninName", str(caught.exception))

    def test_a_system_with_no_special_chance_refuses(self) -> None:
        documents = _documents()
        documents["data/ini/createaherosystem.ini"] = documents[
            "data/ini/createaherosystem.ini"
        ].replace(b"\tSpecialAnimPercentChance = 20.0\n", b"")
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(documents)
        self.assertIn("SpecialAnimPercentChance", str(caught.exception))

    def test_an_anim_file_with_no_creation_state_refuses(self) -> None:
        with self.assertRaises(CahSystemCompilerError) as caught:
            compile_cah_system_descriptor(
                _documents(anims=b"AnimationState = MOVING\nEnd\n")
            )
        self.assertIn("CREATE_A_HERO_IN_CREATION_SCREEN", str(caught.exception))

    def test_the_idle_plan_reaches_the_runtime_document(self) -> None:
        descriptor = compile_cah_system_descriptor(_documents())
        runtime = build_cah_system_runtime(descriptor)
        self.assertEqual(
            runtime["registration"]["creationIdlePlan"], descriptor["creationIdlePlan"]
        )
        creation = runtime["registration"]["classes"][0]["subClasses"][0]["models"][
            "creationScreen"
        ]
        self.assertEqual(creation["creationIdles"]["specialChancePercent"], 20.0)
