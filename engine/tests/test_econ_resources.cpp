// OpenBFME unit tests: the terrain resource claim map, TerrainResourceBehavior income, AutoDepositUpdate, RefundDie and PillageModule (lane ECON-1).
//
// Every expected number is derived by hand from the retail rules (RW 0x75B390 .. 0x75C7AE the manager, 0x8854D3 the income update, 0x89DB5D AutoDepositUpdate,
// 0x888519 RefundDie, 0x88826E PillageModule) and from the geometry of the disc the manager visits (RW 0x75B4BA): a radius of r cells visits 1, 5, 21, 37, 65 cells for
// r = 0 .. 4 (the rows of the retail loop traced by hand in the comments below), the cells of the farm in a corner are counted by rows, and so on.

#include "doctest.h"
#include "EconTestUtil.h"

#include "Common/PlayerList.h"
#include "GameLogic/Module/AutoDepositUpdate.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

using namespace logictest;
using namespace econtest;

namespace
{
const char kTemplates[] =
	"PlayerTemplate FactionC\n"
	"  Side = Gamma\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 1000\n"
	"  StartingBuilding = GammaKeep\n"
	"  ResourceModifierObjectFilter = ANY +SmallFarm\n"
	"  ResourceModifierValues = 100 100 50 25\n"
	"End\n";

const char kObjects[] =
	// Radius 60 / 20 = 3 cells: the disc of 37 cells
	"Object Farm\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
	"    Radius = 60\n"
	"    MaxIncome = 100\n"
	"    IncomeInterval = 15000\n"
	"  End\n"
	"End\n"
	"Object Keep\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_DeadSpot\n"
	"    Radius = 60\n"
	"    MaxIncome = 0\n"
	"    IncomeInterval = 999999\n"
	"    HighPriority = Yes\n"
	"    Visible = No\n"
	"  End\n"
	"End\n"
	"Object SmallFarm\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
	"    Radius = 20\n"
	"    MaxIncome = 100\n"
	"    IncomeInterval = 15000\n"
	"  End\n"
	"End\n"
	"Object GrandFarm\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
	"    Radius = 20\n"
	"    MaxIncome = 100\n"
	"    IncomeInterval = 15000\n"
	"    Upgrade = Upgrade_Harvest\n"
	"    UpgradeBonusPercent = 150%\n"
	"    UpgradeMustBePresent = ANY +Market\n"
	"  End\n"
	"End\n"
	"Object Market\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"End\n"
	"Object Hall\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = AutoDepositUpdate ModuleTag_Deposit\n"
	"    DepositTiming = 6000\n"
	"    DepositAmount = 25\n"
	"    InitialCaptureBonus = 100\n"
	"  End\n"
	"End\n"
	"Object GarrisonHall\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = AutoDepositUpdate ModuleTag_Deposit\n"
	"    DepositTiming = 6000\n"
	"    DepositAmount = 25\n"
	"    OnlyWhenGarrisoned = Yes\n"
	"    GiveNoXP = Yes\n"
	"  End\n"
	"End\n"
	"Object RichHall\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = AutoDepositUpdate ModuleTag_Deposit\n"
	"    DepositTiming = 6000\n"
	"    DepositAmount = 20\n"
	"    Upgrade = Upgrade_Harvest\n"
	"    UpgradeBonusPercent = 150%\n"
	"    UpgradeMustBePresent = ANY +Market\n"
	"  End\n"
	"End\n"
	"Object Refunder\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = RefundDie ModuleTag_Refund\n"
	"    RefundPercent = 50%\n"
	"  End\n"
	"End\n"
	"Object PickyRefunder\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = RefundDie ModuleTag_Refund\n"
	"    RefundPercent = 100%\n"
	"    DeathTypes = NONE +BURNED\n"
	"    UpgradeRequired = Upgrade_Insurance\n"
	"    BuildingRequired = ANY +Market\n"
	"    DamageAmountRequired = 10\n"
	"  End\n"
	"End\n"
	"Object Raider\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = PillageModule ModuleTag_Pillage\n"
	"    PillageAmount = 40\n"
	"    NumDamageEventsPerPillage = 3\n"
	"    PillageFilter = ANY +STRUCTURE\n"
	"  End\n"
	"End\n"
	"Object Victim\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"End\n";

std::uint32_t statusBit(const char *name)
{
	const int b = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	REQUIRE(b >= 0);
	return (std::uint32_t)b;
}
ObjectStatusMaskType mask(const char *name)
{
	ObjectStatusMaskType m{};
	MaskSet(m, statusBit(name), true);
	return m;
}

TerrainResourceBehavior *resourceModule(Object *o)
{
	return dynamic_cast<TerrainResourceBehavior *>(o->findModule("TerrainResourceBehavior"));
}

// the number of OWNED cells of the manager and of cells that hold an entry of `objectId`
int ownedBy(const TerrainResourceManager &m, ObjectID objectId)
{
	int n = 0;
	for (int y = 0; y < m.height(); ++y)
	{
		for (int x = 0; x < m.width(); ++x)
		{
			const TerrainResourceManager::Cell *c = m.cellAt(x, y);
			if (c->state == TerrainResourceManager::CELL_OWNED && !c->claims.empty() && c->claims.front().objectId == objectId)
			{
				++n;
			}
		}
	}
	return n;
}
int sharedBy(const TerrainResourceManager &m, ObjectID objectId)
{
	int n = 0;
	for (int y = 0; y < m.height(); ++y)
	{
		for (int x = 0; x < m.width(); ++x)
		{
			const TerrainResourceManager::Cell *c = m.cellAt(x, y);
			if (c->state == TerrainResourceManager::CELL_SHARED)
			{
				for (const TerrainResourceManager::ClaimEntry &e : c->claims)
				{
					n += e.objectId == objectId ? 1 : 0;
				}
			}
		}
	}
	return n;
}
float share(Object *o)
{
	return resourceModule(o)->claimShare();
}
float fraction(int claimed, int total)
{
	return (float)((double)claimed / (double)total);
}

struct World : Fx
{
	FlatTerrain terrain{ 400.0f, 400.0f };
	World()
		: Fx(kObjects, kTemplates)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Alice", "FactionC", true, 0, 0, 0 });
		setup.players.push_back({ "Bob", "FactionA", false, 1, 0, 1 });
		setup.defaultStartingCash = 2000;
		REQUIRE(players.setupSkirmish(setup).empty());
		economy().initAllCommandPoints();
		economy().resources().init(terrain, 20.0f);
	}
	// a complete resource building at the centre of cell (cx, cy)
	Object *placeComplete(const char *tmpl, const char *owner, int cx, int cy)
	{
		Object *o = makeAt(tmpl, owner, (float)cx * 20.0f, (float)cy * 20.0f);
		o->friend_onBuildComplete();
		return o;
	}
	// a building under construction: its module claims on its first update and sleeps (RW 0x88550B)
	Object *placeUnderConstruction(const char *tmpl, const char *owner, int cx, int cy)
	{
		Object *o = logic->newObject(w.get(tmpl), teamOf(owner), mask("UNDER_CONSTRUCTION"));
		Coord3D p{ (float)cx * 20.0f, (float)cy * 20.0f, 0.0f };
		o->setPosition(&p);
		return o;
	}
};
} // namespace

