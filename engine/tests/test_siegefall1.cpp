// OpenBFME. SIEGEFALL-1 tests: the DieMux killer window (RW 0x8D29A9) and ClearanceTestingSlowDeathBehavior's clearance test in the slow death roulette (RW 0x861712 asking
// slot 0xC, RW 0x887191). Synthetic templates (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include <cmath>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
DamageInfo kill(const Object *source)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = 1000.0f;
	d.m_input.m_damageType = DAMAGE_SLASH;
	return d;
}

// the four falling directions and their windows (the retail MordorSiegeTower's -46..46 / 44..136 / 134..226 / 224..316) and the box each falls into
struct Fall
{
	const char *tag;
	int minAngle, maxAngle;
	const char *offset;
	int weight;
};
const Fall kFalls[4] = {
	{ "ModuleTag_01", -46, 46, "X:-60 Y:0 Z:0", 40 },
	{ "ModuleTag_02", 44, 136, "X:0 Y:60 Z:0", 30 },
	{ "ModuleTag_03", 134, 226, "X:60 Y:0 Z:0", 20 },
	{ "ModuleTag_04", 224, 316, "X:0 Y:-60 Z:0", 15 },
};
const int kFallbackWeight = 10;

std::string tower(const std::string &name, bool windows, bool fallback, const char *extra02 = "")
{
	std::string s = "Object " + name + "\n"
		"  KindOf = SELECTABLE SCORE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 10\n"
		"  GeometryMinorRadius = 10\n"
		"  GeometryHeight = 40\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"    AutoAcquireEnemiesWhenIdle = No\n"
		"  End\n";
	for (const Fall &f : kFalls)
	{
		s += std::string("  Behavior = ClearanceTestingSlowDeathBehavior ") + f.tag + "\n"
			"    DeathTypes = ALL\n"
			"    ProbabilityModifier = " + std::to_string(f.weight) + "\n"
			"    DestructionDelay = 2000\n";
		if (windows)
		{
			s += "    MinKillerAngle = " + std::to_string(f.minAngle) + "\n    MaxKillerAngle = " + std::to_string(f.maxAngle) + "\n";
		}
		s += std::string("    ClearanceGeometry = BOX\n"
			"    ClearanceGeometryMajorRadius = 20\n"
			"    ClearanceGeometryMinorRadius = 10\n"
			"    ClearanceGeometryHeight = 10\n"
			"    ClearanceGeometryOffset = ") + f.offset + "\n";
		if (std::string(f.tag) == "ModuleTag_02")
		{
			s += extra02;
		}
		s += "  End\n";
	}
	if (fallback)
	{
		s += "  Behavior = SlowDeathBehavior ModuleTag_05\n"
			"    DeathTypes = ALL\n"
			"    ProbabilityModifier = " + std::to_string(kFallbackWeight) + "\n"
			"    DestructionDelay = 2000\n"
			"  End\n";
	}
	return s + "End\n";
}

std::string templates()
{
	return tower("SFTower", true, true) + tower("SFTower4", true, false) + tower("SFTowerOpen", false, true) +
		tower("SFTowerCliff", true, false, "    ClearanceMaxHeightFraction = 0.5\n");
}

// the tags of the slow deaths that began
std::vector<std::string> begun(const Object *o)
{
	std::vector<std::string> out;
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (const SlowDeathBehavior *sd = dynamic_cast<const SlowDeathBehavior *>(m.get()))
		{
			if (sd->isSlowDeathActivated())
			{
				out.push_back(sd->getModuleData()->getModuleTagName());
			}
		}
	}
	return out;
}

// the roulette's draws (SlowDeathBehavior.cpp:0x32F) of one death
std::vector<GameLogicRandom::Call> killAndLogRolls(CombatWorld &w, Object *victim, const Object *source)
{
	w.logic->random().clearCallLog();
	w.logic->random().enableCallLog(true);
	DamageInfo d = kill(source);
	victim->attemptDamage(d);
	w.logic->random().enableCallLog(false);
	std::vector<GameLogicRandom::Call> rolls;
	for (const GameLogicRandom::Call &c : w.logic->random().callLog())
	{
		if (c.file == "SlowDeathBehavior.cpp" && c.line == 0x32F)
		{
			rolls.push_back(c);
		}
	}
	return rolls;
}

