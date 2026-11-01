// OpenBFME. GPL-3.0. Source-only AI regressions; no enhanced-policy dependencies.
#include "doctest.h"
#include <limits>
#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"

#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
using namespace combattest;
namespace {
struct Scenario {
 CombatWorld w{R"ini(
ChildObject BreachEngine Swordsman
 BuildCost = 100
 CommandPoints = 5
End
Object BreachWall
 KindOf = STRUCTURE IMMOBILE SELECTABLE
 Geometry = BOX
 GeometryMajorRadius = 5
 GeometryMinorRadius = 40
 GeometryHeight = 25
 Body = ActiveBody Body
 MaxHealth = 500
 End
End
ChildObject BreachWorkshop BreachWall
 KindOf = STRUCTURE IMMOBILE FS_FACTORY SELECTABLE
 CommandSet = BreachWorkshopCommands
 Behavior = ProductionUpdate Production
 End
End
)ini"};
 CommandStore commands;
 CommandStore *savedCommands = TheCommandStore;
 SkirmishAIStore store{w.w.keys};
 AISkirmishPlayer bot;
 Scenario() {
  w.combat().setAutoAcquireEnabled(false);
  auto *p = w.players.findPlayerWithName("Alice");
  p->setPlayerType(PLAYER_COMPUTER, true);
  bot.playerIndex = p->getPlayerIndex();
  bot.build.startPosition = {100, 500, 0};
  bot.store = &store;
  commands.setThingFactory(&w.w.things);
  TheCommandStore = &commands;
  store.registerBlocks(w.w.fx.env.blocks);
  w.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
  w.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
  REQUIRE(w.w.load("CommandButton TrainBreach\n Command = UNIT_BUILD\n Object = BreachEngine\nEnd\nCommandSet BreachWorkshopCommands\n 1 = TrainBreach\nEnd\nArmyDefinition TestArmy\n Side = Test\n ArmyMemberDefinition Engine\n Unit = BreachEngine\n PercentageOfArmyPhase1 = 0\n PercentageOfArmyPhase2 = 0\n PercentageOfArmyPhase3 = 0\n End\nEnd\n").empty());
  bot.army = store.findArmy("Test");
 }
 ~Scenario() { TheCommandStore = savedCommands; }
 Object *factory(const char *name, char side, float x, float y) {
  auto *o = w.unit(name, side, x, y);
  AIBaseBuilder::onObjectEntered(*w.logic, bot, *o);
  return o;
 }
};
}
TEST_CASE("source AI reserve: execution withdraws only the shortfall, never the full price")
{
	// DONOR AIBuildable_doBuild.cpp: a 100-cost unit, cash/reserve before and after execution.
	struct Example { unsigned cash, reserve, cashAfter, reserveAfter; bool queued; };
	for (const Example example : { Example{40, 70, 0, 10, true}, Example{140, 70, 40, 70, true}, Example{40, 50, 40, 50, false} })
	{
		CAPTURE(example.cash);
		CAPTURE(example.reserve);
		Scenario s;
		s.factory("BreachWorkshop", 'A', 100, 500);
		auto *player = s.w.players.findPlayerWithName("Alice");
		REQUIRE(s.w.w.load("SkirmishAIData TestData\nEnd\n").empty());
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		const_cast<AISkirmishPlayer *>(manager.findAI(player->getPlayerIndex()))->disabled = true;
		auto *record = manager.record(player->getPlayerIndex());
		REQUIRE(record);
		player->getMoney()->withdraw(player->getMoney()->countMoney(), nullptr, false);
		player->getMoney()->deposit(example.cash, nullptr, false);
		record->setReserve(example.reserve);
		s.bot.build.baseBuilderDisabled = true;
		s.bot.build.economy.disabled = true;
		s.bot.build.units.disabled = true;
		auto request = std::make_shared<AIBuildRequest>();
		request->unit = true;
		request->reserveMoney = true;
		request->templateName = "BreachEngine";
		request->priority = 500;
		s.bot.build.pending.push_back(request);
		AIBaseBuilder::update(*s.w.logic, s.bot);
		CHECK(request->state == (example.queued ? 1 : 0));
		CHECK(player->getMoney()->countMoney() == example.cashAfter);
		CHECK(record->reserve() == example.reserveAfter);
		CHECK(s.bot.build.failed == 0);
	}
}

TEST_CASE("source AI spatial groups: strict neighbor boundary, bridge merge, and removal rebuild")
{
	// DONOR Rva004DFA43Sphere / Rva00283114GroupMerge: squared distance < padding^2 * 2.
	AIPositionSampler sampler;
	std::map<ObjectID, Coord3D> points{{(ObjectID)1, {0, 0, 0}}, {(ObjectID)2, {24, 0, 0}}, {(ObjectID)3, {12, 0, 0}}, {(ObjectID)4, {10, 10, 0}}};
	auto position = [&](ObjectID id) { return points.at(id); };
	sampler.add((ObjectID)1, 10, position);
	sampler.add((ObjectID)2, 10, position);
	REQUIRE(sampler.groups().size() == 2);
	CHECK(sampler.groups()[0].radius == 10.0f);
	sampler.add((ObjectID)3, 10, position);
	REQUIRE(sampler.groups().size() == 1);
	CHECK((sampler.groups()[0].members == std::vector<ObjectID>{(ObjectID)1, (ObjectID)3, (ObjectID)2}));
	CHECK(sampler.groups()[0].center.x == 12.0f);
	CHECK(sampler.groups()[0].radius == 22.0f);
	sampler.groups()[0].selected = true;
	sampler.remove((ObjectID)3, position);
	REQUIRE(sampler.groups().size() == 2);
	CHECK(sampler.groups()[0].center.x == 0.0f);
	CHECK(sampler.groups()[1].center.x == 24.0f);
	CHECK_FALSE(sampler.groups()[0].selected);
	AIPositionSampler boundary;
	boundary.add((ObjectID)1, 10, position);
	boundary.add((ObjectID)4, 10, position);
	CHECK(boundary.groups().size() == 2);
	// Native removal erases one registration, not every duplicate.
	boundary.add((ObjectID)1, 10, position);
	boundary.remove((ObjectID)1, position);
	REQUIRE(boundary.groups().size() == 2);
	CHECK((boundary.groups()[1].members == std::vector<ObjectID>{(ObjectID)1}));
}