TEST_CASE("terrain resources: the grid is ceil(extent / cell) cells per side, at least one, and the origin is the low corner (RW 0x75BE66)")
{
	World w;
	TerrainResourceManager &m = w.economy().resources();
	CHECK(m.width() == 20);
	CHECK(m.height() == 20);
	CHECK(m.cellSize() == 20.0f);
	CHECK(m.originX() == 0.0f);
	CHECK(m.largestRadius() == 0.0f);
	FlatTerrain odd(401.0f, 399.9f);
	m.init(odd, 20.0f);
	CHECK(m.width() == 21); // 401 * (1 / 20) = 20.05 -> 21
	CHECK(m.height() == 20);
	FlatTerrain tiny(0.5f, 0.25f);
	m.init(tiny, 20.0f); // a region narrower than 1.0 is widened to 1.0 first
	CHECK(m.width() == 1);
	CHECK(m.height() == 1);
	FlatTerrain wide(1000.0f, 20.0f);
	m.init(wide, 40.0f);
	CHECK(m.width() == 25);
	CHECK(m.height() == 1); // 20 * (1 / 40) = 0.5 -> 1
	CHECK_THROWS_AS(m.init(odd, 0.0f), std::logic_error);
}

TEST_CASE("terrain resources: the disc of a complete claim has 1, 5, 21, 37, 65 cells for radius 0 .. 4 (RW 0x75B4BA traced by hand)")
{
	// r = 1: rows cy+1 and cy-1 (x only), the middle row three cells: a plus. r = 3 (esi = 2 - 2r = -4): the first pass widens without a row, then rows
	// +-3 (3 cells), +-2 (5), +-1 (7) and the middle row (7): 6 + 10 + 14 + 7 = 37. r = 4: rows +-4 (3), +-3 (7), +-2 (9), +-1 (9), 0 (9): 6 + 14 + 18 + 18 + 9 = 65.
	const struct
	{
		float radius;
		int cells;
	} cases[] = { { 0.0f, 1 }, { 20.0f, 5 }, { 40.0f, 21 }, { 60.0f, 37 }, { 80.0f, 65 }, { 50.0f, 37 }, { 61.0f, 65 } };
	for (const auto &c : cases)
	{
		World w;
		Object *f = w.makeAt("SmallFarm", "Alice", 205.0f, 205.0f); // floor(205 / 20 + 0.5) = 10: cell (10, 10)
		const TerrainResourceBehaviorModuleData *data = resourceModule(f)->data();
		TerrainResourceManager &m = w.economy().resources();
		m.claim(*f, c.radius, data->m_visible, false);
		m.onBuildComplete(*f);
		CHECK_MESSAGE(ownedBy(m, f->getID()) == c.cells, "radius " << c.radius);
		CHECK_MESSAGE(share(f) == 1.0f, "radius " << c.radius);
	}
}