bool stopNoted(CombatWorld &w, const std::string &prefix)
{
	for (const std::string &s : w.logic->report().stops)
	{
		if (s.compare(0, prefix.size(), prefix) == 0)
		{
			return true;
		}
	}
	return false;
}

// a unit at `radius` from `at` in the direction `degrees` (counter-clockwise from +x)
Object *unitAround(CombatWorld &w, const char *name, const Object *at, double degrees, double radius = 150.0)
{
	const double r = degrees * 3.14159265358979323846 / 180.0;
	return w.unit(name, 'B', (float)(at->getPosition()->x + radius * std::cos(r)), (float)(at->getPosition()->y + radius * std::sin(r)));
}

// a blocker standing in the centre of a module's clearance box
Object *blockerIn(CombatWorld &w, const Object *towerObj, const char *tag)
{
	for (const std::unique_ptr<BehaviorModule> &m : towerObj->modules())
	{
		if (const ClearanceTestingSlowDeathBehavior *c = dynamic_cast<const ClearanceTestingSlowDeathBehavior *>(m.get()))
		{
			if (c->getModuleData()->getModuleTagName() == tag)
			{
				const Coord3D at = c->getClearanceTestGeometryCenter();
				return w.unit("Dummy", 'B', at.x, at.y);
			}
		}
	}
	return nullptr;
}

// replays RW 0x861712's walk: the pick of `roll` over the candidates' weights
size_t walk(const std::vector<int> &weights, int roll)
{
	size_t i = 0;
	for (; i < weights.size(); ++i)
	{
		if (roll < weights[i])
		{
			return i;
		}
		roll -= weights[i];
	}
	return weights.size() - 1;
}
} // namespace

TEST_CASE("siegefall1: DieMux killer window maths at the edges (+-46 degrees) and across 180 (RW 0x8D29A9)")
{
	CombatWorld w(templates().c_str());
	Object *t = w.unit("Dummy", 'A', 500, 500);
	const float degToRad = 3.14159265358979323846f / 180.0f;
	DieMuxData front;
	front.m_minKillerAngle = -46.0f * degToRad;
	front.m_maxKillerAngle = 46.0f * degToRad;
	DieMuxData back;
	back.m_minKillerAngle = 134.0f * degToRad;
	back.m_maxKillerAngle = 226.0f * degToRad;
	auto applies = [&](const DieMuxData &mux, double bearing) {
		Object *k = unitAround(w, "Dummy", t, bearing, 100.0);
		DieModuleInterface::Event e;
		e.sourceId = k->getID();
		const bool r = mux.isDieApplicable(*t, e);
		w.logic->destroyObject(k);
		w.frames(1);
		return r;
	};
	// the angle is the facing minus the killer's bearing: a facing of 0 sees a killer at bearing b at -b
	CHECK(applies(front, 0.0));
	CHECK(applies(front, 45.9));
	CHECK(applies(front, -45.9));
	CHECK_FALSE(applies(front, 46.1));
	CHECK_FALSE(applies(front, -46.1));
	CHECK_FALSE(applies(front, 180.0));
	CHECK(applies(back, 180.0));
	CHECK(applies(back, 179.0));
	CHECK(applies(back, -179.0));
	CHECK(applies(back, 135.0));
	CHECK(applies(back, -135.0));
	CHECK_FALSE(applies(back, 133.0));
	CHECK_FALSE(applies(back, -133.0));
	CHECK_FALSE(applies(back, 0.0));
	// no source: a window refuses, no window applies
	DieModuleInterface::Event none;
	CHECK_FALSE(front.isDieApplicable(*t, none));
	CHECK(DieMuxData().isDieApplicable(*t, none));
}

