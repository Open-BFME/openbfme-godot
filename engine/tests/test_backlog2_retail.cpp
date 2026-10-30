// OpenBFME unit tests (lane BACKLOG2) on a retail skirmish (Evendim, Men against Mordor): what retail does to the ground before a structure appears there
// (GameLogic/ConstructionSite.h). SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
GameMessage selectMsg(int player, ObjectID id)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	m.appendObjectIDArgument(id);
	return m;
}

Object *spawn(GameLogic &logic, const char *name, Player &owner, const Coord3D &at)
{
	const ThingTemplate *t = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(t != nullptr, "retail template " << name);
	Object *o = logic.newObject(t, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o != nullptr, name);
	Coord3D p = at;
	o->setPosition(&p);
	return o;
}

// the draws one file made, in order
std::vector<GameLogicRandom::Call> drawsOf(GameLogic &logic, const char *file)
{
	std::vector<GameLogicRandom::Call> out;
	for (const GameLogicRandom::Call &c : logic.random().callLog())
	{
		if (c.file == file)
		{
			out.push_back(c);
		}
	}
	return out;
}

float boundingCircle(const ThingTemplate &tt)
{
	PathfindGeometry g;
	ObjectGeometry::fillPathfindGeometry(*tt.getFinalOverride(), g);
	return g.boundingCircleRadius();
}

// the goal RW 0x797465 gives a unit it moves (see GameLogic/ConstructionSite.h): the radius of the box around the site and the two draws
Coord3D buildAssistantGoal(const ThingTemplate &what, const Coord3D &site, float r1, float angle)
{
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*what.getFinalOverride());
	float circle;
	if (shapes.size() == 1 && shapes[0].type == ObjectGeometry::SHAPE_BOX)
	{
		circle = boundingCircle(what); // RW 0x7974BB: the template's own single box
	}
	else
	{
		PathfindGeometry box; // RW 0x7974AB: BOX(10, c, c) at no offset
		box.type = PATHFIND_GEOMETRY_BOX;
		box.majorRadius = box.minorRadius = boundingCircle(what);
		box.height = 10.0f;
		circle = box.boundingCircleRadius();
	}
	const float radius = SimMath::mulf32(circle, 1.4f);
	const float v = NumericState::fstpDword(NumericState::pc24MulW(r1, radius));
	const float s = SimMath::sinf32(angle), c = SimMath::cosf32(angle);
	return { SimMath::addf32(site.x, SimMath::subf32(SimMath::mulf32(c, v), SimMath::mulf32(s, 0.0f))),
		SimMath::addf32(site.y, SimMath::addf32(SimMath::mulf32(s, v), SimMath::mulf32(c, 0.0f))), site.z };
}

std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof u);
	return u;
}

bool alive(GameLogic &logic, ObjectID id)
{
	const Object *o = logic.findObjectByID(id);
	return o && !o->isDestroyed();
}
} // namespace

TEST_CASE("BACKLOG2 S-653: a plot's build clears the shrubbery off the site and sends the allied units standing there away; an enemy stays (RW 0x797796 -> 0x796576 / 0x797465)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	Player *mordor = live.players().findPlayerWithName(live.report().startSlotPlayers[1]);
	REQUIRE(men);
	REQUIRE(mordor);
	std::vector<Object *> pads;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == men && o->getTemplate()->getName() == "MenFortressExpansionPadSide")
		{
			pads.push_back(o);
		}
	}
	REQUIRE(pads.size() == 2); // the Men fortress's two side pads (Fortress_Men)
	const ThingTemplate *tower = logic.things().findTemplate("MenArrowTowerExpansion")->getFinalOverride(); // MenFortressExpansionPadSideCommandSet
	const int pi = men->getPlayerIndex();
	auto build = [&](Object *pad) {
		// the dispatcher's cases run here directly (as the frame's command list would run them), so the orders are read before the AI's frame acts on them
		live.dispatch().dispatch(selectMsg(pi, pad->getID()));
		GameMessage m(MSG_FOUNDATION_CONSTRUCT, pi);
		m.appendIntegerArgument((int)tower->getTemplateID());
		m.appendLocationArgument(*pad->getPosition());
		m.appendRealArgument(0.0f);
		live.dispatch().dispatch(m);
		Object *built = nullptr;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			built = o->getTemplate() == tower && !o->isDestroyed() && o->getProducerID() == pad->getID() ? o : built;
		}
		return built;
	};
	logic.random().enableCallLog(true);
	// 1. an enemy on the site: no order, no draw (Player RW 0x6ADBEB: ENEMIES)
	const Coord3D siteB = *pads[1]->getPosition();
	Object *gothmog = spawn(logic, "MordorGothmog", *mordor, { siteB.x + 6.0f, siteB.y, siteB.z });
	logic.runLogicFrame();
	logic.random().clearCallLog();
	REQUIRE(build(pads[1]));
	CHECK(drawsOf(logic, "BuildAssistant.cpp").empty());
	CHECK(gothmog->getAIUpdateInterface()->evenIfSleepingOrders() == 0u);
	logic.destroyObject(gothmog);
	logic.runLogicFrame();
	// 2. a tree (KindOf SHRUBBERY) on the other site, Boromir (Men) beside it and a second Men hero far away (the control)
	const Coord3D site = *pads[0]->getPosition();
	const ObjectID tree = spawn(logic, "PTree_cine", *men, site)->getID();
	Object *boromir = spawn(logic, "GondorBoromir", *men, { site.x + 6.0f, site.y, site.z });
	Object *far = spawn(logic, "GondorAragorn", *men, { site.x + 600.0f, site.y, site.z });
	logic.runLogicFrame();
	REQUIRE(alive(logic, tree));
	REQUIRE_FALSE(boromir->getAIUpdateInterface()->isMoving()); // a moving unit draws but takes no order (RW 0x797732)
	logic.random().clearCallLog();
	REQUIRE(build(pads[0]));
	CHECK_FALSE(alive(logic, tree)); // RW 0x796602: destroyObject
	// exactly one unit was moved: Boromir drew the radius (0.5 .. 1.5, BuildAssistant line 0xE41), then the angle (-pi .. pi, line 0xE44)
	const std::vector<GameLogicRandom::Call> draws = drawsOf(logic, "BuildAssistant.cpp");
	REQUIRE(draws.size() == 2);
	CHECK(draws[0].real);
	CHECK(draws[0].rlo == 0.5f);
	CHECK(draws[0].rhi == 1.5f);
	CHECK(draws[0].line == 0xE41);
	CHECK(draws[1].rlo == -3.14159274f);
	CHECK(draws[1].rhi == 3.14159274f);
	CHECK(draws[1].line == 0xE44);
	const Coord3D want = buildAssistantGoal(*tower, site, draws[0].rresult, draws[1].rresult);
	CHECK(boromir->getAIUpdateInterface()->evenIfSleepingOrders() == 1u);
	const Coord3D got = boromir->getAIUpdateInterface()->lastEvenIfSleepingGoal();
	CHECK(got.x == want.x);
	CHECK(got.y == want.y);
	CHECK(far->getAIUpdateInterface()->evenIfSleepingOrders() == 0u);
	logic.runLogicFrame();
	CHECK(boromir->getAIUpdateInterface()->isMoving());
	logic.random().enableCallLog(false);
}

