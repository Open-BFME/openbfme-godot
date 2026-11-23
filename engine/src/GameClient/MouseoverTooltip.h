// OpenBFME. GPL-3.0.
//
// MouseoverTooltip (lane HOVER-2): the tool tip RotWK shows next to the pointer while it rests on an object in the world (an enemy's unit, a creep, a
// neutral building): its name, in a network game with its player's name below. The text goes to the same Mouse::setCursorTooltip the menus use
// (CursorTooltip.h, lane HOVER-1) and GuiFX.apt's ToolTip clip draws it.
//
// TARGET FACTS (RotWK game.dat RW 0x69F637 InGameUI::createMouseoverHint, caveat S-001; the BFME2 decomp is the donor: InGameUICreateMouseoverHint.cpp
// lines 443 .. 535, tier B: RotWK's body has the decomp's string references in the decomp's order: SpecialDisguiseUpdate RW 0x69F7B9, "ThingTemplate:%s"
// RW 0x69F95C, SupplyWarehouseDockUpdate RW 0x69F9C0, "TOOLTIP:SupplyWarehouse" RW 0x69FA09, "OBJECT:Prop" RW 0x69FB7C, GlobalData + 0x9B8 (DebugAI)
// RW 0x69FC31):
//   * a MSG_MOUSEOVER_DRAWABLE_HINT first clears the request (setCursorTooltip(""), RW 0x5EE1A6), then, for the hint's object:
//     - the player: the contain's apparent controlling player for the local player, else the object's controlling player;
//     - the name: the template's DisplayName text, overridden by the drawable's script caption and the object's own name when they are set; an empty name
//       is the game text "ThingTemplate:<template name>"; an object with a SupplyWarehouseDockUpdate appends "TOOLTIP:SupplyWarehouse" with its value;
//     - with a player: in a multiplayer game (TheRecorder) whose player is a playable side the text is "<name>\n<player's display name>", else the name;
//     - shown only when the local player's cell status at the object is CLEAR (or, for a TREE, at the ground under the pointer), never for a name equal
//       to the text of "OBJECT:Prop";
//     - an object of the local player that is STRUCTURE, INFANTRY, CAVALRY, MONSTER or MACHINE, and any LINKED_TO_FLAG object, hands a TooltipRecord
//       (object id, context) to the control bar instead; its ordinary tool tip is then sent only with GameData's DebugAI or a SupplyWarehouseDockUpdate;
//   * a MSG_MOUSEOVER_LOCATION_HINT sets no tool tip.
// The request's colour (the indicator colour, a garrison's apparent owner, a disguise's player colour) is not kept: RotWK's Mouse.ini has
// UseTooltipAltTextColor = No, so Mouse::setCursorTooltip (RW 0x5EE1A6, BFME2 decomp MouseRva001EEA6D.cpp) leaves the text colour at TooltipTextColor and
// writes the player colour only to the alternative background colour, which the GuiFX path (RW 0x5EE7A8 -> RW 0x782DD9) does not pass.
// INFERENCE (S-7187): ZH's window manager clears the request on every mouse event (GameWindowManager::winProcessMouseEvent) before a window or
// createMouseoverHint sets it; the port clears it every frame the pointer is over the HUD, scrolling or selecting, or rests on no object. TheRecorder's
// multiplayer test is read as the network game flag (InGameUI::networkGame, RW 0x441B7C) and the playable-side test (decomp rva002AA245) as the
// player template's PlayableSide.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameClient/GUI/GameWindow.h"

#include <functional>
#include <string>
#include <vector>

class GameLogic;
class GameTextSource;
class Object;
class Player;

class MouseoverTooltip
{
public:
	struct Inputs
	{
		const Player *localPlayer = nullptr;
		bool networkGame = false;      ///< TheRecorder's multiplayer test (INFERENCE, see above)
		bool debugAI = false;          ///< GameData DebugAI (GlobalData + 0x9B8)
		bool haveDebugAI = false;      ///< false: the GameData had no DebugAI row (named in `unported`)
		std::function<bool(Coord3D &)> groundUnderPointer; ///< the tactical view's ground point under the pointer (a TREE's shroud test)
	};
	struct Result
	{
		UnicodeString text;              ///< the request ("" none)
		bool controlBarRecord = false;   ///< the TooltipRecord went to the control bar instead (not ported: S-7187)
		std::vector<std::string> unported;
	};
	// RW 0x69F637's MSG_MOUSEOVER_DRAWABLE_HINT branch for `obj` (the request after its clear)
	static Result compose(const Object &obj, GameLogic &logic, const GameTextSource *text, const Inputs &in);
	static std::vector<std::string> acceptanceStops();
};
