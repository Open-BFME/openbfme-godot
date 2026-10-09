// OpenBFME. GPL-3.0.
// Lane XP-1 without retail data: the ExperienceLevel / ExperienceScalarTable / ModifierList parsers (field tables, errors, the first-definition rule), the
// next-level rule of RW 0x689E3C (smallest RequiredExperience above the current one, SinglePlayerOnly / MultiPlayerOnly), the default scalar table and the
// ModifierList value query (RW 0x805268).

#include "doctest.h"

#include "IniTestUtil.h"

#include "Common/INIException.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/ExperienceLevels.h"

namespace
{
struct XpFixture
{
	initest::Fixture fx;
	UpgradeCenter center;
	ExperienceLevelSystem levels;
	AttributeModifierStore modifiers;
	FXListStore fxLists;
	UpgradeCenter *savedCenter = TheUpgradeCenter;
	FXListStore *savedFx = TheFXListStore;
	ExperienceLevelSystem *savedLevels = TheExperienceLevelSystem;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	XpFixture()
	{
		center.init();
		center.registerBlock(fx.env.blocks);
		levels.registerBlocks(fx.env.blocks);
		modifiers.registerBlock(fx.env.blocks);
		fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		TheFXListStore = &fxLists;
		TheUpgradeCenter = &center;
		REQUIRE(load("FXList GenericLevelUp2FX\nEnd\nFXList BuffFX\nEnd\n").empty()); // the FX lists the blocks below name (RW 0x73A302 validates)
		TheExperienceLevelSystem = &levels;
		TheAttributeModifierStore = &modifiers;
	}
	~XpFixture()
	{
		TheFXListStore = savedFx;
		TheUpgradeCenter = savedCenter;
		TheExperienceLevelSystem = savedLevels;
		TheAttributeModifierStore = savedModifiers;
	}
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return initest::loadError(fx.env, "xp.ini", text, type, code);
	}
};

const char *kLevels =
	"Upgrade Upgrade_ObjectLevel2\n"
	"  Type = OBJECT\n"
	"End\n"
	"ExperienceScalarTable DefaultExperienceScalarTable\n"
	"  Scalars = 1.0 0.1 0.01\n"
	"End\n"
	"ExperienceLevel TroopLevel1\n"
	"  TargetNames = Soldier SoldierHorde\n"
	"  RequiredExperience = 1\n"
	"  ExperienceAward = 3\n"
	"  Rank = 1\n"
	"  SelectionDecal\n"
	"    Texture = decal_level1\n"
	"    Style = SHADOW_MERGE_DECAL\n"
	"    OpacityMin = 80%\n"
	"    MaxSelectedUnits = 40\n"
	"  End\n"
	"End\n"
	"ExperienceLevel TroopLevel3\n"
	"  TargetNames = Soldier SoldierHorde\n"
	"  RequiredExperience = 100\n"
	"  ExperienceAward = 5\n"
	"  Rank = 3\n"
	"End\n"
	"ExperienceLevel TroopLevel2\n"
	"  TargetNames = Soldier SoldierHorde\n"
	"  RequiredExperience = 50\n"
	"  ExperienceAward = 4\n"
	"  ExperienceAwardOwnGuysDie = 2\n"
	"  AttributeModifiers = TroopBonusRank2\n"
	"  Upgrades = Upgrade_ObjectLevel2 Upgrade_NoSuchThing\n"
	"  LevelUpFx = FX:GenericLevelUp2FX BONE B_HEAD\n"
	"  LevelUpTintColor = R:255 G:128 B:0\n"
	"  ModelConditionState = WEAPONSET_VETERAN\n"
	"  EmotionType = CHEER\n"
	"  Rank = 2\n"
	"End\n"
	"ExperienceLevel TroopLevel2Solo\n"
	"  TargetNames = Soldier\n"
	"  RequiredExperience = 40\n"
	"  SinglePlayerOnly = Yes\n"
	"  Rank = 2\n"
	"End\n";
} // namespace

