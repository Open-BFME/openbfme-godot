// OpenBFME. GPL-3.0.
// PORT-6: StealthDetectorUpdate's two container permissions, on synthetic INI only (no retail or Ennorath files).
//
// RW 0x8A6477 .. RW 0x8A64AB: no container or contain module continues the scan. Otherwise contain vslot 0x10 (RW 0x8A6492) selects the byte:
// garrisonable -> CanDetectWhileGarrisoned (+0x68, RW 0x8A6499), every other container -> CanDetectWhileContained (+0x69, RW 0x8A64A6). A set byte
// continues through RW 0x8A649C; a clear byte returns DetectionRate (+8) at RW 0x8A649E through RW 0x8A6A36. A completed scan returns the same delay at
// RW 0x8A6A25 .. RW 0x8A6A36. The expected detection and sleep outcomes below come from those branches, not from the port's implementation.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/InvisibilityModules.h"

#include <string>

using namespace combattest;

namespace
{
std::string detector(const char *name, bool garrisoned, bool contained)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"  Behavior = StealthDetectorUpdate ModuleTag_Detector\n"
		"    DetectionRate = 1000\n" // five logic frames at the fixture's five frames per second
		"    DetectionRange = 300\n"
		"    InitiallyDisabled = Yes\n" // the probe calls update() itself; no scheduled scan can race it
		+ std::string("    CanDetectWhileGarrisoned = ") + (garrisoned ? "Yes\n" : "No\n")
		+ std::string("    CanDetectWhileContained = ") + (contained ? "Yes\n" : "No\n")
		+ "  End\n"
		  "End\n";
}

const char kContainerBody[] =
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n";

const std::string kPort6Objects =
	detector("Port6GarrisonOnContainedOff", true, false)
	+ detector("Port6GarrisonOffContainedOn", false, true)
	+ detector("Port6TransportOnGarrisonOff", false, true)
	+ detector("Port6TransportOffGarrisonOn", true, false)
	+ detector("Port6OutsideBothOff", false, false)
	+ "Object Port6HiddenEnemy\n"
	  "  KindOf = INFANTRY SELECTABLE\n"
	  "  Geometry = CYLINDER\n"
	  "  GeometryMajorRadius = 8\n"
	  "  GeometryMinorRadius = 8\n"
	  "  GeometryHeight = 20\n"
	  "  Body = ActiveBody ModuleTag_Body\n"
	  "    MaxHealth = 100\n"
	  "  End\n"
	  "  Behavior = StealthUpdate ModuleTag_Stealth\n"
	  "    StealthDelay = 0\n"
	  "  End\n"
	  "End\n"
	  "Object Port6Garrison\n"
	+ kContainerBody
	+ "  Behavior = GarrisonContain ModuleTag_Contain\n"
	  "    ContainMax = 2\n"
	  "    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n"
	  "  End\n"
	  "End\n"
	  "Object Port6Transport\n"
	+ kContainerBody
	+ "  Behavior = TransportContain ModuleTag_Contain\n"
	  "    Slots = 2\n"
	  "    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n"
	  "  End\n"
	  "End\n";

struct Probe
{
	UpdateSleepTime sleep = UPDATE_SLEEP_INVALID;
	unsigned rate = 0;
	unsigned scans = 0;
	unsigned marks = 0;
	unsigned now = 0;
	unsigned detectionExpires = 0;
	bool stealthedBefore = false;
	bool detectedAfter = false;
};