TEST_CASE("terrain resources: a building in a corner holds the cells that exist, the share is claimed / total (x87 fidiv at 24 bits)")
{
	World w;
	Object *f = w.placeComplete("Farm", "Alice", 0, 0); // r = 3 around cell (0, 0): rows 0..3 of widths 4, 4, 3, 2 inside the grid = 13 of 37
	TerrainResourceManager &m = w.economy().resources();
	CHECK(ownedBy(m, f->getID()) == 13);
	CHECK(share(f) == fraction(13, 37));
	// the far corner: cell (19, 19): the same count
	Object *g = w.placeComplete("Farm", "Bob", 19, 19);
	CHECK(ownedBy(m, g->getID()) == 13);
	CHECK(share(g) == fraction(13, 37));
	CHECK(m.largestRadius() == 60.0f);
}

TEST_CASE("terrain resources: water, cliffs and the other blocked cell types stay out of every claim")
{
	World w;
	// cells 12 and 13 of rows 9 .. 11 are water (their centres (250, 270) x (190, 210, 230)): 6 blocked cells inside the disc of cell (10, 10)
	w.terrain.block(240.0f, 180.0f, 280.0f, 240.0f, 1);
	// a cliff (2), a type 5 and deep water (7) block too; the obstacle type 4 and rubble 3 do not
	w.terrain.block(0.0f, 0.0f, 20.0f, 20.0f, 2);
	w.terrain.block(20.0f, 0.0f, 40.0f, 20.0f, 5);
	w.terrain.block(40.0f, 0.0f, 60.0f, 20.0f, 7);
	w.terrain.block(60.0f, 0.0f, 80.0f, 20.0f, 4);
	w.terrain.block(80.0f, 0.0f, 100.0f, 20.0f, 3);
	TerrainResourceManager &m = w.economy().resources();
	m.init(w.terrain, 20.0f);
	CHECK(m.blockedCells() == 6 + 3);
	CHECK(m.cellAt(12, 10)->state == TerrainResourceManager::CELL_BLOCKED);
	CHECK(m.cellAt(0, 0)->state == TerrainResourceManager::CELL_BLOCKED);
	CHECK(m.cellAt(3, 0)->state == TerrainResourceManager::CELL_FREE);
	CHECK(m.cellAt(4, 0)->state == TerrainResourceManager::CELL_FREE);
	Object *f = w.placeComplete("Farm", "Alice", 10, 10);
	CHECK(ownedBy(m, f->getID()) == 31);
	CHECK(share(f) == fraction(31, 37));
}

TEST_CASE("terrain resources: provisional claims are shared per player, the first building to complete owns the overlap, the other keeps the rest")
{
	World w;
	TerrainResourceManager &m = w.economy().resources();
	// two buildings of two players under construction, three cells apart: the discs of 37 cells overlap in 16 (rows +-2: 2 each, rows +-1: 4 each, the middle row 4)
	Object *a = w.placeUnderConstruction("Farm", "Alice", 10, 10);
	Object *b = w.placeUnderConstruction("Farm", "Bob", 13, 10);
	w.frames(1); // the first update of each module: claim, then sleep until it is complete
	REQUIRE(resourceModule(a)->claimed());
	REQUIRE(resourceModule(b)->claimed());
	CHECK(sharedBy(m, a->getID()) == 37);
	CHECK(sharedBy(m, b->getID()) == 37);
	CHECK(share(a) == 1.0f); // a provisional claim counts the shared cells
	CHECK(share(b) == 1.0f);
	CHECK(m.cellAt(11, 10)->claims.size() == 2); // overlap cell: one entry per player
	CHECK(m.cellAt(8, 10)->claims.size() == 1);
	// Alice's building completes: its 37 cells become owned (a shared cell goes to a flag-1 claimant)
	a->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	a->friend_onBuildComplete();
	CHECK(ownedBy(m, a->getID()) == 37);
	CHECK(share(a) == 1.0f);
	CHECK(m.cellAt(11, 10)->claims.size() == 1);
	// Bob's completes: the 16 cells Alice owns are out of reach
	b->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	b->friend_onBuildComplete();
	CHECK(ownedBy(m, b->getID()) == 21);
	CHECK(share(b) == fraction(21, 37));
	CHECK(ownedBy(m, a->getID()) == 37);
	// Alice's building dies: its ground is released and Bob's building, applied again with its own flag, takes the 16 cells
	a->friend_onDie(DieModuleInterface::Event());
	CHECK(share(a) == 0.0f);
	CHECK(m.claimants().size() == 1);
	CHECK(ownedBy(m, a->getID()) == 0);
	CHECK(ownedBy(m, b->getID()) == 37);
	CHECK(share(b) == 1.0f);
}

