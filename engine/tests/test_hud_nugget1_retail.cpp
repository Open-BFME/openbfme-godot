// OpenBFME retail tests for lane NUGGET-1: the weapon nuggets whose effect had no port (S-321), each fired with the retail weapons that use it (the census of
// RotWK 2.01's weapon.ini). They share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL
// are unset. Every expected value comes from the retail data (the INI numbers quoted at each check) or the binary (the RW address quoted), not from a run.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/LiveScripting.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("nugget1 retail");
}

// two enemy sides on a flat arena with the retail economy, settings and AI data (the DECOMP-1 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;
	std::unique_ptr<LiveScripting> scripting;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.partition().setRegion(0.0f, 0.0f, 1200.0f, 1200.0f);
		logic.combat().setAutoAcquireEnabled(false);
	}
	~Arena()
	{
		if (scripting)
		{
			scripting->detach();
		}
		logic.reset();
		ai.reset();
	}
	// the script engine of the live game (Scripts.lua / ScriptEvents.xml)
	void startScripting(SharedWorld &s)
	{
		std::string err;
		scripting = std::make_unique<LiveScripting>(logic, *s.mount->fs, s.world->nameKeys());
		REQUIRE_MESSAGE(scripting->start("", &err), err);
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle = 0.0f)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		return o;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic.runLogicFrame();
		}
	}
	std::vector<Object *> objectsOf(const char *templateName)
	{
		std::vector<Object *> out;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == templateName && !o->isDestroyed())
			{
				out.push_back(o);
			}
		}
		return out;
	}
};

const WeaponTemplate &weapon(const char *name)
{
	REQUIRE(TheWeaponStore);
	const WeaponTemplate *w = TheWeaponStore->findWeaponTemplate(name);
	REQUIRE_MESSAGE(w != nullptr, "retail weapon " << name);
	return *w;
}

// a weapon holding only the nuggets of `kind` of a retail weapon (the same nugget objects, the same weapon flags)
WeaponTemplate only(const WeaponTemplate &w, WeaponNuggetKind kind)
{
	WeaponTemplate t = w;
	t.m_nuggets.clear();
	for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
	{
		if (n->kind() == kind)
		{
			t.m_nuggets.push_back(n);
		}
	}
	REQUIRE(!t.m_nuggets.empty());
	return t;
}

