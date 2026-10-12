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
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <variant>

namespace
{
constexpr float kAnchorEndDistance = 5.0f; // RW 0xBDAE58: the pixels the pointer must leave the anchor's start before the ghost turns

// the template's PlacementViewAngle (template + 0x4E0): the angle placeBuildAvailable gives the new ghost (RW 0x69C755 -> setOrientation RW 0x70C31E)
float placementViewAngle(const ThingTemplate &tt)
{
	if (const FieldValue *v = tt.findField("PlacementViewAngle"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}

// KindOf PORT (template + 0x11F & 0x10)
bool port(GameLogic &logic, const ThingTemplate &tt)
{
	static const int bit = ObjectTemplateInfoBuilder::kindOfIndex("PORT");
	return bit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

// Coord2D::toAngle (RW 0x405406, BFME2 decomp coord2d.cpp:420, tier A): 0 for a zero vector, else acos of x / length clamped to [-1, 1], negative for y < 0.
// The angle goes into MSG_DOZER_CONSTRUCT, so it is computed with the deterministic maths (RotWK's ACos RW 0x42F4F0 is the CRT's: parity is stop S-167)
float toAngle(float x, float y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = SimMath::divf32(x, len);
	c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
	const float a = (float)SimMath::acosDet((double)c);
	return y < 0.0f ? -a : a;
}
} // namespace

unsigned PlaceEventTranslator::ghostOptions()
{
	// RW 0x6A39D6: 0x48F = 0x0F | 0x80 | 0x400; 0x0F are ZH's TERRAIN_RESTRICTIONS | CLEAR_PATH | NO_OBJECT_OVERLAP | USE_QUICK_PATHFIND (S-306: 0x80, 0x400 not identified)
	return LLF_TERRAIN_RESTRICTIONS | LLF_CLEAR_PATH | LLF_NO_OBJECT_OVERLAP | LLF_USE_QUICK_PATHFIND;
}

unsigned PlaceEventTranslator::placeOptions()
{
	// RW 0x83E80D: 0x49F, the ghost's plus 0x10 (ZH's SHROUD_REVEALED)
	return ghostOptions() | LLF_SHROUD_REVEALED;
}

const char *PlaceEventTranslator::cantBuildLabel(int code)
{
	// RW 0x69CCEC by the code's meaning (RotWK numbers the codes 2 no clear path, 3 too close to supplies, 4 shroud, 5 not flat, 6 restricted terrain,
	// 8 objects in the way; BuildPlacement keeps ZH's enum order)
	switch ((LegalBuildCode)code)
	{
		case LBC_NO_CLEAR_PATH: return "GUI:CantBuildNoClearPath";
		case LBC_TOO_CLOSE_TO_SUPPLIES: return "GUI:CantBuildTooCloseToSupplies";
		case LBC_SHROUD: return "GUI:CantBuildShroud";
		case LBC_NOT_FLAT_ENOUGH: return "GUI:CantBuildNotFlatEnough";
		case LBC_RESTRICTED_TERRAIN: return "GUI:CantBuildRestrictedTerrain";
		case LBC_OBJECTS_IN_THE_WAY: return "GUI:CantBuildObjectsInTheWay";
		default: return "GUI:CantBuildThere";
	}
}

std::vector<std::string> PlaceEventTranslator::stopLines()
{
	return {
		"[S-1770] wall line build (QA2-FIX): RotWK's PlaceEventTranslator line build branch is ported (RW 0x83E911 .. 0x83E9B8: MSG_WALL_HUB_CONSTRUCT_SPAN from the hub to the "
		"click with the build button's Options; the flag of RW 0x6A2D26); not ported: the release's restricted-area test (RW 0x707141, code 8) and the wall's "
		"line ghost (RW 0x6A2D38 ff); the build button is kept with the placement instead of being the GUI command (RW 0x94089B)" };
}

void PlaceEventTranslator::frame()
{
	// RW 0x6A2AE5: the source object (vslot 0xE4) and the pending template (+0x53C) must exist; RW 0x6A2B6D: the line build test; RW 0x6A2D12: not yet started and
	// not anchored (RW 0x6A2B95 takes the anchored branch first) -> vslot 0x100(1)
	if (!m_ctx.ui.isPlacing() || m_ctx.ui.isPlacementAnchored() || m_ctx.ui.isLineBuildStarted())
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

void PlaceEventTranslator::handleBuildPlacements(bool judge)
{
	// RW 0x6A2AE5 (ZH InGameUI::handleBuildPlacements): the builder object (vslot 0xE4) and the pending template (+0x53C) must exist
	if (!m_ctx.ui.isPlacing())
	{
		return;
	}
	const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate()); // the template store does not change during a game
	Object *builder = judge ? m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource()) : nullptr;
	if (!tt || (judge && !builder))
	{
		return;
	}
	if (judge && m_portTemplate != tt->getName())
	{
		m_portTemplate = tt->getName();
		m_port = port(m_ctx.logic, *tt);
		m_port = m_port && BuildPlacement::portSnapView(m_ctx.logic, *tt->getFinalOverride(), m_portView);
	}
	const bool isPort = m_portTemplate == tt->getName() && m_port;
	if (!judge && m_portTemplate != tt->getName())
	{
		return; // the template's PORT kind (ThingTemplate info) and snap view are read on an idle frame; until the first one the ghost has no pose
	}
	// the angle the ghost has (+0x544 slot 0's angle): the template's PlacementViewAngle until the first update
	float angle = m_ctx.ui.placeHasGhost() ? m_ctx.ui.placeSlotAngle() : placementViewAngle(*tt->getFinalOverride());
	ICoord2D pixel = m_pointer;
	if (m_ctx.ui.isPlacementAnchored())
	{
		// RW 0x6A2BA3: the anchor's points; the ghost stands at the start; once the end differs the angle faces from the start's terrain point to the end's
		const ICoord2D start = m_ctx.ui.placementStart(), end = m_ctx.ui.placementEnd();
		pixel = start;
		if (start.x != end.x || start.y != end.y)
		{
			Coord3D a, b;
			if (m_ctx.view.screenToTerrain(end, m_ctx.logic, b) && m_ctx.view.screenToTerrain(start, m_ctx.logic, a))
			{
				angle = toAngle(SimMath::subf32(b.x, a.x), SimMath::subf32(b.y, a.y));
			}
		}
	}
	else if (!m_havePointer)
	{
		return;
	}
	Coord3D world;
	if (!m_ctx.view.screenToTerrain(pixel, m_ctx.logic, world))
	{
		return; // INFERENCE: RotWK does not test TheTacticalView slot 0x168's answer (RW 0x6A346B); a pixel off the terrain leaves the ghost where it was
	}
	// lane BUILD-5 r2: a PORT (KindOf 188, template + 0x11F & 0x10) shows the site snapped to the shore and its angle (RW 0x6A37AD .. 0x6A37E0:
	// BuildAssistant::getPortTransform RW 0x794C63; no answer keeps the pointer's site); the slot keeps the angle before the snap (RW 0x6A3847)
	Coord3D shown = world;
	float shownAngle = angle;
	if (isPort)
	{
		float a = 0.0f;
		Coord3D p;
		if (BuildPlacement::getPortTransform(m_portView, world, angle, a, p)) // r4: every frame, busy ones too (the view's data are fixed, BuildPlacement.h)
		{
			shown = p;
			shownAngle = a;
		}
	}
	m_ctx.ui.setPlaceGhostPose(shown, shownAngle, angle); // RW 0x6A3847 setPosition, RW 0x6A385B setOrientation
	// RW 0x6A3939: the legality only on odd client frames (TheGameClient vslot 0x7C & 1), at the drawable's site with the slot's angle (RW 0x6A39D7 .. 0x6A3A02)
	if (judge && (m_ctx.ui.clientFrame() & 1))
	{
		const LegalBuildCode code = BuildPlacement::isLocationLegalToBuild(m_ctx.logic, shown, *tt->getFinalOverride(), angle, ghostOptions(), builder, nullptr);
		m_ctx.ui.setPlaceLegalCode((int)code);
	}
}

void PlaceEventTranslator::endPlacement()
{
	// RW 0x83E6A4: setGUICommand(null) (vslot 0xBC), placeBuildAvailable(null, null) (vslot 0xDC)
	m_ctx.ui.setGUICommand(nullptr);
	m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
}

MessageDisposition PlaceEventTranslator::releaseLineBuild(const ThingTemplate &tt, Object &builder)
{
	if (!m_ctx.ui.isLineBuildStarted())
	{
		m_ctx.ui.setPlacementStart(nullptr); // RW 0x83E8A1: setPlacementStart(null)
		return MessageDisposition::Destroy;
	}
	// RW 0x83E932 .. 0x83E9B8
	const CommandButton *command = m_ctx.ui.placeBuildCommand();
	ClientMessage &m = m_ctx.stream.append(MSG_WALL_HUB_CONSTRUCT_SPAN);
	m.appendInteger((int)tt.getFinalOverride()->getTemplateID());
	m.appendLocation(*builder.getPosition());
	Coord3D click;
	if (!m_ctx.view.screenToTerrain(m_ctx.ui.placementStart(), m_ctx.logic, click)) // RW 0x83E745: the start pixel's terrain point now (TheTacticalView slot 0x168)
	{
		click = m_ctx.ui.placeLocation(); // INFERENCE: RotWK does not test the answer (RW 0x83E745); a pixel off the terrain keeps the ghost's point
	}
	m.appendLocation(click);
	m.appendInteger(command ? (int)command->m_options : 0xFFFFFE);
	m.appendObjectID(builder.getID());
	++m_stats.wallSpans;
	m_ctx.ui.setLineBuildStarted(false);
	endPlacement();
	return MessageDisposition::Destroy;
}

MessageDisposition PlaceEventTranslator::leftClick(const ClientMessage &msg)
{
	(void)msg;
	// RW 0x83E6C7: the pending template (vslot 0xE0); not anchored: the click is not the placement's
	const ThingTemplate *tt = m_ctx.logic.things().findTemplate(m_ctx.ui.placeBuildTemplate());
	if (!m_ctx.ui.isPlacing() || !m_ctx.ui.isPlacementAnchored())
	{
		return MessageDisposition::Keep;
	}
	Object *builder = m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource());
	if (!tt || !builder)
	{
		endPlacement(); // RW 0x83E7E9 (canMakeUnit refuses a missing builder): vslot 0xDC(0, 0)
		return MessageDisposition::Destroy;
	}
	// RW 0x83E709 .. 0x83E745: the ghost's angle and the anchor start's terrain point now
	const float angle = m_ctx.ui.placeAngle();
	Coord3D location = m_ctx.ui.placeLocation();
	m_ctx.view.screenToTerrain(m_ctx.ui.placementStart(), m_ctx.logic, location);
	const bool lineBuild = BuildAssistant::isLineBuildTemplate(m_ctx.logic, tt, builder); // RW 0x83E764 (slot 0x60)
	// RW 0x83E771: the builder's canMakeUnit (slot 0x64) with build index -1, for every build
	switch (BuildAssistant::canMakeUnit(*builder, tt, -1))
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
			endPlacement(); // RW 0x83E7E9: vslot 0xDC(0, 0)
			return MessageDisposition::Keep;
	}
	if (lineBuild)
	{
		return releaseLineBuild(*tt, *builder); // lane QA2-FIX: RW 0x83E911 (a line build skips the legality test, RW 0x83E7FE)
	}
	// RW 0x83E80D: isLocationLegalToBuild (slot 0x44) with 0x49F (the restricted-area code 8 of RW 0x707141 is not ported: S-1770)
	const LegalBuildCode code = BuildPlacement::isLocationLegalToBuild(m_ctx.logic, location, *tt->getFinalOverride(), angle, placeOptions(), builder, nullptr);
	if (code != LBC_OK)
	{
		// RW 0x83EA4E: displayCantBuildMessage, RW 0x83EAC5: MiscAudio + 0xE8 (BadBuildingPlacementSound), RW 0x83EB02: setPlacementStart(null); the mode stays
		++m_stats.refused;
		m_ctx.ui.setPlaceLegalCode((int)code);
		m_ctx.ui.message(cantBuildLabel((int)code));
		m_ctx.ui.requestMiscSound("BadBuildingPlacementSound");
		m_ctx.ui.setPlacementStart(nullptr);
		return MessageDisposition::Destroy;
	}
	// lane BUILD-5 r2: a PORT's site and angle snap to the shore after the legality test (RW 0x83E8D4 .. 0x83E8FF: getPortTransform RW 0x794C63)
	float placeAngle = angle;
	if (port(m_ctx.logic, *tt))
	{
		float a = 0.0f;
		Coord3D p;
		if (BuildPlacement::getPortTransform(m_ctx.logic, location, *tt->getFinalOverride(), angle, a, p))
		{
			location = p;
			placeAngle = a;
		}
	}
	// RW 0x83E9C2: MSG_DOZER_CONSTRUCT (0x41A) { template id, location, angle } (lane BUILD-1: a BASE_FOUNDATION builder sends MSG_FOUNDATION_CONSTRUCT, S-306)
	ClientMessage &m = m_ctx.stream.append(builder->isKindOfName("BASE_FOUNDATION") ? MSG_FOUNDATION_CONSTRUCT : MSG_DOZER_CONSTRUCT);
	m.appendInteger((int)tt->getFinalOverride()->getTemplateID());
	m.appendLocation(location);
	m.appendReal(placeAngle);
	++m_stats.placed;
	m_ctx.ui.setLineBuildStarted(false); // RW 0x83EA21: vslot 0x100(0)
	endPlacement();                       // RW 0x83EA35 -> 0x83E6A4: the mode always ends (no shift to keep placing)
	return MessageDisposition::Destroy;
}