TEST_CASE("terrain resources: a player's second provisional building does not count the cells that player already shares")
{
	World w;
	TerrainResourceManager &m = w.economy().resources();
	Object *a = w.placeUnderConstruction("Farm", "Alice", 10, 10);
	Object *b = w.placeUnderConstruction("Farm", "Alice", 13, 10);
	w.frames(1);
	CHECK(share(a) == 1.0f);
	CHECK(share(b) == fraction(21, 37)); // 37 - 16: the player has an entry in those cells already (RW 0x75C6C7)
	CHECK(sharedBy(m, b->getID()) == 21);
}

TEST_CASE("terrain resources: a high priority claimant (a dead spot) takes ground first and the building next to it loses the overlap")
{
	World w;
	TerrainResourceManager &m = w.economy().resources();
	Object *keep = w.placeComplete("Keep", "Alice", 10, 10);
	CHECK(m.claimants().size() == 1);
	CHECK(m.claimants().front().id == keep->getID()); // high priority goes to the front
	CHECK(ownedBy(m, keep->getID()) == 37);
	// a farm of the other player under construction next to it: the keep's owned cells (flag 0 cannot use them) cost the farm 16 of its 37
	Object *farm = w.placeUnderConstruction("Farm", "Bob", 13, 10);
	w.frames(1);
	CHECK(sharedBy(m, farm->getID()) == 21);
	CHECK(share(farm) == fraction(21, 37));
	farm->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	farm->friend_onBuildComplete();
	CHECK(ownedBy(m, farm->getID()) == 21);
	CHECK(ownedBy(m, keep->getID()) == 37);
	CHECK(share(farm) == fraction(21, 37));
	CHECK(m.claimants().front().id == keep->getID()); // a normal claimant joined at the back
	CHECK(m.claimants().back().id == farm->getID());
	// a second dead spot claimed AFTER the farm completed: the farm's ground stays the farm's, the new keep gets what is free and the neighbours are re-applied
	Object *keep2 = w.placeComplete("Keep", "Bob", 16, 10); // 3 cells from the farm on the far side
	CHECK(m.claimants().front().id == keep2->getID());      // in front of the first one
	CHECK(ownedBy(m, farm->getID()) == 21);
	CHECK(share(farm) == fraction(21, 37));
	// keep2's disc shares 16 cells with the farm's, 13 of which the farm owns and 3 the first keep owns (the column between them): keep2 gets 37 - 13 - 3 = 21
	CHECK(ownedBy(m, keep2->getID()) == 21);
}

TEST_CASE("terrain resources: claimants made before the grid exists are applied by init (RW 0x75BFE5), a negative radius is reported, not looped on")
{
	World w;
	TerrainResourceManager &m = w.economy().resources();
	m.reset();
	CHECK_FALSE(m.hasGrid());
	Object *f = w.placeComplete("Farm", "Alice", 10, 10);
	CHECK(m.claimants().size() == 1);
	CHECK(share(f) == 0.0f); // no grid, no share
	CHECK(m.largestRadius() == 60.0f);
	m.init(w.terrain, 20.0f);
	CHECK(ownedBy(m, f->getID()) == 37);
	CHECK(share(f) == 1.0f);
	// a data error in a mod: Radius = -50 would make the retail loop run forever
	const std::string err = w.w.load(
		"Object BadFarm\n"
		"  KindOf = SELECTABLE STRUCTURE\n"
		"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
		"    Radius = -50\n"
		"    MaxIncome = 100\n"
		"    IncomeInterval = 15000\n"
		"  End\n"
		"End\n",
		INI_LOAD_OVERWRITE, "bad.ini");
	REQUIRE(err.empty());
	Object *bad = w.makeAt("BadFarm", "Alice", 100.0f, 100.0f);
	bad->friend_onBuildComplete();
	CHECK(share(bad) == 0.0f);
	bool reported = false;
	for (const std::string &e : w.logic->report().errors)
	{
		reported = reported || e.find("negative Radius") != std::string::npos;
	}
	CHECK(reported);
}

TEST_CASE("terrain resource income: the first payment comes at the first update, then every IncomeInterval frames (RW 0x8854D3)")
{
	World w;
	Object *f = w.placeComplete("Farm", "Alice", 10, 10);
	Player *alice = w.player("Alice");
	CHECK(alice->getMoney()->countMoney() == 1000);
	// IncomeInterval = 15000 ms = 75 frames (parseDurationUnsignedInt: ceil(15000 * 0.005)); the module is due at frame 1
	CHECK(resourceModule(f)->data()->m_incomeInterval == 75);
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1100);
	w.frames(74); // frame 75
	CHECK(alice->getMoney()->countMoney() == 1100);
	w.frames(1); // frame 76
	CHECK(alice->getMoney()->countMoney() == 1200);
	w.frames(75); // frame 151
	CHECK(alice->getMoney()->countMoney() == 1300);
	CHECK(alice->getScoreKeeper().moneyEarned() == 300);
	// the events the overlay and the floating text read: three payments of 100 at frames 1, 76, 151
	const std::deque<Economy::IncomeEvent> &log = w.economy().incomeLog();
	REQUIRE(log.size() == 3);
	CHECK(log[0].frame == 1);
	CHECK(log[1].frame == 76);
	CHECK(log[2].frame == 151);
	CHECK(log[0].amount == 100);
	CHECK(log[0].object == f->getID());
	CHECK(log[0].playerIndex == alice->getPlayerIndex());
	// five logic frames a second: one payment per 15 s, so 4 a minute (300 frames): at frame 300 the payments of frames 1, 76, 151 and 226 are in, the fifth is due at 301
	w.frames(300 - 151);
	CHECK(alice->getMoney()->countMoney() == 1000 + 100 * 4);
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1000 + 100 * 5);
}

