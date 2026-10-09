// OpenBFME. GPL-3.0.
// See GameLogic/AI/AICommandSink.h.

#include "GameLogic/AI/AICommandSink.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

void AICommandSink::issue(Object &obj, AICommand command)
{
	command.object = obj.getID();
	command.frame = obj.logic().getFrame();
	bool taken = false;
	if (m_handler)
	{
		taken = m_handler(obj, command);
	}
	if (!taken)
	{
		++m_unexecuted;
	}
	m_issued.push_back(std::move(command));
}
