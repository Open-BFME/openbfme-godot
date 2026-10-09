// OpenBFME unit tests (lane PHYS-1, review r3): the spell rank (PlayerRankInfo, GameLogic/RankInfo.h) and the horde formation rank (RankInfo, Module/HordeContain.h)
// are two different types; this translation unit includes both headers, so a second global RankInfo is a compile error here instead of an ODR violation at link time.
#include "doctest.h"

#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/RankInfo.h"

#include <type_traits>

TEST_CASE("phys1: the spell rank and the horde formation rank are distinct types (review r3 ODR fix)")
{
	CHECK_FALSE(std::is_same<PlayerRankInfo, RankInfo>::value);
	PlayerRankInfo spellRank;
	CHECK(spellRank.getFinalOverride() == &spellRank); // a fresh rank has no override (the r3 sanitizer failure read a garbage next-override pointer)
	RankInfo hordeRank;
	(void)hordeRank;
}
