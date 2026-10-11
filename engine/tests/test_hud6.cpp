// OpenBFME tests, lane HUD-6: RotWK's radial command bubbles over a structure (RW 0x9D6320 / 0x9D5CCE / 0x9457FE), the side command bar's frame
// (SetButtonState, RW 0x92F082 / 0x92F015, gate RW 0x92F27D) and the help box (RW 0x807A81, InGameCommandButtonHelp RW 0x974EEB / 0x9744E4 / 0x974795,
// InGameHelpBoxMovieClip RW 0x92E462). GPL-3.0.
// The retail tests SKIP loudly without the installs; the layout and text cases need nothing.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHelpBox.h"
#include "GameClient/InGameHud.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
std::vector<RadialMenuLayout::Rect> ring(int n, int w, int h, int cx, int cy)
{
	RadialMenuLayout l;
	l.setCount(n);
	l.updateCenter(cx, cy);
	l.update(w, h);
	std::vector<RadialMenuLayout::Rect> out;
	l.place(out);
	return out;
}
// the centre of a button rectangle
int midX(const RadialMenuLayout::Rect &r) { return r.x + r.w / 2; }
int midY(const RadialMenuLayout::Rect &r) { return r.y + r.h / 2; }

struct MapText : GameTextSource
{
	std::map<std::string, std::u16string> t;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = t.find(label);
		if (it == t.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}
};
} // namespace

TEST_CASE("hud6 radial layout: RotWK's button size, radius and ring (RW 0x9D6320 / 0x9D5CCE)")
{
	RadialMenuLayout l;
	l.setCount(3);
	l.update(1024, 768);
	// ftol(1024 * 0.046875) x ftol(768 * 0.0625); radius max(ftol(24 * 1.175 / sin(pi / 3)) = 32, 48 * 0.6666667 = 32.0)
	CHECK(l.buttonW() == 48);
	CHECK(l.buttonH() == 48);
	CHECK(l.radius() == 32);
	// the first straight above the centre, then clockwise by 2 pi / 3 (the fortress's three bubbles of the owner's shot: top, lower right, lower left)
	std::vector<RadialMenuLayout::Rect> r = ring(3, 1024, 768, 500, 400);
	REQUIRE(r.size() == 3);
	CHECK(r[0].x == 476);
	CHECK(r[0].y == 344);
	CHECK(midX(r[0]) == 500);
	CHECK(midY(r[0]) == 368);
	CHECK(midX(r[1]) == 527);
	CHECK(midY(r[1]) == 416);
	CHECK(midX(r[2]) == 472);
	CHECK(midY(r[2]) == 416);
	// seven heroes (the owner's second shot): radius ftol(28.2 / sin(pi / 7)) = 64
	RadialMenuLayout seven;
	seven.setCount(7);
	seven.update(1024, 768);
	CHECK(seven.radius() == 64);
	// one button sits on the centre; two: above and below at 2/3 of the width
	r = ring(1, 1024, 768, 300, 200);
	REQUIRE(r.size() == 1);
	CHECK(midX(r[0]) == 300);
	CHECK(midY(r[0]) == 200);
	r = ring(2, 1024, 768, 300, 200);
	REQUIRE(r.size() == 2);
	CHECK(midY(r[0]) == 168);
	CHECK(midY(r[1]) == 232);
	// the size follows the display: 1600 x 900 -> 75 x 56
	RadialMenuLayout wide;
	wide.setCount(4);
	wide.update(1600, 900);
	CHECK(wide.buttonW() == 75);
	CHECK(wide.buttonH() == 56);
	// the centre follows only a move of more than 2 pixels (RW 0x9D5B77)
	RadialMenuLayout c;
	c.setCount(3);
	CHECK(c.updateCenter(100, 100));
	CHECK_FALSE(c.updateCenter(101, 101));
	CHECK(c.centerX() == 100);
	CHECK(c.updateCenter(102, 101));
	CHECK(c.centerX() == 102);
}

