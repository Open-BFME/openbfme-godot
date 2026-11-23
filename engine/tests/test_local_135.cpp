// OpenBFME. GPL-3.0.
// Local 135 (shared RotWK helpers): unit tests for newly ported shared RotWK helpers across Both editions:
//   - BitFlags<N> single-bit ctor (RW 0x0068FC98, DONOR BFME2 0x0028F5A3), multi-bit ctor (RW 0x00851770, DONOR BFME2 0x0044DEC9),
//     lessByBitOrder comparator (RW 0x0073C692, DONOR BFME2 0x0033ABD1 SparseMatchFinder::MapHelper::operator()),
//     default ctor (RW 0x0064C39A), copy ctor (RW 0x00444D65), count (RW 0x008C2233), countIntersection (RW 0x0073C77B),
//     countInverseIntersection (RW 0x0073C7D2), operator== (RW 0x007628A9);
//   - TeamPrototype::findTeamByID (RW 0x007A0086, DONOR BFME2 0x0039D731) and TeamPrototype::countTeamInstances (RW 0x007A00B0, DONOR BFME2 0x0039D75B);
//   - ScriptEngine::objectListGetNth (RW 0x00603B6A, DONOR BFME2 0x00204224 ObjectTypes::getNthInList);
//   - ModifierListTemplate::getFX (RW 0x00804E32) / getEndFX (RW 0x00804E8A) and AttributeModifierStore::getFX (RW 0x006144FE),
//     getEndFX (RW 0x0061452E), getModelCondition (RW 0x0061455E), getClearModelCondition (RW 0x006145BF), getUpgrade (RW 0x00614620).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/BitFlags.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

// Source of expectations: RW 0x0068FC98 (BitFlags single-bit ctor: -1 leaves mask empty, valid bit sets word[bit>>5] |= 1u<<(bit&31)),
// RW 0x00851770 (BitFlags multi-bit ctor: SpecialAbilityUpdate::clearAbilityModelConditions 14-bit mask),
// RW 0x0073C692 (SparseMatchFinder::MapHelper::operator(): bit-order strict weak ordering from bit 0 upward),
// RW 0x0064C39A (default ctor), RW 0x00444D65 (copy ctor), RW 0x008C2233 (count), RW 0x0073C77B (countIntersection),
// RW 0x0073C7D2 (countInverseIntersection), RW 0x007628A9 (operator==).
TEST_CASE("LOCAL-135: BitFlags single-bit and multi-bit constructors and lessByBitOrder")
{
	BitFlags<608> emptyFromMinusOne(-1);
	CHECK_FALSE(emptyFromMinusOne.any());
	CHECK(emptyFromMinusOne.count() == 0);

	BitFlags<608> single(0x249);
	CHECK(single.test(0x249));
	CHECK(single.count() == 1);

	// RW 0x00851770: the 14 ability model conditions cleared by SpecialAbilityUpdate::clearAbilityModelConditions (RW 0x00851BCF)
	BitFlags<608> multi({ 0x60, 0x5E, 0x29, 0x76, 0x5F, 0x84, 0x61, 0x62, 0x63, 0x249, 0x24A, 0x24B, 0x6E, 0x6F, -1 });
	CHECK(multi.count() == 14);
	CHECK(multi.test(0x29));
	CHECK(multi.test(0x24B));
	CHECK_FALSE(multi.test(0));

	BitFlags<608> copy(multi);
	CHECK(copy == multi);
	CHECK(copy.countIntersection(single) == 1);
	CHECK(single.countInverseIntersection(multi) == 13);

	// RW 0x0073C692: at the first differing bit i, a < b iff b.test(i) is true (a has 0, b has 1).
	BitFlags<128> a({ 5 });
	BitFlags<128> b({ 2, 5 });
	CHECK(a.lessByBitOrder(b));
	CHECK_FALSE(b.lessByBitOrder(a));
	CHECK_FALSE(a.lessByBitOrder(a));
}

