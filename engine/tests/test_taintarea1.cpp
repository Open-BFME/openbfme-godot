// OpenBFME unit tests. GPL-3.0.
// Lane TAINTAREA-1: TheTaintManager (GameLogic/System/TaintManager.h), the taint / elven wood grid. Synthetic cases: the circle raster's cells (inside,
// edge, outside), the painter's id, the clears by painter, the border clip, the realloc's kind-only copy, the save layout and its load checks. Retail
// cases (SKIP without ROTWK_INSTALL): Taint next to an allied Orc horde, Elven Wood next to an allied elf horde: GenericBuff (damage x1.5) on the next
// aura pulse for allies on the painted ground, none for enemies or the ground outside, gone after the horde leaves; the area clears when its object
// goes; the same game at 1 / 2 / 4 logic threads and a save / load in the middle hash alike frame by frame.
// The cell maths are the x87 PC24 sequences of NumericState (pc24Sub / pc24Mul / pc24Div, floor / ceil then FISTP); NumericState's own tests check those
// against x87 hardware (tools/x87_oracle, test_numeric_state.cpp).

#include "doctest.h"
#include "HudTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/JobSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/TaintManager.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace
{
TaintManager::Region square(float size)
{
	TaintManager::Region r;
	r.hiX = size;
	r.hiY = size;
	return r;
}

// the world point at the centre of cell (x, y) of a 40-unit grid from the origin
float mid(int c)
{
	return 40.0f * (float)c + 20.0f;
}

std::uint32_t taintSection(const GameLogic &logic)
{
	std::vector<GameLogic::StateHashSection> sections;
	logic.computeStateHashBreakdown(sections, nullptr);
	for (const GameLogic::StateHashSection &sec : sections)
	{
		if (sec.name == "taint manager")
		{
			return sec.value;
		}
	}
	FAIL("no state hash section 'taint manager'");
	return 0;
}

int valueAt(const TaintManager &t, int cx, int cy, int *owner = nullptr)
{
	int o = -1;
	const int v = t.getTaintAt(mid(cx), mid(cy), &o);
	if (owner)
	{
		*owner = o;
	}
	return v;
}
} // namespace