TEST_CASE("BACKLOG2 S-653: a dead structure that starts its rebuild sends the allied units on it away (RW 0x85679D -> 0x797465)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(men);
	// a free Men wall segment far from the bases (KeepObjectDie: it stays in the world dead), killed, then Boromir on its rubble
	// beside the Men fortress (walkable ground)
	Object *pad = nullptr;
	for (Object *o = logic.getFirstObject(); o && !pad; o = o->getNextObject())
	{
		pad = (o->getControllingPlayer() == men && o->getTemplate()->getName() == "MenFortressExpansionPadSide") ? o : nullptr;
	}
	REQUIRE(pad);
	const Coord3D site{ pad->getPosition()->x + 110.0f, pad->getPosition()->y + 110.0f, pad->getPosition()->z };
	Object *wall = spawn(logic, "GondorCastleWallSegment", *men, site);
	wall->friend_onBuildComplete();
	logic.runLogicFrame();
	wall->kill(DAMAGE_UNRESISTABLE, 0);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE(logic.findObjectByID(wall->getID()) == wall);
	REQUIRE(wall->isEffectivelyDead());
	Object *boromir = spawn(logic, "GondorBoromir", *men, { site.x + 4.0f, site.y, site.z });
	logic.runLogicFrame();
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(wall->findModule("GettingBuiltBehavior"));
	REQUIRE(gb);
	logic.random().enableCallLog(true);
	logic.random().clearCallLog();
	gb->startConstruction(true); // the rebuild's start (RW 0x8566DF slot 1, forced: no price)
	const std::vector<GameLogicRandom::Call> draws = drawsOf(logic, "BuildAssistant.cpp");
	REQUIRE(draws.size() == 2);
	const Coord3D want = buildAssistantGoal(*static_cast<const ThingTemplate *>(wall->getTemplate()), *wall->getPosition(), draws[0].rresult, draws[1].rresult);
	const Coord3D got = boromir->getAIUpdateInterface()->lastEvenIfSleepingGoal();
	CHECK(got.x == want.x);
	CHECK(got.y == want.y);
	logic.random().enableCallLog(false);
	// the movement: he walks to the spot the order named (open ground: the path ends on it) and stands there
	const Coord3D start = *boromir->getPosition();
	int walked = 0;
	for (int i = 0; i < 60; ++i)
	{
		logic.runLogicFrame();
		walked += boromir->getAIUpdateInterface()->isMoving() ? 1 : 0;
	}
	// pinned (measured, Linux / GCC): he walks 10 frames and stops on the cell the move state adjusted the ordered spot to. Lane TARGET-1: was 7 frames to
	// (1144594432, 1167152128): the mood scans' one-off random offsets (RW 0x6686F0) draw from the logic generator before the rebuild's two draws, so the
	// ordered spot moved
	CHECK(walked == 10);
	CHECK(bitsOf(boromir->getPosition()->x) == 1143611392u);
	CHECK(bitsOf(boromir->getPosition()->y) == 1167193088u);
	CHECK(bitsOf(start.x) == 1144391642u);
}

