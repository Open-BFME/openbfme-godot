// OpenBFME. GPL-3.0.
//
// Lane HOVER-2: the mouse-over tool tip over an object in the world (InGameUI::createMouseoverHint RW 0x69F637, GameClient/MouseoverTooltip.h): an enemy's
// name, never the local player's own units and structures (they go to the control bar), nothing in the shroud, the player's name in a network game; and the
// HUD hands it to the window manager's world tool tip while the pointer rests on the object.

#include "doctest.h"

#include "HudTestUtil.h"
#include "HudAudioTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MouseoverTooltip.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <memory>
#include <string>

using namespace hudtest;

namespace
{
// the local player's cell status at the object (RW 0xB4FB20)
CellShroudStatus statusAt(Rig &r, const Object &o)
{
	return r.logic().shroud()->getStatusAt(r.index(), o.getPosition()->x, o.getPosition()->y);
}

UnicodeString displayName(const Object &o, const GameTextSource &text)
{
	return CommandButtonHelp::templateText(*o.getTemplate()->getFinalOverride(), "DisplayName", &text);
}

// an InGameHud on a Rig game in the owned-window-manager form, as test_hud6.cpp's Hud6Rig
struct HoverHud
{
	Rig rig;
	hudaudio::RecordingShellServices services;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	std::unique_ptr<InGameHud> hud;
	explicit HoverHud(SharedWorld &s) : rig(s)
	{
		std::string error;
		REQUIRE_MESSAGE(text.table.loadRetail(*s.mount->fs, &error), error);
		imageEnv.fileSystem = s.mount->fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		hud->setMappedImages(&images);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~HoverHud() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void pointAt(const Object &o)
	{
		const ICoord2D px = rig.screenOf({ o.getPosition()->x, o.getPosition()->y, o.getPosition()->z + 10.0f });
		hud->mouseMove(px.x + 3, px.y, 0); // a move, then the point: the hover pick runs on the move (RW 0x83CC13)
		hud->mouseMove(px.x, px.y, 0);
		frames(1);
	}
};
} // namespace

TEST_CASE("hover2 retail: an enemy in sight gets its DisplayName text, the local player's own structure none, the shrouded enemy none (RW 0x69F637)")
{
	if (!haveWorld("hover2 mouse-over tool tip"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	GameTextTableSource text;
	std::string error;
	REQUIRE_MESSAGE(text.table.loadRetail(*s.mount->fs, &error), error);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D c = r.freeSpot(2400, 1800, 300.0f, false, {}, nullptr, 100.0f);
	Object *troll = r.make("MordorAttackTroll", c.x, c.y, enemy);
	Object *own = r.make("GondorBarracks", c.x + 150.0f, c.y);
	r.frame(30); // the local player's structure clears the shroud around it
	REQUIRE(statusAt(r, *troll) == CELLSHROUD_CLEAR);
	MouseoverTooltip::Inputs in;
	in.localPlayer = r.local;
	in.haveDebugAI = true; // RotWK's GameData: DebugAI = No
	const UnicodeString enemyName = displayName(*troll, text);
	REQUIRE_FALSE(enemyName.empty());

	const MouseoverTooltip::Result e = MouseoverTooltip::compose(*troll, r.logic(), &text, in);
	CHECK(e.text == enemyName);
	CHECK_FALSE(e.controlBarRecord);

	// the local player's STRUCTURE goes to the control bar: no ordinary tool tip without DebugAI (RW 0x69FC31)
	const MouseoverTooltip::Result o = MouseoverTooltip::compose(*own, r.logic(), &text, in);
	CHECK(o.controlBarRecord);
	CHECK(o.text.empty());
	in.debugAI = true;
	CHECK(MouseoverTooltip::compose(*own, r.logic(), &text, in).text == displayName(*own, text));
	in.debugAI = false;

	// a network game with a playable side's player: "<name>\n<player's display name>" (decomp line 498)
	in.networkGame = true;
	std::u16string withPlayer = enemyName;
	withPlayer += u'\n';
	for (char ch : enemy->getPlayerDisplayName())
	{
		withPlayer += (char16_t)(unsigned char)ch; // the rig's player names are ASCII
	}
	REQUIRE(enemy->getPlayerTemplate() != nullptr);
	REQUIRE(enemy->getPlayerTemplate()->m_playableSide);
	CHECK(MouseoverTooltip::compose(*troll, r.logic(), &text, in).text == withPlayer);
	in.networkGame = false;

	// an enemy where the local player sees nothing: no tool tip (RW 0xB4FB20 is not CLEAR)
	const Coord3D far = r.freeSpot(400, 400, 300.0f, false, {}, nullptr, 100.0f);
	Object *hidden = r.make("MordorAttackTroll", far.x, far.y, enemy);
	r.frame(2);
	REQUIRE(statusAt(r, *hidden) != CELLSHROUD_CLEAR);
	CHECK(MouseoverTooltip::compose(*hidden, r.logic(), &text, in).text.empty());
}

TEST_CASE("hover2 retail: the HUD hands the enemy's name to the window manager's world tool tip while the pointer rests on it, and clears it on the ground")
{
	if (!haveWorld("hover2 mouse-over tool tip"))
	{
		return;
	}
	SharedWorld &s = shared();
	HoverHud h(s);
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 300.0f, false, {}, nullptr, 100.0f);
	Object *target = h.rig.make("MordorLumberMill", c.x, c.y, enemy);
	h.rig.make("GondorBarracks", c.x + 150.0f, c.y); // the local player's sight
	h.rig.lookAt({ c.x, c.y, h.rig.logic().getGroundHeight(c.x, c.y) }, 300.0f, 40.0f);
	h.rig.frame(30); // as the first case: 30 logic steps of 0.2 s clear the shroud around the barracks
	h.frames(2);
	REQUIRE(statusAt(h.rig, *target) == CELLSHROUD_CLEAR);
	const UnicodeString name = displayName(*target, h.text);
	REQUIRE_FALSE(name.empty());

	CHECK(h.hud->mouseoverTooltip().empty());
	h.pointAt(*target);
	REQUIRE(h.hud->input().ui().mouseoverObject() == target->getID());
	CHECK(h.hud->mouseoverTooltip() == name);
	CHECK(h.hud->windows().worldTooltip() == name);

	// a script's DISABLE_INPUT (a cutscene: InGameUI::applyScriptInput hides the pointer): no tool tip while the input is off; ENABLE_INPUT with the
	// pointer still there brings it back (review of #404)
	h.hud->scriptInput(false);
	h.frames(1);
	CHECK(h.hud->mouseoverTooltip().empty());
	CHECK(h.hud->windows().worldTooltip().empty());
	h.hud->scriptInput(true);
	h.frames(1);
	CHECK(h.hud->mouseoverTooltip() == name);

	// the object leaves a pointer that does not move: no tool tip; back under the pointer: its name again (ZH's Mouse::createStreamMessages posts the
	// pointer's position every frame, so the hover pick follows the object; review of #404)
	const Coord3D home = *target->getPosition();
	const Coord3D away = h.rig.freeSpot(home.x - 400.0f, home.y + 400.0f, 300.0f, false, {}, nullptr, 100.0f);
	// the hover pick reads the presented drawables, which take a teleport with the next logic frame: at most 7 HUD frames of 0.033 s at the rig's 0.2 s
	// logic step
	target->teleportTo(&away, false);
	h.frames(7);
	CHECK(h.hud->mouseoverTooltip().empty());
	CHECK(h.hud->windows().worldTooltip().empty());
	target->teleportTo(&home, false);
	h.frames(7);
	CHECK(h.hud->mouseoverTooltip() == name);
	CHECK(h.hud->windows().worldTooltip() == name);

	// the pointer on open ground (a MSG_MOUSEOVER_LOCATION_HINT): no tool tip
	const ICoord2D ground = h.rig.screenOf({ c.x - 250.0f, c.y - 250.0f, h.rig.logic().getGroundHeight(c.x - 250.0f, c.y - 250.0f) });
	h.hud->mouseMove(ground.x + 3, ground.y, 0);
	h.hud->mouseMove(ground.x, ground.y, 0);
	h.frames(1);
	REQUIRE(h.hud->input().ui().mouseoverObject() == INVALID_ID);
	CHECK(h.hud->mouseoverTooltip().empty());
	CHECK(h.hud->windows().worldTooltip().empty());

	const std::vector<std::string> stops = h.hud->stops();
	CHECK(std::count_if(stops.begin(), stops.end(), [](const std::string &l) { return l.rfind("[S-7187] mouse-over tool tip (HOVER-2)", 0) == 0; }) == 1);
}
