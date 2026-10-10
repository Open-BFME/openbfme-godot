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

// ZH InGameUI::createMoveHint (InGameUI.cpp:2066); the immobile check needs the logic and is the caller's (HudInput::update)
void InGameUI::createMoveHint(const Coord3D &pos)
{
	m_moveHints[m_nextMoveHint].frame = m_clientFrame;
	m_moveHints[m_nextMoveHint].pos = pos;
	++m_moveHintsMade;
	if (++m_nextMoveHint == MAX_MOVE_HINTS)
	{
		m_nextMoveHint = 0;
	}
}

int InGameUI::liveMoveHintCount() const
{
	int n = 0;
	for (const MoveHint &h : m_moveHints)
	{
		n += h.frame != 0 && m_clientFrame - h.frame <= MOVE_HINT_FRAMES ? 1 : 0;
	}
	return n;
}

std::vector<std::string> InGameUI::takeMessages()
{
	std::vector<std::string> out;
	out.swap(m_messages);
	return out;
}
