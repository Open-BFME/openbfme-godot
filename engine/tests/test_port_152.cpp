// OpenBFME. GPL-3.0. The three CommandButton fields the command bar reads in RotWK 2.01 —
// ToggleButtonName (+0x48) at RW 0x943E67 .. 0x943E91, RequiresValidContainer (+0x107) at RW 0x943F75 .. 0x943F91,
// and the WeaponSlotToggle1/2/3 (+0x84/+0x88/+0x8C) image of the 0x800000 Options bit, the image index RW 0x75D22E writes at RW 0x943E91.
// Everything comes from a synthetic INI: no game or mod files.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "HudTestUtil.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/SpellStores.h"

namespace
{
// The buttons under test, written into one synthetic CommandButton / CommandSet block set (a duplicate name under load type 1 is parsed
// into a throw-away button and discarded, RW 0x5DA7C5, so every case writes its own values instead of an override block).
struct Buttons
{
	std::string weaponCommand = "TOGGLE_WEAPON";     // RW 0x75D22E compares +0x14 with 0x23 and 0x30
	std::string weaponOptions = "TOGGLE_IMAGE_ON_WEAPON"; // the 0x800000 bit (RW 0xDA4C88 index 23)
	std::string weaponToggleName;                    // ToggleButtonName, RW 0x943E67 reads +0x48
	std::string weaponSlot1 = "SECONDARY", weaponSlot2 = "TERTIARY", weaponSlot3 = "QUATERNARY"; // RW 0x75D22E +0x84 / +0x88 / +0x8C
	bool door = true;
	std::string doorRequires = "Yes";                // RequiresValidContainer, RW 0x943F75 reads +0x107
};

std::string buttonsIni(const char *kindOf, const char *contain, const Buttons &b, const char *setBody)
{
	std::string s =
		"CommandButton Port152Weapon\n"
		"  Command = " + b.weaponCommand + "\n"
		"  Options = " + b.weaponOptions + "\n"
		"  InPalantir = Yes\n"
		"  ButtonImage = SlotZero SlotOne SlotTwo SlotThree\n"
		"  TextLabel = Zero One Two Three\n";
	if (!b.weaponToggleName.empty()) s += "  ToggleButtonName = " + b.weaponToggleName + "\n";
	if (!b.weaponSlot1.empty()) s += "  WeaponSlotToggle1 = " + b.weaponSlot1 + "\n";
	if (!b.weaponSlot2.empty()) s += "  WeaponSlotToggle2 = " + b.weaponSlot2 + "\n";
	if (!b.weaponSlot3.empty()) s += "  WeaponSlotToggle3 = " + b.weaponSlot3 + "\n";
	s +=
		"End\n"
		"CommandButton Port152Named\n"
		"  Command = TOGGLE_WEAPON\n"
		"  Options = TOGGLE_IMAGE_ON_WEAPON\n"
		"  ButtonImage = PickedZero PickedOne\n" // two images only: index 1 is the last one (RW 0x75D19E: null past the end)
		"  TextLabel = Picked\n"
		"  WeaponSlotToggle1 = SECONDARY\n"
		"End\n";
	if (b.door)
	{
		s +=
			"CommandButton Port152Door\n"
			"  Command = EXIT_CONTAINER\n"
			"  InPalantir = Yes\n"
			"  ButtonImage = Door\n"
			"  RequiresValidContainer = " + b.doorRequires + "\n"
			"End\n";
	}
	s +=
		"CommandSet Port152Set\n" + std::string(setBody) + "End\n"
		"Object Port152Unit\n"
		"  KindOf = HERO SELECTABLE " + std::string(kindOf) + "\n"
		"  CommandSet = Port152Set\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n" + std::string(contain) + "End\n";
	return s;
}

struct ToggleRig
{
	movetest::MoveWorld world{10, 10};
	SpellStores spells{world.w.keys};
	CommandStore commands;
	CommandStore *saved;
	PinholeView view{{0, -400, 500}, {0, 0, 0}, 1024, 768, 0.7f};
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*world.logic, world.ai.get(), view, ui, stream, &commands, {}};
	ControlBar bar{ctx};
	Object *unit = nullptr;

