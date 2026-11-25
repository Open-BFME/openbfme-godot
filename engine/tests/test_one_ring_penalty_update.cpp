// OpenBFME. GPL-3.0.
// OneRingPenaltyUpdate tests (both editions; RW 0x89D647 / 0x89D2AC / 0x89D41B / 0x89D6B4).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/OneRingPenaltyUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

using namespace combattest;

namespace
{
std::string unitWith(const char *name, const std::string &fields)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE SCORE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1000\n"
		"  End\n"
		"  Behavior = DestroyDie ModuleTag_Destroy\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n" + fields + "End\n";
}

const std::string kRingObjects =
	unitWith("Frodo",
		"  Behavior = StealthUpdate ModuleTag_Stealth\n"
		"    OneRingDelayOn = 200\n"
		"    OneRingDelayOff = 200\n"
		"    RingAnimTimeOn = 200\n"
		"    RingAnimTimeOff = 200\n"
		"    RingDelayAfterRemoving = 2000\n"
		"  End\n"
		"  Behavior = OneRingPenaltyUpdate ModuleTag_Ring\n"
		"    SpecialObjectName = TheEye\n"
		"    RingTimeBeforeSpawning = 2000\n"
		"    TimeSpentRoamingAround = 2000\n"
		"    TimeRingPowerSuppressed = 6000\n"
		"    StartingDistanceFromMe = 100\n"
		"    TimeFrozenFromPenalty = 1000\n"
		"  End\n") +
	unitWith("TheEye", "");

const unsigned kStatusCanStealth = 0x12;
} // namespace

TEST_CASE("modgap1 OneRingPenaltyUpdate: wearing the ring calls the Eye, pays penalty, and round-trips xfer (RW 0x89D647)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w(kRingObjects.c_str());
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		Object *frodo = w.unit("Frodo", 'A', 400, 400);
		REQUIRE(frodo != nullptr);
		StealthUpdate *stealth = StealthUpdate::of(*frodo);
		OneRingPenaltyUpdate *ring = dynamic_cast<OneRingPenaltyUpdate *>(frodo->findModule("OneRingPenaltyUpdate"));
		REQUIRE(stealth != nullptr);
		REQUIRE(ring != nullptr);

		stealth->toggleRing();
		REQUIRE(stealth->ringWorn());
		ObjectID eye = INVALID_ID;
		for (int i = 0; i < 40 && eye == INVALID_ID; ++i)
		{
			w.frames(1);
			eye = ring->eye();
		}
		REQUIRE(eye != INVALID_ID);
		Object *eyeObject = w.byId(eye);
		REQUIRE(eyeObject != nullptr);
		CHECK(eyeObject->getTemplate()->getName() == "TheEye");

		// xfer round-trip while Eye is active
		StateHasher h1;
		ring->crc(h1);
		XferSave save;
		ring->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		ring->xfer(load);
		StateHasher h2;
		ring->crc(h2);
		CHECK(h1.value() == h2.value());

		for (int i = 0; i < 60 && ring->penaltyFrame() == 0; ++i)
		{
			w.frames(1);
		}
		REQUIRE(ring->penaltyFrame() != 0); // tolerance: exact penalty frame depends on Eye roaming arrival
		CHECK(ring->eye() == INVALID_ID);
		CHECK_FALSE(frodo->testStatus(kStatusCanStealth));
		w.frames(10);
		CHECK_FALSE(stealth->ringWorn());
		CHECK(ring->ringFrame() == 0);
		for (int i = 0; i < 40 && ring->penaltyFrame() != 0; ++i)
		{
			w.frames(1);
		}
		CHECK(ring->penaltyFrame() == 0);
		CHECK(frodo->testStatus(kStatusCanStealth));
	}
}

TEST_CASE("modgap1 OneRingPenaltyUpdate: taking the ring off in time removes the Eye and pays no penalty (RW 0x89D6B4)")
{
	CombatWorld w(kRingObjects.c_str());
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *frodo = w.unit("Frodo", 'A', 400, 400);
	StealthUpdate *stealth = StealthUpdate::of(*frodo);
	OneRingPenaltyUpdate *ring = dynamic_cast<OneRingPenaltyUpdate *>(frodo->findModule("OneRingPenaltyUpdate"));
	REQUIRE(ring != nullptr);
	stealth->toggleRing();
	for (int i = 0; i < 40 && ring->eye() == INVALID_ID; ++i)
	{
		w.frames(1);
	}
	REQUIRE(ring->eye() != INVALID_ID);
	stealth->toggleRing();
	for (int i = 0; i < 30 && ring->eye() != INVALID_ID; ++i)
	{
		w.frames(1);
	}
	CHECK(ring->eye() == INVALID_ID);
	CHECK(ring->penaltyFrame() == 0);
}
