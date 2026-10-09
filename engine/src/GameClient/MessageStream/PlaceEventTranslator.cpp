// OpenBFME. GPL-3.0.
// See PlaceEventTranslator.h.

#include "GameClient/MessageStream/PlaceEventTranslator.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const unsigned kPlaceOptions = LLF_TERRAIN_RESTRICTIONS | LLF_CLEAR_PATH | LLF_NO_OBJECT_OVERLAP | LLF_USE_QUICK_PATHFIND | LLF_SHROUD_REVEALED;
}

void PlaceEventTranslator::updateGhost(const ICoord2D &pixel)
{
	Coord3D world;
	if (!m_ctx.view.screenToTerrain(pixel, m_ctx.logic, world))
	{
		return;
	}
	const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate());
	if (!tt)
	{
		return;
	}
	float angle = m_ctx.ui.placeAngle();
	if (m_down)
	{
		const float dx = SimMath::subf32(world.x, m_anchor.x), dy = SimMath::subf32(world.y, m_anchor.y);
		if (SimMath::sumSquares2(dx, dy) > 25.0f)
		{
			angle = (float)SimMath::atan2d(dy, dx); // ZH: the ghost faces from the anchor to the pointer
		}
		world = m_anchor;
	}
	Object *builder = m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource());
	const LegalBuildCode code = BuildPlacement::isLocationLegalToBuild(m_ctx.logic, world, *tt->getFinalOverride(), angle, kPlaceOptions, builder, builder ? builder->getControllingPlayer() : nullptr);
	m_ctx.ui.setPlaceGhost(world, angle, (int)code);
	m_ctx.ui.setCursor(code == LBC_OK ? MouseCursorName::Arrow : MouseCursorName::GenericInvalid);
}

std::vector<std::string> PlaceEventTranslator::stopLines()
{
	return { "[S-1770] wall line build (QA2-FIX): RotWK's PlaceEventTranslator line build branch is ported (RW 0x83E911 .. 0x83E9B8: MSG_WALL_HUB_CONSTRUCT_SPAN from the hub to the "
		     "click with the build button's Options; the flag of RW 0x6A2D26); not ported: the release's restricted-area test (RW 0x707141, code 8), the PORT snap of the "
		     "location (template + 0x11F & 0x10, RW 0x794C63), the release's canMakeUnit check for a non-line build (BUILD-1's path is kept) and the wall's line ghost "
		     "(RW 0x6A2D38 ff); the build button is kept with the placement instead of being the GUI command (RW 0x94089B)" };
}

void PlaceEventTranslator::frame()
{
	// RW 0x6A2AE5: the source object (vslot 0xE4) and the pending template (+0x53C) must exist; RW 0x6A2B6D: the line build test; RW 0x6A2D12: not yet started and
	// not anchored (RW 0x6A2B95 takes the anchored branch first) -> vslot 0x100(1)
	if (!m_ctx.ui.isPlacing() || m_down || m_ctx.ui.isLineBuildStarted())
	{
		return;
	}
	const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate());
	const Object *builder = m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource());
	if (tt && BuildAssistant::isLineBuildTemplate(m_ctx.logic, tt, builder))
	{
		m_ctx.ui.setLineBuildStarted(true);
	}
}

MessageDisposition PlaceEventTranslator::releaseLineBuild(const ThingTemplate &tt, Object &builder)
{
	// RW 0x83E771: the builder's canMakeUnit (slot 0x64) with build index -1
	switch (BuildAssistant::canMakeUnit(builder, &tt, -1))
	{
		case CANMAKE_OK:
		case CANMAKE_NO_PREREQUISITES:
			break;
		case CANMAKE_NO_MONEY:
			m_ctx.ui.message("GUI:NotEnoughMoneyToBuild");
			return MessageDisposition::Keep; // RW 0x83E7E4: the message, the mode stays (the translator answers 0)
		case CANMAKE_QUEUE_FULL:
			m_ctx.ui.message("GUI:ProductionQueueFull");
			return MessageDisposition::Keep;
		case CANMAKE_PARKING_PLACES_FULL:
			m_ctx.ui.message("GUI:ParkingPlacesFull");
			return MessageDisposition::Keep;
		case CANMAKE_MAXED_OUT_FOR_PLAYER:
			m_ctx.ui.message("GUI:UnitMaxedOut");
			return MessageDisposition::Keep;
		default:
			m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID); // RW 0x83E7E9: vslot 0xDC(0, 0)
			return MessageDisposition::Keep;
	}
	if (!m_ctx.ui.isLineBuildStarted())
	{
		return MessageDisposition::Destroy; // RW 0x83E8A1: setPlacementStart(null) (the anchor was dropped with m_down)
	}
	// RW 0x83E932 .. 0x83E9B8
	const CommandButton *command = m_ctx.ui.placeBuildCommand();
	ClientMessage &m = m_ctx.stream.append(MSG_WALL_HUB_CONSTRUCT_SPAN);
	m.appendInteger((int)tt.getFinalOverride()->getTemplateID());
	m.appendLocation(*builder.getPosition());
	Coord3D click;
	if (!m_ctx.view.screenToTerrain(m_anchorPixel, m_ctx.logic, click)) // RW 0x83E745: the start pixel's terrain point now (TheTacticalView slot 0x168)
	{
		click = m_anchor; // INFERENCE: RotWK does not test the answer (RW 0x83E745); a pixel off the terrain keeps the press's point
	}
	m.appendLocation(click);
	m.appendInteger(command ? (int)command->m_options : 0xFFFFFE);
	m.appendObjectID(builder.getID());
	++m_stats.wallSpans;
	m_ctx.ui.setLineBuildStarted(false);
	m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID); // RW 0x83E6A4
	return MessageDisposition::Destroy;
}

