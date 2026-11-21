// PORT-196 (fork issue fork issue 463): the AI spell book's ARMY_BREAKER decision (RW 0x9E92CA, stop S-1421), its taken-player timer RW 0x9E9222 and the
// decision class's constructor RW 0x9E91EA.
//
// TARGET FACTS (RotWK game.dat 2.01, every address read with tools/mod/rw.py dis):
//   * the class chain: the factory RW 0x992724 is `cmp eax, 0x34; jmp dword ptr [eax*4 + 0x992EFE]`; the entry of AI_SPELLBOOK_ARMY_BREAKER (index 24 of
//     TheAISpecialPowerTypeNames RW 0xDB84F8) constructs a 0x2C-byte object with RW 0x9E91EA, which calls the location base RW 0xA01C6B and stores the
//     vtable RW 0xC8E178. Its slot 0x18 is the location base RW 0xA01C54 (kind KIND_LOCATION) and its slot 0x1C — the decision the framework RW 0x993055
//     runs — is RW 0x9E92CA.
//   * RW 0x9E92CA, step by step: the caster's controlling player (RW 0x68B678: Object + 0x31C -> player); TheSkirmishAIManager (RW 0xDE4938) -> RW
//     0x6A950B, which answers 0 for a player without a skirmish AI; the AI's brain (`mov esi, [eax + 0x164]`) and its attacked-at list (+ 0x20 / + 0x24): no
//     brain or an empty list answers no (`test esi, esi; je` at RW 0x9E92EF, RW 0x9E92F7 .. 0x9E9311). Each record in list order
//     (`mov eax, [eax + edi*4]`, + 4 per step) proposes its position + 0x04 .. + 0x0C (RW 0x9E9316 .. 0x9E932F); the record's ENEMIES' threat total
//     (+ 0x1C) must not be below the decision's range (+ 0x14, `movss xmm0, [eax + 0x1C]; comiss xmm0, [ebx + 0x14]; jb` at RW 0x9E9334 / 0x9E9339);
//     0.5 (RW 0xC8E174, `mulss` at RW 0x9E933F) times that total must not be below the record's OTHER objects' total (+ 0x64, `comiss` at RW 0x9E9347,
//     `jb` at RW 0x9E934B) — the spot is broken when the enemy force there is worth at least twice the friendly one; then the location must pass RW
//     0xA0229F (RW 0x9E9356) and the caster's player must be free per RW 0x9E9222 (RW 0x9E9369); the first record that passes all four answers yes
//     (RW 0x9E938B), otherwise no.
//   * the two fields are the record's ThreatRecords: RW 0x6C6F01 constructs them at + 0x1C (`lea ebx, [esi + 0x1c]`, ctor RW 0x7ED8A3) and + 0x64, and
//     RW 0x6C7010..0x6C705A fills the enemy branch into `[ebx]` (+ 0x1C) with the categories at + 0x20 = + 0x1C + 4 + 4 * category and the other branch
//     into + 0x64 (categories at + 0x68). The record's radius (+ 0x14, 300 per RW 0xC16764) and frame (+ 0x18) are not read by this decision.
//   * RW 0x9E9222: the list RW 0xDEC168 / 0xDEC16C (8-byte entries; the vector's own constructor RW 0xBCA446) is searched for the CASTER'S PLAYER
//     (`cmp edi, [edx + 0x54]` at RW 0x9E9244 — Player + 0x54 is the player index, as RW 0x836036 reads it); a miss appends { index, game frame } through
//     RW 0x97D718 (RW 0x9E9256 .. 0x9E9285). Then `mov eax, [esi + 4]; cmp eax, [ecx + 0x40]; ja` (RW 0x9E9289 .. 0x9E9295): an entry still in the
//     future answers no and draws nothing; otherwise the entry takes the game frame plus GetGameLogicRandomValue(RW 0xDEC160, RW 0xDEC164)
//     "AISpellBookArmyBreaker.cpp" line 0x53 (RW 0x9E9297 .. 0x9E92BD, RW 0x6D328E) and the answer is yes.
//   * RW 0xDEC160 / 0xDEC164 are written by the initialisers RW 0xBCA418 / 0xBCA42F as ftol2([0xD9F608] * 15.0) and ftol2([0xD9F608] * 30.0) with
//     [0xD9F608] = 5 (LOGICFRAMES_PER_SECOND) and 15.0 / 30.0 at RW 0xBDC6CC / RW 0xBDAE54: 75 and 150 frames. Both functions sit in the static
//     initialiser table at RW 0xD8ABA8 / 0xD8ABAC (the list's constructor at RW 0xD8ABB0), so the bounds hold before the first game.
//   * RW 0x9E91EA (the constructor): the location base's constructor RW 0xA01C6B, the vtable RW 0xC8E178, then `mov eax, [0xDE412C]; cmp [eax + 0x40], 1;
//     ja 0x9E921E` — from frame 2 on the taken list is emptied (RW 0x9E9203 / RW 0x8643F8); at frames 0 and 1 it is left alone.
//
// The tests are synthetic (no retail or mod file): a logic world on a pathfinder terrain, a command button and set parsed from INI text into the engine's
// own command store, and the skirmish AI's brain (AIBrainState::attackedAt, the records of RW 0x6C6F01) filled by the test. Every expectation is RW's rule
// with its address; none is read back from the port's own output.

