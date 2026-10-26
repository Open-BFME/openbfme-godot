// OpenBFME retail tests, lane SPELLRC-1 (owner, 2026-10-22): "you can't right click mouse move with spellbook powers out". GPL-3.0.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the right press reaches the world unless the Apt has a button under the pointer: RW 0x83EE14 (the translator at priority 5) takes cases 0xE / 0xF only
//     when RW 0x83EB7A -> RW 0x622764 answers TheAptWindowManager + 0x329, which RW 0x624EE1 sets each update from the Apt's current button (RW 0xAE1270:
//     gApt + 0x6C);
//   * a spell book power is a GUI command waiting for its target (evaluateContextCommand RW 0x81E60B casts it). The GUI command translator (RW 0x83D7AD,
//     priority 40) keeps the right press's pixel (case 0xE: + 0x20, + 0x30 cleared) and marks a pointer move beyond the drag tolerance from it (case 3:
//     RW 0x5ED4B9 -> + 0x30); a MSG_MOUSE_RIGHT_CLICK / _DOUBLE_CLICK (0x1B / 0x1C) with + 0x30 clear ends the command (setGUICommand(0), RW 0x83DB8D) and
//     is destroyed; a right drag leaves it waiting and the raw press / release pass on to the LookAt translator (priority 60: the RMB scroll);
//   * with a GUI command other than ATTACK_MOVE waiting, the CommandTranslator's right click orders nothing (RW 0x81FAFE); without one the alternate setup's
//     right click is the move order.
// The port kept the spell book's aim outside InGameUI and cancelled it on the right PRESS, which it swallowed: no RMB scroll while a power was aimed. Now the
// press is the world's, a right drag scrolls with the power still waiting, a right click cancels it without an order and the next one moves.
// The tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MessageStream/LookAtXlat.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>

using namespace hudtest;

namespace
{
struct RcRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<TacticalCamera> cam; // outlives the HUD (~RcRig resets the HUD first)
	std::unique_ptr<InGameHud> hud;
	explicit RcRig(SharedWorld &s) : rig(s, "map mp fall back 4p", "FactionMen")
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~RcRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
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
	void rightClick(int x, int y)
	{
		hud->mouseMove(x, y);
		frames(2);
		hud->mouseButton(HudInput::Button::Right, true, x, y, 0, rig.timeMs += 300);
		hud->mouseButton(HudInput::Button::Right, false, x, y, 0, rig.timeMs += 40);
		frames(4);
	}
	// a right drag from (x, y) 160 pixels to the right; whether the LookAt translator scrolled during it
	bool rightDrag(int x, int y, LookAtTranslator &la)
	{
		hud->mouseMove(x, y);
		hud->mouseButton(HudInput::Button::Right, true, x, y, 0, rig.timeMs += 300);
		bool scrolled = false;
		for (int k = 1; k <= 8; ++k)
		{
			hud->mouseMove(x + 20 * k, y);
			frames(1);
			scrolled = scrolled || la.isScrolling();
		}
		hud->mouseButton(HudInput::Button::Right, false, x + 160, y, 0, rig.timeMs += 400);
		frames(4);
		return scrolled;
	}
};
} // namespace