TEST_CASE("hud6 help: the name's '&' key, the shortcut line, the measured height and the drawn lines (RW 0x97434E / 0x9744E4 / 0x974795)")
{
	MapText text;
	text.t["TOOLTIP:Shortcut"] = u"Shortcut: %s";
	CommandButtonHelp::Text t;
	t.name = u"Morgu&l Blade";
	t.description = u"Requires Level 4 \n Targeted enemy takes poison damage over \n time and becomes a wight upon death";
	CommandButtonHelp help(t, &text, nullptr, nullptr);
	CHECK(help.name() == u"Morgul Blade");
	CHECK(help.shortcutKey() == u'L');
	HelpBoxSettings settings;
	settings.name = { "Albertus MT", 16, false, 0xFFFFFFFFu };
	settings.cost = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.shortcut = { "Albertus MT", 14, false, 0xFFFFCC00u };
	settings.description = { "Albertus MT", 12, false, 0xFFFFCC00u };
	DefaultFontMetrics metrics; // fontHeight = size + 4
	const int height = help.setWidthAndComputeHeight(400, settings, 1.0f, 1.0f, 1.0f, metrics);
	// name 20 + shortcut 18 + three description lines of 16
	CHECK(height == 20 + 18 + 3 * 16);
	std::vector<HelpDrawOp> ops;
	help.render(100.0f, 50.0f, 400.0f, (float)height, ops);
	std::vector<std::string> lines;
	for (const HelpDrawOp &op : ops)
	{
		if (op.kind == HelpDrawOp::TEXT)
		{
			lines.push_back(op.text);
		}
	}
	REQUIRE(lines.size() == 5);
	CHECK(lines[0] == "Morgul Blade");
	CHECK(lines[1] == "Shortcut: L");
	CHECK(lines[2] == "Requires Level 4");
	CHECK(lines[3] == "Targeted enemy takes poison damage over");
	CHECK(lines[4] == "time and becomes a wight upon death");
	// the name is centred in the box, the description lines below the shortcut line
	CHECK(ops[0].y == doctest::Approx(50.0f));
	CHECK(ops[2].y == doctest::Approx(50.0f + 20.0f + 18.0f));
}

TEST_CASE("hud6 help box: the movie clip's states and the provider's delay and hide (RW 0x92E462 / 0x92E2AC / 0x807848 / 0x71FC08)")
{
	std::vector<std::string> calls;
	InGameHelpBox box;
	box.loaded("_level0.helpBox", [&](const std::string &path, const std::string &fn, const std::vector<std::string> &args) {
		calls.push_back(path + "." + fn + (args.empty() ? std::string() : "(" + args[0] + ")"));
		return true;
	});
	CHECK(box.renderName() == "_level0.helpBox_Content");
	auto measure = [](CommandButtonHelp &, int width) { return width / 4; };
	box.update(measure, 0.5f); // state 0 -> SampleContentWidth
	REQUIRE(calls.size() == 1);
	CHECK(calls[0] == "helpBox.SampleContentWidth");
	std::vector<HelpDrawOp> ops;
	box.render(10.0f, 20.0f, 299.6f, 50.0f, ops); // the sampled width: (int)(299.6 + 0.5)
	CHECK(box.sampledWidth() == 300);
	box.update(measure, 0.5f); // state 1 with a width -> Hide
	CHECK(calls.back() == "helpBox.Hide");
	CHECK(box.state() == 2);
	// a provider is kept, then shown once the delay passed (RW 0x8078E0: now > start + delay)
	InGameHelpBox::Provider p{ 2, 7, nullptr };
	int composed = 0;
	auto compose = [&]() {
		++composed;
		return std::make_shared<CommandButtonHelp>();
	};
	box.beginFrame();
	box.showHelp(p, 1000, 1, compose);
	CHECK(composed == 0);
	box.beginFrame();
	box.showHelp(p, 1001, 1, compose);
	CHECK(composed == 0); // 1001 <= 1000 + 1
	box.beginFrame();
	box.showHelp(p, 1002, 1, compose);
	CHECK(composed == 1);
	box.update(measure, 0.5f); // state 2 with a help -> Show(75 * 0.5)
	CHECK(calls.back() == "helpBox.Show(37.500000)");
	CHECK(box.isShown());
	// a frame without a hover: the box hides after 5 updates
	box.beginFrame();
	box.beginFrame();
	CHECK(box.state() == 4);
	for (int i = 0; i < 5; ++i)
	{
		box.update(measure, 0.5f);
	}
	CHECK(box.state() == 2);
	CHECK(calls.back() == "helpBox.Hide");
}

namespace
{
struct Hud6Rig
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	std::unique_ptr<InGameHud> hud;
	explicit Hud6Rig(SharedWorld &s, const char *faction) : rig(s, "map mp fall back 4p", faction)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		imageEnv.fileSystem = s.mount->fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		hud->setMappedImages(&images);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~Hud6Rig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void select(Object *o)
	{
		hud->input().ui().deselectAll(true);
		hud->input().ui().selectObject(o->getID());
	}
	// a left click through the HUD's raw input: the selection reaches the logic (MSG_CREATE_SELECTED_GROUP), as test_hero_hud.cpp's rig does
	void clickSelect(Object *o)
	{
		rig.lookAt(*o->getPosition());
		frames(5);
		const ICoord2D px = rig.screenOf({ o->getPosition()->x, o->getPosition()->y, 10.0f });
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, rig.timeMs += 40);
		frames(20);
	}
};
} // namespace

