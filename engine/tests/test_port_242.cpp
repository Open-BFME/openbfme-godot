// OpenBFME unit tests: PORT-242 (fork issue 566): SpecialPower RequirementsFilterStrategic across all 11 SpecialPowerModule classes.
//
// What RotWK 2.01 does (read with tools/mod/rw.py from game.dat, confirmed against DONOR BFME2 0x00493251 in SpecialPowerModuleRva0049466D.cpp):
//   * SpecialPowerModuleData::buildFieldParse (RW 0x89699B, table RW 0xC64DB0) parses RequirementsFilterMPSkirmish (+0x38, row RW 0xC64E90) and
//     RequirementsFilterStrategic (+0x3C, row RW 0xC64EA0) with iniParseObjectFilter (RW 0x76392F). Constructor RW 0x896834 initialises both slots
//     (RW 0x8968A2 / RW 0x8968AA -> RW 0x76406F) and sets both to ObjectFilter::none with empty masks (RW 0x89695E / RW 0x896982 -> RW 0x763D11,
//     +0x88 flag = 0).
//   * SpecialPowerModule::requirementsMet (RW 0x8969E5, called by SpecialPowerModuleInterface vslot +0x48 RW 0x897E01 and vslot +0x58 RW 0x896A32):
//       - RW 0x8969E8 .. 0x8969FE: reads TheGameLogic ([0xDE412C]) + 0x114 (gameKind); when gameKind != 3 (e.g. a War of the Ring battle, gameKind == 0,
//         RW 0x6DCFDF / RW 0x779CB4), takes moduleData + 0x3C (RequirementsFilterStrategic) without testing RW 0x625456.
//       - RW 0x896A00 .. 0x896A09: when gameKind == 3, calls RW 0x625456 (skirmish / multiplayer); false -> returns true (campaign / solo);
//         true -> takes moduleData + 0x38 (RequirementsFilterMPSkirmish).
//       - RW 0x896A0C .. 0x896A15: calls RW 0x762977 on the chosen filter handle; when unset or filter + 0x88 flag == 0 (parsed NONE with no `+` entry),
//         returns true without scanning player objects.
//       - RW 0x896A17 .. 0x896A2D: calls Object::getControllingPlayer (RW 0x68B678) -> Player::hasObjectMatching(filter, true) (RW 0x6ABD0B).
//
// Synthetic INI only; no game files required.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/SpellStores.h"

#include <string>

