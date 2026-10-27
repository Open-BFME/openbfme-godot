// OpenBFME unit tests (lane SUMMON-1): the Thrall Master's summons. SummonReplacementSpecialAbilityUpdate (RW 0x8B1F59, data RW 0x8B21DA / table RW 0xC05AF8)
// and DoCommandUpgrade (RW 0x8B8DAF, table RW 0xC6E7B4): first in a synthetic world (CombatTestUtil's CombatWorld), then with the retail AngmarThrallMaster.
// Expected values are read from the RotWK binary (each check names the address). GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <string>
#include <vector>

namespace
{
struct SummonWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	explicit SummonWorld(const char *objects)
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~SummonWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
};

// the Thrall Master's module as retail writes it (angmarthrallmaster.ini: 1000 ms unpack and preparation, no pack), on a synthetic summoner
const char kSummoner[] =
	"SpecialPower SpecialAbilityTestSummon\n  Enum = SPECIAL_TOGGLE_MOUNTED\n  ReloadTime = 1000\nEnd\n"
	"Object TestThrall\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 300\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule SummonStarterTag\n  SpecialPowerTemplate = SpecialAbilityTestSummon\n  UpdateModuleStartsAttack = Yes\n  StartsPaused = No\n End\n"
	" Behavior = SummonReplacementSpecialAbilityUpdate SummonTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestSummon\n  UnpackTime = 1000\n  PreparationTime = 1000\n  PersistentPrepTime = 0\n  PackTime = 0\n"
	"  AwardXPForTriggering = 0\n  MountedTemplate = TestSummonedHorde\n  IgnoreFacingCheck = Yes\n  MustFinishAbility = Yes\n"
	" End\n"
	" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 30\n End\n"
	"End\n"
	"Object TestSummonedHorde\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 600\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	"End\n"
	// the same summoner on a locomotor whose speed is 0 (the retail Thrall Master's SET_NORMAL_UPGRADED set has Speed = 0)
	"Object TestRootedThrall\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 300\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule SummonStarterTag\n  SpecialPowerTemplate = SpecialAbilityTestSummon\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = SummonReplacementSpecialAbilityUpdate SummonTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestSummon\n  UnpackTime = 1000\n  PreparationTime = 1000\n  MountedTemplate = TestSummonedHorde\n  IgnoreFacingCheck = Yes\n"
	" End\n"
	" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 0\n End\n"
	"End\n";

SummonReplacementSpecialAbilityUpdate *summonOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (auto *u = dynamic_cast<SummonReplacementSpecialAbilityUpdate *>(m.get()))
		{
			return u;
		}
	}
	return nullptr;
}

int conditionIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}

const unsigned kIgnoreAI = 0x4A; // IGNORE_AI_COMMAND (RW 0x62684D(0x4A, ...))
} // namespace

