// OpenBFME retail tests for lane IDLEMOTION-1 (found by MERGEQ-28: in a Udun 2v2 Hard computer game a Rivendell lancer stood 14 frames with MOVING after a crush
// strike while its horde rode on, then the horde jumped back onto it). The cause, read from RotWK 2.01 with Ghidra: while a horde runs a fleeing target down
// (RUNNING_DOWN_FROM_BEHIND, RW 0x74AA03), its run-down orders every member whose AI state is not active to attack the target (RW 0x87631F; AI vslot 0x1BC
// RW 0x662B3C asks the current state's slot 0x24, false for AIBusyState, vtable RW 0xC274F8). A crushing member enters AIAttackMeleeSquishState (RW 0x74EF4B ->
// setState 0xE9), whose onEnter destroys its path (RW 0x74F2CD) and requests one (RW 0x742271 -> RW 0x667ED1): the queue serves it at the next phase
// (RW 0x62E69F), so the member stands that frame. The horde's pass (phase 5) finds it beyond MeleeAttackLeashDistance of the marching horde and of its slot
// and makes it busy (RW 0x877B4F: RW 0x852E2A(0, 2), only an AIBusyState answers AI vslot 0x1C4) with a walk to its slot and MOVING (RW 0x8751E9, RW 0x87520A).
// The horde's AI update runs before the member's in the next frame (both in updates[0], object order): the run-down orders the busy member again. The member
// stands with MOVING until the run-down ends; the melee behaviour then recentres the horde on the member nearest the members' centroid (RW 0x86F18F): the
// "snap back". Every rule is RotWK's; the expectations here are that outcome on the retail data. The lane also ports two slips met on the way: the run-down's
// member test (the port asked "not in an attack state") and the crush state's request (the port adjusted the point and reserved it as a final goal; RW clears
// the adjust byte + 0x48 at RW 0x74F28D and requests the point as not final, RW 0x742425). SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"
#include "Idle1TurnProbe.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SimMath.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("idlemotion1 retail");
}

// two enemy sides on a flat arena with the retail economy and AI data (test_hud_horde2_retail.cpp's arena, wider: the run-down crosses 800 units)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(160, 120)
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
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle)
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
};

// ElvenRivendellLancerHorde (side 0 at x = 300) attacks MordorFighterHorde (side 1 at x = 420, facing east), which marches east away from it: the orcs flee
// (RW 0x7468D5: they face away and walk faster than a quarter of their speed) and the lancers run them down. Returns the frame RUNNING_DOWN_FROM_BEHIND is first
// set on the lancer horde (-1: never within 200 frames).
int startRunDown(Arena &a, Object *&lancers, Object *&orcs)
{
	lancers = a.place("ElvenRivendellLancerHorde", 0, 300.0f, 600.0f, 0.0f);
	orcs = a.place("MordorFighterHorde", 1, 420.0f, 600.0f, 0.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	orcs->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 1500.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
	REQUIRE(lancers->getAIUpdateInterface()->aiAttackObject(orcs, CMD_FROM_PLAYER));
	const unsigned runDown = (unsigned)CombatNames::statuses().runningDownFromBehind;
	for (int f = 0; f < 200; ++f)
	{
		a.logic.runLogicFrame();
		if (lancers->testStatus(runDown))
		{
			return f;
		}
	}
	return -1;
}

std::vector<Object *> membersOf(Object *horde)
{
	const auto *items = horde->getContain()->getContainedItemsList();
	return std::vector<Object *>(items->begin(), items->end());
}
} // namespace