#include "doctest.h"

#include "MoveTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AISpecialPowerInternal.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <memory>
#include <string>
#include <vector>

using namespace AISpecialPowerDetail;

namespace
{
// RW 0xDEC160 / RW 0xBCA418: LOGICFRAMES_PER_SECOND (5, RW 0xD9F608) * 15.0 (RW 0xBDC6CC)
constexpr int kDelayLo = 75;
// RW 0xDEC164 / RW 0xBCA42F: LOGICFRAMES_PER_SECOND * 30.0 (RW 0xBDAE54)
constexpr int kDelayHi = 150;
// RW 0xC8E174: the factor RW 0x9E933F applies to the record's enemy total before RW 0x9E9347 compares it with the other total
constexpr float kBreakerFactor = 0.5f;

// the AI the manager needs (it disables an AI without an army: RW 0x6A9F96), Bob's side Beta
const char kAIData[] = "SkirmishAIData TheSkirmishAIData\n DisableWallBuilding = No\nEnd\n"
					   "ArmyDefinition BetaArmy\n Side = Beta\n ArmyMemberDefinition M\n  Unit = Port196Caster\n  PercentageOfArmyPhase1 = 100\n End\nEnd\n";

// the caster: an object with the ARMY_BREAKER update and a SpecialPowerModule the ActionManager's location test finds (RW 0x68C26D). The decision's range
// is the module's SpecialPowerRange (RW 0x8B7364: `lea eax, [edi + 0x14]`, the module data's + 0x14 at RW 0x8B7392 .. 0x8B73A2); the tests read it
// from the decision instead of pinning a number. The radius cursor (RW 0x82D56A shrinks the pathfinder extent by it) is 10.
const char kPort196Objects[] =
	"Object Port196Caster\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = Port196ArmyBreakerPower\n"
	"  End\n"
	"  Behavior = AISpecialPowerUpdate ModuleTag_Breaker\n"
	"    CommandButtonName = Port196ArmyBreaker\n"
	"    SpecialPowerAIType = AI_SPELLBOOK_ARMY_BREAKER\n"
	"    SpecialPowerRange = 100\n"
	"  End\n"
	"End\n"
	"Object Port196Caster2\n" // the same object under its own name (a template is made once), for the second caster of the tests
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = AISpecialPowerUpdate ModuleTag_Breaker\n"
	"    CommandButtonName = Port196ArmyBreaker\n"
	"    SpecialPowerAIType = AI_SPELLBOOK_ARMY_BREAKER\n"
	"    SpecialPowerRange = 100\n"
	"  End\n"
	"End\n";

const char kPort196Commands[] =
	"SpecialPower Port196ArmyBreakerPower\n"
	"  Enum = SPECIAL_SPELL_BOOK_DWARVEN_RICHES\n"
	"  ReloadTime = 0\n"
	"  RadiusCursorRadius = 10\n"
	"End\n"
	"CommandButton Port196ArmyBreaker\n"
	"  Command = SPELL_BOOK\n"
	"  SpecialPower = Port196ArmyBreakerPower\n"
	"  TextLabel = PORT196:ArmyBreaker\n"
	"  Options = NEED_TARGET_POS\n"
	"End\n"
	"CommandSet Port196Set\n"
	"  1 = Port196ArmyBreaker\n"
	"End\n";

// the game's own command store, kept alive for the fixture (the command part of retail's TheControlBar)
struct CommandStoreScope
{
	CommandStore store;
	CommandStore *previous = TheCommandStore;
	CommandStoreScope() { TheCommandStore = &store; }
	~CommandStoreScope() { TheCommandStore = previous; }
	CommandStoreScope(const CommandStoreScope &) = delete;
	CommandStoreScope &operator=(const CommandStoreScope &) = delete;
};

// the movement lane's world (a logic world with TheAI and its pathfinder on a flat 1000 x 1000 synthetic terrain, which the ActionManager's location
// test RW 0x82D56A reads), plus the command store, the skirmish AI and the ARMY_BREAKER caster
struct Fixture : movetest::MoveWorld
{
	CommandStoreScope commands;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;
	Object *caster = nullptr;
	AISkirmishPlayer *ai = nullptr;

