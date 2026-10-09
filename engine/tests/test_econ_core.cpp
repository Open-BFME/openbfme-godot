// OpenBFME unit tests: the economy's player-level rules (lane ECON-1): GameData's economy values, command points, money and the score keeper, the ObjectFilter
// decision, the cost modifier upgrades.
//
// The expected numbers are derived by hand from the retail INI values and the RotWK rules cited with each test (RW 0x6A7C86 command point init, 0x6A7B9F limit,
// 0x6A7F79 canAfford, 0x6AA56D / 0x6AA590 gain and loss, 0x763543 ObjectFilter allow, 0x6AD8A7 cost modifiers), never from this engine's own output.

#include "doctest.h"

#include "GameLogic/Object/ExperienceTracker.h"
#include "EconTestUtil.h"

#include "Common/PlayerList.h"
#include "GameLogic/Module/UpgradeModules.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>
#include <cstring>

using namespace logictest;
using namespace econtest;

namespace
{
const char kObjects[] =
	"Object Soldier\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  CommandPoints = 6\n"
	"End\n"
	"Object SoldierHorde\n"
	"  KindOf = SELECTABLE HORDE INFANTRY\n"
	"  CommandPoints = 40\n"
	"End\n"
	"Object Fortress\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  CommandPointBonus = 40\n"
	"End\n"
	"Object Wall\n"
	"  KindOf = STRUCTURE\n"
	"  CommandPoints = 9\n"
	"End\n"
	"Object Statue\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"End\n"
	"Object Wraith\n"
	"  KindOf = SELECTABLE ARMY_OF_DEAD\n"
	"  CommandPoints = 500\n"
	"End\n"
	"Object Hero\n"
	"  KindOf = SELECTABLE HERO INFANTRY\n"
	"  CommandPoints = 12\n"
	"End\n"
	"Object FortressUpgrader\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = CommandPointsUpgrade ModuleTag_CP\n"
	"    TriggeredBy = Upgrade_Anything\n"
	"    CommandPoints = 200\n"
	"  End\n"
	"End\n"
	"Object ConditionalUpgrader\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = CommandPointsUpgrade ModuleTag_CP\n"
	"    CommandPoints = 50\n"
	"    RequiredObject = ANY +Fortress\n"
	"  End\n"
	"End\n"
	"Object Hearth\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = CostModifierUpgrade ModuleTag_CostModifier\n"
	"    LabelForPalantirString = GUI:INFANTRYDiscount\n"
	"    StartsActive = Yes\n"
	"    ObjectFilter = ANY +INFANTRY -HERO\n"
	"    Percentage = -4%\n"
	"    Percentage = -8%\n"
	"    Percentage = -12%\n"
	"  End\n"
	"End\n"
	"Object SlaughterHouse\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = CostModifierUpgrade ModuleTag_Slaughter\n"
	"    StartsActive = Yes\n"
	"    Slaughter = Yes\n"
	"    Percentage = -25%\n"
	"    Percentage = -50%\n"
	"  End\n"
	"End\n"
	"Object Forge\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = CostModifierUpgrade ModuleTag_Anvil\n"
	"    UpgradeDiscount = Yes\n"
	"    StartsActive = Yes\n"
	"    ApplyToTheseUpgrades = Upgrade_Blades Upgrade_Armor\n"
	"    Percentage = -10%\n"
	"    Percentage = -20%\n"
	"  End\n"
	"End\n"
	"Object SoldierAlias\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  EquivalentTo = Soldier\n"
	"End\n";

const char kThreePlayers[] =
	"PlayerTemplate FactionC\n"
	"  Side = Gamma\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 500\n"
	"  StartingBuilding = GammaKeep\n"
	"End\n";

ObjectStatusMaskType statusMask(const char *name)
{
	ObjectStatusMaskType m{};
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	REQUIRE(bit >= 0);
	MaskSet(m, (unsigned)bit, true);
	return m;
}

// a world with `count` playable players (the first human), CommandPoints initialised
void setupPlayers(Fx &fx, int count)
{
	SkirmishSetup setup;
	for (int i = 0; i < count; ++i)
	{
		const bool evil = i % 2 == 1;
		setup.players.push_back({ "P" + std::to_string(i), evil ? "FactionB" : "FactionA", i == 0, i, 0, i });
	}
	setup.defaultStartingCash = 2000;
	REQUIRE(fx.players.setupSkirmish(setup).empty());
	fx.economy().initAllCommandPoints();
}
} // namespace

TEST_CASE("economy settings: the retail GameData economy values, the MPn token grammar, a missing key is an error")
{
	EconomySettings s;
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::scan(kGameData, s, &err), err);
	CHECK(s.loaded);
	CHECK(s.terrainResourceCellSize == 20.0f); // gamedata.ini: TerrainResourceCellSize = 20.0
	// the multiplayer pairs of gamedata.ini (start, cap), players 2 .. 8
	const int caps[7] = { 1000, 875, 750, 675, 625, 575, 500 };
	for (int i = 0; i < 7; ++i)
	{
		CHECK(s.goodMP[i].start == 100);
		CHECK(s.goodMP[i].cap == caps[i]);
		CHECK(s.evilMP[i].start == 100);
		CHECK(s.evilMP[i].cap == caps[i]);
	}
	CHECK(s.goodSolo.start == 100);
	CHECK(s.goodSolo.cap == 150);
	CHECK(s.evilSolo.start == 300);
	CHECK(s.evilSolo.cap == 350);
	CHECK(s.goodAI.start == 600);
	CHECK(s.evilAI.cap == 650);
	CHECK(s.goodBonus == 20);
	CHECK(s.evilBonus == 50);
	CHECK(s.goodLimit == 300);
	CHECK(s.evilLimit == 600);
	CHECK(s.powerLimit == 60);
	CHECK(s.resourceMultiplierLimit == 5.0f);
	CHECK(s.resourceBonusMultiplier == 10.0f);
	for (int i = 0; i < 20; ++i)
	{
		CHECK(s.multiPlayMoneyMult[i] == 1.0f);
	}
	// RW 0x642182: tokens are NAME:value, the name compared without case, MP9 is read and dropped; the slots past MP8 and unread slots stay 1.0
	std::string text = kGameData;
	const std::string line = "  MultiPlayMoneyMult = MP1:1.0 MP2:1.0 MP3:1.0 MP4:1.0 MP5:1.0 MP6:1.0 MP7:1.0 MP8:1.0\n";
	const size_t at = text.find(line);
	REQUIRE(at != std::string::npos);
	text.replace(at, line.size(), "  MultiPlayMoneyMult = mp2:1.5 MP4:0.5 MP9:7.0\n");
	EconomySettings t;
	REQUIRE_MESSAGE(EconomySettings::scan(text, t, &err), err);
	CHECK(t.multiPlayMoneyMult[0] == 1.0f);
	CHECK(t.multiPlayMoneyMult[1] == 1.5f);
	CHECK(t.multiPlayMoneyMult[3] == 0.5f);
	CHECK(t.multiPlayMoneyMult[8] == 1.0f);
	// a missing key names itself
	text = kGameData;
	const size_t cut = text.find("  GoodCommandPointsMP5 = 100 675\n");
	REQUIRE(cut != std::string::npos);
	text.erase(cut, std::string("  GoodCommandPointsMP5 = 100 675\n").size());
	EconomySettings u;
	CHECK_FALSE(EconomySettings::scan(text, u, &err));
	CHECK(err.find("GoodCommandPointsMP5") != std::string::npos);
	CHECK_FALSE(u.loaded);
	CHECK_FALSE(EconomySettings::scan("", u, &err));
	CHECK(err.find("no GameData block") != std::string::npos);
}