TEST_CASE("xp core: ExperienceLevel blocks parse with the binary's field table and chain by TargetNames")
{
	XpFixture f;
	REQUIRE(f.load(kLevels).empty());
	CHECK(f.levels.levelCount() == 4);
	CHECK(f.levels.scalarTableCount() == 1);
	const ExperienceLevelTemplate *l2 = f.levels.findLevel("TroopLevel2");
	REQUIRE(l2);
	CHECK(l2->m_requiredExperience == 50);
	CHECK(l2->m_experienceAward == 4);
	CHECK(l2->m_experienceAwardOwnGuysDie == 2);
	CHECK(f.levels.findLevel("TroopLevel1")->m_experienceAwardOwnGuysDie == -1); // the constructor's -1 (RW 0x689A79)
	CHECK(l2->m_attributeModifiers == std::vector<std::string>{ "TroopBonusRank2" });
	REQUIRE(l2->m_upgrades.size() == 1); // RW 0x6897B9: a name that is not an upgrade is skipped
	CHECK(l2->m_upgrades[0] == f.center.findUpgrade("Upgrade_ObjectLevel2"));
	CHECK(l2->m_skippedUpgradeNames == 1);
	REQUIRE(l2->m_levelUpFx.size() == 1);
	CHECK(l2->m_levelUpFx[0].fxList == "GenericLevelUp2FX");
	CHECK(l2->m_levelUpFx[0].bone == "B_HEAD");
	CHECK(l2->m_levelUpTintColor[1] == doctest::Approx(128.0f / 255.0f).epsilon(0.01));
	CHECK(l2->m_modelConditionState[0] != 0u);
	CHECK(l2->m_emotionType == 1); // CHEER
	CHECK(l2->m_rank == 2);
	const ExperienceLevelTemplate *l1 = f.levels.findLevel("TroopLevel1");
	CHECK(l1->m_selectionDecal.texture == "decal_level1");
	CHECK(l1->m_selectionDecal.maxSelectedUnits == 40u);
	CHECK(l1->m_selectionDecal.opacityMin == doctest::Approx(0.8f));
	REQUIRE(f.levels.levelsFor("SoldierHorde"));
	CHECK(f.levels.levelsFor("SoldierHorde")->size() == 3);
	CHECK(f.levels.levelsFor("Soldier")->size() == 4);
	CHECK(f.levels.levelsFor("Orc") == nullptr);
}

TEST_CASE("xp core: the next level is the smallest RequiredExperience above the current one, filtered by SinglePlayerOnly / MultiPlayerOnly (RW 0x689E3C)")
{
	XpFixture f;
	REQUIRE(f.load(kLevels).empty());
	// multiplayer: the solo level is invalid; definition order does not matter (TroopLevel3 is defined before TroopLevel2)
	CHECK(f.levels.nextLevel("Soldier", "", true)->m_name == "TroopLevel1");
	CHECK(f.levels.nextLevel("Soldier", "TroopLevel1", true)->m_name == "TroopLevel2");
	CHECK(f.levels.nextLevel("Soldier", "TroopLevel2", true)->m_name == "TroopLevel3");
	CHECK(f.levels.nextLevel("Soldier", "TroopLevel3", true) == nullptr);
	// single player: the 40 level comes first
	CHECK(f.levels.nextLevel("Soldier", "TroopLevel1", false)->m_name == "TroopLevel2Solo");
	CHECK(f.levels.nextLevel("Soldier", "TroopLevel2Solo", false)->m_name == "TroopLevel2");
	CHECK(f.levels.nextLevel("SoldierHorde", "TroopLevel1", false)->m_name == "TroopLevel2");
}

TEST_CASE("xp core: scalar tables by name, the NOTFOUND default { 1.0 } (RW 0x68A151)")
{
	XpFixture f;
	REQUIRE(f.load(kLevels).empty());
	const ExperienceScalarTable *t = f.levels.findScalarTable("DefaultExperienceScalarTable");
	REQUIRE(t);
	CHECK(t->m_scalars.size() == 3);
	CHECK(t->m_scalars[1] == doctest::Approx(0.1f));
	const ExperienceScalarTable *d = f.levels.findScalarTable("NoSuchTable");
	CHECK(d == &f.levels.defaultScalarTable());
	CHECK(d->m_name == "NOTFOUND_DEFAULT_ScalarTable");
	CHECK(d->m_scalars == std::vector<float>{ 1.0f });
}

