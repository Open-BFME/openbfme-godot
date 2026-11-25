// OpenBFME. StructureCollapseUpdate::getDisabledTypesToProcess (RW 0x00654FD1; fork issue 763).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/ObjectTypes.h"

#include <string>

using namespace combattest;

namespace
{
const char *kCollapseIni =
	"Object Local98CollapseBuilding\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StructureCollapseUpdate ModuleTag_Collapse\n"
	"    MinCollapseDelay = 1\n"
	"    MaxCollapseDelay = 1\n"
	"    CollapseDamping = 0.5\n"
	"    MaxShudder = 0.6\n"
	"    MinBurstDelay = 5\n"
	"    MaxBurstDelay = 5\n"
	"    BigBurstFrequency = 4\n"
	"  End\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"End\n";

StructureCollapseUpdate *findCollapse(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (auto *c = dynamic_cast<StructureCollapseUpdate *>(m.get()))
		{
			return c;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("LOCAL-98 StructureCollapseUpdate: collapses while DISABLED_USER_FROZEN (RW 0x00654FD1)")
{
	CombatWorld cw;
	StructureModules::registerAll(cw.w.modules);
	const std::string err = cw.w.load(kCollapseIni, INI_LOAD_OVERWRITE, "local98_collapse.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	Object *b = cw.unit("Local98CollapseBuilding", 'A', 400.0f, 400.0f);
	REQUIRE(b != nullptr);
	StructureCollapseUpdate *scu = findCollapse(*b);
	REQUIRE(scu != nullptr);

	// RW 0x00654FD1: UpdateInterface vtable 0x00C6902C slot 1 sets byte[1] |= 0x08 (bit 11 = DISABLED_USER_FROZEN).
	CHECK(scu->getDisabledTypesToProcess() == (DisabledMaskType)(1u << DISABLED_USER_FROZEN));

	b->setDisabled(DISABLED_USER_FROZEN, 1000);
	b->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	CHECK(scu->state() == COLLAPSESTATE_WAITINGFORCOLLAPSESTART);

	for (int f = 0; f < 40 && scu->state() != COLLAPSESTATE_DONE; ++f)
	{
		cw.logic->runLogicFrame();
	}
	CHECK(scu->state() == COLLAPSESTATE_DONE);
}

TEST_CASE("LOCAL-98 StructureCollapseUpdate: stays paused while DISABLED_HELD (RW 0x00654FD1)")
{
	CombatWorld cw;
	StructureModules::registerAll(cw.w.modules);
	const std::string err = cw.w.load(kCollapseIni, INI_LOAD_OVERWRITE, "local98_collapse_held.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	Object *b = cw.unit("Local98CollapseBuilding", 'A', 400.0f, 400.0f);
	REQUIRE(b != nullptr);
	StructureCollapseUpdate *scu = findCollapse(*b);
	REQUIRE(scu != nullptr);

	b->setDisabled(DISABLED_HELD, 1000);
	b->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	CHECK(scu->state() == COLLAPSESTATE_WAITINGFORCOLLAPSESTART);

	for (int f = 0; f < 10; ++f)
	{
		cw.logic->runLogicFrame();
	}
	// DISABLED_HELD is not in RW 0x00654FD1's mask, so the update stays paused.
	CHECK(scu->state() == COLLAPSESTATE_WAITINGFORCOLLAPSESTART);
}