int nameIndex(const char *const *names, const char *name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (std::strcmp(names[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

std::uint32_t cash(Player *p)
{
	return p->getMoney()->countMoney();
}
} // namespace

// StealMoneyNugget (RW 0x9118B1): CreateAHeroStealMoney's level-1 nugget (AmountStolenPerAttack = CREATE_A_HERO_STEAL_MONEY_AMOUNT_L1 = 1000, createaherogamedata.inc)
// on a GondorFarm (CREATE_A_HERO_STEAL_MONEY_OBJECT_FILTER) takes 1000 from the farm's player and gives it to the thief's; a player with less loses what he has
// (Money::withdraw caps it, RW 0x7B17EF) and the thief gets exactly that. Without the level upgrade (RequiredUpgradeNames) nothing moves
TEST_CASE("nugget1 steal money: a create-a-hero thief takes 1000 from a farm's owner, at most what he has, only with the level-1 upgrade")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &w = weapon("CreateAHeroStealMoney");
	Object *thief = a.place("MordorFighter", 0, 600.0f, 600.0f);
	Object *farm = a.place("GondorFarm", 1, 640.0f, 600.0f);
	a.frames(2);
	Player *robbed = a.player(1);
	Player *thiefP = a.player(0);
	robbed->depositMoney(2500, false);
	const std::uint32_t mine = cash(thiefP);
	DeliverNuggets(a.logic, thief->getID(), w, WeaponBonus{}, farm, farm->getPosition(), false, nullptr);
	CHECK(cash(robbed) == 2500u); // no Upgrade_CreateAHeroStealMoney_Level1 yet
	CHECK(cash(thiefP) == mine);
	thief->giveUpgrade("Upgrade_CreateAHeroStealMoney_Level1");
	DeliverNuggets(a.logic, thief->getID(), w, WeaponBonus{}, farm, farm->getPosition(), false, nullptr);
	CHECK(cash(robbed) == 1500u);
	CHECK(cash(thiefP) == mine + 1000u);
	DeliverNuggets(a.logic, thief->getID(), w, WeaponBonus{}, farm, farm->getPosition(), false, nullptr);
	DeliverNuggets(a.logic, thief->getID(), w, WeaponBonus{}, farm, farm->getPosition(), false, nullptr);
	CHECK(cash(robbed) == 0u);
	CHECK(cash(thiefP) == mine + 2500u);
	// not a farm (the SpecialObjectFilter): nothing
	Object *barracks = a.place("GondorBarracks", 1, 700.0f, 700.0f);
	robbed->depositMoney(800, false);
	DeliverNuggets(a.logic, thief->getID(), w, WeaponBonus{}, barracks, barracks->getPosition(), false, nullptr);
	CHECK(cash(robbed) == 800u);
}

// OpenGateNugget (RW 0x9115BB -> 0x911516): Gloin's Shake Foundation (DwarvenGloinShakeFoundationWeapon: "Force open any gate hit by this") opens a closed, settled
// gate: GateOpenAndCloseBehavior state CLOSED (3) -> OPENING (0) (RW 0x89C97A); an open gate stays open
TEST_CASE("nugget1 open gate: Shake Foundation forces a closed gate open")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionDwarves", "FactionAngmar");
	const WeaponTemplate &w = only(weapon("DwarvenGloinShakeFoundationWeapon"), NUGGET_OPEN_GATE);
	Object *gloin = a.place("DwarvenGloin", 0, 600.0f, 600.0f);
	Object *gate = a.place("AngmarWallGateSmall", 1, 640.0f, 600.0f);
	a.frames(2);
	GateOpenAndCloseBehavior *g = GateOpenAndCloseBehavior::findGate(*gate);
	REQUIRE(g);
	REQUIRE(g->state() == GateOpenAndCloseBehavior::CLOSED);
	REQUIRE(g->isSettled());
	CHECK(a.logic.combat().counters().unportedNuggets == 0u);
	DeliverNuggets(a.logic, gloin->getID(), w, WeaponBonus{}, gate, gate->getPosition(), false, &a.logic.combat().counters().unportedNuggets);
	CHECK(g->state() == GateOpenAndCloseBehavior::OPENING);
	CHECK(a.logic.combat().counters().unportedNuggets == 0u);
	// an unsettled (opening) gate is left alone, then it settles open and stays open under a second hit
	a.frames(60);
	CHECK(g->state() == GateOpenAndCloseBehavior::OPEN);
	DeliverNuggets(a.logic, gloin->getID(), w, WeaponBonus{}, gate, gate->getPosition(), false, nullptr);
	CHECK(g->state() == GateOpenAndCloseBehavior::OPEN);
}

// EmotionWeaponNugget (RW 0x911707 -> 0x68F3CB -> 0x8B4EB1): Faramir's wounding arrow (FaramirWoundArrowWarhead: EmotionType TERROR, Duration 5, SpecialObjectFilter
// ANY ENEMIES +MONSTER) forces TERROR on an enemy troll for ceil(0.005 * 5 * 1000) = 25 frames (RW 0x8B4ECC); a troll on Faramir's own side (not ENEMIES) is not touched
TEST_CASE("nugget1 emotion weapon: Faramir's wounding arrow terrifies an enemy troll for five seconds, not a friendly one")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	const WeaponTemplate &w = only(weapon("FaramirWoundArrowWarhead"), NUGGET_EMOTION_WEAPON);
	Object *faramir = a.place("GondorFaramir", 0, 600.0f, 600.0f);
	Object *troll = a.place("MordorMountainTroll", 1, 660.0f, 600.0f);
	Object *orc = a.place("MordorMountainTroll", 0, 600.0f, 700.0f); // a friendly MONSTER
	a.frames(2);
	EmotionTrackerUpdate *tt = EmotionTrackerUpdate::of(*troll);
	EmotionTrackerUpdate *to = EmotionTrackerUpdate::of(*orc);
	REQUIRE(tt);
	REQUIRE(to);
	const int terror = nameIndex(TheEmotionTypeNames, "TERROR");
	REQUIRE(terror == 6); // RW 0xD9FA08: TAUNT, CHEER, HERO_CHEER, POINT, FEAR, UNCONTROLLABLE_FEAR, TERROR, ...
	const int orcForced = to->forcedType();
	DeliverNuggets(a.logic, faramir->getID(), w, WeaponBonus{}, troll, troll->getPosition(), false, nullptr);
	CHECK(tt->forcedType() == terror);
	CHECK(tt->forcedFrames() == 25);
	DeliverNuggets(a.logic, faramir->getID(), w, WeaponBonus{}, orc, orc->getPosition(), false, nullptr);
	CHECK(to->forcedType() == orcForced);
}

