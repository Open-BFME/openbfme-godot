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
	// after Ready: the game is the player's: `writer` continues the save's log at `path` ("" in memory), and when the game runs another execution profile
	// than `own` the switch to it is applied and recorded in the log at F (lane UBER-1, owner decision 7). false + *error, the state Failed, when the
	// recording cannot be opened (the game must not go on unrecorded: it could not be saved again)
	bool handOver(LiveGame &game, ReplayWriter &writer, const std::string &path, SimulationProfile own, std::string *error);
	// lane UBER-1: the profile the log's game starts under (its NewGameMessage's) and the one it runs at F (the last switch's)
	SimulationProfile startProfile() const { return m_log.header.game.game.simulationProfile; }
	SimulationProfile targetProfile() const { return m_targetProfile; }
	const ReplayFile &log() const { return m_log; }
	// installs the playback driver on the started game (its frame must be the log's first frame or earlier than F)
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
	bool m_restoreThread = false;
	double m_elapsedMs = 0.0;
};