// RW 0x8B21DA / RW 0xC05AF8: ModelConditionSpecialAbilityUpdate's five rows (WhichSpecialPower 1, EmotionPulseRadius 50.0 from RW 0xBD88C4) then
// ToggleMounted's five; a row of neither table is an error
TEST_CASE("summon1: SummonReplacementSpecialAbilityUpdate's data is RW 0xC05AF8 (ModelCondition's rows, then ToggleMounted's)")
{
	SummonWorld f(kSummoner);
	REQUIRE(f.w.load("SpecialPower SpecialAbilityTestSummon2\n  Enum = SPECIAL_TOGGLE_MOUNTED\nEnd\n"
					 "Object FullSummoner\n Body = ActiveBody BodyTag\n  MaxHealth = 10\n End\n Behavior = AIUpdateInterface AiTag\n End\n"
					 " Behavior = SummonReplacementSpecialAbilityUpdate SummonTag\n  SpecialPowerTemplate = SpecialAbilityTestSummon2\n"
					 "  WhichSpecialPower = 2\n  GenerateTerror = Yes\n  GenerateUncontrollableFear = Yes\n  EmotionPulseRadius = 75.5\n  ObjectFilter = ANY +INFANTRY\n"
					 "  OpacityTarget = 0.25\n  TriggerInstantlyOnCreate = Yes\n  CancelDisguiseWhenDismounting = Yes\n  MountedTemplate = TestSummonedHorde\n"
					 "  SynchronizeTimerOnSpecialPower = SpecialAbilityTestSummon SpecialAbilityTestSummon2\n End\nEnd\n")
				.empty());
	Object *plain = f.unit("TestThrall", 'A', 300, 300);
	REQUIRE(plain);
	SummonReplacementSpecialAbilityUpdate *u = summonOf(*plain);
	REQUIRE(u);
	const auto *d = dynamic_cast<const SummonReplacementSpecialAbilityUpdateModuleData *>(u->abilityData());
	REQUIRE(d);
	CHECK(d->m_whichSpecialPower == 1);         // RW 0x8B2204: + 0xD0 = 1
	CHECK(d->m_emotionPulseRadius == 50.0f);    // RW 0x8B221C: + 0xD8 = [RW 0xBD88C4]
	CHECK_FALSE(d->m_generateTerror);
	CHECK_FALSE(d->m_triggerInstantlyOnCreate); // + 0xE4 = 0
	CHECK(d->m_mountedTemplate == "TestSummonedHorde");
	CHECK(d->m_unpackFrames == 5);              // 1000 ms (RW 0x73A429)
	CHECK(d->m_preparationFrames == 5);

	const ThingTemplate *full = f.w.things.findTemplate("FullSummoner");
	REQUIRE(full);
	const SummonReplacementSpecialAbilityUpdateModuleData *fd = nullptr;
	for (const ThingTemplate::Nugget &mt : full->behaviorModules().nuggets())
	{
		if (const auto *x = dynamic_cast<const SummonReplacementSpecialAbilityUpdateModuleData *>(mt.data.get()))
		{
			fd = x;
		}
	}
	REQUIRE(fd);
	CHECK(fd->m_whichSpecialPower == 2);
	CHECK(fd->m_generateTerror);
	CHECK(fd->m_generateUncontrollableFear);
	CHECK(fd->m_emotionPulseRadius == 75.5f);
	CHECK(fd->m_objectFilter.get() != nullptr); // tolerance: presence of the parsed filter (RW 0x76392F), not a value
	CHECK(fd->m_opacityTarget == 0.25f);
	CHECK(fd->m_triggerInstantlyOnCreate);
	CHECK(fd->m_cancelDisguiseWhenDismounting);
	CHECK(fd->m_synchronizeTimerOnSpecialPower == std::vector<std::string>{ "SpecialAbilityTestSummon", "SpecialAbilityTestSummon2" });

	const std::string err = f.w.load("Object BadSummoner\n Behavior = SummonReplacementSpecialAbilityUpdate SummonTag\n  SpecialPowerTemplate = SpecialAbilityTestSummon\n"
									 "  DisguiseAsTemplate = TestSummonedHorde\n End\nEnd\n");
	CHECK_FALSE(err.empty()); // SpecialDisguiseUpdate's row (RW 0xC05C10) is not in RW 0xC05AF8
}

// RW 0x8B1FDA (the initiate: IGNORE_AI_COMMAND on), RW 0x89455B (SPECIAL_POWER_1 at the first unpack: WhichSpecialPower 1 = Object + 0x124 bit 19), RW 0x8B2584 /
// 0x8B228C (the trigger replaces the summoner by MountedTemplate with its name, health and team), RW 0x8B202A (the summoner leaves after the packing)
TEST_CASE("summon1: the summoner blows the horn (1 s unpack, 1 s preparation) and MountedTemplate replaces it (RW 0x8B2584 / 0x8B228C)")
{
	SummonWorld f(kSummoner);
	Object *thrall = f.unit("TestThrall", 'A', 300, 300);
	REQUIRE(thrall);
	f.frames(2);
	thrall->setName("TheThrallMaster");
	DamageInfo hurt;
	hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hurt.m_input.m_amount = 50.0f;
	thrall->attemptDamage(hurt);
	const float health = thrall->getBodyModule()->getHealth();
	CHECK(health == 250.0f);
	SummonReplacementSpecialAbilityUpdate *u = summonOf(*thrall);
	REQUIRE(u);
	const ObjectID thrallId = thrall->getID();
	Team *team = thrall->getTeam();
	const int specialPower1 = (0x124 - 0x10C) * 8 + 19; // RW 0x89455B: WhichSpecialPower 1 -> Object + 0x124 bit 19
	REQUIRE(conditionIndex("SPECIAL_POWER_1") == specialPower1);
	CHECK_FALSE(thrall->testStatus(kIgnoreAI));
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	CHECK(thrall->testStatus(kIgnoreAI)); // RW 0x8B201F
	CHECK(u->isActive());
	f.frames(1);
	CHECK(thrall->testModelCondition(specialPower1));
	int frames = 1;
	while (f.logic->findObjectByID(thrallId) && frames < 40)
	{
		f.frames(1);
		++frames;
	}
	REQUIRE(f.logic->findObjectByID(thrallId) == nullptr);
	MESSAGE("summon1: the summoner left after " << frames << " frames");
	ObjectID repId = INVALID_ID;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == thrallId) // Construction::buildObjectNow: the summoner is the producer
		{
			repId = o->getID();
		}
	}
	REQUIRE(repId != INVALID_ID);
	Object *rep = f.logic->findObjectByID(repId);
	REQUIRE(rep);
	CHECK(rep->getTemplate()->getName() == "TestSummonedHorde");
	CHECK(rep->getName() == "TheThrallMaster");
	CHECK(rep->getTeam() == team);
	CHECK(rep->getBodyModule()->getHealth() == health); // body vslot 0xAC with the old body's health: 250 of the new 600
}