// LuaEventNugget (RW 0x911EDC): the Rain of Fire's first phase (RainOfFirePhaseInitialWeapon: LuaEvent BeUncontrollablyAfraid, Radius 200, SendToEnemies Yes,
// SendToAllies No, SendToNeutral Yes) at a point: the enemies within 200 (distance type 1, the 3D centres) get the event nearest first (ITER_SORTED_NEAR_TO_FAR),
// the caster's own units and anything beyond 200 do not; an unknown event name reaches nobody (RW 0x735388 answers null)
TEST_CASE("nugget1 lua event: the rain of fire sends BeUncontrollablyAfraid to the enemies within 200, nearest first")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	const WeaponTemplate &w = weapon("RainOfFirePhaseInitialWeapon");
	Object *caster = a.place("GondorFighter", 0, 300.0f, 300.0f);
	Object *far = a.place("MordorFighter", 1, 600.0f + 150.0f, 600.0f);
	Object *near = a.place("MordorFighter", 1, 600.0f + 20.0f, 600.0f);
	Object *mid = a.place("MordorFighter", 1, 600.0f, 600.0f + 60.0f);
	Object *outside = a.place("MordorFighter", 1, 600.0f + 210.0f, 600.0f);
	Object *ally = a.place("GondorFighter", 0, 600.0f + 10.0f, 600.0f);
	a.frames(2);
	std::vector<std::pair<std::string, ObjectID>> got;
	std::vector<int> argIds;
	bool known = true;
	a.logic.setNamedScriptEventProcs([&](const std::string &) { return known; },
		[&](const std::string &name, Object &self, const LuaEventArgs &args) {
			got.push_back({ name, self.getID() });
			argIds.push_back(args.arg[0].kind == LuaEventArg::OBJECT ? args.arg[0].objectId : -1);
		});
	const Coord3D at{ 600.0f, 600.0f, 0.0f };
	DeliverNuggets(a.logic, caster->getID(), w, WeaponBonus{}, nullptr, &at, false, nullptr);
	REQUIRE(got.size() == 3u);
	CHECK(got[0].second == near->getID());
	CHECK(got[1].second == mid->getID());
	CHECK(got[2].second == far->getID());
	for (const auto &g : got)
	{
		CHECK(g.first == "BeUncontrollablyAfraid");
	}
	for (int id : argIds)
	{
		CHECK(id == (int)caster->getID());
	}
	(void)outside;
	(void)ally;
	got.clear();
	known = false;
	DeliverNuggets(a.logic, caster->getID(), w, WeaponBonus{}, nullptr, &at, false, nullptr);
	CHECK(got.empty());
}

// the same through the live game's script engine: ScriptEvents.xml declares BeUncontrollablyAfraid, so LiveScripting's named hook finds it and dispatches it to
// every receiver with an event list (the dispatch count of the engine grows by the receivers)
TEST_CASE("nugget1 lua event: through LiveScripting the rain of fire's event is found in ScriptEvents.xml and dispatched")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	a.startScripting(shared());
	const WeaponTemplate &w = weapon("RainOfFirePhaseInitialWeapon");
	Object *caster = a.place("GondorFighter", 0, 300.0f, 300.0f);
	a.place("MordorFighter", 1, 620.0f, 600.0f);
	a.place("MordorFighter", 1, 600.0f, 640.0f);
	a.frames(2);
	REQUIRE(a.logic.hasNamedScriptEvents());
	CHECK(a.logic.findNamedScriptEvent("BeUncontrollablyAfraid"));
	CHECK_FALSE(a.logic.findNamedScriptEvent("NoSuchEventAnywhere"));
	const size_t before = a.scripting->stats().engineEvents;
	const unsigned long long lost = a.logic.scriptEventsWithoutDispatch();
	const Coord3D at{ 600.0f, 600.0f, 0.0f };
	DeliverNuggets(a.logic, caster->getID(), w, WeaponBonus{}, nullptr, &at, false, nullptr);
	CHECK(a.scripting->stats().engineEvents == before + 2u);
	CHECK(a.logic.scriptEventsWithoutDispatch() == lost);
}