TEST_CASE("source AI spatial groups: snapshot preserves selection and registration order")
{
	AIPositionSampler sampler;
	std::map<ObjectID, Coord3D> points{{(ObjectID)1, {0, 0, 0}}, {(ObjectID)2, {24, 0, 0}}, {(ObjectID)3, {12, 0, 0}}};
	auto position = [&](ObjectID id) { return points.at(id); };
	for (ObjectID id : {(ObjectID)1, (ObjectID)2, (ObjectID)3})
		sampler.add(id, 10, position);
	sampler.groups()[0].selected = true;
	XferSave save;
	sampler.xfer(save);
	AIPositionSampler restored;
	XferLoad load(save.data());
	restored.xfer(load);
	REQUIRE(restored.groups().size() == 1);
	CHECK(restored.groups()[0].selected);
	CHECK(restored.members() == sampler.members());
	CHECK(restored.groups()[0].members == sampler.groups()[0].members);
	XferSave again;
	restored.xfer(again);
	CHECK(again.data() == save.data());
	restored.remove((ObjectID)3, position);
	CHECK(restored.groups().size() == 2);
	// An internally inconsistent group cannot load and feed later position lookups.
	restored.groups()[0].members.push_back((ObjectID)999);
	XferSave corrupt;
	restored.xfer(corrupt);
	AIPositionSampler refused;
	XferLoad bad(corrupt.data());
	CHECK_THROWS_WITH(refused.xfer(bad), "skirmish ai: spatial groups differ from registrations");
}

TEST_CASE("source AI defense: native target groups create and recruit SimpleDefense on every difficulty")
{
	for (int difficulty = 0; difficulty < 4; ++difficulty)
	{
		CAPTURE(difficulty);
		Scenario s;
		REQUIRE(s.w.w
					.load("SkirmishAIData NativeDefense\nEnd\nArmyDefinition NativeAlpha\n Side = Alpha\n TacticalAITargets = DEFENSIVE\n "
						  "MaxTeamsPerTarget = 1\nEnd\n")
					.empty());
		Object *base = s.w.unit("BreachWorkshop", 'A', 100, 500);
		s.w.unit("BreachWall", 'A', 120, 500);
		Object *enemy = s.w.unit("Swordsman", 'B', 150, 500);
		std::vector<ObjectID> own;
		for (int i = 0; i < 4; ++i)
			own.push_back(s.w.unit("Swordsman", 'A', 80, (float)(i == 0 ? 490 : 440 + i * 10))->getID());
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		auto *player = s.w.players.findPlayerWithName("Alice");
		player->setSkirmishDifficulty(difficulty);
		for (const auto &group : manager.groups())
			for (int index : group->members)
				const_cast<AISkirmishPlayer *>(manager.findAI(index))->disabled = true;
		auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(player->getPlayerIndex()));
		REQUIRE(ai);
		REQUIRE(ai->army);
		ai->brain.farmKillEarly = false;
		s.w.frames(50);
		s.w.logic->partition().update();
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		REQUIRE(ai->brain.targets.size() == 1);
		CHECK(ai->brain.targets[0].object == enemy->getID());
		CHECK(ai->brain.targets[0].defenseSize == 1);
		CHECK(ai->brain.targets[0].radius == 200.0f);
		REQUIRE(ai->brain.tactics.size() == 1);
		auto &defense = ai->brain.tactics[0];
		CHECK(defense.kind == "SimpleDefense");
		REQUIRE(defense.teams.size() == 1);
		CHECK(defense.teams[0].maximum == 3);
		CHECK(defense.teams[0].type == 1);
		AITacticalAI::updateTeamBuilder(*s.w.logic, *ai);
		CHECK(defense.teams[0].members.size() == 3);
		CHECK((defense.teams[0].members == std::vector<ObjectID>{own[0], own[3], own[2]}));
		CHECK(defense.teams[0].hasRecruitOrigin);
		CHECK(defense.teams[0].recruitOrigin.x == enemy->getPosition()->x);
		CHECK(defense.teams[0].handedOver);
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		CHECK(ai->brain.attacksLaunched == 1);
		CHECK(base->getControllingPlayer() == player);
	}
}

TEST_CASE("source AI defense: small group chance and large group half-army sizing survive snapshots")
{
	for (int structures : {1, 3})
	{
		CAPTURE(structures);
		Scenario s;
		REQUIRE(s.w.w
					.load("SkirmishAIData NativeDefense\nEnd\nArmyDefinition NativeAlpha\n Side = Alpha\n TacticalAITargets = DEFENSIVE\n "
						  "MaxTeamsPerTarget = 1\nEnd\n")
					.empty());
		for (int i = 0; i < structures; ++i)
			s.w.unit("BreachWorkshop", 'A', (float)(100 + i * 20), 500);
		Object *enemy = s.w.unit("Swordsman", 'B', 150, 500);
		for (int i = 0; i < 5; ++i)
			s.w.unit("Swordsman", 'A', 80, (float)(450 + i * 10));
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		for (const auto &group : manager.groups())
			for (int index : group->members)
				const_cast<AISkirmishPlayer *>(manager.findAI(index))->disabled = true;
		const int player = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
		auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(player));
		REQUIRE(ai);
		ai->brain.farmKillEarly = false;
		auto &random = s.w.logic->random();
		random.seedRandom(1);
		GameLogicRandom oracle = random;
		(void)oracle.getValue(0, 0, "AITargetChooser.cpp", 0xB8);
		(void)oracle.getValue(0, 0, "AITargetHeuristicBaseDefense.cpp", 0x56);
		const int roll = oracle.getValue(0, 100, "AITargetHeuristicBaseDefense.cpp", 0x5F);
		random.enableCallLog(true);
		random.clearCallLog();
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		REQUIRE(ai->brain.targets.size() == 1);
		const bool accepts = structures != 1 || roll < 30; // Native 30/101 small-base gate; larger groups do not make this draw.
		CHECK(ai->brain.targets[0].inactive == !accepts);
		CHECK(ai->brain.targets[0].defenseSize == (structures == 1 ? 0 : 2));
		bool gateFound = false;
		for (const auto &call : random.callLog())
			if (call.file == "AITargetHeuristicBaseDefense.cpp" && call.line == 0x5F)
			{
				gateFound = true;
				CHECK(call.lo == 0);
				CHECK(call.hi == 100);
				CHECK(call.result == roll);
			}
		CHECK(gateFound == (structures == 1));
		if (accepts)
		{
			CHECK(ai->brain.targets[0].object == enemy->getID());
			REQUIRE(ai->brain.tactics.size() == 1);
			CHECK(ai->brain.tactics[0].teams[0].maximum == (structures == 1 ? 1 : 2));
			XferSave save;
			manager.xfer(save);
			ai->brain.targets[0].defenseSize = 1;
			XferLoad load(save.data());
			manager.xfer(load);
			const auto *restored = manager.findAI(player);
			REQUIRE(restored);
			CHECK(restored->brain.targets[0].defenseSize == (structures == 1 ? 0 : 2));
			CHECK(restored->brain.tactics[0].kind == "SimpleDefense");
			CHECK(restored->brain.tactics[0].teams[0].hasRecruitOrigin);
			CHECK(restored->brain.tactics[0].teams[0].recruitOrigin.x == enemy->getPosition()->x);
		}
	}
}