TEST_CASE("command points: the multiplayer pair of the live player count (RW 0x6A7C86), the lobby percentage, a script's values")
{
	Fx fx(kObjects, kThreePlayers);
	// two playable players (the neutral player is not playable): MP2, 100 / 1000
	CHECK(fx.economy().livePlayableCount(false) == 2);
	for (const char *name : { "Alice", "Bob" })
	{
		const CommandPoints &cp = fx.player(name)->commandPoints();
		CHECK(cp.getBase() == 100);
		CHECK(cp.getCap() == 1000);
		CHECK(cp.getUsage() == 0);
		CHECK(cp.getBonus() == 0);
		CHECK(cp.getPlayerIndex() == fx.player(name)->getPlayerIndex());
	}
	CHECK(fx.player("Alice")->commandPointLimit() == 100); // min(cap, bonus + base)
	CHECK(fx.player("Alice")->commandPointsAvailable() == 100);
	// n players: n >= 8 -> MP8 ... n == 3 -> MP3, n <= 2 -> MP2 (gamedata.ini caps 1000 875 750 675 625 575 500)
	const int expectedCap[10] = { 0, 0, 1000, 875, 750, 675, 625, 575, 500, 500 };
	for (int n = 2; n <= 9; ++n)
	{
		setupPlayers(fx, n);
		CHECK(fx.economy().livePlayableCount(false) == n);
		for (int i = 0; i < n; ++i)
		{
			CHECK_MESSAGE(fx.player(("P" + std::to_string(i)).c_str())->commandPoints().getCap() == expectedCap[n], "players " << n);
			CHECK(fx.player(("P" + std::to_string(i)).c_str())->commandPoints().getBase() == 100);
		}
	}
	// an observer and a defeated player are not counted (RW 0x6A8630: + 0x754 / + 0x35A)
	setupPlayers(fx, 4);
	fx.player("P1")->setDefeated(true);
	CHECK(fx.economy().livePlayableCount(false) == 3);
	fx.economy().initAllCommandPoints();
	CHECK(fx.player("P0")->commandPoints().getCap() == 875); // MP3
	// the lobby percentage scales the cap (GameLogic + 0x114 == 3 and TheGameInfo): cap * percent / 100 with C integer division
	setupPlayers(fx, 2);
	fx.economy().context().lobbyCommandPointPercent = 50;
	fx.economy().initAllCommandPoints();
	CHECK(fx.player("P0")->commandPoints().getCap() == 500);
	fx.economy().context().lobbyCommandPointPercent = 33;
	fx.economy().initAllCommandPoints();
	CHECK(fx.player("P0")->commandPoints().getCap() == 330); // 1000 * 33 / 100
	fx.economy().context().gameInfoPresent = false;
	fx.economy().initAllCommandPoints();
	CHECK(fx.player("P0")->commandPoints().getCap() == 1000); // no TheGameInfo: no scaling
	fx.economy().context().gameInfoPresent = true;
	fx.economy().context().lobbyCommandPointPercent = 100;
	// a script's values stick until a larger value arrives (RW 0x6A7DC7 .. 0x6A7DE2)
	setupPlayers(fx, 2);
	Player *p0 = fx.player("P0");
	p0->commandPoints().setFromScript(500, 2000);
	fx.economy().initCommandPoints(*p0);
	CHECK(p0->commandPoints().getBase() == 500); // 100 is not larger
	CHECK(p0->commandPoints().getCap() == 2000); // 1000 is not larger
	p0->commandPoints().setFromScript(40, 70);
	fx.economy().initCommandPoints(*p0);
	CHECK(p0->commandPoints().getBase() == 100);
	CHECK(p0->commandPoints().getCap() == 1000);
	CHECK(p0->commandPoints().scriptSet());
	// the other game kinds: a human player takes the solo pair, a computer player the AI pair
	setupPlayers(fx, 2);
	fx.economy().context().gameMode = EconomyContext::MODE_SINGLE_PLAYER;
	fx.economy().initAllCommandPoints();
	CHECK(fx.player("P0")->commandPoints().getBase() == 100); // human, good: GoodCommandPoints 100 150
	CHECK(fx.player("P0")->commandPoints().getCap() == 150);
	CHECK(fx.player("P1")->commandPoints().getBase() == 600); // computer, evil: EvilCommandPointsAI 600 650
	CHECK(fx.player("P1")->commandPoints().getCap() == 650);
}

TEST_CASE("command points: objects gain and lose them exactly once (RW 0x68E0C2 / 0x68E114 / 0x6AA56D / 0x6AA590)")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	Player *bob = fx.player("Bob");
	// a unit counts its CommandPoints while it lives
	Object *s1 = fx.make("Soldier", fx.teamOf("Alice"));
	CHECK(alice->commandPoints().getUsage() == 6);
	CHECK(s1->isCountedInCommandPoints());
	Object *s2 = fx.make("Soldier", fx.teamOf("Alice"));
	CHECK(alice->commandPoints().getUsage() == 12);
	// a structure does not use them (CommandPointBonus adds to the limit instead), a horde leader does not, a template without CommandPoints counts nothing
	fx.make("Wall", fx.teamOf("Alice"));
	fx.make("SoldierHorde", fx.teamOf("Alice"));
	fx.make("Statue", fx.teamOf("Alice"));
	CHECK(alice->commandPoints().getUsage() == 12);
	CHECK(alice->commandPoints().getBonus() == 0);
	Object *fortress = fx.make("Fortress", fx.teamOf("Alice"));
	CHECK(alice->commandPoints().getBonus() == 40);
	CHECK(alice->commandPoints().getUsage() == 12);
	CHECK(alice->commandPointLimit() == 140);
	CHECK(alice->commandPointsAvailable() == 128);
	// death removes them once; destruction afterwards removes nothing more
	s1->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->commandPoints().getUsage() == 6);
	CHECK_FALSE(s1->isCountedInCommandPoints());
	fx.logic->destroyObject(s1);
	fx.logic->processDestroyList();
	CHECK(alice->commandPoints().getUsage() == 6);
	// destruction without dying (RW 0x69031F) removes them too, and a destroyed building takes its bonus away
	fx.logic->destroyObject(s2);
	fx.logic->destroyObject(fortress);
	fx.logic->processDestroyList();
	CHECK(alice->commandPoints().getUsage() == 0);
	CHECK(alice->commandPoints().getBonus() == 0);
	// an object under construction does not count until it is added once it is complete
	ObjectStatusMaskType building = statusMask("UNDER_CONSTRUCTION");
	Object *half = fx.logic->newObject(fx.w.get("Soldier"), fx.teamOf("Alice"), building);
	CHECK(alice->commandPoints().getUsage() == 0);
	half->addToPlayerCommandPoints();
	CHECK(alice->commandPoints().getUsage() == 0); // still under construction
	half->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"), false);
	half->addToPlayerCommandPoints();
	CHECK(alice->commandPoints().getUsage() == 6);
	half->addToPlayerCommandPoints(); // once
	CHECK(alice->commandPoints().getUsage() == 6);
	ObjectStatusMaskType pending = statusMask("PENDING_CONSTRUCTION");
	fx.logic->newObject(fx.w.get("Soldier"), fx.teamOf("Alice"), pending);
	CHECK(alice->commandPoints().getUsage() == 6);
	// capture: the usage moves to the new owner (RW 0x696F0A -> 0x6914B7)
	half->setTeam(fx.teamOf("Bob"));
	CHECK(alice->commandPoints().getUsage() == 0);
	CHECK(bob->commandPoints().getUsage() == 6);
	// a temporary defection does not move them
	half->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED"), true);
	half->setTeam(fx.teamOf("Alice"));
	CHECK(alice->commandPoints().getUsage() == 0);
	CHECK(bob->commandPoints().getUsage() == 6);
}