TEST_CASE("BACKLOG2 S-950: the foundation's creation interface clears the pad and pushes the units on it out to the nearer of two random spots (RW 0x859198 -> 0x858C41 / 0x858CFC)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(men);
	Object *pad = nullptr;
	for (Object *o = logic.getFirstObject(); o && !pad; o = o->getNextObject())
	{
		pad = (o->getControllingPlayer() == men && o->getTemplate()->getName() == "MenFortressExpansionPadSide") ? o : nullptr;
	}
	REQUIRE(pad);
	const Coord3D site = *pad->getPosition();
	const ObjectID tree = spawn(logic, "PTree_cine", *men, site)->getID();
	Object *boromir = spawn(logic, "GondorBoromir", *men, { site.x + 6.0f, site.y + 2.0f, site.z });
	logic.runLogicFrame();
	FoundationAIUpdate *foundation = nullptr;
	for (const std::unique_ptr<BehaviorModule> &mod : pad->modules())
	{
		foundation = foundation ? foundation : mod->getFoundationAIUpdate();
	}
	REQUIRE(foundation);
	const ThingTemplate *tower = logic.things().findTemplate("MenArrowTowerExpansion")->getFinalOverride();
	logic.random().enableCallLog(true);
	logic.random().clearCallLog();
	Object *made = foundation->createOnSite(*tower, site, 0.0f, *men, true);
	REQUIRE(made);
	CHECK(made->getTemplate()->getFinalOverride() == tower);
	CHECK_FALSE(alive(logic, tree));
	// Boromir: the radius draw (line 0x2AB), then two angles (lines 0x2AF, 0x2B7): the tower is no WALK_ON_TOP_OF_WALL template
	const std::vector<GameLogicRandom::Call> draws = drawsOf(logic, "FoundationAIUpdate.cpp");
	// two units were in the way: Boromir and the tower just built (it has an AI; RW 0x858CFC has no IMMOBILE test: it draws, and its AI takes no move).
	// Each drew the radius (line 0x2AB), then two angles (lines 0x2AF, 0x2B7): the tower is no WALK_ON_TOP_OF_WALL template
	REQUIRE(draws.size() == 6);
	for (size_t k = 0; k < 6; ++k)
	{
		CHECK(draws[k].line == (k % 3 == 0 ? 0x2AB : (k % 3 == 1 ? 0x2AF : 0x2B7)));
	}
	// the model: gi = BOX(50, the tower's x / y half extents * 1.5); radius = gi's circle * 1.5; dest1 = site + (v, 0) turned by a1, dest2 = that turned again by
	// a2; the goal is the nearer of the two (dest1 when the unit is farther from dest2)
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*tower);
	float hx = 0.0f, hy = 0.0f;
	for (const ObjectGeometry::Shape &sh : shapes)
	{
		if (sh.active)
		{
			const float rx = sh.majorRadius, ry = sh.type == ObjectGeometry::SHAPE_BOX ? sh.minorRadius : sh.majorRadius;
			hx = std::max(hx, std::max(SimMath::addf32(sh.offsetX, rx), SimMath::subf32(rx, sh.offsetX)));
			hy = std::max(hy, std::max(SimMath::addf32(sh.offsetY, ry), SimMath::subf32(ry, sh.offsetY)));
		}
	}
	PathfindGeometry box;
	box.type = PATHFIND_GEOMETRY_BOX;
	box.majorRadius = SimMath::mulf32(hx, 1.5f);
	box.minorRadius = SimMath::mulf32(hy, 1.5f);
	box.height = 50.0f;
	const float radius = SimMath::mulf32(box.boundingCircleRadius(), 1.5f);
	auto model = [&](size_t k, const Coord3D &at) {
		const float v = NumericState::fstpDword(NumericState::pc24MulW(draws[k].rresult, radius));
		const float s1 = SimMath::sinf32(draws[k + 1].rresult), c1 = SimMath::cosf32(draws[k + 1].rresult);
		const float vx = SimMath::subf32(SimMath::mulf32(c1, v), SimMath::mulf32(s1, 0.0f)), vy = SimMath::addf32(SimMath::mulf32(s1, v), SimMath::mulf32(c1, 0.0f));
		const float s2 = SimMath::sinf32(draws[k + 2].rresult), c2 = SimMath::cosf32(draws[k + 2].rresult);
		const float wx = SimMath::subf32(SimMath::mulf32(c2, vx), SimMath::mulf32(s2, vy)), wy = SimMath::addf32(SimMath::mulf32(vx, s2), SimMath::mulf32(c2, vy));
		const Coord3D d1{ SimMath::addf32(site.x, vx), SimMath::addf32(site.y, vy), site.z }, d2{ SimMath::addf32(site.x, wx), SimMath::addf32(site.y, wy), site.z };
		const float e1 = SimMath::addf32(SimMath::mulf32(SimMath::subf32(at.y, d1.y), SimMath::subf32(at.y, d1.y)), SimMath::mulf32(SimMath::subf32(at.x, d1.x), SimMath::subf32(at.x, d1.x)));
		const float e2 = SimMath::addf32(SimMath::mulf32(SimMath::subf32(at.y, d2.y), SimMath::subf32(at.y, d2.y)), SimMath::mulf32(SimMath::subf32(at.x, d2.x), SimMath::subf32(at.x, d2.x)));
		return e2 > e1 ? d1 : d2;
	};
	AIUpdateInterface *bai = boromir->getAIUpdateInterface();
	AIUpdateInterface *tai = made->getAIUpdateInterface();
	REQUIRE(tai);
	CHECK(bai->evenIfSleepingOrders() == 1u);
	CHECK(tai->evenIfSleepingOrders() == 0u);
	// the partition's order decides who drew first (pinned below): Boromir has one triple, the tower the other
	const Coord3D b0 = model(0, *boromir->getPosition()), b3 = model(3, *boromir->getPosition());
	const bool boromirFirst = bai->lastEvenIfSleepingGoal().x == b0.x && bai->lastEvenIfSleepingGoal().y == b0.y;
	const Coord3D wantBoromir = boromirFirst ? b0 : b3;
	CHECK(bai->lastEvenIfSleepingGoal().x == wantBoromir.x);
	CHECK(bai->lastEvenIfSleepingGoal().y == wantBoromir.y);
	CHECK(made->getPosition()->x == site.x);
	CHECK(made->getPosition()->y == site.y);
	logic.random().enableCallLog(false);
	logic.runLogicFrame();
	REQUIRE(bai->isMoving());
	int walked = 0;
	while (bai->isMoving() && walked < 300)
	{
		logic.runLogicFrame();
		++walked;
	}
	// pinned (measured, Linux / GCC): the partition gives the new tower the first triple and Boromir the second; he walks 15 frames to the cell the move
	// state adjusted the ordered spot to
	CHECK_FALSE(boromirFirst);
	// Lane TARGET-1: was 15 frames to (1143119872, 1166701568): the mood scans' random offset draws (RW 0x6686F0) come before the pad's draws
	CHECK(walked == 22);
	CHECK(bitsOf(boromir->getPosition()->x) == 1142300672u);
	CHECK(bitsOf(boromir->getPosition()->y) == 1167070208u);
	logic.random().enableCallLog(false);
}

