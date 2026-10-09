// OpenBFME retail tests (lane QA2-FIX, QA-2 #3): a player builds a wall span through the HUD. A Gondor wall hub is clicked, its Begin Wall Span button is
// pressed (placement mode with the hub as the source), the pointer goes 320 away and the click sends MSG_WALL_HUB_CONSTRUCT_SPAN (RotWK's PlaceEventTranslator
// line build branch, RW 0x83E911 .. 0x83E9B8) through the translators; the logic builds the span (BUILD-2 / BUILD-4) and its segments rise.  GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/BuildAssistant.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameHud.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameLogic/WallSpan.h"

#include <cstdio>

using namespace hudtest;

namespace
{
// the hub's line build button: DOZER_CONSTRUCT of a WALL_HUB template (RW 0x793E33 asks the template and the source)
const ControlBarButton *findSpanButton(const ControlBar &bar, GameLogic &logic, const Object &hub)
{
	for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons() })
	{
		for (const ControlBarButton &b : *list)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_DOZER_CONSTRUCT && BuildAssistant::isLineBuildTemplate(logic, b.button->getThingTemplate(), &hub))
			{
				return &b;
			}
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("qa2fix walls: a player selects a Gondor wall hub, presses Begin Wall Span and clicks: MSG_WALL_HUB_CONSTRUCT_SPAN goes out and the segments rise")
{
	if (!haveWorld("qa2fix walls"))
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
	const auto worldContext = hud.enterContext();
	GameLogic &logic = rig.logic();
	rig.local->depositMoney(5000);
	// a site where the span from the hub 320 to the east is legal (the logic's plan, BUILD-2): the test is about the HUD, not the terrain
	Object *hub = nullptr;
	Coord3D spot{};
	for (int k = 0; k < 24 && !hub; ++k)
	{
		spot = rig.freeSpot(1200.0f + 400.0f * (float)(k % 6), 1000.0f + 500.0f * (float)(k / 6), 420.0f);
		Object *h = rig.make("MenWallHubSmall", spot.x, spot.y);
		Coord3D e{ spot.x + 320.0f, spot.y, 0.0f };
		e.z = logic.getGroundHeight(e.x, e.y);
		WallSpan::Plan plan;
		if (WallHubBehavior *wh = WallHubBehavior::find(*h, 0); wh && WallSpan::plan(logic, *h, *h->getPosition(), e, wh->data()->m_options, plan) && plan.worstCode == 0)
		{
			hub = h;
		}
		else
		{
			logic.destroyObject(h);
			rig.game->advance(0.2);
		}
	}
	REQUIRE(hub != nullptr);
	REQUIRE(hub->isKindOfName("WALL_HUB"));
	rig.lookAt({ spot.x + 160.0f, spot.y, 0 });
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
	// 1. click the hub: its command set shows the line build button
	click(rig.screenOf(*hub->getPosition()).x, rig.screenOf(*hub->getPosition()).y);
	frames(3);
	REQUIRE(hud.input().ui().firstSelected() == hub->getID());
	const ControlBarButton *btn = findSpanButton(hud.controlBar(), logic, *hub);
	REQUIRE_MESSAGE(btn != nullptr, "the selected hub's command set " << hud.controlBar().commandSetName() << " has no line build button");
	const CommandButton *spanButton = btn->button;
	MESSAGE("line build button " << spanButton->m_name << " options " << spanButton->m_options << " template " << spanButton->getThingTemplate()->getName());
	// the hub has the WallHubBehavior the button's Options select (RW 0x693B55: an exact match)
	REQUIRE(WallHubBehavior::find(*hub, spanButton->m_options) != nullptr);
	// 2. press it: placement with the hub as the source and the button kept with it (RW 0x940AA1 / 0x94089B)
	REQUIRE(hud.controlBar().pressButton(btn->slot, btn->inPalantir));
	CHECK(hud.input().ui().isPlacing());
	CHECK(hud.input().ui().placeBuildSource() == hub->getID());
	CHECK(hud.input().ui().placeBuildCommand() == spanButton);
	CHECK(!hud.input().ui().isLineBuildStarted());
	// 3. the pointer goes 320 away: the placement update starts the line (RW 0x6A2D26: +0x8C6 = 1)
	Coord3D end{ spot.x + 320.0f, spot.y, 0.0f };
	end.z = logic.getGroundHeight(end.x, end.y);
	const ICoord2D endPx = rig.screenOf(end);
	move(endPx.x, endPx.y);
	CHECK(hud.input().ui().isLineBuildStarted());
	// 4. RotWK's release without the flag only drops the anchor (RW 0x83E8A1): nothing is sent, the mode stays
	const size_t sentBefore = hud.input().sentMessages();
	button(true, endPx.x, endPx.y);
	hud.input().ui().setLineBuildStarted(false);
	button(false, endPx.x, endPx.y);
	CHECK(hud.input().sentMessages() == sentBefore);
	CHECK(hud.input().ui().isPlacing());
	CHECK(hud.input().placeTranslator().stats().wallSpans == 0);
	// 5. the click: the span message from the hub to the click, the mode ends
	click(endPx.x, endPx.y);
	CHECK(!hud.input().ui().isPlacing());
	CHECK(!hud.input().ui().isLineBuildStarted());
	CHECK(hud.input().ui().placeBuildCommand() == nullptr);
	CHECK(hud.input().placeTranslator().stats().wallSpans == 1);
	CHECK(hud.input().placeTranslator().stats().placed == 0);
	std::string spanLine;
	for (const std::string &line : hud.input().messageLog())
	{
		CHECK(line.find("MSG_DOZER_CONSTRUCT") == std::string::npos); // QA-2 #3: the hub's click was a plain construct
		CHECK(line.find("MSG_DO_MOVETO") == std::string::npos);
		if (line.compare(0, 27, "MSG_WALL_HUB_CONSTRUCT_SPAN") == 0)
		{
			spanLine = line;
		}
	}
	REQUIRE(!spanLine.empty());
	char head[200], tail[80];
	const Coord3D &hp = *hub->getPosition();
	std::snprintf(head, sizeof head, "MSG_WALL_HUB_CONSTRUCT_SPAN i%d L(%.3f,%.3f,%.3f) L(", (int)spanButton->getThingTemplate()->getFinalOverride()->getTemplateID(),
	              (double)hp.x, (double)hp.y, (double)hp.z);
	std::snprintf(tail, sizeof tail, ") i%d o%u", (int)spanButton->m_options, (unsigned)hub->getID());
	CHECK(spanLine.compare(0, std::string(head).size(), head) == 0);
	CHECK(spanLine.size() > std::string(tail).size());
	CHECK(spanLine.compare(spanLine.size() - std::string(tail).size(), std::string::npos, tail) == 0);
	INFO(spanLine);
	// 6. the logic: the span's tiles are the hub's products and rise (BUILD-4)
	frames(2);
	std::vector<ObjectID> tiles;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o != hub && o->getProducerID() == hub->getID() && o->getControllingPlayer() == rig.local && o->isKindOfName("STRUCTURE"))
		{
			tiles.push_back(o->getID());
		}
	}
	for (const std::string &why : rig.game->buildCommands().refusals())
	{
		MESSAGE("refused: " << why);
	}
	CHECK(rig.game->buildCommands().stats().wallSpans == 1);
	REQUIRE(tiles.size() >= 3);
	bool rising = false, allUp = false;
	for (int guard = 0; guard < 1500 && !allUp; ++guard)
	{
		frames(1);
		allUp = true;
		for (ObjectID id : tiles)
		{
			const Object *o = logic.findObjectByID(id);
			REQUIRE(o != nullptr);
			rising = rising || o->getConstructionPercent() > 0.0f;
			allUp = allUp && o->getConstructionPercent() == -1.0f;
		}
	}
	CHECK(rising);
	CHECK(allUp);
	for (ObjectID id : tiles)
	{
		const Object *o = logic.findObjectByID(id);
		const float dy = o->getPosition()->y - spot.y;
		CHECK(dy * dy < 400.0f); // along the line from the hub to the click
		CHECK(o->getPosition()->x > spot.x - 20.0f);
		CHECK(o->getPosition()->x < end.x + 60.0f);
	}
}

TEST_CASE("qa2fix walls: S-1770 is in HudInput::acceptanceStops() exactly once")
{
	int n = 0;
	for (const std::string &s : HudInput::acceptanceStops())
	{
		n += s.rfind("[S-1770]", 0) == 0 ? 1 : 0;
	}
	CHECK(n == 1);
}