// SpawnAndFadeNugget (RW 0x911C46): the Isengard mine's drop (IsengardExplosiveMineDroppingWeaponPosition: SpawnedObjectName IsengardDeployedExplosiveMine,
// SpawnOffset X:8 Y:1) makes the deployed mine at the dropper's position + the offset turned by its angle (0: +8, +1; pi/2: -1, +8), facing as the dropper, and the
// dropper dies FADED (RW 0x698EC3(8, 0x16)); the target point is not read
TEST_CASE("nugget1 spawn and fade: the Isengard mine drops its deployed mine at the offset turned by its facing and fades")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionIsengard", "FactionMen");
	const WeaponTemplate &w = weapon("IsengardExplosiveMineDroppingWeaponPosition");
	Object *m1 = a.place("IsengardExplosiveMine", 0, 500.0f, 500.0f, 0.0f);
	Object *m2 = a.place("IsengardExplosiveMine", 0, 700.0f, 700.0f, 1.5707964f);
	a.frames(2);
	const Coord3D target{ 900.0f, 900.0f, 0.0f };
	const Coord3D p1 = *m1->getPosition(), p2 = *m2->getPosition();
	const float o1 = m1->getOrientation(), o2 = m2->getOrientation();
	REQUIRE(o1 == 0.0f);
	REQUIRE(o2 == 1.5707964f);
	REQUIRE(a.objectsOf("IsengardDeployedExplosiveMine").empty());
	DeliverNuggets(a.logic, m1->getID(), w, WeaponBonus{}, nullptr, &target, false, nullptr);
	std::vector<Object *> made = a.objectsOf("IsengardDeployedExplosiveMine");
	REQUIRE(made.size() == 1u);
	CHECK(made[0]->getPosition()->x == p1.x + 8.0f); // angle 0: c = 1, s = 0 exactly
	CHECK(made[0]->getPosition()->y == p1.y + 1.0f);
	CHECK(made[0]->getOrientation() == o1);
	CHECK(made[0]->getTeam() == m1->getTeam());
	CHECK(m1->isEffectivelyDead());
	DeliverNuggets(a.logic, m2->getID(), w, WeaponBonus{}, nullptr, &target, false, nullptr);
	made = a.objectsOf("IsengardDeployedExplosiveMine");
	REQUIRE(made.size() == 2u);
	const Object *second = made[0]->getPosition()->x > 600.0f ? made[0] : made[1];
	// tolerance: cos(1.5707964f) is -4.4e-8, far below one float ulp at 700: the turned offset is (-1, 8) to within that
	CHECK(second->getPosition()->x == doctest::Approx(p2.x - 1.0f).epsilon(1e-5));
	// tolerance: as above
	CHECK(second->getPosition()->y == doctest::Approx(p2.y + 8.0f).epsilon(1e-5));
	CHECK(second->getOrientation() == o2);
	CHECK(m2->isEffectivelyDead());
}