	Fixture()
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		std::string err = w.load(kPort196Commands, INI_LOAD_OVERWRITE, "port196commands.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kPort196Objects, INI_LOAD_OVERWRITE, "port196objects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		buildMap(); // the terrain the location test reads
		caster = make("Port196Caster", teamOf("Bob"));
		REQUIRE(caster != nullptr);
		caster->setCommandSetOverride("Port196Set"); // the set the initialisation RW 0x8B714F searches for the button by name
		store.registerBlocks(env.blocks);
		{
			INI ini(env);
			const std::string text = kAIData;
			ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		}
		logic->skirmishAI().setStore(&store);
		REQUIRE(logic->skirmishAI().newGame(true) == 1); // one AI: Bob, the computer player
		ai = logic->skirmishAI().findAI(players.findPlayerWithName("Bob")->getPlayerIndex());
		REQUIRE(ai != nullptr);
	}
	~Fixture()
	{
		logic->skirmishAI().reset();
		TheSpecialPowerStore = savedPowers;
	}
	AISpecialPowerUpdate *breaker()
	{
		for (const std::unique_ptr<BehaviorModule> &m : caster->modules())
		{
			auto *u = dynamic_cast<AISpecialPowerUpdate *>(m.get());
			if (u && u->data()->m_commandButtonName == "Port196ArmyBreaker")
			{
				return u;
			}
		}
		return nullptr;
	}
	// the initialisation RW 0x8B714F as if the command set held the button (a retail spell book's set does; the synthetic set is the caster's)
	AISpecialPowerUpdate *initialisedBreaker()
	{
		AISpecialPowerUpdate *u = breaker();
		REQUIRE(u != nullptr);
		u->initAsIfInCommandSetForTest();
		REQUIRE(u->decision() != nullptr);
		REQUIRE(u->active());
		return u;
	}
	// an attacked-at record as RW 0x6C6F01 makes it (position + 0x04, the 300 radius RW 0xC16764 + 0x14, the frame + 0x18) with the two threat totals the
	// fill of RW 0x6C7010 / 0x6C7035 writes: the enemies' (+ 0x1C) and the other objects' (+ 0x64)
	AIAttackedAt &record(float x, float y, float enemyThreat, float otherThreat, unsigned id = 1)
	{
		AIAttackedAt r;
		r.id = id;
		r.position = Coord3D{ x, y, 0.0f };
		r.radius = 300.0f;
		r.frame = logic->getFrame();
		r.enemies.v[0] = enemyThreat;
		r.others.v[0] = otherThreat;
		ai->brain.attackedAt.push_back(r);
		return ai->brain.attackedAt.back();
	}
	void advanceTo(unsigned at)
	{
		while (logic->getFrame() < at)
		{
			logic->runLogicFrame();
		}
	}
};

} // namespace

