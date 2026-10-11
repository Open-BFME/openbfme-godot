// OpenBFME retail tests, lane AUTOCAST-1 (owner, 2026-10-10): right-clicking a hero's ability button toggles its autocast and the button glows. GPL-3.0.
//
// The Witch-king's Morgul Blade (Command_WitchkingMorgulBlade, AutoAbility = Yes) on the Palantir arc: the content clip's _OnPress with TheAptWindowManager
// + 0x318 == 2 (RW 0x9D6E54) is the right press, ControlBar::rightPressButton (RW 0x9D28D4 -> processCommandUI RW 0x940435) sends MSG_DO_AUTO_ABILITY
// (RW 0x941511), the logic toggles the AutoAbilityBehavior (RW 0x77B9BA -> 0x85D76C), and the clip's update shows the glow (RW 0x9D2D0E:
// SetAutoAbilityOverlayState "_show"); a second right press turns both off. A left press still casts (MSG_DO_SPECIAL_POWER path, not a toggle).
// The tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/MessageStream/LookAtXlat.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/TacticalCamera.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct AutocastRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<TacticalCamera> cam; // a camera attached by a test (it outlives the HUD: ~AutocastRig resets the HUD first)
	std::unique_ptr<InGameHud> hud;
	explicit AutocastRig(SharedWorld &s) : rig(s, "map mp fall back 4p", "FactionAngmar")
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~AutocastRig() { hud.reset(); }
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
		rig.lookAt(*o->getPosition());
		frames(5);
		const ICoord2D px = rig.screenOf({ o->getPosition()->x, o->getPosition()->y, 10.0f });
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, rig.timeMs += 40);
		frames(20);
	}
	const ControlBarButton *arcButton(const std::string &name)
	{
		for (const ControlBarButton &b : hud->controlBar().palantirButtons())
		{
			if (b.button && b.button->m_name == name)
			{
				return &b;
			}
		}
		return nullptr;
	}
	// the movie's own press of the arc position (the callback its clip calls on a click), with the mouse button the input translator recorded
	bool pressInMovie(const ControlBarButton &b, int mouseButton)
	{
		const std::string name = "_level" + std::to_string(hud->palantir()->level()) + ".CommandButtons." + std::to_string(b.position) + ".content_OnPress";
		hud->windows().setPressButton(mouseButton);
		const bool ok = hud->windows().hasCommand(name) && hud->windows().invokeCallback(name, "");
		hud->windows().setPressButton(0);
		return ok;
	}
	int count(const std::string &type)
	{
		int n = 0;
		for (const std::string &l : hud->input().messageLog())
		{
			n += l.rfind(type, 0) == 0 && (l.size() == type.size() || l[type.size()] != '_') ? 1 : 0;
		}
		return n;
	}
	// the window point (= stage point of a 1024 x 768 window) where the input routes to the button of the arc content showing `slot`: the grid of the
	// window searched for a point whose topmost Apt button lies inside that content (InGameHud::commandContentAt's own test)
	bool contentPoint(int slot, int &x, int &y)
	{
		Apt &apt = hud->windows().apt();
		for (int py = 380; py < 768; py += 3)
		{
			for (int px = 0; px < 1024; px += 3)
			{
				AptButtonInst *b = apt.input().hitTestButtons((float)px, (float)py);
				bool arc = false;
				int at = -1;
				if (b && hud->palantir()->contentSlotAt(b->targetPath(), arc, at) && arc && at == slot)
				{
					x = px;
					y = py;
					return true;
				}
			}
		}
		return false;
	}
	void rightClick(int x, int y)
	{
		hud->mouseMove(x, y);
		frames(2);
		hud->mouseButton(HudInput::Button::Right, true, x, y, 0, rig.timeMs += 300);
		hud->mouseButton(HudInput::Button::Right, false, x, y, 0, rig.timeMs += 40);
	}
	int overlayCalls(const char *which)
	{
		int n = 0;
		for (const std::string &c : hud->palantir()->autoAbilityOverlayCalls())
		{
			n += c.size() >= std::string(which).size() && c.compare(c.size() - std::string(which).size(), std::string::npos, which) == 0 ? 1 : 0;
		}
		return n;
	}
};
} // namespace

