// OpenBFME retail tests for lane BACKLOG-1 (stops S-1100 / S-1101 / S-1620 / S-785: the RotWK functions the BFME2 decomp has byte-matched source for). They
// share the HUD tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("garrison backlog");
}

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	// the W3D assets: the garrison points are the keep's PassengerBonePrefix bones (RW 0x87C2B7 through the launch bone provider)
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	WW3DDrawAssets drawAssets;
	std::unique_ptr<DrawableLaunchBones> bones;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;
	GameLogicDispatch dispatch;

	explicit Arena(SharedWorld &s, unsigned seed = 7)
		: context(s.world->enterContext())
		, source(*s.mount->fs)
		, assets(source)
		, drawAssets(assets)
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
		, dispatch(logic)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
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
		GarrisonCommands::registerHandlers(dispatch);
		bones = std::make_unique<DrawableLaunchBones>(drawAssets, logic.settings().standardPublicBones);
		logic.combat().setLaunchOffsets(bones.get());
	}
	~Arena()
	{
		logic.reset();
		logic.combat().setLaunchOffsets(nullptr);
		ai.reset();
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
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
	void send(int side, int type, std::vector<ObjectID> selection, std::vector<ObjectID> args)
	{
		player(side)->selection() = selection;
		GameMessage m(type, player(side)->getPlayerIndex());
		for (ObjectID id : args)
		{
			m.appendObjectIDArgument(id);
		}
		dispatch.dispatch(m);
	}
};

bool templateHasKind(const ThingTemplate &t, const char *kind)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	REQUIRE(bit >= 0); // tolerance: any index the binary's KindOf table has (-1 = unknown name)
	const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(t);
	return (info.kindOf[(size_t)bit >> 5] >> (bit & 31)) & 1u;
}
std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

GarrisonContain *garrisonOf(Object &o)
{
	return dynamic_cast<GarrisonContain *>(o.getContain());
}

bool garrisonDone(Object &tower, Object &horde)
{
	HordeContainInterface *h = horde.getContain() ? horde.getContain()->getHordeContainInterface() : nullptr;
	return horde.getContainedBy() == &tower && h && h->allMembersEntered() && !membersOf(horde).empty();
}

// a Gondor archer horde ordered into a Gondor keep; true once it and its members are inside
bool garrisonArchers(Arena &a, Object *&tower, Object *&archers)
{
	tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	archers = a.place("GondorArcherHorde", 0, 450.0f, 600.0f);
	a.run(20);
	a.send(0, MSG_ENTER, { archers->getID() }, { archers->getID(), tower->getID() });
	for (int f = 0; f < 600 && !garrisonDone(*tower, *archers); ++f)
	{
		a.run(1);
	}
	return garrisonDone(*tower, *archers);
}

const Weapon *currentWeapon(const Object &o)
{
	const ObjectWeapons *w = o.getWeapons();
	return w ? w->currentWeapon() : nullptr;
}