// RW 0x8B1FDA: with a current locomotor whose max speed for the condition (RW 0x5E3F49) is 0 the initiate is refused: nothing starts, no IGNORE_AI_COMMAND
TEST_CASE("summon1: a summoner whose locomotor speed is 0 refuses the initiate (RW 0x8B1FDA)")
{
	SummonWorld f(kSummoner);
	Object *rooted = f.unit("TestRootedThrall", 'A', 300, 300);
	REQUIRE(rooted);
	f.frames(2);
	REQUIRE(rooted->getAIUpdateInterface());
	REQUIRE(rooted->getAIUpdateInterface()->curLocomotor());
	CHECK(rooted->getAIUpdateInterface()->curLocomotorSpeed() == 0.0f);
	SummonReplacementSpecialAbilityUpdate *u = summonOf(*rooted);
	REQUIRE(u);
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	CHECK_FALSE(u->isActive());
	CHECK_FALSE(rooted->testStatus(kIgnoreAI));
	f.frames(15);
	CHECK(f.logic->findObjectByID(rooted->getID()) == rooted);
	CHECK(u->summons() == 0u);
}

// lane SUMMON-1: the module's state is in the snapshot (versioned); a truncated record throws
TEST_CASE("summon1: SummonReplacementSpecialAbilityUpdate saves and loads its state (xfer version 1)")
{
	SummonWorld f(kSummoner);
	Object *thrall = f.unit("TestThrall", 'A', 300, 300);
	REQUIRE(thrall);
	f.frames(2);
	SummonReplacementSpecialAbilityUpdate *u = summonOf(*thrall);
	REQUIRE(u);
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(3);
	StateHasher before;
	u->crc(before);
	XferSave saved;
	u->xfer(saved);
	const int packing = u->packingState();
	f.frames(3); // the state moves on
	StateHasher moved;
	u->crc(moved);
	CHECK(moved.value() != before.value());
	{
		XferLoad l(saved.data());
		u->xfer(l);
	}
	StateHasher after;
	u->crc(after);
	CHECK(after.value() == before.value());
	CHECK(u->packingState() == packing);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(u->xfer(lt), XferException);
}

// ---- retail -------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct ThrallGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	spelltest::GameAI ai;
	starttest::Shared &s;

	explicit ThrallGame(starttest::Shared &shared)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		const char *factions[2] = { "FactionAngmar", "FactionMen" };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishPlayer p;
			p.name = "t" + std::to_string(i);
			p.faction = factions[i];
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		logic->random().seedRandom(31);
		ai.attach(shared, *logic);
	}
	~ThrallGame() { logic->reset(); }
	Player *player(int i) { return players.findPlayerWithName("t" + std::to_string(i)); }
	Object *place(const char *name, int i, float x, float y)
	{
		const ThingTemplate *t = s.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		Object *o = logic->newObject(t, player(i)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		o->friend_onBuildComplete();
		return o;
	}
};

bool hasKind(const Object &o, const char *kind)
{
	const int k = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	return k >= 0 && o.isKindOf((unsigned)k);
}
} // namespace

