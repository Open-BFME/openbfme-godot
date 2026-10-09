// OpenBFME. GPL-3.0.
// Lane STEALTH-1 binary facts (SKIP when RW_GAME_DAT is unset): the InvisibilityManager / nugget port against the RotWK binary (the name run, the forest range, the
// statuses and model conditions the code pushes, the detector's random draw, the manager's period). The retail-data scenarios are test_hud_stealth_retail.cpp.

#include "doctest.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include <cstdint>
#include <string>

TEST_CASE("stealth binary facts: the nugget name run, the forest range, the statuses and conditions the manager uses (RW 0xDA7484, 0xBD88C4, 0x81B01C, 0x68FC07)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("stealth binary facts (RW_GAME_DAT unset)");
		return;
	}
	// the run of name pointers RW 0xDA7484 .. 0xDA74D4 (NULL)
	const char *const *run = InvisibilityNugget::typeNames();
	for (unsigned i = 0;; ++i)
	{
		const std::uint32_t p = pe->u32At(0xDA7484 + 4 * i);
		if (!run[i])
		{
			CHECK(p == 0u);
			break;
		}
		REQUIRE(p != 0u);
		CHECK(pe->cstring(p) == run[i]);
	}
	CHECK(InvisibilityNugget::forbiddenNames() == run + 2); // RW 0xDA748C
	CHECK(InvisibilityNugget::optionNames() == run + 12);   // RW 0xDA74B4
	const char *const *levels = StealthUpdateModuleData::forbiddenConditionNames(); // RW 0xDA5524
	for (unsigned i = 0; levels[i]; ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xDA5524 + 4 * i)) == levels[i]);
	}
	// the forest range 50.0 (RW 0xBD88C4) and the camouflage scan's 3D squared test against (multiplier * range)^2
	CHECK(pe->hex(0xBD88C4, 4) == "00004842");
	// RW 0x81B01C: push 0x60 (INVISIBLE_DETECTED); RW 0x81B026: push 0x5F (INVISIBLE_DETECTED_BY_FRIEND)
	CHECK(pe->hex(0x81B01B, 3) == "536a60");
	CHECK(pe->hex(0x81B025, 3) == "536a5f");
	CHECK(CombatNames::status("INVISIBLE_DETECTED") == 0x60);
	CHECK(CombatNames::status("INVISIBLE_DETECTED_BY_FRIEND") == 0x5F);
	// RW 0x68FC07 / 0x68FC1B: model conditions 0x222 / 0x223; RW 0x81B970: 0x220
	CHECK(pe->hex(0x68FC07, 5) == "6822020000");
	CHECK(pe->hex(0x68FC1B, 5) == "6823020000");
	CHECK(pe->hex(0x81B970, 5) == "6820020000");
	CHECK(CombatNames::modelCondition("INVISIBLE_STEALTH") == 0x222);
	CHECK(CombatNames::modelCondition("INVISIBLE_CAMOUFLAGE") == 0x223);
	CHECK(CombatNames::modelCondition("BURNINGDEATH") == 0x220);
	// RW 0x8A6398: the detector's first wake GameLogicRandomValue(1, DetectionRate) at StealthDetectorUpdate.cpp line 0x4F
	CHECK(pe->hex(0x8A6398, 2) == "6a4f");
	// RW 0x81BEA6: the manager's period is LOGICFRAMES_PER_SECOND ([0xD9F608] = 5)
	CHECK(pe->hex(0x81BEA6, 6) == "8b0d08f6d900");
	CHECK(pe->u32At(0xD9F608) == 5u);
}