TEST_CASE("TAINTAREA-1: the grid rasters the midpoint circle (RW 0xAD52B0), answers inside / edge / outside with the painter, clears by painter")
{
	TaintManager t;
	// the constructor: one cell over (0, 0) .. (1, 1), size 1.0; GameLogic::init's SetExtents keeps the region and takes PartitionCellSize
	CHECK(t.width() == 1);
	CHECK(t.height() == 1);
	t.init(40.0f);
	CHECK(t.cellSize() == 40.0f);
	CHECK(t.width() == 1);
	t.setExtents(square(400.0f), 0.0f); // the map's extent, the size kept
	REQUIRE(t.width() == 10);
	REQUIRE(t.height() == 10);
	CHECK_FALSE(t.anyNonDefault());
	// radius 80 over 40-unit cells: ceil(2.0) = 2; centre cell (5, 5). The raster of radius 2: rows 3 and 7 span x 4 .. 6, rows 4 .. 6 span 3 .. 7
	t.applyCircleWorld(Coord3D{ 205.0f, 205.0f, 0.0f }, 80.0f, 0x00, true, 7);
	std::set<std::pair<int, int>> painted;
	for (int y = 0; y < 10; ++y)
	{
		for (int x = 0; x < 10; ++x)
		{
			if (t.cellAt(x, y)->value != TaintManager::VALUE_NONE)
			{
				painted.insert({ x, y });
			}
		}
	}
	std::set<std::pair<int, int>> expect;
	for (int x = 4; x <= 6; ++x)
	{
		expect.insert({ x, 3 });
		expect.insert({ x, 7 });
	}
	for (int y = 4; y <= 6; ++y)
	{
		for (int x = 3; x <= 7; ++x)
		{
			expect.insert({ x, y });
		}
	}
	CHECK(painted == expect);
	int owner = 0;
	CHECK(valueAt(t, 5, 5, &owner) == TaintManager::VALUE_TAINT); // inside
	CHECK(owner == 7);
	CHECK(valueAt(t, 7, 5) == TaintManager::VALUE_TAINT); // the edge on the axis
	CHECK(valueAt(t, 6, 7) == TaintManager::VALUE_TAINT); // the edge row
	CHECK(valueAt(t, 7, 7, &owner) == TaintManager::VALUE_NONE); // the corner is outside the raster
	CHECK(owner == -1);                                          // a neutral cell's owner
	CHECK(valueAt(t, 8, 5) == TaintManager::VALUE_NONE);         // outside
	CHECK(t.cellAt(5, 5)->kind == 0x00);
	// outside the grid: 0 and the owner untouched (RW 0xAD4FA0 writes it only for a cell)
	owner = 99;
	CHECK(t.getTaintAt(-5.0f, 20.0f, &owner) == TaintManager::VALUE_NONE);
	CHECK(owner == 99);
	CHECK(t.getTaintAt(20.0f, 400.0f, &owner) == TaintManager::VALUE_NONE);
	CHECK(owner == 99);
	CHECK(t.anyNonDefault());

	// elven wood over the centre (radius 40: one cell, the plus of radius 1), painter 9
	t.applyCircleWorld(Coord3D{ 205.0f, 205.0f, 0.0f }, 40.0f, 0xFF, true, 9);
	CHECK(valueAt(t, 5, 5, &owner) == TaintManager::VALUE_ELVEN_WOOD);
	CHECK(owner == 9);
	CHECK(valueAt(t, 4, 5) == TaintManager::VALUE_ELVEN_WOOD);
	CHECK(valueAt(t, 5, 6) == TaintManager::VALUE_ELVEN_WOOD);
	CHECK(valueAt(t, 4, 4, &owner) == TaintManager::VALUE_TAINT); // the diagonal stays 7's
	CHECK(owner == 7);
	// painter 7's clear (the radius clear of its object, RW 0x683393): only 7's cells turn neutral (RW 0xAD5740's owner test)
	t.applyCircleWorld(Coord3D{ 205.0f, 205.0f, 0.0f }, 80.0f, 0x80, true, 7);
	int left = 0;
	for (int y = 0; y < 10; ++y)
	{
		for (int x = 0; x < 10; ++x)
		{
			left += t.cellAt(x, y)->isDefault() ? 0 : 1;
		}
	}
	CHECK(left == 5);
	CHECK(valueAt(t, 5, 5) == TaintManager::VALUE_ELVEN_WOOD);
	CHECK(valueAt(t, 4, 4) == TaintManager::VALUE_NONE);
	// a clear of every painter (-1): all neutral, the hash section goes
	t.applyCircleWorld(Coord3D{ 205.0f, 205.0f, 0.0f }, 80.0f, 0x80, true, -1);
	CHECK_FALSE(t.anyNonDefault());

	// a relative paint adds to the kind (clamped to a byte): 0x80 - 0x10 is taint, + 0x20 is elven wood; amount 0 without absolute and a negative radius
	// do nothing (RW 0xAD56E0)
	t.changeTaintLevel(1, 1, 0, -0x10, false, 3);
	CHECK(t.cellAt(1, 1)->kind == 0x70);
	CHECK(t.cellAt(1, 1)->value == TaintManager::VALUE_TAINT);
	t.changeTaintLevel(1, 1, 0, 0x20, false, 4);
	CHECK(t.cellAt(1, 1)->kind == 0x90);
	CHECK(t.cellAt(1, 1)->value == TaintManager::VALUE_ELVEN_WOOD);
	CHECK(t.cellAt(1, 1)->owner == 4);
	t.changeTaintLevel(1, 1, 0, 0x7FFF, false, 4);
	CHECK(t.cellAt(1, 1)->kind == 0xFF);
	t.changeTaintLevel(1, 1, 0, 0, false, 5);
	CHECK(t.cellAt(1, 1)->owner == 4);
	t.changeTaintLevel(1, 1, -1, 0x00, true, 5);
	CHECK(t.cellAt(1, 1)->owner == 4);
	t.changeTaintLevel(1, 1, 0, 0x80, true, -1);
	CHECK_FALSE(t.anyNonDefault());
}

