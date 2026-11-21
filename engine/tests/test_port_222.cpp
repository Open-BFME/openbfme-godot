// OpenBFME unit tests. PORT-222: the experience a LevelGrantSpecialPower gives lands as RotWK 2.01 gives it — one pool per member template
// in the horde contain, every member and the horde on that pool. GPL-3.0.
// Synthetic INI only; no ROTWK_INSTALL / OPENBFME_ENNORATH_INSTALL.
//
// What RotWK 2.01 does (every address read with tools/mod/rw.py from the owner's game.dat, md5 daed3668…):
//   * RW 0x8C6FA4 (LevelGrantSpecialPower::triggerAbilityEffect, vslot 17): the base trigger first (RW 0x8C6FB6 -> RW 0x853EDF), then every live
//     allied object the module's AcceptanceFilter allows within RadiusEffect (+ 0xD4, RW 0x8C7062) of the module's location (+ 0x44, RW 0x8C7075) is
//     handed to RW 0x8C6F32 in near-to-far order (RW 0x8C7069 push 1, RW 0x8C707D -> RW 0xA39340).
//   * RW 0x8C6F32 (the per-hit wrapper): the hit's horde — the object found for its + 0x78 id (RW 0x8C6F3B -> RW 0x449681) whose template has
//     KindOf HORDE (RW 0x8C6F57: test dword ptr [edx+0x114], 0x2000) — replaces the hit; such a horde is granted once per call (the granted-id list,
//     RW 0x8C6F74 .. 0x8C6F90), anything else every time. Then the grant runs (RW 0x8C6F98 -> RW 0x8C6E82).
//   * RW 0x8C6E82 (the grant): Experience <= 0 does nothing (RW 0x8C6E95). A HORDE (RW 0x8C6EA2: test byte ptr [ecx+0x115], 0x20) takes the amount
//     through its horde contain interface: the object's contain (RW 0x8C6EAD -> RW 0x694BF8 -> RW 0x68C866: Object + 0x258, vslot 0x7C), the member
//     count (RW 0x8C6EBD, vslot 0x180(0); 0 -> nothing granted, RW 0x8C6EC7) and amount = Experience / count (RW 0x8C6EC9: fild [esi+0xD0]; fild count;
//     fdivp). The per-object grant (RW 0x8C6D43) hands each member to the source at grant + 0x08 through its vslot 0xC0 (RW 0x8C6D4F .. 0x8C6D5E:
//     mov ecx,[esi+8]; call [eax+0xC0]) — the horde contain's addExperience, RW 0x873A02. Without a source the amount goes to the object's own
//     tracker (RW 0x8C6D6A: Object + 0x26C) when it still accepts experience (RW 0x8C6D76 -> RW 0x79D322): addExperiencePoints(amount, 1, 1, 1, 0)
//     (RW 0x8C6D8F -> RW 0x79D833). LevelFX follows on that object (RW 0x8C6D94 .. 0x8C6DA2 -> RW 0x4B1B5A).
//   * RW 0x873A02 (HordeContain::addExperience): one pool per member template id (template + 0x5E8, RW 0x873A19); a NEW pool starts at
//     amount + 1.0 (RW 0x873A49: addss [0xBD1908]), an existing one grows by amount (RW 0x873A58). Every member of that template is set to the pool
//     (RW 0x873A90: contain vslot 0x110 -> RW 0x86BC09 -> RW 0x79D8EF setExperienceAndLevel(pool, 0)), and a pool above the horde's own experience is
//     set on the horde too, a rank it raised running the banner check (RW 0x873AA7 .. 0x873AD7 -> RW 0x8719E4(1)).
//   So a grant of Experience 100 on a six-member horde puts 100 + 1.0 into the pool and levels the horde on RequiredExperience 100; the
//   per-member split of 100 / 6 that the port did before left every member below the next level.
//
// The fixture's numbers: Experience 100, RadiusEffect 120 and two levels with RequiredExperience 1 / 100.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "IniTestUtil.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