TEST_CASE("command points: canAfford is usage + cost <= limit, ARMY_OF_DEAD is always affordable, a free template needs no limit (RW 0x6A7F79)")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	for (int i = 0; i < 16; ++i)
	{
		fx.make("Soldier", fx.teamOf("Alice")); // 96 used of 100
	}
	CHECK(alice->commandPoints().getUsage() == 96);
	const ThingTemplate *soldier = fx.w.get("Soldier"); // 6 points
	CHECK_FALSE(alice->canAffordCommandPoints(*soldier)); // 96 + 6 = 102 > 100
	const ThingTemplate *statue = fx.w.get("Statue");       // no points
	CHECK(alice->canAffordCommandPoints(*statue));
	CHECK(alice->canAffordCommandPoints(*fx.w.get("Wraith"))); // 500 points, ARMY_OF_DEAD
	fx.make("Fortress", fx.teamOf("Alice"));                   // +40: limit 140
	CHECK(alice->canAffordCommandPoints(*soldier));
	CHECK(alice->commandPointsAvailable() == 44);
	// the limit never passes the cap
	for (int i = 0; i < 30; ++i)
	{
		fx.make("Fortress", fx.teamOf("Alice"));
	}
	CHECK(alice->commandPoints().getBonus() == 31 * 40);
	CHECK(alice->commandPointLimit() == 1000); // min(1000, 100 + 1240)
}

TEST_CASE("command points: CommandPointsUpgrade records raise the limit while their RequiredObject lives (RW 0x6A81E3, 0x6A7B9F)")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	Object *up = fx.make("FortressUpgrader", fx.teamOf("Alice"));
	CommandPointsUpgrade *cpu = dynamic_cast<CommandPointsUpgrade *>(up->findModule("CommandPointsUpgrade"));
	REQUIRE(cpu);
	CHECK(alice->commandPointLimit() == 100);
	cpu->giveUpgrade();
	REQUIRE(alice->commandPoints().records().size() == 1);
	CHECK(alice->commandPoints().records()[0].value == 200);
	CHECK(alice->commandPoints().records()[0].objectId == up->getID());
	CHECK(alice->commandPointLimit() == 300);
	cpu->giveUpgrade(); // once
	CHECK(alice->commandPoints().records().size() == 1);
	cpu->takeUpgrade();
	CHECK(alice->commandPoints().records().empty());
	CHECK(alice->commandPointLimit() == 100);
	// a record with a RequiredObject counts only while the player owns a live object it allows
	Object *cond = fx.make("ConditionalUpgrader", fx.teamOf("Alice"));
	CommandPointsUpgrade *cc = dynamic_cast<CommandPointsUpgrade *>(cond->findModule("CommandPointsUpgrade"));
	REQUIRE(cc);
	cc->giveUpgrade();
	CHECK(alice->commandPointLimit() == 100); // no Fortress yet
	Object *fortress = fx.make("Fortress", fx.teamOf("Alice")); // +40 bonus
	CHECK(alice->commandPointLimit() == 190);                   // 100 + 40 + 50
	ObjectStatusMaskType building = statusMask("UNDER_CONSTRUCTION");
	fx.logic->destroyObject(fortress);
	fx.logic->processDestroyList();
	Object *unfinished = fx.logic->newObject(fx.w.get("Fortress"), fx.teamOf("Alice"), building);
	(void)unfinished;
	CHECK(alice->commandPointLimit() == 100); // a Fortress under construction does not satisfy the filter (completedOnly)
	// the cap bounds records too
	alice->commandPoints().setFromScript(100, 80);
	CHECK(alice->commandPointLimit() == 80);
	// the first record with that value and object id goes (RW 0x6A8033)
	alice->commandPoints().addRecord(7, 99, nullptr);
	alice->commandPoints().addRecord(7, 99, nullptr);
	CHECK(alice->commandPoints().removeRecord(7, 99));
	CHECK(alice->commandPoints().records().size() == 2); // the conditional one and one 7
	CHECK_FALSE(alice->commandPoints().removeRecord(8, 99));
}

TEST_CASE("money: deposits and withdrawals pass the score keeper like RW 0x7B18B8 / 0x7B17EF, a zero amount does nothing, a withdrawal is capped at the cash")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	const std::uint32_t start = alice->getMoney()->countMoney();
	CHECK(start == 1500); // FactionA StartMoney
	alice->depositMoney(250);
	CHECK(alice->getMoney()->countMoney() == start + 250);
	CHECK(alice->getScoreKeeper().moneyEarned() == 250);
	CHECK(alice->getScoreKeeper().moneyEarnedSecond() == 250);
	CHECK(alice->getScoreKeeper().moneySpent() == 0);
	alice->depositMoney(0);
	CHECK(alice->getScoreKeeper().moneyEarned() == 250);
	CHECK(alice->withdrawMoney(100) == 100);
	CHECK(alice->getScoreKeeper().moneySpent() == 100);
	CHECK(alice->withdrawMoney(1000000) == start + 250 - 100); // the rest of the cash
	CHECK(alice->getMoney()->countMoney() == 0);
	CHECK(alice->getScoreKeeper().moneySpent() == start + 250);
	CHECK(alice->withdrawMoney(5) == 0);
	CHECK(alice->getScoreKeeper().moneySpent() == start + 250);
	CHECK_FALSE(alice->canAfford(1));
	alice->depositMoney(40);
	CHECK(alice->canAfford(40));
	CHECK_FALSE(alice->canAfford(41));
	// the keep-score switch (GameLogic + 0x98) stops the counters, not the cash
	fx.economy().setScoring(false);
	alice->depositMoney(10);
	CHECK(alice->getMoney()->countMoney() == 50);
	CHECK(alice->getScoreKeeper().moneyEarned() == 250 + 40);
	fx.economy().setScoring(true);
	alice->depositMoney(10);
	CHECK(alice->getScoreKeeper().moneyEarned() == 250 + 40 + 10);
	// the raw Money API keeps ZH's signature: no keeper, no counters
	alice->getMoney()->deposit(5);
	CHECK(alice->getScoreKeeper().moneyEarned() == 300);
}