TEST_CASE("PORT-196 ARMY_BREAKER: the decision needs the brain's records, and the enemy total must reach the range (RW 0x9E92CA: 0x9E92EF / 0x9E92F7, 0x9E9334 / 0x9E9339)")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	CHECK(u->decision()->type == AISP_SPELLBOOK_ARMY_BREAKER);          // the factory RW 0x992724's table entry 24
	CHECK(u->decision()->kind == AISpecialPowerDecision::KIND_LOCATION); // the constructor RW 0x9E91EA calls RW 0xA01C6B (slot 0x18 RW 0xA01C54)
	f.advanceTo(10);                                                     // the pathfinder extent RW 0x82D56A reads exists after the map is built
	// no record: RW 0x9E92F7's empty test answers no
	CHECK_FALSE(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty());
	// a record whose enemy total is below the decision's range: RW 0x9E9339's `jb` sends the decision past it, and the list holds nothing else.
	// The range is the module's SpecialPowerRange (RW 0x8B7364: `lea eax, [edi + 0x14]` -> the decision's + 0x14); the test sets one through the INI.
	CHECK(u->decision()->range == 100.0f); // SpecialPowerRange in the INI above (RW 0x8B7392 .. 0x8B73A2 writes the decision's + 0x14)
	f.record(100.0f, 100.0f, u->decision()->range - 1.0f, 0.0f);
	CHECK_FALSE(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty()); // RW 0x9E9369 was not reached, so nothing was taken
	// the same record with the total at the range passes RW 0x9E9339 (comiss + jb: not below)
	f.ai->brain.attackedAt.clear();
	f.record(100.0f, 100.0f, u->decision()->range, 0.0f);
	CHECK(u->decideNow());
	REQUIRE(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	CHECK(f.logic->skirmishAI().armyBreakerTimers().front().first == f.players.findPlayerWithName("Bob")->getPlayerIndex()); // RW 0x9E9244: keyed by Player + 0x54
}

TEST_CASE("PORT-196 ARMY_BREAKER: 0.5 * the record's enemy total must cover its other objects' total (RW 0x9E933F / 0x9E9347)")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	f.advanceTo(10);
	const float range = u->decision()->range;
	// 0.5 * 400 = 200, and the friendly total is 201: RW 0x9E9347's comiss + jb (RW 0x9E934B) refuses the record
	f.record(200.0f, 200.0f, 400.0f, 201.0f);
	CHECK_FALSE(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty());
	(void)range;
	// 0.5 * 400 = 200 with the friendly total at 200: not below, so the record passes (one soldier must not refuse a broken spot)
	f.ai->brain.attackedAt.clear();
	f.record(200.0f, 200.0f, 400.0f, 200.0f);
	CHECK(u->decideNow());
	CHECK(kBreakerFactor * 400.0f == 200.0f);
}

TEST_CASE("PORT-196 ARMY_BREAKER: the first record that passes is the one taken, and the location is its position (RW 0x9E9313 .. 0x9E9369)")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	f.advanceTo(10);
	const float range = f.breaker()->decision()->range;
	f.record(120.0f, 140.0f, range, 1000.0f);                        // refused by RW 0x9E9347
	f.record(220.0f, 240.0f, range * 4.0f, 0.0f); // the one the decision takes
	// by index, not a reference: the next record() grows the vector and would leave a reference dangling (red on macOS libc++, CI 2026-11-18)
	const size_t second = f.ai->brain.attackedAt.size() - 1;
	f.record(320.0f, 340.0f, range * 4.0f, 0.0f);                    // never reached: the walk stops at the first record that passes
	// the pathfinder extent RW 0x82D56A shrunk by the power's RadiusCursorRadius: 10 .. 990 both ways
	CHECK(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	CHECK(u->decision()->location.x == 220.0f); // RW 0x9E9316 .. 0x9E932F copied the record's + 0x04 .. + 0x0C
	CHECK(u->decision()->location.y == 240.0f);
	CHECK(f.ai->brain.attackedAt[second].position.x == 220.0f);
	// the record's position is the point RW 0xA0229F was handed (its random walk is off: SpecialPowerAIType alone does not set RandomizeTargetLocation)
	CHECK(u->proposedLocationForTest().x == 220.0f);
	CHECK(u->proposedLocationForTest().y == 240.0f);
}