TEST_CASE("autocast hud retail: a right press on the Witch-king's Morgul Blade toggles autocast through MSG_DO_AUTO_ABILITY and the button glows")
{
	if (!haveWorld("autocast hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	AutocastRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = h.rig.make("AngmarWitchking", c.x, c.y);
	wk->giveUpgrade("Upgrade_WitchkingMorgulBlade"); // UnpauseSpecialPowerUpgrade ModuleTag_WitchkingMorgulBlade
	h.frames(10);
	h.select(wk);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ wk->getID() });
	const ControlBarButton *blade = h.arcButton("Command_WitchkingMorgulBlade");
	REQUIRE(blade);
	REQUIRE(blade->state != ButtonState::Restricted);
	CHECK_FALSE(blade->autoAbility);
	AutoAbilityBehavior *aa = AutoAbilityBehavior::forPower(*wk, "SpecialAbilityWitchkingMorgulBlade");
	REQUIRE(aa);
	CHECK(aa->autoButton().empty());
	const int slot = blade->slot;

	// the right press: the toggle message, then the logic's auto button and the glow
	REQUIRE(h.pressInMovie(*blade, 2));
	h.frames(15);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 1);
	CHECK(h.count("MSG_DO_SPECIAL_POWER") == 0);
	CHECK(h.hud->controlBar().autoAbilityToggles() == 1);
	CHECK(aa->autoButton() == "Command_WitchkingMorgulBlade");
	blade = h.arcButton("Command_WitchkingMorgulBlade");
	REQUIRE(blade);
	CHECK(blade->autoAbility);
	CHECK(h.hud->palantir()->autoAbilityOverlayShown(slot, true));
	CHECK(h.overlayCalls(" _show") == 1);
	CHECK(h.hud->palantir()->autoAbilityOverlayErrors() == 0u);
	for (const std::string &e : h.hud->palantir()->callErrors())
	{
		CHECK_MESSAGE(e.find("SetAutoAbilityOverlayState") == std::string::npos, e); // the movie's content clip has the function (libInGameUI.apt)
	}
	// mouse button 1 (neither left nor right): RW 0x9D6E54 does nothing
	REQUIRE(h.pressInMovie(*blade, 1));
	h.frames(10);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 1);

	// a second right press: off, the glow hides
	REQUIRE(h.pressInMovie(*blade, 2));
	h.frames(15);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 2);
	CHECK(aa->autoButton().empty());
	blade = h.arcButton("Command_WitchkingMorgulBlade");
	REQUIRE(blade);
	CHECK_FALSE(blade->autoAbility);
	CHECK_FALSE(h.hud->palantir()->autoAbilityOverlayShown(slot, true));
	CHECK(h.overlayCalls(" _hide") == 1);

	// a button without AutoAbility / CANCELABLE (the stance toggle): a right press sends nothing
	const unsigned before = h.hud->controlBar().autoAbilityToggles();
	for (const ControlBarButton &b : h.hud->controlBar().palantirButtons())
	{
		if (b.button && !b.button->m_autoAbility && !b.button->hasOption(COMMAND_OPTION_CANCELABLE))
		{
			CHECK_FALSE(h.hud->controlBar().rightPressButton(b.slot, true));
		}
	}
	CHECK(h.hud->controlBar().autoAbilityToggles() == before);
}