	ToggleRig(const char *kindOf, const char *contain, const Buttons &b, const char *setBody)
	{
		saved = TheCommandStore;
		TheCommandStore = &commands;
		spells.install();
		SpellStores::registerBlocks(world.w.fx.env.blocks);
		world.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		world.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string err = world.w.load(buttonsIni(kindOf, contain, b, setBody));
		REQUIRE_MESSAGE(err.empty(), err);
		unit = world.make("Port152Unit", world.teamOf("Alice"));
		REQUIRE(unit);
		world.players.setLocalPlayer(unit->getControllingPlayer());
		ui.selectObject(unit->getID());
		bar.update();
	}
	~ToggleRig() { TheCommandStore = saved; }
	// the bar entry of the command set slot `slot`, wherever the bar shows it
	const ControlBarButton *shown(int slot) const
	{
		for (const auto *v : {&bar.palantirButtons(), &bar.sideButtons(), &bar.offBarButtons()})
		{
			for (const auto &cb : *v)
			{
				if (cb.slot == slot)
				{
					return &cb;
				}
			}
		}
		return nullptr;
	}
	const CommandButton *button(const char *name) const { return commands.findCommandButton(name); }
	// the status RW 0x943E59 asks for before the ToggleButtonName lookup (index in TheObjectStatusNames, RW 0xD8AFF0 = 0x64)
	void toggled(bool on) { unit->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("COMMAND_BUTTON_TOGGLED"), on); bar.update(); }
};
} // namespace

TEST_CASE("PORT-152 weapon toggle image: the KindOf picks WeaponSlotToggle1/2/3 (RW 0x75D22E 0x800000 arm, RW 0x75D2BB .. 0x75D319, read at RW 0x943E91)")
{
	SUBCASE("no special KindOf: WeaponSlotToggle1")
	{
		ToggleRig r("", "", Buttons{}, "  1 = Port152Weapon\n");
		const ControlBarButton *b = r.shown(0);
		REQUIRE(b);
		CHECK(b->button == r.button("Port152Weapon"));
		CHECK(b->image == "SlotOne");
		CHECK(b->textLabel == "One");
	}
	SUBCASE("KindOf CRITTER_EMITTER (RW 0xDA0E68 bit 0x91): WeaponSlotToggle2")
	{
		ToggleRig r("CRITTER_EMITTER", "", Buttons{}, "  1 = Port152Weapon\n");
		const ControlBarButton *b = r.shown(0);
		REQUIRE(b);
		CHECK(b->image == "SlotTwo");
		CHECK(b->textLabel == "Two");
	}
	SUBCASE("KindOf SALT_LICK (RW 0xDA0E68 bit 0x92): WeaponSlotToggle3")
	{
		ToggleRig r("SALT_LICK", "", Buttons{}, "  1 = Port152Weapon\n");
		const ControlBarButton *b = r.shown(0);
		REQUIRE(b);
		CHECK(b->image == "SlotThree");
		CHECK(b->textLabel == "Three");
	}
	SUBCASE("a slot past the ButtonImage list: the image is empty (RW 0x75D19E: null past the end)")
	{
		Buttons b;
		b.weaponSlot3 = "QUINARY"; // 4, and the button has four images
		ToggleRig r("SALT_LICK", "", b, "  1 = Port152Weapon\n");
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->image.empty());
		CHECK(c->textLabel == "Three"); // the labels stop at the last one
	}
	SUBCASE("the image index is the button's own WeaponSlotToggle1")
	{
		Buttons b;
		b.weaponSlot1 = "PRIMARY";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->image == "SlotZero");
	}
}