TEST_CASE("source AI spatial snapshots: non-finite coordinates are refused")
{
 AIPositionSampler sampler;
 sampler.add((ObjectID)1, 10, [](ObjectID) { return Coord3D{0, 0, 0}; });
 sampler.groups()[0].center.x = std::numeric_limits<float>::quiet_NaN();
 XferSave save;
 sampler.xfer(save);
 AIPositionSampler restored;
 XferLoad load(save.data());
 CHECK_THROWS_WITH(restored.xfer(load), "skirmish ai: non-finite spatial group");
}

TEST_CASE("BFME2 base defense: support-only enemies are excluded and native draw sites are used")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData NativeDefense\nEnd\nArmyDefinition NativeAlpha\n Side = Alpha\n TacticalAITargets = DEFENSIVE\n MaxTeamsPerTarget = 1\nEnd\nChildObject SupportOnly Swordsman\n KindOf = SELECTABLE SUPPORT\nEnd\n").empty());
 s.w.unit("BreachWorkshop", 'A', 100, 500);
 s.w.unit("BreachWall", 'A', 120, 500);
 auto *support = s.w.unit("SupportOnly", 'B', 150, 500);
 REQUIRE(support->isKindOfName("SUPPORT"));
 REQUIRE_FALSE(support->isKindOfName("CAN_ATTACK"));
 auto &manager = s.w.logic->skirmishAI();
 manager.setStore(&s.store);
 manager.newGame(true);
 const int index = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
 auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(index));
 REQUIRE(ai);
 ai->disabled = true;
 ai->brain.farmKillEarly = false;
 s.w.logic->partition().update();
 AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
 REQUIRE(ai->brain.targets.size() == 1);
 CHECK(ai->brain.targets[0].inactive);
 CHECK(ai->brain.tactics.empty());
 auto *enemy = s.w.unit("Swordsman", 'B', 160, 500);
 s.w.logic->partition().update();
 auto &random = s.w.logic->random();
 random.enableCallLog(true);
 random.clearCallLog();
 AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
 CHECK_FALSE(ai->brain.targets[0].inactive);
 CHECK(ai->brain.targets[0].object == enemy->getID());
 unsigned draws = 0;
 for (const auto &call : random.callLog())
  if (call.file == "AITargetHeuristicBaseDefense.cpp")
  {
   ++draws;
   CHECK(call.line == 0x56);
   CHECK(call.lo == 0);
   CHECK(call.hi == 0);
  }
 CHECK(draws == 1);
}

TEST_CASE("BFME2 roaming defense: Easy-only delay and running gate")
{
 Scenario s;
 auto *player = s.w.players.findPlayerWithName("Alice");
 for (int difficulty = 1; difficulty != 4; ++difficulty) {
  player->setSkirmishDifficulty(difficulty);
  CHECK_FALSE(AITacticalAI::roamingDefenseAppliesForTest(*s.w.logic, s.bot));
  CHECK(s.bot.brain.namedCounters.empty());
 }
 player->setSkirmishDifficulty(0);
 CHECK_FALSE(AITacticalAI::roamingDefenseAppliesForTest(*s.w.logic, s.bot));
 CHECK(s.bot.brain.namedCounters["AIRoamingDefenseTactic_NextLogicFrameRun"] == 25);
 s.w.frames(24);
 CHECK_FALSE(AITacticalAI::roamingDefenseAppliesForTest(*s.w.logic, s.bot));
 s.w.frames(1);
 CHECK(AITacticalAI::roamingDefenseAppliesForTest(*s.w.logic, s.bot));
 s.bot.brain.namedCounters["AIRoamingDefenseTactic_IsRunning"] = 1;
 CHECK_FALSE(AITacticalAI::roamingDefenseAppliesForTest(*s.w.logic, s.bot));
}

TEST_CASE("BFME2 roaming defense: zero-team lifecycle and strict economy radius")
{
 for (const int distance : {899, 900, 901}) {
  CAPTURE(distance);
  Scenario s;
  REQUIRE(s.w.w.load("SkirmishAIData RoamData\nEnd\nChildObject RoamFarm BreachWall\n KindOf = STRUCTURE ECONOMY_STRUCTURE\nEnd\nChildObject RoamFortress BreachWall\n KindOf = STRUCTURE DOZER_FACTORY\nEnd\n").empty());
  auto &manager = s.w.logic->skirmishAI();
  manager.setStore(&s.store);
  manager.newGame(true);
  const int index = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
  auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(index));
  REQUIRE(ai);
  ai->disabled = true;
  ai->build.startPosition = {0, 500, 0};
  auto *busy = s.w.unit("Swordsman", 'A', 100, 500);
  busy->setStatus((unsigned)CombatNames::status("IS_LEAVING_FACTORY"), true);
  auto *unit = s.w.unit("Swordsman", 'A', 110, 500);
  auto *other = s.w.unit("Swordsman", 'A', 120, 500);
  auto *farm = s.w.unit("RoamFarm", 'A', (float)distance, 500);
  s.w.unit("RoamFortress", 'A', 200, 500); // Must not be used when economy is registered but out of range.
  REQUIRE(manager.record(index)->economy().size() == 1);
  AITactic tactic;
  tactic.kindIndex = 107;
  tactic.kind = "AIRoamingDefenseTactic";
  tactic.targetless = true;
  auto &random = s.w.logic->random();
  random.enableCallLog(true);
  random.clearCallLog();
  s.w.frames(1);
  AITacticalAI::updateTacticForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.ready);
  CHECK_FALSE(tactic.started);
  REQUIRE(tactic.teams.empty());
  s.w.frames(1);
  AITacticalAI::updateTacticForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.started);
  CHECK(tactic.mainLaunched);
  CHECK_FALSE(tactic.ended);
  AITacticalAI::updateTacticForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.ended);
  CHECK(unit->getAIUpdateInterface()->attackMoveActive() == (distance == 899));
  CHECK_FALSE(busy->getAIUpdateInterface()->attackMoveActive());
  CHECK_FALSE(other->getAIUpdateInterface()->attackMoveActive());
  if (distance < 900) {
   REQUIRE(unit->getAIUpdateInterface()->resumeAttackMove());
   const auto &goal = unit->getAIUpdateInterface()->stateMachine().goalPosition();
   CHECK(goal.x == farm->getPosition()->x);
   CHECK(goal.y == farm->getPosition()->y);
  }
  int draws = 0;
  for (const auto &call : random.callLog()) {
   if (call.file != "AIRoamingDefenseTactic.cpp") { continue; }
   ++draws;
   CHECK(call.line == 164);
   CHECK(call.lo == 0);
   CHECK(call.hi == 0);
  }
  CHECK(draws == (distance == 899 ? 1 : 0));
  CHECK(ai->brain.namedCounters["AIRoamingDefenseTactic_IsRunning"] == 0);
  CHECK(ai->brain.namedCounters["AIRoamingDefenseTactic_NextLogicFrameRun"] == 0);
 }
}