TEST_CASE("siegefall1: the killer's side selects exactly that window's module (+x 01, -y 02, -x 03, +y 04)")
{
	CombatWorld w(templates().c_str());
	const double bearings[4] = { 0.0, -90.0, 180.0, 90.0 };
	for (int i = 0; i < 4; ++i)
	{
		CAPTURE(i);
		Object *t = w.unit("SFTower4", 'A', 150.0f + 230.0f * (float)i, 500);
		Object *k = unitAround(w, "Dummy", t, bearings[i]);
		const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, k);
		REQUIRE(rolls.size() == 1);
		CHECK(rolls[0].lo == 0);
		CHECK(rolls[0].hi == kFalls[i].weight - 1); // the only applicable candidate
		CHECK(begun(t) == std::vector<std::string>{ kFalls[i].tag });
	}
	CHECK_FALSE(stopNoted(w, "[S-7163]")); // fractions 1.1: no footprint walk
}

TEST_CASE("siegefall1: without a source every window module is inapplicable: the plain SlowDeathBehavior is the only candidate")
{
	CombatWorld w(templates().c_str());
	Object *t = w.unit("SFTower", 'A', 300, 500);
	const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, nullptr);
	REQUIRE(rolls.size() == 1);
	CHECK(rolls[0].hi == kFallbackWeight - 1);
	CHECK(begun(t) == std::vector<std::string>{ "ModuleTag_05" });
	// the four window modules alone: no candidate, no draw, nothing begins
	Object *t4 = w.unit("SFTower4", 'A', 700, 500);
	CHECK(killAndLogRolls(w, t4, nullptr).empty());
	CHECK(begun(t4).empty());
	CHECK(t4->isEffectivelyDead());
}

TEST_CASE("siegefall1: a blocker in a module's box refuses it and the roll repeats with the remaining weights")
{
	CombatWorld w(templates().c_str());
	int repeated = 0;
	for (int n = 0; n < 4; ++n)
	{
		CAPTURE(n);
		Object *t = w.unit("SFTower", 'A', 150.0f + 230.0f * (float)n, 300);
		REQUIRE(blockerIn(w, t, "ModuleTag_02") != nullptr);
		Object *k = unitAround(w, "Dummy", t, -90.0); // 02's window
		const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, k);
		REQUIRE_FALSE(rolls.empty());
		CHECK(rolls[0].hi == kFalls[1].weight + kFallbackWeight - 1); // 02 and 05
		if (rolls[0].result < kFalls[1].weight)
		{
			// 02 won, its clearance test refused, its weight left the total: one more draw over 05 alone
			REQUIRE(rolls.size() == 2);
			CHECK(rolls[1].hi == kFallbackWeight - 1);
			++repeated;
		}
		else
		{
			CHECK(rolls.size() == 1);
		}
		CHECK(begun(t) == std::vector<std::string>{ "ModuleTag_05" });
	}
	CHECK(repeated == 3); // the seeded rolls of the four towers: 02 wins the first draw three times
}

TEST_CASE("siegefall1: all four boxes blocked: the plain SlowDeathBehavior is chosen; every repeat removes the refused weight")
{
	CombatWorld w(templates().c_str());
	Object *t = w.unit("SFTowerOpen", 'A', 500, 500);
	for (const Fall &f : kFalls)
	{
		REQUIRE(blockerIn(w, t, f.tag) != nullptr);
	}
	Object *k = unitAround(w, "Dummy", t, 30.0, 200.0);
	const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, k);
	std::vector<int> weights = { kFalls[0].weight, kFalls[1].weight, kFalls[2].weight, kFalls[3].weight, kFallbackWeight };
	std::vector<int> ids = { 0, 1, 2, 3, 4 };
	int total = 0;
	for (int v : weights)
	{
		total += v;
	}
	size_t r = 0;
	for (;; ++r)
	{
		REQUIRE_FALSE(r == rolls.size());
		CHECK(rolls[r].hi == total - 1);
		const size_t i = walk(weights, rolls[r].result);
		total -= weights[i];
		if (ids[i] == 4)
		{
			break;
		}
		ids[i] = ids.back();
		weights[i] = weights.back();
		ids.pop_back();
		weights.pop_back();
	}
	CHECK(rolls.size() == r + 1);
	CHECK(begun(t) == std::vector<std::string>{ "ModuleTag_05" });
}

