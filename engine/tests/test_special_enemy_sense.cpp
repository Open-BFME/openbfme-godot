// OpenBFME unit tests. GPL-3.0.
// SpecialEnemySenseUpdate (RW 0x89D7E6): SPECIAL_ENEMY_NEAR while an enemy that the filter names is within ScanRange. Synthetic data
// (combattest::CombatWorld), no retail files.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const int kNear = 0x102;

std::string seer(const std::string &name, const std::string &fields)
{
	return "Object " + name + "\n"
		   "  KindOf = INFANTRY SELECTABLE\n"
		   "  Geometry = CYLINDER\n"
		   "  GeometryMajorRadius = 8\n"
		   "  GeometryMinorRadius = 8\n"
		   "  GeometryHeight = 20\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "  Behavior = SpecialEnemySenseUpdate ModuleTag_Sense\n" + fields + "  End\n"
		   "End\n";
}

std::string thing(const std::string &name, const std::string &kinds)
{
	return "Object " + name + "\n"
		   "  KindOf = " + kinds + "\n"
		   "  Geometry = CYLINDER\n"
		   "  GeometryMajorRadius = 8\n"
		   "  GeometryMinorRadius = 8\n"
		   "  GeometryHeight = 20\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "End\n";
}

// combattest's ArcherHorde with the module
std::string sensingHorde()
{
	std::string s = kArcherHorde;
	s.replace(s.find("ArcherHorde"), 11, "SenseHorde");
	s.insert(s.find("  Behavior = HordeAIUpdate"), "  Behavior = SpecialEnemySenseUpdate ModuleTag_Sense\n"
												   "    SpecialEnemyFilter = ANY +ORC\n"
												   "    ScanRange = 300\n"
												   "    ScanInterval = 1000\n"
												   "  End\n");
	return s;
}

const char kOrcs[] =
	"    SpecialEnemyFilter = ANY +ORC +Shelob\n"
	"    ScanRange = 200\n"
	"    ScanInterval = 2000\n"; // 10 frames