// SpecialModelConditionNugget (RW 0x90EF46 -> 0x68B581): the mountain troll's bored swipe (MordorMtTrollBored: USER_2 for 2000 ms on the trolls, USER_3 for
// 21000 ms on mountain trolls only; RadiusDamageAffects ALLIES) gives an allied mountain troll USER_2 for 10 frames and USER_3 for 105, a cave troll USER_2 only
TEST_CASE("nugget1 special model condition: a bored mountain troll's swipe sets USER_2 / USER_3 on the trolls it hits for their durations")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &w = weapon("MordorMtTrollBored");
	Object *troll = a.place("MordorMountainTroll", 0, 600.0f, 600.0f);
	Object *buddy = a.place("MordorMountainTroll", 0, 640.0f, 600.0f);
	Object *cave = a.place("MordorCaveTroll", 0, 600.0f, 640.0f);
	a.frames(2);
	const int user2 = CombatNames::modelCondition("USER_2");
	const int user3 = CombatNames::modelCondition("USER_3");
	const std::uint32_t now = a.logic.getFrame();
	DeliverNuggets(a.logic, troll->getID(), w, WeaponBonus{}, buddy, buddy->getPosition(), false, nullptr);
	DeliverNuggets(a.logic, troll->getID(), w, WeaponBonus{}, cave, cave->getPosition(), false, nullptr);
	CHECK(buddy->testModelCondition(user2));
	CHECK(buddy->testModelCondition(user3));
	CHECK(cave->testModelCondition(user2));
	CHECK_FALSE(cave->testModelCondition(user3));
	while (a.logic.getFrame() < now + 9u)
	{
		a.frames(1);
	}
	CHECK(buddy->testModelCondition(user2));
	a.frames(2);
	CHECK_FALSE(buddy->testModelCondition(user2));
	CHECK_FALSE(cave->testModelCondition(user2));
	CHECK(buddy->testModelCondition(user3));
}

// GrabNugget (RW 0x910C8A): Gwaihir's grab (EagleGrabAttack: ContainTargetOnEffect, ImpactTargetOnEffect; TransportContain Slots = 1) puts a goblin (GRAB_AND_DROP)
// into the eagle's contain. With the slot taken slot 1 (RW 0x910C27: the contain must accept the victim, slot 0x98(victim, 1, 0)) refuses the second goblin: nothing.
// The shockwave branch (RW 0x910ACA: ShockWaveAmount 50, Radius 16, TaperOff 0.75, ZMult 1.1) is reached only by a grab that does not contain: the same nugget with
// ContainTargetOnEffect off knocks the goblin over (the shockwave handler's STUNNED_FLAILING, RW 0x6968BC)
TEST_CASE("nugget1 grab: Gwaihir picks up a goblin; a full eagle grabs nothing; a non-containing grab knocks the goblin over")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionElves", "FactionWild");
	const WeaponTemplate &w = only(weapon("EagleGrabAttack"), NUGGET_GRAB);
	Object *eagle = a.place("GondorGwaihir", 0, 600.0f, 600.0f);
	Object *g1 = a.place("GoblinFighter", 1, 615.0f, 600.0f);
	Object *g2 = a.place("GoblinFighter", 1, 600.0f, 615.0f);
	a.frames(2);
	REQUIRE(eagle->getContain());
	DeliverNuggets(a.logic, eagle->getID(), w, WeaponBonus{}, g1, g1->getPosition(), false, nullptr);
	CHECK(g1->getContainedBy() == eagle);
	CHECK(eagle->getContain()->getContainCount() == 1u);
	const int flailing = CombatNames::modelCondition("STUNNED_FLAILING");
	REQUIRE_FALSE(g2->testModelCondition(flailing));
	DeliverNuggets(a.logic, eagle->getID(), w, WeaponBonus{}, g2, g2->getPosition(), false, nullptr);
	CHECK(g2->getContainedBy() == nullptr);
	a.frames(2);
	CHECK_FALSE(g2->testModelCondition(flailing));
	// the impact branch: the retail nugget with ContainTargetOnEffect off, from an eagle with a free slot
	Object *eagle2 = a.place("GondorGwaihir", 0, 900.0f, 900.0f);
	Object *g3 = a.place("GoblinFighter", 1, 900.0f, 915.0f);
	a.frames(2);
	WeaponTemplate shove = w;
	auto g = std::make_shared<GrabNugget>(static_cast<const GrabNugget &>(*w.m_nuggets[0]));
	g->m_containTargetOnEffect = false;
	shove.m_nuggets.assign(1, g);
	REQUIRE_FALSE(g3->testModelCondition(flailing));
	DeliverNuggets(a.logic, eagle2->getID(), shove, WeaponBonus{}, g3, g3->getPosition(), false, nullptr);
	CHECK(g3->getContainedBy() == nullptr);
	a.frames(2);
	CHECK(g3->testModelCondition(flailing));
}