TEST_CASE("siegefall1: on open ground all five candidates enter the roll in module order with their weights")
{
	CombatWorld w(templates().c_str());
	const std::vector<std::string> tags = { "ModuleTag_01", "ModuleTag_02", "ModuleTag_03", "ModuleTag_04", "ModuleTag_05" };
	const std::vector<int> weights = { kFalls[0].weight, kFalls[1].weight, kFalls[2].weight, kFalls[3].weight, kFallbackWeight };
	for (int n = 0; n < 8; ++n)
	{
		CAPTURE(n);
		Object *t = w.unit("SFTowerOpen", 'A', 120.0f + 110.0f * (float)n, 150.0f + 350.0f * (float)(n % 2));
		Object *k = unitAround(w, "Dummy", t, 30.0, 200.0);
		const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, k);
		REQUIRE(rolls.size() == 1);
		CHECK(rolls[0].hi == 40 + 30 + 20 + 15 + 10 - 1);
		CHECK(begun(t) == std::vector<std::string>{ tags[walk(weights, rolls[0].result)] });
	}
}

TEST_CASE("siegefall1: a fraction at or below 1.0 reports the unported footprint walk (S-7163) and the terrain counts as clear")
{
	CombatWorld w(templates().c_str());
	Object *t = w.unit("SFTowerCliff", 'A', 500, 500);
	Object *k = unitAround(w, "Dummy", t, -90.0); // 02's window; 02 has ClearanceMaxHeightFraction 0.5
	const std::vector<GameLogicRandom::Call> rolls = killAndLogRolls(w, t, k);
	REQUIRE(rolls.size() == 1);
	CHECK(begun(t) == std::vector<std::string>{ "ModuleTag_02" });
	CHECK(stopNoted(w, "[S-7163] ClearanceTestingSlowDeathBehavior of SFTowerCliff: the footprint height walk"));
}

TEST_CASE("siegefall1: the clearance data parses RotWK's rows and keeps the BFME2 ctor defaults")
{
	CombatWorld w(templates().c_str());
	Object *t = w.unit("SFTower", 'A', 500, 500);
	const ClearanceTestingSlowDeathBehavior *c = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : t->modules())
	{
		if (const ClearanceTestingSlowDeathBehavior *x = dynamic_cast<const ClearanceTestingSlowDeathBehavior *>(m.get()))
		{
			c = x;
			break;
		}
	}
	REQUIRE(c != nullptr);
	const ClearanceTestingSlowDeathBehaviorModuleData *d = c->clearanceData();
	REQUIRE(d->m_clearanceGeometry.size() == 1);
	CHECK(d->m_clearanceGeometry[0].type == ObjectGeometry::SHAPE_BOX);
	CHECK(d->m_clearanceGeometry[0].majorRadius == 20.0f);
	CHECK(d->m_clearanceGeometry[0].minorRadius == 10.0f);
	CHECK(d->m_clearanceGeometryOffset.x == -60.0f);
	CHECK(d->m_clearanceMaxHeight == 20.0f);
	CHECK(d->m_clearanceMaxHeightFraction == 1.1f);
	CHECK(d->m_clearanceMinHeight == -20.0f);
	CHECK(d->m_clearanceMinHeightFraction == 1.1f);
	// the centre: the offset through the transform (facing 0: the offset itself)
	const Coord3D at = c->getClearanceTestGeometryCenter();
	CHECK(at.x == t->getPosition()->x - 60.0f);
	CHECK(at.y == t->getPosition()->y);
}