bool hasError(const GameLogic &logic, const std::string &needle)
{
	for (const std::string &e : logic.report().errors)
	{
		if (e.find(needle) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}
} // namespace

// the private garrison point calls of RW 0x87B042 / 0x87B3EC for the tests (GarrisonContain names this class its friend)
class GarrisonContainTestAccess
{
public:
	static void putAtPoint(GarrisonContain &g, Object *obj, int condition, int index) { g.putObjectAtGarrisonPoint(obj, INVALID_ID, condition, index); }
	static void validateRallyPoint(GarrisonContain &g) { g.validateRallyPoint(); }
};

namespace
{
} // namespace

// ---- S-1100: OpenContain's CHUNK_VENDOR / ROCK_VENDOR entry (RW 0x8660F0) ------------------------------------------------------------------------------------

TEST_CASE("garrison backlog: a CHUNK_VENDOR / ROCK_VENDOR asks to enter as its body's GrabObject: the cave troll takes the rock vendors, nothing is reported (RW 0x8660F0)")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s);
	// vendors of the body classes the 2.01 vendors use (HighlanderBody, InactiveBody, ImmortalBody, StructureBody, ActiveBody): the GrabObject comes from the
	// body (ActiveBody and its subclasses: the data's GrabObject; InactiveBody: "")
	const std::vector<std::pair<const char *, const char *>> vendors = { { "GenericRockVendor", "GenericThrowableRock" }, { "ElvenBridge", "" },
		{ "HelmsDeepCauseway", "EntThrownBuildingRock" }, { "CINE_BlackGate", "EntThrownBuildingRock" }, { "CINEBones01", "RockGreyThrown01" } };
	Object *keep = a.place("GondorKeep", 0, 600.0f, 600.0f);
	std::vector<Object *> made;
	for (const auto &v : vendors)
	{
		Object *o = a.place(v.first, 0, 300.0f, 300.0f);
		REQUIRE(o->getBodyModule() != nullptr);
		CHECK_MESSAGE(o->getBodyModule()->getGrabObjectName() == v.second, std::string(v.first) << " grabs '" << o->getBodyModule()->getGrabObjectName() << "'");
		CHECK((templateHasKind(*o->getTemplate(), "CHUNK_VENDOR") || templateHasKind(*o->getTemplate(), "ROCK_VENDOR")));
		made.push_back(o);
	}
	// every contain of the 2.01 templates asked: the pairs that accept (the PassengerFilter takes the grabbed template, RW 0x763543)
	int contains = 0;
	std::set<std::string> accepted;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		bool hasContain = false;
		for (const ThingTemplate::Nugget &n : t->behaviorModules().nuggets())
		{
			hasContain = hasContain || n.name.find("Contain") != std::string::npos;
		}
		if (!hasContain)
		{
			continue;
		}
		Object *c = a.logic.newObject(t, a.player(0)->getDefaultTeam(), ObjectStatusMaskType{});
		if (c && c->getContain())
		{
			++contains;
			for (Object *v : made)
			{
				if (c->getContain()->isValidContainerFor(*v, false, false))
				{
					accepted.insert(t->getName() + " takes " + v->getTemplate()->getName());
				}
			}
		}
		if (c)
		{
			a.logic.destroyObject(c);
		}
	}
	MESSAGE(contains << " contains asked");
	// the cave troll's contain takes the two rock vendors whose GrabObject its filter allows (before lane BACKLOG-1 every vendor was refused and reported)
	CHECK(accepted == std::set<std::string>{ "MordorCaveTroll takes CINEBones01", "MordorCaveTroll takes GenericRockVendor" });
	CHECK_FALSE(keep->getContain()->isValidContainerFor(*made[0], false, false));
	CHECK_FALSE(hasError(a.logic, "CHUNK_VENDOR"));
}

// ---- S-1101: canEnterObject's DOZER + HARVESTER test (RW 0x82CC09) -------------------------------------------------------------------------------------------

TEST_CASE("garrison backlog: a worker (DOZER + HARVESTER) enters a goblin mine shaft only while the shaft's health is at least 99 percent (RW 0x82CC09)")
{
	if (!haveWorld())
	{
		return;
	}
	for (const char *workerName : { "MordorWorker", "WildLaborer" })
	{
		for (int damaged = 0; damaged < 2; ++damaged)
		{
			CAPTURE(workerName);
			CAPTURE(damaged);
			Arena a(shared());
			Object *worker = a.place(workerName, 1, 300.0f, 300.0f);
			REQUIRE(templateHasKind(*worker->getTemplate(), "DOZER"));
			REQUIRE(templateHasKind(*worker->getTemplate(), "HARVESTER"));
			Object *shaft = a.place("WildMineShaft", 1, 450.0f, 300.0f);
			a.run(2);
			ActiveBody *body = dynamic_cast<ActiveBody *>(shaft->getBodyModule());
			REQUIRE(body != nullptr);
			// 98.9 percent refuses, 99 percent (0.99f exactly: not below RW 0xBE53BC) still lets it in
			body->friend_setHealthRaw(SimMath::mulf32(body->getMaxHealth(), damaged ? 0.989f : 0.99f));
			CHECK(GarrisonRules::canEnterObject(*worker, shaft, CMD_FROM_PLAYER, 0, false) == !damaged);
			a.send(1, MSG_ENTER, { worker->getID() }, { worker->getID(), shaft->getID() });
			for (int f = 0; f < 300 && !worker->getContainedBy(); ++f)
			{
				a.run(1);
			}
			CHECK((worker->getContainedBy() == shaft) == !damaged);
		}
	}
}