TEST_CASE("TAINTAREA-1: a circle past the grid's sides is clipped (RW 0xAD4E00) and the visitor sees retail's span x; the realloc copies the kind only")
{
	TaintManager t;
	t.init(40.0f);
	t.setExtents(square(400.0f), 0.0f);
	std::vector<std::vector<int>> seen;
	t.setVisitor([&](int x, int y, std::uint8_t kind) { seen.push_back({ x, y, kind }); });
	// at the origin, radius 2 cells: the rows -2 .. 2 of x -2 .. 2 (rows +-2: -1 .. 1) keep only x >= 0, y >= 0: rows 0, 1 three cells, row 2 two
	t.applyCircleWorld(Coord3D{ 0.0f, 0.0f, 0.0f }, 80.0f, 0x00, true, 11);
	int painted = 0;
	for (int y = 0; y < 10; ++y)
	{
		for (int x = 0; x < 10; ++x)
		{
			painted += t.cellAt(x, y)->value == TaintManager::VALUE_TAINT ? 1 : 0;
		}
	}
	CHECK(painted == 8);
	REQUIRE(seen.size() == 8u);
	// the raster's first span is row +2 (x -1 .. 1): its first cell is x 0, the visitor is told the span's x -1 (RW 0xAD5040 counts from the span start)
	CHECK(seen[0] == std::vector<int>{ -1, 2, 0 });
	CHECK(seen[1] == std::vector<int>{ 0, 2, 0 });
	// the realloc (RW 0xAD5340): a larger map keeps each old cell's KIND where it overlaps; the value and the owner are the constructor's, so the query
	// answers none there (a retail quirk: SetExtents of a new map over a painted grid)
	t.setExtents(square(800.0f), 0.0f);
	REQUIRE(t.width() == 20);
	CHECK(t.cellAt(0, 0)->kind == 0x00);
	CHECK(t.cellAt(0, 0)->value == TaintManager::VALUE_NONE);
	CHECK(t.cellAt(0, 0)->owner == -1);
	CHECK(t.anyNonDefault()); // the copied kind differs from the constructor's
	CHECK(t.getTaintAt(10.0f, 10.0f, nullptr) == TaintManager::VALUE_NONE);
	// reset (RW 0xAD4BB0): a zero region (one cell), the size kept; the one cell takes the old (0, 0) kind
	t.reset();
	CHECK(t.width() == 1);
	CHECK(t.cellSize() == 40.0f);
	CHECK(t.cellAt(0, 0)->kind == 0x00);
	// the region's 1.0 minimum and a narrow extent (RW 0xAD5340: hi = lo + 1, ceil of the width over the size)
	t.setExtents(square(0.25f), 0.0f);
	CHECK(t.width() == 1);
	CHECK(t.region().hiX == 1.0f);
	TaintManager::Region odd = square(401.0f);
	t.setExtents(odd, 0.0f);
	CHECK(t.width() == 11);
	// a negative extent is ignored (RW 0xAD5660)
	TaintManager::Region neg;
	neg.loX = 10.0f;
	neg.hiX = 5.0f;
	neg.hiY = 100.0f;
	t.setExtents(neg, 0.0f);
	CHECK(t.width() == 11);
}