// DamageFieldNugget (RW 0x90F5F3): the flaming catapult rock's death weapon (MordorCatapultRockFlame: WeaponTemplateName SmallFireFieldWeapon, Duration 3000 ms = 15
// frames) at a point makes GameData's GenericDamageField there (system.ini; GenericDamageFieldName, RW 0xBFF5D0), produced by the shooter and owned by its player's
// team, firing SmallFireFieldWeapon (Damage 1, Radius 24, every 250 ms) through its FireWeaponUpdate's single weapon (RW 0x88F041), until its DeletionUpdate ends it 15
// frames later (RW 0x88B830). An enemy beside it burns; the field object then goes
TEST_CASE("nugget1 damage field: a flaming catapult rock leaves a fire field that burns for three seconds, then goes")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	REQUIRE(a.logic.settings().genericDamageFieldName == "GenericDamageField");
	const WeaponTemplate &w = weapon("MordorCatapultRockFlame");
	Object *catapult = a.place("MordorCatapult", 0, 300.0f, 300.0f);
	Object *victim = a.place("GondorFighter", 1, 610.0f, 600.0f);
	a.frames(2);
	const Coord3D at{ 600.0f, 600.0f, 0.0f };
	const std::uint32_t now = a.logic.getFrame();
	DeliverNuggets(a.logic, catapult->getID(), w, WeaponBonus{}, nullptr, &at, false, nullptr);
	std::vector<Object *> fields = a.objectsOf("GenericDamageField");
	REQUIRE(fields.size() == 1u);
	Object *field = fields[0];
	CHECK(field->getPosition()->x == 600.0f);
	CHECK(field->getPosition()->y == 600.0f);
	CHECK(field->getProducerID() == catapult->getID());
	CHECK(field->getControllingPlayer() == a.player(0));
	FireWeaponUpdate *fire = dynamic_cast<FireWeaponUpdate *>(field->findModule("FireWeaponUpdate"));
	DeletionUpdate *del = dynamic_cast<DeletionUpdate *>(field->findModule("DeletionUpdate"));
	REQUIRE(fire);
	REQUIRE(del);
	REQUIRE(fire->singleWeapon());
	CHECK(fire->singleWeapon()->getTemplate()->getName() == "SmallFireFieldWeapon");
	CHECK(del->dieFrame() == now + 15u);
	const float hp = victim->getBodyModule()->getHealth();
	a.frames(4);
	// tolerance: the burn's size is the soldier's ArmorSet FLAME scalar on Damage 1 (armour data, not this lane's); the point is that the field hurts
	CHECK(victim->getBodyModule()->getHealth() < hp);
	// tolerance: how many 250 ms shots fall in four frames is FireWeaponUpdate's timing (lane SPELL-2); at least the first one fired
	CHECK(fire->shots() >= 1u);
	a.frames(14);
	CHECK(a.objectsOf("GenericDamageField").empty());
}

// lane NUGGET-1 save / load: FireWeaponUpdate's single weapon (version 2 of its snapshot) saves and loads into another field object: the same weapon and
// the same bytes saved again (the CRC holds the object's own id and scheduler index, so two objects never hash alike)
TEST_CASE("nugget1 damage field: the field's FireWeaponUpdate saves and loads its single weapon")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &w = weapon("MordorCatapultRockFlame");
	Object *catapult = a.place("MordorCatapult", 0, 300.0f, 300.0f);
	a.frames(2);
	const Coord3D at{ 600.0f, 600.0f, 0.0f };
	DeliverNuggets(a.logic, catapult->getID(), w, WeaponBonus{}, nullptr, &at, false, nullptr);
	REQUIRE(a.objectsOf("GenericDamageField").size() == 1u);
	a.frames(3);
	FireWeaponUpdate *src = dynamic_cast<FireWeaponUpdate *>(a.objectsOf("GenericDamageField")[0]->findModule("FireWeaponUpdate"));
	REQUIRE(src);
	REQUIRE(src->singleWeapon());
	XferSave w1;
	src->xfer(w1);
	Object *other = a.place("GenericDamageField", 0, 800.0f, 800.0f);
	FireWeaponUpdate *dst = dynamic_cast<FireWeaponUpdate *>(other->findModule("FireWeaponUpdate"));
	REQUIRE(dst);
	CHECK(dst->singleWeapon() == nullptr);
	XferLoad r(w1.data());
	dst->xfer(r);
	CHECK(r.atEnd());
	REQUIRE(dst->singleWeapon());
	CHECK(dst->singleWeapon()->getTemplate()->getName() == "SmallFireFieldWeapon");
	XferSave w2;
	dst->xfer(w2);
	CHECK(w2.data() == w1.data());
}

