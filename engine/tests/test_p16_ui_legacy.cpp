// OpenBFME tests: P16 must preserve the legacy lobby's persisted shape. GPL-3.0.

#include "StartShellFx.h"

TEST_CASE("p16 ui legacy: kept hero extension remains exactly eight slots on a retail map")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	MemorySkirmishProfiles profiles;
	REQUIRE(profiles.add(u"Legacy"));
	REQUIRE(profiles.setCurrent(u"Legacy"));
	fx.environment.profiles = &profiles;
	fx.shell->push("Skirmish.apt");
	fx.tick(90);
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	REQUIRE(screen->setup());
	REQUIRE(screen->setup()->info().mapStartCount == LEGACY_MAX_SLOTS);
	fx.shell->pop();
	fx.tick(30);
	CHECK(profiles.profileValue(0, "OpenBFMEHeroes") == "-,-,-,-,-,-,-,-");
}