TEST_CASE("idlemotion1 retail: a lancer left behind while its horde runs a fleeing horde down stands busy with MOVING on the leash's frames and catches up (RW 0x87631F, "
          "RW 0x877B4F)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionElves", "FactionMordor");
	Object *lancers = nullptr, *orcs = nullptr;
	const int start = startRunDown(a, lancers, orcs);
	MESSAGE("run-down from frame " << start);
	// merge queue q29 (measured at IDLEMOTION-1's merge point and on the merged tree): the run-down starts at 59, not 65 (the lane's tree had neither
	// AIDATA-1's run-down rules, RW 0x748650 / 0x74AA69, nor PATHPRIO-1 and the other items merged before it; not split per item)
	REQUIRE(start == 59);
	const unsigned runDown = (unsigned)CombatNames::statuses().runningDownFromBehind;
	// the last member is put 120 units behind its horde: beyond the leash of the marching horde and of its slot
	Object *m = membersOf(lancers).back();
	Coord3D p = *lancers->getPosition();
	p.x = SimMath::subf32(p.x, 120.0f);
	m->setPosition(&p);
	const HordeContain *hc = static_cast<const HordeContain *>(lancers->getContain());
	static const int moving = AIUpdateInterface::modelConditionBit("MOVING");
	TurnProbe probe;
	probe.sample(a.logic); // the probe's per-horde leash count starts at the run-down's first frame
	int heldFrames = 0, heldAll = 0, runDownFrames = 0, leashFrames = 0, firstWalk = -1;
	Coord3D last = *m->getPosition();
	const std::vector<Object *> all = membersOf(lancers);
	std::vector<Coord3D> lastAll;
	for (Object *x : all)
	{
		lastAll.push_back(*x->getPosition());
	}
	std::ostringstream log;
	for (int i = 0; i < 40; ++i)
	{
		const unsigned long long leash = hc->passStats().leashCommands;
		a.logic.runLogicFrame();
		probe.sample(a.logic);
		const Coord3D q = *m->getPosition();
		const bool still = q.x == last.x && q.y == last.y;
		const bool rd = lancers->testStatus(runDown);
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		log << "  f" << i << " (" << q.x << "," << q.y << ") state " << ai->currentStateId() << " MOVING " << m->testModelCondition(moving) << " goal " << (int)ai->mover().goalType()
			<< " | horde (" << lancers->getPosition()->x << "," << lancers->getPosition()->y << ") run-down " << rd << " leash " << (hc->passStats().leashCommands - leash) << "\n";
		runDownFrames += rd ? 1 : 0;
		leashFrames += hc->passStats().leashCommands != leash ? 1 : 0;
		heldFrames += still && rd && m->testModelCondition(moving) && ai->currentStateId() == (unsigned)AI_BUSY ? 1 : 0;
		for (size_t k = 0; k < all.size(); ++k)
		{
			const Coord3D xq = *all[k]->getPosition();
			const bool xs = xq.x == lastAll[k].x && xq.y == lastAll[k].y;
			const std::vector<ObjectID> &leashed = hc->leashedInLastPass();
			const bool leashedNow = hc->lastPassFrame() == a.logic.getFrame() && std::find(leashed.begin(), leashed.end(), all[k]->getID()) != leashed.end();
			heldAll += xs && rd && lancers->getAIUpdateInterface()->isMoving() && leashedNow && all[k]->testModelCondition(moving) &&
					all[k]->getAIUpdateInterface()->currentStateId() == (unsigned)AI_BUSY ? 1 : 0;
			lastAll[k] = xq;
		}
		if (!still && firstWalk < 0)
		{
			firstWalk = i;
		}
		last = q;
	}
	MESSAGE("member #" << m->getID() << ": standing busy with MOVING during the run-down " << heldFrames << " frames, run-down frames " << runDownFrames << ", leash frames "
					   << leashFrames << ", first step at " << firstWalk << "; every lancer " << heldAll << "; probe: held by the run-down " << probe.framesHeldByRunDown << ", streaks " << probe.stillMovingStreaks
					   << "\n" << log.str());
	// lane IDLEMOTION-1 (its tree had no AIDATA-1): the run-down lasted 4 frames and the member stood, busy, with MOVING, on every one of them (the leash made
	// it busy each frame and the run-down ordered it again), then walked back; every lancer 29 held frames. Merge queue q29 (measured at IDLEMOTION-1's merge
	// point and on the merged tree): with AIDATA-1's run-down (the goal's speed in the gap, RW 0x74AA69; the status across the horde, RW 0x748650) the run-down
	// runs the whole 40-frame window and the leash reaches the member on 8 of them; it stands busy with MOVING on 3 (frames 2, 3, 8) and catches up between
	// them (its first step at frame 0)
	CHECK(heldFrames == 3);
	CHECK(leashFrames == 8);
	CHECK(runDownFrames == 40);
	CHECK(firstWalk == 0);
	// the idle1 probe (test_exit1_motion.cpp) counts those frames apart (the other lancers stand on leash frames as well) and finds no member standing with
	// MOVING otherwise
	CHECK(probe.framesHeldByRunDown == heldAll);
	CHECK(heldAll == 3);
	CHECK(probe.stillMovingStreaks == 0);
}

TEST_CASE("idlemotion1 retail: the run-down orders a member whose attack has no current state again: AI vslot 0x1BC, not \"in an attack state\" (RW 0x87631F)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionElves", "FactionMordor");
	Object *lancers = nullptr, *orcs = nullptr;
	REQUIRE(startRunDown(a, lancers, orcs) == 59); // the run-down of the test above
	// a lancer attacks one orc; its attack's own machine is then cleared (no current state): RW 0x740ECD answers "not active" (RW 0x742DD0: the machine's
	// current state, none: idle), while the state is still AI_ATTACK_OBJECT
	Object *m = membersOf(lancers).back();
	Object *orc = membersOf(orcs).front();
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	REQUIRE(ai->aiAttackObject(orc, CMD_FROM_AI));
	AIAttackState *attack = dynamic_cast<AIAttackState *>(ai->stateMachineOrNull()->currentState());
	REQUIRE(attack != nullptr);
	REQUIRE(attack->attackMachine() != nullptr);
	attack->attackMachine()->clear();
	CHECK(ai->isAttacking());
	CHECK_FALSE(ai->isStateActive());
	REQUIRE(ai->stateMachineOrNull()->goalObjectID() == orc->getID());
	// the horde's AI update (updates[0], phases 3 / 4, before its members') runs the run-down: the member is ordered onto the run-down's target, the orc horde. The
	// state is read after phase 4, before the horde's pass (updates[1], phase 5) whose leash may make the member busy again
	for (int phase = 1; phase <= 4; ++phase)
	{
		a.logic.update(phase);
	}
	MESSAGE("after phase 4: state " << ai->currentStateId() << ", goal #" << ai->stateMachineOrNull()->goalObjectID() << " (orc #" << orc->getID() << ", orc horde #"
									 << orcs->getID() << ")");
	// an attack ordered on a horde fights its nearest member (AIUpdateCombat.cpp): the goal is now an orc of the horde other than the one it attacked, which lives
	const Object *goal = a.logic.findObjectByID(ai->stateMachineOrNull()->goalObjectID());
	CHECK(ai->currentStateId() == (unsigned)AI_ATTACK_OBJECT);
	REQUIRE(goal != nullptr);
	CHECK(goal != orc);
	CHECK(goal->getContainedBy() == orcs);
	CHECK_FALSE(orc->isEffectivelyDead());
	a.logic.update(5);
	a.logic.update(6);
}
