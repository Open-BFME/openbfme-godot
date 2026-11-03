// OpenBFME unit tests (lane MODCOMPAT-AI): the script conditions and actions the retail mods' AI scripts evaluate (Edain, Edain Unchained, Arnor Gondor Rohan,
// Ages of Discord: logs/mod-gap-census.md), on a synthetic world (no retail files). The rules are RotWK's (caveat S-001, static disassembly): the condition
// switch RW 0x7EB7CD and the action switch RW 0x7CAFA5 (see ScriptConditions.cpp, ScriptActionsUnits.cpp, ScriptActionsOrders.cpp).  GPL-3.0.

#include "doctest.h"

#include "ProdTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Upgrade.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"

#include <vector>

using namespace prodtest;
using namespace scripttest;

namespace
{
const char kObjects[] =
	"Object UKeep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CASTLE_KEEP\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object UPlot\n"
	"  KindOf = BASE_FOUNDATION IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = FoundationAIUpdate ModuleTag_Foundation\n  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_Member\n  End\n"
	"End\n"
	"Object UFlag\n"
	"  KindOf = IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Alpha Base_M 300\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE +BASE_FOUNDATION\n"
	"  End\n"
	"End\n"
	"Object UTower\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  BuildCost = 400\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n"
	// an upgrade hall: a ProductionUpdate, affected by Upgrade_Barracks2 (an OBJECT upgrade) through its CommandSetUpgrade, the upgrade buttons in its set
	"Object UHall\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = HallSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 3\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Level2\n"
	"    TriggeredBy = Upgrade_Barracks2\n"
	"    CommandSet = HallSet\n"
	"  End\n"
	"End\n";
const char kCommands[] = "CommandButton Command_HallBasic\n  Command = PLAYER_UPGRADE\n  Upgrade = Upgrade_Basic\nEnd\n"
						 "CommandButton Command_HallLevel2\n  Command = OBJECT_UPGRADE\n  Upgrade = Upgrade_Barracks2\nEnd\n"
						 "CommandSet HallSet\n  1 = Command_HallBasic\n  2 = Command_HallLevel2\nEnd\n";

struct ModFx : ProdWorld
{
	std::vector<ScriptList> lists = std::vector<ScriptList>(3);
	Object *flag = nullptr;
	ModFx()
	{
		load(kBarracksObjects);
		load(kBarracksCommands);
		load(kObjects);
		load(kCommands);
		logic->settings().buildRulesLoaded = true;
		logic->settings().bodyThresholdsLoaded = true; // GameData's UnitDamagedThreshold / UnitReallyDamagedThreshold (the bodies use the defaults)
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		CastleTemplate layout;
		layout.name = "Base_M";
		layout.version = 5;
		layout.entries.push_back({ "", "UKeep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		layout.entries.push_back({ "", "UPlot", 80.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		layout.entries.push_back({ "", "UPlot", -80.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		logic->castleTemplates().add(layout);
		flag = make("UFlag", teamOf("Alice"));
		REQUIRE(flag != nullptr);
		const Coord3D at{ 400.0f, 400.0f, 0.0f };
		flag->setPosition(&at);
	}
	Player *bob() { return players.findPlayerWithName("Bob"); }
	void script(int side, const char *name, std::vector<ScriptActionRec> actions, bool oneShot = true,
		ScriptCondition when = cond("CONDITION_TRUE", {}))
	{
		lists[(size_t)side].items.push_back(item(scripttest::script(name, { { when } }, std::move(actions), {}, oneShot)));
	}
	void start()
	{
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		engine().newGame(sides, nullptr, nullptr, false);
		flag->setName("Center1"); // after newGame, which clears the named cache
	}
	ScriptEngine &engine() { return logic->scriptEngine(); }
	CastleBehavior *cb() { return dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior")); }
	bool eval(const char *name, std::vector<ScriptParameter> params) { return ScriptConditions::evaluate(engine(), cond(name, std::move(params))); }
	void run(const char *name, std::vector<ScriptParameter> params) { ScriptActions::execute(engine(), act(name, std::move(params))); }
	void setMoney(Player *p, std::uint32_t amount)
	{
		p->getMoney()->withdraw(p->getMoney()->countMoney(), false);
		p->getMoney()->deposit(amount, false);
	}
	unsigned long long noteCount(const std::string &prefix) const
	{
		unsigned long long n = 0;
		for (const auto &kv : logic->scriptEngine().stats().notes)
		{
			n += kv.first.rfind(prefix, 0) == 0 ? kv.second : 0;
		}
		return n;
	}
	const UpgradeTemplate *upgrade(const char *name) { return Player::resolveUpgrade(name, true); }
};

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}
} // namespace

TEST_CASE("modcompat-ai PLAYER_HAS_NUM_UNITS_WITH_UPGRADE (RW 0x7E9356): the one player of the mask, its team members with the upgrade that are no "
		  "HORDE_MEMBER, count >= n; a mask of several players or an unknown upgrade: false")
{
	ModFx f;
	f.start();
	const UpgradeTemplate *up = f.upgrade("Upgrade_Barracks2");
	std::vector<Object *> mine;
	for (int i = 0; i < 4; ++i)
	{
		mine.push_back(f.make("Soldier", f.teamOf("Alice")));
	}
	mine[0]->giveUpgradeSelf(up);
	mine[1]->giveUpgradeSelf(up);
	mine[2]->giveUpgradeSelf(up);
	mine[2]->setStatus((unsigned)statusBit("HORDE_MEMBER"), true); // RW 0x7E93E1: a horde member is not counted
	Object *theirs = f.make("Soldier", f.teamOf("Bob"));
	theirs->giveUpgradeSelf(up);
	auto has = [&](const char *player, int n, const char *upgrade) {
		return f.eval("PLAYER_HAS_NUM_UNITS_WITH_UPGRADE", { sp(11, player), ip(0, n), sp(33, upgrade) });
	};
	CHECK(has("Alice", 2, "Upgrade_Barracks2"));
	CHECK_FALSE(has("Alice", 3, "Upgrade_Barracks2"));
	CHECK(has("Alice", 0, "Upgrade_Basic")); // nobody has it: a count of 0 against 0
	CHECK_FALSE(has("Alice", 1, "Upgrade_Basic"));
	CHECK(has("Bob", 1, "Upgrade_Barracks2"));
	CHECK_FALSE(has("Bob", 2, "Upgrade_Barracks2"));
	CHECK_FALSE(has("Alice", 0, "Upgrade_NoSuchThing")); // RW 0x7E9392: no upgrade
	CHECK_FALSE(has("<All Players>", 0, "Upgrade_Barracks2")); // RW 0x6A85B6: a mask of several players has no player
	// no alive test (RW 0x7E93D8 .. 0x7E93F9): a dead member still counts
	mine[3]->giveUpgradeSelf(up);
	CHECK(has("Alice", 3, "Upgrade_Barracks2"));
	mine[3]->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK(has("Alice", 3, "Upgrade_Barracks2"));
	CHECK(f.engine().stats().unportedConditions.count("PLAYER_HAS_NUM_UNITS_WITH_UPGRADE") == 0);
}

TEST_CASE("modcompat-ai PLAYER_ALL_DESTROYED (RW 0x7E4866): false before ftol(5 * SecondsBeforeBaseCheckActive); then true when no player of the mask has a "
		  "team member that counts (RW 0x7A0956: alive, not PROJECTILE / INERT / IGNORE_FOR_VICTORY)")
{
	ModFx f;
	f.start();
	REQUIRE(f.logic->settings().secondsBeforeBaseCheckActive == 5.0f);
	auto gone = [&](const char *player) { return f.eval("PLAYER_ALL_DESTROYED", { sp(11, player) }); };
	f.frames(24);
	CHECK_FALSE(gone("Bob")); // frame 24, the delay is 25
	f.frames(1);
	CHECK(gone("Bob"));       // Bob has nothing
	CHECK_FALSE(gone("Alice")); // the castle flag
	CHECK(gone("Nobody"));    // an empty mask: true (RW 0x7E48BB)
	CHECK_FALSE(gone("<All Players>"));
	Object *s = f.make("UTower", f.teamOf("Bob")); // a body: it can die
	CHECK_FALSE(gone("Bob"));
	s->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK(gone("Bob")); // a dead member does not count
	CHECK(f.engine().stats().unportedConditions.count("PLAYER_ALL_DESTROYED") == 0);
}

TEST_CASE("modcompat-ai PLAYER_BUILT_UPGRADE[_FROM_NAMED] (RW 0x7E4F6A -> RW 0x758DE5): ProductionUpdate records a completed upgrade for its player "
		  "(RW 0x8A207B -> RW 0x759A1C); the condition finds it once (the entry is removed), by producer with the unit parameter")
{
	ModFx f;
	Object *hall = f.make("UHall", f.teamOf("Alice"));
	REQUIRE(hall);
	f.start();
	hall->setName("Hall");
	Object *other = f.make("Soldier", f.teamOf("Alice"));
	other->setName("Other");
	const UpgradeTemplate *basic = f.upgrade("Upgrade_Basic");
	auto built = [&](const char *player, const char *upgrade) { return f.eval("PLAYER_BUILT_UPGRADE", { sp(11, player), sp(33, upgrade) }); };
	auto builtBy = [&](const char *player, const char *upgrade, const char *unit) {
		return f.eval("PLAYER_BUILT_UPGRADE_FROM_NAMED", { sp(11, player), sp(33, upgrade), sp(14, unit) });
	};
	CHECK_FALSE(built("Alice", "Upgrade_Basic"));
	REQUIRE(f.pu(hall)->queueUpgrade(basic));
	int frames = 0;
	while (!f.alice()->hasUpgradeComplete(basic) && frames < 10)
	{
		f.frames(1);
		++frames;
	}
	REQUIRE(f.alice()->hasUpgradeComplete(basic));
	const int alice = f.alice()->getPlayerIndex();
	REQUIRE(f.engine().completedUpgrades(alice).size() == 1);
	CHECK(f.engine().completedUpgrades(alice)[0].nameCrc == ScriptEngine::specialPowerNameCrc("Upgrade_Basic"));
	CHECK(f.engine().completedUpgrades(alice)[0].source == hall->getID());
	CHECK_FALSE(built("Bob", "Upgrade_Basic"));
	CHECK_FALSE(builtBy("Alice", "Upgrade_Basic", "Other")); // another producer
	CHECK_FALSE(builtBy("Alice", "Upgrade_Basic", "Nobody")); // RW 0x7E4F81: no unit, false
	CHECK(builtBy("Alice", "Upgrade_Basic", "Hall"));
	CHECK_FALSE(built("Alice", "Upgrade_Basic")); // RW 0x758E3E: the entry went with the first answer
	f.engine().notifyOfCompletedUpgrade(alice, "Upgrade_Basic", other->getID());
	f.engine().notifyOfCompletedUpgrade(alice, "Upgrade_Basic", other->getID());
	CHECK(built("<All Players>", "Upgrade_Basic")); // every player of the mask is asked (RW 0x6A85EE)
	CHECK(built("Alice", "Upgrade_Basic"));
	CHECK_FALSE(built("Alice", "Upgrade_Basic"));
	CHECK(f.engine().stats().unportedConditions.empty());
}

TEST_CASE("modcompat-ai CAN_BUILD_OBJECTTYPE_AT_BASE (RW 0x7E5BF4 -> RW 0x7996D3) and BUILD_BASE_BUILDING (RW 0x7BEE14): an unpacked castle of the one player "
		  "of the mask with a free plot; the build is the script player's, needs the money (nothing is paid) and names the structure")
{
	auto canBuild = [](ModFx &f, const char *player, const char *unit, const char *type) {
		return f.eval("CAN_BUILD_OBJECTTYPE_AT_BASE", { sp(11, player), sp(14, unit), sp(15, type) });
	};
	// the castle by the reference its unpack wrote (as Edain's scripts name it: the unpack hands the castle's own name on)
	const ScriptCondition free = cond("CAN_BUILD_OBJECTTYPE_AT_BASE", { sp(11, "Alice"), sp(14, "Alice/MyCastle"), sp(15, "UTower") });
	auto towers = [](ModFx &f) {
		int n = 0;
		for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate()->getName() == "UTower" ? 1 : 0;
		}
		return n;
	};
	SUBCASE("the castle's script player builds on a free plot and names the structure")
	{
		ModFx f;
		f.script(1, "Unpack", { act("NAMED_BASE_UNPACK_FREE", { sp(14, "Center1"), sp(54, "MyCastle") }) });
		f.script(1, "Build", { act("BUILD_BASE_BUILDING", { sp(15, "UTower"), sp(14, "MyCastle"), sp(54, "Tower1") }) }, true, free);
		f.start();
		CHECK_FALSE(canBuild(f, "Alice", "Center1", "UTower")); // packed: state 0 (RW 0x7996D9)
		const std::uint32_t money = f.alice()->getMoney()->countMoney();
		REQUIRE(money == 1500u); // the start money, above the tower's 400
		f.frames(3);
		REQUIRE(f.cb()->state() == CastleBehavior::STATE_UNPACKED);
		REQUIRE(f.cb()->foundations().size() == 2);
		REQUIRE(towers(f) == 1);
		Object *tower = f.engine().getUnitNamed("Tower1");
		REQUIRE(tower);
		CHECK(tower->getTemplate()->getName() == "UTower");
		CHECK(tower->getControllingPlayer() == f.alice());
		CHECK(f.alice()->getMoney()->countMoney() == money); // RW 0x79A5EC builds through the plot (RW 0x859198): nothing is withdrawn
		const auto ref = f.engine().references().find({ "Alice", "Tower1" });
		REQUIRE(ref != f.engine().references().end());
		CHECK(ref->second == tower->getID());
		// the second plot is still free
		CHECK(canBuild(f, "Alice", "Alice/MyCastle", "UTower"));
		CHECK_FALSE(canBuild(f, "Bob", "Alice/MyCastle", "UTower"));      // not the unit's controller
		CHECK_FALSE(canBuild(f, "Alice", "Alice/MyCastle", "NoSuchType")); // RW 0x7E5C0C: no template
		CHECK_FALSE(canBuild(f, "Alice", "Nobody", "UTower"));
		CHECK_FALSE(canBuild(f, "<All Players>", "Alice/MyCastle", "UTower")); // RW 0x6A85B6: a mask of several players
		CHECK(f.engine().stats().unportedActions.count("BUILD_BASE_BUILDING") == 0);
		CHECK(f.engine().stats().unportedConditions.count("CAN_BUILD_OBJECTTYPE_AT_BASE") == 0);
	}
	SUBCASE("another side's script, a player without the money or with structures forbidden builds nothing")
	{
		ModFx f;
		f.script(1, "Unpack", { act("NAMED_BASE_UNPACK_FREE", { sp(14, "Center1"), sp(54, "MyCastle") }) });
		f.script(2, "Build", { act("BUILD_BASE_BUILDING", { sp(15, "UTower"), sp(14, "Alice/MyCastle"), sp(54, "Theirs") }) }, false, free);
		f.script(1, "Build", { act("BUILD_BASE_BUILDING", { sp(15, "UTower"), sp(14, "MyCastle"), sp(54, "") }) }, false, free);
		f.setMoney(f.alice(), 399);
		f.start();
		f.frames(3);
		REQUIRE(f.cb()->state() == CastleBehavior::STATE_UNPACKED);
		CHECK(towers(f) == 0); // Bob's script: RW 0x7BEE5C, not the script's player; Alice's: RW 0x6AAA2E, the cost 400 against 399
		f.setMoney(f.alice(), 400);
		f.alice()->setCanBuildBase(false); // RW 0x7BEE42: Player + 0x359
		f.frames(1);
		CHECK(towers(f) == 0);
		f.alice()->setCanBuildBase(true);
		f.frames(1);
		CHECK(towers(f) == 1);
		CHECK(f.engine().references().size() == 1); // only the castle's: an empty reference names nothing (RW 0x7BEEB9)
	}
}

TEST_CASE("modcompat-ai SET_UNIT_REFERENCE (RW 0x7C2AAC(.., 0)) writes the side's reference map and the named cache; SET_UNIT_REFERENCE_TO_REFERENCE "
		  "(RW 0x7C2AAC(.., 1) -> RW 0x608504) copies a reference's object to another name of the current side")
{
	ModFx f;
	Object *u = f.make("Soldier", f.teamOf("Alice"));
	f.script(1, "Refs", { act("SET_UNIT_REFERENCE", { sp(54, "RefA"), sp(14, "Unit1") }), act("SET_UNIT_REFERENCE_TO_REFERENCE", { sp(54, "RefB"), sp(54, "RefA") }),
							 act("SET_UNIT_REFERENCE_TO_REFERENCE", { sp(54, "RefC"), sp(54, "Missing") }),
							 act("SET_UNIT_REFERENCE_TO_REFERENCE", { sp(54, "RefD"), sp(54, "") }) });
	f.script(2, "Refs", { act("SET_UNIT_REFERENCE_TO_REFERENCE", { sp(54, "RefE"), sp(54, "RefA") }),
							 act("SET_UNIT_REFERENCE_TO_REFERENCE", { sp(54, "RefF"), sp(54, "Alice/RefA") }) });
	f.start();
	u->setName("Unit1");
	f.frames(1);
	const auto &refs = f.engine().references();
	auto at = [&](const char *side, const char *name) {
		const auto it = refs.find({ side, name });
		return it == refs.end() ? INVALID_ID : it->second;
	};
	CHECK(at("Alice", "RefA") == u->getID()); // RW 0x7C2AF6 -> RW 0x608397
	CHECK(at("Alice", "RefB") == u->getID());
	CHECK(at("Alice", "RefC") == INVALID_ID); // a source not in the map copies nothing
	CHECK(at("Alice", "RefD") == INVALID_ID); // an empty source (RW 0x60851B)
	CHECK(at("Bob", "RefE") == INVALID_ID);   // "RefA" is Bob's own key there
	CHECK(at("Bob", "RefF") == u->getID());   // "Alice/RefA" names Alice's (RW 0x72C43C)
	CHECK(refs.size() == 3);
	CHECK(f.engine().getUnitNamed("RefA") == u); // the named cache too (RW 0x7C2B07 -> RW 0x60A1D5)
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("modcompat-ai SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER (RW 0x7C37C9): the one player of the mask's free command points (RW 0x6A7F6A) "
		  "into the counter; a mask of several players leaves it alone")
{
	ModFx f;
	f.start();
	setUsage(*f.alice(), 30);
	f.run("SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER", { sp(11, "Alice"), sp(4, "cp") });
	CHECK(f.engine().counter("cp").value == f.alice()->commandPointsAvailable());
	CHECK(f.engine().counter("cp").value == f.economy().commandPointLimit(*f.alice()) - 30);
	f.engine().counter("cp").value = 42;
	f.run("SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER", { sp(11, "<All Players>"), sp(4, "cp") });
	CHECK(f.engine().counter("cp").value == 42);
	f.run("SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER", { sp(11, "Nobody"), sp(4, "other") });
	CHECK(f.engine().findCounter("other") == nullptr); // RW 0x7C37E3: no counter is made
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("modcompat-ai AI_PLAYER_BUILD_UPGRADE (RW 0x7BBBBF -> AIPlayer::buildUpgrade RW 0x8F7599): a computer player's first object with the upgrade's "
		  "button queues the PLAYER upgrade; a human player, an OBJECT upgrade or one already researched: nothing")
{
	ModFx f;
	Object *bobHall = f.make("UHall", f.teamOf("Bob"));
	Object *aliceHall = f.make("UHall", f.teamOf("Alice"));
	f.start();
	REQUIRE(f.bob()->getPlayerType() == PLAYER_COMPUTER);
	REQUIRE(f.alice()->getPlayerType() == PLAYER_HUMAN);
	const UpgradeTemplate *basic = f.upgrade("Upgrade_Basic");
	f.run("AI_PLAYER_BUILD_UPGRADE", { sp(11, "Alice"), sp(33, "Upgrade_Basic") });
	CHECK_FALSE(f.pu(aliceHall)->isUpgradeInQueue(basic)); // RW 0x6AA80E: no AIPlayer
	f.run("AI_PLAYER_BUILD_UPGRADE", { sp(11, "Bob"), sp(33, "Upgrade_Barracks2") });
	CHECK(f.pu(bobHall)->getProductionCount() == 0); // an OBJECT upgrade is refused (RW 0x8F75E2)
	f.run("AI_PLAYER_BUILD_UPGRADE", { sp(11, "<All Players>"), sp(33, "Upgrade_Basic") }); // every player of the mask
	CHECK(f.pu(bobHall)->isUpgradeInQueue(basic));
	CHECK(f.bob()->hasUpgradeInProduction(basic));
	CHECK_FALSE(f.pu(aliceHall)->isUpgradeInQueue(basic));
	f.run("AI_PLAYER_BUILD_UPGRADE", { sp(11, "Bob"), sp(33, "Upgrade_Basic") });
	CHECK(f.pu(bobHall)->getProductionCount() == 1); // in production already (RW 0x6AB2E1)
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("modcompat-ai PLAYER_AFFECT_RECEIVING_EXPERIENCE (RW 0x7BD6A8): every player of the mask's skill points modifier (Player + 0x18)")
{
	ModFx f;
	f.start();
	REQUIRE(f.alice()->science().skillPointsModifier() == 1.0f);
	f.run("PLAYER_AFFECT_RECEIVING_EXPERIENCE", { sp(11, "Alice"), rp(0.5f) });
	CHECK(f.alice()->science().skillPointsModifier() == 0.5f);
	CHECK(f.bob()->science().skillPointsModifier() == 1.0f);
	f.run("PLAYER_AFFECT_RECEIVING_EXPERIENCE", { sp(11, "<All Players>"), rp(2.0f) });
	CHECK(f.alice()->science().skillPointsModifier() == 2.0f);
	CHECK(f.bob()->science().skillPointsModifier() == 2.0f);
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("modcompat-ai TEAM_PARTIAL_USE_COMMANDBUTTON (RW 0x7C7D12): of the team's members that may use the button with no target (RW 0x75CF21), "
		  "newest first, the first ceil(n * percent * 0.01) run it (an OBJECT_UPGRADE button queues the upgrade); an upgrade button needs an empty upgrade queue and an "
		  "unowned upgrade it affects")
{
	ModFx f;
	std::vector<Object *> halls;
	for (int i = 0; i < 5; ++i)
	{
		halls.push_back(f.make("UHall", f.teamOf("Alice")));
	}
	f.start();
	const UpgradeTemplate *level2 = f.upgrade("Upgrade_Barracks2");
	const CommandButton *b = TheCommandStore->findCommandButton("Command_HallLevel2");
	REQUIRE(b);
	halls[4]->giveUpgradeSelf(level2); // owns it already
	REQUIRE(f.pu(halls[3])->queueUpgrade(f.upgrade("Upgrade_Basic"))); // an upgrade in its queue
	CHECK(ActionManager::canUseCommandButton(*halls[0], *b));
	CHECK_FALSE(ActionManager::canUseCommandButton(*halls[3], *b));
	CHECK_FALSE(ActionManager::canUseCommandButton(*halls[4], *b));
	CHECK_FALSE(ActionManager::canUseCommandButton(*f.make("Soldier", f.teamOf("Alice")), *TheCommandStore->findCommandButton("Command_BuildSoldier"))); // no power
	const std::string team = "Alice/" + f.teamOf("Alice")->getName();
	auto queued = [&](int i) { return f.pu(halls[(size_t)i])->isUpgradeInQueue(level2); };
	// 3 usable halls (0, 1, 2; the soldier made above has no ProductionUpdate), newest first: 50 % -> ceil(1.5) = 2 runs, halls 2 and 1 queue the
	// upgrade (Object::doCommandButton RW 0x696FD2 -> RW 0x6970F2)
	f.run("TEAM_PARTIAL_USE_COMMANDBUTTON", { rp(50.0f), sp(3, team), sp(42, "Command_HallLevel2") });
	CHECK(queued(2));
	CHECK(queued(1));
	CHECK_FALSE(queued(0));
	// halls 2 and 1 hold an upgrade entry now: hall 0 alone is usable, ceil(1 * 10 * 0.01) = 1
	f.run("TEAM_PARTIAL_USE_COMMANDBUTTON", { rp(10.0f), sp(3, team), sp(42, "Command_HallLevel2") });
	CHECK(queued(0));
	CHECK_FALSE(queued(4)); // it owns the upgrade
	CHECK(f.pu(halls[3])->getProductionCount() == 1); // only its Upgrade_Basic
	// nothing usable is left; an unknown team or button does nothing
	f.run("TEAM_PARTIAL_USE_COMMANDBUTTON", { rp(100.0f), sp(3, team), sp(42, "Command_HallLevel2") });
	f.run("TEAM_PARTIAL_USE_COMMANDBUTTON", { rp(100.0f), sp(3, "Alice/NoTeam"), sp(42, "Command_HallLevel2") });
	f.run("TEAM_PARTIAL_USE_COMMANDBUTTON", { rp(100.0f), sp(3, team), sp(42, "NoSuchButton") });
	for (int i = 0; i < 3; ++i)
	{
		CHECK(f.pu(halls[(size_t)i])->getProductionCount() == 1);
	}
	CHECK(f.noteCount("[S-1190]") == 0);
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("modcompat-ai SET_CAMERA_CLIP_DEPTH_MULTIPLIER (RW 0x7CAFA5 case 0x14E: GlobalData + 0x950) is a client request, not an unported action")
{
	ModFx f;
	f.start();
	f.run("SET_CAMERA_CLIP_DEPTH_MULTIPLIER", { rp(1.5f) });
	CHECK(f.engine().stats().unportedActions.empty());
	const std::vector<ScriptClientRequest> requests = f.engine().takeClientRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].action == "SET_CAMERA_CLIP_DEPTH_MULTIPLIER");
	REQUIRE(requests[0].params.size() == 1);
	CHECK(requests[0].params[0].realValue == 1.5f);
}