TEST_CASE("terrain resource income: a building under construction claims and sleeps, completing wakes it (RW 0x88550B, 0x8853A0)")
{
	World w;
	Object *f = w.placeUnderConstruction("Farm", "Alice", 10, 10);
	Player *alice = w.player("Alice");
	w.frames(10);
	CHECK(alice->getMoney()->countMoney() == 1000);
	CHECK(resourceModule(f)->claimed());
	CHECK(resourceModule(f)->needsBuildComplete());
	// the sleeping module is in the scheduler's sleeping vector (FOREVER)
	bool sleeping = false;
	for (const UpdateModule *u : w.logic->sleepingVector())
	{
		sleeping = sleeping || u == static_cast<UpdateModule *>(resourceModule(f));
	}
	CHECK(sleeping);
	f->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	f->friend_onBuildComplete(); // frame 10: due at frame 11
	CHECK_FALSE(resourceModule(f)->needsBuildComplete());
	CHECK(share(f) == 1.0f);
	CHECK(alice->getMoney()->countMoney() == 1000);
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1100);
}

TEST_CASE("terrain resource income: the amount is ceil(MaxIncome * share), at 24 bits (the retail x87 chain)")
{
	World w;
	// share 13 / 37 = 0.351351...: 100 * 0.35135135 = 35.135 -> 36; 21 / 37 -> 56.76 -> 57; 31 / 37 -> 83.78 -> 84
	Object *corner = w.placeComplete("Farm", "Alice", 0, 0);
	Player *alice = w.player("Alice");
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1000 + 36);
	Object *a = w.placeComplete("Farm", "Bob", 10, 10);
	Object *b = w.placeComplete("Farm", "Bob", 13, 10);
	(void)a;
	CHECK(share(b) == fraction(21, 37));
	const std::uint32_t before = w.player("Bob")->getMoney()->countMoney();
	w.frames(1);
	CHECK(w.player("Bob")->getMoney()->countMoney() == before + 100 + 57); // a: 100 (it holds all 37), b: 57
	(void)corner;
	// a share of exactly 1 and a MaxIncome of 0 (the dead spot) pay nothing
	Object *keep = w.placeComplete("Keep", "Alice", 4, 15);
	const std::uint32_t cash = alice->getMoney()->countMoney();
	w.frames(200);
	CHECK(alice->getMoney()->countMoney() > cash); // the corner farm keeps paying
	CHECK(w.economy().incomeLog().back().object != keep->getID());
	for (const Economy::IncomeEvent &e : w.economy().incomeLog())
	{
		CHECK(e.object != keep->getID());
	}
}

TEST_CASE("terrain resource income: the upgrade bonus needs the upgrade and a completed building the filter allows (RW 0x885549)")
{
	World w;
	Object *f = w.placeComplete("GrandFarm", "Alice", 10, 10);
	Player *alice = w.player("Alice");
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1100); // no upgrade
	alice->addCompletedUpgrade("Upgrade_Harvest");
	w.frames(75);
	CHECK(alice->getMoney()->countMoney() == 1200); // the upgrade without a Market
	Object *market = w.logic->newObject(w.w.get("Market"), w.teamOf("Alice"), mask("UNDER_CONSTRUCTION"));
	w.frames(75);
	CHECK(alice->getMoney()->countMoney() == 1300); // a Market under construction does not count (completedOnly)
	market->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	w.frames(75);
	CHECK(alice->getMoney()->countMoney() == 1450); // ceil(100 * 1.5) = 150
	(void)f;
	// the bonus is the owner's: Bob has the upgrade and a Market too
	w.player("Bob")->addCompletedUpgrade("Upgrade_Harvest");
	w.logic->newObject(w.w.get("Market"), w.teamOf("Bob"), ObjectStatusMaskType{});
	Object *g = w.placeComplete("GrandFarm", "Bob", 2, 2);
	(void)g;
	const std::uint32_t bobBefore = w.player("Bob")->getMoney()->countMoney();
	w.frames(1);
	CHECK(w.player("Bob")->getMoney()->countMoney() == bobBefore + 150);
}