// the retail chain: the Thrall Master's OBJECT upgrade (Upgrade_ThrallMasterOrcWarriors ...) reaches StatusBitsUpgrade (IGNORE_AI_COMMAND,
// SUMMONING_REPLACEMENT), CommandSetUpgrade (EmptyCommandSet) and DoCommandUpgrade, whose button (Command_SpecialAbilityAngmarThrallMaster...) runs the
// SpecialPowerModule (RW 0x8B8E2E -> RW 0x696FD2 -> RW 0x68E5A3); the SummonReplacementSpecialAbilityUpdate blows the horn and the horde of MountedTemplate
// replaces him, with his name and team
TEST_CASE("summon1 retail: each Thrall Master upgrade summons its horde through DoCommandUpgrade and SummonReplacementSpecialAbilityUpdate")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	struct Summon
	{
		const char *upgrade, *horde;
	};
	const Summon summons[] = { { "Upgrade_ThrallMasterOrcWarriors", "AngmarOrcWarriors" },
							   { "Upgrade_ThrallMasterWolfRiders", "AngmarWolfRiders" },
							   { "Upgrade_ThrallMasterRhudaurSpearmen", "AngmarRhudaurSpearmen" },
							   { "Upgrade_ThrallMasterRhudaurSlingers", "AngmarRhudaurSlingers" } };
	for (const Summon &sm : summons)
	{
		INFO(std::string(sm.upgrade));
		ThrallGame g(*shared);
		Object *thrall = g.place("AngmarThrallMaster", 0, 3000.0f, 3000.0f);
		for (int i = 0; i < 3; ++i)
		{
			g.logic->runLogicFrame();
		}
		thrall->setName("Thrall");
		const ObjectID id = thrall->getID();
		Team *team = thrall->getTeam();
		SummonReplacementSpecialAbilityUpdate *u = summonOf(*thrall);
		REQUIRE(u);
		thrall->giveUpgrade(sm.upgrade);
		CHECK(thrall->testStatus(kIgnoreAI)); // StatusBitsUpgrade's StatusToSet, then the initiate's RW 0x8B201F
		CHECK(thrall->getCommandSetOverride() == "EmptyCommandSet");
		int frames = 0;
		while (g.logic->findObjectByID(id) && frames < 60)
		{
			g.logic->runLogicFrame();
			++frames;
		}
		CHECK(g.logic->findObjectByID(id) == nullptr);
		MESSAGE("summon1 retail: " << std::string(sm.upgrade) << ": the Thrall Master left after " << frames << " frames");
		Object *horde = nullptr;
		int members = 0;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == sm.horde && o->getProducerID() == id)
			{
				horde = o;
			}
		}
		REQUIRE_MESSAGE(horde, sm.horde);
		CHECK(hasKind(*horde, "HORDE"));
		CHECK(horde->getName() == "Thrall");
		CHECK(horde->getTeam() == team);
		CHECK(horde->getControllingPlayer() == g.player(0));
		for (int i = 0; i < 5; ++i)
		{
			g.logic->runLogicFrame();
		}
		if (ContainModuleInterface *c = horde->getContain())
		{
			members = (int)c->getContainCount();
		}
		MESSAGE("summon1 retail: " << std::string(sm.horde) << " has " << members << " members");
		CHECK(members > 0); // tolerance: the member count is the horde's own (its HordeContain's InitialPayload), not this module's
	}
}

// RW 0x8B8E2E: a DoCommandUpgrade whose button is of a type the port does not run (PLAYER_UPGRADE: the Hall of Twilight's technologies) is counted and noted
TEST_CASE("summon1: DoCommandUpgrade runs its SPECIAL_POWER button; the other command types are S-5145")
{
	const std::vector<std::string> lines = UpgradeModuleClasses::stopLines();
	bool found = false;
	for (const std::string &l : lines)
	{
		found = found || l.rfind("[S-5145]", 0) == 0;
	}
	CHECK(found);
}