// ---- S-1100: the garrison points' weapon test (RW 0x87BBE8 / 0x87C5D8 -> RW 0x441B59) -------------------------------------------------------------------------

TEST_CASE("garrison backlog: only a member whose current weapon is not a MeleeWeapon takes a point; a melee holder is freed, a vanished holder keeps it (RW 0x87BBE8 / 0x87C5D8)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr;
	REQUIRE(garrisonArchers(a, tower, archers));
	a.run(5);
	GarrisonContain *g = garrisonOf(*tower);
	REQUIRE(g != nullptr);
	const int condition = g->findConditionIndex();
	REQUIRE(condition == 0); // the keep is pristine
	REQUIRE(g->pointCount(condition) == 12); // the keep's 12 pristine PassengerBonePrefix bones
	// no victim yet: an ENCLOSED rider takes no point (RW 0x87BCD9)
	REQUIRE(g->pointsInUse() == 0);
	// a Gondor soldier (a member of a fighter horde: its current weapon is a MeleeWeapon) put on point 0 is freed by the next update (RW 0x87C61E)
	Object *fighters = a.place("GondorFighterHorde", 0, 300.0f, 300.0f);
	Object *otherArchers = a.place("GondorArcherHorde", 0, 300.0f, 450.0f);
	a.run(2);
	REQUIRE_FALSE(membersOf(*fighters).empty());
	REQUIRE_FALSE(membersOf(*otherArchers).empty());
	Object *sword = membersOf(*fighters).front();
	Object *bow = membersOf(*otherArchers).front();
	REQUIRE(currentWeapon(*sword) != nullptr);
	CHECK(currentWeapon(*sword)->getTemplate()->m_meleeWeapon);
	REQUIRE(currentWeapon(*bow) != nullptr);
	CHECK_FALSE(currentWeapon(*bow)->getTemplate()->m_meleeWeapon);
	GarrisonContainTestAccess::putAtPoint(*g, sword, condition, 0);
	REQUIRE(g->getObjectGarrisonPointIndex(sword->getID()) == 0);
	a.run(1);
	CHECK(g->getObjectGarrisonPointIndex(sword->getID()) == -1);
	CHECK(g->pointsInUse() == 0);
	// control: an archer (a ranged current weapon) put on the same point keeps it
	GarrisonContainTestAccess::putAtPoint(*g, bow, condition, 0);
	a.run(3);
	CHECK(g->getObjectGarrisonPointIndex(bow->getID()) == 0);
	// a holder that is gone keeps its point (RW 0x87C5FF: no object, the next point)
	const ObjectID bowId = bow->getID();
	a.logic.destroyObject(bow);
	a.run(3);
	REQUIRE(a.logic.findObjectByID(bowId) == nullptr);
	CHECK(g->points()[0].object == bowId);
	CHECK(g->pointsInUse() == 1);
	// a Mordor horde in range: the garrisoned archers (ranged current weapons, RW 0x87BC75) take the other points
	a.place("MordorFighterHorde", 1, 780.0f, 600.0f);
	a.run(10);
	int held = 0;
	for (const GarrisonContain::GarrisonPoint &p : g->points())
	{
		if (p.object == INVALID_ID || p.object == bowId)
		{
			continue;
		}
		Object *o = a.logic.findObjectByID(p.object);
		REQUIRE(o != nullptr);
		CHECK(o->getContainedBy() == archers);
		REQUIRE(currentWeapon(*o) != nullptr);
		CHECK_FALSE(currentWeapon(*o)->getTemplate()->m_meleeWeapon);
		++held;
	}
	MESSAGE(held << " archers on the keep's " << g->pointCount(condition) << " points");
	CHECK(held == std::min(g->pointCount(condition) - 1, (int)membersOf(*archers).size()));
	CHECK(g->pointsInUse() == held + 1);
}

