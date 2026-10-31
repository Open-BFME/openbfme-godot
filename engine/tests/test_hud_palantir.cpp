// OpenBFME retail tests of the Palantir HUD (lane HUD-1): the movie in its window manager driven by the control bar, the native render components and the
// command buttons pressed through the movie. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"
#include "HudAudioTestUtil.h"

#include "GameClient/CursorFile.h"
#include "GameClient/CameraSettings.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Common/BuildAssistant.h"
#include "Common/AsciiString.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Libraries/Source/Apt/AptRenderList.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <cstdio>
#include <set>

using namespace hudtest;

namespace
{
struct HudRig
{
	Rig rig;
	hudaudio::RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit HudRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~HudRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	std::vector<AptRenderCommand> placeholders()
	{
		AptRenderList rl;
		hud->apt().buildRenderList(rl);
		std::vector<AptRenderCommand> out;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.kind == AptRenderCommand::Kind::Placeholder)
			{
				out.push_back(c);
			}
		}
		return out;
	}
};
} // namespace

TEST_CASE("hud palantir: the movie starts, answers OnInitialized and shows money and command points from the player")
{
	if (!haveWorld("hud palantir"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.frames(30);
	REQUIRE(h.hud->palantir() != nullptr);
	CHECK(h.hud->palantir()->initialized());
	const std::string *money = h.hud->windows().aptText("APT:PalantirResources");
	REQUIRE(money != nullptr);
	CHECK(*money == std::to_string(h.rig.local->getMoney()->countMoney()));
	const std::string *cp = h.hud->windows().aptText("APT:PalantirCommandPoints");
	REQUIRE(cp != nullptr);
	CHECK(cp->find('/') != std::string::npos);
}

TEST_CASE("hud palantir: a selected building fills the command arc with its CommandSet and the buttons carry image, cost and state")
{
	if (!haveWorld("hud palantir arc"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(40);
	const ControlBar &bar = h.hud->controlBar();
	CHECK(bar.commandSetName() == "GondorBarracksCommandSet");
	REQUIRE(bar.palantirButtons().size() >= 3);
}

TEST_CASE("hud click audio: the movie root PlaySound reaches the audio request log")
{
	if (!haveWorld("hud click audio")) return;
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.frames(40);
	std::string error;
	REQUIRE_MESSAGE(h.hud->windows().invokeAS(h.hud->palantir()->level(), "PlaySound", { "Gui_PalantirHeroButtonClick" }, nullptr, &error), error);
	CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirHeroButtonClick" });
	CHECK(hudaudio::takeRequests(*audio.manager, "HUD movie") == std::vector<std::string>{ "Gui_PalantirHeroButtonClick" });
}

namespace
{
AptButtonInst *firstButtonIn(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonIn(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

// the stage point (= window pixel of a 1024 x 768 window) the pointer hits the button of the arc position at
bool buttonCentre(HudRig &h, const std::string &path, int &x, int &y)
{
	AptButtonInst *b = firstButtonIn(h.hud->apt().resolvePath(h.hud->apt().level(h.hud->palantir()->level()), path));
	if (!b)
	{
		return false;
	}
	float x0, y0, x1, y1;
	if (!b->contentBounds(x0, y0, x1, y1))
	{
		return false;
	}
	float fx, fy;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, fx, fy);
	x = (int)fx;
	y = (int)fy;
	return true;
}
} // namespace

TEST_CASE("hud palantir input: select a barracks by a click, press its first command button in the movie, see the queue and the unit")
{
	if (!haveWorld("hud palantir input"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	// a click on the building through the HUD's raw input (the pointer is over the world)
	const ICoord2D px = h.rig.screenOf({ c.x, c.y, 10.0f });
	h.hud->mouseMove(px.x, px.y);
	h.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 1000);
	h.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 1040);
	h.frames(40);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ b->getID() });
	const ControlBar &bar = h.hud->controlBar();
	REQUIRE(bar.palantirButtons().size() >= 1);
	const ControlBarButton first = bar.palantirButtons().front();
	REQUIRE(first.button->m_command == GUI_COMMAND_UNIT_BUILD);
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons.0.content", x, y));
	CHECK(h.hud->isOverGui(x, y));
	const std::uint32_t money0 = h.rig.local->getMoney()->countMoney();
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.services.sounds.clear();
	audio.manager->takeEventLog();
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2000);
	h.frames(2);
	const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
	CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2040);
	h.frames(2);
	const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
	hudaudio::printSequence("APT enabled command", down, up);
	CHECK(down == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
	CHECK(up.empty());
	CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
	// the press became the retail message, and the click did not select / order anything in the world
	bool queued = false;
	for (const std::string &l : h.hud->input().messageLog())
	{
		queued = queued || l.compare(0, 20, "MSG_QUEUE_UNIT_CREAT") == 0;
	}
	CHECK(queued);
	h.frames(10);
	CHECK(h.rig.local->getMoney()->countMoney() < money0); // the logic took the cost: the command ran in its frame
	const ProductionUpdateInterface *pu = b->getProductionUpdate();
	REQUIRE(pu != nullptr);
	CHECK(pu->getProductionCount() == 1);
	CHECK(h.hud->controlBar().queue().size() == 1);
	CHECK(h.hud->controlBar().palantirButtons().front().queued >= 1);
	CHECK(h.hud->input().ui().selected() == std::vector<ObjectID>{ b->getID() }); // still selected
	// the unit comes out
	for (int i = 0; i < 4000 && pu->getProductionCount() > 0; ++i)
	{
		h.frames(1);
	}
	CHECK(pu->getProductionCount() == 0);
	size_t made = 0;
	for (Object *o = h.rig.logic().getFirstObject(); o; o = o->getNextObject())
	{
		made += o->getProducerID() == b->getID() ? 1 : 0;
	}
	CHECK(made >= 1);
}

TEST_CASE("hud click audio: APT restricted DISABLED_HELD gate")
{
	if (!haveWorld("hud click audio restricted"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0.0f });
	h.frames(20);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(40);
	// tolerance: require a first command before indexing; other command slots do not affect this click.
	REQUIRE(h.hud->controlBar().palantirButtons().size() >= 1);
	b->setDisabled(DISABLED_HELD, 0x3FFFFFFFu);
	h.frames(4);
	const ControlBarButton &first = h.hud->controlBar().palantirButtons().front();
	REQUIRE(first.button != nullptr);
	REQUIRE(first.button->m_command == GUI_COMMAND_UNIT_BUILD);
	REQUIRE(first.state == ButtonState::Restricted);
	CHECK(AptPalantir::commandStateName(first.state, first.button->m_options) == std::string("_disabled"));
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons.0.content", x, y));
	const auto queued = [&]() {
		size_t count = 0;
		for (const std::string &line : h.hud->input().messageLog())
		{
			count += line.compare(0, 20, "MSG_QUEUE_UNIT_CREAT") == 0;
		}
		return count;
	};
	const size_t queuedBefore = queued();
	audio.manager->takeEventLog();
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2200);
	h.frames(2);
	const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2240);
	h.frames(4);
	const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
	hudaudio::printSequence("APT restricted DISABLED_HELD", down, up);
	CHECK(down == std::vector<std::string>{ "Gui_PalantirCommandButtonDisabledClick" });
	CHECK(up.empty());
	CHECK(queued() == queuedBefore);
}