TEST_CASE("BFME2 roaming defense: empty economy uses first fortress or issues no order")
{
 for (bool hasFortress : {false, true}) {
  Scenario s;
  REQUIRE(s.w.w.load("SkirmishAIData RoamData\nEnd\nChildObject RoamFortress BreachWall\n KindOf = STRUCTURE DOZER_FACTORY\nEnd\n").empty());
  auto &manager = s.w.logic->skirmishAI();
  manager.setStore(&s.store);
  manager.newGame(true);
  const int index = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
  auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(index));
  REQUIRE(ai);
  ai->disabled = true;
  auto *unit = s.w.unit("Swordsman", 'A', 100, 500);
  if (hasFortress) {
   s.w.unit("RoamFortress", 'A', 100, 500);
   s.w.unit("RoamFortress", 'A', 3000, 500);
  }
  auto &random = s.w.logic->random();
  // Find an angle-zero seed before exercising the decision: exact coordinates need no trig tolerance.
  unsigned seed = 0;
  for (; seed < 100; ++seed) {
   GameLogicRandom oracle = random;
   oracle.seedRandom(seed);
   if (oracle.getValue(0, 6, "AIRoamingDefenseTactic.cpp", 186) == 0) { break; }
  }
  REQUIRE(seed < 100); // tolerance: bounded fixture seed search, not a simulation-result tolerance.
  random.seedRandom(seed);
  random.enableCallLog(true);
  random.clearCallLog();
  AITactic tactic;
  tactic.kindIndex = 107;
  tactic.targetless = true;
  tactic.started = true;
  tactic.mainLaunched = true;
  AITacticalAI::updateTacticForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.ended);
  CHECK(unit->getAIUpdateInterface()->attackMoveActive() == hasFortress);
  if (hasFortress) {
   REQUIRE(unit->getAIUpdateInterface()->resumeAttackMove());
   CHECK(unit->getAIUpdateInterface()->stateMachine().goalPosition().x == 500);
   CHECK(unit->getAIUpdateInterface()->stateMachine().goalPosition().y == 500);
  }
  REQUIRE(random.callLog().size() == (hasFortress ? 1 : 0));
  if (hasFortress) {
   CHECK(random.callLog()[0].line == 186);
   CHECK(random.callLog()[0].lo == 0);
   CHECK(random.callLog()[0].hi == 6);
  }
 }
}

TEST_CASE("BFME2 tech opportunity: linked sites, danger zones and phase gate")
{
 for (int phase : {0, 1, 2}) {
  for (bool danger : {false, true}) {
   for (bool economy : {false, true}) {
    CAPTURE(phase); CAPTURE(danger); CAPTURE(economy);
    Scenario s;
    REQUIRE(s.w.w.load("SkirmishAIData TechData\nEnd\nObject TechFlag\n KindOf = CAPTUREFLAG\nEnd\nChildObject TechBuilding BreachWall\n KindOf = STRUCTURE LINKED_TO_FLAG\nEnd\nChildObject TechFarm TechBuilding\n KindOf = STRUCTURE LINKED_TO_FLAG ECONOMY_STRUCTURE\nEnd\n").empty());
    auto &manager = s.w.logic->skirmishAI();
    manager.setStore(&s.store);
    manager.newGame(true);
    s.w.logic->partition().setRegion(0, 0, 2000, 2000);
    auto *flag = s.w.unit("TechFlag", 'B', 200, 500);
    auto *building = s.w.unit(economy ? "TechFarm" : "TechBuilding", 'B', 210, 500);
    REQUIRE(building->isKindOfName("LINKED_TO_FLAG"));
    REQUIRE(manager.captureFlags().size() == 1);
    REQUIRE(s.w.players.findPlayerWithName("Alice")->getRelationship(flag->getTeam()) == ENEMIES);
    s.w.logic->partition().update();
    s.bot.build.phase = phase;
    if (danger) { AITacticalAI::addAttackedAtForTest(*s.w.logic, s.bot, {500,500,0}); }
    auto &random = s.w.logic->random();
    random.enableCallLog(true); random.clearCallLog();
    AITarget target;
    AITacticalAI::techBuildingHeuristicForTest(*s.w.logic, s.bot, target);
    const bool accepted = phase == 0 || !danger || economy;
    CHECK(target.inactive == !accepted);
    CHECK(target.object == (accepted ? flag->getID() : INVALID_ID));
    CHECK(random.callLog().empty());
    if (accepted) {
     CHECK(target.position.x == 200);
     CHECK(target.radius == s.store.data().defaultTargetThreatRadius);
    }
   }
  }
 }
}

