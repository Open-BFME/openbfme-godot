// OpenBFME. GPL-3.0.
//
// LiveGameFrameDriver (lane MP-1; split into protocol owner and simulation owner halves by SMOOTH-1, review r4): see the class comment.

#pragma once

#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"

#include <cstdint>
#include <vector>

// One logic frame's complete, ordered input (lane SMOOTH-1 + MP-1): acquired exactly once from the driver by the protocol owner, then immutable. The
// simulation owner installs `commands` into the logic's command list immediately before phase 1 of logic frame `frame` (it must equal the logic's frame).
struct FrameBatch
{
	UnsignedInt frame = 0;
	std::vector<GameMessage> commands;
};

// What the simulation owner captured of the completed state after batch N ran (the logic's frame is then N + 1), published with that frame's snapshot and
// consumed by the protocol owner in frame order, exactly once. Immutable once published.
struct FrameCompletion
{
	UnsignedInt batchFrame = 0;   ///< N: the batch that ran
	UnsignedInt frame = 0;        ///< N + 1: the logic frame after it (what the hash describes; ZH's frameNow of the CRC step)
	bool hashed = false;
	std::uint32_t hash = 0;       ///< GameLogic::computeStateHash of that state (when hashed)
	bool breakdown = false;       ///< the per-subsystem breakdown was captured (a CRC frame of the driver's interval)
	std::vector<GameLogic::StateHashSection> sections;
	std::vector<GameLogic::ObjectStateHash> objects;
	std::uint32_t rng = 0;        ///< diagnostic: a fold of the logic RNG's seed array in that state
};

// lane MP-1: what decides which commands a logic frame runs with, when a driver is installed (LiveGame::setFrameDriver): the lockstep network
// (GameNetwork/LockstepDriver.h), a replay recorder or a replay player (Common/Recorder.h). Without one, commands() goes to the next frame as before.
//
// SMOOTH-1 (review r4, MP-1 integration): the driver has ONE owner, the protocol owner: the thread that calls LiveGame::advance (and installs the driver).
// Its network, transport and replay state are touched only there. The simulation (on the logic worker, or inline in the single-thread fallback) never
// calls the protocol half: it receives immutable FrameBatches and hands back immutable FrameCompletions.
class LiveGameFrameDriver
{
public:
	virtual ~LiveGameFrameDriver() = default;

	// What the simulation owner captures after every batch; fixed for the driver's installation (read when it is installed)
	struct Capture
	{
		bool hashEveryFrame = false; ///< the state hash after every frame (replay recording / playback)
		int breakdownInterval = 0;   ///< > 0: the hash breakdown on every frame whose number after the batch is a multiple of it (the CRC frames)
	};
	virtual Capture capture() const = 0;

	// ---- protocol owner ----
	// Every advance (also advance(0) and while the worker runs or the lockstep stalls): `pending` holds what the local player issued since the last call
	// (taken by the driver: assigned ONCE to an execution frame derived from `protocolFrame`, the logic frame of the last completion consumed); the
	// transport is serviced and what arrived is filed.
	virtual void pump(UnsignedInt protocolFrame, CommandList &pending) = 0;
	// The complete batch of logic frame `frame` (the next one not acquired yet: frames are asked in order, each acquired exactly once): true and `out`
	// filled, or false when its input is not complete yet (nothing consumed; asked again later).
	virtual bool acquire(UnsignedInt frame, FrameBatch &out) = 0;
	// A completed batch, in frame order, exactly once (batch N -> c.frame == N + 1)
	virtual void completed(const FrameCompletion &c) = 0;

	// ---- simulation owner ----
	// after a batch's frame ran, before its completion is captured (tests: an injected divergence); runs on the logic worker when there is one
	virtual void simulationAfterFrame(GameLogic &logic) { (void)logic; }
};