TEST_CASE("hud click audio: APT cannot-afford gate")
{
	if (!haveWorld("hud click audio cannot afford"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0.0f });
	h.frames(20);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(40);
	// tolerance: require a first command before indexing; other command slots do not affect this click.
	REQUIRE(h.hud->controlBar().palantirButtons().size() >= 1);
	ControlBarButton first = h.hud->controlBar().palantirButtons().front();
	REQUIRE(first.button != nullptr);
	REQUIRE(first.button->m_command == GUI_COMMAND_UNIT_BUILD);
	const std::uint32_t total = h.rig.local->getMoney()->countMoney();
	// tolerance: any positive balance establishes the transition to zero funds.
	REQUIRE(total > 0);
	h.rig.local->getMoney()->withdraw(total, false);
	h.frames(4);
	const ControlBarButton &shown = h.hud->controlBar().palantirButtons().front();
	REQUIRE(shown.state == ButtonState::CantAfford);
	CHECK(AptPalantir::commandStateName(shown.state, shown.button->m_options) == std::string("_cantAfford"));
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons.0.content", x, y));
	const auto queued = [&]() {
		size_t count = 0;
		for (const std::string &line : h.hud->input().messageLog())
		{
			count += line.compare(0, 20, "MSG_QUEUE_UNIT_CREAT") == 0;
		}
		return count;
	};
	const size_t queuedBefore = queued();
	audio.manager->takeEventLog();
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2300);
	h.frames(2);
	const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2340);
	h.frames(4);
	const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
	hudaudio::printSequence("APT cannot afford", down, up);
	CHECK(down == std::vector<std::string>{ "Gui_PalantirCommandButtonDisabledClick" });
	CHECK(up.empty());
	CHECK(queued() == queuedBefore);
}

TEST_CASE("hud click audio: APT restricted cancelable right press")
{
	if (!haveWorld("hud click audio restricted right"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0.0f });
	h.frames(20);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(40);
	b->setDisabled(DISABLED_HELD, 0x3FFFFFFFu);
	h.frames(4);
	int position = -1;
	for (size_t i = 0; i < h.hud->controlBar().palantirButtons().size(); ++i)
	{
		const ControlBarButton &candidate = h.hud->controlBar().palantirButtons()[i];
		if (candidate.button && candidate.button->hasOption(COMMAND_OPTION_CANCELABLE) && candidate.state == ButtonState::Restricted)
		{
			position = static_cast<int>(i);
			break;
		}
	}
	// tolerance: the selected cancelable command may occupy any valid movie slot.
	REQUIRE(position >= 0);
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons." + std::to_string(position) + ".content", x, y));
	audio.manager->takeEventLog();
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Right, true, x, y, 0, 2500);
	h.frames(2);
	const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
	h.hud->mouseButton(HudInput::Button::Right, false, x, y, 0, 2540);
	h.frames(4);
	const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
	hudaudio::printSequence("APT restricted cancelable right", down, up);
	CHECK(down == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
	CHECK(up.empty());
}