// ---- S-951: a player's unpack and pack of a camp (MSG_CASTLE_UNPACK 1085, MSG_CASTLE_PACK 1086, MSG_CASTLE_UNPACK_EXPLICIT_OBJECT 1087) ----------------------------
namespace
{
struct CampGame
{
	buildtest::Game g;
	LiveGame *live = nullptr;
	GameLogic *logic = nullptr;
	Player *men = nullptr;
	int pi = -1;
	CampGame(starttest::Shared &s)
	{
		std::string error;
		REQUIRE_MESSAGE(buildtest::startGame(s, "FactionMen", "FactionMordor", 4242, g, &error), error);
		live = g.live.get();
		logic = &live->logic();
		men = live->players().findPlayerWithName(live->report().startSlotPlayers[0]);
		REQUIRE(men);
		pi = men->getPlayerIndex();
	}
	// a flag placed in the open with a Men hero beside it: the capture scan (RW 0x79B3C4, every 5 frames) gives it to Men
	Object *capturedFlag(const char *name, const Coord3D &at)
	{
		Object *flag = spawn(*logic, name, *men, at);
		flag->friend_onBuildComplete();
		spawn(*logic, "GondorBoromir", *men, { at.x + 40.0f, at.y, at.z });
		for (int i = 0; i < 12; ++i)
		{
			logic->runLogicFrame();
		}
		REQUIRE(flag->getControllingPlayer() == men);
		live->dispatch().dispatch(selectMsg(pi, flag->getID()));
		return flag;
	}
	void setMoney(std::uint32_t amount)
	{
		men->getMoney()->withdraw(men->getMoney()->countMoney(), nullptr, false);
		men->getMoney()->deposit(amount, nullptr, false);
	}
	std::uint32_t money() const { return men->getMoney()->countMoney(); }
};
} // namespace

