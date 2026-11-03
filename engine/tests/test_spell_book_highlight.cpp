// OpenBFME. GPL-3.0.
// Shared BFME2/RotWK presentation regression: newly available powers flash by identity, even when the compact bar moves existing powers.
// The in-memory movie records the controller's calls; it does not stand in for retail-script or simulation parity.

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace apttest;

struct SpellHighlightRig
{
	MemorySource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	std::unique_ptr<WindowManager> windows;
	std::unique_ptr<Shell> shell;
	std::unique_ptr<AptPalantir> palantir;
	std::vector<int> flashes;

	SpellHighlightRig()
	{
		TestMovie level0;
		level0.setRootFrames({ {} });
		source.add("AptLevel0", level0);
		TestMovie movie;
		movie.addCharacter(0);
		const auto script = program(movie, [](Asm &a) {
			a.defineFunction("SetButtonState", { "slot", "state" }, [](Asm &) {});
			a.defineFunction("FlashButton", { "slot" }, [](Asm &f) {
				f.pushString("FSCommand:SpellHighlightTest::Flash").getStringVar("slot").opU8(APT_OP_GETURL2, 0);
			});
		});
		const auto sprite = movie.addSprite({ { movie.addActionItem(script) } });
		const auto spriteId = movie.addCharacter(sprite);
		movie.setRootFrames({ { movie.addPlaceItem(placeChar(spriteId, 1, "SpellBookUI")) } });
		source.add("Palantir", movie);
		windows = std::make_unique<WindowManager>(source, services);
		shell = std::make_unique<Shell>(*windows, factories, services, environment);
		windows->registerCommand("SpellHighlightTest::Flash", [this](const std::string &slot) { flashes.push_back(std::stoi(slot)); });
		windows->init();
		palantir = std::make_unique<AptPalantir>(*windows, *shell);
		windows->update(33);
		windows->update(33);
		REQUIRE(windows->invokeCallback("OnAptInGameSpellBookLoaded", "SpellBookUI"));
		shown();
	}

	void shown() { REQUIRE(windows->invokeCallback("OnAptInGameSpellBookShown", "")); }
	void sync(const std::vector<AptPalantir::SpellSlot> &slots)
	{
		flashes.clear();
		palantir->syncSpellBook(slots);
		CHECK(palantir->callErrors().empty());
	}
};

AptPalantir::SpellSlot power(int index, int state = AptPalantir::SPELL_UP)
{
	AptPalantir::SpellSlot slot;
	slot.buttonIndex = index;
	slot.image = "TestPower" + std::to_string(index);
	slot.state = state;
	return slot;
}
} // namespace

TEST_CASE("spell highlight: inserting a newly acquired power flashes it rather than an existing power")
{
	SpellHighlightRig rig;
	rig.sync({});
	std::vector<AptPalantir::SpellSlot> slots;
	for (int index : { 2, 4, 6, 8, 10, 12, 14, 16, 18 })
	{
		slots.push_back(power(index));
	}
	rig.sync(slots);
	int position = 0, newIndex = 1;
	SUBCASE("before every existing power") {}
	SUBCASE("between existing powers") { position = 4; newIndex = 9; }
	SUBCASE("after every existing power") { position = 9; newIndex = 19; }
	slots.insert(slots.begin() + position, power(newIndex));
	rig.sync(slots);
	CHECK(rig.flashes == std::vector<int>{ position + 1 });
	for (int i = 0; i < 10; ++i)
	{
		CHECK(rig.palantir->spellSlotButton(i) == slots[i].buttonIndex);
		const auto *image = rig.palantir->imageFor("InGameSpellBookSpell" + std::to_string(i + 1) + "Image");
		REQUIRE(image);
		CHECK(image->image == slots[i].image);
	}
	rig.sync(slots);
	CHECK(rig.flashes.empty());
}

TEST_CASE("spell highlight: cooldown completion follows a power that moves to another slot")
{
	SpellHighlightRig rig;
	rig.sync({ power(3, AptPalantir::SPELL_NOT_READY), power(10) });
	CHECK(rig.flashes.empty());
	rig.sync({ power(2, AptPalantir::SPELL_NOT_READY), power(3), power(10) });
	CHECK(rig.flashes == std::vector<int>{ 2 });
	rig.sync({ power(2), power(3), power(10) });
	CHECK(rig.flashes == std::vector<int>{ 1 });
	rig.sync({ power(3), power(10) });
	CHECK(rig.flashes.empty());
}

TEST_CASE("spell highlight: initial population and reopening do not flash already owned powers")
{
	SpellHighlightRig rig;
	const std::vector<AptPalantir::SpellSlot> slots{ power(3), power(10, AptPalantir::SPELL_STATIC) };
	rig.sync(slots);
	CHECK(rig.flashes.empty());
	rig.shown();
	rig.sync(slots);
	CHECK(rig.flashes.empty());
	rig.sync({ power(2, AptPalantir::SPELL_STATIC), slots[0], slots[1] });
	CHECK(rig.flashes == std::vector<int>{ 1 });
}
