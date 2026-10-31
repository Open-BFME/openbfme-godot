// OpenBFME. GPL-3.0.
// See InGameUI.h.

#include "GameClient/InGameUI.h"

#include "GameClient/ControlBarCommands.h"

#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/HudContext.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <set>

// RW 0x69D90C selects across the screen from the current selection, not the object that passed the ControlBar gate.
// The region scan retains the ZH-based port (S-1202): contained-template matching and drawable traversal ordering are not yet reproduced.
int InGameUI::selectMatching(HudContext &ctx, bool acrossMap)
{
	std::set<const ThingTemplate *> templates;
	for (ObjectID id : selected())
	{
		if (Object *o = ctx.logic.findObjectByID(id))
		{
			if (HudObjects::isLocallyControlled(ctx, *o) && o->getTemplate())
			{
				templates.insert(o->getTemplate());
			}
		}
	}
	if (templates.empty())
	{
		return 0;
	}
	const ICoord2D size = ctx.view.size();
	std::vector<Object *> found;
	for (Object *o = ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isDestroyed() || o->getContainedBy() || !HudObjects::isLocallyControlled(ctx, *o) || !HudObjects::isSelectable(*o, getGUICommand()) || o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		if (!templates.count(o->getTemplate()) || isSelected(o->getID()))
		{
			continue;
		}
		if (!acrossMap)
		{
			ICoord2D px;
			if (!ctx.view.worldToScreen(*o->getPosition(), px) || px.x < 0 || px.y < 0 || px.x > size.x || px.y > size.y)
			{
				continue;
			}
		}
		if (getMaxSelectCount() > 0 && getSelectCount() + (int)found.size() >= getMaxSelectCount())
		{
			break;
		}
		found.push_back(o);
	}
	if (found.empty())
	{
		return 0;
	}
	ClientMessage &m = ctx.stream.append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
	m.appendBoolean(false);
	for (Object *o : found)
	{
		selectObject(o->getID());
		m.appendObjectID(o->getID());
	}
	return (int)found.size();
}

bool InGameUI::isSelected(ObjectID id) const
{
	return std::find(m_selected.begin(), m_selected.end(), id) != m_selected.end();
}

void InGameUI::selectObject(ObjectID id)
{
	if (id != INVALID_ID && !isSelected(id))
	{
		markSelectionChanged();
		m_selected.insert(m_selected.begin(), id); // ZH m_selectedDrawables.push_front: the newest selection is the first one
	}
}

void InGameUI::deselectObject(ObjectID id)
{
	if (isSelected(id))
	{
		markSelectionChanged();
	}
	m_selected.erase(std::remove(m_selected.begin(), m_selected.end(), id), m_selected.end());
}

void InGameUI::markSelectionChanged()
{
	if (m_logic)
	{
		m_frameSelectionChanged = m_logic->getFrame();
	}
}

void InGameUI::deselectAll(bool postMessage)
{
	if (!m_selected.empty())
	{
		markSelectionChanged(); // ZH / RotWK: deselectDrawable of each selected drawable
	}
	m_selected.clear();
	// ZH deselectAllDrawables (InGameUI.cpp:3260): with postMsg the logic's group is destroyed by a message even when nothing was selected
	// ("@todo don't we want to not emit this message if there wasn't a group at all"); its argument is TRUE (deletes the entire group)
	if (postMessage && m_stream)
	{
		m_stream->append(MSG_DESTROY_SELECTED_GROUP).appendBoolean(true);
	}
}