// ---- the ObjectCreationList's CreateObject path (RW 0x5F275D / 0x5F0EE6 / 0x5F1B5A) ------------------------------------------------------------------------
namespace
{
struct OCLWorld : combattest::CombatWorld
{
	ObjectCreationListStore store{ w.keys };
	ObjectCreationListStore *saved = TheObjectCreationListStore;
	explicit OCLWorld(const char *lists, const char *objects = "")
		: combattest::CombatWorld(objects)
	{
		TheObjectCreationListStore = &store;
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		const std::string err = w.load(lists, INI_LOAD_OVERWRITE, "objectcreationlist.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~OCLWorld() { TheObjectCreationListStore = saved; }
	const ObjectCreationList &list(const char *name)
	{
		const ObjectCreationList *l = store.findObjectCreationList(name);
		REQUIRE_MESSAGE(l, name);
		return *l;
	}
	// the lines of the logic random calls `fn` makes from ObjectCreationList.cpp and TerrainLogic.cpp (FindPositionAround), in call order (the object
	// creation's own draws, GameLogic.cpp's seed and the modules', are left out)
	template <class Fn>
	std::vector<int> drawLines(Fn fn)
	{
		logic->random().clearCallLog();
		logic->random().enableCallLog(true);
		fn();
		logic->random().enableCallLog(false);
		std::vector<int> lines;
		for (const auto &c : logic->random().callLog())
		{
			if (c.file == "ObjectCreationList.cpp" || c.file == "TerrainLogic.cpp")
			{
				lines.push_back(c.line);
			}
		}
		return lines;
	}
};

const char kLists[] =
	"ObjectCreationList OCL_Plain\n  CreateObject\n    ObjectNames = Dummy\n    Count = 2\n  End\nEnd\n"
	"ObjectCreationList OCL_Around\n  CreateObject\n    ObjectNames = Dummy\n    Count = 3\n    Disposition = SPAWN_AROUND\n"
	"    DispositionIntensity = 100\n    DispositionAngle = 90\n  End\nEnd\n"
	"ObjectCreationList OCL_Health\n  CreateObject\n    ObjectNames = Swordsman\n    Disposition = LIKE_EXISTING\n    MinHealth = 50%\n    MaxHealth = 50%\n"
	"    UseJustBuiltFlag = Yes\n    JustBuiltDuration = 2000\n  End\nEnd\n"
	"ObjectCreationList OCL_Fields\n  CreateObject\n    ObjectNames = Dummy Swordsman\n    Disposition = ABSOLUTE_ANGLE USE_CLIFF\n    DispositionIntensity = 10\n"
	"    DispositionAngle = 180\n    MinLifetime = 1000\n    MaxLifetime = 2000\n    OrientationOffset = 90\n    MinHealth = 25%\n    InvulnerableTime = 3000\n"
	"    RequiredUpgrades = Upgrade_A Upgrade_B\n    SpreadFormation = Yes\n    MinDistanceAFormation = 20\n    MinDistanceBFormation = 30\n    MaxDistanceFormation = 60\n"
	"  End\nEnd\n"
	"ObjectCreationList OCL_Live\n  CreateObject\n    ObjectNames = Dummy\n    RequiresLivePlayer = Yes\n  End\nEnd\n"
	"ObjectCreationList OCL_Flying\n  CreateObject\n    ObjectNames = Swordsman\n    Disposition = SEND_IT_FLYING\n    DispositionIntensity = 5\n  End\nEnd\n"
	"ObjectCreationList OCL_Spread\n  CreateObject\n    ObjectNames = Dummy\n    Count = 2\n    Disposition = LIKE_EXISTING\n    SpreadFormation = Yes\n"
	"    MinDistanceAFormation = 20\n    MinDistanceBFormation = 30\n    MaxDistanceFormation = 60\n    IgnoreAllObjects = Yes\n  End\nEnd\n"
	"ObjectCreationList OCL_Secondary\n  CreateObject\n    ObjectNames = Dummy\n    Disposition = LIKE_EXISTING\n    OrientInSecondaryDirection = Yes\n  End\nEnd\n"
	"ObjectCreationList OCL_Debris\n  CreateDebris\n    ModelNames = SomeModel\n  End\nEnd\n";
} // namespace

TEST_CASE("summon1 OCL: a created structure owns its footprint at its final placement")
{
	OCLWorld f(
		"ObjectCreationList OCL_Structure\n CreateObject\n ObjectNames = SummonedStructure\n OrientInSecondaryDirection = Yes\n End\nEnd\n",
		"Object SummonedStructure\n KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
		" Geometry = BOX\n GeometryMajorRadius = 20\n GeometryMinorRadius = 10\n GeometryHeight = 20\n GeometryIsSmall = No\n"
		" Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n");
	Object *source = f.unit("Swordsman", 'A', 200, 200);
	bool rotated = false;
	SUBCASE("initial orientation") {}
	SUBCASE("secondary facing") { rotated = true; }
	const Coord3D secondary{500, 600, 0};
	const auto made = f.list("OCL_Structure").create(*f.logic, source, Coord3D{500, 500, 0}, rotated ? &secondary : nullptr);
	REQUIRE(made.size() == 1);
	const PathfindCell *cell = f.ai->pathfinder().getCell(LAYER_GROUND, made[0]->getPosition());
	REQUIRE(cell);
	CHECK(cell->isObstaclePresent(made[0]->getID()));
	const Coord3D alongX{515, 505, 0}, alongY{505, 515, 0};
	CHECK(f.ai->pathfinder().getCell(LAYER_GROUND, &alongX)->isObstaclePresent(made[0]->getID()) == !rotated);
	CHECK(f.ai->pathfinder().getCell(LAYER_GROUND, &alongY)->isObstaclePresent(made[0]->getID()) == rotated);
	const Coord3D origin{0, 0, 0};
	const PathfindCell *originCell = f.ai->pathfinder().getCell(LAYER_GROUND, &origin);
	REQUIRE(originCell);
	CHECK_FALSE(originCell->isObstaclePresent(made[0]->getID()));
	f.logic->destroyObject(made[0]);
	f.frames(1); // destruction and footprint removal are deferred to the frame tail
	CHECK_FALSE(cell->getType() == PathfindCell::CELL_OBSTACLE);
}

// RW 0xBF6E50 / 0xBF74F8: DispositionIntensity is a velocity (RW 0x73A4B6: per second * 0.2), the angles degrees (RW 0x42EE15), the lifetimes durations
// (RW 0x73A429: 5 frames a second), the health a percent (RW 0x42EEFA); the defaults are RW 0x5F0689's
TEST_CASE("summon1 OCL: the CreateObject rows parse with the binary's parsers and defaults (RW 0xBF6E50 / 0xBF74F8)")
{
	OCLWorld f(kLists);
	const OCLNugget &plain = f.list("OCL_Plain").nuggets().at(0);
	CHECK(plain.count == 2);
	CHECK(plain.disposition == (unsigned)OCLNugget::ON_GROUND_ALIGNED);
	CHECK(plain.velocityScale == 1.0f);
	CHECK(plain.minHealth == 1.0f);
	CHECK(plain.maxHealth == 1.0f);
	CHECK(plain.justBuiltDuration == 1u);
	CHECK(plain.preserveLayer);
	CHECK(plain.issueMoveAfterCreation);
	const OCLNugget &n = f.list("OCL_Fields").nuggets().at(0);
	CHECK(n.objectNames == std::vector<std::string>{ "Dummy", "Swordsman" });
	CHECK(n.disposition == (unsigned)(OCLNugget::ABSOLUTE_ANGLE | OCLNugget::USE_CLIFF));
	CHECK(n.dispositionIntensity == 2.0f);           // 10 * 0.2
	CHECK(n.dispositionAngle == 3.14159274101257324f); // 180 degrees (float pi)
	CHECK(n.orientationOffset == 1.57079637050628662f); // 90 degrees
	CHECK(n.minLifetime == 5u);
	CHECK(n.maxLifetime == 10u);
	CHECK(n.minHealth == 0.25f);
	CHECK(n.invulnerableTime == 15u);
	CHECK(n.requiredUpgrades == std::vector<std::string>{ "Upgrade_A", "Upgrade_B" });
	CHECK(n.spreadFormation);
	CHECK(n.minDistanceAFormation == 20.0f);
	CHECK(n.minDistanceBFormation == 30.0f);
	CHECK(n.maxDistanceFormation == 60.0f);
}

// RW 0x5F0EE6 / 0x5F1B5A: per object the name pick (line 0x686), the health (0x50A, drawn even for 1.0 .. 1.0) and, with the default ON_GROUND_ALIGNED,
// the angle (0x5B6); the objects are made on the source player's default team, the source is their producer
TEST_CASE("summon1 OCL: a plain CreateObject draws the name, the health and the ground angle per object, in the binary's order")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	f.frames(2);
	std::vector<Object *> made;
	const std::vector<int> lines = f.drawLines([&] { made = f.list("OCL_Plain").create(*f.logic, source, Coord3D{ 500.0f, 500.0f, 0.0f }); });
	CHECK(lines == std::vector<int>{ 0x686, 0x50A, 0x5B6, 0x686, 0x50A, 0x5B6 });
	REQUIRE(made.size() == 2);
	for (Object *o : made)
	{
		CHECK(o->getTeam() == source->getControllingPlayer()->getDefaultTeam());
		CHECK(o->getProducerID() == source->getID());
		CHECK(o->getPosition()->x == 500.0f);
		CHECK(o->getPosition()->y == 500.0f);
		CHECK(o->getPosition()->z == f.logic->getGroundHeight(500.0f, 500.0f));
	}
}

// RW 0x5F1FE4: SPAWN_AROUND puts object i at angle i * DispositionAngle on the circle of radius DispositionIntensity (20 = 100 * 0.2): (c * r - s * 0) + x,
// (r * s + c * 0) + y, facing the angle
TEST_CASE("summon1 OCL: SPAWN_AROUND spreads the objects on the DispositionIntensity circle at index * DispositionAngle (RW 0x5F1FE4)")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	const std::vector<Object *> made = f.list("OCL_Around").create(*f.logic, source, Coord3D{ 500.0f, 500.0f, 0.0f });
	REQUIRE(made.size() == 3);
	CHECK(made[0]->getPosition()->x == 520.0f); // index 0: angle 0, (20, 0)
	CHECK(made[0]->getPosition()->y == 500.0f);
	CHECK(made[0]->getOrientation() == 0.0f);
	const float a1 = 1.57079637050628662f, a2 = 3.14159274101257324f; // 1 * 90 and 2 * 90 degrees
	CHECK(made[1]->getOrientation() == a1);
	CHECK(made[2]->getOrientation() == a2);
	// cos / sin of the float angle (MSVCR71 on the double), the products and sums in float: cos(pi/2) * 20 = -8.74e-07 and sin(pi) * 20 = -1.75e-06 vanish
	// against 500 (a float's step there is 3.05e-05)
	CHECK(made[1]->getPosition()->x == 500.0f);
	CHECK(made[1]->getPosition()->y == 520.0f);
	CHECK(made[2]->getPosition()->x == 480.0f);
	CHECK(made[2]->getPosition()->y == 500.0f);
}