TEST_CASE("TAINTAREA-1: the grid's save is retail's CHUNK_TaintManager layout (RW 0xAD4B80 / 0xAD50B0 / 0xAD57E0); a load checks the cells")
{
	TaintManager t;
	t.init(40.0f);
	t.setExtents(square(80.0f), 0.0f); // 2 x 2 cells
	t.applyCircleWorld(Coord3D{ 20.0f, 20.0f, 0.0f }, 1.0f, 0x00, true, 0x1234); // radius ceil(1 / 40) = 1: the plus around (0, 0)
	REQUIRE(t.cellAt(1, 1)->isDefault());
	XferSave s;
	t.xfer(s, nullptr);
	const std::vector<std::uint8_t> &d = s.data();
	// the manager's version (1), the grid's (1), the region (6 reals), size, inverse, width, height, the checksum word 0, then 4 cells of 10 bytes
	REQUIRE(d.size() == 2u + 24u + 8u + 8u + 4u + 4u * 10u);
	CHECK(d[0] == 1);
	CHECK(d[1] == 1);
	const std::vector<std::uint8_t> hiX(d.begin() + 2 + 12, d.begin() + 2 + 16);
	CHECK(hiX == std::vector<std::uint8_t>{ 0x00, 0x00, 0xA0, 0x42 }); // 80.0f
	const size_t cells = 2 + 24 + 8 + 8 + 4;
	CHECK(std::vector<std::uint8_t>(d.begin() + (std::ptrdiff_t)cells, d.begin() + (std::ptrdiff_t)cells + 10) ==
		std::vector<std::uint8_t>{ 2, 0x00, 2, 0, 0, 0, 0x34, 0x12, 0, 0 }); // cell (0, 0): version 2, kind 0, value 2, owner 0x1234
	CHECK(std::vector<std::uint8_t>(d.begin() + (std::ptrdiff_t)cells + 30, d.begin() + (std::ptrdiff_t)cells + 40) ==
		std::vector<std::uint8_t>{ 2, 0x80, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF }); // cell (1, 1): neutral
	TaintManager u;
	u.init(40.0f);
	u.setExtents(square(80.0f), 0.0f); // the same map and cell size
	XferLoad l(d);
	TaintManager::Staged staged;
	u.xfer(l, &staged);
	CHECK(l.atEnd());
	CHECK(u.cellAt(0, 0)->isDefault()); // nothing taken before the commit
	int visited = 0;
	u.setVisitor([&](int, int, std::uint8_t) { ++visited; });
	u.commit(std::move(staged));
	CHECK(visited == 4); // the load refreshes the visitor (RW 0xAD5292)
	CHECK(u.width() == 2);
	CHECK(u.cellSize() == 40.0f);
	int owner = 0;
	CHECK(u.getTaintAt(20.0f, 20.0f, &owner) == TaintManager::VALUE_TAINT);
	CHECK(owner == 0x1234);
	XferSave again;
	u.xfer(again, nullptr);
	CHECK(again.data() == d);
	// a cell whose value is not its kind's, an owner on a neutral cell, a zero width: refused by name
	auto refused = [&](std::vector<std::uint8_t> bytes) {
		XferLoad b(bytes);
		TaintManager v;
		v.init(40.0f);
		v.setExtents(square(80.0f), 0.0f);
		TaintManager::Staged st;
		try
		{
			v.xfer(b, &st);
		}
		catch (const XferException &e)
		{
			return std::string(e.what());
		}
		return std::string("(loaded)");
	};
	std::vector<std::uint8_t> bad = d;
	bad[cells + 2] = 1; // taint kind 0 with value 1
	CHECK(refused(bad) == "taint manager: cell 0 has kind 0, value 1 and owner 4660");
	bad = d;
	bad[cells + 36] = 5; // a neutral cell with an owner
	CHECK(refused(bad).rfind("taint manager: cell 3 has kind 128, value 0 and owner", 0) == 0);
	bad = d;
	bad[2 + 24 + 8] = 0; // width 0
	CHECK(refused(bad) == "taint manager: a grid of 0 x 2 cells");
	// review r1 (Sol): another grid than the game's is refused before a cell is read: an inverse that does not fit the size (the raster's cell radius
	// would overflow), a cell size, an extent, a width
	bad = d;
	bad[2 + 24 + 4 + 3] = 0x4B; // the inverse 0.025 -> 0x4BCCCCCD (26843546.0)
	CHECK(refused(bad) == "taint manager: the snapshot's grid (2 x 2 cells of 40.000000 over 80.000000 x 80.000000) is not this game's (2 x 2 cells of "
	                      "40.000000 over 80.000000 x 80.000000)");
	bad = d;
	bad[2 + 24 + 3] = 0x3F; // the cell size 40.0 (0x42200000) -> 0x3F200000 (0.625)
	CHECK(refused(bad).rfind("taint manager: the snapshot's grid (2 x 2 cells of 0.625000 ", 0) == 0);
	bad = d;
	bad[2 + 12 + 3] = 0x43; // hi.x 80.0 (0x42A00000) -> 0x43A00000 (320.0)
	CHECK(refused(bad).rfind("taint manager: the snapshot's grid (2 x 2 cells of 40.000000 over 320.000000 x 80.000000)", 0) == 0);
	bad = d;
	bad[2 + 24 + 8] = 3; // width 3
	CHECK(refused(bad).rfind("taint manager: the snapshot's grid (3 x 2 cells", 0) == 0);
}

