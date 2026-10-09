// OpenBFME unit tests (lane STEALTH-1): parse-time facts of InvisibilityUpdate / InvisibilityNugget, StealthDetectorUpdate and StealthUpdate, on synthetic objects
// (no retail data). The name runs and defaults are the binary's (see GameLogic/System/InvisibilityManager.h, GameLogic/Module/InvisibilityModules.h).

#include "doctest.h"

#include "ObjectTestUtil.h"

#include "GameLogic/Module/InvisibilityModules.h"

#include <cstring>

using objtest::contains;

namespace
{
struct StealthWorld : objtest::World
{
	StealthWorld() { InvisibilityModules::registerAll(modules); }
	template <class D>
	const D *dataOf(const char *name, const char *cls)
	{
		for (const auto &n : get(name)->behaviorModules().nuggets())
		{
			if (n.name == cls)
			{
				return dynamic_cast<const D *>(n.data.get());
			}
		}
		return nullptr;
	}
};

size_t runLength(const char *const *names)
{
	size_t n = 0;
	while (names[n])
	{
		++n;
	}
	return n;
}
} // namespace

TEST_CASE("InvisibilityNugget: the three name lists are one run (RW 0xDA7484 .. 0xDA74D0, NULL at 0xDA74D4)")
{
	CHECK(runLength(InvisibilityNugget::typeNames()) == 20);
	CHECK(runLength(InvisibilityNugget::forbiddenNames()) == 18);
	CHECK(runLength(InvisibilityNugget::optionNames()) == 8);
	CHECK(std::strcmp(InvisibilityNugget::typeNames()[1], "CAMOUFLAGE") == 0);
	CHECK(std::strcmp(InvisibilityNugget::forbiddenNames()[7], "FIRING_ANY") == 0);
	CHECK(std::strcmp(InvisibilityNugget::optionNames()[3], "UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH") == 0);
	CHECK(InvisibilityNugget::FIRING_ANY == (1u << 7));
	CHECK(InvisibilityNugget::USING_ABILITY == (1u << 9));
	CHECK(runLength(StealthUpdateModuleData::forbiddenConditionNames()) == 13); // RW 0xDA5524
}

TEST_CASE("InvisibilityUpdate: defaults (RW 0x655222 / 0x6540C0) and a nugget parsed as a nested block")
{
	StealthWorld w;
	REQUIRE(w.load("Object A\n  Behavior = InvisibilityUpdate T\n  End\nEnd\n"
	               "Object B\n  Behavior = InvisibilityUpdate T\n"
	               "    InvisibilityNugget\n      InvisibilityType = CAMOUFLAGE\n      DetectionRange = 100.0\n"
	               "      ForbiddenConditions = AWAY_FROM_TREES MOVING FIRING_ANY\n      Options = ALLOW_NEAR_TREES DETECTED_BY_FRIENDLIES\n"
	               "      HintDetectableConditions = IS_FIRING_WEAPON\n    End\n"
	               "    UpdatePeriod = 2000\n    StartsActive = Yes\n    Broadcast = Yes\n    BroadcastRange = 300\n"
	               "    UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea = VoiceMoveToTrees\n  End\nEnd\n")
	          .empty());
	const auto *a = w.dataOf<InvisibilityUpdateModuleData>("A", "InvisibilityUpdate");
	const auto *b = w.dataOf<InvisibilityUpdateModuleData>("B", "InvisibilityUpdate");
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->m_updatePeriod == 10u); // RW 0x65524F: 0xA frames
	CHECK(a->m_nugget.invisibilityType == InvisibilityNugget::NONE);
	CHECK_FALSE(a->m_startsActive);
	CHECK_FALSE(a->m_broadcast);
	CHECK(b->m_nugget.invisibilityType == InvisibilityNugget::CAMOUFLAGE);
	CHECK(b->m_nugget.detectionRange == 100.0f);
	CHECK(b->m_nugget.forbiddenConditions == (InvisibilityNugget::AWAY_FROM_TREES | InvisibilityNugget::MOVING | InvisibilityNugget::FIRING_ANY));
	CHECK(b->m_nugget.options == (InvisibilityNugget::ALLOW_NEAR_TREES | InvisibilityNugget::DETECTED_BY_FRIENDLIES));
	CHECK(b->m_nugget.hintDetectableConditions != ObjectStatusMaskType{});
	CHECK(b->m_updatePeriod == 10u); // 2000 ms at 5 frames per second
	CHECK(b->m_startsActive);
	CHECK(b->m_broadcast);
	CHECK(b->m_broadcastRange == 300.0f);
	CHECK(b->m_voiceMoveToStealthyArea == "VoiceMoveToTrees");
}