// RW 0x5F1E70 / 0x8C47DE: MinHealth = MaxHealth = 50% -> the health is 0.5 of the revive reference (the initial health); UseJustBuiltFlag sets JUST_BUILT
// (0xDA) for JustBuiltDuration (RW 0x68B581)
TEST_CASE("summon1 OCL: MinHealth / MaxHealth set the object's health (body vslot 0x58) and UseJustBuiltFlag JUST_BUILT")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	const std::vector<Object *> made = f.list("OCL_Health").create(*f.logic, source, Coord3D{ 450.0f, 400.0f, 0.0f });
	REQUIRE(made.size() == 1);
	CHECK(made[0]->getBodyModule()->getHealth() == 50.0f); // MaxHealth 100 * 0.5
	CHECK(made[0]->testModelCondition(0xDA));
	CHECK(conditionIndex("JUST_BUILT") == 0xDA);
}

// RW 0x5F0F94: RequiresLivePlayer makes nothing for a defeated source player (RW 0x6AAC52), nor without a source
TEST_CASE("summon1 OCL: RequiresLivePlayer needs a source whose player is active (RW 0x5F0F94)")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	CHECK(f.list("OCL_Live").create(*f.logic, source, Coord3D{ 450.0f, 400.0f, 0.0f }).size() == 1);
	CHECK(f.list("OCL_Live").create(*f.logic, nullptr, Coord3D{ 450.0f, 400.0f, 0.0f }).empty());
	source->getControllingPlayer()->setDefeated(true);
	CHECK(f.list("OCL_Live").create(*f.logic, source, Coord3D{ 450.0f, 400.0f, 0.0f }).empty());
}