TEST_CASE("TAINTAREA-1: GameLogic hashes the grid only while a cell is painted; obfme:Weather carries it (trailer version 2), version 1 loads it neutral")
{
	logictest::LogicWorld a;
	GameLogic &la = *a.logic;
	const std::vector<std::string> before = la.stateHashSectionNames();
	CHECK(std::find(before.begin(), before.end(), "taint manager") == before.end());
	const std::uint32_t h0 = la.computeStateHash();
	la.taint().init(40.0f);
	la.taint().setExtents(square(400.0f), 0.0f);
	CHECK(la.computeStateHash() == h0); // an unpainted grid of any size keeps the hash
	la.taint().applyCircleWorld(Coord3D{ 205.0f, 205.0f, 0.0f }, 80.0f, 0x00, true, 7);
	const std::vector<std::string> after = la.stateHashSectionNames();
	CHECK(std::find(after.begin(), after.end(), "taint manager") != after.end());
	const std::uint32_t h1 = la.computeStateHash();
	CHECK(h1 != h0);
	// the weather block's trailer: version 2, the burn decay, then the grid
	XferSave s;
	la.weather().xfer(s);
	const std::vector<std::uint8_t> bytes = s.data();
	const size_t gridBytes = 2u + 24u + 8u + 8u + 4u + 100u * 10u;
	REQUIRE(bytes.size() > gridBytes + 5u); // tolerance: the weather's own fields come first (test_save4d.cpp pins them byte by byte)
	const size_t trailer = bytes.size() - gridBytes - 5u;
	CHECK(bytes[trailer] == 2);
	logictest::LogicWorld b;
	GameLogic &lb = *b.logic;
	lb.taint().init(40.0f);
	lb.taint().setExtents(square(400.0f), 0.0f);
	const std::uint32_t hb0 = lb.computeStateHash();
	{
		XferLoad l(bytes);
		lb.weather().xfer(l);
		CHECK(l.atEnd());
	}
	CHECK(lb.taint().getTaintAt(205.0f, 205.0f, nullptr) == TaintManager::VALUE_TAINT);
	CHECK(taintSection(lb) == taintSection(la));
	// a version 1 trailer (the builds before this lane): the grid stays the map's, every cell neutral
	std::vector<std::uint8_t> v1(bytes.begin(), bytes.begin() + (std::ptrdiff_t)(trailer + 5u));
	v1[trailer] = 1;
	{
		XferLoad l(v1);
		lb.weather().xfer(l);
		CHECK(l.atEnd());
	}
	CHECK(lb.taint().width() == 10);
	CHECK_FALSE(lb.taint().anyNonDefault());
	CHECK(lb.computeStateHash() == hb0);
}

// ---- retail -------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct TaintGame
{
	hudtest::Rig rig;
	Object *book = nullptr;

	TaintGame(hudtest::SharedWorld &s, const char *faction, const char *citadel, const char *allyFaction)
		: rig(s, "map mp fall back 4p", faction, allyFaction)
	{
		std::string err;
		Object *anchor = rig.game->createObject(citadel, rig.index(), Coord3D{ 600.0f, 600.0f, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(anchor, err);
		anchor->friend_onBuildComplete();
	}
	GameLogic &logic() { return rig.logic(); }
	int playerIndex(const char *name) { return rig.game->players().findPlayerWithName(name)->getPlayerIndex(); }
	Object *horde(const char *name, int player, float x, float y)
	{
		std::string err;
		Object *h = rig.game->createObject(name, player, Coord3D{ x, y, logic().getGroundHeight(x, y) }, 0.0f, &err);
		REQUIRE_MESSAGE(h, err);
		return h;
	}
	void cast(const char *power, const Coord3D &at)
	{
		const SpecialPowerTemplate *p = TheSpecialPowerStore->findSpecialPowerTemplate(power);
		REQUIRE(p);
		for (ScienceType st : p->getRequiredSciences())
		{
			rig.local->science().grantScience(st);
		}
		if (!book)
		{
			book = SpecialPowerModules::findSpellBookObject(logic(), *rig.local);
			REQUIRE(book);
		}
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, rig.index());
		m.appendIntegerArgument((int)p->getID());
		m.appendLocationArgument(at);
		m.appendObjectIDArgument(INVALID_ID);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(book->getID());
		rig.game->commands().append(m);
	}
};

// the horde's members (its contain, HordeContain)
std::vector<ObjectID> membersOf(Object &horde)
{
	std::vector<ObjectID> out;
	ContainModuleInterface *c = horde.getContain();
	REQUIRE(c);
	if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
	{
		for (const Object *o : *items)
		{
			out.push_back(o->getID());
		}
	}
	return out;
}

// how many of `ids` (alive) carry GenericBuff and its damage x1.5 (DAMAGE_MULT 150%, attributemodifier.ini; ARMOR 50% is the list's other entry)
int buffed(GameLogic &logic, const std::vector<ObjectID> &ids)
{
	int n = 0;
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (!o)
		{
			continue;
		}
		const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
		float damage = 1.0f;
		if (pool && pool->hasList("GenericBuff") && pool->product(ATTRIBUTE_DAMAGE_MULT, nullptr, true, damage) && damage == 1.5f)
		{
			++n;
		}
	}
	return n;
}