TEST_CASE("BACKLOG2 S-951: a captured camp flag's unpack (MSG_CASTLE_UNPACK) costs its side's 2000, unpacks Camp_Men over FadeTime + BuildTime, and refuses when too poor or busy; pack takes it down (RW 0x77C2C1 / 0x77C265)")
{
	OPENBFME_REQUIRE_START(s);
	CampGame c(*s);
	GameLogic &logic = *c.logic;
	Object *flag = c.capturedFlag("CampFlag", { 1500.0f, 1500.0f, 0.0f });
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(cb);
	REQUIRE(cb->state() == CastleBehavior::STATE_IDLE);
	REQUIRE(cb->data()->entryFor("Men"));
	CHECK(cb->data()->entryFor("Men")->cost == 2000u); // civilianbuildings.ini: CastleToUnpackForFaction = Men Camp_Men 2000
	CHECK(cb->unpackCost(c.men) == 2000u);
	// too poor: refused, nothing paid, nothing changes (the whole state hash)
	c.setMoney(1999);
	const std::uint32_t before = logic.computeStateHash();
	c.live->dispatch().dispatch(GameMessage(MSG_CASTLE_UNPACK, c.pi));
	CHECK(logic.computeStateHash() == before);
	CHECK(c.money() == 1999u);
	CHECK(cb->state() == CastleBehavior::STATE_IDLE);
	// malformed (the client sends no argument): with the money to pay, an unpack carrying an argument is refused by name and nothing changes
	c.setMoney(2500);
	{
		const size_t errors0 = logic.report().errors.size();
		const std::uint32_t h0 = logic.computeStateHash();
		GameMessage bad(MSG_CASTLE_UNPACK, c.pi);
		bad.appendIntegerArgument(1);
		c.live->dispatch().dispatch(bad);
		CHECK(logic.computeStateHash() == h0);
		CHECK(c.money() == 2500u);
		CHECK(cb->state() == CastleBehavior::STATE_IDLE);
		const std::vector<std::string> errors = logic.report().errors;
		REQUIRE(errors.size() == errors0 + 1);
		CHECK(errors.back().find("MSG_CASTLE_UNPACK from player") == 0);
		CHECK(errors.back().find("malformed") != std::string::npos);
	}
	// enough: paid at once, the price kept (+ 0x4C), state 1
	c.live->dispatch().dispatch(GameMessage(MSG_CASTLE_UNPACK, c.pi));
	CHECK(c.money() == 500u);
	CHECK(cb->storedPrice() == 2000.0f);
	CHECK(cb->state() == CastleBehavior::STATE_PACKED);
	// busy: a second order is refused and costs nothing
	c.setMoney(5000);
	c.live->dispatch().dispatch(GameMessage(MSG_CASTLE_UNPACK, c.pi));
	CHECK(c.money() == 5000u);
	// the timed unpack: state 2 on the module's next update (state 0 sleeps 5 frames, RW 0x79D0D5; the layout is made then), FadeTime 2 s, BuildTime 5 s
	// (default), then state 4 with the keep
	int wait = 0;
	while (cb->state() == CastleBehavior::STATE_PACKED && wait < 5)
	{
		logic.runLogicFrame();
		++wait;
	}
	CHECK(cb->state() == CastleBehavior::STATE_UNPACKING);
	// Camp_Men: the camp's floor, keep, five walls, five defence nodes and six building foundations (the rock goes to the neutral player)
	std::map<std::string, int> made;
	Object *keep = nullptr;
	for (ObjectID id : cb->ownedObjects())
	{
		Object *o = logic.findObjectByID(id);
		REQUIRE(o);
		++made[o->getTemplate()->getName()];
		keep = o->getTemplate()->getName() == "GondorCampKeep" ? o : keep;
	}
	CHECK(made["GondorCampKeep"] == 1);
	CHECK(made["GBCampWall"] == 5);
	CHECK(made["GondorBuildingFoundation"] == 6);
	REQUIRE(keep);
	CHECK(keep->getControllingPlayer() == c.men);
	int frames = 0;
	while (cb->state() != CastleBehavior::STATE_UNPACKED && frames < 100)
	{
		logic.runLogicFrame();
		++frames;
	}
	CHECK(cb->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(frames == 36); // FadeTime 2 s and BuildTime 5 s at 5 frames a second, plus the frame on which the binary32 timer of RW 0x79CF2A first falls below 0
	// pack (MSG_CASTLE_PACK): abandon at once (state 5), then after FadeTime the members are destroyed
	const std::vector<ObjectID> owned = cb->ownedObjects();
	{
		// a pack carrying an argument is malformed: refused by name, the camp stays unpacked, the hash unchanged
		const size_t errors0 = logic.report().errors.size();
		const std::uint32_t h0 = logic.computeStateHash();
		GameMessage bad(MSG_CASTLE_PACK, c.pi);
		bad.appendBooleanArgument(true);
		c.live->dispatch().dispatch(bad);
		CHECK(logic.computeStateHash() == h0);
		CHECK(cb->state() == CastleBehavior::STATE_UNPACKED);
		const std::vector<std::string> errors = logic.report().errors;
		REQUIRE(errors.size() == errors0 + 1);
		CHECK(errors.back().find("MSG_CASTLE_PACK from player") == 0);
		CHECK(errors.back().find("malformed") != std::string::npos);
	}
	c.live->dispatch().dispatch(GameMessage(MSG_CASTLE_PACK, c.pi));
	CHECK(cb->state() == CastleBehavior::STATE_ABANDONED);
	for (int i = 0; i < 12; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK(cb->state() == CastleBehavior::STATE_IDLE);
	CHECK_FALSE(alive(logic, keep->getID()));
	size_t standing = 0;
	for (ObjectID id : owned)
	{
		const Object *o = logic.findObjectByID(id);
		standing += o && !o->isDestroyed() && o->getControllingPlayer() == c.men ? 1 : 0;
	}
	CHECK(standing == 0u);
}

TEST_CASE("BACKLOG2 S-951: an economy plot's explicit unpack (MSG_CASTLE_UNPACK_EXPLICIT_OBJECT) pays the template's price once and raises it on the plot; malformed messages are refused unchanged (RW 0x77C338 / 0x797B06)")
{
	OPENBFME_REQUIRE_START(s);
	CampGame c(*s);
	GameLogic &logic = *c.logic;
	Object *flag = c.capturedFlag("EconomyPlotFlag", { 1500.0f, 1500.0f, 0.0f });
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(cb);
	const ThingTemplate *farm = logic.things().findTemplate("RohanFarmMultiplayer");
	REQUIRE(farm);
	c.setMoney(5000);
	const int price = BuildAssistant::calcCostToBuild(*farm, c.men, nullptr, -1);
	REQUIRE(price == 200); // gamedata.ini: ROHAN_FLAG_ECONOMY_UNPACK_FARM_COST
	// malformed: no argument, a real instead of the template id, two arguments: refused, reported, the world unchanged
	const size_t errorsBefore = logic.report().errors.size();
	const std::uint32_t before = logic.computeStateHash();
	c.live->dispatch().dispatch(GameMessage(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, c.pi));
	GameMessage real(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, c.pi);
	real.appendRealArgument(1.0f);
	c.live->dispatch().dispatch(real);
	GameMessage two(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, c.pi);
	two.appendIntegerArgument((int)farm->getTemplateID());
	two.appendIntegerArgument(0);
	c.live->dispatch().dispatch(two);
	// an id no template has: nothing (RW 0x77C376)
	GameMessage unknown(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, c.pi);
	unknown.appendIntegerArgument(0xFFFF);
	c.live->dispatch().dispatch(unknown);
	const std::vector<std::string> errors = logic.report().errors;
	REQUIRE(errors.size() == errorsBefore + 4);
	for (size_t k = 0; k < 3; ++k)
	{
		CHECK(errors[errorsBefore + k].find("MSG_CASTLE_UNPACK_EXPLICIT_OBJECT from player") == 0);
		CHECK(errors[errorsBefore + k].find("malformed") != std::string::npos);
	}
	CHECK(errors.back().find("template id 65535 names no template: refused") != std::string::npos);
	CHECK(logic.computeStateHash() == before);
	CHECK(c.money() == 5000u);
	// the farm: its price paid once (RW 0x797B06; construct RW 0x858701 charges nothing: the flag has no CastleMemberBehavior), then it rises on the flag
	GameMessage m(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT, c.pi);
	m.appendIntegerArgument((int)farm->getTemplateID());
	c.live->dispatch().dispatch(m);
	CHECK(c.money() == 5000u - (std::uint32_t)price);
	CHECK(cb->storedPrice() == (float)price);
	CHECK(cb->explicitTemplateName() == "RohanFarmMultiplayer");
	for (int i = 0; i < 5 && cb->state() == CastleBehavior::STATE_PACKED; ++i)
	{
		logic.runLogicFrame();
	}
	CHECK(cb->explicitTemplateName().empty());
	Object *built = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		built = o->getTemplate()->getFinalOverride() == farm->getFinalOverride() && !o->isDestroyed() ? o : built;
	}
	REQUIRE(built);
	CHECK(built->getControllingPlayer() == c.men);
	CHECK(built->getProducerID() == flag->getID());
	CHECK(built->getPosition()->x == flag->getPosition()->x);
	CHECK(built->getPosition()->y == flag->getPosition()->y);
	CHECK(c.money() == 5000u - (std::uint32_t)price);
}

// ---- review r1: a construction determinism vector with several units, movement effects, the wall segment's 20 % call site -----------------------------------------
namespace
{

struct VectorRun
{
	std::vector<std::string> drawOrder;    // the unit that took each (radius, angle) pair, in draw order
	std::vector<std::uint32_t> goals;      // x, y bits of each unit's order (Boromir, Aragorn, Faramir)
	std::vector<std::uint32_t> positions;  // x, y bits of each unit after the run
	std::vector<std::uint32_t> hashes;     // the state hash after every frame
};

// three Men heroes stand on a fortress pad; the player builds a tower there through the frame's command list (the path a replay or a peer feeds)
VectorRun runPadVector(starttest::Shared &s, int frames)
{
	VectorRun out;
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(men);
	Object *pad = nullptr;
	for (Object *o = logic.getFirstObject(); o && !pad; o = o->getNextObject())
	{
		pad = (o->getControllingPlayer() == men && o->getTemplate()->getName() == "MenFortressExpansionPadSide") ? o : nullptr;
	}
	REQUIRE(pad);
	const Coord3D site = *pad->getPosition();
	const char *names[3] = { "GondorBoromir", "GondorAragorn", "GondorFaramir" };
	Object *units[3] = { spawn(logic, names[0], *men, { site.x + 6.0f, site.y, site.z }), spawn(logic, names[1], *men, { site.x - 6.0f, site.y + 3.0f, site.z }),
		spawn(logic, names[2], *men, { site.x, site.y - 7.0f, site.z }) };
	logic.runLogicFrame();
	for (Object *u : units)
	{
		REQUIRE_FALSE(u->getAIUpdateInterface()->isMoving());
	}
	const ThingTemplate *tower = logic.things().findTemplate("MenArrowTowerExpansion")->getFinalOverride();
	logic.random().enableCallLog(true);
	logic.random().clearCallLog();
	live.commands().append(selectMsg(men->getPlayerIndex(), pad->getID()));
	GameMessage m(MSG_FOUNDATION_CONSTRUCT, men->getPlayerIndex());
	m.appendIntegerArgument((int)tower->getTemplateID());
	m.appendLocationArgument(site);
	m.appendRealArgument(0.0f);
	live.commands().append(m);
	logic.runLogicFrame();
	out.hashes.push_back(logic.computeStateHash());
	const std::vector<GameLogicRandom::Call> draws = drawsOf(logic, "BuildAssistant.cpp");
	logic.random().enableCallLog(false);
	REQUIRE(draws.size() == 6);
	for (size_t k = 0; k < 6; k += 2)
	{
		const Coord3D want = buildAssistantGoal(*tower, site, draws[k].rresult, draws[k + 1].rresult);
		std::string who = "?";
		for (int u = 0; u < 3; ++u)
		{
			const Coord3D got = units[u]->getAIUpdateInterface()->lastEvenIfSleepingGoal();
			if (got.x == want.x && got.y == want.y)
			{
				who = names[u];
			}
		}
		out.drawOrder.push_back(who);
	}
	for (Object *u : units)
	{
		CHECK(u->getAIUpdateInterface()->evenIfSleepingOrders() == 1u);
		out.goals.push_back(bitsOf(u->getAIUpdateInterface()->lastEvenIfSleepingGoal().x));
		out.goals.push_back(bitsOf(u->getAIUpdateInterface()->lastEvenIfSleepingGoal().y));
	}
	for (int i = 1; i < frames; ++i)
	{
		logic.runLogicFrame();
		out.hashes.push_back(logic.computeStateHash());
	}
	for (Object *u : units)
	{
		out.positions.push_back(bitsOf(u->getPosition()->x));
		out.positions.push_back(bitsOf(u->getPosition()->y));
	}
	return out;
}
} // namespace

TEST_CASE("BACKLOG2 r1 determinism: three units on a build site: the draw assignment, the orders, the movement and the hashes are the same in two runs and are the pinned ones (RW 0x797465)")
{
	OPENBFME_REQUIRE_START(s);
	const VectorRun a = runPadVector(*s, 40);
	const VectorRun b = runPadVector(*s, 40);
	CHECK(a.drawOrder == b.drawOrder);
	CHECK(a.goals == b.goals);
	CHECK(a.positions == b.positions);
	CHECK(a.hashes == b.hashes);
	std::string measured = "order";
	for (const std::string &n : a.drawOrder) measured += " " + n;
	measured += " goals";
	for (std::uint32_t v : a.goals) measured += " 0x" + [](std::uint32_t x) { char buf[16]; std::snprintf(buf, sizeof buf, "%08X", x); return std::string(buf); }(v);
	measured += " positions";
	for (std::uint32_t v : a.positions) measured += " 0x" + [](std::uint32_t x) { char buf[16]; std::snprintf(buf, sizeof buf, "%08X", x); return std::string(buf); }(v);
	char last[16];
	std::snprintf(last, sizeof last, "%08X", a.hashes.back());
	measured += std::string(" lastHash 0x") + last;
	INFO(measured);
	// the pins (measured on the Deck, Linux / GCC; the gate hosts must agree): the partition's order gives Boromir the first pair of draws, Faramir the second,
	// Aragorn the third
	CHECK(a.drawOrder == std::vector<std::string>{ "GondorBoromir", "GondorFaramir", "GondorAragorn" });
	// Lane TARGET-1: the goals, positions and hash moved (were 0x4412CD2B .. / 0x4413A000 .. / 0x0EFDBFD5): the mood scans' one-off random offsets
	// (RW 0x6686F0) draw from the logic generator before the pad's draws, the new AI state (AI + 0x3BC / + 0x1A4) and AIData rows are hashed; the hash moved
	// again (0x9D9B68D1 -> 0xA0300E3C, goals and positions unchanged) when the attack-move order pushed its mood check back (RW 0x662DD9) instead of zeroing it
	// lane ATTACKPATH-1 (RotWK's attack path, the doPathfind failure branch, the towards-goal line's corridor term RW 0x6F6D57): Aragorn ends elsewhere, the others
	// do not; positions and the last hash re-measured on the lane's tree (the Deck and a CI host agree). Was 0x43FDF2A9 0x458D2F2B / 0x0EFDBFD5
	CHECK(a.goals == std::vector<std::uint32_t>{ 0x4429DD96u, 0x458EC911u, 0x441570DFu, 0x458BC0F4u, 0x440CCC09u, 0x458F1B95u });
	// merge queue q23 (TARGET-1's mood-scan draws with ATTACKPATH-1's attack path and corridor term, measured once on the merged tree, the Mac and the laptop
	// agreeing): the goals are TARGET-1's, Aragorn ends elsewhere (was 0x4425E21A 0x458D2BBD) and the last hash is 0x165E2BAF (TARGET-1: 0xA0300E3C)
	// merge queue q26: community #74's spiral (RW 0x6FA2A4) moves Faramir and Aragorn as #74's own port measured (were 0x4422A000 0x458CA400 / 0x44224BB5
	// 0x458CE977), the goals unchanged
	// merge queue q27: LOS-1 (the terrain line of sight in the target search and the attack view) moves Aragorn as LOS-1's own port measured (was
	// 0x44246B33 0x458D5A9A), the goals unchanged; the last hash 0x4BFCBFF6 -> 0x5572D938 (measured at each merge point: LOS-1 alone)
	CHECK(a.positions == std::vector<std::uint32_t>{ 0x442A2000u, 0x458ED400u, 0x44202000u, 0x458CA400u, 0x4413A000u, 0x458D9400u });
	// merge queue q24: the last hash 0x165E2BAF -> 0x0E2AE1AF, goals and positions unchanged: OFFMAP-1 (RotWK's idle state RW 0x745A64: no ZH frame <= 1 snap, no
	// pathfinder position update) moves it (bisected: the q24 tree with CAMPUI-1 alone keeps q23's hash); measured once on the merged tree
	// merge queue q26 (community #73 / #74 with BANNER-1, SIEGE-1 and the rest, measured once on the merged tree): 0x0E2AE1AF -> 0x4BFCBFF6
	// merge queue q28 (measured once on the merged tree and at each merge point): 0x5572D938 -> 0x30F97357
	// lane AIEXPAND-1 (on q27's tree): 0x5572D938 -> 0xAE2CC3E1, goals and positions unchanged: the skirmish AI's state hash holds the EXPANSION heuristic's frame
	// counter (RW 0x97D035); measured on a CI host
	// merge queue q29 (measured once on the merged tree and at each merge point; goals and positions unchanged): 0x30F97357 -> 0xCE63EAC9: community #113
	// (0xCC8A6937: the capture registry in the AI's hash, its FlagCaptureSquad's draws), FBSIM-1 (0x4629C1DE: phantom sites), AIEXPAND-1 (the EXPANSION
	// heuristic's frame counter in the AI's hash)
	// merge queue q30 (measured once on the merged tree and at each merge point; goals and positions unchanged): 0xCE63EAC9 -> 0x3B3C3468: PORTERFOG-1 (an
	// object out of the world keeps its shroud record, RW 0x68C18F / 0x68E31F: the porters at their sites keep their hashed shroud state); #130 keeps it
	CHECK(a.hashes.back() == 0x3B3C3468U);   // q19: BACKLOG-8 hashes the formation and march rows of AIData / GameData in GameLogicSettings::crc (unconditionally) and moves the march;
	                                         // re-measured on the merged tree (a CI host); was 0x78A9CB12 (q18: DEVPORT-3C castle keep gate / Horde removal record, before 0x2BD4804B); goals and positions unchanged
	// every unit left the spot it stood on
	CHECK(a.positions[0] != a.goals[0]);
}

TEST_CASE("BACKLOG2 r1 S-653: a wall segment that repairs itself sends the allied unit on it away on the one frame its health stands within a heal above 20 %, and on no other (RW 0x857FD6 .. 0x85805E)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 4242, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *men = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(men);
	const Coord3D site{ 1500.0f, 1500.0f, 0.0f };
	Object *wall = spawn(logic, "IsengardCastleWallSegment", *men, site); // a WALL_SEGMENT whose GettingBuiltBehavior has no WorkerName: it heals itself
	wall->friend_onBuildComplete();
	for (int i = 0; i < 8; ++i)
	{
		logic.runLogicFrame();
	}
	REQUIRE_FALSE(wall->isUnderConstruction());
	BodyModuleInterface *body = wall->getBodyModule();
	REQUIRE(body);
	const float max = body->getMaxHealth();
	const float threshold = NumericState::fstpDword(NumericState::pc24MulW((double)max, (double)0.2f));
	// down to a tenth of its health, then Boromir beside it
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_amount = SimMath::mulf32(max, 0.9f);
	body->attemptDamage(info);
	REQUIRE_FALSE(wall->isEffectivelyDead());
	Object *boromir = spawn(logic, "GondorBoromir", *men, { site.x, site.y + 3.0f, site.z });
	AIUpdateInterface *ai = boromir->getAIUpdateInterface();
	for (int i = 0; i < 25; ++i) // past the 4 seconds a damaged structure waits before it builds on (RW 0x68C933)
	{
		logic.runLogicFrame();
	}
	// the repair is started here (GettingBuiltBehavior slot 1, forced: no price), as the structure's own restart would; its update then heals MaxHealth /
	// buildFrames a frame
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(wall->findModule("GettingBuiltBehavior"));
	REQUIRE(gb);
	gb->startConstruction(true);
	REQUIRE(gb->isConstructing());
	const float amount = NumericState::fstpDword(NumericState::pc24DivW((double)max, NumericState::fildU32(gb->buildFrames()))); // RW 0x857FBD
	logic.random().enableCallLog(true);
	int crossings = 0, orderFrames = 0;
	float before = body->getHealth();
	for (int frame = 0; frame < 400 && body->getHealth() != max; ++frame)
	{
		logic.random().clearCallLog();
		const unsigned orders0 = ai->evenIfSleepingOrders();
		const bool still = !ai->isMoving();
		logic.runLogicFrame();
		const float now = body->getHealth();
		// RW 0x857FD6: the health before this frame's heal is at or above 20 % and within one heal of it (threshold <= h < threshold + amount)
		const bool crossed = !(before < threshold) && (double)before < NumericState::pc24AddW((double)threshold, (double)amount);
		const size_t draws = drawsOf(logic, "BuildAssistant.cpp").size();
		if (crossed)
		{
			++crossings;
			CHECK(draws == 2);
			CHECK(ai->evenIfSleepingOrders() == orders0 + (still ? 1u : 0u));
			CHECK(still);
			orderFrames += ai->evenIfSleepingOrders() != orders0 ? 1 : 0;
		}
		else
		{
			CHECK(draws == 0);
			CHECK(ai->evenIfSleepingOrders() == orders0);
		}
		before = now;
	}
	logic.random().enableCallLog(false);
	CHECK(body->getHealth() == max);
	CHECK(crossings == 1);
	CHECK(orderFrames == 1);
}

// ---- review r1: the camp flag's button, from its availability through the press to the world ------------------------------------------------------------------------
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalView.h"

namespace
{
struct BarHud
{
	MouseSettings mouse;
	std::unique_ptr<INIEnvironment> metaEnv;
	MetaMap meta;
	RecordingShellServices services;
	PinholeView view{ { 0, -400, 500 }, { 0, 0, 0 }, 1024, 768, 0.7f };
	std::unique_ptr<InGameHud> hud;
	BarHud(starttest::Shared &s, LiveGame &live)
	{
		std::string error;
		REQUIRE_MESSAGE(MouseSettings::load(*s.mount->fs, mouse, &error), error);
		metaEnv = std::make_unique<INIEnvironment>();
		metaEnv->fileSystem = s.mount->fs.get();
		meta.registerBlocks(*metaEnv);
		REQUIRE_MESSAGE(meta.load(*metaEnv, "Data\\INI\\CommandMap.ini", &error), error);
		REQUIRE_MESSAGE(meta.load(*metaEnv, "CommandMap.ini", &error), error);
		InGameHud::Config cfg{ live, *s.world, *s.mount->fs, services, view, mouse, meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	// the state of the named button of the selected object's bar (Hidden when the bar does not show it)
	const ControlBarButton *button(ObjectID id, const char *name)
	{
		hud->input().ui().selectObject(id);
		ControlBar &bar = hud->controlBar();
		bar.update();
		for (const std::vector<ControlBarButton> *list : { &bar.palantirButtons(), &bar.sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button && b.button->m_name == name)
				{
					return &b;
				}
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("BACKLOG2 r1 S-951: the camp flag's Construct Camp button is cannot-afford when poor, active when the price is there, and its press pays and unpacks the camp; then the bar no longer offers it (RW 0x942CA9 / 0x940AB6 / 0x77C2C1)")
{
	OPENBFME_REQUIRE_START(s);
	CampGame c(*s);
	GameLogic &logic = *c.logic;
	Object *flag = c.capturedFlag("CampFlag", { 1500.0f, 1500.0f, 0.0f });
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(cb);
	BarHud h(*s, *c.live);
	const auto ctx = h.hud->enterContext();
	// poor: shown, cannot afford (availability 6); a press does nothing
	c.setMoney(1999);
	const ControlBarButton *b = h.button(flag->getID(), "Command_UnpackCamp");
	REQUIRE(b);
	CHECK(b->state == ButtonState::CantAfford);
	CHECK_FALSE(h.hud->controlBar().pressButton(b->slot, b->inPalantir));
	// the price is there and the unpack delay is over: active (availability 2)
	c.setMoney(2500);
	b = h.button(flag->getID(), "Command_UnpackCamp");
	REQUIRE(b);
	CHECK(b->state == ButtonState::Active);
	// the press sends MSG_CASTLE_UNPACK; the next logic frame pays the 2000 and starts the timed unpack
	REQUIRE(h.hud->controlBar().pressButton(b->slot, b->inPalantir));
	h.hud->update(0.2);
	c.live->advance(0.2);
	CHECK(c.money() == 500u);
	CHECK(cb->state() != CastleBehavior::STATE_IDLE);
	CHECK(cb->storedPrice() == 2000.0f);
	// while it unpacks and once it stands, the bar does not offer the button (availability 3: canUnpack is false)
	b = h.button(flag->getID(), "Command_UnpackCamp");
	CHECK(b == nullptr); // a hidden button is not listed (ControlBar::update)
	int frames = 0;
	while (cb->state() != CastleBehavior::STATE_UNPACKED && frames < 100)
	{
		logic.runLogicFrame();
		++frames;
	}
	REQUIRE(cb->state() == CastleBehavior::STATE_UNPACKED);
	int keeps = 0;
	for (ObjectID id : cb->ownedObjects())
	{
		const Object *o = logic.findObjectByID(id);
		keeps += o && o->getTemplate()->getName() == "GondorCampKeep" && o->getControllingPlayer() == c.men ? 1 : 0;
	}
	CHECK(keeps == 1);
	b = h.button(flag->getID(), "Command_UnpackCamp");
	CHECK(b == nullptr); // a hidden button is not listed (ControlBar::update)
}