// ---- S-1100: removeAllContained's exit position search (RW 0x87B4DC -> RW 0x87B3EC) -----------------------------------------------------------------------

TEST_CASE("garrison backlog: a destroyed keep searches its exit rally point around itself, from its radius to 1.8 times it, one random start angle per search (RW 0x87B3EC)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = nullptr, *archers = nullptr;
	REQUIRE(garrisonArchers(a, tower, archers));
	GarrisonContain *g = garrisonOf(*tower);
	REQUIRE(g != nullptr);
	CHECK_FALSE(g->exitRallyValid());
	// the search itself: no rally yet, so only the ring search around the keep runs, drawing one random start angle (GameLogicRandomValueReal(0, 2 pi))
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	GarrisonContainTestAccess::validateRallyPoint(*g);
	const std::vector<GameLogicRandom::Call> calls = a.logic.random().callLog();
	a.logic.random().enableCallLog(false);
	REQUIRE(calls.size() == 1u);
	CHECK(calls[0].real);
	CHECK(calls[0].rlo == 0.0f);
	CHECK(calls[0].file == "TerrainLogic.cpp");
	CHECK(calls[0].line == 0x113E);
	REQUIRE(g->exitRallyValid());
	const float r = CombatQueries::boundingCircleRadius(*tower);
	const Coord3D &rally = g->exitRallyPoint();
	const float dx = rally.x - tower->getPosition()->x, dy = rally.y - tower->getPosition()->y;
	const float d = std::sqrt(dx * dx + dy * dy);
	MESSAGE("keep radius " << r << ", rally (" << rally.x << ", " << rally.y << ") at " << d);
	CHECK(d >= r - 0.01f);        // tolerance: the length of the stored float spot on the first ring (radius r); the search runs r .. 1.8 r
	CHECK(d <= r * 1.8f + 0.01f); // tolerance: as above
	// a valid rally is kept when the spot is still free: one search at it (radius 0), one draw, the point unchanged
	const Coord3D before = rally;
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	GarrisonContainTestAccess::validateRallyPoint(*g);
	CHECK(a.logic.random().callLog().size() == 1u);
	a.logic.random().enableCallLog(false);
	CHECK(g->exitRallyValid());
	CHECK(g->exitRallyPoint().x == before.x);
	CHECK(g->exitRallyPoint().y == before.y);
	// the death path: the keep killed with its garrison inside ejects it through removeAllContained, which runs the search first (RW 0x87B4F0): its draws are
	// in the kill's own random calls
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	tower->kill(DAMAGE_UNRESISTABLE, 0);
	a.run(2);
	int searches = 0;
	for (const GameLogicRandom::Call &c : a.logic.random().callLog())
	{
		searches += (c.file == "TerrainLogic.cpp" && c.line == 0x113E) ? 1 : 0;
	}
	a.logic.random().enableCallLog(false);
	CHECK(searches == 1); // the valid rally is still free: the search at it only
	CHECK(archers->getContainedBy() == nullptr);
}

// ---- S-1101: AIEnterState (RW 0x756067), canEnterObject (RW 0x82CBD5), MSG_ENTER (RW 0x77AC26), privateEnter (RW 0x66D85F) --------------------------------------

namespace
{
// a lone Lindon warrior of side 0 given AI command 0x17 (aiEnterObject) into the keep from 150 away: true once AIEnterState (15) runs
bool startEnter(Arena &a, Object *&tower, Object *&warrior)
{
	tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	warrior = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	a.run(5);
	warrior->getAIUpdateInterface()->aiEnterObject(tower, CMD_FROM_PLAYER);
	a.run(2);
	return warrior->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ENTER;
}
} // namespace