TEST_CASE("hud6 retail: a structure's Radial buttons ring over it, a builder's go to the side bar's frames (SetButtonState) and the help box shows")
{
	if (!haveWorld("hud6 radial and side bar"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(30);
	REQUIRE(h.hud->palantir());
	for (const std::string &l : h.hud->stops())
	{
		if (l.rfind("[S-2703] help box settings", 0) == 0 || l.rfind("[S-2703] help box delay", 0) == 0)
		{
			FAIL_CHECK(l);
		}
	}
	REQUIRE(h.hud->helpBoxSettings().loaded);
	{
		// the lane's stops are reported (docs/STOPS.md S-2700 .. S-2703)
		const std::vector<std::string> stops = h.hud->stops();
		for (const char *id : { "[S-2700]", "[S-2701]", "[S-2702]", "[S-2703]" })
		{
			CHECK_MESSAGE(std::any_of(stops.begin(), stops.end(), [id](const std::string &l) { return l.rfind(id, 0) == 0; }), id);
		}
	}
	CHECK(h.hud->helpBoxSettings().description.pointSize == 12); // Language.ini's HelpBoxDescriptionFont replaces InGameUI's 14
	CHECK(h.hud->tooltipDelayMs() == 1);                         // ControlBar.wnd:ButtonCommand TOOLTIPDELAY
	// the help box movie came with the Palantir (OnHelpBoxLoaded)
	CHECK(h.hud->helpBox().isLoaded());
	CHECK(h.hud->helpBox().renderName().find("_Content") != std::string::npos);

	// a builder: its Radial buttons fill the side bar's frames, each shown by SetButtonState(i, "_show")
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *builder = h.rig.make("MenPorter", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(builder);
	h.frames(40);
	const size_t sideButtons = h.hud->controlBar().sideButtons().size();
	REQUIRE(sideButtons > 0);
	CHECK(h.hud->palantir()->sideBarShown());
	CHECK(h.hud->palantir()->sideBarCount() == (int)std::min<size_t>(sideButtons, 12));
	const std::vector<std::string> &calls = h.hud->palantir()->sideBarCalls();
	CHECK(std::find(calls.begin(), calls.end(), "0 _show") != calls.end());
	CHECK(h.hud->radialMenu().ops().empty()); // a DOZER's ring sits at (-999, -999)

	// a structure: the side bar hides, its Radial buttons ring over it
	const Coord3D c2 = h.rig.freeSpot(2400, 1400, 250.0f);
	Object *pit = h.rig.make("GondorBarracks", c2.x, c2.y);
	h.rig.lookAt({ c2.x, c2.y, 0 });
	h.frames(10);
	h.select(pit);
	h.frames(40);
	CHECK_FALSE(h.hud->palantir()->sideBarShown());
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	const size_t n = h.hud->controlBar().sideButtons().size();
	REQUIRE(n > 0);
	REQUIRE(radial.buttons().size() == n);
	CHECK(radial.layout().buttonW() == 48);
	// the ring's centre is the structure's projected point; the first button straight above it
	CHECK(radial.layout().centerX() > 0);
	if (n >= 2)
	{
		CHECK(radial.buttons()[0].rect.x + radial.buttons()[0].rect.w / 2 == radial.layout().centerX());
		CHECK(radial.buttons()[0].rect.y < radial.layout().centerY());
	}
	bool border = false;
	for (const RadialDrawOp &op : radial.ops())
	{
		border = border || (op.kind == RadialDrawOp::OVERLAY && op.image == "RadialBorder");
	}
	CHECK(border);
	CHECK(radial.errors().empty());

	// the pointer on the first bubble: RadialOver, the help box samples its width, then shows the button's help
	const ControlBarRadialMenu::Button first = radial.buttons()[0];
	const int px = first.rect.x + first.rect.w / 2, py = first.rect.y + first.rect.h / 2;
	h.hud->mouseMove(px, py, 0);
	h.frames(2);
	CHECK(radial.hilitedSlot() == first.slot);
	bool over = false;
	for (const RadialDrawOp &op : radial.ops())
	{
		over = over || (op.kind == RadialDrawOp::OVERLAY && op.image == "RadialOver");
	}
	CHECK(over);
	CHECK(h.hud->isOverGui(px, py));
	// the device's render of the content clip gives the sampled width (state 1)
	std::vector<HelpDrawOp> ops;
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 40.0f, ops);
	h.frames(6);
	CHECK(h.hud->helpBox().isShown());
	REQUIRE(h.hud->helpBox().help() != nullptr);
	{
		// its frame is PalantirExport's images, imported (S-2704): the top's fill resolves to apt_PalantirExport_2.tga
		AptRenderList rl;
		h.hud->apt().buildRenderList(rl);
		bool top = false;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.path.find("helpBox.box.top.instance1") != std::string::npos)
			{
				for (const AptRenderFill &f : c.fills)
				{
					top = top || (f.imageResolved && f.textureName == "apt_PalantirExport_2.tga");
				}
			}
		}
		CHECK(top);
	}
	CHECK_FALSE(h.hud->helpBox().help()->name().empty());
	ops.clear();
	h.hud->helpBoxRender(400.0f, 500.0f, 360.0f, 120.0f, ops);
	CHECK_FALSE(ops.empty());
	// the pointer leaves: the box hides soon
	h.hud->mouseMove(5, 5, 0);
	h.frames(10);
	CHECK_FALSE(h.hud->helpBox().isShown());
}