// lane OWNERFIX-1: RW 0x69B5ED (InGameUI.h)
void InGameUI::setInputEnabled(bool on)
{
	const bool was = m_inputEnabled;
	m_inputEnabled = on;
	if (!was || on)
	{
		return;
	}
	setSelecting(false); // RW 0x69B628: vslot 0xAC
	noteStep("setSelecting");
	if (m_radiusCursorNone)
	{
		m_radiusCursorNone(); // RW 0x69B632: vslot 0x144, before the mode flags
	}
	noteStep("setRadiusCursorNone");
	m_forceAttack = false; // + 0x8B8
	m_forceMove = false;   // + 0x8B9
	m_waypoint = false;    // + 0x8B0
	m_preferSelection = false; // + 0x8BA (RW 0x820106 / 0x820112: the prefer-selection mode, as lane INPUT-1 found; CommandXlat.cpp)
	noteStep("modeFlags");
	if (m_stream)
	{
		ClientMessage &m = m_stream->append(MSG_CHANGE_ORDERMODE); // 0x469 (0, 1): decomp InGameUIInputModes.cpp, last statement of the edge
		m.appendInteger(0);
		m.appendInteger(1);
	}
	noteStep("MSG_CHANGE_ORDERMODE");
}

// lane OWNERFIX-1: RW 0x7BC586 / RW 0x7BC5E9 (InGameUI.h)
void InGameUI::applyScriptInput(bool enable)
{
	setInputEnabled(enable); // RW 0x69B5DA
	m_scriptMouseHidden = !enable; // RW 0x5EDD0F
	noteStep("mouseVisibility");
	if (enable)
	{
		return;
	}
	deselectAll(true); // the stream's MSG_DESTROY_SELECTED_GROUP (true), then RW 0x69C817: ZH deselectAllDrawables(true)
	noteStep("MSG_DESTROY_SELECTED_GROUP");
	m_waypoint = false; // + 0x8B0 = 0
	noteStep("waypointMode");
}

std::string InGameUI::scriptInputStopLine()
{
	return "[S-4964] cutscene input (lane OWNERFIX-1): DISABLE_INPUT (RW 0x7BC586) turns the input off, hides the pointer, drops the selection and the pending "
		   "radius cursor, clears the force attack / force move / waypoint modes, the help provider (RW 0x8076CD: its flag +0x21C and provider are ported, "
		   "its call on the radar window override's inner object +0xC8 is not) and the LookAt flags (RW 0x83AB5D: +0x38 / +0x39 / +0x154 .. +0x157; +0x3B / "
		   "+0x3C are not mapped); +0x8BA is the prefer-selection mode (RW 0x820106), the other eight mode flags +0x8BB .. +0x8C2 are not mapped; a pending GUI command (the attack move button's) is not cleared, as in retail; the CommandTranslator's input-off pass list (RW 0x81F8F2) keeps "
		   "its logic messages, its client message numbers (0x1D, 0x70, 0x6A5, 0x6A6, 0x6B8, 0x6BA, 0x6BB, 0x7D9, 0x7ED) are not mapped; INFERENCE: the HUD's "
		   "own entry paths (the spell book bar and its target click, the hero bar, the radar's look-at) do nothing while the input is off (retail's windows "
		   "still run their callbacks, its translators drop the result); the pointer over a menu opened during a cutscene shows (not read)";
}

void InGameUI::mirrorSelection(const std::vector<ObjectID> &ids)
{
	if (m_selected != ids)
	{
		markSelectionChanged();
		m_selected = ids;
	}
}

void InGameUI::setGUICommand(const CommandButton *command)
{
	// BFME2 InGameUI::setGUICommand (0x0029A889, InGameUIInputModes.cpp, matched): the command is kept, the cursor is set to Arrow (setMouseCursor(ARROW)) and
	// the GUI command mode's cursor (+ 0x800) becomes the mouse's cursor, Arrow. Not ported here: the recorder playback guard, the drop of a command whose
	// Options need no target, the radius cursor (the HUD's: InGameHud::updateRadiusCursor)
	m_guiCommand = command;
	setCursor(MouseCursorName::Arrow);
	m_guiModeCursor = MouseCursorName::Arrow;
}

int InGameUI::mouseMode() const
{
	if (isPlacing())
	{
		return 1;
	}
	// BFME2 0x0029A889: MOUSEMODE_GUI_COMMAND (2) for a command other than DOZER_CONSTRUCT (0x35) with an Options bit of COMMAND_OPTION_NEED_TARGET (0x227)
	if (m_guiCommand && m_guiCommand->m_command != GUI_COMMAND_DOZER_CONSTRUCT && (m_guiCommand->m_options & 0x227u) != 0)
	{
		return 2;
	}
	return 0;
}

