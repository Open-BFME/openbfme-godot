// OpenBFME. GPL-3.0.
//
// FogCheckDriver (lane ANTICHEAT-1): a replay's frame driver with the integrity check D1 (GameNetwork/OrdersIntoFog.h) beside it, for openbfme_peer
// --fog-check and the tests. Not simulation code.

#pragma once

#include "Common/Recorder.h"
#include "GameClient/LiveGameFrameDriver.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/CommandStreamChecks.h"
#include "GameNetwork/OrdersIntoFog.h"
#include "GameNetwork/ViewChecks.h"

#include <functional>
#include <vector>

// the replay driver with the integrity check D1 beside it. The check sees each batch's commands on the state they run on: before the
// first frame, then after every frame (simulationAfterFrame: the logic is between frames on its owner, the next batch is the logic's frame), read from
// the replay itself; everything else is the playback's
class FogCheckDriver : public LiveGameFrameDriver
{
public:
	FogCheckDriver(ReplayPlayback &inner, const ReplayFile &replay, OrdersIntoFog &check)
		: m_inner(inner)
		, m_replay(replay)
		, m_check(check)
	{
	}
	Capture capture() const override { return m_inner.capture(); }
	bool hashWanted(UnsignedInt frame) const override { return m_inner.hashWanted(frame); }
	void pump(UnsignedInt protocolFrame, CommandList &pending) override { m_inner.pump(protocolFrame, pending); }
	bool acquire(UnsignedInt frame, FrameBatch &out) override { return m_inner.acquire(frame, out); }
	void completed(const FrameCompletion &c) override { m_inner.completed(c); }
	void simulationAfterFrame(GameLogic &logic) override
	{
		m_inner.simulationAfterFrame(logic);
		offer(logic);
	}
	void offer(GameLogic &logic)
	{
		const UnsignedInt next = logic.getFrame();
		if (next >= m_replay.finalFrame)
		{
			return;
		}
		if (onState)
		{
			onState(logic, next);
		}
		static const std::vector<GameMessage> none;
		const auto it = m_replay.frames.find(next);
		const std::vector<GameMessage> &batch = it == m_replay.frames.end() ? none : it->second;
		m_check.beforeFrame(next, batch);
		if (inputRate)
		{
			inputRate->beforeFrame(next, batch);
		}
		if (commands)
		{
			commands->beforeFrame(next, batch);
		}
		if (views)
		{
			views->beforeFrame(next); // after D1: its samples of this state are taken
		}
	}

	// called with every state a batch is offered on, before the check sees it (openbfme_peer --fog-check-dump: the evidence view's frames)
	std::function<void(GameLogic &, UnsignedInt)> onState;
	// lane ANTICHEAT-1 (owner 2026-10-21): D5 and D6 see the same batches on the same states (optional)
	InputRateCheck *inputRate = nullptr;
	CommandIntegrityCheck *commands = nullptr;
	ViewCheck *views = nullptr; ///< lane ANTICHEAT-1: D2 (the replay's VIEW records)

private:
	ReplayPlayback &m_inner;
	const ReplayFile &m_replay;
	OrdersIntoFog &m_check;
};