// Source of expectations: RW 0x007A0086 (DONOR BFME2 0x0039D731 TeamPrototype::findTeamByID: id == 0 returns nullptr;
// walks prototype's team instance list +0x334 and returns Team* whose m_id == id, or nullptr) and
// RW 0x007A00B0 (DONOR BFME2 0x0039D75B TeamPrototype::countTeamInstances: number of Team instances in +0x334).
TEST_CASE("LOCAL-135: TeamPrototype findTeamByID and countTeamInstances")
{
	TeamFactory factory;
	TeamPrototype *singletonProto = factory.initTeam("PlyrMordor/SingletonGuard", nullptr, true);
	TeamPrototype *multiProto = factory.initTeam("PlyrMordor/ReinforcementWave", nullptr, false);
	REQUIRE(singletonProto != nullptr);
	REQUIRE(multiProto != nullptr);

	// initTeam creates the single instance for a singleton prototype, and 0 instances for a non-singleton prototype.
	CHECK(singletonProto->countTeamInstances() == 1);
	CHECK(multiProto->countTeamInstances() == 0);
	CHECK(multiProto->findTeamByID(0) == nullptr);

	Team *t1 = factory.createTeam(multiProto);
	Team *t2 = factory.createTeam(multiProto);
	REQUIRE(t1 != nullptr);
	REQUIRE(t2 != nullptr);
	CHECK(multiProto->countTeamInstances() == 2);

	CHECK(multiProto->findTeamByID(t1->getID()) == t1);
	CHECK(multiProto->findTeamByID(t2->getID()) == t2);
	// A team ID belonging to another prototype is not found on multiProto.
	Team *singletonTeam = singletonProto->getFirstTeam();
	REQUIRE(singletonTeam != nullptr);
	CHECK(multiProto->findTeamByID(singletonTeam->getID()) == nullptr);
	CHECK(singletonProto->findTeamByID(singletonTeam->getID()) == singletonTeam);
}

// Source of expectations: RW 0x00603B6A (DONOR BFME2 0x00204224 ObjectTypes::getNthInList: returns "" if index < 0 or
// index >= m_objectTypes.size(), else m_objectTypes[index]).
TEST_CASE("LOCAL-135: ScriptEngine objectListGetNth")
{
	combattest::CombatWorld w;
	ScriptEngine &scripts = w.logic->scriptEngine();

	scripts.objectListAdd("MordorInfantryList", "MordorFighterHorde", true);
	scripts.objectListAdd("MordorInfantryList", "MordorArcherHorde", true);
	scripts.objectListAdd("MordorInfantryList", "MordorEasterlingHorde", true);

	CHECK(scripts.objectListGetNth("MordorInfantryList", -1).empty());
	CHECK(scripts.objectListGetNth("MordorInfantryList", 0) == "MordorFighterHorde");
	CHECK(scripts.objectListGetNth("MordorInfantryList", 1) == "MordorArcherHorde");
	CHECK(scripts.objectListGetNth("MordorInfantryList", 2) == "MordorEasterlingHorde");
	CHECK(scripts.objectListGetNth("MordorInfantryList", 3).empty());
	CHECK(scripts.objectListGetNth("UnknownObjectList", 0).empty());
}