int alive(GameLogic &logic, const std::vector<ObjectID> &ids)
{
	int n = 0;
	for (ObjectID id : ids)
	{
		n += logic.findObjectByID(id) ? 1 : 0;
	}
	return n;
}

Object *areaObject(GameLogic &logic, const char *name)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == name)
		{
			return o;
		}
	}
	return nullptr;
}

struct Case
{
	const char *faction, *citadel, *allyFaction, *power, *areaName;
	const char *ownHorde, *allyHorde, *enemyHorde;
	int value;
};

void runCase(const Case &c)
{
	hudtest::SharedWorld &s = hudtest::shared();
	auto ctx = s.world->enterContext();
	TaintGame g(s, c.faction, c.citadel, c.allyFaction);
	GameLogic &logic = g.logic();
	const Coord3D at{ 1500.0f, 900.0f, logic.getGroundHeight(1500.0f, 900.0f) };
	// within the circle (radius SPELLBOOK_TAINT_RADIUS 175): two own hordes (one leaves later), the ally's and the enemy's; one own horde 700 away
	Object *leaver = g.horde(c.ownHorde, g.rig.index(), at.x - 90.0f, at.y + 40.0f);
	Object *stay = g.horde(c.ownHorde, g.rig.index(), at.x + 70.0f, at.y + 70.0f);
	Object *ally = g.horde(c.allyHorde, g.playerIndex("Player_3"), at.x + 70.0f, at.y - 60.0f);
	Object *enemy = g.horde(c.enemyHorde, g.playerIndex("Player_2"), at.x - 60.0f, at.y - 100.0f);
	Object *far = g.horde(c.ownHorde, g.rig.index(), at.x - 700.0f, at.y);
	g.rig.frame(10);
	const std::vector<ObjectID> leaverIds = membersOf(*leaver), stayIds = membersOf(*stay), allyIds = membersOf(*ally), enemyIds = membersOf(*enemy),
								farIds = membersOf(*far);
	for (const std::vector<ObjectID> *ids : { &leaverIds, &stayIds, &allyIds, &enemyIds, &farIds })
	{
		REQUIRE(ids->size() >= 5u); // tolerance: a horde's member count is its template's (the horde tests pin it); the scene needs a few in each
	}
	CHECK(buffed(logic, leaverIds) == 0);
	CHECK(logic.taint().getTaintAt(at.x, at.y, nullptr) == TaintManager::VALUE_NONE);
	{
		// LiveGame sized the grid over the map (RW 0x62FCE2): GameData PartitionCellSize 40, the terrain's extent
		float mx = 0.0f, my = 0.0f;
		REQUIRE(logic.terrain());
		REQUIRE(logic.terrain()->getExtent(0, mx, my));
		CHECK(logic.taint().cellSize() == 40.0f);
		CHECK(logic.taint().width() == SimMath::ceilToInt(SimMath::pc24Mul(mx, SimMath::pc24Div(1.0f, 40.0f))));
		CHECK(logic.taint().height() == SimMath::ceilToInt(SimMath::pc24Mul(my, SimMath::pc24Div(1.0f, 40.0f))));
	}
	g.cast(c.power, at);
	g.rig.frame(1);
	Object *area = areaObject(logic, c.areaName);
	REQUIRE(area);
	const ObjectID areaId = area->getID();
	CHECK(area->getTeam() == g.rig.local->getDefaultTeam()); // RW 0x69954A: the caster's team
	int owner = 0;
	CHECK(logic.taint().getTaintAt(at.x, at.y, &owner) == c.value);
	CHECK(owner == (int)areaId);
	// the aura wakes the frame after its creation (RW 0x89EDAB) and pulses then
	g.rig.frame(3);
	MESSAGE("TAINTAREA-1 " << std::string(c.power) << ": own " << buffed(logic, leaverIds) + buffed(logic, stayIds) << " / " << leaverIds.size() + stayIds.size()
	                       << ", ally " << buffed(logic, allyIds) << " / " << allyIds.size() << ", enemy " << buffed(logic, enemyIds) << " / " << enemyIds.size()
	                       << ", far " << buffed(logic, farIds));
	CHECK(buffed(logic, leaverIds) == alive(logic, leaverIds));
	CHECK(buffed(logic, stayIds) == alive(logic, stayIds));
	CHECK(buffed(logic, allyIds) == alive(logic, allyIds));
	CHECK(buffed(logic, enemyIds) == 0);
	CHECK(buffed(logic, farIds) == 0);
	// the enemy goes (the rest of the scene is the own hordes); one horde leaves: 900 west, beyond the circle and the aura's Range. GenericBuff lasts
	// 3000 ms (15 logic frames) after its last pulse; the aura pulses every RefreshDelay 2000 ms (10 frames) + id % 5
	logic.destroyObject(enemy);
	// the leaver and its members are put 900 west (the same formation, off the circle and the aura's Range)
	for (ObjectID id : leaverIds)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			Coord3D p{ o->getPosition()->x - 900.0f, o->getPosition()->y, 0.0f };
			p.z = logic.getGroundHeight(p.x, p.y);
			o->setPosition(&p);
		}
	}
	{
		Coord3D p{ leaver->getPosition()->x - 900.0f, leaver->getPosition()->y, 0.0f };
		p.z = logic.getGroundHeight(p.x, p.y);
		leaver->setPosition(&p);
	}
	// 30 frames (6 s at retail's 5 logic frames a second): two pulses for the horde that stays, the leaver's last buff (15 frames) ran out; the leaver's AI
	// walks back to where it was told to stand, which takes longer
	g.rig.frame(30);
	{
		float maxD = 0.0f, minD = 1e9f;
		for (ObjectID id : leaverIds)
		{
			if (const Object *o = logic.findObjectByID(id))
			{
				const float dx = o->getPosition()->x - at.x, dy = o->getPosition()->y - at.y;
				const float d = std::sqrt(dx * dx + dy * dy);
				maxD = std::max(maxD, d);
				minD = std::min(minD, d);
			}
		}
		const Coord3D *hp = leaver->getPosition();
		MESSAGE("TAINTAREA-1 leaver: members " << minD << " .. " << maxD << " from the cast, horde at " << hp->x - at.x << ", " << hp->y - at.y << ", frame "
		                                       << logic.getFrame() << ", grid at the leaver " << logic.taint().getTaintAt(hp->x, hp->y, nullptr));
		REQUIRE(minD > 400.0f); // tolerance: the move is 900; any distance past the area (radius 175), the aura's Range (175) and a horde's half width will do
	}
	CHECK(alive(logic, stayIds) == (int)stayIds.size());
	CHECK(buffed(logic, stayIds) == (int)stayIds.size()); // refreshed while it stands on the area
	CHECK(buffed(logic, leaverIds) == 0);                 // gone after it left
	CHECK(buffed(logic, farIds) == 0);
	// the area object goes (its DeletionUpdate after SPELLBOOK_TAINT_TIME; here at once): its destructor's radius clear (RW 0x89EE67 -> 0x683393)
	logic.destroyObject(logic.findObjectByID(areaId));
	g.rig.frame(1);
	CHECK(logic.findObjectByID(areaId) == nullptr);
	CHECK(logic.taint().getTaintAt(at.x, at.y, nullptr) == TaintManager::VALUE_NONE);
	CHECK_FALSE(logic.taint().anyNonDefault());
	// and the buff is not refreshed any more (15 frames after the last pulse)
	g.rig.frame(30);
	CHECK(buffed(logic, stayIds) == 0);
}
} // namespace