TEST_CASE("BFME2 tech opportunity: nearest linked object determines alliance, not a farther enemy")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData TechData\nEnd\nObject TechFlag\n KindOf = CAPTUREFLAG\nEnd\nChildObject TechBuilding BreachWall\n KindOf = STRUCTURE LINKED_TO_FLAG\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI();
 manager.setStore(&s.store); manager.newGame(true);
    s.w.logic->partition().setRegion(0, 0, 2000, 2000);
 auto *flag = s.w.unit("TechFlag", 'B', 200, 500);
 s.w.unit("TechBuilding", 'A', 210, 500);
 s.w.unit("TechBuilding", 'B', 220, 500);
 s.w.logic->partition().update();
 AITarget target;
 AITacticalAI::techBuildingHeuristicForTest(*s.w.logic, s.bot, target);
 CHECK(target.inactive);
 // Ownership of the flag is also an independent guard.
 flag->setCapturedTeam(s.w.players.findPlayerWithName("Alice")->getDefaultTeam());
 AITacticalAI::techBuildingHeuristicForTest(*s.w.logic, s.bot, target);
 CHECK(target.inactive);
}

TEST_CASE("BFME2 tech opportunity: missing linked object, inclusive radius and registration-order ties")
{
 for (int distance : {999, 1000, 1001}) {
  Scenario s;
  REQUIRE(s.w.w.load("SkirmishAIData TechData\nEnd\nObject TechFlag\n KindOf = CAPTUREFLAG\nEnd\nChildObject TechBuilding BreachWall\n KindOf = STRUCTURE LINKED_TO_FLAG\nEnd\n").empty());
  auto &manager = s.w.logic->skirmishAI();
  manager.setStore(&s.store); manager.newGame(true);
    s.w.logic->partition().setRegion(0, 0, 2000, 2000);
  s.bot.build.startPosition = {0,0,0};
  auto *first = s.w.unit("TechFlag", 'B', (float)distance, 0);
  s.w.unit("TechFlag", 'B', 0, (float)distance);
  AITarget missing;
  s.w.logic->partition().update();
  AITacticalAI::techBuildingHeuristicForTest(*s.w.logic, s.bot, missing);
  CHECK(missing.inactive);
  s.w.unit("TechBuilding", 'B', (float)distance, 10);
  s.w.unit("TechBuilding", 'B', 10, (float)distance);
  s.w.logic->partition().update();
  AITarget target;
  AITacticalAI::techBuildingHeuristicForTest(*s.w.logic, s.bot, target);
  CHECK(target.inactive == (distance == 1001));
  CHECK(target.object == (distance == 1001 ? INVALID_ID : first->getID()));
 }
}

TEST_CASE("BFME2 farm raid: constructor roll and edition-aware native team template")
{
 for (GameEdition edition : {GameEdition::BFME2_106, GameEdition::RotWK201})
 for (int phase : {0,1,2}) {
  Scenario s;
  s.w.logic->setEdition(edition);
  s.bot.build.phase = phase;
  auto &random = s.w.logic->random();
  random.seedRandom(51027);
  GameLogicRandom oracle = random;
  const int early = oracle.getValue(1,100,"AIFarmKillSquad.cpp",53);
  random.enableCallLog(true); random.clearCallLog();
  AITacticalAI::makeGenerator(*s.w.logic, s.bot);
  CHECK(s.bot.brain.farmKillEarly == (early < 20)); // tolerance: source's exact d100 branch, not an approximate numerical result.
  REQUIRE(random.callLog().size() == 1);
  CHECK(random.callLog()[0].line == 53);
  AITactic tactic;
  AITacticalAI::startFarmKillForTest(*s.w.logic, s.bot, tactic);
  REQUIRE(tactic.teams.size() == 1);
  const auto &team = tactic.teams[0];
  // Retain main’s edition variant: BFME2 0x9AD00B = 30, RW 0x9BB49D = 500.
  CHECK(team.priority == (edition == GameEdition::BFME2_106 ? 30 : 500));
  CHECK(team.minimum == (phase == 0 ? 1 : 2));
  CHECK(team.maximum == random.callLog()[1].result);
  CHECK(team.forbiddenKinds == std::vector<int>{ObjectTemplateInfoBuilder::kindOfIndex("HERO"),ObjectTemplateInfoBuilder::kindOfIndex("ARCHER"),ObjectTemplateInfoBuilder::kindOfIndex("MACHINE")});
  REQUIRE(random.callLog().size() == 2);
  CHECK(random.callLog()[1].line == 371);
  CHECK(random.callLog()[1].lo == team.minimum);
  CHECK(random.callLog()[1].hi == 3);
  CHECK(s.bot.brain.namedCounters["AIFarmKillSquad_IsRunning"] == 1);
 }
}

TEST_CASE("BFME2 farm raid: no fortress draws from tracked registration order")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData RaidData\nEnd\nChildObject RaidFarm BreachWall\n KindOf = STRUCTURE ECONOMY_STRUCTURE\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI();
 manager.setStore(&s.store); manager.newGame(true);
 auto *first = s.w.unit("RaidFarm",'B',300,300);
 auto *second = s.w.unit("RaidFarm",'B',700,700);
 auto *workshop = s.w.unit("BreachWorkshop",'B',800,300);
 const int enemy = s.w.players.findPlayerWithName("Bob")->getPlayerIndex();
 auto &random = s.w.logic->random();
 random.seedRandom(1234); random.enableCallLog(true); random.clearCallLog();
 GameLogicRandom oracle = random;
 const int farm = oracle.getValue(0,1,"AIFarmKillSquad.cpp",304);
 AITactic tactic;
 CHECK(AITacticalAI::farmKillTargetForTest(*s.w.logic,tactic,enemy,false) == (farm == 0 ? first->getID() : second->getID()));
 CHECK(AITacticalAI::farmKillTargetForTest(*s.w.logic,tactic,enemy,true) == workshop->getID());
 REQUIRE(random.callLog().size() == 2);
 CHECK(random.callLog()[0].line == 304);
 CHECK(random.callLog()[0].lo == 0);
 CHECK(random.callLog()[0].hi == 1);
 CHECK(random.callLog()[1].line == 250);
 CHECK(random.callLog()[1].lo == 0);
 CHECK(random.callLog()[1].hi == 0);
}