TEST_CASE("PORT-196 ARMY_BREAKER: a taken player answers no until its frame comes, then draws the delay RW 0x9E9222 takes (AISpellBookArmyBreaker.cpp line 0x53)")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	f.advanceTo(10);
	const float range = u->decision()->range;
	f.record(400.0f, 400.0f, range * 4.0f, 0.0f);
	f.logic->random().enableCallLog(true);
	REQUIRE(u->decideNow());
	std::vector<std::pair<int, unsigned>> &taken = f.logic->skirmishAI().armyBreakerTimers();
	REQUIRE(taken.size() == 1);
	// RW 0x9E9297 .. 0x9E92BD: the entry takes now + GetGameLogicRandomValue(75, 150) "AISpellBookArmyBreaker.cpp" line 0x53. The draw is read back from
	// the random log (the same call the decision made), so the expectation is an equality, not a range
	const GameLogicRandom::Call draw = f.logic->random().callLog().back();
	CHECK(draw.real == false);
	CHECK(draw.lo == kDelayLo);
	CHECK(draw.hi == kDelayHi);
	CHECK(draw.file == "AISpellBookArmyBreaker.cpp");
	CHECK(draw.line == 0x53);
	const unsigned now = f.logic->getFrame();
	CHECK(taken.front().second == now + (unsigned)draw.result);
	// the frame has not come: RW 0x9E9289 .. 0x9E9295 answers no BEFORE the draw, so no call is logged and the entry keeps its frame
	f.logic->random().clearCallLog();
	CHECK_FALSE(u->decideNow());
	CHECK(f.logic->random().callLog().empty());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	CHECK(taken.front().second == now + (unsigned)draw.result);
	CHECK(taken.size() == 1); // and the list keeps its one entry (one per player)
	// the frame has come: the decision answers yes again and takes a new delay. The brain's records live 20 frames (RW 0x6C7344: a record older than
	// LOGICFRAMES_PER_SECOND * 4 goes), so the fight the record describes has to be re-registered for the decision to see it again — exactly what the
	// brain does every frame while it lasts
	f.advanceTo(taken.front().second);
	f.logic->random().clearCallLog();
	f.ai->brain.attackedAt.clear();
	f.record(400.0f, 400.0f, range * 4.0f, 0.0f);
	CHECK(u->decideNow());
	REQUIRE(taken.size() == 1);
	const GameLogicRandom::Call again = f.logic->random().callLog().back();
	CHECK(again.lo == kDelayLo);
	CHECK(again.hi == kDelayHi);
	CHECK(taken.front().second == f.logic->getFrame() + (unsigned)again.result);
}

TEST_CASE("PORT-196 ARMY_BREAKER: the decision class's constructor empties the taken list only above frame 1 (RW 0x9E91EA: RW 0xDE412C + 0x40 > 1)")
{
	Fixture f;
	CHECK(f.logic->getFrame() == 0);
	// a list from an earlier game at frame 0: RW 0x9E91FD compares the game frame with 1 and leaves the list alone
	f.logic->skirmishAI().armyBreakerTimers().push_back({ f.players.findPlayerWithName("Bob")->getPlayerIndex(), 1234 });
	REQUIRE(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	f.initialisedBreaker(); // the decision object is made here: the constructor's `cmp [eax + 0x40], 1; ja` does not fire
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	// frame 2: the same emptiness test at RW 0x9E9203 empties it (RW 0x9E9219 -> RW 0x8643F8)
	f.advanceTo(2);
	f.logic->skirmishAI().armyBreakerTimers().push_back({ f.players.findPlayerWithName("Bob")->getPlayerIndex(), 1234 });
	Object *late = f.make("Port196Caster2", f.teamOf("Bob"));
	REQUIRE(late != nullptr);
	late->setCommandSetOverride("Port196Set");
	AISpecialPowerUpdate *second = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : late->modules())
	{
		auto *candidate = dynamic_cast<AISpecialPowerUpdate *>(m.get());
		if (candidate && candidate->data()->m_commandButtonName == "Port196ArmyBreaker")
		{
			second = candidate;
		}
	}
	REQUIRE(second != nullptr);
	second->initAsIfInCommandSetForTest();
	REQUIRE(second->decision() != nullptr);
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty());
}

TEST_CASE("PORT-196 ARMY_BREAKER: a human's caster and a caster of a player without records answer no (RW 0x9E92EF / 0x9E92F7)")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	f.advanceTo(10);
	const float range = u->decision()->range;
	f.record(200.0f, 200.0f, range * 4.0f, 0.0f);
	// Alice is the human player: RW 0x6A950B answers 0 for her, so the decision answers no and takes nothing
	Object *humanCaster = f.make("Port196Caster", f.teamOf("Alice"));
	REQUIRE(humanCaster != nullptr);
	humanCaster->setCommandSetOverride("Port196Set");
	AISpecialPowerUpdate *theirs = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : humanCaster->modules())
	{
		auto *candidate = dynamic_cast<AISpecialPowerUpdate *>(m.get());
		if (candidate && candidate->data()->m_commandButtonName == "Port196ArmyBreaker")
		{
			theirs = candidate;
		}
	}
	REQUIRE(theirs != nullptr);
	theirs->initAsIfInCommandSetForTest();
	REQUIRE(theirs->decision() != nullptr);
	CHECK_FALSE(theirs->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty());
	// the computer's own caster answers yes on the same record: the skirmish AI's presence is the difference (one reaction, one place)
	CHECK(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
}