TEST_CASE("xp core: ExperienceLevel errors: 'fx' expected, a map.ini override needs an existing level, load type 5 is not ported")
{
	XpFixture f;
	int code = 0;
	CHECK(f.load("ExperienceLevel Bad\n  LevelUpFx = OCL:Something\nEnd\n", INI_LOAD_OVERWRITE, &code).find("'fx' expected") != std::string::npos);
	CHECK(code == 3);
	REQUIRE(f.load(kLevels).empty());
	CHECK(f.load("ExperienceLevel Missing\n  Rank = 9\nEnd\n", INI_LOAD_CREATE_OVERRIDES, &code).find("Experience Level Missing not found in map.ini") != std::string::npos);
	REQUIRE(f.load("ExperienceLevel TroopLevel2\n  RequiredExperience = 60\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(f.levels.findLevel("TroopLevel2")->m_requiredExperience == 60); // the override, a copy of the base with the new field
	CHECK(f.levels.findLevel("TroopLevel2")->m_experienceAward == 4);
	f.levels.resetOverrides();
	CHECK(f.levels.findLevel("TroopLevel2")->m_requiredExperience == 50);
	CHECK_FALSE(f.load("ExperienceLevel TroopLevel2\n  Rank = 2\nEnd\n", INI_LOAD_RELOAD).empty());
}

TEST_CASE("xp core: ModifierList parse (RW 0xC4EAF0), the first definition wins, the value query filters on names (RW 0x805268)")
{
	XpFixture f;
	REQUIRE(f.load("Upgrade Upgrade_Buff\n  Type = OBJECT\nEnd\n"
				   "ModifierList TroopBonusRank2\n"
				   "  Category = LEVEL\n"
				   "  Modifier = HEALTH 20\n"
				   "  Modifier = DAMAGE_ADD 10\n"
				   "  Modifier = ARMOR 25% SLASH PIERCE\n"
				   "  Modifier = DAMAGE_ADD 12\n"
				   "  Duration = 1000\n"
				   "  Upgrade = Upgrade_Buff Delay 400\n"
				   "  ReplaceInCategoryIfLongest = Yes\n"
				   "End\n"
				   "ModifierList TroopBonusRank2\n"
				   "  Category = BUFF\n"
				   "  Modifier = HEALTH 999\n"
				   "End\n")
			  .empty());
	CHECK(f.modifiers.size() == 1);
	const ModifierListTemplate *l = f.modifiers.find("TroopBonusRank2");
	REQUIRE(l);
	CHECK(l->m_category == ATTRIBUTE_CATEGORY_LEVEL);
	CHECK(l->m_duration == 5u); // 1000 ms -> 5 frames
	REQUIRE(l->m_modifiers.size() == 3);
	float v = 0.0f;
	CHECK(l->value(ATTRIBUTE_HEALTH, nullptr, v));
	CHECK(v == 20.0f);
	CHECK(l->value(ATTRIBUTE_DAMAGE_ADD, nullptr, v));
	CHECK(v == 12.0f); // a type the list has: the new value (RW 0x806247)
	CHECK(l->value(ATTRIBUTE_ARMOR, "SLASH", v));
	CHECK(v == doctest::Approx(0.25f));
	CHECK_FALSE(l->value(ATTRIBUTE_ARMOR, "CRUSH", v));
	CHECK(l->value(ATTRIBUTE_ARMOR, nullptr, v));
	CHECK_FALSE(l->value(ATTRIBUTE_DAMAGE_MULT, nullptr, v));
	REQUIRE(l->m_upgrade);
	CHECK(l->m_upgrade->upgrade == f.center.findUpgrade("Upgrade_Buff"));
	CHECK(l->m_upgrade->delayFrames == 2u);
	CHECK(l->m_replaceInCategoryIfLongest);
	int code = 0;
	CHECK(f.load("ModifierList X\n  Modifier = Health 20\nEnd\n", INI_LOAD_OVERWRITE, &code).find("Attribute 'Health' not found") != std::string::npos); // strcmp
	CHECK(f.load("ModifierList Y\n  Category = NONE\nEnd\n", INI_LOAD_OVERWRITE, &code).find("Invalid Category") != std::string::npos);
	CHECK(f.load("ModifierList Z\n  Modifier = ATTRIBUTE_NONE 1\nEnd\n", INI_LOAD_OVERWRITE, &code).find("not found") != std::string::npos);
}

TEST_CASE("xp core: LevelUpFx and every ModifierList FX / EndFX name must be an FXList (RW 0x73A302: INIException 3), None in any case stores none")
{
	XpFixture f;
	int code = 0;
	CHECK(f.load("ExperienceLevel BadFx\n  TargetNames = X\n  LevelUpFx = FX:NoSuchFX\nEnd\n", INI_LOAD_OVERWRITE, &code).find("NoSuchFX not found") != std::string::npos);
	CHECK(code == 3);
	REQUIRE(f.load("ExperienceLevel NoneFx\n  TargetNames = X\n  LevelUpFx = FX:nOnE\n  LevelUpFx = FX:GenericLevelUp2FX\nEnd\n").empty());
	const ExperienceLevelTemplate *l = f.levels.findLevel("NoneFx");
	REQUIRE(l);
	REQUIRE(l->m_levelUpFx.size() == 2);
	CHECK(l->m_levelUpFx[0].fxList.empty());
	CHECK(l->m_levelUpFx[1].fxList == "GenericLevelUp2FX");
	for (const char *field : { "FX", "FX2", "FX3", "EndFX", "EndFX2", "EndFX3" })
	{
		CAPTURE(field);
		code = 0;
		const std::string bad = std::string("ModifierList Bad") + field + "\n  Category = BUFF\n  " + field + " = NoSuchFX\nEnd\n";
		CHECK(f.load(bad, INI_LOAD_OVERWRITE, &code).find("NoSuchFX not found") != std::string::npos);
		CHECK(code == 3);
		const std::string good = std::string("ModifierList Good") + field + "\n  Category = BUFF\n  " + field + " = BuffFX\nEnd\n"
			"ModifierList None" + field + "\n  Category = BUFF\n  " + field + " = NONE\nEnd\n";
		CHECK(f.load(good).empty());
	}
	CHECK(f.modifiers.find("GoodFX")->m_fx[0] == "BuffFX");
	CHECK(f.modifiers.find("GoodEndFX3")->m_endFx[2] == "BuffFX");
	CHECK(f.modifiers.find("NoneFX2")->m_fx[1].empty());
}