// Source of expectations: RW 0x00804E32 (ModifierListTemplate::getFX), RW 0x00804E8A (ModifierListTemplate::getEndFX),
// RW 0x006144FE (AttributeModifierStore::getFX), RW 0x0061452E (AttributeModifierStore::getEndFX),
// RW 0x0061455E (AttributeModifierStore::getModelCondition), RW 0x006145BF (AttributeModifierStore::getClearModelCondition),
// RW 0x00614620 (AttributeModifierStore::getUpgrade).
TEST_CASE("LOCAL-135: ModifierListTemplate and AttributeModifierStore accessors")
{
	UpgradeCenter upgrades;
	FXListStore fxLists;
	AttributeModifierStore store;
	UpgradeCenter *prevUpgrades = TheUpgradeCenter;
	FXListStore *prevFx = TheFXListStore;
	AttributeModifierStore *prevStore = TheAttributeModifierStore;
	TheUpgradeCenter = &upgrades;
	TheFXListStore = &fxLists;
	TheAttributeModifierStore = &store;

	INIEnvironment env;
	upgrades.registerBlock(env.blocks);
	env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
	store.registerBlock(env.blocks);

	const std::string text =
		"FXList FX_Lead1\nEnd\n"
		"FXList FX_Lead2\nEnd\n"
		"FXList FX_Lead3\nEnd\n"
		"FXList FX_EndLead1\nEnd\n"
		"FXList FX_EndLead2\nEnd\n"
		"FXList FX_EndLead3\nEnd\n"
		"FXList FX_Single\nEnd\n"
		"FXList FX_Ignored2\nEnd\n"
		"FXList FX_SingleEnd\nEnd\n"
		"Upgrade Upgrade_TestLeadership\n"
		"  Type = OBJECT\n"
		"End\n"
		"ModifierList MultiLevelLeadership\n"
		"  Category = LEADERSHIP\n"
		"  MultiLevelFX = Yes\n"
		"  FX = FX_Lead1\n"
		"  FX2 = FX_Lead2\n"
		"  FX3 = FX_Lead3\n"
		"  EndFX = FX_EndLead1\n"
		"  EndFX2 = FX_EndLead2\n"
		"  EndFX3 = FX_EndLead3\n"
		"  ModelCondition = HERO\n"
		"  ClearModelCondition = WEAPONSET_HERO_MODE\n"
		"  Upgrade = Upgrade_TestLeadership Delay:1000\n"
		"End\n"
		"ModifierList SingleLevelBuff\n"
		"  Category = SPELL\n"
		"  MultiLevelFX = No\n"
		"  FX = FX_Single\n"
		"  FX2 = FX_Ignored2\n"
		"  EndFX = FX_SingleEnd\n"
		"End\n";
	INI ini(env);
	ini.loadMemory("local135_modifiers.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);

	const int idxMulti = store.findIndex("MultiLevelLeadership");
	const int idxSingle = store.findIndex("SingleLevelBuff");
	REQUIRE(idxMulti == 0);
	REQUIRE(idxSingle == 1);

	// Out-of-range index returns empty defaults (RW 0x006144FE..0x00614620).
	CHECK(store.getFX(-1, 1).empty());
	CHECK(store.getEndFX(99, 1).empty());
	CHECK(store.getModelCondition(-1) == std::array<std::uint32_t, 19>{});
	CHECK(store.getClearModelCondition(-1) == std::array<std::uint32_t, 19>{});
	CHECK(store.getUpgrade(-1) == nullptr);

	// MultiLevelFX = Yes selects slot (categoryCount - 1) clamped to [0, 2] (RW 0x00804E32 / RW 0x00804E8A).
	CHECK(store.getFX(idxMulti, 1) == "FX_Lead1");
	CHECK(store.getFX(idxMulti, 2) == "FX_Lead2");
	CHECK(store.getFX(idxMulti, 3) == "FX_Lead3");
	CHECK(store.getFX(idxMulti, 5) == "FX_Lead3");
	CHECK(store.getEndFX(idxMulti, 1) == "FX_EndLead1");
	CHECK(store.getEndFX(idxMulti, 2) == "FX_EndLead2");
	CHECK(store.getEndFX(idxMulti, 3) == "FX_EndLead3");

	// MultiLevelFX = No always selects slot 0 regardless of categoryCount.
	CHECK(store.getFX(idxSingle, 2) == "FX_Single");
	CHECK(store.getEndFX(idxSingle, 3) == "FX_SingleEnd");
	CHECK(store.getUpgrade(idxSingle) == nullptr);

	// ModelCondition, ClearModelCondition, and Upgrade accessors (RW 0x0061455E, RW 0x006145BF, RW 0x00614620).
	const auto mc = store.getModelCondition(idxMulti);
	const auto cmc = store.getClearModelCondition(idxMulti);
	bool anyMc = false;
	bool anyCmc = false;
	for (size_t wIdx = 0; wIdx < 19; ++wIdx)
	{
		anyMc = anyMc || (mc[wIdx] != 0);
		anyCmc = anyCmc || (cmc[wIdx] != 0);
	}
	CHECK(anyMc);
	CHECK(anyCmc);
	const ModifierListTemplate::UpgradeGrant *ug = store.getUpgrade(idxMulti);
	REQUIRE(ug != nullptr);
	CHECK(ug->upgrade != nullptr);

	TheUpgradeCenter = prevUpgrades;
	TheFXListStore = prevFx;
	TheAttributeModifierStore = prevStore;
}
