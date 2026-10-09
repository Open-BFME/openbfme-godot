// OpenBFME unit tests: the economy's state is part of the deterministic world hash (lane ECON-1).
//
// Each case changes ONE field group of an otherwise identical world and requires the hash to change (a mutation test, like test_logic_hash.cpp): a field the hash
// misses would let two peers of a lockstep game desync silently.

#include "doctest.h"
#include "EconTestUtil.h"

#include "GameLogic/Module/AutoDepositUpdate.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Module/UpgradeModules.h"
#include "GameLogic/ObjectTemplateInfo.h"

using namespace logictest;
using namespace econtest;

namespace
{
const char kObjects[] =
	"Object Farm\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
	"    Radius = 60\n"
	"    MaxIncome = 100\n"
	"    IncomeInterval = 15000\n"
	"  End\n"
	"End\n"
	"Object Hall\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = AutoDepositUpdate ModuleTag_Deposit\n"
	"    DepositTiming = 6000\n"
	"    DepositAmount = 25\n"
	"  End\n"
	"  Behavior = CommandPointsUpgrade ModuleTag_CP\n"
	"    CommandPoints = 10\n"
	"  End\n"
	"  Behavior = CostModifierUpgrade ModuleTag_Cost\n"
	"    ObjectFilter = ANY +INFANTRY\n"
	"    Percentage = -4%\n"
	"  End\n"
	"End\n"
	"Object Raider\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  CommandPoints = 5\n"
	"  Behavior = PillageModule ModuleTag_Pillage\n"
	"    PillageAmount = 40\n"
	"    NumDamageEventsPerPillage = 3\n"
	"  End\n"
	"End\n";

struct HashWorld : Fx
{
	FlatTerrain terrain{ 400.0f, 400.0f };
	Object *farm = nullptr, *hall = nullptr, *raider = nullptr;
	HashWorld()
		: Fx(kObjects)
	{
		economy().resources().init(terrain, 20.0f);
		farm = makeAt("Farm", "Alice", 200.0f, 200.0f);
		farm->friend_onBuildComplete();
		hall = makeAt("Hall", "Alice", 100.0f, 100.0f);
		raider = makeAt("Raider", "Bob", 50.0f, 50.0f);
		frames(2);
	}
};

#define MUTATE(NAME, WORLD, STATEMENT)                                                                                                    \
	do                                                                                                                                    \
	{                                                                                                                                     \
		HashWorld WORLD;                                                                                                                  \
		const std::uint32_t before = WORLD.hash();                                                                                        \
		STATEMENT;                                                                                                                        \
		CHECK_MESSAGE(WORLD.hash() != before, NAME);                                                                                      \
	} while (0)
} // namespace

TEST_CASE("economy hash: a second identical world has the same hash and an untouched world does not drift")
{
	HashWorld a, b;
	CHECK(a.hash() == b.hash());
	CHECK(a.hash() == a.hash());
}