TEST_CASE("terrain resource income: the PRODUCTION attribute modifier multiplies the amount, the owner's resource list divides it among many buildings (RW 0x885591, 0x885650)")
{
	World w;
	Player *alice = w.player("Alice");
	// PRODUCTION modifier 1.5 on every object (an upgrade lane's provider): ceil(100 * 1 * 1.5 * 1) = 150
	w.economy().setAttributeModifierProvider([](const Object &, int type, float &product) {
		if (type == Economy::ATTRIBUTE_MODIFIER_PRODUCTION)
		{
			product = product * 1.5f;
			return true;
		}
		return false;
	});
	Object *grand = w.placeComplete("SmallFarm", "Bob", 2, 2); // Bob's template has no resource list
	(void)grand;
	const std::uint32_t bob0 = w.player("Bob")->getMoney()->countMoney();
	w.frames(1);
	CHECK(w.player("Bob")->getMoney()->countMoney() == bob0 + 150);
	w.economy().setAttributeModifierProvider(nullptr);
	// Alice's template (ResourceModifierValues = 100 100 50 25 over SmallFarm): with n farms in the world every farm pays by values[n]:
	// n = 1 -> 100%, 2 -> 50%, 3 -> 25%, 4 -> back * 0.01 = 25%, 5 -> 0.25 - 0.02 = 23%, 17 -> 0.25 - 13 * 0.02 < 0 -> clamped at 0 -> the minimum of 1
	const struct
	{
		int n, each;
	} expected[] = { { 1, 100 }, { 2, 50 }, { 3, 25 }, { 4, 25 }, { 5, 23 }, { 17, 1 } };
	for (const auto &e : expected)
	{
		const int n = e.n;
		World wn;
		for (int i = 0; i < n; ++i)
		{
			wn.placeComplete("SmallFarm", "Alice", 1 + (i % 6) * 3, 1 + (i / 6) * 3); // 3 cells apart: the discs of radius 1 never overlap
		}
		const std::uint32_t before = wn.player("Alice")->getMoney()->countMoney();
		wn.frames(1);
		const std::uint32_t paid = wn.player("Alice")->getMoney()->countMoney() - before;
		CHECK_MESSAGE(paid == (std::uint32_t)(n * e.each), n << " farms paid " << paid);
	}
	(void)alice;
}

TEST_CASE("terrain resource income: a farm under construction is not counted in the owner's farm number (RW 0x885230)")
{
	World w;
	w.placeUnderConstruction("SmallFarm", "Alice", 1, 1);
	w.placeUnderConstruction("SmallFarm", "Alice", 4, 1);
	w.placeUnderConstruction("SmallFarm", "Alice", 7, 1);
	Object *real = w.placeComplete("SmallFarm", "Alice", 10, 1);
	const std::uint32_t before = w.player("Alice")->getMoney()->countMoney();
	w.frames(1);
	CHECK(w.player("Alice")->getMoney()->countMoney() == before + 100); // n = 1 -> 100%
	(void)real;
}

TEST_CASE("auto deposit: DepositAmount every DepositTiming frames from creation, nothing for the neutral player, under construction or in rubble (RW 0x89DB5D)")
{
	World w;
	Player *alice = w.player("Alice");
	// the money multiplier of a skirmish with two players: MP2 (index 1); retail data is 1.0
	Object *hall = w.makeAt("Hall", "Alice", 100.0f, 100.0f);
	std::vector<std::pair<Object *, float>> xp;
	w.economy().setExperienceHook([&](Object &o, float points) { xp.push_back({ &o, points }); });
	w.frames(29);
	CHECK(alice->getMoney()->countMoney() == 1000); // DepositTiming 6000 ms = 30 frames: due at frame 30
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1025);
	w.frames(29);
	CHECK(alice->getMoney()->countMoney() == 1025);
	w.frames(1);
	CHECK(alice->getMoney()->countMoney() == 1050);
	REQUIRE(xp.size() == 2);
	CHECK(xp[0].first == hall);
	CHECK(xp[0].second == 25.0f); // DepositAmount * bonus, before the multiplayer money multiplier
	// the multiplayer money multiplier: 25 * 1.5 = 37.5 -> 37 (cvttss2si)
	w.economy().settings().multiPlayMoneyMult[1] = 1.5f;
	w.frames(30);
	CHECK(alice->getMoney()->countMoney() == 1050 + 37);
	// a single player game has no multiplier
	w.economy().context().gameMode = EconomyContext::MODE_SINGLE_PLAYER;
	w.frames(30);
	CHECK(alice->getMoney()->countMoney() == 1050 + 37 + 25);
	w.economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	w.economy().settings().multiPlayMoneyMult[1] = 1.0f;
	// under construction: nothing (the construction percent is not -1.0)
	hall->setStatus(statusBit("UNDER_CONSTRUCTION"), true);
	const std::uint32_t c0 = alice->getMoney()->countMoney();
	w.frames(60);
	CHECK(alice->getMoney()->countMoney() == c0);
	hall->setStatus(statusBit("UNDER_CONSTRUCTION"), false);
	// rubble, post rubble, post collapse (model conditions): nothing
	std::string condition;
	w.economy().setModelConditionProvider([&](const Object &, const char *c) { return condition == c; });
	for (const char *c : { "RUBBLE", "POST_RUBBLE", "POST_COLLAPSE" })
	{
		condition = c;
		const std::uint32_t before = alice->getMoney()->countMoney();
		w.frames(60);
		CHECK_MESSAGE(alice->getMoney()->countMoney() == before, c);
	}
	condition.clear();
	const std::uint32_t before = alice->getMoney()->countMoney();
	w.frames(30);
	CHECK(alice->getMoney()->countMoney() == before + 25);
	// the neutral player's building pays nobody
	Object *neutral = w.logic->newObject(w.w.get("Hall"), w.players.getNeutralPlayer()->getDefaultTeam(), ObjectStatusMaskType{});
	(void)neutral;
	const std::uint32_t b0 = alice->getMoney()->countMoney();
	w.frames(90);
	CHECK(alice->getMoney()->countMoney() == b0 + 3 * 25); // only Alice's own hall
	CHECK(w.player("Bob")->getMoney()->countMoney() == 1500);
}

