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
// INFERENCE (stop S-306): the RotWK PlaceEventTranslator body was not read: the rotation drag, the shift to keep placing, the cursor names (GenericInvalid for an illegal site)
// and the line build of walls (BeginPathBuild, a separate message) are ZH's; the clear-path and shroud options are those the legality code can decide (S-301).

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

class PlaceEventTranslator : public MessageTranslator
{
public:
	explicit PlaceEventTranslator(HudContext &ctx) : m_ctx(ctx) {}
	MessageDisposition translate(const ClientMessage &message) override;

	// the legality code the ghost shows for the current pointer location (LegalBuildCode)
	struct Stats
	{
		unsigned long long placed = 0, refused = 0, cancelled = 0;
	};
	const Stats &stats() const { return m_stats; }

private:
	void updateGhost(const ICoord2D &pixel);
	HudContext &m_ctx;
	bool m_down = false;
	bool m_eatClick = false; // the click MetaEvent derives from the release that placed (or refused) a building is the placement's too
	Coord3D m_anchor;
	Stats m_stats;
};