TEST_CASE("garrison backlog: an enemy container the unit can no longer enter is attacked instead (AIEnterState RW 0x7560FB -> getCanAttackObject RW 0x82CFD4)")
{
	if (!haveWorld())
	{
		return;
	}
	for (int enemy = 0; enemy < 2; ++enemy)
	{
		CAPTURE(enemy);
		Arena a(shared());
		a.logic.combat().setAutoAcquireEnabled(false); // nobody picks a target by itself: only the state's own attack order can hurt the keep
		Object *tower = nullptr, *warrior = nullptr;
		REQUIRE(startEnter(a, tower, warrior));
		const unsigned long long attacksBefore = GarrisonRules::enemyContainerAttacks();
		if (enemy)
		{
			tower->setTeam(a.player(1)->getDefaultTeam()); // the keep falls to Mordor while the warrior walks to it
			REQUIRE(warrior->getRelationship(*tower) == ENEMIES);
		}
		else
		{
			tower->setTeam(a.players.getNeutralPlayer()->getDefaultTeam()); // control: a neutral keep it may not enter (AllowNeutralInside No) is not attacked
			REQUIRE(warrior->getRelationship(*tower) != ENEMIES);
		}
		REQUIRE_FALSE(GarrisonRules::canEnterObject(*warrior, tower, CMD_FROM_PLAYER, 0, false));
		const float health = tower->getBodyModule()->getHealth();
		const ObjectID warriorId = warrior->getID();
		// the keep (Mordor's now) shoots back: look the warrior up every frame and stop at the first hit on the keep
		int hitFrame = -1;
		unsigned stateAtHit = 0;
		for (int f = 0; f < 150 && hitFrame < 0; ++f)
		{
			a.run(1);
			Object *w = a.logic.findObjectByID(warriorId);
			REQUIRE(w != nullptr);
			CHECK(w->getContainedBy() == nullptr);
			if (tower->getBodyModule()->getHealth() < health)
			{
				hitFrame = f;
				stateAtHit = w->getAIUpdateInterface()->currentStateId();
			}
		}
		MESSAGE("the keep was first hit after " << hitFrame << " frames");
		if (enemy)
		{
			CHECK(GarrisonRules::enemyContainerAttacks() == attacksBefore + 1);
			CHECK(hitFrame == 7); // only the warrior is near the keep
			CHECK(stateAtHit == (unsigned)AI_ATTACK_OBJECT);
		}
		else
		{
			CHECK(GarrisonRules::enemyContainerAttacks() == attacksBefore);
			CHECK(hitFrame == -1);
			CHECK(a.logic.findObjectByID(warriorId)->getAIUpdateInterface()->currentStateId() != (unsigned)AI_ATTACK_OBJECT);
		}
	}
}

TEST_CASE("garrison backlog: a container that is itself contained and above the terrain cannot be entered from the ground (AIEnterState RW 0x756088)")
{
	if (!haveWorld())
	{
		return;
	}
	for (int lifted = 0; lifted < 2; ++lifted)
	{
		CAPTURE(lifted);
		Arena a(shared());
		Object *tower = nullptr, *warrior = nullptr;
		REQUIRE(startEnter(a, tower, warrior));
		Object *holder = a.place("GondorKeep", 0, 900.0f, 900.0f);
		tower->friend_setContainedBy(holder); // the test stands in for a container carried by another (RotWK + 0x27C)
		if (lifted)
		{
			Coord3D p = *tower->getPosition();
			p.z = a.logic.getGroundHeight(p.x, p.y) + 20.0f;
			tower->setPosition(&p);
		}
		CHECK(GarrisonRules::canEnterObject(*warrior, tower, CMD_FROM_PLAYER, 0, false)); // the entry test itself still allows it
		a.run(1);
		const bool stillEntering = warrior->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ENTER;
		tower->friend_setContainedBy(nullptr);
		CHECK(stillEntering == !lifted);
	}
}

