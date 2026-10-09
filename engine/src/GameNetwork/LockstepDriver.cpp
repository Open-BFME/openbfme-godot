// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/LockstepDriver.h.

#include "GameNetwork/LockstepDriver.h"

#include "GameLogic/SelfDestruct.h"
#include "GameNetwork/Transport.h"

void RegisterLogicCRCHandler(GameLogicDispatch &dispatch)
{
	RegisterSelfDestructHandler(dispatch); // lane MP-2: every network game and replay executes the disconnect path's MSG_SELF_DESTRUCT
	if (dispatch.handlerLane(MSG_LOGIC_CRC).empty())
	{
		dispatch.registerHandler(MSG_LOGIC_CRC, "MP-1", [](GameLogic &, const GameMessage &m) {
			// ZH GameLogicDispatch MSG_LOGIC_CRC: { integer CRC, boolean playback }; the comparison happened when the frame was relayed (Network::relayCommands)
			return m.getArgumentCount() == 2 && m.getArgument(0)->type == ARGUMENTDATATYPE_INTEGER && m.getArgument(1)->type == ARGUMENTDATATYPE_BOOLEAN;
		});
	}
}

LockstepDriver::LockstepDriver(Network *network, ReplayWriter *writer)
	: m_network(network)
	, m_writer(writer)
{
}

LiveGameFrameDriver::Capture LockstepDriver::capture() const
{
	Capture c;
	c.hashEveryFrame = m_writer != nullptr; // the replay's hash record of every frame
	c.breakdownInterval = m_network ? m_network->config().crcInterval : 0;
	return c;
}

void LockstepDriver::pump(UnsignedInt protocolFrame, CommandList &pending)
{
	for (const GameMessage &m : pending.messages())
	{
		m_local.append(m);
	}
	pending.reset();
	m_protocolFrame = protocolFrame;
	sourceInput(protocolFrame);
	if (m_network)
	{
		// ZH Network::update: the local messages get their execution frame here, once (protocol frame + run-ahead); the transport is serviced
		m_network->update(protocolFrame, m_local);
	}
}

bool LockstepDriver::acquire(UnsignedInt frame, FrameBatch &out)
{
	if (m_started && frame != m_nextBatch)
	{
		m_errors.push_back("batch " + std::to_string(frame) + " asked, the next one is " + std::to_string(m_nextBatch));
		return false;
	}
	if (frame >= m_stopFrame)
	{
		return false;
	}
	CommandList commands;
	if (m_network)
	{
		m_network->update(m_protocolFrame, m_local); // what arrived since the pump (no new local message: m_local holds only the protocol's own)
		// lane MP-2: and the disconnect path's frame gate (an announced disconnect frame waits for the next packet router, S-1121)
		if (!m_network->allLoaded() || !m_network->isFrameReady(frame) || !m_network->allowedToContinue(frame))
		{
			const std::uint64_t now = NetMilliseconds();
			if (m_waitStart == 0)
			{
				m_waitStart = now;
			}
			m_waitingSlots = m_network->waitingFor(frame);
			m_gated = m_waitingSlots.empty() && m_network->allLoaded();
			if (!m_stallReported && now - m_waitStart >= m_disconnectMs)
			{
				std::string slots;
				for (int s : m_waitingSlots)
				{
					slots += (slots.empty() ? "" : ", ") + std::to_string(s);
				}
				m_longStalls.push_back("frame " + std::to_string(frame) + " waited " + std::to_string(now - m_waitStart) + " ms for slot(s) " + slots);
				m_stallReported = true;
			}
			return false;
		}
		m_waitStart = 0;
		m_stallReported = false;
		m_waitingSlots.clear();
		m_gated = false;
		m_network->relayCommands(frame, commands);
	}
	else
	{
		for (const GameMessage &m : m_local.messages())
		{
			commands.append(m);
		}
		m_local.reset();
	}
	if (m_writer)
	{
		m_writer->recordFrame(frame, commands); // batch N, exactly once
	}
	if (!m_started)
	{
		m_started = true;
		m_nextCompletion = frame + 1;
	}
	m_nextBatch = frame + 1;
	++m_acquired;
	out.frame = frame;
	out.commands = commands.messages();
	return true;
}

void LockstepDriver::simulationAfterFrame(GameLogic &logic)
{
	if (m_afterFrame)
	{
		m_afterFrame(logic);
	}
}

void LockstepDriver::completed(const FrameCompletion &c)
{
	if (c.frame != m_nextCompletion || c.batchFrame + 1 != c.frame)
	{
		m_errors.push_back("completion of frame " + std::to_string(c.frame) + " (batch " + std::to_string(c.batchFrame) + "), expected frame "
			+ std::to_string(m_nextCompletion));
	}
	m_nextCompletion = c.frame + 1;
	++m_completions;
	if (m_writer)
	{
		if (c.hashed)
		{
			m_writer->recordHash(c.frame, c.hash); // the result of batch N under N + 1, exactly once
		}
		else
		{
			m_errors.push_back("completion of frame " + std::to_string(c.frame) + " has no hash for the replay");
		}
	}
	m_protocolFrame = c.frame;
	if (m_network)
	{
		m_network->frameCompleted(c, m_local);
	}
	sourceInput(c.frame); // after the CRC: both are stamped against c.frame
	if (m_network)
	{
		m_network->update(c.frame, m_local); // ZH processCommand on the new frame: the CRC and the next frame infos go out at once
	}
	if (m_onCompleted)
	{
		m_onCompleted(c);
	}
}

void LockstepDriver::sourceInput(UnsignedInt protocolFrame)
{
	if (!m_inputSource || (m_inputStarted && protocolFrame <= m_inputFrame))
	{
		return;
	}
	m_inputStarted = true;
	m_inputFrame = protocolFrame;
	m_inputSource(protocolFrame, m_local);
}

std::uint64_t LockstepDriver::waitingMs() const
{
	return m_waitStart == 0 ? 0 : NetMilliseconds() - m_waitStart;
}
