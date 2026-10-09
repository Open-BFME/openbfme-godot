// OpenBFME unit tests. GPL-3.0.
// Shared retail fixture of the START-1 tests (starting a skirmish): the pure 2.01 mount, the retail object world (loaded once, about 12 s), the
// MapObjectOptions and GameLogicSettings every LiveGame needs.  The tests SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.

#pragma once

#include "RetailTestMount.h"

#include "GameClient/MapObjectDrawables.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <memory>
#include <string>

namespace starttest
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	MapObjectOptions options;
	GameLogicSettings settings;
	std::string error;
};
// nullptr when the install variables are unset (the caller prints the SKIP); a failure to load is a doctest failure at the caller
Shared *shared();
// OPENBFME_REQUIRE_START also selects the world's stores (TheCommandStore, ...) for the scope: another shared world loaded since (hudtest::shared())
// would otherwise stay current
} // namespace starttest

#define OPENBFME_REQUIRE_START(var)                                                         \
	starttest::Shared *var = starttest::shared();                                           \
	if (!var)                                                                               \
	{                                                                                       \
		retailtest::printSkip("a START-1 retail test (" __FILE__ ")");                     \
		return;                                                                             \
	}                                                                                       \
	REQUIRE_MESSAGE(var->mount->fs != nullptr, var->mount->error);                          \
	REQUIRE_MESSAGE(var->world != nullptr, var->error);                                     \
	auto var##Context = var->world->enterContext()