TEST_CASE("object filter match: rules, KindOf masks, names, S: names, equivalence, side and relationships (RW 0x763543, 0x73D5C2)")
{
	Fx fx(kObjects);
	GameLogic &logic = *fx.logic;
	Object *soldier = fx.make("Soldier", fx.teamOf("Alice"));
	Object *hero = fx.make("Hero", fx.teamOf("Alice"));
	Object *fortress = fx.make("Fortress", fx.teamOf("Bob"));
	Object *alias = fx.make("SoldierAlias", fx.teamOf("Alice"));
	const int infantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY");
	const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	REQUIRE(infantry >= 0);
	REQUIRE(heroBit >= 0);
	REQUIRE(structure >= 0);
	Player *alice = fx.player("Alice");
	Player *bob = fx.player("Bob");
	// the default filter is rule ALL: everything passes unless an exclude says no
	ObjectFilter all;
	CHECK(ObjectFilterMatch::allows(logic, all, *soldier, nullptr));
	ObjectFilter noHeroes;
	BitFlagsSet(noHeroes.excludeKindOf, (size_t)heroBit);
	CHECK(ObjectFilterMatch::allows(logic, noHeroes, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, noHeroes, *hero, nullptr));
	// ANY +INFANTRY -HERO
	ObjectFilter anyInfantry;
	anyInfantry.rule = ObjectFilter::RULE_ANY;
	BitFlagsSet(anyInfantry.includeKindOf, (size_t)infantry);
	BitFlagsSet(anyInfantry.excludeKindOf, (size_t)heroBit);
	CHECK(ObjectFilterMatch::allows(logic, anyInfantry, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, anyInfantry, *hero, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, anyInfantry, *fortress, nullptr));
	// ANY with no include at all allows nothing; NONE with an include mask needs ALL of its bits (RW 0x70B8C7)
	ObjectFilter anyNothing;
	anyNothing.rule = ObjectFilter::RULE_ANY;
	CHECK_FALSE(ObjectFilterMatch::allows(logic, anyNothing, *soldier, nullptr));
	ObjectFilter noneSet;
	noneSet.rule = ObjectFilter::RULE_NONE;
	BitFlagsSet(noneSet.includeKindOf, (size_t)infantry);
	BitFlagsSet(noneSet.includeKindOf, (size_t)heroBit);
	CHECK_FALSE(ObjectFilterMatch::allows(logic, noneSet, *soldier, nullptr)); // lacks HERO
	CHECK(ObjectFilterMatch::allows(logic, noneSet, *hero, nullptr));          // has both
	ObjectFilter noneEmpty = ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{});
	CHECK_FALSE(ObjectFilterMatch::allows(logic, noneEmpty, *soldier, nullptr));
	// names: a plain name is the template or anything equivalent to it, S: is the exact name only, -Name excludes
	ObjectFilter byName;
	byName.rule = ObjectFilter::RULE_ANY;
	byName.includeNames.push_back("Soldier");
	CHECK(ObjectFilterMatch::allows(logic, byName, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, byName, *hero, nullptr));
	CHECK(ObjectFilterMatch::allows(logic, byName, *alias, nullptr)); // SoldierAlias: EquivalentTo = Soldier
	ObjectFilter exact;
	exact.rule = ObjectFilter::RULE_ANY;
	exact.includeNames.push_back("S:Soldier");
	CHECK(ObjectFilterMatch::allows(logic, exact, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, exact, *alias, nullptr));
	ObjectFilter notSoldier;
	notSoldier.excludeNames.push_back("Soldier");
	CHECK_FALSE(ObjectFilterMatch::allows(logic, notSoldier, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, notSoldier, *alias, nullptr));
	CHECK(ObjectFilterMatch::allows(logic, notSoldier, *hero, nullptr));
	CHECK(ObjectFilterMatch::unresolvedNames(fx.w.things, byName).empty());
	ObjectFilter unknown;
	unknown.includeNames.push_back("NoSuchThing");
	unknown.excludeNames.push_back("S:AlsoMissing");
	CHECK(ObjectFilterMatch::unresolvedNames(fx.w.things, unknown) == std::vector<std::string>{ "NoSuchThing", "S:AlsoMissing" });
	// the first matching include name wins over an exclude KindOf of a later step only through the documented order: names are decided before the exclude mask
	ObjectFilter order;
	order.includeNames.push_back("S:Hero");
	BitFlagsSet(order.excludeKindOf, (size_t)heroBit);
	CHECK(ObjectFilterMatch::allows(logic, order, *hero, nullptr));
	// side: EVIL needs the object's owner to be an evil faction (Bob is FactionB, Evil = Yes)
	ObjectFilter evil;
	evil.side = ObjectFilter::SIDE_EVIL;
	ObjectFilter good;
	good.side = ObjectFilter::SIDE_GOOD;
	CHECK(ObjectFilterMatch::allows(logic, evil, *fortress, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, evil, *soldier, nullptr));
	CHECK(ObjectFilterMatch::allows(logic, good, *soldier, nullptr));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, good, *fortress, nullptr));
	// relationships: B's view of A's default team. Alice and Bob are enemies; a filter with a relationship and no second player fails (RW 0x7635ED)
	ObjectFilter enemies;
	enemies.relationships = ObjectFilter::REL_ENEMIES;
	CHECK(ObjectFilterMatch::allows(logic, enemies, *fortress, alice));     // Alice vs Bob's building
	CHECK_FALSE(ObjectFilterMatch::allows(logic, enemies, *soldier, alice)); // Alice's own
	CHECK_FALSE(ObjectFilterMatch::allows(logic, enemies, *fortress, nullptr));
	ObjectFilter own;
	own.relationships = ObjectFilter::REL_SAME_PLAYER;
	CHECK(ObjectFilterMatch::allows(logic, own, *soldier, alice));
	CHECK_FALSE(ObjectFilterMatch::allows(logic, own, *soldier, bob));
	ObjectFilter allies;
	allies.relationships = ObjectFilter::REL_ALLIES;
	CHECK(ObjectFilterMatch::allows(logic, allies, *soldier, alice)); // a player is its own ally (ZH finishRelationships)
	// the "valid" test of a stored filter (RW 0x762977): set and flagged
	CHECK_FALSE(ObjectFilterMatch::isValid(nullptr));
	CHECK(ObjectFilterMatch::isValid(&anyInfantry));
	CHECK_FALSE(ObjectFilterMatch::isValid(&noneEmpty)); // NONE has flag 0
	CHECK(ObjectFilterMatch::isEquivalentTo(fx.w.get("Soldier"), fx.w.get("SoldierAlias")));
	CHECK_FALSE(ObjectFilterMatch::isEquivalentTo(fx.w.get("Soldier"), fx.w.get("Hero")));
	CHECK_FALSE(ObjectFilterMatch::isEquivalentTo(nullptr, fx.w.get("Hero")));
	(void)structure;
}

