// OpenBFME. GPL-3.0.
// See PlaceEventTranslator.h.

#include "GameClient/MessageStream/PlaceEventTranslator.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
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
				updateGhost(msg.arg(0).pixel);
			}
			return MessageDisposition::Destroy;
		}
		case CMSG_RAW_MOUSE_LEFT_BUTTON_UP:
		{
			const bool wasDown = m_down;
			m_down = false;
			m_eatClick = wasDown;
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