TEST_CASE("InvisibilityNugget: every later name of the run is accepted (retail parse acceptance), an unknown name is rejected")
{
	StealthWorld w;
	REQUIRE(w.load("Object A\n  Behavior = InvisibilityUpdate T\n    InvisibilityNugget\n      InvisibilityType = AWAY_FROM_TREES\n"
	               "      ForbiddenConditions = ALLOW_NEAR_TREES\n      Options = SPAWN\n    End\n  End\nEnd\n")
	          .empty());
	const auto *a = w.dataOf<InvisibilityUpdateModuleData>("A", "InvisibilityUpdate");
	REQUIRE(a);
	CHECK(a->m_nugget.invisibilityType == 2);                 // the third name of the run
	CHECK(a->m_nugget.forbiddenConditions == (1u << 10));     // ALLOW_NEAR_TREES is bit 10 of the forbidden run
	CHECK(a->m_nugget.options == (1u << 7));                  // SPAWN is the eighth name of the option run
	CHECK_FALSE(w.load("Object B\n  Behavior = InvisibilityUpdate T\n    InvisibilityNugget\n      InvisibilityType = INVISIBLE\n    End\n  End\nEnd\n", INI_LOAD_OVERWRITE, "b.ini")
	                .empty());
	CHECK_FALSE(w.load("Object C\n  Behavior = InvisibilityUpdate T\n    InvisibilityNugget\n      ForbiddenConditions = STEALTH\n    End\n  End\nEnd\n", INI_LOAD_OVERWRITE, "c.ini")
	                .empty()); // STEALTH comes before the forbidden run
}

TEST_CASE("StealthDetectorUpdate: defaults (RW 0x65511F) and its fields")
{
	StealthWorld w;
	REQUIRE(w.load("Object A\n  Behavior = StealthDetectorUpdate T\n  End\nEnd\n"
	               "Object B\n  Behavior = StealthDetectorUpdate T\n    DetectionRate = 1000\n    DetectionRange = 400\n    InitiallyDisabled = Yes\n"
	               "    CanDetectWhileContained = Yes\n    ExtraForbiddenKindOf = STRUCTURE\n    RequiredUpgrade = Upgrade_Something\n  End\nEnd\n")
	          .empty());
	const auto *a = w.dataOf<StealthDetectorUpdateModuleData>("A", "StealthDetectorUpdate");
	const auto *b = w.dataOf<StealthDetectorUpdateModuleData>("B", "StealthDetectorUpdate");
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->m_detectionRate == 1u);
	CHECK(a->m_detectionRange == 0.0f);
	CHECK_FALSE(a->m_initiallyDisabled);
	CHECK(b->m_detectionRate == 5u);
	CHECK(b->m_detectionRange == 400.0f);
	CHECK(b->m_initiallyDisabled);
	CHECK(b->m_canDetectWhileContained);
	CHECK(b->m_extraForbiddenKindOf != KindOfMaskType{});
	CHECK(b->m_requiredUpgrade == "Upgrade_Something");
}

TEST_CASE("StealthUpdate: the RotWK table (RW 0xC2E968) parses the forest / horde conditions over RW 0xDA5524")
{
	StealthWorld w;
	REQUIRE(w.load("Object A\n  Behavior = StealthUpdate T\n    StealthDelay = 500\n    FriendlyOpacityMin = 50%\n    FriendlyOpacityMax = 100%\n"
	               "    PulseFrequency = 750\n    StealthForbiddenConditions = AWAY_FROM_TREES ATTACKING\n    OrderIdleEnemiesToAttackMeUponReveal = Yes\n"
	               "    DetectedByAnyoneRange = 120\n    RevealWeaponSets = CLOSE_RANGE CONTESTING_BUILDING\n    InnateStealth = No\n  End\nEnd\n")
	          .empty());
	const auto *a = w.dataOf<StealthUpdateModuleData>("A", "StealthUpdate");
	REQUIRE(a);
	CHECK(a->m_stealthDelay == 3u); // ceil(500 * 0.005f)
	CHECK(a->m_stealthForbiddenConditions == ((1u << 9) | (1u << 0)));
	CHECK(a->m_friendlyOpacityMin == doctest::Approx(0.5f));
	CHECK(a->m_detectedByAnyoneRange == 120.0f);
	CHECK(a->m_orderIdleEnemiesToAttackMeUponReveal);
	CHECK(a->m_revealWeaponSets != WeaponConditionFlags{});
	CHECK_FALSE(w.load("Object B\n  Behavior = StealthUpdate T\n    StealthForbiddenConditions = INVISIBLE\n  End\nEnd\n", INI_LOAD_OVERWRITE, "b.ini").empty());
	REQUIRE(w.load("Object C\n  Behavior = StealthUpdate T\n  End\nEnd\n", INI_LOAD_OVERWRITE, "c.ini").empty());
	const auto *c = w.dataOf<StealthUpdateModuleData>("C", "StealthUpdate");
	REQUIRE(c);
	CHECK(c->m_stealthDelay == 0xFFFFFFFFu); // RW 0x777BDA
	CHECK(c->m_startsActive);
	CHECK(c->m_innateStealth);
	CHECK(c->m_friendlyOpacityMin == 0.5f);
	CHECK(c->m_friendlyOpacityMax == 1.0f);
	CHECK(c->m_pulseFrequency == 30u);
}