TEST_CASE("BFME2 farm raid: farms must exceed 300 from the fortress line, production has no cutoff")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData RaidData\nEnd\nChildObject RaidFarm BreachWall\n KindOf = STRUCTURE ECONOMY_STRUCTURE\nEnd\nChildObject RaidFortress BreachWall\n KindOf = STRUCTURE DOZER_FACTORY\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI();
 manager.setStore(&s.store); manager.newGame(true);
 auto *unit = s.w.unit("Swordsman",'A',100,100);
 s.w.unit("RaidFortress",'B',900,100);
 s.w.unit("RaidFarm",'B',500,400); // exactly 300 from horizontal line
 auto *factory = s.w.unit("BreachWorkshop",'B',500,200); // only 100 from it
 const int enemy = s.w.players.findPlayerWithName("Bob")->getPlayerIndex();
 AITactic tactic;
 AITacticTeam team; team.index=0; team.members={unit->getID()}; tactic.teams.push_back(team);
 auto &random = s.w.logic->random();
 random.enableCallLog(true); random.clearCallLog();
 CHECK(AITacticalAI::farmKillTargetForTest(*s.w.logic,tactic,enemy,false) == INVALID_ID);
 CHECK(AITacticalAI::farmKillTargetForTest(*s.w.logic,tactic,enemy,true) == factory->getID());
 auto *first = s.w.unit("RaidFarm",'B',500,700);
 s.w.unit("RaidFarm",'B',600,700); // equal score: first registration wins, not nearest to the finder
 random.clearCallLog();
 CHECK(AITacticalAI::farmKillTargetForTest(*s.w.logic,tactic,enemy,false) == first->getID());
 CHECK(random.callLog().empty());
}

TEST_CASE("BFME2 farm raid: both selectors precede the choice draw, cleanup schedules 30 to 120 seconds")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData RaidData\nEnd\nChildObject RaidFarm BreachWall\n KindOf = STRUCTURE ECONOMY_STRUCTURE\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI();
 manager.setStore(&s.store); manager.newGame(true);
 auto *unit = s.w.unit("Swordsman",'A',100,100);
 auto *farm = s.w.unit("RaidFarm",'B',500,700);
 auto *factory = s.w.unit("BreachWorkshop",'B',500,200);
 s.bot.brain.currentEnemy = s.w.players.findPlayerWithName("Bob")->getPlayerIndex();
 AITactic tactic;
 tactic.kindIndex=101; tactic.targetless=true; tactic.started=true;
 AITacticTeam team; team.index=0; team.handedOver=true; team.members={unit->getID()}; tactic.teams.push_back(team);
 auto &random = s.w.logic->random();
 random.seedRandom(1234); random.enableCallLog(true); random.clearCallLog();
 GameLogicRandom oracle = random;
 (void)oracle.getValue(0,0,"AIFarmKillSquad.cpp",304);
 (void)oracle.getValue(0,0,"AIFarmKillSquad.cpp",250);
 const int choice = oracle.getValue(0,2,"AIFarmKillSquad.cpp",135);
 AITacticalAI::updateTacticForTest(*s.w.logic,s.bot,tactic);
 CHECK(tactic.squadTarget == (choice == 0 ? farm->getID() : factory->getID()));
 std::vector<int> lines;
 for (const auto &call : random.callLog()) { if(call.file == "AIFarmKillSquad.cpp") lines.push_back(call.line); }
 CHECK(lines == std::vector<int>{304,250,135});
 random.clearCallLog();
 oracle = random;
 const int delay = oracle.getValue(150,600,"AIFarmKillSquad.cpp",337);
 tactic.teams.clear();
 AITacticalAI::updateTacticForTest(*s.w.logic,s.bot,tactic);
 CHECK(tactic.ended);
 CHECK(s.bot.brain.namedCounters["AIFarmKillSquad_IsRunning"] == 0);
 CHECK(s.bot.brain.namedCounters["AIFarmKillSquad_FrameNextRun"] == (int)s.w.logic->getFrame()+delay);
 REQUIRE(random.callLog().size() == 1);
 CHECK(random.callLog()[0].line == 337);
 CHECK(random.callLog()[0].lo == 150);
 CHECK(random.callLog()[0].hi == 600);
}

TEST_CASE("BFME2 team handover: raw template totals, support exclusion and danger maximum")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData ThreatData\nEnd\nChildObject Raw120 Swordsman\n ThreatLevel = 120\nEnd\nChildObject Hero60 Swordsman\n KindOf = HERO SELECTABLE\n ThreatLevel = 60\nEnd\nChildObject Support800 Swordsman\n KindOf = SUPPORT SELECTABLE\n ThreatLevel = 800\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI(); manager.setStore(&s.store); manager.newGame(true);
 const int index = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
 auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(index)); REQUIRE(ai); ai->disabled=true;
 auto *first = s.w.unit("Raw120",'A',100,100);
 auto *second = s.w.unit("Raw120",'A',110,100);
 auto *hero = s.w.unit("Hero60",'A',120,100);
 auto *support = s.w.unit("Support800",'A',130,100);
 AITarget target; target.threat=230; ai->brain.targets.push_back(target);
 AITactic tactic; tactic.targetOwner=index; tactic.targetIndex=0;
 AITacticTeam team; team.members={support->getID()};
 CHECK_FALSE(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team));
 team.members={first->getID(),second->getID(),support->getID()};
 CHECK(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team)); // 240 >= 230, support adds zero
 AIAttackedAt zone; zone.nativeThreat=300; ai->brain.attackedAt.push_back(zone);
 CHECK_FALSE(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team));
 team.members.push_back(hero->getID());
 CHECK(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team)); // exactly 300
 ai->brain.targets[0].threat=301;
 CHECK_FALSE(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team));
 tactic.targetless=true;
 CHECK(AITacticalAI::teamMeetsThreatForTest(*s.w.logic,tactic,team)); // no target means threshold zero
}