// RW 0x5F2496 .. 0x5F2530: SEND_IT_FLYING draws x, y in [-4i, 4i] (0x5E7, 0x5E8) and z in [3i * 0.33, 3i] (0x5E9) with i = 5 * 0.2 = 1, after the name and
// the health, and flings the object through its PhysicsBehavior (RW 0x792DBD)
TEST_CASE("summon1 OCL: SEND_IT_FLYING draws its force (lines 0x5E7 .. 0x5E9) and flings the object")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	std::vector<Object *> made;
	f.logic->random().clearCallLog();
	f.logic->random().enableCallLog(true);
	made = f.list("OCL_Flying").create(*f.logic, source, Coord3D{ 450.0f, 400.0f, 0.0f });
	f.logic->random().enableCallLog(false);
	std::vector<int> ocl;
	std::vector<std::pair<float, float>> bounds;
	for (const auto &c : f.logic->random().callLog())
	{
		if (c.file == "ObjectCreationList.cpp")
		{
			ocl.push_back(c.line);
			if (c.real)
			{
				bounds.emplace_back(c.rlo, c.rhi);
			}
		}
	}
	CHECK(ocl == std::vector<int>{ 0x686, 0x50A, 0x5E7, 0x5E8, 0x5E9 });
	REQUIRE(bounds.size() == 4);
	CHECK(bounds[1] == std::make_pair(-4.0f, 4.0f));
	CHECK(bounds[2] == std::make_pair(-4.0f, 4.0f));
	CHECK(bounds[3] == std::make_pair(0.99f, 3.0f)); // 3 * 0.33 (RW 0xBF7348), 3
	REQUIRE(made.size() == 1);
	PhysicsBehavior *p = PhysicsBehavior::find(*made[0]);
	REQUIRE(p);
	CHECK_FALSE(p->flightPoints().empty());
}