TEST_CASE("TAINTAREA-1 retail: Taint next to an allied Orc horde: GenericBuff on the own and the ally's units on the tainted ground, none for the enemy "
          "or far away; it falls after the horde leaves; the area clears with its object")
{
	if (!hudtest::haveWorld("taintarea1 taint"))
	{
		return;
	}
	runCase(Case{ "FactionMordor", "MordorFortressCitadel", "FactionIsengard", "SpellBookTaint", "TaintLand", "MordorFighterHorde", "IsengardFighterHorde",
	              "GondorFighterHorde", TaintManager::VALUE_TAINT });
}

TEST_CASE("TAINTAREA-1 retail: Elven Wood next to an allied elf horde: the ELVEN_WOOD condition (value 1) gives GenericBuff the same way")
{
	if (!hudtest::haveWorld("taintarea1 elven wood"))
	{
		return;
	}
	runCase(Case{ "FactionElves", "ElvenCitadel", "FactionMen", "SpellBookElvenWood", "ElvenGrove", "ElvenLorienWarriorHorde", "GondorFighterHorde",
	              "MordorFighterHorde", TaintManager::VALUE_ELVEN_WOOD });
}

namespace
{
struct LogicThreadsGuard
{
	int before = JobSystem::logic().threadCount();
	~LogicThreadsGuard() { JobSystem::logic().setThreadCount(before); }
};

// the Taint scene, every frame's hash for `frames` frames after the cast; `saveAt` >= 0: a save there, the rest run, the save loaded and run again
// (`reloaded` gets the second run's hashes from the save on)
std::vector<std::uint32_t> taintHashes(int frames, int saveAt, std::vector<std::uint32_t> *reloaded)
{
	hudtest::SharedWorld &s = hudtest::shared();
	auto ctx = s.world->enterContext();
	TaintGame g(s, "FactionMordor", "MordorFortressCitadel", "FactionIsengard");
	GameLogic &logic = g.logic();
	const Coord3D at{ 1500.0f, 900.0f, logic.getGroundHeight(1500.0f, 900.0f) };
	g.horde("MordorFighterHorde", g.rig.index(), at.x - 40.0f, at.y);
	g.horde("IsengardFighterHorde", g.playerIndex("Player_3"), at.x + 60.0f, at.y + 60.0f);
	g.horde("MordorFighterHorde", g.playerIndex("Player_2"), at.x + 60.0f, at.y - 70.0f);
	g.rig.frame(5);
	g.cast("SpellBookTaint", at);
	std::vector<std::uint32_t> out;
	std::vector<std::uint8_t> snapshot;
	for (int f = 0; f < frames; ++f)
	{
		if (f == saveAt)
		{
			std::string error;
			REQUIRE_MESSAGE(logic.saveSnapshot(snapshot, &error), error);
		}
		g.rig.frame(1);
		out.push_back(logic.computeStateHash());
	}
	if (saveAt >= 0 && reloaded)
	{
		std::string error;
		REQUIRE_MESSAGE(g.rig.game->loadSnapshot(snapshot, &error), error);
		CHECK(logic.taint().anyNonDefault());
		for (int f = saveAt; f < frames; ++f)
		{
			g.rig.frame(1);
			reloaded->push_back(g.logic().computeStateHash());
		}
	}
	return out;
}
} // namespace

