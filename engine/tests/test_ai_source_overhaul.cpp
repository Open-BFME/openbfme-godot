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
  AITacticalAI::updateRoamingDefenseForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.ready);
  CHECK_FALSE(tactic.started);
  REQUIRE(tactic.teams.empty());
  s.w.frames(1);
  AITacticalAI::updateRoamingDefenseForTest(*s.w.logic, *ai, tactic);
  CHECK(tactic.started);
  CHECK(tactic.mainLaunched);
  CHECK_FALSE(tactic.ended);
  AITacticalAI::updateRoamingDefenseForTest(*s.w.logic, *ai, tactic);
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
  AITacticalAI::updateRoamingDefenseForTest(*s.w.logic, *ai, tactic);
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