namespace
{
const char kRanks[] =
	"Rank 1\n  SkillPointsNeededDefault = 0\nEnd\n"
	"Rank 2\n  SkillPointsNeededDefault = 100\nEnd\n";

struct ClassSpec
{
	const char *moduleClass;
	const char *powerWotr;
	const char *powerStratReq;
};

const ClassSpec kElevenClasses[] = {
	{ "CloudBreakSpecialPower", "SP_Wotr_CloudBreak", "SP_Req_CloudBreak" },
	{ "OCLSpecialPower", "SP_Wotr_OCL", "SP_Req_OCL" },
	{ "PlayerHealSpecialPower", "SP_Wotr_Heal", "SP_Req_Heal" },
	{ "PlayerUpgradeSpecialPower", "SP_Wotr_Upgrade", "SP_Req_Upgrade" },
	{ "SpecialPowerModule", "SP_Wotr_Base", "SP_Req_Base" },
	{ "DevastateSpecialPower", "SP_Wotr_Devastate", "SP_Req_Devastate" },
	{ "ElvenWoodSpecialPower", "SP_Wotr_ElvenWood", "SP_Req_ElvenWood" },
	{ "ProductionSpeedBonus", "SP_Wotr_ProdSpeed", "SP_Req_ProdSpeed" },
	{ "ScavengerSpecialPower", "SP_Wotr_Scavenger", "SP_Req_Scavenger" },
	{ "TaintSpecialPower", "SP_Wotr_Taint", "SP_Req_Taint" },
	{ "UntamedAllegianceSpecialPower", "SP_Wotr_Untamed", "SP_Req_Untamed" },
};

std::string buildSyntheticIni()
{
	std::string ini = kRanks;
	for (const ClassSpec &c : kElevenClasses)
	{
		ini += std::string("SpecialPower ") + c.powerWotr + "\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 100\nEnd\n";
		ini += std::string("SpecialPower ") + c.powerStratReq + "\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 100\nEnd\n";
	}
	ini +=
		"SpecialPower SP_PlusNone\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 100\nEnd\n"
		"SpecialPower SP_Unset\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 100\nEnd\n"
		"Object Port242Citadel\n"
		"  KindOf = STRUCTURE COMMANDCENTER\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000.0\n    MaxHealthDamaged = 500.0\n    MaxHealthReallyDamaged = 250.0\n    InitialHealth = 1000.0\n  End\n"
		"End\n"
		"Object Port242Sanctuary\n"
		"  KindOf = STRUCTURE\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500.0\n    MaxHealthDamaged = 250.0\n    MaxHealthReallyDamaged = 125.0\n    InitialHealth = 500.0\n  End\n"
		"End\n"
		"Object Port242Soldier\n"
		"  KindOf = SELECTABLE INFANTRY\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100.0\n    MaxHealthDamaged = 50.0\n    MaxHealthReallyDamaged = 25.0\n    InitialHealth = 40.0\n  End\n"
		"End\n";

	// Book A: retail spell-book pattern across all 11 classes (skirmish requires COMMANDCENTER, strategic is NONE).
	ini += "Object Port242BookWotrNone\n  KindOf = SPELL_BOOK\n";
	int idx = 0;
	for (const ClassSpec &c : kElevenClasses)
	{
		++idx;
		ini += std::string("  Behavior = ") + c.moduleClass + " ModuleTag_A" + std::to_string(idx) + "\n";
		ini += std::string("    SpecialPowerTemplate = ") + c.powerWotr + "\n";
		ini += "    AvailableAtStart = Yes\n";
		if (std::string(c.moduleClass) == "PlayerHealSpecialPower")
		{
			ini += "    HealAmount = 50.0\n    HealAsPercent = No\n    HealRadius = 200.0\n    HealAffects = INFANTRY\n";
		}
		ini += "    RequirementsFilterMPSkirmish = ANY +COMMANDCENTER\n";
		ini += "    RequirementsFilterStrategic = NONE\n";
		ini += "  End\n";
	}
	ini += "End\n";

	// Book B: reverse pattern across all 11 classes (skirmish is NONE, strategic requires +Port242Sanctuary).
	ini += "Object Port242BookStratBuilding\n  KindOf = SPELL_BOOK\n";
	idx = 0;
	for (const ClassSpec &c : kElevenClasses)
	{
		++idx;
		ini += std::string("  Behavior = ") + c.moduleClass + " ModuleTag_B" + std::to_string(idx) + "\n";
		ini += std::string("    SpecialPowerTemplate = ") + c.powerStratReq + "\n";
		ini += "    AvailableAtStart = Yes\n";
		ini += "    RequirementsFilterMPSkirmish = NONE\n";
		ini += "    RequirementsFilterStrategic = ANY +Port242Sanctuary\n";
		ini += "  End\n";
	}
	ini +=
		"  Behavior = SpecialPowerModule ModuleTag_PlusNone\n"
		"    SpecialPowerTemplate = SP_PlusNone\n"
		"    AvailableAtStart = Yes\n"
		"    RequirementsFilterMPSkirmish = NONE +COMMANDCENTER\n"
		"    RequirementsFilterStrategic = NONE +Port242Sanctuary\n"
		"  End\n"
		"  Behavior = SpecialPowerModule ModuleTag_Unset\n"
		"    SpecialPowerTemplate = SP_Unset\n"
		"    AvailableAtStart = Yes\n"
		"  End\n"
		"End\n";
	return ini;
}

struct Port242Rig
{
	logictest::LogicWorld lw;
	SpellStores stores;