struct SenseWorld : CombatWorld
{
	SenseWorld()
	{
		combat().setAutoAcquireEnabled(false);
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the terrain's extent (RW 0x62FCCD)
		const std::string err = w.load(seer("Seer", kOrcs) + seer("Plain", "") + seer("Wide", "    ScanRange = 200\n") + thing("Orc", "INFANTRY SELECTABLE ORC") +
				thing("Man", "INFANTRY SELECTABLE") + thing("Shelob", "MONSTER SELECTABLE") + sensingHorde(),
			INI_LOAD_OVERWRITE, "senseobjects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	static const SpecialEnemySenseUpdate *moduleOf(Object *o)
	{
		const SpecialEnemySenseUpdate *m = dynamic_cast<SpecialEnemySenseUpdate *>(o->findModule("SpecialEnemySenseUpdate"));
		REQUIRE(m);
		return m;
	}
	static void moveTo(Object *o, float x, float y)
	{
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
	}
	// the condition after `n` more frames
	bool seenAfter(Object *o, int n)
	{
		frames(n);
		return o->testModelCondition(kNear);
	}
};
} // namespace

TEST_CASE("SpecialEnemySenseUpdate: the module data's defaults (RW 0x653E06) and fields (RW 0xC07E98)")
{
	SenseWorld e;
	const SpecialEnemySenseUpdateModuleData *d = e.moduleOf(e.unit("Plain", 'A', 300, 300))->data();
	CHECK(d->m_specialEnemyFilter == ObjectFilter());
	CHECK(d->m_scanRange == 0.0f);
	CHECK(d->m_scanInterval == 1u);
	d = e.moduleOf(e.unit("Seer", 'A', 400, 400))->data();
	CHECK(d->m_specialEnemyFilter.rule == ObjectFilter::RULE_ANY);
	CHECK(d->m_specialEnemyFilter.includeNames == std::vector<std::string>{ "Shelob" });
	CHECK(d->m_scanRange == 200.0f);
	CHECK(d->m_scanInterval == 10u);
}

TEST_CASE("SpecialEnemySenseUpdate: SPECIAL_ENEMY_NEAR while an enemy of the filter is within ScanRange; the scan runs every ScanInterval")
{
	SenseWorld e;
	Object *seer = e.unit("Seer", 'A', 300, 300);
	CHECK_FALSE(e.seenAfter(seer, 12));
	Object *orc = e.unit("Orc", 'B', 450, 300);
	// the next scan is at most 10 frames away, then it stays
	e.runUntil([&] { return seer->testModelCondition(kNear); }, 10);
	REQUIRE(seer->testModelCondition(kNear));
	CHECK(e.seenAfter(seer, 10));
	// out of reach: the condition ends exactly at the next scan, 10 frames after the last
	e.moveTo(orc, 520, 300);
	CHECK(e.seenAfter(seer, 9));
	CHECK_FALSE(e.seenAfter(seer, 1));
	e.moveTo(orc, 490, 300);
	CHECK_FALSE(e.seenAfter(seer, 9));
	CHECK(e.seenAfter(seer, 1));
	// the dead are not near (RW 0xC10E20)
	orc->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	CHECK_FALSE(e.seenAfter(seer, 10));
}

TEST_CASE("SpecialEnemySenseUpdate: only enemies (RW 0xC11DC0, mask 1) that the filter allows; a template by its name, the default filter takes everything")
{
	SenseWorld e;
	Object *seer = e.unit("Seer", 'A', 300, 300);
	Object *wide = e.unit("Wide", 'A', 300, 300);
	// an orc of its own side and an enemy that is no orc
	e.unit("Orc", 'A', 320, 300);
	CHECK_FALSE(e.seenAfter(seer, 11));
	CHECK_FALSE(wide->testModelCondition(kNear));
	Object *man = e.unit("Man", 'B', 340, 300);
	CHECK_FALSE(e.seenAfter(seer, 11));
	CHECK(wide->testModelCondition(kNear)); // the default filter: ALL
	e.moveTo(man, 900, 900);
	e.unit("Shelob", 'B', 360, 300);
	CHECK(e.seenAfter(seer, 11));
}

TEST_CASE("SpecialEnemySenseUpdate: a horde shows it on its members, not on itself (RW 0x89D883 .. 0x89D92F)")
{
	SenseWorld e;
	Object *horde = e.unit("SenseHorde", 'A', 300, 300);
	e.frames(6);
	auto showing = [&] {
		int n = 0;
		for (Object *m : *horde->getContain()->getContainedItemsList())
		{
			n += m->testModelCondition(kNear) ? 1 : 0;
		}
		return n;
	};
	REQUIRE(horde->getContain()->getContainedItemsList()->size() == 6u);
	CHECK(showing() == 0);
	Object *orc = e.unit("Orc", 'B', 420, 300);
	e.frames(6);
	CHECK(showing() == 6);
	CHECK_FALSE(horde->testModelCondition(kNear));
	e.moveTo(orc, 900, 900);
	e.frames(6);
	CHECK(showing() == 0);
}

TEST_CASE("SpecialEnemySenseUpdate: the snapshot keeps the next scan's frame (RW 0x89D7C5)")
{
	SenseWorld e;
	Object *seer = e.unit("Seer", 'A', 300, 300);
	e.frames(12);
	SpecialEnemySenseUpdate *m = const_cast<SpecialEnemySenseUpdate *>(e.moduleOf(seer));
	XferSave saved;
	m->xfer(saved);
	const auto next = m->friend_getNextCallFrame();
	e.frames(10);
	CHECK(m->friend_getNextCallFrame() == next + 10u);
	XferLoad load(saved.data());
	m->xfer(load);
	CHECK(m->friend_getNextCallFrame() == next);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad shortLoad(truncated);
	CHECK_THROWS_AS(m->xfer(shortLoad), XferException);
}