namespace
{
// the retail value of 1.0f + percentage as a 24-bit x87 add stored as float: one rounding of the exact sum (the operands are exact in double)
float onePlus(float percentage)
{
	return (float)(1.0 + (double)percentage);
}
} // namespace

TEST_CASE("cost modifiers: the entries that apply take the percentage at the index of the entries before them (RW 0x6AD8A7)")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	Player *bob = fx.player("Bob");
	const ThingTemplate *soldier = fx.w.get("Soldier");
	const ThingTemplate *hero = fx.w.get("Hero");
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == 1.0f);
	// the Percentage values as the INI parser makes them (parsePercentToReal: x87 value * 0.01f at 24 bits, INI-1's tests pin the parser)
	const std::vector<float> hp = static_cast<const CostModifierUpgradeModuleData *>(fx.w.get("Hearth")->behaviorModules().find("ModuleTag_CostModifier")->data.get())->m_percentage;
	REQUIRE(hp.size() == 3);
	CHECK(hp[0] == (float)(-4.0 * (double)0.01f)); // 4 * 0.01f is exact: -0.04f
	CHECK(hp[1] == (float)(-8.0 * (double)0.01f));
	CHECK(hp[2] == (float)(-12.0 * (double)0.01f));
	// a Hearth completed: StartsActive runs the implementation when it is built (RW 0x8B9D0F); ANY +INFANTRY -HERO, Percentage -4% -8% -12%
	Object *h1 = fx.make("Hearth", fx.teamOf("Alice"));
	CHECK(alice->costModifiers().empty()); // not complete yet
	h1->friend_onBuildComplete();
	REQUIRE(alice->costModifiers().size() == 1);
	CHECK(alice->costModifiers()[0].source == h1->getID());
	CHECK(alice->costModifiers()[0].percents.size() == 3);
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[0]));
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*hero) == 1.0f); // -HERO
	CHECK(bob->getProductionCostChangeBasedOnKindOf(*soldier) == 1.0f);  // the entry is Alice's
	// the second Hearth: the percentage at index 1
	Object *h2 = fx.make("Hearth", fx.teamOf("Alice"));
	h2->friend_onBuildComplete();
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[1]));
	Object *h3 = fx.make("Hearth", fx.teamOf("Alice"));
	h3->friend_onBuildComplete();
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[2]));
	Object *h4 = fx.make("Hearth", fx.teamOf("Alice")); // four entries, three percentages: the fourth applying entry keeps the third value
	h4->friend_onBuildComplete();
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[2]));
	// a building that is sold or gone stops counting, the others shift down
	h1->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD"), true);
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[2])); // h2, h3, h4: index 2
	fx.logic->destroyObject(h2);
	fx.logic->processDestroyList();
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[1])); // h3, h4
	CHECK(alice->costModifiers().size() == 3);                                      // h1 (sold), h3, h4: deleting h2 took its entry (RW 0x8B9B3C)
	// removing the upgrade takes the first matching entry out (RW 0x6AE74C)
	CostModifierUpgrade *m3 = dynamic_cast<CostModifierUpgrade *>(h3->findModule("CostModifierUpgrade"));
	REQUIRE(m3);
	m3->takeUpgrade();
	CHECK(alice->costModifiers().size() == 2); // h1 (sold) and h4 stay
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[0])); // only h4 applies
	// capture moves an executed upgrade's entry with the building (RW 0x8B9C4D)
	CostModifierUpgrade *m4 = dynamic_cast<CostModifierUpgrade *>(h4->findModule("CostModifierUpgrade"));
	REQUIRE(m4);
	h4->setTeam(fx.teamOf("Bob"));
	CHECK(bob->costModifiers().size() == 1);
	CHECK(bob->getProductionCostChangeBasedOnKindOf(*soldier) == onePlus(hp[0]));
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier) == 1.0f);
	CHECK(m4->isAlreadyUpgraded());
}

TEST_CASE("cost modifiers: Slaughter entries apply to the slaughter value only, UpgradeDiscount entries are reference counted and priced by upgrade name")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	const ThingTemplate *soldier = fx.w.get("Soldier");
	const std::vector<float> sp = static_cast<const CostModifierUpgradeModuleData *>(fx.w.get("SlaughterHouse")->behaviorModules().find("ModuleTag_Slaughter")->data.get())->m_percentage;
	const std::vector<float> fp = static_cast<const CostModifierUpgradeModuleData *>(fx.w.get("Forge")->behaviorModules().find("ModuleTag_Anvil")->data.get())->m_percentage;
	REQUIRE(sp.size() == 2);
	REQUIRE(fp.size() == 2);
	Object *house = fx.make("SlaughterHouse", fx.teamOf("Alice"));
	house->friend_onBuildComplete();
	REQUIRE(alice->costModifiers().size() == 1);
	CHECK(alice->costModifiers()[0].slaughter);
	// the module data's ObjectFilter is NONE (RW 0x8B9E08): the entry allows no template for a normal cost
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier, false) == 1.0f);
	// with the slaughter flag the filter is skipped for a Slaughter entry: percentage index 0 = -25%
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier, true) == onePlus(sp[0]));
	Object *house2 = fx.make("SlaughterHouse", fx.teamOf("Alice"));
	house2->friend_onBuildComplete();
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier, true) == onePlus(sp[1]));
	// a null template is neutral (RW 0x6AD8AD)... asked through the host with a template that no entry allows
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*fx.w.get("Hero"), false) == 1.0f);
	// UpgradeDiscount: keyed by (source template name, percentages, upgrades), counted, the percentage at count - 1 (RW 0x6B2C67 / 0x6AB5E0)
	Object *forge = fx.make("Forge", fx.teamOf("Alice"));
	forge->friend_onBuildComplete();
	REQUIRE(alice->upgradeDiscounts().size() == 1);
	CHECK(alice->upgradeDiscounts()[0].count == 1);
	CHECK(alice->upgradeDiscounts()[0].current == fp[0]);
	CHECK(alice->getUpgradeCostChange("Upgrade_Blades") == fp[0]);
	CHECK(alice->getUpgradeCostChange("Upgrade_Other") == 0.0f); // not in ApplyToTheseUpgrades
	CHECK(alice->getUpgradeCostChange("") == fp[0]);            // the empty name takes every entry
	Object *forge2 = fx.make("Forge", fx.teamOf("Alice"));
	forge2->friend_onBuildComplete();
	CHECK(alice->upgradeDiscounts().size() == 1);
	CHECK(alice->upgradeDiscounts()[0].count == 2);
	CHECK(alice->getUpgradeCostChange("Upgrade_Armor") == fp[1]);
	Object *forge3 = fx.make("Forge", fx.teamOf("Alice")); // a third: only two percentages, the second repeats
	forge3->friend_onBuildComplete();
	CHECK(alice->upgradeDiscounts()[0].count == 3);
	CHECK(alice->getUpgradeCostChange("Upgrade_Armor") == fp[1]);
	dynamic_cast<CostModifierUpgrade *>(forge3->findModule("CostModifierUpgrade"))->takeUpgrade();
	CHECK(alice->upgradeDiscounts()[0].count == 2);
	dynamic_cast<CostModifierUpgrade *>(forge2->findModule("CostModifierUpgrade"))->takeUpgrade();
	dynamic_cast<CostModifierUpgrade *>(forge->findModule("CostModifierUpgrade"))->takeUpgrade();
	CHECK(alice->upgradeDiscounts().empty());
	CHECK(alice->getUpgradeCostChange("Upgrade_Blades") == 0.0f);
}