Probe probe(const char *detectorName, const char *containerName = nullptr)
{
	CombatWorld w(kPort6Objects.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *detectorObject = w.unit(detectorName, 'A', 400, 400);
	Object *enemy = w.unit("Port6HiddenEnemy", 'B', 450, 400);
	Object *container = containerName ? w.unit(containerName, 'A', 400, 400) : nullptr;
	StealthDetectorUpdate *detector = dynamic_cast<StealthDetectorUpdate *>(detectorObject->findModule("StealthDetectorUpdate"));
	StealthUpdate *stealth = StealthUpdate::of(*enemy);
	REQUIRE(detector != nullptr);
	REQUIRE(stealth != nullptr);
	const auto *data = dynamic_cast<const StealthDetectorUpdateModuleData *>(detector->getModuleData());
	REQUIRE(data != nullptr);
	REQUIRE(data->m_detectionRate == 5u); // DetectionRate = 1000 ms, the duration conversion used by RW's field parser

	w.frames(3); // StealthUpdate makes the nearby enemy stealthed; the detector stays asleep until the explicit probe
	const int stealthed = CombatNames::status("STEALTHED");
	const int detected = CombatNames::status("DETECTED");
	REQUIRE(enemy->testStatus((unsigned)stealthed));
	REQUIRE_FALSE(enemy->testStatus((unsigned)detected));
	REQUIRE(detector->scans() == 0u);

	if (container)
	{
		REQUIRE(container->getContain() != nullptr);
		CHECK(container->getContain()->isGarrisonable() == (std::string(containerName) == "Port6Garrison"));
		REQUIRE(container->getContain()->addToContain(detectorObject));
		REQUIRE(detectorObject->getContainedBy() == container);
		REQUIRE_FALSE(detectorObject->isInWorld()); // ObjectStatusOfContained = ENCLOSED
	}
	else
	{
		REQUIRE(detectorObject->getContainedBy() == nullptr);
	}

	Probe p;
	p.now = w.logic->getFrame();
	p.rate = data->m_detectionRate;
	p.stealthedBefore = enemy->testStatus((unsigned)stealthed);
	p.sleep = detector->update();
	p.scans = detector->scans();
	p.marks = detector->marks();
	p.detectedAfter = enemy->testStatus((unsigned)detected);
	p.detectionExpires = stealth->detectionExpiresFrame();
	return p;
}

void checkDetected(const Probe &p)
{
	CHECK(p.stealthedBefore);
	CHECK(p.sleep == UPDATE_SLEEP((int)p.rate)); // RW 0x8A6A25 .. RW 0x8A6A36
	CHECK(p.scans == 1u);
	CHECK(p.marks == 1u);
	CHECK(p.detectedAfter);
	CHECK(p.detectionExpires == p.now + p.rate + 1u); // RW 0x8A667A .. RW 0x8A6685
}

void checkNotDetected(const Probe &p)
{
	CHECK(p.stealthedBefore);
	CHECK(p.sleep == UPDATE_SLEEP((int)p.rate)); // RW 0x8A649E .. RW 0x8A64A1 -> RW 0x8A6A36
	CHECK(p.scans == 0u);
	CHECK(p.marks == 0u);
	CHECK_FALSE(p.detectedAfter);
	CHECK(p.detectionExpires == 0u);
}
} // namespace

TEST_CASE("PORT-6 garrison: CanDetectWhileGarrisoned alone permits or blocks detection (RW 0x8A6492, RW 0x8A6499)")
{
	SUBCASE("Yes, with CanDetectWhileContained deliberately No")
	{
		checkDetected(probe("Port6GarrisonOnContainedOff", "Port6Garrison"));
	}
	SUBCASE("No, with CanDetectWhileContained deliberately Yes")
	{
		checkNotDetected(probe("Port6GarrisonOffContainedOn", "Port6Garrison"));
	}
}

TEST_CASE("PORT-6 transport: CanDetectWhileContained alone permits or blocks detection (RW 0x8A6497, RW 0x8A64A6)")
{
	SUBCASE("Yes, with CanDetectWhileGarrisoned deliberately No")
	{
		checkDetected(probe("Port6TransportOnGarrisonOff", "Port6Transport"));
	}
	SUBCASE("No, with CanDetectWhileGarrisoned deliberately Yes")
	{
		checkNotDetected(probe("Port6TransportOffGarrisonOn", "Port6Transport"));
	}
}

TEST_CASE("PORT-6 outside a container: both permissions clear do not suppress the neighbouring scan path (RW 0x8A6482)")
{
	checkDetected(probe("Port6OutsideBothOff"));
}
