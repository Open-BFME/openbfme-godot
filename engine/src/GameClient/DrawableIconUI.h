// OpenBFME. GPL-3.0.
//
// Lane HUD-5 (the owner's report: no construction progress, no level / experience marks, no health bars over units): the drawable decorations retail draws
// over the 3D view each frame, as a list of 2D draw operations (window pixels) the device draws under the Palantir. Client only: it reads the logic and the
// drawables, never changes them.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp map: tier A / B, decomp sources DrawableConstructPercentRva002779CE.cpp, DrawableDrawVeterancy.cpp,
// Rva0027900BFinish.cpp, DrawableIconAnchorPosition.cpp, reverse/attempts/0x0027601b.cpp and 0x00270e48.cpp as donors):
//   * the queue (RW 0x679129, per drawable and frame): only while drawing icons is on and no script letterbox; a drawable with an object: its icon anchor
//     (RW 0x6778F4: the drawable's position, z + the template's HealthBoxHeightOffset (+0x548, default 10.0 RW 0xBD83D8) + the geometry's max height above the
//     position (RW 0xAD1990); a HORDE's is its contain's slot 0x22C, RW 0x87127D: the mean drawable position of its members and of H+0x170, not raised) and its health box (RW 0x68DF5B: none for KindOf IGNORED_IN_GUI; width = 2 * clamp(the
//     geometry's bounding circle radius (Object + 0xB8) * HealthBoxScale (+0x544, default 1.5 RW 0x73FE06), 20, 150), at least 20) projected on screen (off
//     screen: only an object under construction tries again a third of the way down to its position); the bar's rectangle is x0 = (int)(sx - w/zoom * 0.45),
//     y0 = (int)(sy - 2), x1 = (int)(x0 + w/zoom), y1 = (int)(y0 + 4) (Drawable + 0x460 .. + 0x46C). List 0 (every drawable with an object): the health bar
//     (RW 0x676346) and the construction text (RW 0x677CF9); list 2 (not dead, not IGNORED_IN_GUI): the veterancy marks (RW 0x67939F);
//   * the health bar (RW 0x676346, BFME2 0x67601B): GameData ShowObjectHealth (GlobalData + 0x9BD); the drawable is selected, or the object's container is a
//     selected HORDE, or the drawable is under the mouse; an invisible object (RW 0x68FC2F) gets none from an active local player that is not its ally (RW 0x6763DA); only a drawn drawable queues icons (RW 0x4853EB: an enemy's invisible object, clientLook 5, queues nothing); the
//     KindOf must be HERO, MACHINE, MONSTER, MINE, STRUCTURE or HAS_HEALTH_BAR, else Options.ini AllHealthBars (GlobalData + 0x9BE, RW 0x601CF9) and INFANTRY or
//     CAVALRY; a WALL_UPGRADE without its upgrade active, a DEFENSIVE_WALL without HAS_HEALTH_BAR, UNATTACKABLE, ROCK_VENDOR and HORDE draw none; no bar at
//     zero max health or zero health. The look (RW 0x674445: RW 0x671FD5 under construction, RW 0x6721CC KindOf STRUCTURE, else RW 0x6723C3; the building looks have frames 10 / 8 / 6
//     high and four rows: RW 0x670FDE's structure bands (tables RW 0xC10F28 / 0xC10F68 / 0xC10FA8), RW 0x670FA0's construction blend RW 0xC10EA8 -> 0xC10EE8; a unit's): an open rectangle 0x7F000000 (x0 - 3, y0 - 3, width + 6, 9), one 0xFFBA9252 (x0 - 2, y0 - 2, width + 4, 7), a filled 0xFF000000 (x0 - 1,
//     y0 - 1, width + 2, 5), then three 1 pixel rows (y0 + i) of width * ratio in the band colours of RW 0x671142 (>= 0.8 green, 0.6 .. 0.8 amber to green,
//     0.4 .. 0.6 amber, 0.2 .. 0.4 red to amber, below red; the tables RW 0xC11048 / 0xC11018 / 0xC10FE8);
//   * the construction text (RW 0x677CF9): an object UNDER_CONSTRUCTION and not SOLD: the game text CONTROLBAR:UnderConstructionDesc ("Building: %.0f%%") with the
//     percent, unless KindOf WALL_SEGMENT or DEFENSIVE_WALL (an empty text); at the icon anchor lowered a third of the way to the drawable's position, centred,
//     white with a black drop shadow, in the in-game UI's drawable caption font (not read: S-1951);
//   * the veterancy marks (RW 0x67939F -> drawVeterancy RW 0x6780DF): an object with an experience tracker whose rank is above 1 and that passes GameData's
//     VeterancyPipDrawObjectFilter; the images Good_Vet / Good_Vet_Dot, or Evil_Vet / Evil_Vet_Dot when the object's player template is Evil (RW 0x675530); the rank of the tracker's current level (none at rank <= 1 or for a level of RequiredExperience <= 1 without a next one); a horde member
//     draws none; a HORDE draws them on its banner carrier's drawable (H+0x26C), else its first member's (RW 0x67939F); rank / 5
//     big icons above rank % 5 dots, centred over the icon anchor's screen point lifted 7 pixels; sizes = image size * (screen / 1024 x 768) * 0.5 / zoom, 2 * that
//     scale between them. // INFERENCE / NOT PORTED (S-1950, S-1951): the other lists (1: the group number, 3, 5) and the caption, the drawable caption font, the wall upgrade test.

