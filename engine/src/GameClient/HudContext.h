// OpenBFME. GPL-3.0.
//
// What the input translators and the control bar read of the running game (lane HUD-1). ZH reaches these through globals (TheGameLogic, ThePlayerList,
// TheTacticalView, TheInGameUI, TheMessageStream, TheMouse); the port passes them in one struct so a test can build a game of its own and two games
// can run in one process.

#pragma once

#include "GameClient/DrawablePick.h"
#include "GameClient/InGameUI.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameLogic.h"

#include <functional>
#include <string>

class AIWorld;
class ArchiveFileSystem;
class CommandStore;
class MessageStream;
class Object;
class Player;

// ZH Mouse::m_dragTolerance / m_dragTolerance3D / m_dragToleranceMS: the `Mouse` block of Data\INI\Mouse.ini (retail: 15, 5, 150)
struct MouseSettings
{
	int dragTolerance = 0;
	int dragTolerance3D = 0;
	int dragToleranceMS = 0;
	// reads the three keys from Data\INI\Mouse.ini; false + *error when the file or a key is missing or malformed (no default)
	static bool load(ArchiveFileSystem &fs, MouseSettings &out, std::string *error);
};

struct HudContext
{
	GameLogic &logic;
	AIWorld *ai;               ///< the objects' geometry for picking (AIWorld::movementInfo); null: every object picks as a sphere of radius 10 (stop S-284)
	TacticalView &view;
	InGameUI &ui;
	MessageStream &stream;
	const CommandStore *commands;   ///< the command buttons the control bar reads (null until the HUD installs it)
	MouseSettings mouse;
	// lane QA-1: the pick ray against what the object's drawable shows (GameClient/DrawablePick: ZH W3DView::pickDrawable casts the ray at the scene's
	// render objects). Set by the HUD from the live game's drawables; empty: every object picks by its geometry (S-284)
	std::function<DrawablePick::Result(const Object &, const Coord3D &, const Coord3D &, float *)> pickRay;

	Player *localPlayer() const { return logic.players().getLocalPlayer(); }
};