TEST_CASE("hud6 retail: the Mordor fortress rings its three bubbles (the spire's fireball hides while disabled, RW 0x943B39) and its hero bubble opens the hero ring")
{
	if (!haveWorld("hud6 mordor fortress"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMordor");
	h.frames(10);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *citadel = h.rig.make("MordorFortressCitadel", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(citadel);
	h.frames(30);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	std::vector<std::string> names;
	const ControlBarRadialMenu::Button *heroes = nullptr;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		names.push_back(b.source.button ? b.source.button->m_name : std::string());
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.source.button->m_name.find("Revivables") != std::string::npos)
		{
			heroes = &b;
		}
	}
	// retail-1.png: the builder (top), the upgrades and the heroes; Command_FireWeaponMordorFortressSpireFireball (HIDE_WHILE_DISABLED) is not shown
	CHECK(std::find(names.begin(), names.end(), "Command_FireWeaponMordorFortressSpireFireball") == names.end());
	CHECK(names.size() == 3);
	CHECK(radial.layout().radius() == 32);
	REQUIRE(heroes != nullptr);
	const int px = heroes->rect.x + heroes->rect.w / 2, py = heroes->rect.y + heroes->rect.h / 2;
	h.hud->mouseMove(px, py, 0);
	h.hud->mouseButton(HudInput::Button::Left, true, px, py, 0, 1000);
	h.hud->mouseButton(HudInput::Button::Left, false, px, py, 0, 1040);
	h.frames(10);
	// the hero ring (retail-2.png): the revive slots and the back button, more than the first ring had
	CHECK(radial.presses() == 1);
	CHECK(radial.buttons().size() > 3);
	bool back = false;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		back = back || (b.source.button && b.source.button->m_command == GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE);
	}
	CHECK(back);
}

TEST_CASE("hud6 retail: a right press on a bubble cancels only a CANCELABLE button's queue (RW 0x940C70); the edges are inclusive (RW 0x715A84)")
{
	if (!haveWorld("hud6 right press"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMordor");
	h.frames(10);
	h.rig.local->getMoney()->deposit(5000, false);
	const Coord3D c = h.rig.freeSpot(2400, 1400, 300.0f);
	Object *citadel = h.rig.make("MordorFortressCitadel", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.clickSelect(citadel);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ citadel->getID() });
	h.frames(30);
	const ControlBarRadialMenu &radial = h.hud->radialMenu();
	ControlBarRadialMenu::Button porter, heroes;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (b.source.button && b.source.button->m_name == "Command_ConstructMordorPorter")
		{
			porter = b; // UNIT_BUILD, Options = CANCELABLE
		}
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.source.button->m_name.find("Revivables") != std::string::npos)
		{
			heroes = b; // not CANCELABLE
		}
	}
	REQUIRE(porter.slot >= 0);
	REQUIRE(heroes.slot >= 0);
	REQUIRE(porter.source.button->hasOption(COMMAND_OPTION_CANCELABLE));
	REQUIRE_FALSE(heroes.source.button->hasOption(COMMAND_OPTION_CANCELABLE));
	auto press = [&](HudInput::Button which, int x, int y) {
		h.hud->mouseMove(x, y, 0);
		h.hud->mouseButton(which, true, x, y, 0, 1000);
		h.hud->mouseButton(which, false, x, y, 0, 1040);
		h.frames(4);
	};
	// the bottom-right corner pixel itself is the button's (lo <= p <= lo + size)
	const int ex = porter.rect.x + porter.rect.w, ey = porter.rect.y + porter.rect.h;
	CHECK(radial.hit(ex, ey) == porter.slot);
	CHECK(radial.hit(ex + 1, ey) != porter.slot);
	press(HudInput::Button::Left, ex, ey); // queues a Porter
	h.frames(30);
	ProductionUpdateInterface *pu = citadel->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 1);
	// a right press on the non-cancelable hero bubble: the ring takes it (the selection stays), nothing is cancelled
	const std::vector<ObjectID> before = h.hud->input().ui().selected();
	press(HudInput::Button::Right, heroes.rect.x + heroes.rect.w / 2, heroes.rect.y + heroes.rect.h / 2);
	h.frames(60);
	CHECK(radial.cancels() == 0);
	CHECK(h.hud->input().ui().selected() == before);
	CHECK(pu->getProductionCount() == 1);
	// a right press on the CANCELABLE Porter bubble cancels its queue
	press(HudInput::Button::Right, porter.rect.x + porter.rect.w / 2, porter.rect.y + porter.rect.h / 2);
	h.frames(60);
	CHECK(radial.cancels() == 1);
	CHECK(pu->getProductionCount() == 0);
}