TEST_CASE("hud palantir input: a reused button frame dispatches the slot it shows now, not the one it showed when it was created (review r1 #3)")
{
	if (!haveWorld("hud palantir reuse"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	std::vector<std::pair<int, bool>> pressed;
	h.hud->palantir()->setButtonHandler([&](int slot, bool arc) { pressed.push_back({ slot, arc }); });
	const char *kinds[] = { "GondorBarracks", "GondorStable", "GondorWorkshop", "GondorBarracks" };
	std::vector<Object *> objs;
	for (int i = 0; i < 4; ++i)
	{
		const Coord3D c = h.rig.freeSpot(2800 + 500.0f * (float)i, 1400, 200.0f);
		objs.push_back(h.rig.make(kinds[i], c.x, c.y));
	}
	h.rig.lookAt({ 2800, 1400, 0 });
	h.frames(20);
	std::map<int, const CommandButton *> firstButton; // position -> the button its frame was created for
	size_t differing = 0, pressesChecked = 0;
	for (Object *o : objs)
	{
		h.hud->input().ui().deselectAll();
		h.frames(10);
		h.hud->input().ui().selectObject(o->getID());
		h.frames(40);
		const std::vector<ControlBarButton> shown = h.hud->controlBar().palantirButtons();
		for (const ControlBarButton &b : shown)
		{
			// lane HUD-4: the arc is positional (window = slot - range start, RW 0x930035): the frame of position p shows window p's button
			const int p = b.position;
			int x = 0, y = 0;
			if (p < 0 || !buttonCentre(h, "CommandButtons." + std::to_string(p) + ".content", x, y))
			{
				continue;
			}
			if (!firstButton.count(p))
			{
				firstButton[p] = b.button;
			}
			const bool allowed = b.state == ButtonState::Enabled || b.state == ButtonState::Active;
			pressed.clear();
			h.hud->mouseMove(x, y);
			h.frames(2);
			h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2000);
			h.frames(2);
			h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2040);
			h.frames(2);
			if (allowed)
			{
				REQUIRE(pressed.size() == 1);
				CHECK(pressed[0].first == b.slot);
				CHECK(pressed[0].second);
				differing += firstButton[p] != b.button ? 1 : 0;
				++pressesChecked;
			}
			else
			{
				CHECK(pressed.empty());
			}
		}
	}
	CHECK(pressesChecked >= 8);
	CHECK(differing >= 1); // the frames really were reused for a different button
	// no selection: the frames are emptied and a press at the old place does nothing
	h.hud->input().ui().deselectAll();
	h.frames(40);
	CHECK(h.hud->controlBar().palantirButtons().empty());
}

TEST_CASE("hud palantir input: a rally point by the command button and by a click on the ground; both are lockstep messages the logic executes")
{
	if (!haveWorld("hud rally"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 250.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(40);
	ExitInterface *exit = b->getObjectExitInterface();
	REQUIRE(exit != nullptr);
	// a click on the ground with only the producer selected sets its rally point (ZH ACTIONTYPE_SET_RALLY_POINT; the standard setup: the left click)
	h.hud->input().commandTranslator().setUseAlternateMouse(false);
	const Coord3D target{ c.x + 200.0f, c.y - 150.0f, 0.0f };
	const ICoord2D px = h.rig.screenOf(target);
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(b->getID());
	h.hud->input().update();
	h.hud->mouseMove(px.x, px.y);
	h.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 5000);
	h.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 5040);
	h.frames(6);
	bool rally = false;
	for (const std::string &l : h.hud->input().messageLog())
	{
		rally = rally || l.compare(0, 20, "MSG_SET_RALLY_POINT ") == 0;
	}
	CHECK(rally);
	REQUIRE(exit->getRallyPoint() != nullptr);
	CHECK(std::fabs(exit->getRallyPoint()->x - target.x) < 12.0f);
	CHECK(std::fabs(exit->getRallyPoint()->y - target.y) < 12.0f);
}

