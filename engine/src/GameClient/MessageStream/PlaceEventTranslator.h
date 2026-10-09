// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlaceEventTranslator (ZH Include/GameClient/PlaceEventTranslator.h, Source/GameClient/MessageStream/PlaceEventTranslator.cpp), lane BUILD-1: while a build button waits for its
// site (InGameUI::placeBuildTemplate), the pointer moves the placement ghost (the legality of the site is BuildPlacement's), a left press anchors a rotation, the release places
// the building (MSG_DOZER_CONSTRUCT for a dozer, MSG_FOUNDATION_CONSTRUCT for a build plot) and a right click or Escape cancels. The press and the clicks are eaten, so no
// other translator selects or orders anything meanwhile.
//
// DONOR FACTS (ZH PlaceEventTranslator.cpp): attached at priority 30; MSG_RAW_MOUSE_POSITION updates the anchor-relative angle (the ghost turns to face from the left-press anchor
// to the pointer while the button is down); the left release (MSG_RAW_MOUSE_LEFT_BUTTON_UP) with a legal location (`isLocationLegalToBuild` with TERRAIN_RESTRICTIONS | CLEAR_PATH |
// NO_OBJECT_OVERLAP | USE_QUICK_PATHFIND | SHROUD_REVEALED) sends the construct message with { template id, location, angle } and ends the mode unless the shift key holds
// it; an illegal site keeps the mode; a right press or Escape ends it.
// TARGET (RW 0x77B011 / 0x77A91B, GameLogicDispatch): the message layout is { int template id, location, real angle }; the builder is the first object of the logic's selection group.
// INFERENCE (stop S-306): the RotWK PlaceEventTranslator body was read for the line build only (below); for other builds the rotation drag, the shift to keep placing, the cursor names (GenericInvalid for an illegal site)
// and the clear-path and shroud options are those the legality code can decide (S-301).
//
// The wall span (lane QA2-FIX, QA-2 #3; TARGET FACTS, RotWK game.dat S-001 caveat, static disassembly of RotWK's PlaceEventTranslator RW 0x83E486 and the placement
// update of W3DInGameUI RW 0x6A2AE5; TheInGameUI's vtable RW 0xC134E8):
//   * a line build is a pending template and a source object that are both KindOf WALL_HUB (BuildAssistant slot 0x60, RW 0x793E33): the Begin Wall Span button of
//     a hub (DOZER_CONSTRUCT from the hub);
//   * the placement update, once per client frame (RW 0x6A2D08 .. 0x6A2D33): a line build that is not anchored sets TheInGameUI's line build flag (+0x8C6, vslot 0x100(1))
//     the first time; frame() is that step;
//   * the left press anchors as for any build (RW 0x83E4FC .. 0x83E578: setPlacementStart at the pressed pixel);
//   * the left release (RW 0x83E6D0 ..): the location is the terrain under the anchor's start pixel (getPlacementPoints vslot 0xF4, TheTacticalView slot 0x168); the
//     builder's canMakeUnit (BuildAssistant slot 0x64) answering 2 / 4 / 5 / 6 shows GUI:NotEnoughMoneyToBuild / ProductionQueueFull / ParkingPlacesFull / UnitMaxedOut
//     and keeps the mode, any other refusal than 0 / 1 ends it; a line build skips the legality test (RW 0x83E7FE); without the flag the release only drops the anchor
//     (RW 0x83E8AA: setPlacementStart(null)); with it the release sends MSG_WALL_HUB_CONSTRUCT_SPAN (0x463, RW 0x83E93A) { int the pending template id (word + 0x5E8),
//     location the source object's position (+0x38), location the click, int the pending command button's Options (+0x1C; 0xFFFFFE without one, RW 0x83E992),
//     object id the source object (+0x74) }, clears the flag (vslot 0x100(0)) and the anchor (vslot 0x104) and ends the mode (RW 0x83E6A4: vslots 0xBC(0), 0xDC(0, 0)):
//     no shift to keep placing.
// Not ported (stop S-1770): the restricted-area test of the release (RW 0x707141: the local player's areas against the template's geometry, code 8, then the message
// of RW 0x83EA43), the PORT snap of the location (template + 0x11F & 0x10, BuildAssistant RW 0x794C63) and the release's canMakeUnit check for a non-line build (the
// port keeps BUILD-1's), and the wall's line ghost (the hub and segment drawables RW 0x6A2D38 ff draw along the pointer).

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

class Object;
class ThingTemplate;

class PlaceEventTranslator : public MessageTranslator
{
public:
	explicit PlaceEventTranslator(HudContext &ctx) : m_ctx(ctx) {}
	MessageDisposition translate(const ClientMessage &message) override;
	// the line build step of the placement update (RW 0x6A2D08 .. 0x6A2D33), once per client frame before the stream runs (ZH: InGameUI::update precedes propagateMessages)
	void frame();
	// the acceptance stop of the line build (S-1770), one "[S-1770] ..." line
	static std::vector<std::string> stopLines();

	// the legality code the ghost shows for the current pointer location (LegalBuildCode)
	struct Stats
	{
		unsigned long long placed = 0, refused = 0, cancelled = 0, wallSpans = 0;
	};
	const Stats &stats() const { return m_stats; }

private:
	void updateGhost(const ICoord2D &pixel);
	MessageDisposition releaseLineBuild(const ThingTemplate &tt, Object &builder);
	HudContext &m_ctx;
	bool m_down = false;
	bool m_eatClick = false; // the click MetaEvent derives from the release that placed (or refused) a building is the placement's too
	Coord3D m_anchor;
	ICoord2D m_anchorPixel{};
	Stats m_stats;
};
