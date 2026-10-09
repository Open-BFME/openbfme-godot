// OpenBFME retail tests (lane BUILD-1): building from the HUD with input events only. A Porter is clicked, the barracks button of its command bar is pressed (placement mode), the
// pointer moves the ghost (an illegal site shows the legality code and a click there places nothing, a legal one is accepted), the click sends MSG_DOZER_CONSTRUCT through the
// translators, the logic walks the Porter to the site and the building rises and completes.  GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/BuildAssistant.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "StartTestUtil.h"

using namespace hudtest;

namespace
{
const ControlBarButton *findButton(const ControlBar &bar, const char *tmplName)
{
	for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons() })
	{
		for (const ControlBarButton &b : *list)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_DOZER_CONSTRUCT && b.button->getThingTemplate() && b.button->getThingTemplate()->getName() == tmplName)
			{
				return &b;
			}
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("hud build: click a Porter, press the barracks button, place the ghost with the pointer, the building rises and completes")
{
	if (!haveWorld("hud build"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig rig(s);
	RecordingShellServices services;
	InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	std::string error;
	REQUIRE_MESSAGE(hud.boot(&error), error);
	const auto worldContext = hud.enterContext(); // the controlBar() calls below read the process-wide command store
	GameLogic &logic = rig.logic();
	rig.local->depositMoney(3000);
	const std::uint32_t moneyStart = rig.local->getMoney()->countMoney();
	// the Porter and, a little away, a wall of stone: a Gondor farm to put the illegal ghost on
	const Coord3D spot = rig.freeSpot(2800, 1400, 260.0f);
	Object *porter = rig.make("MenPorter", spot.x, spot.y);
	Object *farm = rig.make("GondorFarm", spot.x + 120.0f, spot.y);
	rig.lookAt({ spot.x + 60.0f, spot.y, 0 });
	int timeMs = 1000;
	auto move = [&](int x, int y) {
		timeMs += 20;
		hud.input().mouseMove(x, y, 0);
		hud.input().update();
	};
	auto button = [&](bool down, int x, int y) {
		timeMs += 20;
		hud.input().mouseButton(HudInput::Button::Left, down, x, y, 0, timeMs);
		hud.input().update();
	};
	auto click = [&](int x, int y) {
		move(x, y);
		button(true, x, y);
		button(false, x, y);
	};
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			hud.update(0.2);
			rig.game->advance(0.2);
		}
	};
	frames(3);
	// 1. click the Porter: the selection message reaches the logic and the command bar shows the Porter's set
	click(rig.screenOf(*porter->getPosition()).x, rig.screenOf(*porter->getPosition()).y);
	frames(3);
	REQUIRE(hud.input().ui().firstSelected() == porter->getID());
	REQUIRE(!rig.logic().players().getLocalPlayer()->selection().empty());
	CHECK(hud.controlBar().commandSetName() == "MenPorterCommandSet");
	// 2. the barracks button (it may be on a later page of the set: the push-range buttons open the pages)
	const ControlBarButton *btn = findButton(hud.controlBar(), "GondorBarracks");
	for (int tries = 0; !btn && tries < 4; ++tries)
	{
		bool pushed = false;
		for (const auto *list : { &hud.controlBar().palantirButtons(), &hud.controlBar().sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (!pushed && b.button && b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
				{
					hud.controlBar().pressButton(b.slot, b.inPalantir);
					pushed = true;
				}
			}
		}
		frames(2);
		btn = findButton(hud.controlBar(), "GondorBarracks");
		if (!pushed)
		{
			break;
		}
	}
	REQUIRE(btn != nullptr);
	CHECK(btn->state == ButtonState::Enabled);
	const int slot = btn->slot;
	const bool inPalantir = btn->inPalantir;
	CHECK(hud.controlBar().pressButton(slot, inPalantir));
	CHECK(hud.input().ui().isPlacing());
	CHECK(hud.input().ui().placeBuildTemplate() == "GondorBarracks");
	CHECK(hud.input().ui().placeBuildSource() == porter->getID());
	// 3. an illegal site: the ghost over the farm; the click places nothing and the mode stays
	const size_t sentBefore = hud.input().sentMessages();
	const Coord3D onFarm = *farm->getPosition();
	move(rig.screenOf(onFarm).x, rig.screenOf(onFarm).y);
	CHECK(hud.input().ui().placeHasGhost());
	CHECK(hud.input().ui().placeLegalCode() == (int)LBC_OBJECTS_IN_THE_WAY);
	click(rig.screenOf(onFarm).x, rig.screenOf(onFarm).y);
	CHECK(hud.input().ui().isPlacing());
	CHECK(hud.input().sentMessages() == sentBefore);
	CHECK(hud.input().placeTranslator().stats().refused == 1);
	// 4. a legal site away from everything: the click sends the construct message and ends the mode
	const Coord3D site = rig.freeSpot(spot.x - 400.0f, spot.y, 260.0f);
	move(rig.screenOf(site).x, rig.screenOf(site).y);
	CHECK(hud.input().ui().placeLegalCode() == (int)LBC_OK);
	click(rig.screenOf(site).x, rig.screenOf(site).y);
	CHECK(!hud.input().ui().isPlacing());
	CHECK(hud.input().placeTranslator().stats().placed == 1);
	bool sawConstruct = false;
	for (const std::string &line : hud.input().messageLog())
	{
		sawConstruct = sawConstruct || line.find("MSG_DOZER_CONSTRUCT") != std::string::npos;
	}
	CHECK(sawConstruct);
	// the release that placed it ordered no move (the click MetaEvent derived from it belongs to the placement)
	for (const std::string &line : hud.input().messageLog())
	{
		CHECK(line.find("MSG_DO_MOVETO") == std::string::npos);
	}
	// 5. the logic: the foundation, the price, the Porter walks there, the building completes
	frames(2);
	Object *building = nullptr;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() && o->getTemplate()->getName() == "GondorBarracks" && o->getControllingPlayer() == rig.local)
		{
			building = o;
		}
	}
	REQUIRE(building != nullptr);
	CHECK(building->isUnderConstruction());
	CHECK(moneyStart - rig.local->getMoney()->countMoney() == (std::uint32_t)BuildAssistant::calcCostToBuild(*building->getTemplate(), rig.local, porter, -1)); // paid once, at the construct
	int guard = 1200;
	while (building->isUnderConstruction() && guard-- > 0)
	{
		frames(1);
	}
	CHECK(!building->isUnderConstruction());
	CHECK(building->getConstructionPercent() == -1.0f);
	const float dx = building->getPosition()->x - site.x, dy = building->getPosition()->y - site.y;
	CHECK(dx * dx + dy * dy < 4.0f);
	// the cancel path: a second placement ended by Escape sends nothing
	hud.controlBar().update();
	CHECK(hud.controlBar().pressButton(slot, inPalantir));
	CHECK(hud.input().ui().isPlacing());
	hud.input().key(KEY_ESC, KEY_STATE_DOWN);
	hud.input().update();
	hud.input().key(KEY_ESC, KEY_STATE_UP);
	hud.input().update();
	CHECK(!hud.input().ui().isPlacing());
}