TEST_CASE("hud rally banner: real right click sets the point and animates the retail flag while the produced horde walks there")
{
	if (!haveWorld("hud rally banner"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const auto context = h.hud->enterContext();
	h.hud->input().commandTranslator().setUseAlternateMouse(true);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f, false, { { 150.0f, 250.0f } });
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt(c);
	h.frames(30);
	const ICoord2D building = h.rig.screenOf({ c.x, c.y, b->getPosition()->z + 20.0f });
	h.hud->mouseMove(building.x, building.y);
	h.hud->mouseButton(HudInput::Button::Left, true, building.x, building.y, 0, 1000);
	h.hud->mouseButton(HudInput::Button::Left, false, building.x, building.y, 0, 1040);
	h.frames(20);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ b->getID() });
	DrawableManager &dm = h.rig.game->drawables();
	auto marker = [&]() -> Drawable * {
		Drawable *found = nullptr;
		for (size_t i = 1; i < dm.slotCount(); ++i)
		{
			Drawable *d = dm.find((DrawableID)i);
			if (d && d->getTemplate()->getName() == "RallyPointMarker")
			{
				CHECK(found == nullptr);
				found = d;
			}
		}
		return found;
	};
	CHECK(marker() == nullptr);
	const Coord3D target{ c.x + 150.0f, c.y + 250.0f, 0.0f };
	const ICoord2D px = h.rig.screenOf(target);
	REQUIRE_FALSE(h.hud->isOverGui(px.x, px.y));
	h.hud->mouseMove(px.x, px.y);
	h.hud->mouseButton(HudInput::Button::Right, true, px.x, px.y, 0, 2000);
	h.hud->mouseButton(HudInput::Button::Right, false, px.x, px.y, 0, 2040);
	h.frames(12);
	bool command = false;
	for (const std::string &line : h.hud->input().messageLog())
	{
		command = command || line.compare(0, 20, "MSG_SET_RALLY_POINT ") == 0;
	}
	CHECK(command);
	REQUIRE(b->getObjectExitInterface() != nullptr);
	const Coord3D *point = b->getObjectExitInterface()->getRallyPoint();
	REQUIRE(point != nullptr);
	CHECK(std::fabs(point->x - target.x) < 12.0f); // tolerance: click coordinates are quantized to screen pixels before unprojecting.
	CHECK(std::fabs(point->y - target.y) < 12.0f); // tolerance: click coordinates are quantized to screen pixels before unprojecting.
	const Coord3D rally = *point;
	// Do not stop at the missing visual: the red run also proves that the command and subsequent movement already work.
	CHECK(marker() != nullptr);
	if (Drawable *flag = marker())
	{
		CHECK(flag->getObjectID() == INVALID_ID);
		CHECK(flag->getPosition()->x == point->x);
		CHECK(flag->getPosition()->y == point->y);
		CHECK(flag->getPosition()->z == point->z);
		CameraSettings gd;
		std::string error;
		REQUIRE(CameraSettings::load(*s.mount->fs, gd, &error));
		CHECK(gd.haveDownwindAngle);
		CHECK(flag->getOrientation() == gd.downwindAngle);
		CHECK(flag->getHouseColor() == h.rig.local->getPlayerColor());
		REQUIRE(flag->entries().size() == 1);
		REQUIRE(flag->entries()[0].draw != nullptr);
		const W3DDrawFrame frame = flag->entries()[0].draw->frame();
		CHECK(AsciiStringUtil::lowered(frame.modelName) == "rallyflag_skn");
		REQUIRE(frame.trackCount == 1);
		CHECK(AsciiStringUtil::lowered(frame.tracks[0].clipName) == "rallyflag_skl.rallyflag_wava");
		CHECK(frame.tracks[0].mode == W3D_ANIM_MODE_LOOP);
		CHECK(frame.model != nullptr);
		CHECK(frame.tracks[0].anim != nullptr);
		h.frames(3);
		CHECK(flag->entries()[0].draw->frame().tracks[0].frame != frame.tracks[0].frame);
	}
	REQUIRE(!h.hud->controlBar().palantirButtons().empty());
	const ControlBarButton first = h.hud->controlBar().palantirButtons().front();
	REQUIRE(first.button->m_command == GUI_COMMAND_UNIT_BUILD);
	const ThingTemplate *unit = first.button->getThingTemplate();
	REQUIRE(unit != nullptr);
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons.0.content", x, y));
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 3000);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 3040);
	h.frames(12);
	Object *made = nullptr;
	bool arrived = false;
	for (int i = 0; i < 4000 && !arrived; ++i)
	{
		h.frames(1);
		if (!made)
		{
			for (Object *o = h.rig.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getProducerID() == b->getID() && o->getTemplate() == unit)
				{
					made = o;
					break;
				}
			}
		}
		if (made)
		{
			const Coord3D pos = *made->getPosition();
			arrived = std::hypot(pos.x - rally.x, pos.y - rally.y) < 40.0f; // tolerance: existing 40-unit production-arrival acceptance radius.
		}
	}
	REQUIRE(made != nullptr);
	CHECK(arrived);
	CHECK(marker() != nullptr);
	const DrawableID id = marker() ? marker()->getID() : 0;
	// A second actual right click repositions the existing flag, not an additional object.
	const ICoord2D next = h.rig.screenOf({ target.x - 60.0f, target.y, 0.0f });
	h.hud->mouseMove(next.x, next.y);
	h.hud->mouseButton(HudInput::Button::Right, true, next.x, next.y, 0, 150000);
	h.hud->mouseButton(HudInput::Button::Right, false, next.x, next.y, 0, 150040);
	h.frames(12);
	if (Drawable *flag = marker())
	{
		CHECK(flag->getID() == id);
		CHECK(flag->getPosition()->x == b->getObjectExitInterface()->getRallyPoint()->x);
		CHECK(std::fabs(flag->getPosition()->x - rally.x) > 40.0f); // tolerance: the 60-unit target shift must exceed arrival radius despite pixel rounding.
		h.rig.local->setPlayerNightColor(0xff123456);
		h.rig.logic().settings().night = true;
		h.frames(1);
		CHECK(flag->getHouseColor() == 0xff123456);
		h.rig.logic().settings().night = false;
	}
	h.hud->input().ui().deselectAll(false);
	h.frames(1);
	CHECK(marker() == nullptr);
	CHECK(dm.find(id) == nullptr);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(1);
	CHECK(marker() != nullptr);
	if (marker())
	{
		CHECK(marker()->getID() != id);
	}
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(made->getID());
	h.frames(1);
	CHECK(marker() == nullptr);
}