TEST_CASE("BFME2 interest zones: attacking member position and raw enemy threat survive snapshots")
{
 Scenario s;
 REQUIRE(s.w.w.load("SkirmishAIData ThreatData\nEnd\nChildObject Enemy200 Swordsman\n ThreatLevel = 200\nEnd\nChildObject Support900 Swordsman\n KindOf = SUPPORT SELECTABLE\n ThreatLevel = 900\nEnd\n").empty());
 auto &manager = s.w.logic->skirmishAI(); manager.setStore(&s.store); manager.newGame(true);
 const int index=s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
 auto *ai=const_cast<AISkirmishPlayer *>(manager.findAI(index)); REQUIRE(ai); ai->disabled=true;
 auto *member=s.w.unit("Swordsman",'A',100,500);
 auto *enemy=s.w.unit("Enemy200",'B',300,500);
 s.w.unit("Support900",'B',310,500);
 member->getAIUpdateInterface()->setCurrentVictim(enemy);
 AITactic tactic; tactic.kind="SimpleAttack"; tactic.kindIndex=0;
 AITacticTeam team; team.index=0; team.members={member->getID()}; tactic.teams.push_back(team);
 ai->brain.tactics.push_back(tactic);
 s.w.logic->partition().update();
 AITacticalAI::updateInterestZonesForTest(*s.w.logic,*ai);
 REQUIRE(ai->brain.attackedAt.size()==1);
 CHECK(ai->brain.attackedAt[0].position.x==100);
 CHECK(ai->brain.attackedAt[0].position.y==500);
 CHECK(ai->brain.attackedAt[0].nativeThreat==200);
 ai->brain.tactics.clear(); // Keep this snapshot fixture independent of recruitment state.
 XferSave save; save.beginBlock(); manager.xfer(save); save.endBlock();
 StateHasher before; manager.crc(before);
 ai->brain.attackedAt[0].nativeThreat=999;
 StateHasher changed; manager.crc(changed);
 CHECK_FALSE(before.value()==changed.value());
 XferLoad load(save.data()); load.beginBlock(); manager.xfer(load); load.endBlock();
 ai=const_cast<AISkirmishPlayer *>(manager.findAI(index)); REQUIRE(ai);
 CHECK(ai->brain.attackedAt[0].nativeThreat==200);
 StateHasher restored; manager.crc(restored);
 CHECK(restored.value()==before.value());
}

TEST_CASE("BFME2 generator: native prototype names and registration order")
{
 // Donor AITacticsGenerator.cpp RVA 0x505E5D / 0x506020, not the RotWK ten/eleven lists.
 CHECK(AITacticalAI::offensivePrototypesForTest() == std::vector<std::string>{"SimpleAttack","FlankAttack","SimpleSiege","SiegeGates","AIBasePenetrationTroopsTactic"});
 CHECK(AITacticalAI::targetlessPrototypesForTest() == std::vector<std::string>{"FarmKillSquad","AIRingHeroTactic","StructureCreep","ReturnTheRing","AIStartWoTRBattleTactic","AIRoamingDefenseTactic"});
}

namespace {
struct SiegeScenario : Scenario {
 AISkirmishPlayer *ai = nullptr;
 Object *unit = nullptr, *gate = nullptr, *objective = nullptr;
 SiegeScenario() {
  REQUIRE(w.w.load(R"ini(
SkirmishAIData SiegeData
End
ChildObject SiegeGate BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_GATE
 GeometryMinorRadius = 500
End
ChildObject SiegeSegment BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_SEGMENT
End
ChildObject WalkableSegment SiegeSegment
 KindOf = STRUCTURE IMMOBILE WALL_SEGMENT WALK_ON_TOP_OF_WALL
End
ChildObject NoAcquireGate BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_GATE NOT_AUTOACQUIRABLE
End
ChildObject SiegeFortress BreachWall
 KindOf = STRUCTURE IMMOBILE DOZER_FACTORY
End
ChildObject SiegeUnit Swordsman
 ThreatLevel = 1000
 ThreatBreakdown
 AIKindOf = SIEGEWEAPON
 End
End
)ini").empty());
  auto &manager=w.logic->skirmishAI(); manager.setStore(&store); manager.newGame(true);
  ai=const_cast<AISkirmishPlayer *>(manager.findAI(bot.playerIndex)); REQUIRE(ai); ai->disabled=true;
  ai->build.phase=1; ai->brain.currentEnemy=w.players.findPlayerWithName("Bob")->getPlayerIndex();
  unit=w.unit("Swordsman",'A',100,500);
  gate=w.unit("SiegeGate",'B',500,500);
  objective=w.unit("BreachWall",'B',800,500);
  w.buildMap();
  AITarget target; target.type=0; target.object=objective->getID(); target.position=*objective->getPosition();
  target.inactive=false; target.maxTeams=2; target.id=ai->brain.nextTargetId++;
  ai->brain.targets.push_back(target);
 }
 void breach() { w.logic->destroyObject(gate); gate=nullptr; w.buildMap(); }
};
}

TEST_CASE("BFME2 siege: closed wall selects gates, category seven selects SimpleSiege, breach refuses both")
{
 for (int difficulty=0;difficulty<4;++difficulty) {
  CAPTURE(difficulty);
  SiegeScenario s;
  s.w.players.findPlayerWithName("Alice")->setSkirmishDifficulty(difficulty);
  CHECK(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],false));
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],true));
  auto *siege=s.w.unit("SiegeUnit",'A',120,500);
  REQUIRE_FALSE(siege->isKindOfName("SIEGEENGINE")); // Native choice is ThreatBreakdown category 7.
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],false));
  CHECK(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],true));
  auto *fortress=s.w.unit("SiegeFortress",'B',800,600);
  s.ai->brain.targets[0].object=fortress->getID(); s.ai->brain.targets[0].position=*fortress->getPosition();
  s.ai->build.phase=0;
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],false));
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],true));
  s.ai->build.phase=1;
  s.breach();
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],false));
  CHECK_FALSE(AITacticalAI::siegeAppliesForTest(*s.w.logic,*s.ai,s.ai->brain.targets[0],true));
 }
}

TEST_CASE("BFME2 siege gates: native setup, nearest valid wall, breach and idle completion")
{
 SiegeScenario s;
 s.w.unit("WalkableSegment",'B',150,500);
 s.w.unit("NoAcquireGate",'B',160,500);
 auto *near=s.w.unit("SiegeSegment",'B',300,500);
 s.w.unit("SiegeSegment",'B',300,500); // Equal distance keeps the first registration.
 AITacticalAI::startSiegeForTest(*s.w.logic,*s.ai,0,false);
 auto &tactic=s.ai->brain.tactics.back();
 REQUIRE(tactic.teams.size()==1);
 CHECK(tactic.teams[0].minimum==1); CHECK(tactic.teams[0].maximum==-1);
 CHECK(tactic.teams[0].forbiddenKinds==std::vector<int>{ObjectTemplateInfoBuilder::kindOfIndex("ARCHER")});
 tactic.teams[0].members={s.unit->getID()}; tactic.teams[0].handedOver=true; tactic.teams[0].wantsMembers=false;
 tactic.started=true;
 AITacticalAI::runSiegeForTest(*s.w.logic,*s.ai,tactic);
 CHECK(tactic.squadTarget==near->getID());
 CHECK(s.unit->getAIUpdateInterface()->stateMachine().goalObjectID()==near->getID());
 // Remove every physical segment too: the target path must actually become reachable.
 std::vector<Object *> walls;
 for(Object *o=s.w.logic->getFirstObject();o;o=o->getNextObject()) if(o!=s.objective && o->getControllingPlayer()==s.objective->getControllingPlayer()) walls.push_back(o);
 for(Object *o:walls) s.w.logic->destroyObject(o);
 s.gate=nullptr; s.w.buildMap();
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic);
 CHECK(tactic.mainLaunched); CHECK(s.unit->getAIUpdateInterface()->attackMoveActive());
 tactic.teams[0].idleFrames=1000;
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic);
 CHECK(tactic.ended); CHECK(tactic.success);
}