TEST_CASE("autocast hud retail: a real right click on the Morgul Blade button toggles autocast; right clicks and right drags on the ground still order and scroll")
{
	if (!haveWorld("autocast hud mouse"))
	{
		return;
	}
	SharedWorld &s = shared();
	AutocastRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *wk = h.rig.make("AngmarWitchking", c.x, c.y);
	wk->giveUpgrade("Upgrade_WitchkingMorgulBlade");
	h.hud->input().commandTranslator().setUseAlternateMouse(true); // RotWK's default setup (RW 0x642A4B): the right click orders
	h.frames(10);
	h.select(wk);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ wk->getID() });
	const ControlBarButton *blade = h.arcButton("Command_WitchkingMorgulBlade");
	REQUIRE(blade);
	const int slot = blade->slot;
	AutoAbilityBehavior *aa = AutoAbilityBehavior::forPower(*wk, "SpecialAbilityWitchkingMorgulBlade");
	REQUIRE(aa);
	int bx = 0, by = 0;
	REQUIRE_MESSAGE(h.contentPoint(slot, bx, by), "no window point routes to the Morgul Blade's content");
	MESSAGE("the Morgul Blade's content is under window point " << bx << ", " << by);

	// the right click on the button: through InGameHud::mouseButton (TheAptWindowManager + 0x318 = 2, the Apt's press, the content's _OnPress)
	h.rightClick(bx, by);
	h.frames(15);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 1);
	CHECK(h.count("MSG_DO_MOVETO") == 0); // the world did not see the click
	CHECK(aa->autoButton() == "Command_WitchkingMorgulBlade");
	CHECK(h.hud->palantir()->autoAbilityOverlayShown(slot, true));
	CHECK(h.hud->palantir()->autoAbilityOverlayErrors() == 0u);
	CHECK(h.hud->windows().pressButton() == 2); // the last press the game posted
	const ICoord2D ground = h.rig.screenOf({ c.x + 120.0f, c.y + 40.0f, 0.0f });
	REQUIRE(ground.y < 380);

	// a right click on the ground (the alternate setup): the selected Witch-king's move order (MSG_DO_MOVETO), the autocast untouched
	h.rightClick(ground.x, ground.y);
	h.frames(10);
	CHECK(h.count("MSG_DO_MOVETO") == 1);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 1);
	CHECK(aa->autoButton() == "Command_WitchkingMorgulBlade");

	// a right drag on the ground scrolls (the LookAtTranslator's RMB scroll), orders nothing, toggles nothing
	// the camera the game scene attaches (TacticalCamera, CameraSettings from GameData; test_camera.cpp's rig): the LookAtTranslator's RMB scroll
	CameraSettings gd;
	std::string camError;
	REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &camError), camError);
	h.cam = std::make_unique<TacticalCamera>(gd);
	h.cam->setViewport(1024, 768);
	h.hud->input().attachCamera(*h.cam);
	LookAtTranslator *la = h.hud->input().lookAt();
	REQUIRE(la);
	h.hud->mouseMove(ground.x, ground.y);
	h.hud->mouseButton(HudInput::Button::Right, true, ground.x, ground.y, 0, h.rig.timeMs += 300);
	bool scrolled = false;
	for (int k = 1; k <= 8; ++k)
	{
		h.hud->mouseMove(ground.x + 20 * k, ground.y);
		h.frames(1);
		scrolled = scrolled || la->isScrolling();
	}
	h.hud->mouseButton(HudInput::Button::Right, false, ground.x + 160, ground.y, 0, h.rig.timeMs += 400);
	h.frames(5);
	CHECK(scrolled);
	CHECK(h.count("MSG_DO_MOVETO") == 1);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 1);
	CHECK(aa->autoButton() == "Command_WitchkingMorgulBlade");

	// the button again (the camera moved: the arc did not): off
	h.rightClick(bx, by);
	h.frames(15);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 2);
	CHECK(aa->autoButton().empty());
	CHECK_FALSE(h.hud->palantir()->autoAbilityOverlayShown(slot, true));

	// the standard setup: a right click on the ground deselects (MSG_DESTROY_SELECTED_GROUP) and toggles nothing
	h.hud->input().commandTranslator().setUseAlternateMouse(false);
	h.rightClick(ground.x, ground.y);
	h.frames(10);
	CHECK(h.count("MSG_DESTROY_SELECTED_GROUP") >= 1);
	CHECK(h.count("MSG_DO_AUTO_ABILITY") == 2);
	CHECK(aa->autoButton().empty());
}