// RW 0x5F14DA: SpreadFormation draws the minimum radius between MinDistanceA / B (line 0x719) and finds a spot from the position (TerrainLogic's random
// start angle next); each object stands within MaxDistanceFormation of it
TEST_CASE("summon1 OCL: SpreadFormation draws the ring radius (line 0x719) and puts each object on a free spot around the position")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	f.frames(2);
	std::vector<Object *> made;
	// (300, 300): inside the synthetic map's extent (a centre outside it is the result at once, without the start angle draw)
	std::vector<int> lines = f.drawLines([&] { made = f.list("OCL_Spread").create(*f.logic, source, Coord3D{ 300.0f, 300.0f, 0.0f }); });
	REQUIRE(made.size() == 2);
	REQUIRE(lines.size() >= 4); // tolerance: the name pick, the radius, then TerrainLogic's start angle draw before the per-object draws
	CHECK(lines[0] == 0x686);
	CHECK(lines[1] == 0x719);
	CHECK(lines[2] == 0x113E); // RW 0x68592D: GameLogicRandomValueReal(0, 2 pi) "TerrainLogic.cpp" 0x113E
	for (Object *o : made)
	{
		const float dx = o->getPosition()->x - 300.0f, dy = o->getPosition()->y - 300.0f;
		const float d2 = dx * dx + dy * dy;
		CHECK(d2 >= 400.0f * 0.999f); // tolerance: the ring radius is drawn in [20, 30], the search steps 5 out to 60
		CHECK(d2 <= 3600.0f * 1.001f); // tolerance: as above
	}
}

// RW 0x5F28F8: OrientInSecondaryDirection turns each object to the secondary position (atan2, RW 0x644FD0)
TEST_CASE("summon1 OCL: OrientInSecondaryDirection faces the secondary position (RW 0x5F28F8)")
{
	OCLWorld f(kLists);
	Object *source = f.unit("Swordsman", 'A', 400, 400);
	REQUIRE(source);
	const Coord3D secondary{ 500.0f, 600.0f, 0.0f };
	const std::vector<Object *> made = f.list("OCL_Secondary").create(*f.logic, source, Coord3D{ 500.0f, 500.0f, 0.0f }, &secondary);
	REQUIRE(made.size() == 1);
	CHECK(made[0]->getOrientation() == 1.57079637050628662f); // atan2(100, 0)
}

// S-530: a nugget kind the port does not run is counted and noted
TEST_CASE("summon1 OCL: a CreateDebris nugget is counted and noted as S-530")
{
	OCLWorld f(kLists);
	const ObjectCreationList &l = f.list("OCL_Debris");
	const unsigned long long before = l.m_unportedUses;
	CHECK(l.create(*f.logic, nullptr, Coord3D{ 1.0f, 1.0f, 0.0f }).empty());
	CHECK(l.m_unportedUses == before + 1);
	bool noted = false;
	for (const std::string &s : f.logic->report().stops)
	{
		noted = noted || s.find("[S-530] ObjectCreationList: not run: CreateDebris") != std::string::npos;
	}
	CHECK(noted);
}