TEST_CASE("PORT-152 weapon toggle image: without the 0x800000 Options bit or without Command TOGGLE_WEAPON RW 0x75D22E writes nothing and the first image stays")
{
	SUBCASE("no 0x800000 option")
	{
		Buttons b;
		b.weaponOptions = "None";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->image == "SlotZero");
		CHECK(c->textLabel == "Zero"); // the first labels too (the bar takes ButtonImage[0] / TextLabel[0], RW 0x71CF3E)
	}
	SUBCASE("another Command")
	{
		Buttons b;
		b.weaponCommand = "FIRE_WEAPON";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->image == "SlotZero");
	}
}

TEST_CASE("PORT-152 ToggleButtonName: the bar shows the button of that name in the toggle's slot (RW 0x943E67, 0x943E7C -> 0x71D6EA, 0x943E91)")
{
	SUBCASE("ToggleButtonName is read")
	{
		Buttons b;
		b.weaponToggleName = "Port152Named";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		r.toggled(true); // RW 0x943E59: the toggle is on
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->button == r.button("Port152Named"));
		CHECK(c->image == "PickedOne"); // the named button's own WeaponSlotToggle1 = SECONDARY
		CHECK(c->textLabel == "Picked");
	}
	SUBCASE("with the status off the lookup does not happen: the button itself (RW 0x943E65 skips the whole block)")
	{
		Buttons b;
		b.weaponToggleName = "Port152Named";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->button == r.button("Port152Weapon"));
		CHECK(c->image == "SlotOne");
	}
	SUBCASE("an empty ToggleButtonName keeps the button itself (RW 0x943E6C: the empty string skips the lookup)")
	{
		ToggleRig r("", "", Buttons{}, "  1 = Port152Weapon\n");
		r.toggled(true);
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->button == r.button("Port152Weapon"));
		CHECK(c->image == "SlotOne");
	}
	SUBCASE("a name that no button has keeps the button itself (RW 0x943E85: the lookup's null leaves the button)")
	{
		Buttons b;
		b.weaponToggleName = "Port152Missing";
		ToggleRig r("", "", b, "  1 = Port152Weapon\n");
		r.toggled(true);
		const ControlBarButton *c = r.shown(0);
		REQUIRE(c);
		CHECK(c->button == r.button("Port152Weapon"));
		CHECK(c->image == "SlotOne");
	}
}

TEST_CASE("PORT-152 RequiresValidContainer: no button while the object's OpenContain Enabled is off (RW 0x943F75 .. 0x943F91)")
{
	const char *kOpen = "  Behavior = TransportContain ModuleTag_Contain\n    Slots = 5\n    Enabled = Yes\n  End\n";
	const char *kClosed = "  Behavior = TransportContain ModuleTag_Contain\n    Slots = 5\n    Enabled = No\n  End\n";
	const char *kSet = "  1 = Port152Weapon\n  2 = Port152Door\n";
	SUBCASE("Enabled = Yes: the button is shown")
	{
		ToggleRig r("", kOpen, Buttons{}, kSet);
		REQUIRE(r.shown(1));
		CHECK(r.shown(1)->button == r.button("Port152Door"));
		CHECK(r.shown(0)); // the rest of the set is untouched
	}
	SUBCASE("Enabled = No: only that button is missing, the rest of the bar stays")
	{
		ToggleRig r("", kClosed, Buttons{}, kSet);
		CHECK(r.shown(1) == nullptr);
		REQUIRE(r.shown(0));
		CHECK(r.shown(0)->button == r.button("Port152Weapon"));
	}
	SUBCASE("a button without RequiresValidContainer stays while the container is closed (the neighbour)")
	{
		Buttons b;
		b.doorRequires = "No";
		ToggleRig r("", kClosed, b, kSet);
		REQUIRE(r.shown(1));
		CHECK(r.shown(1)->button == r.button("Port152Door"));
	}
	SUBCASE("no container at all: the check has nothing to ask and the button stays (RW 0x943F86: null module data)")
	{
		ToggleRig r("", "", Buttons{}, kSet);
		REQUIRE(r.shown(1));
		CHECK(r.shown(1)->button == r.button("Port152Door"));
	}
}