TEST_CASE("SPELLRC-1 retail: with a spell book power aimed a right drag scrolls the camera and keeps it, a right click cancels it without an order, the next "
		  "right click moves; with the book and the store up the ground's right click is the move")
{
	if (!haveWorld("spellrc1"))
	{
		return;
	}
	SharedWorld &s = shared();
	RcRig h(s);
	h.hud->setWindowSize(1024, 768);
	h.rig.view.setScreen(1024, 768);
	auto ctx = h.hud->enterContext();
	h.hud->input().commandTranslator().setUseAlternateMouse(true); // RotWK's default setup (RW 0x642A4B): the right click orders
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *u = h.rig.make("GondorFighter", c.x, c.y);
	REQUIRE(u);
	// the spell book's requirement (a COMMANDCENTER) and a power bought through the store
	const ThingTemplate *cit = s.world->things().findTemplate("MenFortressCitadel");
	REQUIRE(cit);
	Object *fort = h.rig.logic().newObject(cit, h.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(fort);
	Coord3D at{ 600, 600, 0 };
	fort->setPosition(&at);
	fort->friend_onBuildComplete();
	h.frames(40);
	REQUIRE(h.hud->palantir()->spellBookShown());
	std::string error;
	REQUIRE_MESSAGE(h.hud->openSpellStore(&error), error);
	h.frames(40);
	// tolerance: which power the rank-1 points buy first is the retail store's (Heal for the Men); any that wants a position serves
	int buy = -1;
	for (const SpellStoreModel::Button &b : h.hud->spellStore()->model().buttons())
	{
		if (b.state == SpellStoreModel::STATE_AVAILABLE && buy < 0)
		{
			buy = b.index;
		}
	}
	REQUIRE(buy >= 0); // tolerance: the index is the retail store's first power the rank-1 points buy
	REQUIRE(h.hud->spellStore()->click(buy));
	h.hud->closeSpellStore();
	h.frames(40);
	int aim = -1;
	for (const InGameSpellBookModel::Button &b : h.hud->spellBar().buttons())
	{
		if (b.owned && b.ready && b.needsPosition && aim < 0)
		{
			aim = b.index;
		}
	}
	REQUIRE_MESSAGE(aim >= 0, "no bought spell book power that wants a position is ready"); // tolerance: the bought power's command set slot comes from retail data

	// the unit, selected by a click
	h.rig.lookAt(*u->getPosition());
	h.frames(5);
	const ICoord2D px = h.rig.screenOf({ c.x, c.y, 10.0f });
	h.hud->mouseMove(px.x, px.y);
	h.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, h.rig.timeMs += 500);
	h.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, h.rig.timeMs += 40);
	h.frames(20);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ u->getID() });
	const ICoord2D ground = h.rig.screenOf({ c.x + 120.0f, c.y + 40.0f, 0.0f });
	REQUIRE_FALSE(h.hud->isOverGui(ground.x, ground.y));

	// the spell book shown, the store up: the ground's right click beside the store's buttons is the move order (RW 0x83EE14: nothing of the Apt under the
	// pointer); on one of its buttons it is the store's
	REQUIRE_MESSAGE(h.hud->openSpellStore(&error), error);
	h.frames(40);
	ICoord2D beside{ -1, -1 }, onStore{ -1, -1 };
	for (const float dx : { 120.0f, -120.0f, 200.0f, -200.0f, 280.0f, -280.0f })
	{
		for (const float dy : { 40.0f, -40.0f, 120.0f, -120.0f })
		{
			const ICoord2D p = h.rig.screenOf({ c.x + dx, c.y + dy, 0.0f });
			if (p.x < 0 || p.y < 0 || p.x >= 1024 || p.y >= 560)
			{
				continue;
			}
			ICoord2D &slot = h.hud->isOverGui(p.x, p.y) ? onStore : beside;
			if (slot.x < 0)
			{
				slot = p;
			}
		}
	}
	REQUIRE(beside.x >= 0); // tolerance: the pixel depends on the camera and the store movie's layout; any ground point beside the store serves
	int moves = h.count("MSG_DO_MOVETO");
	h.rightClick(beside.x, beside.y);
	CHECK(h.count("MSG_DO_MOVETO") == moves + 1);
	if (onStore.x >= 0)
	{
		h.rightClick(onStore.x, onStore.y);
		CHECK(h.count("MSG_DO_MOVETO") == moves + 1);
	}
	h.hud->closeSpellStore();
	h.frames(10);

	// the camera the game scene attaches (the LookAtTranslator's RMB scroll), as test_autocast1_hud.cpp's rig does
	CameraSettings gd;
	std::string camError;
	REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &camError), camError);
	h.cam = std::make_unique<TacticalCamera>(gd);
	h.cam->setViewport(1024, 768);
	h.hud->input().attachCamera(*h.cam);
	LookAtTranslator *la = h.hud->input().lookAt();
	REQUIRE(la);

	// the power aimed (the cast bar's press: its targeting)
	REQUIRE(h.hud->pressSpell(aim));
	h.frames(2);
	REQUIRE(h.hud->spellBar().targeting());

	// a right drag on the ground: the camera scrolls, the power still waits, no order, nothing cast
	moves = h.count("MSG_DO_MOVETO");
	const int casts = h.count("MSG_DO_SPECIAL_POWER_AT_LOCATION");
	CHECK(h.rightDrag(ground.x, ground.y, *la)); // before SPELLRC-1: the press cancelled the power and was swallowed, the camera never scrolled
	CHECK(h.hud->spellBar().targeting());
	CHECK(h.count("MSG_DO_MOVETO") == moves);
	CHECK(h.count("MSG_DO_SPECIAL_POWER_AT_LOCATION") == casts);

	// a right click (not dragged): the power is cancelled (RW 0x83DB8D), the click orders nothing, the selection stays
	h.rightClick(ground.x, ground.y);
	CHECK_FALSE(h.hud->spellBar().targeting());
	CHECK(h.count("MSG_DO_MOVETO") == moves);
	CHECK(h.count("MSG_DO_SPECIAL_POWER_AT_LOCATION") == casts);
	CHECK(h.hud->input().ui().selected() == std::vector<ObjectID>{ u->getID() });

	// the next right click is the move order again
	h.rightClick(ground.x, ground.y);
	CHECK(h.count("MSG_DO_MOVETO") == moves + 1);

	// with no power aimed a right drag still scrolls and orders nothing (the cancel hook answers false)
	moves = h.count("MSG_DO_MOVETO");
	CHECK(h.rightDrag(ground.x, ground.y, *la));
	CHECK(h.count("MSG_DO_MOVETO") == moves);

	// lane OWNERFIX-1: aimed before a script turns the input off, the power keeps waiting through a right click while it is off (Sol r1 follow-up); with
	// the input back on the right click cancels it
	if (!h.hud->spellBar().targeting())
	{
		REQUIRE(h.hud->pressSpell(aim));
		h.frames(2);
	}
	REQUIRE(h.hud->spellBar().targeting());
	h.hud->input().ui().setInputEnabled(false);
	h.rightClick(ground.x, ground.y);
	CHECK(h.hud->spellBar().targeting());
	h.hud->input().ui().setInputEnabled(true);
	h.rightClick(ground.x, ground.y);
	CHECK_FALSE(h.hud->spellBar().targeting());
}