namespace
{
// Two levels for the fixture's horde: level 1 at 1 point, level 2 at 100 (the grant's amount)
const char kPort222Levels[] =
	"ExperienceLevel Port222Level1\n"
	"  TargetNames = Port222Pikeman Port222PikemanHorde Port222PikemanHordePorcupine Port222Caster\n"
	"  RequiredExperience = 1\n"
	"  ExperienceAward = 10\n"
	"  Rank = 1\n"
	"End\n"
	"ExperienceLevel Port222Level2\n"
	"  TargetNames = Port222Pikeman Port222PikemanHorde Port222PikemanHordePorcupine Port222Caster\n"
	"  RequiredExperience = 100\n"
	"  ExperienceAward = 15\n"
	"  Rank = 2\n"
	"End\n";

// A SpecialPowerModule + LevelGrantSpecialPower on a synthetic caster, a six-member horde and the horde's member template; the filter names both
// kinds so one module reaches either branch of RW 0x8C6E9F
const char kPort222Fixture[] =
	"SpecialPower Port222KingsFavor\n"
	"  Enum = SPECIAL_KINGS_FAVOR\n"
	"  ReloadTime = 200000\n"
	"  RadiusCursorRadius = 120.0\n"
	"  Flags = NEEDS_OBJECT_FILTER\n"
	"  ObjectFilter = ANY +Port222PikemanHorde ALLIES\n"
	"End\n"
	"Object Port222PikemanHorde\n"
	"  KindOf = HORDE MELEE_HORDE SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Swordsman "
	"Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 "
	"Position:X:80 Y:0 Position:X:80 Y:20 Position:X:80 Y:-20\n"
	"    Slots = 6\n"
	"    InitialPayload = Port222Pikeman 6\n"
	"  End\n"
	"End\n"
	"Object Port222Caster\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 30\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_KingsFavorSpecialPowerModule\n"
	"    SpecialPowerTemplate = Port222KingsFavor\n"
	"    StartsPaused = No\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = LevelGrantSpecialPower ModuleTag_KingsFavor\n"
	"    SpecialPowerTemplate = Port222KingsFavor\n"
	"    StartAbilityRange = 200.0\n"
	"    Experience = 100\n"
	"    RadiusEffect = 120\n"
	"    AcceptanceFilter = ANY +Port222PikemanHorde +Port222PikemanHordePorcupine +Port222Pikeman\n"
	"    PreparationTime = 0\n"
	"  End\n"
	"End\n"
	"Object Port222Pikeman\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

// The stores the fixture's INI needs, registered the way lane BACKLOG4's power tests register them (test_backlog4_powers.cpp)
struct Port222World
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	ExperienceLevelSystem levels;
	ExperienceLevelSystem *savedLevels = TheExperienceLevelSystem;

	Port222World()
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.w.fx.env.blocks);
		w.w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		levels.registerBlocks(w.w.fx.env.blocks);
		TheExperienceLevelSystem = &levels;
		SpecialAbilityModules::registerAll(w.w.modules);
		SpecialPowerModules::registerAll(w.w.modules);
		const std::string levelsErr = w.w.load(kPort222Levels, INI_LOAD_OVERWRITE, "port222-levels.ini");
		REQUIRE_MESSAGE(levelsErr.empty(), levelsErr);
		const std::string err = w.w.load(kPort222Fixture, INI_LOAD_OVERWRITE, "port222.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Port222World()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
		TheExperienceLevelSystem = savedLevels;
	}

	combattest::CombatWorld w;
};

// the module the power's button drives: the LevelGrantSpecialPower of that power
LevelGrantSpecialPower *kingsFavorOf(Object &horde)
{
	const SpecialPowerTemplate *power = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate("Port222KingsFavor") : nullptr;
	REQUIRE(power);
	SpecialPowerUpdateInterface *u = SpecialAbilityModules::findUpdate(horde, power);
	REQUIRE(u);
	LevelGrantSpecialPower *grant = dynamic_cast<LevelGrantSpecialPower *>(u);
	REQUIRE(grant);
	return grant;
}

float experienceOf(const Object &o)
{
	const ExperienceTracker *t = o.getExperienceTracker();
	REQUIRE(t);
	return t ? t->getExperience() : -1.0f;
}