TEST_CASE("hud build: the movie's own button callback holds this world's context while another retail world is the active one (review r1 #3)")
{
	if (!haveWorld("hud movie callback"))
	{
		return;
	}
	starttest::Shared *other = starttest::shared(); // a second retail world: its stores sit on top of the owner chain once it is entered
	REQUIRE(other != nullptr);
	REQUIRE(other->world != nullptr);
	SharedWorld &s = shared();
	Rig rig(s);
	RecordingShellServices services;
	InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	std::string error;
	REQUIRE_MESSAGE(hud.boot(&error), error);
	hud.setWindowSize(1024, 768);
	rig.view.setScreen(1024, 768);
	const Coord3D c = rig.freeSpot(2800, 1400, 200.0f);
	Object *barracks = rig.make("GondorBarracks", c.x, c.y);
	rig.lookAt({ c.x, c.y, 0 });
	for (int i = 0; i < 20; ++i)
	{
		hud.update(0.2);
		rig.game->advance(0.2);
	}
	hud.input().ui().selectObject(barracks->getID());
	for (int i = 0; i < 40; ++i)
	{
		hud.update(0.2);
		rig.game->advance(0.2);
	}
	REQUIRE(!hud.controlBar().palantirButtons().empty());
	REQUIRE(hud.controlBar().palantirButtons().front().button->m_command == GUI_COMMAND_UNIT_BUILD);
	const size_t sent = hud.input().messageLog().size();
	const std::string name = "_level" + std::to_string(hud.palantir()->level()) + ".CommandButtons.0.content_OnPress";
	REQUIRE(hud.windows().hasCommand(name));
	{
		const auto otherContext = other->world->enterContext(); // the other world is the active one while the movie runs the command
		CHECK(TheCommandStore == &other->world->commands());
		CHECK(hud.windows().invokeCallback(name, ""));
	}
	hud.update(0.2); // the stream runs: the press became the message
	bool queued = false;
	const std::vector<std::string> &log = hud.input().messageLog();
	for (size_t i = sent; i < log.size(); ++i)
	{
		queued = queued || log[i].compare(0, 20, "MSG_QUEUE_UNIT_CREAT") == 0;
	}
	CHECK(queued);
}