#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class DrawableManager;
class GameLogic;
class GameTextSource;
class MappedImageCollection;
class Player;
class TacticalView;
class ThingTemplate;
struct ObjectFilter;

struct IconUIOp
{
	enum Kind : std::uint8_t
	{
		OPEN_RECT,
		FILL_RECT,
		IMAGE,
		TEXT
	};
	Kind kind = FILL_RECT;
	float x = 0, y = 0, w = 0, h = 0; ///< window pixels; a TEXT op's x is its centre, y its top
	std::uint32_t color = 0xFFFFFFFFu; ///< 0xAARRGGBB
	std::uint32_t dropColor = 0;       ///< TEXT: the drop shadow
	std::string image;                 ///< IMAGE: a MappedImage name
	std::string text;                  ///< TEXT: UTF-8
	ObjectID object = 0;
};

struct IconUISettings
{
	bool showObjectHealth = false;       ///< GameData ShowObjectHealth
	bool allHealthBars = false;          ///< Options.ini AllHealthBars
	const ObjectFilter *veterancyFilter = nullptr; ///< GameData VeterancyPipDrawObjectFilter (null: no marks, reported)
	const MappedImageCollection *images = nullptr;
	const GameTextSource *text = nullptr;
	float zoom = 1.0f;                   ///< View::getZoom (+0x124)
};

class DrawableIconUI
{
public:
	// the ops of this frame, in retail's list order (0, then 2), drawables in id order within a list
	void build(GameLogic &logic, const DrawableManager &drawables, const TacticalView &view, const IconUISettings &settings, const std::set<ObjectID> &selected,
		ObjectID mousedOver, std::vector<IconUIOp> &out);
	const std::set<std::string> &notes() const { return m_notes; }
	static std::vector<std::string> acceptanceStops();

private:
	struct TemplateBox
	{
		float radius = 0.0f;       ///< GeometryInfo + 0x10 (the bounding circle of the active shapes)
		float maxHeight = 0.0f;    ///< RW 0xAD1990
		float scale = 1.5f;        ///< HealthBoxScale
		float heightOffset = 10.0f; ///< HealthBoxHeightOffset
	};
	const TemplateBox &boxOf(const ThingTemplate &tt);
	std::map<const ThingTemplate *, TemplateBox> m_boxes;
	std::set<std::string> m_notes;
};