// retail vtable slot 8 of CostModifierUpgrade is the UpgradeMux removal (RW 0xC6EE3C + 0x20 = 0x8B9B3C -> [mux + 0x20]): deleting the object takes the upgrade
TEST_CASE("cost modifiers: deleting the object takes its entry back (RW 0x8B9B3C): ordinary, reference counted, and after a capture")
{
	Fx fx(kObjects);
	Player *alice = fx.player("Alice");
	Player *bob = fx.player("Bob");
	const ThingTemplate *soldier = fx.w.get("Soldier");
	const std::vector<float> fp = static_cast<const CostModifierUpgradeModuleData *>(fx.w.get("Forge")->behaviorModules().find("ModuleTag_Anvil")->data.get())->m_percentage;
	const std::vector<float> sp = static_cast<const CostModifierUpgradeModuleData *>(fx.w.get("SlaughterHouse")->behaviorModules().find("ModuleTag_Slaughter")->data.get())->m_percentage;
	auto remove = [&](Object *o) {
		fx.logic->destroyObject(o);
		fx.frames(1); // phase 5 deletes it
	};
	// ordinary entry
	Object *house = fx.make("SlaughterHouse", fx.teamOf("Alice"));
	house->friend_onBuildComplete();
	REQUIRE(alice->costModifiers().size() == 1);
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier, true) == onePlus(sp[0]));
	remove(house);
	CHECK(alice->costModifiers().empty());
	CHECK(alice->getProductionCostChangeBasedOnKindOf(*soldier, true) == 1.0f);
	// an object that never became active leaves nothing to take
	Object *idle = fx.make("SlaughterHouse", fx.teamOf("Alice"));
	remove(idle);
	CHECK(alice->costModifiers().empty());
	// reference counted upgrade discounts: three forges, delete them one by one (the count and the percentage follow)
	Object *f1 = fx.make("Forge", fx.teamOf("Alice"));
	Object *f2 = fx.make("Forge", fx.teamOf("Alice"));
	Object *f3 = fx.make("Forge", fx.teamOf("Alice"));
	f1->friend_onBuildComplete();
	f2->friend_onBuildComplete();
	f3->friend_onBuildComplete();
	REQUIRE(alice->upgradeDiscounts().size() == 1);
	CHECK(alice->upgradeDiscounts()[0].count == 3);
	remove(f3);
	CHECK(alice->upgradeDiscounts()[0].count == 2);
	CHECK(alice->getUpgradeCostChange("Upgrade_Blades") == fp[1]);
	remove(f1);
	CHECK(alice->upgradeDiscounts()[0].count == 1);
	CHECK(alice->getUpgradeCostChange("Upgrade_Blades") == fp[0]);
	remove(f2);
	CHECK(alice->upgradeDiscounts().empty());
	CHECK(alice->getUpgradeCostChange("Upgrade_Blades") == 0.0f);
	// capture, then delete: the entry moved to the new owner and leaves from THERE (RW 0x8B9C4D, then 0x8B9B3C on the new owner)
	Object *h = fx.make("SlaughterHouse", fx.teamOf("Alice"));
	h->friend_onBuildComplete();
	h->setTeam(fx.teamOf("Bob"));
	CHECK(alice->costModifiers().empty());
	REQUIRE(bob->costModifiers().size() == 1);
	remove(h);
	CHECK(bob->costModifiers().empty());
	CHECK(alice->costModifiers().empty());
	Object *fg = fx.make("Forge", fx.teamOf("Alice"));
	fg->friend_onBuildComplete();
	fg->setTeam(fx.teamOf("Bob"));
	CHECK(alice->upgradeDiscounts().empty());
	REQUIRE(bob->upgradeDiscounts().size() == 1);
	remove(fg);
	CHECK(bob->upgradeDiscounts().empty());
	CHECK(alice->upgradeDiscounts().empty());
}

TEST_CASE("economy report: every stop of the lane is a line of the logic report and the run-time counts follow the seams")
{
	Fx fx(kObjects);
	const GameLogic::Report r = fx.logic->report();
	std::string all;
	for (const std::string &s : r.stops)
	{
		all += s + "\n";
	}
	for (int id = 250; id <= 261; ++id)
	{
		const std::string tag = "[S-" + std::to_string(id) + "]";
		INFO(tag);
		CHECK(all.find(tag) != std::string::npos);
	}
	// the attribute modifier stop is the always-on line (a missing provider) plus the seam line of S-253: without a provider the report says so, with one the run-time line goes
	CHECK(all.find("no AttributeModifier provider is installed") != std::string::npos);
	fx.economy().setAttributeModifierProvider([](const Object &, int, float &) { return false; });
	const std::vector<std::string> with = fx.economy().report();
	for (const std::string &s : with)
	{
		CHECK(s.find("no AttributeModifier provider is installed") == std::string::npos);
	}
	// lane XP-1: GameLogic's ExperienceWorld installs the experience hook, so the grant reaches the object's tracker; without a hook it would be counted (S-256)
	Object *o = fx.makeAt("Soldier", "Alice", 1.0f, 1.0f);
	const float before = o->getExperienceTracker()->getExperience();
	fx.economy().grantBuildingExperience(*o, 5.0f);
	bool counted = false;
	for (const std::string &s : fx.economy().report())
	{
		counted = counted || s.find("[S-256] 1 experience grants") != std::string::npos;
	}
	CHECK_FALSE(counted);
	CHECK(o->getExperienceTracker()->getExperience() > before);
	fx.economy().setExperienceHook(Economy::ExperienceHook());
	fx.economy().grantBuildingExperience(*o, 5.0f);
	for (const std::string &s : fx.economy().report())
	{
		counted = counted || s.find("[S-256] 1 experience grants") != std::string::npos;
	}
	CHECK(counted);
}

