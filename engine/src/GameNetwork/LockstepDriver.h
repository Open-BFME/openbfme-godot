// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LockstepDriver (ZH GameEngine::update's network branch: TheNetwork->update(), then TheGameLogic->UPDATE() only when TheNetwork->isFrameDataReady()),
// lane MP-1: the LiveGame frame driver of a network game, and of a recorded game. With a Network, the local player's messages go to the network and a
// frame runs when every player's commands for it are in (Network.h); without one (a local game being recorded), the local messages run on the next frame
// as LiveGame does without a driver. With a ReplayWriter, every frame's command list and state hash are recorded (Common/Recorder.h).
//
// The stall watch (ZH NetworkDisconnectTime, GameData.ini 15000 ms in 2.01: the disconnect screen comes up after that long on one frame) is reported, not
// acted on: the disconnect vote (DisconnectManager) is not ported (stop S-723).

#pragma once

#include "Common/Recorder.h"
#include "GameClient/LiveGameFrameDriver.h"
#include "GameNetwork/Network.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// registers the logic's executor of MSG_LOGIC_CRC (the comparison is the Network's / the replay's; the logic itself does nothing with it) unless one is there
void RegisterLogicCRCHandler(GameLogicDispatch &dispatch);

// SMOOTH-1 (review r4, MP-1 integration): the driver, its Network, the Network's transport and the ReplayWriter have one owner, the protocol owner (the thread
// that calls LiveGame::advance). The logic worker never touches them: it gets immutable FrameBatches (acquire) and returns immutable FrameCompletions
// (completed), captured on the worker (the hash, the CRC frame's breakdown). The numbering stays MP-1's: batch N runs logic frame N and its completion
// describes frame N + 1 (the replay's hash record N + 1, the CRC of frameNow = N + 1 sent with execution frame N + 1 + run-ahead).
class LockstepDriver : public LiveGameFrameDriver
{
public:
	// `network` may be null (a local game); `writer` may be null (no recording)
	LockstepDriver(Network *network, ReplayWriter *writer);

	Capture capture() const override;
	void pump(UnsignedInt protocolFrame, CommandList &pending) override;
	bool acquire(UnsignedInt frame, FrameBatch &out) override;
	void completed(const FrameCompletion &c) override;
	void simulationAfterFrame(GameLogic &logic) override;