int rankOf(const Object &o)
{
	const ExperienceTracker *t = o.getExperienceTracker();
	REQUIRE(t);
	return t ? t->getRank() : -1;
}
} // namespace

// The case that fails without the fix: one grant on a horde of six. RW 0x873A02 keeps the pool (a new one starts at
// amount + 1.0, RW 0x873A49) and every member and the horde follow it, with amount = Experience / count (RW 0x8C6EC9): the six members at 100 / 6
// each leave the pool at 100 + 1.0, so the horde holds Port222Level2 (Rank 2, RequiredExperience 100). The old per-member split left every
// member at 100 / 6 = 16.67, below it.
TEST_CASE("PORT-222 a level grant fills a horde's experience through the pool (RW 0x8C6E82 -> RW 0x873A02)")
{
	Port222World f;
	// RW 0x8C6FA4 skips the casting object itself (the filter at RW 0xC0F374), so the caster stands apart from the horde it fills
	Object *caster = f.w.spawn("Port222Caster", 300.0f, 300.0f);
	REQUIRE(caster);
	Object *horde = f.w.spawn("Port222PikemanHorde", 340.0f, 300.0f); // inside RadiusEffect 120
	REQUIRE(horde);
	HordeContainInterface *contain = horde->getContain() ? horde->getContain()->getHordeContainInterface() : nullptr;
	REQUIRE(contain);
	REQUIRE(contain->countWithEntering() == 6);
	const float before = experienceOf(*horde);
	CHECK(rankOf(*horde) == 1); // the fixture's first level: RequiredExperience 1, Rank 1

	LevelGrantSpecialPower *grant = kingsFavorOf(*caster);
	grant->triggerAtForTest(*horde->getPosition()); // the cast lands on the horde's own ground point

	// RW 0x873A02: a new pool of the members' template starts at amount + 1.0 and the horde follows a pool above its own experience
	const float after = experienceOf(*horde);
	CHECK(after == doctest::Approx(before + 100.0f).epsilon(0.001)); // tolerance: float sum of 6 shares plus +1.0 pool start
	CHECK(rankOf(*horde) == 2);

	// every member of the granted template follows the same pool (RW 0x86BC09 -> RW 0x79D8EF)
	unsigned members = 0, atPool = 0;
	const ContainModuleInterface::ContainedItemsList *items = horde->getContain()->getContainedItemsList();
	REQUIRE(items);
	for (Object *m : *items)
	{
		++members;
		atPool += experienceOf(*m) == doctest::Approx(after) ? 1 : 0; // tolerance: float equality against horde pool
	}
	CHECK(members == 6);
	CHECK(atPool == 6);
	// RW 0x8C6D43 runs once per member (contain vslot 0x110), and the counter and the LevelFX of the context are inside it: six grants
	CHECK(grant->granted() == 6);
}

// The neighbour that must not move: a grant on an object that is not a horde still adds the whole amount to its own tracker
// (RW 0x8C6D6A -> RW 0x79D833), not a share, and levels on the same RequiredExperience.
TEST_CASE("PORT-222 a level grant on a lone unit adds the whole amount to its tracker (RW 0x8C6D6A -> RW 0x79D833)")
{
	Port222World f;
	Object *caster = f.w.spawn("Port222Caster", 300.0f, 300.0f);
	REQUIRE(caster);
	Object *unit = f.w.spawn("Port222Pikeman", 340.0f, 300.0f);
	REQUIRE(unit);
	REQUIRE(unit->getExperienceTracker());
	const float before = experienceOf(*unit);
	CHECK(rankOf(*unit) == 1);

	// the same module data, cast on a single object that is not a horde: RW 0x8C6F09 .. 0x8C6F24 hands it the whole Experience, no division
	LevelGrantSpecialPower *grant = kingsFavorOf(*caster);
	REQUIRE(grant);
	grant->triggerAtForTest(*unit->getPosition());

	const float after = experienceOf(*unit);
	CHECK(after == doctest::Approx(before + 100.0f)); // tolerance: float equality; RW 0x8C6D43 with no source
	CHECK(rankOf(*unit) == 2);                       // RequiredExperience 100
	CHECK(grant->granted() == 1);                    // one object, one per-object grant (RW 0x8C6D94)
}