namespace
{
const char kBountyObjects[] =
	"Object Grunt\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  BountyValue = 100\n"
	"End\n"
	"Object Champion\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  BountyValue = 100\n"
	"End\n"
	"Object Killer\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"End\n"
	"Object Whale\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  BountyValue = 2147483776\n"
	"End\n";
} // namespace

// RW 0x6AC06F: amount = ceil(value * percent) at 24 bits, then * MultiPlayMoneyMult[n - 1] (ftol), then the handicap
TEST_CASE("bounty: the victim's BountyValue times the killer's percentage, the multiplayer money multiplier, then the deposit (RW 0x6AC06F)")
{
	Fx fx(kBountyObjects);
	Player *alice = fx.player("Alice");
	Player *bob = fx.player("Bob");
	Object *killer = fx.makeAt("Killer", "Alice", 10.0f, 10.0f);
	Object *grunt = fx.makeAt("Grunt", "Bob", 20.0f, 10.0f);
	const std::uint32_t cash = alice->getMoney()->countMoney();
	// the default percent is 0.0 (RW 0x6B0346) and no modifier is active: nothing is paid
	CHECK(alice->getBountyPercent() == 0.0f);
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 0);
	CHECK(alice->getMoney()->countMoney() == cash);
	// 100 * 0.5 = 50
	alice->setBountyPercent(0.5f);
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 50);
	CHECK(alice->getMoney()->countMoney() == cash + 50);
	CHECK(alice->getScoreKeeper().moneyEarned() >= 50);
	// 100 * 0.333 = 33.3 -> ceil 34 (the binary32 product 33.299999 .. rounds at 24 bits; the ceiling gives 34)
	alice->setBountyPercent(0.333f);
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 34);
	// the killer's own BOUNTY_PERCENTAGE modifier wins when it is not exactly 0.0: 100 * 0.25 = 25
	fx.economy().setAttributeModifierSumProvider([](const Object &, int type, float &sum) {
		if (type == Economy::ATTRIBUTE_MODIFIER_BOUNTY_PERCENTAGE)
		{
			sum = 0.25f;
			return true;
		}
		return false;
	});
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 25);
	fx.economy().setAttributeModifierSumProvider(nullptr);
	// the multiplayer money multiplier of two live players (MP2): 100 * 0.5 = 50, * 1.5 = 75 (ftol)
	alice->setBountyPercent(0.5f);
	fx.economy().settings().multiPlayMoneyMult[1] = 1.5f;
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 75);
	// a single player game pays without it
	fx.economy().context().gameMode = EconomyContext::MODE_SINGLE_PLAYER;
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 50);
	fx.economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	fx.economy().settings().multiPlayMoneyMult[1] = 1.0f;
	// no killer: nothing; a victim under construction: nothing
	const std::uint32_t before = alice->getMoney()->countMoney();
	CHECK(fx.economy().awardBounty(*alice, nullptr, *grunt) == 0);
	grunt->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"), true);
	CHECK(fx.economy().awardBounty(*alice, killer, *grunt) == 0);
	CHECK(alice->getMoney()->countMoney() == before);
	grunt->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"), false);
	CHECK(bob->getMoney()->countMoney() == fx.player("Bob")->getMoney()->countMoney());
}

TEST_CASE("bounty: a HERO victim's value grows with its experience level through the hook, without it the unscaled value is paid and counted (S-261)")
{
	Fx fx(kBountyObjects);
	Player *alice = fx.player("Alice");
	Object *killer = fx.makeAt("Killer", "Alice", 10.0f, 10.0f);
	Object *hero = fx.makeAt("Champion", "Bob", 20.0f, 10.0f);
	alice->setBountyPercent(1.0f);
	CHECK(fx.economy().awardBounty(*alice, killer, *hero) == 100);
	CHECK(fx.economy().unscaledHeroBounties() == 1);
	bool counted = false;
	for (const std::string &s : fx.economy().report())
	{
		counted = counted || s.find("[S-261] 1 bounties") != std::string::npos;
	}
	CHECK(counted);
	// level data 3 / 4: value = trunc(100 * 3 / 4 + 100) = 175
	fx.economy().setHeroBountyScale([](const Object &, int &n, int &d) {
		n = 3;
		d = 4;
		return true;
	});
	CHECK(fx.economy().awardBounty(*alice, killer, *hero) == 175);
	CHECK(fx.economy().unscaledHeroBounties() == 1);
	// 1 / 3: x87 24 bits: 0.33333334 * 100 = 33.333336 (24 bits: 33.333336) + 100 = 133.33334 -> trunc 133; * 1.0 -> ceil(133) = 133
	fx.economy().setHeroBountyScale([](const Object &, int &n, int &d) {
		n = 1;
		d = 3;
		return true;
	});
	CHECK(fx.economy().awardBounty(*alice, killer, *hero) == 133);
}

// RW 0x6AC0E3 .. 0x6AC0F0: `fild` the value as a SIGNED dword, `fadd 2^32` at 24 bits when it was negative, THEN `fmul` the percentage. Derived by hand: 2147483776 = 2^31 + 128 is
// -2147483520 as a signed dword; adding 2^32 at 24 bits gives 2^31 + 128, a tie between 2^31 and 2^31 + 256, rounded to even = 2^31. The percentage with bits 0x3B0000C2 is
// 2^-9 * (1 + 194 * 2^-23); 2^31 times it is 2^22 + 97 = 4194401 exactly (a native double would multiply 2147483776 and get 4194401.25: ceil 4194402).
TEST_CASE("bounty: the unsigned fix-up rounds to 24 bits before the multiply (RW 0x6AC0E3), the multiplayer product likewise (RW 0x6AC13C)")
{
	Fx fx(kBountyObjects);
	Player *alice = fx.player("Alice");
	Object *killer = fx.makeAt("Killer", "Alice", 10.0f, 10.0f);
	Object *whale = fx.makeAt("Whale", "Bob", 20.0f, 10.0f);
	const std::uint32_t bits = 0x3B0000C2u;
	float percent;
	std::memcpy(&percent, &bits, sizeof(percent));
	alice->setBountyPercent(percent);
	fx.economy().settings().multiPlayMoneyMult[1] = 1.0f;
	const std::uint32_t cash = alice->getMoney()->countMoney();
	CHECK(fx.economy().awardBounty(*alice, killer, *whale) == 4194401);
	CHECK(alice->getMoney()->countMoney() == cash + 4194401);
	// the multiplayer tail (RW 0x6AC13C): with percentage 1.0 the first product is 2^31 (the value rounded to even as above), fistp of 2^31 overflows to 0x80000000 (a negative int),
	// and the tail loads that amount as unsigned again (fild; fadd 2^32 at 24 bits = 2^31), multiplies by 1.0 and ftol gives 0x80000000: a deposit of 2^31
	alice->setBountyPercent(1.0f);
	const std::uint32_t before = alice->getMoney()->countMoney();
	const int paid = fx.economy().awardBounty(*alice, killer, *whale);
	CHECK((std::uint32_t)paid == 2147483648u);
	CHECK(alice->getMoney()->countMoney() == before + 2147483648u);
}