TEST_CASE("BFME2 SimpleSiege: predefined templates recruit before handover and save unfinished requests")
{
 SiegeScenario s;
 auto *first=s.w.unit("SiegeUnit",'A',110,500);
 auto *second=s.w.unit("SiegeUnit",'A',120,500);
 AITacticalAI::startSiegeForTest(*s.w.logic,*s.ai,0,true);
 auto &tactic=s.ai->brain.tactics.back();
 REQUIRE(tactic.teams.size()==2);
 CHECK(tactic.teams[0].remainingUnits==std::vector<std::string>{"SiegeUnit","SiegeUnit"});
 CHECK(tactic.teams[0].minimum==1); CHECK(tactic.teams[0].maximum==-1);
 CHECK(tactic.teams[1].minimum==1); CHECK(tactic.teams[1].maximum==-1);
 auto &manager=s.w.logic->skirmishAI();
 XferSave saved; saved.beginBlock(); manager.xfer(saved); saved.endBlock();
 tactic.teams[0].remainingUnits.clear();
 XferLoad load(saved.data()); load.beginBlock(); manager.xfer(load); load.endBlock();
 s.ai=const_cast<AISkirmishPlayer *>(manager.findAI(s.bot.playerIndex)); REQUIRE(s.ai);
 auto &restored=s.ai->brain.tactics.back();
 CHECK(restored.teams[0].remainingUnits==std::vector<std::string>{"SiegeUnit","SiegeUnit"});
 restored.teams[0].remainingUnits={"MissingSiegeTemplate"};
 XferSave corrupt; corrupt.beginBlock(); manager.xfer(corrupt); corrupt.endBlock();
 restored.teams[0].remainingUnits={"SiegeUnit","SiegeUnit"};
 StateHasher beforeBad; manager.crc(beforeBad);
 XferLoad bad(corrupt.data()); bad.beginBlock();
 CHECK_THROWS_WITH(manager.xfer(bad),"skirmish ai: unknown predefined unit template 'MissingSiegeTemplate'");
 StateHasher afterBad; manager.crc(afterBad);
 CHECK(afterBad.value()==beforeBad.value());

 s.w.frames(50);
 AITacticalAI::updateTeamBuilder(*s.w.logic,*s.ai);
 CHECK(restored.teams[0].remainingUnits.empty());
 CHECK(restored.teams[0].members==std::vector<ObjectID>{first->getID(),second->getID()});
 CHECK(restored.teams[0].priority==20);
 CHECK(restored.teams[0].handedOver);
 CHECK(restored.teams[1].members==std::vector<ObjectID>{s.unit->getID()});
}

TEST_CASE("BFME2 SimpleSiege: stand back 500, wait for breach, advance assault team, finish at idle")
{
 SiegeScenario s;
 auto *siege=s.w.unit("SiegeUnit",'A',100,500);
 AITacticalAI::startSiegeForTest(*s.w.logic,*s.ai,0,true);
 auto &tactic=s.ai->brain.tactics.back();
 tactic.teams[0].remainingUnits.clear(); tactic.teams[0].members={siege->getID()};
 tactic.teams[1].members={s.unit->getID()}; tactic.started=true;
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic);
 CHECK(tactic.step==1); CHECK(tactic.squadTarget==s.gate->getID());
 CHECK(siege->getAIUpdateInterface()->stateMachine().goalObjectID()==s.gate->getID());
 REQUIRE(s.unit->getAIUpdateInterface()->attackMoveActive());
 REQUIRE(s.unit->getAIUpdateInterface()->resumeAttackMove());
 // Source destination before the existing AIGroup placement/clipping facade.
 const Coord3D standBack=AITacticalAI::siegeStandBackPointForTest(*siege->getPosition(),*s.gate->getPosition());
 CHECK(standBack.x==0); CHECK(standBack.y==500); CHECK(standBack.z==0);
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic); CHECK(tactic.step==1);
 s.breach();
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic); CHECK(tactic.step==2);
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic); CHECK(tactic.step==3);
 REQUIRE(s.unit->getAIUpdateInterface()->resumeAttackMove());
 CHECK(s.unit->getAIUpdateInterface()->attackMoveActive());
 tactic.teams[1].idleFrames=1000;
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic);
 CHECK(tactic.ended); CHECK(tactic.success);
}

TEST_CASE("BFME2 SimpleSiege: save between breach and assault resumes the next stage")
{
 SiegeScenario s;
 auto *siege=s.w.unit("SiegeUnit",'A',100,500);
 AITacticalAI::startSiegeForTest(*s.w.logic,*s.ai,0,true);
 auto &tactic=s.ai->brain.tactics.back();
 tactic.started=true; tactic.step=1;
 tactic.teams[0].remainingUnits.clear(); tactic.teams[0].members={siege->getID()};
 tactic.teams[1].members={s.unit->getID()};
 s.breach();
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,tactic); REQUIRE(tactic.step==2);
 auto &manager=s.w.logic->skirmishAI();
 XferSave saved; saved.beginBlock(); manager.xfer(saved); saved.endBlock();
 tactic.step=3;
 XferLoad load(saved.data()); load.beginBlock(); manager.xfer(load); load.endBlock();
 s.ai=const_cast<AISkirmishPlayer *>(manager.findAI(s.bot.playerIndex)); REQUIRE(s.ai);
 auto &restored=s.ai->brain.tactics.back();
 CHECK(restored.step==2);
 AITacticalAI::updateSiegeForTest(*s.w.logic,*s.ai,restored);
 CHECK(restored.step==3); CHECK(s.unit->getAIUpdateInterface()->attackMoveActive());
}