// DamageContainedNugget (RW 0x91117C): the Fire Drake's breath (FireDrakeWarhead: KillCount 5, KillKindof INFANTRY, DeathType BURNED) on a garrisoned keep kills the
// infantry inside, each scored to the drake; the keep stays
TEST_CASE("nugget1 damage contained: the fire drake's breath burns the infantry garrisoned in a keep")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &w = only(weapon("FireDrakeWarhead"), NUGGET_DAMAGE_CONTAINED);
	Object *drake = a.place("MordorFighter", 0, 500.0f, 600.0f);
	Object *keep = a.place("GondorKeep", 1, 600.0f, 600.0f);
	Object *s1 = a.place("GondorFighter", 1, 800.0f, 800.0f);
	Object *s2 = a.place("GondorFighter", 1, 820.0f, 800.0f);
	a.frames(3);
	REQUIRE(keep->getContain());
	REQUIRE(keep->getContain()->addToContain(s1));
	REQUIRE(keep->getContain()->addToContain(s2));
	DeliverNuggets(a.logic, drake->getID(), w, WeaponBonus{}, keep, keep->getPosition(), false, nullptr);
	CHECK(s1->isEffectivelyDead());
	CHECK(s2->isEffectivelyDead());
	CHECK_FALSE(keep->isEffectivelyDead());
	// an empty keep: nothing (slot 1, RW 0x911086)
	DeliverNuggets(a.logic, drake->getID(), w, WeaponBonus{}, keep, keep->getPosition(), false, nullptr);
	CHECK_FALSE(keep->isEffectivelyDead());
}

// RW 0x91117C with a HORDE occupant (template + 0x115 bit 5): the horde's members (its HordeContain's list) are the ones tested and killed, and KillCount (5, the Fire
// Drake's) caps them: a garrisoned GondorFighterHorde (15 members) loses exactly 5, the horde object and the keep stay
TEST_CASE("nugget1 damage contained: a garrisoned horde loses exactly KillCount members")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &w = only(weapon("FireDrakeWarhead"), NUGGET_DAMAGE_CONTAINED);
	Object *drake = a.place("MordorFighter", 0, 400.0f, 400.0f);
	Object *keep = a.place("GondorKeep", 1, 600.0f, 600.0f);
	Object *horde = a.place("GondorFighterHorde", 1, 700.0f, 700.0f);
	a.frames(3);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->getContainedItemsList());
	std::vector<ObjectID> members;
	for (const Object *m : *horde->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	REQUIRE(members.size() == 15u);
	REQUIRE(keep->getContain()->addToContain(horde));
	DeliverNuggets(a.logic, drake->getID(), w, WeaponBonus{}, keep, keep->getPosition(), false, nullptr);
	unsigned dead = 0;
	for (ObjectID id : members)
	{
		const Object *m = a.logic.findObjectByID(id);
		dead += (!m || m->isEffectivelyDead()) ? 1u : 0u;
	}
	CHECK(dead == 5u);
	CHECK_FALSE(horde->isEffectivelyDead());
	CHECK_FALSE(keep->isEffectivelyDead());
}