// The retail command point code is 32-bit `add` / `sub` / `imul` (modulo 2^32) followed by SIGNED compares (RW 0x6A7B9F cmovg, 0x6A7F79 jle). Every vector below is derived by
// hand in two's complement; under UBSan (the lane's sanitizer run) none of them may be a signed overflow.
TEST_CASE("command points: 32-bit wrapping arithmetic with signed compares at the int boundaries (RW 0x6A7B9F, 0x6A7F79, 0x6A7F6A)")
{
	constexpr int kMax = 2147483647, kMin = -2147483647 - 1;
	CHECK(CpMath::add(kMax, 1) == kMin);
	CHECK(CpMath::add(kMin, -1) == kMax);
	CHECK(CpMath::sub(kMin, 1) == kMax);
	CHECK(CpMath::sub(kMax, -1) == kMin);
	CHECK(CpMath::sub(0, kMin) == kMin);
	CHECK(CpMath::mul(65536, 65536) == 0);
	CHECK(CpMath::mul(0x40000001, 4) == 4);
	CHECK(CpMath::mul(kMin, -1) == kMin);
	CHECK(CpMath::mul(kMax, kMax) == 1); // (2^31 - 1)^2 = 2^62 - 2^32 + 1 = 1 mod 2^32

	CommandPoints cp;
	cp.reset();
	cp.setFromScript(kMax, kMax);
	cp.addBonus(1); // bonus 1
	// limit: bonus + base = 1 + INT_MAX wraps to INT_MIN; the signed `cmovg` against the cap INT_MAX keeps INT_MIN
	CHECK(cp.getLimit([](const ObjectFilter &) { return true; }) == kMin);
	cp.removeBonus(1);
	CHECK(cp.getLimit([](const ObjectFilter &) { return true; }) == kMax);
	// records: base INT_MAX + a record of 1 wraps the same way
	cp.addRecord(1, 5, nullptr);
	CHECK(cp.getLimit([](const ObjectFilter &) { return true; }) == kMin);
	CHECK(cp.removeRecord(1, 5));
	// usage wraps and available = limit - usage wraps
	cp.addUsage(kMax);
	cp.addUsage(1);
	CHECK(cp.getUsage() == kMin);
	cp.removeUsage(1);
	CHECK(cp.getUsage() == kMax);
	cp.removeUsage(kMax);
	cp.removeUsage(1); // 0 - 1
	CHECK(cp.getUsage() == -1);
	CHECK(cp.getAvailable(kMin) == kMin + 1); // INT_MIN - (-1) = INT_MIN + 1, not an overflow; and with usage 1:
	cp.addUsage(2);
	CHECK(cp.getUsage() == 1);
	CHECK(cp.getAvailable(kMin) == kMax); // INT_MIN - 1 wraps to INT_MAX
	// canAfford: usage INT_MAX + a cost of 1 wraps to INT_MIN, which is <= any limit (the signed jle): affordable
	cp.removeUsage(1);
	cp.addUsage(kMax);
	CHECK(cp.canAfford(1, false, 0));
	cp.removeUsage(kMax);
	CHECK_FALSE(cp.canAfford(5, false, 4));
	CHECK(cp.canAfford(5, true, 4)); // ARMY_OF_DEAD

	// init: base = start + the territory terms (wrapping), the lobby percentage product wraps like the imul
	CommandPointsSource src;
	src.start = kMax;
	src.territoryGoodTerm = 1;
	src.territoryEvilTerm = 1;
	src.cap = 65536;
	src.lobbyPercent = 65536;
	src.applyLobbyPercent = true;
	CommandPoints fresh;
	fresh.reset();
	fresh.init(3, src);
	CHECK(fresh.getBase() == CpMath::add(CpMath::add(kMax, 1), 1)); // INT_MIN + 1
	CHECK(fresh.getBase() == kMin + 1);
	CHECK(fresh.getCap() == 0); // 65536 * 65536 = 0 mod 2^32, / 100 = 0
}

TEST_CASE("command points: the economy's counts and territory products wrap in 32 bits (RW 0x6A8630, 0x6A7ECA, 0x642002)")
{
	Fx fx(kObjects, kThreePlayers);
	constexpr int kMax = 2147483647, kMin = -2147483647 - 1;
	// MultiPlayMoneyMult[n - 1]: n = INT_MIN gives the index INT_MIN - 1 = INT_MAX (wrapped), outside [0, 20): 1.0
	CHECK(fx.economy().multiPlayMoneyMult(kMin) == 1.0f);
	CHECK(fx.economy().multiPlayMoneyMult(kMax) == 1.0f);
	CHECK(fx.economy().multiPlayMoneyMult(0) == 1.0f);
	CHECK(fx.economy().multiPlayMoneyMult(1) == fx.economy().settings().multiPlayMoneyMult[0]);
	// the live count with the territory counters: Alice's counters are INT_MAX and 1 (sum wraps), Bob's none; the count adds 1 per live player
	fx.player("Alice")->commandPoints().setTerritoryCounters(kMax, 1);
	const int withCounters = fx.economy().livePlayableCount(true);
	CHECK(withCounters == CpMath::add(CpMath::add(kMax, 1), 2)); // (INT_MAX + 1 wraps to INT_MIN) + 1 + 1 = INT_MIN + 2
	CHECK(withCounters == kMin + 2);
	CHECK(fx.economy().livePlayableCount(false) == 2);
	// the init with counters of 65536: the terms are counter * the pair's start (wrapped imul); MP tier of the (negative) count is MP2: start 100 -> 6553600; no wrap here,
	// but the next uses a counter that does: 0x02000000 * 100 = 0xC8000000 (negative as an int) and the base wraps with it
	fx.player("Alice")->commandPoints().setTerritoryCounters(0x02000000, 0);
	fx.player("Bob")->commandPoints().setTerritoryCounters(0, 0);
	fx.economy().initCommandPoints(*fx.player("Alice"));
	const int term = CpMath::mul(0x02000000, 100); // = 3355443200 - 2^32 = -939524096
	CHECK(term == -939524096);
	// the live count includes the counters (0x02000000 + 2 players) = 33554434 >= 8: the MP8 pair (start 100)
	CHECK(fx.economy().livePlayableCount(true) == 0x02000002);
	CHECK(fx.player("Alice")->commandPoints().getBase() == CpMath::add(100, CpMath::mul(0x02000000, 100)));
}