TEST_CASE("hud6 retail: the help's upgrade warnings follow RW 0x807D8F .. 0x807EB0: only after a DescriptLabel, queue full first, the local player's money")
{
	if (!haveWorld("hud6 help guards"))
	{
		return;
	}
	SharedWorld &s = shared();
	Hud6Rig h(s, "FactionMen");
	h.frames(10);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *barracks = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	h.select(barracks);
	h.frames(20);
	// an OBJECT_UPGRADE button of the barracks with a DescriptLabel
	ControlBarButton up;
	for (const auto *v : { &h.hud->controlBar().palantirButtons(), &h.hud->controlBar().sideButtons(), &h.hud->controlBar().offBarButtons() })
	{
		for (const ControlBarButton &b : *v)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_OBJECT_UPGRADE && !b.descriptLabel.empty() && up.slot < 0)
			{
				up = b;
			}
		}
	}
	REQUIRE(up.slot >= 0);
	Player *local = h.rig.local;
	std::u16string poorText, fullText;
	REQUIRE(h.text.fetch("TOOLTIP:TooltipNotEnoughMoneyToBuild", poorText));
	REQUIRE(h.text.fetch("TOOLTIP:TooltipCannotPurchaseBecauseQueueFull", fullText));
	auto ends = [](const std::u16string &s, const std::u16string &tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; };
	// no money: the warning follows the description
	local->getMoney()->init();
	CommandButtonHelp::Text t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK(ends(t.description, u"\n\n" + poorText));
	// a richer selection owner does not help: the money is the LOCAL player's (RW 0xDE4928 + 0x10)
	local->getMoney()->deposit(100000, false);
	t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK_FALSE(ends(t.description, poorText));
	local->getMoney()->init();
	// no DescriptLabel: no warning at all (the warnings live in the label's branch)
	ControlBarButton bare = up;
	bare.descriptLabel.clear();
	t = CommandButtonHelp::compose(bare, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
	CHECK(t.description.empty());
	// a queue of 0x14 entries: the queue-full text wins over the money
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu != nullptr);
	const ThingTemplate *unit = nullptr;
	for (const ControlBarButton &b : h.hud->controlBar().palantirButtons())
	{
		if (b.button && b.button->m_command == GUI_COMMAND_UNIT_BUILD && b.button->getThingTemplate() && !unit)
		{
			unit = b.button->getThingTemplate();
		}
	}
	REQUIRE(unit != nullptr);
	local->getMoney()->deposit(1000000, false);
	for (int i = 0; i < 0x14; ++i)
	{
		pu->queueCreateUnit(unit, -1, pu->requestUniqueUnitID(), 0, false, std::string(), false);
	}
	local->getMoney()->init();
	if (pu->getProductionCount() == 0x14)
	{
		t = CommandButtonHelp::compose(up, &h.text, h.rig.game->logic(), local, local, barracks, "Men");
		CHECK(ends(t.description, u"\n\n" + fullText));
		CHECK(t.description.find(poorText) == std::u16string::npos);
	}
	else
	{
		MESSAGE("the barracks' queue holds ", pu->getProductionCount(), " entries at most: the queue-full case is not reachable with it");
	}
}