namespace
{
std::string sideOf(const ThingTemplate &tt)
{
	if (const FieldValue *v = tt.findField("Side"))
	{
		if (const std::string *name = std::get_if<std::string>(v))
		{
			return *name;
		}
	}
	return std::string();
}
} // namespace

TEST_CASE("hud control bar: the buildings of every playable faction show exactly their CommandSet (slots, buttons, images) with retail's states")
{
	if (!haveWorld("hud control bar factions"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const PlayerTemplateStore &store = h.rig.game->players().templates();
	Coord3D at = h.rig.freeSpot(2800, 1400, 250.0f);
	size_t factions = 0, buildings = 0, buttonsChecked = 0, emptyStarting = 0;
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		const PlayerTemplate *pt = store.findPlayerTemplate(faction);
		REQUIRE_MESSAGE(pt != nullptr, faction);
		REQUIRE(!pt->m_startingBuilding.empty());
		// the starting building, then every other structure of the faction that has a CommandSet (the first six by name)
		std::vector<std::string> names{ pt->m_startingBuilding };
		for (const ThingTemplate *tt : s.world->things().templates())
		{
			const ThingTemplate *f = tt->getFinalOverride();
			if (names.size() >= 7 || sideOf(*f) != pt->m_side || f->getName() == pt->m_startingBuilding)
			{
				continue;
			}
			const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*f);
			const bool structure = MaskTest(info.kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE"));
			if (structure && f->findField("CommandSet") && hasNugget(*f, "ProductionUpdate"))
			{
				names.push_back(f->getName());
			}
		}
		for (const std::string &name : names)
		{
			at.x += 300.0f;
			Object *b = h.rig.make(name, at.x, at.y);
			h.hud->input().ui().deselectAll(false);
			h.hud->input().ui().selectObject(b->getID());
			h.hud->controlBar().update();
			const ControlBar &bar = h.hud->controlBar();
			INFO(faction << " " << name << " set '" << b->getCommandSetName() << "'");
			++buildings;
			const CommandSet *set = b->getCommandSetName().empty() ? nullptr : s.world->commands().findCommandSet(b->getCommandSetName());
			if (!set)
			{
				// no CommandSet (the starting fortress takes its sets from upgrade modules, UPGRADE-1): the bar is empty, as retail's is before any upgrade
				CHECK(bar.palantirButtons().empty());
				CHECK(bar.sideButtons().empty());
				emptyStarting += name == pt->m_startingBuilding ? 1 : 0;
				continue;
			}
			CHECK(bar.commandSetName() == set->getName());
			std::map<int, const ControlBarButton *> shown, arcAt, sideAt;
			for (const auto *v : { &bar.palantirButtons(), &bar.sideButtons(), &bar.offBarButtons() })
			{
				for (const ControlBarButton &cb : *v)
				{
					shown[cb.slot] = &cb;
					if (v == &bar.palantirButtons())
					{
						arcAt[cb.slot] = &cb;
					}
					else if (v == &bar.sideButtons())
					{
						sideAt[cb.slot] = &cb;
					}
				}
			}
			for (int slot = 0; slot < std::min(set->m_initialVisible, (int)CommandSet::MAX_BUTTONS); ++slot)
			{
				const CommandButton *want = set->getCommandButton(slot);
				auto it = shown.find(slot);
				if (!want)
				{
					CHECK(it == shown.end());
					continue;
				}
				if (it == shown.end())
				{
					// lane HERO-1: a REVIVE slot without a hero record for it, or without its NeededUpgrade (RW 0x943D6F), is hidden; a special power the object has no
					// module for too
					// lane PLAY-4 r3: a DEFENSIVE_WALL under construction hides its OBJECT_UPGRADE buttons (RW 0x943050, FACT-A's rule): a wall segment builds
					// itself from its creation (GettingBuiltBehavior::onObjectCreated, RW 0x856434), so the made segment is under construction here
					const bool wallBuilding = want->m_command == GUI_COMMAND_OBJECT_UPGRADE && b->isUnderConstruction() && b->isKindOfName("DEFENSIVE_WALL");
					const bool hiddenByRule = !want->m_showButton || (want->m_command == GUI_COMMAND_UNIT_BUILD && want->getThingTemplate() && BuildAssistant::buildable(*want->getThingTemplate()) >= 2) ||
						want->m_command == GUI_COMMAND_REVIVE || want->m_command == GUI_COMMAND_SPECIAL_POWER || wallBuilding;
					CHECK_MESSAGE(hiddenByRule, "slot " << slot << " " << want->m_name << " vanished without a retail rule");
					continue;
				}
				++buttonsChecked;
				CHECK(it->second->button == want);
				// lane HUD-4: the arc shows windows 0 .. 5 that are InPalantir, the side bar every Radial button (RW 0x92FF5C / 0x92F082)
				const ControlBar::Placement pl = ControlBar::placementOf(*want, slot, 0);
				CHECK(arcAt.count(slot) == (pl.arc ? 1u : 0u));
				CHECK(sideAt.count(slot) == (pl.side ? 1u : 0u));
				if (pl.arc)
				{
					CHECK(arcAt[slot]->position == slot);
				}
				if (want->m_command == GUI_COMMAND_REVIVE)
				{
					CHECK(!it->second->image.empty()); // lane HERO-1: the hero record's image (RW 0x7810BC), the button names none
				}
				else
				{
					CHECK(it->second->image == (want->m_buttonImageName.empty() ? std::string() : want->m_buttonImageName.front()));
				}
				if (want->m_command == GUI_COMMAND_UNIT_BUILD && want->getThingTemplate())
				{
					CHECK(it->second->cost == BuildAssistant::calcCostToBuild(*want->getThingTemplate(), h.rig.local, b, -1));
					CHECK(it->second->queued == 0);
					CHECK(it->second->timer < 0.0f);
					const CanMakeType make = BuildAssistant::canMakeUnit(*b, want->getThingTemplate(), -1);
					CHECK((make == CANMAKE_OK) == (it->second->state == ButtonState::Enabled));
				}
			}
		}
		++factions;
	}
	CHECK(factions == 7);
	CHECK(buildings >= 14);
	CHECK(buttonsChecked > 30);
	std::printf("control bar factions: %zu buildings, %zu buttons checked, %zu starting buildings without a CommandSet of their own\n", buildings, buttonsChecked, emptyStarting);
}

TEST_CASE("stops S-289 .. S-295: the control bar, the radar and the Palantir report what they do not port")
{
	std::vector<std::string> all = ControlBar::acceptanceStops();
	for (const std::string &l : AptPalantir::acceptanceStops())
	{
		all.push_back(l);
	}
	for (const std::string &l : Radar::acceptanceStops())
	{
		all.push_back(l);
	}
	for (const std::string &l : CursorAcceptanceStops())
	{
		all.push_back(l);
	}
	std::set<std::string> ids;
	for (const std::string &l : all)
	{
		ids.insert(l.substr(0, 7));
	}
	CHECK(ids == std::set<std::string>{ "[S-289]", "[S-290]", "[S-291]", "[S-292]", "[S-293]", "[S-294]", "[S-295]", "[S-761]", "[S-762]", "[S-763]", "[S-764]", "[S-1260", "[S-1674", "[S-1922", "[S-2450", "[S-2451", "[S-2452", "[S-2453", "[S-2454", "[S-2455", "[S-2456", "[S-2457", "[S-3460", "[S-1990", "[S-1992", "[S-3950", "[S-3801", "[S-3803", "[S-5040" }); // lane HUD-4: S-1674 (the radar events) // lane UI-1: S-1260 (the id is eight characters) // lane RADAR-1: S-2450 .. S-2457 // lane AUTOCAST-1: S-3460 // lane FACT-B: S-1990 .. S-1992 // lane HUD-7: S-3950
}

TEST_CASE("hud radar: the map picture, the object blips in the owners' colours, the view box and a click on the radar are world points")
{
	if (!haveWorld("hud radar"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	Radar &radar = h.hud->radar();
	REQUIRE(radar.ready());
	const int size = 128;
	// the mapping is its own inverse
	for (float wx : { 100.0f, 1500.0f, 2900.0f })
	{
		float rx, ry, bx, by;
		radar.worldToRadar(wx, 800.0f, size, rx, ry);
		radar.radarToWorld(rx, ry, size, bx, by);
		CHECK(bx == doctest::Approx(wx).epsilon(1e-4));
		CHECK(by == doctest::Approx(800.0f).epsilon(1e-4));
	}
	const std::vector<std::uint8_t> &img = radar.terrainImage(size);
	REQUIRE(img.size() == (size_t)size * size * 4);
	size_t lit = 0;
	for (size_t i = 3; i < img.size(); i += 4)
	{
		lit += img[i] == 255;
	}
	CHECK(lit == (size_t)size * size); // every pixel of the picture is opaque terrain
	const Coord3D c = h.rig.freeSpot(2800, 1400, 250.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(5);
	bool found = false;
	float bx = 0, by = 0;
	for (const Radar::Blip &blip : radar.blips(size))
	{
		if (blip.object == b->getID())
		{
			found = true;
			bx = blip.x;
			by = blip.y;
			CHECK(blip.mine);
			CHECK((blip.color & 0xFFFFFFu) == (h.rig.local->getPlayerColor() & 0xFFFFFFu));
		}
	}
	REQUIRE(found);
	// clicking the blip's pixel gives back the building's position within one radar pixel
	float wx, wy;
	REQUIRE(radar.click(bx, by, size, wx, wy));
	const float pixelWorld = 3000.0f / (float)size;
	CHECK(std::fabs(wx - c.x) < pixelWorld * 2.0f);
	CHECK(std::fabs(wy - c.y) < pixelWorld * 2.0f);
	CHECK(!radar.click(-1.0f, 5.0f, size, wx, wy));
	float box[8];
	REQUIRE(radar.viewBox(size, box));
	// the camera looks at the building: the middle of the box is near its blip
	const float mx = (box[0] + box[2] + box[4] + box[6]) / 4.0f, my = (box[1] + box[3] + box[5] + box[7]) / 4.0f;
	CHECK(std::fabs(mx - bx) < 20.0f);
	CHECK(std::fabs(my - by) < 20.0f);
}

TEST_CASE("hud radar input: a left click on the radar moves the view, a right click orders the selection there; the world sees neither")
{
	if (!haveWorld("hud radar input"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 250.0f);
	Object *unit = h.rig.make(templates(s).infantry.front(), c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(40);
	float sq[4];
	REQUIRE(h.hud->radarSquare(sq));
	const int mx = (int)((sq[0] + sq[2]) * 0.5f), my = (int)((sq[1] + sq[3]) * 0.5f);
	CHECK(h.hud->isOverGui(mx, my));
	const Coord3D before = h.rig.view.position();
	h.hud->mouseMove(mx, my);
	h.hud->mouseButton(HudInput::Button::Left, true, mx, my, 0, 100);
	h.hud->mouseButton(HudInput::Button::Left, false, mx, my, 0, 140);
	h.frames(2);
	const Coord3D after = h.rig.view.position();
	CHECK((std::fabs(after.x - before.x) + std::fabs(after.y - before.y)) > 50.0f); // the view jumped to the middle of the map
	CHECK(h.hud->input().messageLog().empty()); // nothing reached the world
	// select the unit, right click on the radar: a move message
	h.hud->input().ui().selectObject(unit->getID());
	h.frames(2);
	const int rx = (int)(sq[0] + (sq[2] - sq[0]) * 0.25f), ry = (int)(sq[1] + (sq[3] - sq[1]) * 0.75f);
	h.hud->mouseButton(HudInput::Button::Right, true, rx, ry, 0, 200);
	h.hud->mouseButton(HudInput::Button::Right, false, rx, ry, 0, 240);
	h.frames(2);
	size_t moves = 0;
	for (const std::string &l : h.hud->input().messageLog())
	{
		moves += l.compare(0, 11, "MSG_DO_MOVE") == 0;
	}
	CHECK(moves == 1);
}

namespace
{
// lane PLAY-1: a point inside the button's hit shape that the input routes to it (the centre of the content bounds may lie on another button)
bool buttonHitPoint(HudRig &h, const std::string &path, int &x, int &y)
{
	AptButtonInst *b = firstButtonIn(h.hud->apt().resolvePath(h.hud->apt().level(h.hud->palantir()->level()), path));
	float x0, y0, x1, y1;
	if (!b || !b->contentBounds(x0, y0, x1, y1))
	{
		return false;
	}
	// the button's hit area: its mesh placed by each Hit record's matrix (AptButtonInst::hitTest); the file's bounds are the mesh's own
	if (const AptButtonInfo *info = b->info())
	{
		bool any = false;
		float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
		for (const AptButtonRecord &rec : info->records)
		{
			if (!(rec.stateMask & 8))
			{
				continue;
			}
			const float cxs[4] = { x0, x1, x1, x0 }, cys[4] = { y0, y0, y1, y1 };
			for (int k = 0; k < 4; ++k)
			{
				const float px = rec.matrix[0] * cxs[k] + rec.matrix[2] * cys[k] + rec.translation[0];
				const float py = rec.matrix[1] * cxs[k] + rec.matrix[3] * cys[k] + rec.translation[1];
				hx0 = any ? std::min(hx0, px) : px;
				hy0 = any ? std::min(hy0, py) : py;
				hx1 = any ? std::max(hx1, px) : px;
				hy1 = any ? std::max(hy1, py) : py;
				any = true;
			}
		}
		if (any)
		{
			x0 = hx0;
			y0 = hy0;
			x1 = hx1;
			y1 = hy1;
		}
	}
	// the hit point nearest the centre
	float cx, cy, best = 0.0f;
	bool found = false;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, cx, cy);
	for (int gy = 1; gy < 16; ++gy)
	{
		for (int gx = 1; gx < 16; ++gx)
		{
			float px, py;
			b->globalMatrix().apply(x0 + (x1 - x0) * (float)gx / 16.0f, y0 + (y1 - y0) * (float)gy / 16.0f, px, py);
			const float d = (px - cx) * (px - cx) + (py - cy) * (py - cy);
			if ((!found || d < best) && b->hitTest(px, py) && h.hud->apt().input().hitTestButtons(px, py) == b)
			{
				x = (int)px;
				y = (int)py;
				best = d;
				found = true;
			}
		}
	}
	return found;
}

bool commandSeen(HudRig &h, const std::string &name)
{
	for (const std::string &l : h.hud->palantir()->commandLog())
	{
		if (l.compare(0, name.size(), name) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("play1 palantir input: the key, the powers button and the flag above the radar reach the engine as the movie's commands")
{
	if (!haveWorld("play1 palantir input"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	h.frames(40);
	struct Press
	{
		const char *path, *command;
	};
	for (const Press &p : { Press{ "PalantirButtons.Buttons.Options", "AptPalantir::OnBttnOptions" }, Press{ "PalantirButtons.Buttons.PlayerMagic", "AptPalantir::OnBttnSpellStore" },
			 Press{ "PalantirButtons.Buttons.Objectives", "AptPalantir::OnBttnObjectives" } })
	{
		INFO(p.path);
		int x = 0, y = 0;
		REQUIRE(buttonHitPoint(h, p.path, x, y));
		CHECK(h.hud->isOverGui(x, y));
		h.hud->mouseMove(x, y);
		h.frames(2);
		audio.manager->takeEventLog();
		h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2000);
		h.frames(2);
		const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
		h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2040);
		h.frames(4);
		const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
		std::vector<std::string> sequence = down;
		sequence.insert(sequence.end(), up.begin(), up.end());
		hudaudio::printSequence(std::string("Palantir ") + p.path, down, up);
		CHECK(sequence == std::vector<std::string>{ "Gui_PalantirButtonClick" });
		CHECK_MESSAGE(commandSeen(h, p.command), p.command);
	}
}

// lane MENUSND-1: the Palantir's click sounds, from its movies (Palantir.apt's buttons: Gui_PalantirButtonClick on the press; libInGameUI.apt's command
// button, button 37 transition 4: _root.PlaySound("Gui_PalantirCommandButtonClick") before its FSCommand _OnPress), through the Palantir's PlaySound
// (FSCommand PlaySound, RW 0x815772). The pointer on them asks for nothing.
TEST_CASE("menusnd1 palantir input: the Palantir's buttons and a command button ask for RotWK's click sound on the press, once")
{
	if (!haveWorld("menusnd1 palantir input"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	h.frames(40);
	h.services.sounds.clear();
	for (const char *path : { "PalantirButtons.Buttons.Options", "PalantirButtons.Buttons.PlayerMagic", "PalantirButtons.Buttons.Objectives" })
	{
		INFO(path);
		int x = 0, y = 0;
		REQUIRE(buttonHitPoint(h, path, x, y));
		h.hud->mouseMove(x, y);
		h.frames(4);
		CHECK(h.services.sounds.empty());
		h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2000);
		h.frames(2);
		h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2040);
		h.frames(10);
		CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirButtonClick" });
		h.services.sounds.clear();
		h.hud->mouseMove(5, 5);
		h.frames(30);
		h.services.sounds.clear();
	}
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	const ICoord2D px = h.rig.screenOf({ c.x, c.y, 10.0f });
	h.hud->mouseMove(px.x, px.y);
	h.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 3000);
	h.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 3040);
	h.frames(40);
	h.services.sounds.clear();
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, "CommandButtons.0.content", x, y));
	h.hud->mouseMove(x, y);
	h.frames(2);
	CHECK(h.services.sounds.empty());
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 4000);
	h.frames(2);
	CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" }); // on the press
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 4040);
	h.frames(10);
	CHECK(h.services.sounds == std::vector<std::string>{ "Gui_PalantirCommandButtonClick" });
}

TEST_CASE("hud click audio: hero portrait movie request")
{
	if (!haveWorld("hud click audio hero"))
	{
		return;
	}
	SharedWorld &s = shared();
	hudaudio::RetailAudio audio(*s.mount->fs);
	HudRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *hero = h.rig.make("GondorAragornMP", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0.0f });
	h.frames(40);
	REQUIRE(h.hud->heroSelect() != nullptr);
	int heroSlot = -1;
	for (int i = 0; i < 8; ++i)
	{
		if (h.hud->heroSelect()->slot(i).hero == hero->getID())
		{
			heroSlot = i;
			break;
		}
	}
	// tolerance: the hero may occupy any valid portrait slot, but must be present.
	REQUIRE(heroSlot >= 0);
	int x = 0, y = 0;
	REQUIRE(buttonCentre(h, h.hud->heroSelect()->name() + ".Hero" + std::to_string(heroSlot + 1), x, y));
	audio.manager->takeEventLog();
	h.hud->mouseMove(x, y);
	h.frames(2);
	h.hud->mouseButton(HudInput::Button::Left, true, x, y, 0, 2400);
	h.frames(2);
	const std::vector<std::string> down = hudaudio::takeRequests(*audio.manager, "HUD movie");
	h.hud->mouseButton(HudInput::Button::Left, false, x, y, 0, 2440);
	h.frames(4);
	const std::vector<std::string> up = hudaudio::takeRequests(*audio.manager, "HUD movie");
	hudaudio::printSequence("hero portrait", down, up);
	CHECK(down == std::vector<std::string>{ "Gui_PalantirHeroButtonClick" });
	CHECK(up.empty());
}
