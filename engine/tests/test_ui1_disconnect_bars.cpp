// OpenBFME unit tests. GPL-3.0.
// Lane UI-1 (S-1123): DisconnectScreen.apt's timeout bars. The movie's SetBarPercent(barIdx, to) evaluates "UIClip.ProgressBars.Bar" + barIdx (a dotted
// target path through GetVariable) and calls SetPercent on it; each bar clip's own frame script defines SetPercent, which scales its `bar` clip's _width.
// SKIP loudly without the retail install.

#include "doctest.h"

#include "AptRetail.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"

#include <string>

namespace
{
AptObject *child(AptObject *o, const char *name)
{
	AptValue v;
	if (!o || !o->getMember(name, v))
	{
		return nullptr;
	}
	return v.isObject() ? v.asObject() : nullptr;
}
} // namespace

TEST_CASE("ui1 retail: DisconnectScreen.apt's timeout bars follow SetBarPercent (S-1123)")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager wm(source, services);
	Shell shell(wm, factories, services, environment);
	wm.init();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm.update(33);
		}
	};
	tick(2);
	AptDisconnectScreen screen(wm, shell);
	std::array<AptDisconnectScreen::Row, 7> rows{};
	rows[0].used = true;
	rows[0].nameUtf8 = "Alice";
	rows[0].barPercent = 100;
	rows[1].used = true;
	rows[1].nameUtf8 = "Bob";
	rows[1].barPercent = 42;
	bool applied = false;
	for (int i = 0; i < 60 && !applied; ++i)
	{
		applied = screen.apply(rows);
		tick(1);
	}
	REQUIRE(applied);
	tick(5);
	CHECK(wm.noteCount("call-without-function") == 0);
	CHECK(wm.noteCount("invoke-failed") == 0);
	AptObject *root = wm.apt().level(screen.level());
	REQUIRE(root != nullptr);
	AptObject *bars = child(child(root, "UIClip"), "ProgressBars");
	REQUIRE(bars != nullptr);
	auto widthOf = [&](int row, const char *clip) {
		AptObject *bar = child(bars, ("Bar" + std::to_string(row)).c_str());
		REQUIRE(bar != nullptr);
		AptObject *c = child(bar, clip);
		REQUIRE(c != nullptr);
		AptValue w;
		REQUIRE(c->getMember("_width", w));
		return w.toNumber();
	};
	// every bar's SetPercent (its frame script) scales its `bar` clip to percent * barFullSize._width / 100
	const float full = widthOf(0, "barFullSize");
	REQUIRE(full > 1.0f);
	CHECK(widthOf(0, "bar") == doctest::Approx(full).epsilon(0.001));
	CHECK(widthOf(1, "bar") == doctest::Approx(full * 0.42f).epsilon(0.001));
	// the row's text: SetPercent's textPercent
	AptValue text;
	REQUIRE(child(bars, "Bar1")->getMember("textPercent", text));
	CHECK(text.toString() == "42%");
	// a later value (RW 0x91934F only sends a change) moves the bar both ways: the width is absolute, not a factor of the last one
	rows[1].barPercent = 7;
	REQUIRE(screen.apply(rows));
	tick(1);
	CHECK(widthOf(1, "bar") == doctest::Approx(full * 0.07f).epsilon(0.001));
	rows[1].barPercent = 90;
	REQUIRE(screen.apply(rows));
	tick(1);
	CHECK(widthOf(1, "bar") == doctest::Approx(full * 0.90f).epsilon(0.001));
	// the unused rows were set to 0 at load (each bar's frame script calls SetPercent(0)): RW 0xB03197 turns 0 into 1e-4 and stops the scale at its minimum,
	// 1.132257342338562 % (RW 0xD05820)
	AptObject *unused = child(child(bars, "Bar2"), "bar");
	REQUIRE(unused != nullptr);
	AptValue xs;
	REQUIRE(unused->getMember("_xscale", xs));
	CHECK(xs.toNumber() == doctest::Approx(1.132257342338562f).epsilon(1e-5));
}
