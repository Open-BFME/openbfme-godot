// OpenBFME. GPL-3.0.
//
// CommandLogLoad (lane SAVE-2, docs/SAVES.md section 2 "How a save loads", case 2 "same build, no snapshot: catch up from the log"): a game loaded from a
// command-log save (Common/System/SaveGame/SaveGameFile.h) is brought to the saved frame F by playing the save's log back at full speed (the replay player,
// Common/Recorder.h's ReplayPlayback, ZH RECORDERMODETYPE_PLAYBACK) with every recorded state hash compared; at F the state hash must equal the header's,
// then the caller hands the game to the player (a recording driver that continues the log: ReplayWriter::openContinuing).
//
// The host has already started the game the log records (the log's NewGameMessage through prepareNewGame / LiveGame::load, exactly as a replay starts).
// step() runs logic frames one by one through LiveGame::advance(0.2) on the calling thread (the logic worker is switched off while it runs and restored at
// the end: the results are the same, SMOOTH-1 pins that) until F or the time budget, so a host can draw a progress screen between steps. Lane UBER-1: the
// log is replayed under the execution profiles it ran (its start message's, then its own profile switch records, ReplayPlayback), so every recorded hash is
// compared whatever profile the loading player runs; handOver switches to the player's profile at F and records the switch in the continued log.
//
// ONLINE-3 (lane/online3, not merged at this lane's base) has the same loop for a rejoin (CommandLogCatchUp over the relay's log, NetGameSession::rejoinStep);
// nothing is copied from it. What it can reuse later: this class drives a ReplayPlayback, so a rejoin from a saved log would need only the relay log's
// assembly into a ReplayFile.
//
// Lane SAVE-4e (SAVES.md section 2, case 1 "same build and identity, snapshot present"): a save that carries a snapshot (SAVE-4a..d's blocks at F) is
// loaded from it: begin() hands the snapshot to the started game (LiveGame::loadSnapshot: transactional, a refused snapshot leaves the game as it was
// or closes it, never half-loaded), checks the frame and the state hash against the header and is Ready at once: no frame of the log runs. The log is
// still parsed and checked as for a catch-up (it is the replay so far, continued by handOver) and is the cross-check of the verify mode
// (setVerifyLog): the save is caught up from its log as an old save is, and at F the snapshot the caught-up game saves must be the save's snapshot,
// byte for byte (T2 of SAVES.md section 6 across the two load paths). A save without a snapshot (the earlier alphas' log-only saves) catches up as
// before (case 2).
//
// Not simulation code: it decides which commands the logic runs (the log's), never what a frame computes.

#pragma once

#include "Common/Recorder.h"
#include "Common/SimulationProfile.h"
#include "Common/System/SaveGame/SaveGameFile.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class LiveGame;

class CommandLogLoad
{
public:
	enum class Mode
	{
		CatchUp,  ///< the log is played to F (a save without a snapshot, or the verify mode)
		Snapshot  ///< the save's snapshot is loaded (lane SAVE-4e)
	};
	enum class State
	{
		Idle,
		CatchingUp,
		Ready,  ///< the game is at F with the saved state hash; the playback driver is removed, the caller installs the live one
		Failed
	};

	// parses the save's log (it must be a complete replay ending at the header's frame and hash, recorded by the header's identity, with no lost command,
	// a state hash of every frame up to F and nothing after F; its execution profile switches must end in the header's execution profile): false + *error,
	// the state Failed
	bool prepare(const SaveGameFile &save, std::string *error);
	// lane SAVE-4e: off by default. On (before prepare): a save with a snapshot is caught up from its log instead, and at F the caught-up game's snapshot
	// must equal the save's (a named failure naming the first differing byte otherwise)
	void setVerifyLog(bool on) { m_verifyLog = on; }
	bool verifyLog() const { return m_verifyLog; }
	Mode mode() const { return m_mode; }
	bool hasSnapshot() const { return !m_snapshot.empty(); }
	// after Ready: the game is the player's: `writer` continues the save's log at `path` ("" in memory), and when the game runs another execution profile
	// than `own` the switch to it is applied and recorded in the log at F (lane UBER-1, owner decision 7). false + *error, the state Failed, when the
	// recording cannot be opened (the game must not go on unrecorded: it could not be saved again)
	bool handOver(LiveGame &game, ReplayWriter &writer, const std::string &path, SimulationProfile own, std::string *error);
	// lane UBER-1: the profile the log's game starts under (its NewGameMessage's) and the one it runs at F (the last switch's)
	SimulationProfile startProfile() const { return m_log.header.game.game.simulationProfile; }
	SimulationProfile targetProfile() const { return m_targetProfile; }
	const ReplayFile &log() const { return m_log; }
	// lane UBER-2C: whether a recording holds frame f's state hash (an Uber frame only on the kUberRecordedHashEvery cadence; ReplaySeek asks it too)
	static bool hashRecordedFor(const ReplayFile &log, UnsignedInt f);
	// installs the playback driver on the started game (its frame must be the log's first frame or earlier than F); lane SAVE-4e: in the Snapshot mode
	// loads the snapshot instead (the state is then Ready or Failed on return)
	bool begin(LiveGame &game, std::string *error);
	// runs frames for at most `budgetMs` (at least one frame); returns the state
	State step(LiveGame &game, std::uint32_t budgetMs);

	State state() const { return m_state; }
	const std::string &failure() const { return m_failure; }
	UnsignedInt target() const { return m_target; }
	UnsignedInt startFrame() const { return m_startFrame; }
	UnsignedInt reached() const { return m_reached; }
	unsigned long long hashesCompared() const { return m_playback ? m_playback->hashesCompared() : 0; }
	unsigned long long localInputIgnored() const { return m_playback ? m_playback->localInputIgnored() : 0; }
	double elapsedMs() const { return m_elapsedMs; }
	double snapshotLoadMs() const { return m_snapshotMs; } ///< lane SAVE-4e: the snapshot's load (Snapshot mode), wall time

	// lane SAVE-4e review r1: the snapshot a save carries (GameWorld.save_game): the game's whole snapshot at its frame, or false + *error "the game cannot
	// be saved: <the refusal>" (a block without an xfer, a hash section without a block, a block that cannot be written) - the save then fails; it never
	// falls back to a log-only save
	static bool takeSnapshot(LiveGame &game, std::vector<std::uint8_t> &out, std::string *error);
	// the lane's acceptance stops (S-4000 .. S-4005, docs/STOPS.md), reported by GameWorld's save and load ("stops")
	static std::vector<std::string> stopLines();

private:
	void fail(LiveGame *game, const std::string &why);
	ReplayFile m_log;
	std::unique_ptr<ReplayPlayback> m_playback;
	State m_state = State::Idle;
	std::string m_failure;
	UnsignedInt m_target = 0, m_startFrame = 0, m_reached = 0;
	std::uint32_t m_targetHash = 0;
	SimulationProfile m_targetProfile = SimulationProfile::Faithful;
	std::vector<std::uint8_t> m_logBytes; ///< the save's log, continued by handOver
	void checkCoverage(UnsignedInt target);
	bool hashRequired(UnsignedInt f, UnsignedInt target) const; // lane UBER-2C
	bool m_restoreThread = false;
	double m_elapsedMs = 0.0;
	// lane SAVE-4e
	bool beginSnapshot(LiveGame &game, std::string *error);
	bool m_verifyLog = false;
	Mode m_mode = Mode::CatchUp;
	std::vector<std::uint8_t> m_snapshot;
	double m_snapshotMs = 0.0;
};