TEST_CASE("economy hash: the player's money, score, command point and cost modifier state")
{
	MUTATE("money", w, w.player("Alice")->getMoney()->deposit(1));
	MUTATE("score earned", w, w.player("Alice")->getScoreKeeper().addMoneyEarned(1));
	MUTATE("score spent", w, w.player("Alice")->getScoreKeeper().addMoneySpent(1));
	MUTATE("score switch", w, w.player("Alice")->getScoreKeeper().setEnabled(false));
	MUTATE("cp base", w, w.player("Alice")->commandPoints().setFromScript(7, w.player("Alice")->commandPoints().getCap()));
	MUTATE("cp cap", w, w.player("Alice")->commandPoints().setFromScript(w.player("Alice")->commandPoints().getBase(), 7));
	MUTATE("cp usage", w, w.player("Alice")->commandPoints().addUsage(1));
	MUTATE("cp bonus", w, w.player("Alice")->commandPoints().addBonus(1));
	MUTATE("cp territory good", w, w.player("Alice")->commandPoints().setTerritoryCounters(1, 0));
	MUTATE("cp territory evil", w, w.player("Alice")->commandPoints().setTerritoryCounters(0, 1));
	MUTATE("cp record value", w, w.player("Alice")->commandPoints().addRecord(1, 1, nullptr));
	{
		HashWorld w;
		w.player("Alice")->commandPoints().addRecord(1, 1, nullptr);
		const std::uint32_t one = w.hash();
		w.player("Alice")->commandPoints().removeRecord(1, 1);
		w.player("Alice")->commandPoints().addRecord(2, 1, nullptr);
		CHECK(w.hash() != one); // the value
		w.player("Alice")->commandPoints().removeRecord(2, 1);
		w.player("Alice")->commandPoints().addRecord(1, 2, nullptr);
		CHECK(w.hash() != one); // the object id
		w.player("Alice")->commandPoints().removeRecord(1, 2);
		auto filter = std::make_shared<const ObjectFilter>();
		w.player("Alice")->commandPoints().addRecord(1, 1, filter);
		CHECK(w.hash() != one); // an unset filter against a set one
		w.player("Alice")->commandPoints().removeRecord(1, 1);
		ObjectFilter other;
		other.rule = ObjectFilter::RULE_ANY;
		const std::uint32_t withDefault = [&] {
			w.player("Alice")->commandPoints().addRecord(1, 1, filter);
			const std::uint32_t h = w.hash();
			w.player("Alice")->commandPoints().removeRecord(1, 1);
			return h;
		}();
		w.player("Alice")->commandPoints().addRecord(1, 1, std::make_shared<const ObjectFilter>(other));
		CHECK(w.hash() != withDefault); // the filter's content
	}
	MUTATE("completed upgrade", w, w.player("Alice")->addCompletedUpgrade("Upgrade_X"));
	MUTATE("defeated", w, w.player("Alice")->setDefeated(true));
	MUTATE("bounty percent", w, w.player("Alice")->setBountyPercent(0.25f));
	MUTATE("cost modifier added", w, w.player("Alice")->addCostModifier(nullptr, { -0.1f }, 5, false));
	{
		HashWorld w;
		w.player("Alice")->addCostModifier(nullptr, { -0.1f }, 5, false);
		const std::uint32_t base = w.hash();
		w.player("Alice")->removeCostModifier(nullptr, { -0.1f }, 5);
		w.player("Alice")->addCostModifier(nullptr, { -0.2f }, 5, false);
		CHECK(w.hash() != base); // a percentage
		w.player("Alice")->removeCostModifier(nullptr, { -0.2f }, 5);
		w.player("Alice")->addCostModifier(nullptr, { -0.1f }, 6, false);
		CHECK(w.hash() != base); // the source object
		w.player("Alice")->removeCostModifier(nullptr, { -0.1f }, 6);
		w.player("Alice")->addCostModifier(nullptr, { -0.1f }, 5, true);
		CHECK(w.hash() != base); // Slaughter
		w.player("Alice")->removeCostModifier(nullptr, { -0.1f }, 5);
		w.player("Alice")->addCostModifier(std::make_shared<const ObjectFilter>(), { -0.1f }, 5, false);
		CHECK(w.hash() != base); // the filter
	}
	MUTATE("upgrade discount", w, w.player("Alice")->addUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_A" }));
	{
		HashWorld w;
		w.player("Alice")->addUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_A" });
		const std::uint32_t one = w.hash();
		w.player("Alice")->addUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_A" });
		CHECK(w.hash() != one); // the count
		w.player("Alice")->removeUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_A" });
		CHECK(w.hash() == one);
		w.player("Alice")->removeUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_A" });
		w.player("Alice")->addUpgradeDiscount("Forge", { -0.1f }, { "Upgrade_B" });
		CHECK(w.hash() != one); // the upgrade names
	}
}

TEST_CASE("economy hash: object, module and claim map state")
{
	{
		// the object's flag is hashed in the "objects" section; the player's command point usage of the same change lands in "players and teams", and in the
		// chained total the two deltas can cancel (rotate-and-add, StateHasher: they did once lane HERO-2's object fields moved the rotation distance), so the
		// object's section is compared on its own
		HashWorld w;
		std::vector<GameLogic::StateHashSection> s0, s1;
		w.logic->computeStateHashBreakdown(s0, nullptr);
		w.raider->removeFromPlayerCommandPoints();
		w.logic->computeStateHashBreakdown(s1, nullptr);
		REQUIRE(s0.size() == s1.size());
		bool objects = false;
		for (size_t i = 0; i < s0.size(); ++i)
		{
			objects = objects || (s0[i].name == "objects" && s0[i].value != s1[i].value);
		}
		CHECK_MESSAGE(objects, "object counted flag");
	}
	MUTATE("object paid cost", w, w.hall->setBuildCostPaid(12.0f));
	MUTATE("terrain module share", w, dynamic_cast<TerrainResourceBehavior *>(w.farm->findModule("TerrainResourceBehavior"))->setClaimShare(0.5f));
	MUTATE("auto deposit timer", w, dynamic_cast<AutoDepositUpdate *>(w.hall->findModule("AutoDepositUpdate"))->awardInitialCaptureBonus(nullptr));
	MUTATE("pillage counter", w, dynamic_cast<PillageModule *>(w.raider->findModule("PillageModule"))->onDamageDealt(*w.hall));
	MUTATE("cost modifier upgrade executed", w, dynamic_cast<CostModifierUpgrade *>(w.hall->findModule("CostModifierUpgrade"))->giveUpgrade());
	MUTATE("command point upgrade executed", w, dynamic_cast<CommandPointsUpgrade *>(w.hall->findModule("CommandPointsUpgrade"))->giveUpgrade());
	MUTATE("claim entry", w, w.economy().resources().unclaim(*w.farm, 60.0f));
	MUTATE("claimant added", w, w.economy().resources().claim(*w.hall, 20.0f, true, false));
	{
		// the claimants' order and flags: a high priority claimant goes first, a normal one last
		HashWorld a, b;
		a.economy().resources().claim(*a.hall, 20.0f, true, false);
		b.economy().resources().claim(*b.hall, 20.0f, true, true);
		CHECK(a.hash() != b.hash());
		HashWorld c, d;
		c.economy().resources().claim(*c.hall, 20.0f, true, false);
		d.economy().resources().claim(*d.hall, 20.0f, false, false);
		CHECK(c.hash() != d.hash()); // visible
		HashWorld e, f;
		e.economy().resources().claim(*e.hall, 20.0f, true, false);
		f.economy().resources().claim(*f.hall, 40.0f, true, false);
		CHECK(e.hash() != f.hash()); // radius
		HashWorld g, h;
		g.economy().resources().claim(*g.hall, 20.0f, true, false);
		h.economy().resources().claim(*h.hall, 20.0f, true, false);
		h.economy().resources().onBuildComplete(*h.hall);
		CHECK(g.hash() != h.hash()); // complete flag and owned cells
	}
	MUTATE("grid reset", w, w.economy().resources().reset());
	{
		// the blocked cells are part of the hash through their set, not their count alone
		HashWorld a, b;
		FlatTerrain t1(400.0f, 400.0f), t2(400.0f, 400.0f);
		t1.block(0, 0, 20, 20, 1);
		t2.block(20, 0, 40, 20, 1);
		a.economy().resources().init(t1, 20.0f);
		b.economy().resources().init(t2, 20.0f);
		CHECK(a.hash() != b.hash());
		HashWorld c, d;
		FlatTerrain t3(400.0f, 380.0f);
		d.economy().resources().init(t3, 20.0f);
		CHECK(c.hash() != d.hash()); // the dimensions
		HashWorld e, f;
		f.economy().resources().init(e.terrain, 40.0f);
		CHECK(e.hash() != f.hash()); // the cell size
	}
}

TEST_CASE("economy hash: the GameData economy values and the game context")
{
	MUTATE("cell size", w, w.economy().settings().terrainResourceCellSize = 30.0f);
	MUTATE("good mp cap", w, w.economy().settings().goodMP[3].cap = 1);
	MUTATE("evil mp start", w, w.economy().settings().evilMP[0].start = 1);
	MUTATE("solo pair", w, w.economy().settings().goodSolo.cap = 1);
	MUTATE("ai pair", w, w.economy().settings().evilAI.start = 1);
	MUTATE("bonus", w, w.economy().settings().goodBonus = 1);
	MUTATE("limit", w, w.economy().settings().evilLimit = 1);
	MUTATE("power limit", w, w.economy().settings().powerLimit = 1);
	MUTATE("resource limit", w, w.economy().settings().resourceMultiplierLimit = 1.0f);
	MUTATE("resource bonus", w, w.economy().settings().resourceBonusMultiplier = 1.0f);
	MUTATE("money mult", w, w.economy().settings().multiPlayMoneyMult[7] = 2.0f);
	MUTATE("loaded", w, w.economy().settings().loaded = false);
	MUTATE("game mode", w, w.economy().context().gameMode = EconomyContext::MODE_LAN);
	MUTATE("game kind", w, w.economy().context().gameKind = 1);
	MUTATE("living world", w, w.economy().context().livingWorld = true);
	MUTATE("game info", w, w.economy().context().gameInfoPresent = false);
	MUTATE("lobby percent", w, w.economy().context().lobbyCommandPointPercent = 50);
	MUTATE("scoring", w, w.economy().setScoring(false));
}
