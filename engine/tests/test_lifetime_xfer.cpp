// OpenBFME. Synthetic tests for LifetimeUpdate::xfer (RW 0x007A7E88): version (1, 2) preserves m_waitingForWakeUp (+0x28)
// across save/load and remains backwards-compatible with v1 snapshots.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/StructureModules.h"

#include <cstdint>
#include <vector>

using namespace combattest;

namespace
{
const char kLifetimeWaitObjects[] =
	"Object WaitLifetimeProbe\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 1000\n"
	"    MaxLifetime = 1000\n"
	"    WaitForWakeUp = Yes\n"
	"  End\n"
	"End\n";

struct WaitLifetimeWorld : CombatWorld
{
	WaitLifetimeWorld()
	{
		const std::string err = w.load(kLifetimeWaitObjects, INI_LOAD_OVERWRITE, "lifetime_xfer.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
};
} // namespace

// RW 0x007A7E88: LifetimeUpdate::xfer version (1, 2) serializes m_waitingForWakeUp (+0x28) so a woken WaitForWakeUp
// module stays awake (waitingForWakeUp() == false) after save/load instead of reverting to the constructor's true.
TEST_CASE("lifetime xfer: WaitForWakeUp state is preserved across save/load (RW 0x007A7E88)")
{
	WaitLifetimeWorld w1;
	Object *src = w1.unit("WaitLifetimeProbe", 'B', 100, 100);
	REQUIRE(src != nullptr);
	auto *srcLife = dynamic_cast<LifetimeUpdate *>(src->findModule("LifetimeUpdate"));
	REQUIRE(srcLife != nullptr);
	CHECK(srcLife->waitsForWakeUp());
	CHECK(srcLife->waitingForWakeUp());

	// RW 0x7A7E79: wakeUp() clears +0x28 and schedules m_dieFrame.
	srcLife->wakeUp();
	CHECK_FALSE(srcLife->waitingForWakeUp());
	const unsigned expectedDie = srcLife->dieFrame();
	const unsigned expectedStart = srcLife->startFrame();
	CHECK(expectedDie == expectedStart + 5u);

	XferSave save;
	srcLife->xfer(save);

	// Fresh object's constructor sets m_waitingForWakeUp = true.
	WaitLifetimeWorld w2;
	Object *dst = w2.unit("WaitLifetimeProbe", 'B', 100, 100);
	REQUIRE(dst != nullptr);
	auto *dstLife = dynamic_cast<LifetimeUpdate *>(dst->findModule("LifetimeUpdate"));
	REQUIRE(dstLife != nullptr);
	CHECK(dstLife->waitingForWakeUp());

	XferLoad load(save.data());
	dstLife->xfer(load);
	CHECK_FALSE(dstLife->waitingForWakeUp());
	CHECK(dstLife->dieFrame() == expectedDie);
	CHECK(dstLife->startFrame() == expectedStart);
}

// RW 0x007A7E88 (0x7A7ECC..0x7A7ED0): version 1 snapshots omit +0x28; loading a v1 payload must succeed and leave
// m_waitingForWakeUp at its initialized value.
TEST_CASE("lifetime xfer: legacy v1 snapshot loads without WaitForWakeUp byte (RW 0x007A7E88)")
{
	WaitLifetimeWorld w1;
	Object *src = w1.unit("WaitLifetimeProbe", 'B', 100, 100);
	REQUIRE(src != nullptr);
	auto *srcLife = dynamic_cast<LifetimeUpdate *>(src->findModule("LifetimeUpdate"));
	REQUIRE(srcLife != nullptr);

	XferSave modProbe;
	srcLife->xfer(modProbe);
	REQUIRE(modProbe.data().size() >= 10u); // tolerance: UpdateModule::xfer header precedes the 9-byte LifetimeUpdate tail
	std::vector<std::uint8_t> payload;
	payload.push_back(1u); // v1
	payload.insert(payload.end(), modProbe.data().begin() + 1, modProbe.data().end() - 9);
	std::uint32_t die = 42u;
	std::uint32_t start = 12u;
	XferSave tail;
	tail.xferUnsignedInt(&die);
	tail.xferUnsignedInt(&start);
	payload.insert(payload.end(), tail.data().begin(), tail.data().end());

	WaitLifetimeWorld w2;
	Object *dst = w2.unit("WaitLifetimeProbe", 'B', 100, 100);
	REQUIRE(dst != nullptr);
	auto *dstLife = dynamic_cast<LifetimeUpdate *>(dst->findModule("LifetimeUpdate"));
	REQUIRE(dstLife != nullptr);
	CHECK(dstLife->waitingForWakeUp());

	XferLoad load(payload);
	dstLife->xfer(load);
	CHECK(dstLife->waitingForWakeUp());
	CHECK(dstLife->dieFrame() == 42u);
	CHECK(dstLife->startFrame() == 12u);
}
