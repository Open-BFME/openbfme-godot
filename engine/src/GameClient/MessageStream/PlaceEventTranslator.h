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
// Lane BUILD-5 (the owner played preview.4: the ghost "close but not accurate", no right-drag scroll while placing): the translator is RotWK's, TARGET FACTS
// (RotWK game.dat, caveat S-001; RW 0x83E486 is tier B "same-shape" with BFME2 0x83218A, whose decomp has no source for it; read statically; RotWK's message
// numbers: 3 mouse position, 4 left down, 0xE right down, 0x10 right up, 0x17 / 0x18 left click / double click, 0x1B / 0x1C right click / double click):
//   * the right button is tracked in every mode and never eaten: the press (0xE) keeps its pixel and time and clears the dragged flag (+0x14 / +0x15), a move
//     (3) while it is down sets the flag when the pointer leaves the drag tolerance (RW 0x5ED4B9), the release (0x10) keeps its time; so the LookAt translator
//     after it (priority 60) scrolls the view while the button is held and dragged, during a placement too;
//   * the right click (0x1B / 0x1C, MetaEvent derives it from every release) cancels a placement unless the press was a drag (MouseSettings::isRightClick:
//     dragged AND held DragToleranceMS or longer): setGUICommand(null) (vslot 0xBC) and placeBuildAvailable(null) (vslot 0xDC), the click is eaten; a drag
//     leaves the mode and passes the click on;
//   * the left press (4) of a placement that is not anchored anchors it at the pressed pixel (setPlacementStart, vslot 0xE8) and is eaten; no builder object
//     ends the mode instead;
//   * a move while anchored sets the anchor's end (setPlacementEnd, vslot 0xEC) once the pointer is 5 pixels (RW 0xBDAE58, the root of the integer sum of
//     squares) or more from the start, and eats the move; the placement update turns the ghost from the start's terrain point to the end's (RW 0x6A2BFD:
//     Coord2D::toAngle RW 0x405406) and draws the W_Arrow model there (W3DInGameUI::drawPlaceAngle RW 0x48EAD8);
//   * the left click (0x17 / 0x18) of an anchored placement places it: the angle is the ghost's (vslot 0xF8), the location the terrain point of the anchor's
//     start now (vslot 0xF4, TheTacticalView slot 0x168); the builder's canMakeUnit (BuildAssistant slot 0x64) refusals keep or end the mode (see below);
//     isLocationLegalToBuild (slot 0x44) with the options 0x49F (the ghost's 0x48F plus 0x10, ZH's SHROUD_REVEALED); legal: MSG_DOZER_CONSTRUCT (0x41A)
//     { template id, location, angle } and the mode ends (RW 0x83E6A4) - there is no shift key to keep placing; illegal: displayCantBuildMessage (RW 0x69CCEC,
//     the label of the code), MiscAudio BadBuildingPlacementSound (+0xE8, RW 0x83EAC5), the anchor is dropped (setPlacementStart(null)) and the mode stays.
//   * the placement update (W3DInGameUI RW 0x6A2AE5, once per client frame): the ghost stands at the pointer (the live mouse position, Mouse + 0x4F0C) or, anchored,
//     at the anchor's start, re-projected each frame (a scrolled view moves it with the screen); its legality (options 0x48F) is asked on odd client frames
//     only (RW 0x6A3939: TheGameClient vslot 0x7C & 1).
// Not ported (stop S-306): the options' bits 0x80 and 0x400 (not identified), the message 0x7EB an illegal click appends and the one RW 0x83EA10 appends
// after the construct message, the PlacementProjection start point of getPlacementPoints, and the illegal site's code 8 (S-1770); the cursor is
// InGameUI's (mode cursor Target, createCommandHint: Build / InvalidBuild while placing).
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
// of RW 0x83EA43) and the wall's line ghost (the hub and segment drawables RW 0x6A2D38 ff draw along the pointer). Lane BUILD-5 ports the release's canMakeUnit
// check for every build (RW 0x83E771) and, r2, the PORT snap (template + 0x11F & 0x10, BuildAssistant::getPortTransform RW 0x794C63) of the ghost (RW 0x6A37AD)
// and of the click after its legality test (RW 0x83E8D4).

#pragma once

#include "GameLogic/BuildPlacement.h"
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
	// lane BUILD-5: the placement update's ghost step (RW 0x6A2AE5; ZH InGameUI::handleBuildPlacements), once per client frame after the stream ran: the ghost's
	// location and angle from the pointer or the anchor, its legality on odd client frames
	// lane BUILD-5 r3: `judge` false while the logic worker runs (the device's busy frames): only the pose (the pointer or the anchor projected through the
	// displayed view, a PORT's snap: static terrain data) is updated; the builder lookup and the legality (live objects) wait for an idle frame
	void handleBuildPlacements(bool judge = true);
	// lane BUILD-5 r3: the live pointer of the frame drawn (RotWK reads Mouse + 0x4F0C, not the message queue): the device hands it in every render frame,
	// also while its input waits for the worker
	void setLivePointer(const ICoord2D &p)
	{
		m_pointer = p;
		m_havePointer = true;
	}
	// the acceptance stop of the line build (S-1770), one "[S-1770] ..." line (S-306, the placement mode, is BuildStops' line)
	static std::vector<std::string> stopLines();

	// the ghost's legality options (RW 0x6A39D6 pushes 0x48F) and the click's (RW 0x83E80D pushes 0x49F): the bits identified with ZH's flags
	static unsigned ghostOptions();
	static unsigned placeOptions();
	// RotWK InGameUI::displayCantBuildMessage (RW 0x69CCEC, BFME2 decomp InGameUIDisplayCantBuild.cpp, tier A): the label of a refused site's code
	static const char *cantBuildLabel(int legalBuildCode);

	struct Stats
	{
		unsigned long long placed = 0, refused = 0, cancelled = 0, wallSpans = 0, rightDrags = 0;
	};
	const Stats &stats() const { return m_stats; }
	// the right button as the translator tracks it (tests)
	bool rightDown() const { return m_rightDown; }
	bool rightDragged() const { return m_rightDragged; }

private:
	MessageDisposition releaseLineBuild(const ThingTemplate &tt, Object &builder);
	MessageDisposition leftClick(const ClientMessage &msg);
	void endPlacement();
	HudContext &m_ctx;
	ICoord2D m_pointer{};      // the live pointer (RotWK reads Mouse + 0x4F0C; here the last mouse position message)
	bool m_havePointer = false;
	std::string m_portTemplate;   // lane BUILD-5 r3: the template whose PORT kind m_port and snap view m_portView cache (read on an idle frame)
	bool m_port = false;
	BuildPlacement::PortSnapView m_portView; // r4: the PORT snap's shapes and terrain view, so a busy-worker frame snaps without the logic's caches
	// the right button (PlaceEventTranslator + 0x4 .. + 0x15)
	ICoord2D m_rightAnchor{};
	unsigned m_rightDownMs = 0, m_rightUpMs = 0;
	bool m_rightDown = false, m_rightDragged = false;
	Stats m_stats;
};