TEST_CASE("garrison backlog: every collide module a Lindon warrior has would not like to collide with the keep; it enters without a report (RW 0x82CD2E)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *warrior = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	int collides = 0;
	for (const std::unique_ptr<BehaviorModule> &m : warrior->modules())
	{
		if (const CollideModuleInterface *c = m->getCollide())
		{
			++collides;
			CHECK_FALSE(c->wouldLikeToCollideWith(*tower));
		}
	}
	CHECK(collides == 1); // its SquishCollide
	CHECK(GarrisonRules::canEnterObject(*warrior, tower, CMD_FROM_PLAYER, 0, false));
	CHECK_FALSE(hasError(a.logic, "would like to collide"));
}

TEST_CASE("garrison backlog: MSG_ENTER releases the group's temporary weapon locks and the order sets the locomotor's preferred point (RW 0x77AC55 / 0x66D880)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *warrior = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	a.run(5);
	ObjectWeapons *w = warrior->getWeapons();
	REQUIRE(w != nullptr);
	REQUIRE(w->setWeaponLock(0, LOCKED_TEMPORARILY));
	REQUIRE(w->isCurWeaponLocked());
	const Coord3D at = *warrior->getPosition();
	a.send(0, MSG_ENTER, { warrior->getID() }, { warrior->getID(), tower->getID() });
	CHECK_FALSE(w->isCurWeaponLocked());
	AIUpdateInterface *ai = warrior->getAIUpdateInterface();
	REQUIRE(ai->curLocomotor() != nullptr);
	CHECK(ai->curLocomotor()->preferredPoint().x == at.x);
	CHECK(ai->curLocomotor()->preferredPoint().y == at.y);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO_POSITION_AND_ENTER);
}

// ---- S-785: the move-away handler (RW 0x66DA5F) ------------------------------------------------------------------------------------------------------------

TEST_CASE("garrison backlog: a repeated move-away request while blocked ignores collisions for 2 seconds; the order saves the goal and sets the preferred point (RW 0x66DA5F)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *unit = a.place("LindonElvenWarrior", 0, 400.0f, 400.0f);
	Object *requester = a.place("LindonElvenWarrior", 0, 380.0f, 400.0f);
	a.run(5);
	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	// the unit is walking to a far goal: the request takes a temporary state and keeps that goal (RW 0x66DD04: RW 0x741675 before RW 0x74168E)
	const Coord3D goal{ 900.0f, 400.0f, 0.0f };
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	a.run(1);
	const Coord3D machineGoal = ai->stateMachine().goalPosition();
	const Coord3D at = *unit->getPosition();
	ai->aiMoveAwayFromUnit(requester, *requester->getPosition(), CMD_FROM_AI);
	REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_MOVE_OUT_OF_THE_WAY);
	CHECK(ai->stateMachine().savedGoalPosition().x == machineGoal.x);
	CHECK(ai->stateMachine().savedGoalPosition().y == machineGoal.y);
	CHECK(ai->curLocomotor()->preferredPoint().x == at.x);
	CHECK(ai->curLocomotor()->preferredPoint().y == at.y);
	// the same requester again, not blocked: nothing
	ai->aiMoveAwayFromUnit(requester, *requester->getPosition(), CMD_FROM_AI);
	CHECK(ai->mover().ignoreCollisionsUntil() == 0u);
	// blocked (AI + 0x3B8): collisions are ignored until now + 2 * LOGICFRAMES_PER_SECOND (RW 0x66DB98)
	ai->mover().friend_setBlocked(true);
	ai->aiMoveAwayFromUnit(requester, *requester->getPosition(), CMD_FROM_AI);
	CHECK(ai->mover().ignoreCollisionsUntil() == a.logic.getFrame() + 2u * LOGICFRAMES_PER_SECOND);
}