MessageDisposition PlaceEventTranslator::translate(const ClientMessage &msg)
{
	if (!m_ctx.ui.isPlacing())
	{
		m_down = false;
		if (m_eatClick && (msg.type() == CMSG_MOUSE_LEFT_CLICK || msg.type() == CMSG_MOUSE_LEFT_DOUBLE_CLICK))
		{
			m_eatClick = false;
			return MessageDisposition::Destroy; // the release that ended the mode derived this click: it must not order a move
		}
		if (msg.type() == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN)
		{
			m_eatClick = false;
		}
		return MessageDisposition::Keep;
	}
	switch (msg.type())
	{
		case CMSG_RAW_MOUSE_POSITION:
			updateGhost(msg.arg(0).pixel);
			return MessageDisposition::Keep; // the other translators still track the pointer (cursor hints, scrolling)
		case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		{
			Coord3D world;
			if (m_ctx.view.screenToTerrain(msg.arg(0).pixel, m_ctx.logic, world))
			{
				m_down = true;
				m_anchor = world;
				m_anchorPixel = msg.arg(0).pixel;
				updateGhost(msg.arg(0).pixel);
			}
			return MessageDisposition::Destroy;
		}
		case CMSG_RAW_MOUSE_LEFT_BUTTON_UP:
		{
			const bool wasDown = m_down;
			m_down = false;
			m_eatClick = wasDown;
			if (wasDown)
			{
				const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate());
				Object *builder = m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource());
				if (tt && builder && BuildAssistant::isLineBuildTemplate(m_ctx.logic, tt, builder))
				{
					return releaseLineBuild(*tt, *builder); // lane QA2-FIX: RW 0x83E911 (see the header)
				}
			}
			if (!wasDown || !m_ctx.ui.placeHasGhost())
			{
				return MessageDisposition::Destroy;
			}
			if (m_ctx.ui.placeLegalCode() != LBC_OK)
			{
				++m_stats.refused;
				m_ctx.ui.message("GUI:CantBuildThere"); // ZH: the illegal site keeps the mode
				return MessageDisposition::Destroy;
			}
			const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate());
			Object *builder = m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource());
			if (!tt || !builder)
			{
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
				return MessageDisposition::Destroy;
			}
			// the construct message (RW 0x77B011 / 0x77A91B layout): { template id, location, angle }; a plot builds by MSG_FOUNDATION_CONSTRUCT
			ClientMessage &m = m_ctx.stream.append(builder->isKindOfName("BASE_FOUNDATION") ? MSG_FOUNDATION_CONSTRUCT : MSG_DOZER_CONSTRUCT);
			m.appendInteger((int)tt->getFinalOverride()->getTemplateID());
			m.appendLocation(m_ctx.ui.placeLocation());
			m.appendReal(m_ctx.ui.placeAngle());
			++m_stats.placed;
			if (!(msg.arg(1).integer & (KEY_STATE_LSHIFT | KEY_STATE_RSHIFT)))
			{
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
			}
			return MessageDisposition::Destroy;
		}
		case CMSG_MOUSE_LEFT_CLICK:
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
			return MessageDisposition::Destroy; // the release did the work
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		case CMSG_MOUSE_RIGHT_CLICK:
			if (msg.type() == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN)
			{
				++m_stats.cancelled;
				m_down = false;
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
				m_ctx.ui.setCursor(MouseCursorName::Arrow);
			}
			return MessageDisposition::Destroy;
		case CMSG_RAW_KEY_DOWN:
			if (msg.arg(0).integer == KEY_ESC)
			{
				++m_stats.cancelled;
				m_down = false;
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
				m_ctx.ui.setCursor(MouseCursorName::Arrow);
				return MessageDisposition::Destroy;
			}
			return MessageDisposition::Keep;
		default:
			return MessageDisposition::Keep;
	}
}