TEST_CASE("auto deposit: OnlyWhenGarrisoned, the upgrade bonus (completedOnly false), the initial capture bonus once (RW 0x89DA1E)")
{
	World w;
	Player *alice = w.player("Alice");
	Player *bob = w.player("Bob");
	Object *gh = w.makeAt("GarrisonHall", "Alice", 100.0f, 100.0f);
	bool garrisoned = false;
	w.economy().setModelConditionProvider([&](const Object &, const char *c) { return garrisoned && std::string(c) == "GARRISONED"; });
	w.frames(60);
	CHECK(alice->getMoney()->countMoney() == 1000);
	garrisoned = true;
	w.frames(30);
	CHECK(alice->getMoney()->countMoney() == 1025);
	(void)gh;
	// RichHall: 20 * 1.5 = 30 with the upgrade and any Market of the owner, even an unfinished one
	Object *rich = w.makeAt("RichHall", "Bob", 120.0f, 120.0f);
	(void)rich;
	w.frames(30);
	CHECK(bob->getMoney()->countMoney() == 1500 + 20);
	bob->addCompletedUpgrade("Upgrade_Harvest");
	w.frames(30);
	CHECK(bob->getMoney()->countMoney() == 1500 + 20 + 20); // the upgrade alone is not enough
	w.logic->newObject(w.w.get("Market"), w.teamOf("Bob"), mask("UNDER_CONSTRUCTION"));
	w.frames(30);
	CHECK(bob->getMoney()->countMoney() == 1500 + 40 + 30);
	// the initial capture bonus: once, to the first player that captures the building after it started
	Object *hall = w.makeAt("Hall", "Alice", 140.0f, 140.0f);
	AutoDepositUpdate *ad = dynamic_cast<AutoDepositUpdate *>(hall->findModule("AutoDepositUpdate"));
	REQUIRE(ad);
	CHECK_FALSE(ad->initialBonusPending());
	w.frames(1);
	CHECK_FALSE(ad->initialBonusPending()); // the flag is raised when the first deposit time is reached, not by the first update
	w.frames(29); // frame 210: the first deposit
	CHECK(ad->initialBonusPending());
	const std::uint32_t b1 = bob->getMoney()->countMoney();
	hall->setTeam(w.teamOf("Bob")); // captured: 100 to Bob (MP money multiplier 1.0, no handicap in a skirmish)
	CHECK(bob->getMoney()->countMoney() == b1 + 100);
	CHECK_FALSE(ad->initialBonusPending());
	const std::uint32_t a1 = alice->getMoney()->countMoney();
	hall->setTeam(w.teamOf("Alice"));
	CHECK(alice->getMoney()->countMoney() == a1); // no bonus the second time
	// capturing resets the deposit timer to now + DepositTiming
	CHECK(ad->depositOnFrame() == w.logic->getFrame() + 30);
}