	// ms the current frame has waited so far (0 when not waiting); the slots it waits for
	std::uint64_t waitingMs() const;
	const std::vector<int> &waitingSlots() const { return m_waitingSlots; }
	// lane MP-2: the current frame has every command but the disconnect path holds it (an announced disconnect frame waits for the router)
	bool gatedByDisconnect() const { return m_gated; }
	// frames that waited longer than the disconnect time (ZH NetworkDisconnectTime), with the slots they waited for
	const std::vector<std::string> &longStalls() const { return m_longStalls; }
	void setDisconnectTimeMs(std::uint64_t ms) { m_disconnectMs = ms; }
	// a test hook: when set, called on the SIMULATION owner after every frame (the injected-divergence test changes state on one peer here). Set it
	// before the driver is installed (LiveGame::setFrameDriver), never while installed.
	void setAfterFrame(std::function<void(GameLogic &)> f) { m_afterFrame = std::move(f); }
	// protocol owner: called with every completion, in frame order (reports: the per-frame hashes of a peer)
	void setOnCompleted(std::function<void(const FrameCompletion &)> f) { m_onCompleted = std::move(f); }
	// the protocol frame: the logic frame of the last completion consumed (what local input is stamped against)
	UnsignedInt protocolFrame() const { return m_protocolFrame; }
	// the batches acquired and the completions consumed (each exactly once, in order)
	unsigned long long batchesAcquired() const { return m_acquired; }
	unsigned long long completionsConsumed() const { return m_completions; }
	// protocol owner: an input source keyed to protocol frames, called exactly once for every protocol frame P the driver reaches (the first pump's
	// frame, then every completion's), in order; what it appends is stamped against P exactly (execution frame P + run-ahead), whatever the render rate
	// or the worker's timing. For sources that read no live game state (tests, scripted protocol input); the HUD's input goes through pump().
	void setInputSource(std::function<void(UnsignedInt protocolFrame, CommandList &out)> f) { m_inputSource = std::move(f); }
	// no batch for this frame or a later one is handed out (a peer that plays a fixed number of frames); default none
	void setStopFrame(UnsignedInt frame) { m_stopFrame = frame; }
	// lane MP-3 (smoothness, protocol owner, wall clock only: never simulation state): input-to-action latency, the time from a local command's issue (its
	// pump / input source, before the network stamps its execution frame) to the acquire of the batch that runs it, matched in issue order with the local
	// player's commands of each relayed batch (MSG_LOGIC_CRC excluded); and network stalls, every wait of acquire() for a frame (missing commands or the
	// disconnect gate) from its first refusal to the batch handed out
	struct SmoothStats
	{
		std::vector<std::uint32_t> inputLatencyMs; ///< one sample per local command
		std::vector<std::uint32_t> stallMs;        ///< one sample per wait episode (a refused acquire until the batch is handed out)
		unsigned long long unmatchedCommands = 0;  ///< relayed local commands without an issue record (should stay 0)
	};
	const SmoothStats &smoothStats() const { return m_smooth; }
	// lane MP-3 (the owner's measurements): every completion's census (Capture::census: the frame's wall time on the simulation owner, living battalions,
	// troops, objects), in frame order. Set it before the driver is installed (LiveGame reads capture() then)
	struct CensusRow
	{
		UnsignedInt frame = 0;
		std::int64_t simUs = -1;
		int battalions = 0, troops = 0, objects = 0;
	};
	void setCensus(bool on) { m_census = on; }
	const std::vector<CensusRow> &censusLog() const { return m_censusLog; }
	// "count N p50 X p95 Y max Z" of samples (ms); "count 0" when empty
	static std::string percentiles(std::vector<std::uint32_t> samples);
	// protocol violations seen by the driver (a batch asked out of order, a completion out of order): never silent
	const std::vector<std::string> &errors() const { return m_errors; }

private:
	Network *m_network;
	ReplayWriter *m_writer;
	CommandList m_local; ///< the local player's messages not yet given to the network (or, locally, to the next batch)
	UnsignedInt m_protocolFrame = 0;
	UnsignedInt m_stopFrame = 0xFFFFFFFFu;
	bool m_started = false;          ///< a frame was asked or completed (the cursors below are valid)
	UnsignedInt m_nextBatch = 0;     ///< the next frame acquire() may hand out
	UnsignedInt m_nextCompletion = 0; ///< the frame the next completion must describe
	unsigned long long m_acquired = 0, m_completions = 0;
	std::uint64_t m_waitStart = 0;
	bool m_stallReported = false;
	std::uint64_t m_disconnectMs = 15000;
	std::vector<int> m_waitingSlots;
	bool m_gated = false;
	std::vector<std::string> m_longStalls;
	std::vector<std::string> m_errors;
	std::function<void(GameLogic &)> m_afterFrame;
	std::function<void(const FrameCompletion &)> m_onCompleted;
	std::function<void(UnsignedInt, CommandList &)> m_inputSource;
	bool m_inputStarted = false;
	UnsignedInt m_inputFrame = 0; ///< the last protocol frame the input source was called for
	void sourceInput(UnsignedInt protocolFrame);
	// lane MP-3
	SmoothStats m_smooth;
	std::deque<std::pair<std::uint64_t, size_t>> m_issued; ///< (issue time, local commands) per network update that sent any, in order
	int m_localPlayer = -1;
	bool m_waitAfterLoad = false;
	bool m_census = false;
	std::vector<CensusRow> m_censusLog;
	void noteIssued();
	void noteRelayed(const CommandList &commands);
};