// FireWeaponUpdate's snapshot paths (lane NUGGET-1): a version-1 snapshot (no single weapon) loads with none; a weapon name the WeaponStore lacks is an XferException,
// never a default; a saved single weapon loads back over another one with the object as its owner and the same CRC (the same object: its id and scheduler index
// are unchanged), and it keeps firing
TEST_CASE("nugget1 damage field: FireWeaponUpdate's snapshot loads version 1, refuses an unknown weapon and restores the single weapon in place")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	Object *field = a.place("GenericDamageField", 0, 600.0f, 600.0f);
	Object *victim = a.place("GondorFighter", 1, 610.0f, 600.0f);
	FireWeaponUpdate *f = dynamic_cast<FireWeaponUpdate *>(field->findModule("FireWeaponUpdate"));
	REQUIRE(f);
	XferSave none;
	f->xfer(none);
	// version 1: the same bytes without the trailing (empty) name, the version byte 1
	std::vector<std::uint8_t> v1 = none.data();
	REQUIRE(v1.back() == 0u); // the empty name's length byte
	v1.pop_back();
	REQUIRE(v1[0] == 2u);
	v1[0] = 1u;
	XferLoad r1(v1);
	f->xfer(r1);
	CHECK(r1.atEnd());
	CHECK(f->singleWeapon() == nullptr);
	// an unknown weapon name
	std::vector<std::uint8_t> bad = none.data();
	bad.pop_back();
	const std::string name = "NoSuchWeaponNugget1";
	bad.push_back((std::uint8_t)name.size());
	bad.insert(bad.end(), name.begin(), name.end());
	XferLoad rb(bad);
	CHECK_THROWS_AS(f->xfer(rb), XferException);
	// a single weapon saved, replaced, loaded back
	f->setWeapon(&weapon("SmallFireFieldWeapon"));
	REQUIRE(f->singleWeapon());
	CHECK(f->singleWeapon()->getOwnerID() == field->getID());
	XferSave saved;
	f->xfer(saved);
	StateHasher before;
	f->crc(before);
	f->setWeapon(&weapon("MordorMtTrollBored"));
	XferLoad back(saved.data());
	f->xfer(back);
	CHECK(back.atEnd());
	REQUIRE(f->singleWeapon());
	CHECK(f->singleWeapon()->getTemplate()->getName() == "SmallFireFieldWeapon");
	CHECK(f->singleWeapon()->getOwnerID() == field->getID());
	StateHasher after;
	f->crc(after);
	CHECK(after.value() == before.value());
	const unsigned long long shots = f->shots();
	const float hp = victim->getBodyModule()->getHealth();
	a.frames(4);
	// tolerance: the shot count in four frames is FireWeaponUpdate's timing (lane SPELL-2); the restored weapon fires at least once
	CHECK(f->shots() > shots);
	// tolerance: the burn's size is armour data (see the damage field test)
	CHECK(victim->getBodyModule()->getHealth() < hp);
}

// the kind left counted (S-321): FireLogicNugget (TheFireLogicSystem, S-191) still goes to unportedNuggets; every other kind of retail weapon.ini is
// delivered (lane MODCOMPAT-MOD1: SlaveAttackNugget orders the expansion's slave, test_slavewatcher.cpp; a source without a SlaveWatcherBehavior: nothing)
TEST_CASE("nugget1: only FireLogic nuggets are still counted as not delivered")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	Object *src = a.place("MordorFighter", 0, 500.0f, 500.0f);
	a.frames(2);
	unsigned long long counted = 0;
	const Coord3D at{ 900.0f, 900.0f, 0.0f };
	DeliverNuggets(a.logic, src->getID(), only(weapon("GondorTrebuchetExpansionWeapon"), NUGGET_SLAVE_ATTACK), WeaponBonus{}, nullptr, &at, false, &counted);
	CHECK(counted == 0u);
	const WeaponTemplate fire = only(weapon("BabyDrakeInferno"), NUGGET_FIRE_LOGIC);
	DeliverNuggets(a.logic, src->getID(), fire, WeaponBonus{}, nullptr, &at, false, &counted);
	CHECK(counted == fire.m_nuggets.size());
	counted = 0;
	// the mine drop last: it fades its source
	for (const char *name : { "AngmarMorgramirRuinWeapon", "CreateAHeroStealMoney", "MordorGateWatchersFearWeapon", "EarthquakePhaseInitialWeapon",
		     "IsengardSharkuManEaterGrab", "MordorCatapultRockFlame", "IsengardExplosiveMineDroppingWeapon" })
	{
		DeliverNuggets(a.logic, src->getID(), weapon(name), WeaponBonus{}, nullptr, &at, false, &counted);
	}
	CHECK(counted == 0u);
}