// The list is state: it is in the save (version 10) and in the world hash. It is hashed only while it holds an entry, so a game whose AI never took
// ARMY_BREAKER keeps the hash this manager had before the lane — that is what leaves the perf1 / uber1 reference vectors where they were (a pin is
// re-measured only when the simulation is MEANT to change, so this list must not shift every game's frames)
TEST_CASE("PORT-196 ARMY_BREAKER: the taken-player list enters the world hash only once it holds an entry (empty list, the manager's hash is the same)")
{
	Fixture f;
	SkirmishAIManager &ai = f.logic->skirmishAI();
	REQUIRE(ai.armyBreakerTimers().empty());
	StateHasher before;
	ai.crc(before);
	// an empty vector hashed unconditionally would add a size word here: the two runs must agree word for word
	StateHasher again;
	ai.crc(again);
	CHECK(before.value() == again.value());
	// RW 0x9E9222's entry { the caster's player, the frame it is free again } is a change the hash must see
	ai.armyBreakerTimers().push_back({ 1, 900u });
	StateHasher filled;
	ai.crc(filled);
	CHECK(filled.value() != before.value());
	// and a different frame is a different state
	ai.armyBreakerTimers().front().second = 901u;
	StateHasher next;
	ai.crc(next);
	CHECK(next.value() != filled.value());
	ai.armyBreakerTimers().clear();
}

TEST_CASE("PORT-196 ARMY_BREAKER: neighbouring behaviour — HEAL stays unported (52 of 53), the timer list is this decision's alone, and RW 0x9E91EA does not touch the assist timers")
{
	Fixture f;
	AISpecialPowerUpdate *u = f.initialisedBreaker();
	f.advanceTo(10);
	// S-1421's count: ARMY_BREAKER is ported, HEAL (RW 0x9E85C6, the ZH AIPlayer list whose producer is S-7389) is the one unported decision
	int unported = 0;
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		if (!AISpecialPowerDecision::decisionPorted(t))
		{
			++unported;
			CHECK(std::string(TheAISpecialPowerTypeNames[t]).rfind("AI_SPELLBOOK_", 0) == 0);
		}
	}
	CHECK(unported == 1);
	CHECK(AISpecialPowerDecision::decisionPorted(AISP_SPELLBOOK_ARMY_BREAKER));
	CHECK_FALSE(AISpecialPowerDecision::decisionPorted(AISP_SPELLBOOK_HEAL));
	// a second caster object of the SAME player, its decision made while the list is still empty (a decision object made later would empty the list per
	// RW 0x9E91EA, as the constructor test shows)
	Object *other = f.make("Port196Caster2", f.teamOf("Bob"));
	REQUIRE(other != nullptr);
	other->setCommandSetOverride("Port196Set");
	AISpecialPowerUpdate *second = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : other->modules())
	{
		auto *candidate = dynamic_cast<AISpecialPowerUpdate *>(m.get());
		if (candidate && candidate->data()->m_commandButtonName == "Port196ArmyBreaker")
		{
			second = candidate;
		}
	}
	REQUIRE(second != nullptr);
	second->initAsIfInCommandSetForTest();
	REQUIRE(second->decision() != nullptr);
	CHECK(f.logic->skirmishAI().armyBreakerTimers().empty());
	// the ARMY_BREAKER timer list and the ASSIST_BATTLE one (RW 0xDEC154) are separate: this decision touches only its own
	f.logic->skirmishAI().spellBookAssistTimers().push_back({ 7, 4242 });
	const float range = u->decision()->range;
	f.record(200.0f, 200.0f, range * 4.0f, 0.0f);
	CHECK(u->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
	CHECK(f.logic->skirmishAI().spellBookAssistTimers().size() == 1);
	CHECK(f.logic->skirmishAI().spellBookAssistTimers().front().second == 4242);
	CHECK(f.logic->skirmishAI().armyBreakerTimers().front().first == f.players.findPlayerWithName("Bob")->getPlayerIndex());
	// the other caster of the same player asks at the same frame: RW 0x9E9244's key finds the player's entry still in the future, so it answers no and
	// the list keeps its one entry — the timer belongs to the player, not to the decision object
	CHECK_FALSE(second->decideNow());
	CHECK(f.logic->skirmishAI().armyBreakerTimers().size() == 1);
}