TEST_CASE("refund die: ceil(price paid * RefundPercent) to the owner, with the die mux, upgrade and building conditions (RW 0x888519)")
{
	World w;
	Player *alice = w.player("Alice");
	Object *r = w.makeAt("Refunder", "Alice", 50.0f, 50.0f);
	r->setBuildCostPaid(301.0f);
	const std::uint32_t before = alice->getMoney()->countMoney();
	r->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == before + 151); // 301 * 0.5 = 150.5 -> 151
	CHECK(alice->getScoreKeeper().moneyEarned() == 151);
	// nothing paid: nothing refunded
	Object *free = w.makeAt("Refunder", "Alice", 60.0f, 50.0f);
	const std::uint32_t b2 = alice->getMoney()->countMoney();
	free->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == b2);
	// under construction and sold buildings refund nothing here
	Object *building = w.logic->newObject(w.w.get("Refunder"), w.teamOf("Alice"), mask("UNDER_CONSTRUCTION"));
	building->setBuildCostPaid(100.0f);
	building->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == b2);
	Object *sold = w.makeAt("Refunder", "Alice", 70.0f, 50.0f);
	sold->setBuildCostPaid(100.0f);
	sold->setStatus(statusBit("SOLD"), true);
	sold->friend_onDie(DieModuleInterface::Event());
	CHECK(alice->getMoney()->countMoney() == b2);
	// the picky one: DeathTypes = BURNED only, an upgrade, a Market, damage >= 10
	Object *p = w.makeAt("PickyRefunder", "Alice", 80.0f, 50.0f);
	p->setBuildCostPaid(80.0f);
	DieModuleInterface::Event e;
	e.deathType = 3; // BURNED (RW 0xDA1630: NORMAL 0, NONE 1, CRUSHED 2, BURNED 3): the mask bit is type - 1 = 2
	e.damageAmount = 50.0f;
	RefundDie *rd = dynamic_cast<RefundDie *>(p->findModule("RefundDie"));
	REQUIRE(rd);
	CHECK(rd->refundAmount(e) == 0); // no upgrade
	alice->addCompletedUpgrade("Upgrade_Insurance");
	CHECK(rd->refundAmount(e) == 0); // no Market
	Object *market = w.makeAt("Market", "Alice", 90.0f, 50.0f);
	CHECK(rd->refundAmount(e) == 80);
	e.damageAmount = 9.0f;
	CHECK(rd->refundAmount(e) == 0); // DamageAmountRequired 10
	e.damageAmount = 10.0f;
	CHECK(rd->refundAmount(e) == 80);
	e.deathType = 2; // CRUSHED: bit 1, not in the mask
	CHECK(rd->refundAmount(e) == 0);
	e.deathType = 3;
	(void)market;
	CHECK(rd->refundAmount(e) == 80);
}

TEST_CASE("pillage: every Nth hit on a matching victim moves cash from the victim's owner to the pillager's owner (RW 0x88826E)")
{
	World w;
	Player *alice = w.player("Alice");
	Player *bob = w.player("Bob");
	Object *raider = w.makeAt("Raider", "Alice", 50.0f, 50.0f);
	Object *tower = w.makeAt("Market", "Bob", 60.0f, 50.0f);   // a structure: matches ANY +STRUCTURE
	Object *man = w.makeAt("Victim", "Bob", 70.0f, 50.0f);     // infantry: does not
	PillageModule *pm = dynamic_cast<PillageModule *>(raider->findModule("PillageModule"));
	REQUIRE(pm);
	for (int i = 0; i < 5; ++i)
	{
		pm->onDamageDealt(*man);
	}
	CHECK(pm->counter() == 0);
	CHECK(alice->getMoney()->countMoney() == 1000);
	pm->onDamageDealt(*tower);
	pm->onDamageDealt(*tower);
	CHECK(pm->counter() == 2);
	CHECK(bob->getMoney()->countMoney() == 1500);
	pm->onDamageDealt(*tower); // the third: 40 moves
	CHECK(pm->counter() == 0);
	CHECK(alice->getMoney()->countMoney() == 1040);
	CHECK(bob->getMoney()->countMoney() == 1460);
	CHECK(alice->getScoreKeeper().moneyEarned() == 40);
	CHECK(bob->getScoreKeeper().moneySpent() == 40);
	// the victim has 25 left: min(40, 25)
	bob->getMoney()->withdraw(1460 - 25);
	for (int i = 0; i < 3; ++i)
	{
		pm->onDamageDealt(*tower);
	}
	CHECK(alice->getMoney()->countMoney() == 1065);
	CHECK(bob->getMoney()->countMoney() == 0);
	// a broke victim: the withdrawal of 0 stops there
	for (int i = 0; i < 3; ++i)
	{
		pm->onDamageDealt(*tower);
	}
	CHECK(alice->getMoney()->countMoney() == 1065);
}

TEST_CASE("economy: the same inputs give the same state hash frame by frame, and the grid is part of it")
{
	std::vector<std::uint32_t> a, b;
	for (int run = 0; run < 2; ++run)
	{
		World w;
		std::vector<std::uint32_t> &h = run == 0 ? a : b;
		w.placeComplete("Farm", "Alice", 10, 10);
		w.placeUnderConstruction("Farm", "Bob", 13, 10);
		w.placeComplete("Keep", "Alice", 4, 4);
		w.makeAt("Hall", "Alice", 100.0f, 100.0f);
		for (int i = 0; i < 160; ++i)
		{
			w.frames(1);
			h.push_back(w.hash());
		}
		Object *b2 = w.makeAt("Farm", "Bob", 140.0f, 200.0f);
		b2->friend_onBuildComplete();
		for (int i = 0; i < 160; ++i)
		{
			w.frames(1);
			h.push_back(w.hash());
		}
	}
	CHECK(a == b);
	CHECK(a.front() != a.back());
}