// ---- S-1620: the contain's rally point in the garrison exit (RW 0x87CC13, RW 0x865679 / 0x865694) ------------------------------------------------------------

TEST_CASE("garrison backlog: archers evacuated from a keep with a rally point walk to it; without one they stop at the exit (RW 0x87CC13)")
{
	if (!haveWorld())
	{
		return;
	}
	const Coord3D rally{ 600.0f, 300.0f, 0.0f };
	float distToRally[2] = { 0.0f, 0.0f };
	for (int withRally = 0; withRally < 2; ++withRally)
	{
		CAPTURE(withRally);
		Arena a(shared());
		Object *tower = nullptr, *archers = nullptr;
		REQUIRE(garrisonArchers(a, tower, archers));
		GarrisonContain *g = garrisonOf(*tower);
		REQUIRE(g != nullptr);
		CHECK(g->getRallyPoint() == nullptr);
		if (withRally)
		{
			g->setRallyPoint(rally);
			REQUIRE(g->getRallyPoint() != nullptr);
		}
		a.send(0, MSG_EVACUATE, { tower->getID() }, {});
		for (int f = 0; f < 100 && archers->getContainedBy(); ++f)
		{
			a.run(1);
		}
		REQUIRE(archers->getContainedBy() == nullptr);
		a.run(300);
		const float dx = archers->getPosition()->x - rally.x, dy = archers->getPosition()->y - rally.y;
		distToRally[withRally] = std::sqrt(dx * dx + dy * dy);
		CHECK(g->exitStats().rallyExits == (withRally ? (unsigned long long)g->exitStats().exitPaths : 0ull));
	}
	MESSAGE("the horde ends " << distToRally[0] << " from the rally spot without a rally point, " << distToRally[1] << " with one");
	CHECK(distToRally[1] < 5.0f);   // tolerance: the re-formed horde object stops within its locomotor's close-enough distance of the rally point (300 from the keep)
	CHECK(distToRally[0] > 250.0f); // tolerance: without a rally it stops at the keep's ExitOffset, about 300 from the rally spot
}

// ---- S-4320 / S-4321: the reported gaps ----------------------------------------------------------------------------------------------------------------------

namespace
{
bool hasStop(const GameLogic &logic, const char *line)
{
	const std::vector<std::string> stops = logic.report().stops;
	return std::find(stops.begin(), stops.end(), std::string(line)) != stops.end();
}
} // namespace

TEST_CASE("garrison backlog: MSG_SET_RALLY_POINT on a keep reports S-4320 and leaves its rally point unset")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	a.run(2);
	GarrisonContain *g = garrisonOf(*tower);
	REQUIRE(g != nullptr);
	REQUIRE(tower->getObjectExitInterface() == nullptr); // no exit module: RotWK would answer the contain's (RW 0x68BB37)
	CHECK_FALSE(hasStop(a.logic, GameLogicDispatch::containRallyStopLine()));
	const std::uint32_t before = a.logic.computeStateHash();
	GameMessage m(MSG_SET_RALLY_POINT, a.player(0)->getPlayerIndex());
	m.appendObjectIDArgument(tower->getID());
	m.appendLocationArgument(Coord3D{ 600.0f, 300.0f, 0.0f });
	m.appendBooleanArgument(false);
	m.appendObjectIDArgument(INVALID_ID);
	a.dispatch.dispatch(m);
	CHECK(hasStop(a.logic, GameLogicDispatch::containRallyStopLine()));
	CHECK(std::string(GameLogicDispatch::containRallyStopLine()).rfind("[S-4320]", 0) == 0u);
	CHECK(g->getRallyPoint() == nullptr);
	CHECK(a.logic.computeStateHash() == before); // the refusal changes nothing
}