const std::string &InGameUI::mouseModeCursor() const
{
	static const std::string target = MouseCursorName::Target, arrow = MouseCursorName::Arrow;
	switch (mouseMode())
	{
		case 1:
			return target;
		case 2:
			return m_guiModeCursor;
		default:
			return arrow;
	}
}

void InGameUI::setCursor(const std::string &name)
{
	// BFME2 InGameUI::setMouseCursor (0x0029A5D6, InGameUIInputModes.cpp, matched): TheMouse->setCursor, then in the GUI command mode (2) a cursor other than
	// Arrow / Scroll becomes the mode's cursor (+ 0x800)
	m_cursor = name;
	if (mouseMode() == 2 && name != MouseCursorName::Arrow && name != MouseCursorName::Scroll)
	{
		m_guiModeCursor = name;
	}
}

void InGameUI::setScrolling(bool on)
{
	// lane BUILD-5: RW 0x69B28F (InGameUIInputModes.cpp:330)
	if (m_scrolling == on)
	{
		return;
	}
	setCursor(on ? MouseCursorName::Scroll : MouseCursorName::Arrow);
	m_scrolling = on;
}

void InGameUI::placeBuildAvailable(const std::string &templateName, ObjectID sourceObject, const CommandButton *command)
{
	m_placeTemplate = templateName;
	m_placeSource = sourceObject;
	m_placeCommand = templateName.empty() ? nullptr : command;
	m_placeHasGhost = false;
	m_placeAngle = 0.0f; // the first placement update turns the ghost to the template's PlacementViewAngle (template + 0x4E0, RW 0x69C755; PlaceEventTranslator)
	m_placeSlotAngle = 0.0f;
	m_placeLegal = 0;
	m_placeAnchored = false; // RW 0x69C7C3 / the end path: setPlacementStart(null)
	// lane BUILD-5: RW 0x69C69B setMouseCursor(Target) with a template, RW 0x69C7B0 / 0x69C7EE setMouseCursor(Arrow) without one
	setCursor(templateName.empty() ? MouseCursorName::Arrow : MouseCursorName::Target);
}

// RW 0x69F54F (its selection and location tests need the logic: HudInput::hintSpy)
void InGameUI::createMoveHint(const Coord3D &pos)
{
	expireMoveHints();
	MoveHint &h = m_moveHints[m_nextMoveHint];
	h.frame = m_clientFrame;
	h.pos = pos;
	h.expired = false;
	++m_moveHintsMade;
	if (++m_nextMoveHint == MAX_MOVE_HINTS)
	{
		m_nextMoveHint = 0;
	}
}

// RW 0x69B76C(0) -> RW 0x69B745 for each slot
void InGameUI::expireMoveHints()
{
	for (MoveHint &h : m_moveHints)
	{
		h.frame = 0;
		h.expired = true;
	}
}

bool InGameUI::moveHintDrawn(int i) const
{
	if (i < 0 || i >= MAX_MOVE_HINTS)
	{
		return false;
	}
	const MoveHint &h = m_moveHints[i];
	// RW 0x48EDFF .. 0x48EE24: elapsed = frame - hint frame, taken as 41 while the client frame is below 41; drawn when not expired and elapsed < 41
	const unsigned elapsed = m_clientFrame < MOVE_HINT_FRAMES + 1 ? MOVE_HINT_FRAMES + 1 : m_clientFrame - h.frame;
	return !h.expired && elapsed <= MOVE_HINT_FRAMES;
}

int InGameUI::liveMoveHintCount() const
{
	int n = 0;
	for (int i = 0; i < MAX_MOVE_HINTS; ++i)
	{
		n += moveHintDrawn(i) ? 1 : 0;
	}
	return n;
}

std::vector<std::string> InGameUI::takeMessages()
{
	std::vector<std::string> out;
	out.swap(m_messages);
	return out;
}