MessageDisposition PlaceEventTranslator::translate(const ClientMessage &msg)
{
	switch (msg.type())
	{
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
			// RW 0x83E4BC: the press's pixel and time; never eaten (the LookAt translator scrolls with it)
			m_rightAnchor = msg.arg(0).pixel;
			m_rightDownMs = (unsigned)msg.arg(2).integer;
			m_rightDown = true;
			m_rightDragged = false;
			return MessageDisposition::Keep;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
			// RW 0x83E62F: the release's time while the press is tracked
			if (m_rightDown)
			{
				m_rightUpMs = (unsigned)msg.arg(2).integer;
				m_rightDown = false;
				m_stats.rightDrags += m_rightDragged ? 1 : 0;
			}
			return MessageDisposition::Keep;
		case CMSG_RAW_MOUSE_POSITION:
		{
			// RW 0x83E57E
			const ICoord2D p = msg.arg(0).pixel;
			m_pointer = p;
			m_havePointer = true;
			if (m_rightDown && m_ctx.mouse.isDragging(m_rightAnchor, p))
			{
				m_rightDragged = true;
			}
			if (!m_ctx.ui.isPlacing() || !m_ctx.ui.isPlacementAnchored())
			{
				return MessageDisposition::Keep;
			}
			// RW 0x83E5D9 .. 0x83E62D: the anchor's end follows the pointer once it is 5 pixels from the start (the root of the integer sum of squares)
			const ICoord2D start = m_ctx.ui.placementStart();
			const int dx = p.x - start.x, dy = p.y - start.y;
			if (SimMath::sqrtd((double)(dy * dy + dx * dx)) < (double)kAnchorEndDistance)
			{
				return MessageDisposition::Keep;
			}
			m_ctx.ui.setPlacementEnd(p);
			return MessageDisposition::Destroy;
		}
		case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		{
			// RW 0x83E4F4: a pending placement that is not anchored anchors at the pressed pixel; no builder object ends the mode
			if (!m_ctx.ui.isPlacing() || m_ctx.ui.isPlacementAnchored())
			{
				return MessageDisposition::Keep;
			}
			const ICoord2D p = msg.arg(0).pixel;
			if (!m_ctx.logic.findObjectByID(m_ctx.ui.placeBuildSource()))
			{
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID); // RW 0x83E7F1: vslot 0xDC(0)
				return MessageDisposition::Keep;
			}
			m_ctx.ui.setPlacementStart(&p); // RW 0x83E570: vslot 0xE8
			return MessageDisposition::Destroy;
		}
		case CMSG_MOUSE_LEFT_CLICK:
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
			return leftClick(msg); // RW 0x83E6C7
		case CMSG_MOUSE_RIGHT_CLICK:
		case CMSG_MOUSE_RIGHT_DOUBLE_CLICK:
			// RW 0x83E66C: a pending placement ends unless the press was a drag held long enough (the camera scroll's)
			if (!m_ctx.ui.isPlacing() || !m_ctx.mouse.isRightClick(m_rightDragged, m_rightDownMs, m_rightUpMs))
			{
				return MessageDisposition::Keep;
			}
			++m_stats.cancelled;
			endPlacement();
			return MessageDisposition::Destroy;
		case CMSG_RAW_KEY_DOWN:
			// INFERENCE (BUILD-1, S-306): Escape ends the placement (RotWK's translator has no key case; its route was not traced)
			if (m_ctx.ui.isPlacing() && msg.arg(0).integer == KEY_ESC)
			{
				++m_stats.cancelled;
				endPlacement();
				return MessageDisposition::Destroy;
			}
			return MessageDisposition::Keep;
		default:
			return MessageDisposition::Keep;
	}
}