TEST_CASE("garrison backlog: a plain move of a CanMoveBackwards unit reports S-4321 and leaves the locomotor's preferred point as it was")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *warrior = a.place("LindonElvenWarrior", 0, 450.0f, 600.0f);
	a.run(5);
	AIUpdateInterface *ai = warrior->getAIUpdateInterface();
	REQUIRE(ai->curLocomotor() != nullptr);
	REQUIRE(ai->curLocomotor()->getTemplate().m_canMoveBackwards == 1); // HumanLocomotor: CanMoveBackwards = Yes
	const Coord3D before = ai->curLocomotor()->preferredPoint();
	CHECK_FALSE(hasStop(a.logic, AIUpdateInterface::preferredPointStopLine()));
	ai->aiMoveToPosition(Coord3D{ 700.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
	CHECK(hasStop(a.logic, AIUpdateInterface::preferredPointStopLine()));
	CHECK(std::string(AIUpdateInterface::preferredPointStopLine()).rfind("[S-4321]", 0) == 0u);
	CHECK(ai->curLocomotor()->preferredPoint().x == before.x);
	CHECK(ai->curLocomotor()->preferredPoint().y == before.y);
}

// ---- S-785: the move-away in the world ------------------------------------------------------------------------------------------------------------------------

TEST_CASE("garrison backlog: a walking unit asked to move away leaves its line, then goes on to the destination it had (RW 0x66DA5F: RW 0x741675 + 0x74168E)")
{
	if (!haveWorld())
	{
		return;
	}
	const Coord3D goal{ 900.0f, 400.0f, 0.0f };
	const int frames = 400;
	std::vector<Coord3D> track[2];
	int tempFrames = 0;
	for (int asked = 0; asked < 2; ++asked)
	{
		Arena a(shared());
		a.logic.combat().setAutoAcquireEnabled(false);
		Object *unit = a.place("LindonElvenWarrior", 0, 400.0f, 400.0f);
		Object *requester = a.place("LindonElvenWarrior", 0, 380.0f, 400.0f);
		a.run(5);
		AIUpdateInterface *ai = unit->getAIUpdateInterface();
		ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
		a.run(20); // the walk is under way on its own path
		if (asked)
		{
			ai->aiMoveAwayFromUnit(requester, *requester->getPosition(), CMD_FROM_AI);
			REQUIRE(ai->stateMachine().temporaryStateId() == (unsigned)AI_MOVE_OUT_OF_THE_WAY);
		}
		for (int f = 0; f < frames; ++f)
		{
			a.run(1);
			track[asked].push_back(*unit->getPosition());
			tempFrames += (asked && ai->stateMachine().temporaryStateId() == (unsigned)AI_MOVE_OUT_OF_THE_WAY) ? 1 : 0;
		}
		CHECK(ai->stateMachine().temporaryStateId() != (unsigned)AI_MOVE_OUT_OF_THE_WAY);
	}
	// the largest distance between the two runs at the same frame: the asked unit left the line the other one walked
	float apart = 0.0f;
	for (int f = 0; f < frames; ++f)
	{
		const float dx = track[1][(size_t)f].x - track[0][(size_t)f].x, dy = track[1][(size_t)f].y - track[0][(size_t)f].y;
		apart = std::max(apart, std::sqrt(dx * dx + dy * dy));
	}
	MESSAGE("moved out of the way for " << tempFrames << " frames, at most " << apart << " from the unasked walk; ends (" << track[1].back().x << ", " << track[1].back().y
		<< ") / unasked (" << track[0].back().x << ", " << track[0].back().y << ")");
	CHECK(tempFrames > 0);  // tolerance: the temporary state ran for some frames (at most 10 * LOGICFRAMES_PER_SECOND)
	CHECK(apart > 5.0f);    // tolerance: a displacement of more than half a cell, not rounding
	// both end on the destination the order gave: the saved goal came back when the temporary state ended
	CHECK(track[1].back().x == track[0].back().x);
	CHECK(track[1].back().y == track[0].back().y);
	const float ex = track[1].back().x - goal.x, ey = track[1].back().y - goal.y;
	CHECK(std::sqrt(ex * ex + ey * ey) < 10.0f); // tolerance: the locomotor's close-enough distance of the goal
}