	Port242Rig()
		: lw(RandomAlgorithm::ZH_CarryChain)
		, stores(lw.w.keys)
	{
		REQUIRE_MESSAGE(lw.loadError.empty(), lw.loadError);
		stores.install();
		SpellStores::registerBlocks(lw.w.fx.env.blocks);
		LogicModules::registerAll(lw.w.modules);
		SpecialPowerModules::registerAll(lw.w.modules);
		SpellBookPowers::registerAll(lw.w.modules);
		const std::string err = lw.w.load(buildSyntheticIni(), INI_LOAD_OVERWRITE, "port242.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Port242Rig()
	{
		lw.logic.reset();
		stores.uninstall();
	}

	Player *alice()
	{
		Player *p = lw.players.findPlayerWithName("Alice");
		REQUIRE(p != nullptr);
		return p;
	}
	const SpecialPowerTemplate *power(const char *name)
	{
		const SpecialPowerTemplate *t = stores.specialPowers().findSpecialPowerTemplate(name);
		REQUIRE_MESSAGE(t != nullptr, name);
		return t;
	}
	Object *spawn(const char *tmplName, float x = 0.0f, float y = 0.0f)
	{
		const ThingTemplate *tt = lw.logic->things().findTemplate(tmplName);
		REQUIRE_MESSAGE(tt != nullptr, tmplName);
		Object *o = lw.logic->newObject(tt, alice()->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o != nullptr);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		o->friend_onBuildComplete();
		return o;
	}
};
} // namespace

TEST_CASE("PORT-242 strategic filter: gameKind != 3 selects RequirementsFilterStrategic across all 11 SpecialPowerModule classes (RW 0x8969E5)")
{
	Port242Rig rig;
	Player *alice = rig.alice();
	alice->initScience(SpellGameMode{ true, false }); // LiveGame skirmish / WotR tactical battle spell mode
	Object *book = rig.spawn("Port242BookWotrNone");

	// Skirmish (GameLogic + 0x114 == 3, RW 0x8969EE .. 0x896A09): without a COMMANDCENTER all 11 classes fail RequirementsFilterMPSkirmish.
	rig.lw.logic->economy().context().gameKind = 3;
	for (const ClassSpec &c : kElevenClasses)
	{
		INFO(std::string(c.moduleClass));
		const SpecialPowerTemplate *t = rig.power(c.powerWotr);
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, t);
		REQUIRE(sp != nullptr);
		auto *spm = dynamic_cast<SpecialPowerModule *>(sp);
		REQUIRE(spm != nullptr);
		REQUIRE(static_cast<bool>(spm->spData()->m_requirementsFilterMPSkirmish));
		REQUIRE(static_cast<bool>(spm->spData()->m_requirementsFilterStrategic));
		CHECK(spm->spData()->m_requirementsFilterMPSkirmish->flag);
		CHECK_FALSE(spm->spData()->m_requirementsFilterStrategic->flag); // RW 0x76392F: NONE leaves +0x88 flag = 0
		CHECK_FALSE(sp->requirementsMet());
		CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*book, t));
	}

	// War of the Ring battle (GameLogic + 0x114 == 0 != 3, RW 0x8969EE .. 0x8969FE -> +0x3C, RW 0x896A0E -> RW 0x762977):
	// without any COMMANDCENTER on the field, all 11 classes pass RequirementsFilterStrategic = NONE and the heal power casts.
	rig.lw.logic->economy().context().gameKind = 0;
	for (const ClassSpec &c : kElevenClasses)
	{
		INFO(std::string(c.moduleClass));
		const SpecialPowerTemplate *t = rig.power(c.powerWotr);
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, t);
		REQUIRE(sp != nullptr);
		CHECK(sp->requirementsMet());
		CHECK(SpecialPowerModules::canUseSpecialPower(*book, t));
	}

	Object *soldier = rig.spawn("Port242Soldier", 50.0f, 50.0f);
	REQUIRE(soldier->getBodyModule()->getHealth() == 40.0f);
	const SpecialPowerTemplate *healTmpl = rig.power("SP_Wotr_Heal");
	SpecialPowerModuleInterface *healSp = SpecialPowerModules::findModule(*book, healTmpl);
	REQUIRE(healSp != nullptr);
	REQUIRE(soldier->getBodyModule() != nullptr);
	const Coord3D at{ 50.0f, 50.0f, 0.0f };
	CHECK(SpecialPowerModules::doSpecialPowerAtLocation(*book, healTmpl, at, 0, false));
	CHECK(soldier->getBodyModule()->getHealth() == 90.0f);

	// Neighboring skirmish check: returning to gameKind == 3 still requires COMMANDCENTER until one is built.
	rig.lw.logic->economy().context().gameKind = 3;
	CHECK_FALSE(healSp->requirementsMet());
	rig.spawn("Port242Citadel", 200.0f, 200.0f);
	for (const ClassSpec &c : kElevenClasses)
	{
		INFO(std::string(c.moduleClass));
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, rig.power(c.powerWotr));
		REQUIRE(sp != nullptr);
		CHECK(sp->requirementsMet());
	}
}

