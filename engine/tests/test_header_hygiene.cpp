// OpenBFME unit tests: the raw INI bit mask aliases (GameLogic/BitFlags.h) and the canonical model condition flags (Common/ModelState.h) live in
// one translation unit. GPL-3.0. Lane WEAPON-1 review: both headers once defined `ModelConditionFlags`; the raw 608-bit array alias is
// `ModelConditionMask` now, so any unit may include both.

#include "doctest.h"

#include "Common/ModelState.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ThingCombatSets.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

#include <type_traits>

static_assert(!std::is_same<ModelConditionFlags, ModelConditionMask>::value, "the canonical flags and the raw array alias are different types");
static_assert(std::tuple_size<ModelConditionMask>::value == 19, "608 bits in 19 words");

TEST_CASE("headers: ModelConditionFlags (ModelState.h) and ModelConditionMask (BitFlags.h) coexist in one translation unit")
{
	ModelConditionFlags flags;
	ModelConditionMask mask{};
	CHECK_FALSE(flags.any());
	CHECK(mask[0] == 0u);
	WeaponTemplateSet set; // its OnlyInCondition masks are raw arrays
	CHECK(set.m_onlyInCondition[0] == mask);
	BitFlagsSet(mask, 3);
	CHECK(BitFlagsTest(mask, 3));
}