TEST_CASE("TAINTAREA-1 retail: the Taint scene hashes alike at 1, 2 and 4 logic threads and after a save / load in the middle of the buff")
{
	if (!hudtest::haveWorld("taintarea1 determinism"))
	{
		return;
	}
	LogicThreadsGuard guard;
	const int frames = 150;
	std::vector<std::vector<std::uint32_t>> runs;
	for (int threads : { 1, 2, 4 })
	{
		JobSystem::logic().setThreadCount(threads);
		runs.push_back(taintHashes(frames, -1, nullptr));
		INFO(threads << " logic threads");
		REQUIRE(runs.back().size() == (size_t)frames);
		CHECK(runs.back() == runs.front());
	}
	JobSystem::logic().setThreadCount(1);
	std::vector<std::uint32_t> reloaded;
	const int saveAt = 40; // after the first pulses, before the buff's first refresh
	const std::vector<std::uint32_t> straight = taintHashes(frames, saveAt, &reloaded);
	CHECK(straight == runs.front());
	REQUIRE(reloaded.size() == (size_t)(frames - saveAt));
	for (size_t i = 0; i < reloaded.size(); ++i)
	{
		INFO("frame " << saveAt + (int)i << " after the load");
		CHECK(reloaded[i] == straight[(size_t)saveAt + i]);
	}
}