TEST_CASE("PORT-242 NONE flag and non-skirmish gates: RW 0x762977 passes NONE in skirmish and gameKind != 3 enforces non-NONE strategic filters (RW 0x8969E5)")
{
	Port242Rig rig;
	Player *alice = rig.alice();
	Object *book = rig.spawn("Port242BookStratBuilding");

	// 1. Skirmish (gameKind == 3, skirmishOrMultiplayer == true): RequirementsFilterMPSkirmish = NONE has +0x88 flag == 0
	//    (retail Imladris SpellBookErestorWisdom / SpellBookManveWisdom), so RW 0x762977 returns false and RW 0x896A15 returns true.
	rig.lw.logic->economy().context().gameKind = 3;
	alice->initScience(SpellGameMode{ true, false });
	for (const ClassSpec &c : kElevenClasses)
	{
		INFO(std::string(c.moduleClass));
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, rig.power(c.powerStratReq));
		REQUIRE(sp != nullptr);
		CHECK(sp->requirementsMet());
	}
	// NONE +COMMANDCENTER sets +0x88 flag = 1 (RW 0x76392F), so RW 0x762977 returns true and requires COMMANDCENTER.
	SpecialPowerModuleInterface *plusNone = SpecialPowerModules::findModule(*book, rig.power("SP_PlusNone"));
	SpecialPowerModuleInterface *unsetSp = SpecialPowerModules::findModule(*book, rig.power("SP_Unset"));
	REQUIRE(plusNone != nullptr);
	REQUIRE(unsetSp != nullptr);
	CHECK_FALSE(plusNone->requirementsMet());
	CHECK(unsetSp->requirementsMet()); // RW 0x896834 -> RW 0x763D11 default has flag == 0

	// 2. Campaign / solo with gameKind == 3 and skirmishOrMultiplayer == false (RW 0x896A00 -> RW 0x625456 == 0 -> RW 0x896A2D):
	//    ignores RequirementsFilterMPSkirmish even when +0x88 flag == 1.
	alice->initScience(SpellGameMode{ false, true });
	CHECK(plusNone->requirementsMet());

	// 3. Strategic gameKind != 3 (RW 0x8969EE .. 0x8969FE skips RW 0x625456): enforces RequirementsFilterStrategic = ANY +Port242Sanctuary
	//    across all 11 classes regardless of skirmishOrMultiplayer, until Port242Sanctuary is owned by the player (RW 0x6ABD0B).
	rig.lw.logic->economy().context().gameKind = 0;
	for (bool mpFlag : { false, true })
	{
		INFO(mpFlag);
		alice->initScience(SpellGameMode{ mpFlag, !mpFlag });
		for (const ClassSpec &c : kElevenClasses)
		{
			INFO(std::string(c.moduleClass));
			SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, rig.power(c.powerStratReq));
			REQUIRE(sp != nullptr);
			CHECK_FALSE(sp->requirementsMet());
		}
		CHECK_FALSE(plusNone->requirementsMet());
		CHECK(unsetSp->requirementsMet());
	}

	rig.spawn("Port242Sanctuary", 300.0f, 300.0f);
	for (const ClassSpec &c : kElevenClasses)
	{
		INFO(std::string(c.moduleClass));
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, rig.power(c.powerStratReq));
		REQUIRE(sp != nullptr);
		CHECK(sp->requirementsMet());
	}
	CHECK(plusNone->requirementsMet());
}
