// OpenBFME. GPL-3.0.
// See InGameUI.h.

#include "GameClient/InGameUI.h"

#include "GameClient/MessageStream/MessageStream.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>

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

void InGameUI::setGUICommand(const CommandButton *command)
{
	m_guiCommand = command;
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
