// OpenBFME. GPL-3.0.
// See GameClient/CommandLogLoad.h (lane SAVE-2).

#include "GameClient/CommandLogLoad.h"

#include "Common/GameState.h"
#include "GameLogic/GameLogic.h"

#include "GameClient/LiveGame.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/LockstepDriver.h"

#include <chrono>

namespace
{
double nowMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool CommandLogLoad::handOver(LiveGame &game, ReplayWriter &writer, const std::string &path, SimulationProfile own, std::string *error)
{
	if (m_state != State::Ready)
	{
		if (error)
		{
			*error = m_failure.empty() ? "the saved game has not caught up" : m_failure;
		}
		return false;
	}
	// review: a game handed over without its recording could not be saved again: a named failure, never a silent go-on
	std::string why;
	writer.setKeepCopy(true);
	if (!writer.openContinuing(path, m_logBytes, &why))
	{
		fail(&game, "the loaded game's recording could not be opened: " + why);
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	// the continued log is flushed and checked: a full disk shows only then (review r2: it went on "ready" with a recording that was already incomplete)
	if (!writer.flush(&why))
	{
		fail(&game, "the loaded game's recording could not be written: " + why);
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	// owner decision 7 (single player): this player's execution profile from F on, recorded in the replay itself so the continued log replays exactly
	if (game.logic().simulationProfile() != own)
	{
		writer.recordProfileSwitch(game.logic().getFrame(), own);
		if (!writer.flush(&why))
		{
			fail(&game, "the loaded game's recording could not be written: " + why);
			if (error)
			{
				*error = m_failure;
			}
			return false;
		}
		game.logic().setSimulationProfile(own);
	}
	return true;
}

std::vector<std::string> CommandLogLoad::stopLines()
{
	std::vector<std::string> lines = {
		"[S-4000] saves: a save is the game's command log up to the saved frame (OpenBFME's .obfmesave, SAVES.md phase SAVE-2): a load replays it at full speed "
		"(about 1.8 minutes for a 60-minute game on a fast PC), and only the build that made it can (another build's save is refused naming that build); "
		"retail's Xfer snapshot (SAVE-4: its framework is built, S-4120) and multiplayer saves (SAVE-3) are not built",
		"[S-4001] saves: a save of the other execution profile (Faithful <-> Uber) loads in single player (owner decision 7): its log is replayed under "
		"the profiles it ran (every hash compared), then GameLogic::setSimulationProfile switches to the player's profile at the saved frame and the "
		"switch is recorded in the continued replay (INFERENCE: UBER-1 documents the profile as set before the first frame; nothing it keeps across frames was found "
		"that a switch would break); a campaign mission always runs Faithful",
		"[S-4002] saves: a campaign mission is recorded in memory only (the replay format starts a game from a NewGameMessage, a mission starts from its "
		"campaign): its save carries the campaign, mission and difficulty, which start it again before the catch-up; lane CAMPSAVE-1: the auto-save "
		"after a won mission and CampaignMenu.apt's saves hold the campaign's state between missions (the campaign menu save point), not retail's "
		"GameState snapshot (S-1360)",
		"[S-4003] saves: SaveLoad.apt's saved-game pages (AptSaveLoad): the host lists the Save folder (the campaign's auto-saves in the AutoSaveList); the War "
		"of the Ring page lists nothing and the multiplayer page is not offered; the main menu's Load Game opens the types 0xB (RW 0x91C360, inferred to be its "
		"case); file-name characters are refused at Save, not while typing (S-1409); one result box (APT:SaveGameProgress with GUI:GameSaved or the "
		"error) instead of retail's two, after which (and after a refusal) the pressed button is revealed again as the movie's RevealButtonList does; the disk "
		"space check (RW 0x6DD795) is the write's failure",
		"[S-4004] saves: the catch-up of a loaded save is silent (the audio entry points are uninstalled while it runs) and drops the effects its frames "
		"queued; the script engine's client requests of those frames (camera, interface, music) are applied after it; a command issued but not yet run "
		"when the game is saved is not in the save; a game is saved only after its first frame ran",
		"[S-4005] saves: retail's own saves (.BfME2Skirmish / .BfME2Campaign in the Save folder) are listed as import entries that do not load (SAVE-7)",
	};
	for (const std::string &l : GameState::stopLines()) // lane SAVE-4a: the snapshot's stops
	{
		lines.push_back(l);
	}
	for (const std::string &l : GameLogic::objectSnapshotStopLines()) // lane SAVE-4b: the object side's
	{
		lines.push_back(l);
	}
	for (const std::string &l : GameLogic::aiSnapshotStopLines()) // lane SAVE-4c: its blocks' stops
	{
		lines.push_back(l);
	}
	for (const std::string &l : GameLogic::save4dStopLines()) // lane SAVE-4d: its blocks' stops
	{
		lines.push_back(l);
	}
	// lane SAVE-4e: the snapshot load and the save's loading screen
	lines.push_back("[S-5010] saves: a save with a snapshot is loaded by starting the game its log records (the START, as a replay starts) and loading the "
		"snapshot over it (LiveGame::loadSnapshot), not by retail's GameState::loadGame (RW 0x6DE5D1: a new game made from the file's blocks). The whole "
		"stream's framing is checked before any block loads (GameState::checkFraming: the blocks in this build's order, inside the stream, the end); a "
		"block refused later is undone by loading the game's own snapshot back (teams past the older snapshot's are removed, trees go back to the map's "
		"first), checked by its state hash and bytes; a game that cannot be put back is closed. A load from the quit menu keeps the running game's own save "
		"in memory and loads it back when the chosen save is refused (OpenBFME's: retail returns to the shell). The command log stays the replay so far "
		"and the cross-check of the verify mode (off by default). A snapshot the game refuses fails the save, named (no log-only fallback)");
	lines.push_back("[S-5011] saves: after a snapshot load the client gets each object's whole model condition set and a flush (INFERENCE: retail's loaded "
		"drawables are made from the loaded objects, ZH GameClient::xfer); not given back: the drawables' module / sub object visibility requests of "
		"upgrades and scripts, Create-a-Hero colours set after the object's creation, a disguised object's look, effects, sounds and animations in "
		"flight, the client's own state (camera, selection, the control bar)");
	lines.push_back("[S-5012] saves: loading a save shows retail's picture screen with a film still (GameLogic::getLoadScreen RW 0x626A87 with a save loading: "
		"StillImage_Film01 .. 21, the ring, GUI:Loading) for a campaign mission and a skirmish (INFERENCE: GameLogic + 0x114 is 0 when no lobby ran, so a "
		"skirmish gets the picture screen too); the AudioSettings' LoadScreenMusic retail passes there is not played; an earlier alpha's log-only save "
		"catches up under OpenBFME's progress text and bar drawn on that screen in its font and colour (retail's screen has none)");
	lines.push_back("[S-5013] saves: T3 (the snapshot's bytes on Linux / GCC and Windows): the Windows peer is the release's llvm-mingw build, not MSVC, and "
		"it runs under Wine (Proton) for the test, not on Windows itself");
	lines.push_back("[S-5014] saves: an object id a snapshot names (Xfer::xferObjectID, the id list helpers) is looked up when it is used "
		"(GameLogic::findObjectByID is bounded: an id that names no object finds none) and is not checked against the ids the game has issued, except "
		"the AI's (AIXfer::objectId) and the links the Objects block resolves; an id not yet issued would name the object that later gets it");
	return lines;
}

void CommandLogLoad::fail(LiveGame *game, const std::string &why)
{
	if (m_failure.empty())
	{
		m_failure = why;
	}
	m_state = State::Failed;
	if (game && m_playback)
	{
		game->setFrameDriver(nullptr);
		if (m_restoreThread)
		{
			game->setLogicThread(true);
			m_restoreThread = false;
		}
	}
}

bool CommandLogLoad::prepare(const SaveGameFile &save, std::string *error)
{
	m_state = State::Idle;
	m_failure.clear();
	std::string why;
	m_snapshot.clear();
	m_mode = Mode::CatchUp;
	m_elapsedMs = 0.0;
	m_snapshotMs = 0.0;
	if (!ReplayFile::parse(save.log, m_log, &why))
	{
		fail(nullptr, "the save's command log is damaged: " + why);
	}
	else if (!m_log.complete)
	{
		fail(nullptr, "the save's command log has no end record");
	}
	else if (!m_log.lost.empty())
	{
		fail(nullptr, "the save's command log is incomplete: " + m_log.lost.front());
	}
	else if (m_log.finalFrame != save.header.frame || m_log.finalHash != save.header.stateHash)
	{
		fail(nullptr, "the save's command log ends at frame " + std::to_string(m_log.finalFrame) + " (hash " + std::to_string(m_log.finalHash)
			+ "), its header says frame " + std::to_string(save.header.frame) + " (hash " + std::to_string(save.header.stateHash) + ")");
	}
	else if (m_log.header.profile != save.header.profile)
	{
		fail(nullptr, "the save's command log was recorded by another profile than its header names: "
			+ ProfileIdentity::difference(save.header.profile, m_log.header.profile));
	}
	else
	{
		checkCoverage(save.header.frame);
	}
	m_targetProfile = m_log.header.game.game.simulationProfile;
	for (const auto &sw : m_log.profileSwitches)
	{
		m_targetProfile = sw.second;
	}
	const char *targetName = m_targetProfile == SimulationProfile::Uber ? "uber" : "enhanced";
	if (m_state != State::Failed && save.header.executionProfile != targetName)
	{
		fail(nullptr, "the save's execution profile is " + save.header.executionProfile + ", its log ends under " + targetName);
	}
	if (m_state == State::Failed)
	{
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	m_target = save.header.frame;
	m_targetHash = save.header.stateHash;
	m_logBytes = save.log;
	m_snapshot = save.snapshot; // lane SAVE-4e: the container checked its digest and size (SaveGameFile)
	m_mode = !m_snapshot.empty() && !m_verifyLog ? Mode::Snapshot : Mode::CatchUp;
	return true;
}

bool CommandLogLoad::takeSnapshot(LiveGame &game, std::vector<std::uint8_t> &out, std::string *error)
{
	game.drainFrames(); // between frames: the frames already taken run first
	std::string why;
	out.clear();
	if (!game.logic().saveSnapshot(out, &why))
	{
		out.clear();
		if (error)
		{
			*error = "the game cannot be saved: " + why;
		}
		return false;
	}
	return true;
}

bool CommandLogLoad::beginSnapshot(LiveGame &game, std::string *error)
{
	// lane SAVE-4e: the started game (the log's START, frame m_startFrame) takes the snapshot's state at F
	const double t0 = nowMs();
	std::string why;
	// the frame and the state hash the header names are checked inside the load: a mismatch is refused like a refused block (the game is put back)
	const bool loaded = game.loadSnapshot(m_snapshot, &why, [this](GameLogic &logic) -> std::string {
		if (logic.getFrame() != m_target)
		{
			return "it is of frame " + std::to_string(logic.getFrame()) + ", the save's header says frame " + std::to_string(m_target);
		}
		const std::uint32_t hash = logic.computeStateHash();
		if (hash != m_targetHash)
		{
			return "it loads with state hash " + std::to_string(hash) + " at frame " + std::to_string(m_target) + ", the save recorded "
				+ std::to_string(m_targetHash);
		}
		return std::string();
	});
	m_snapshotMs = nowMs() - t0;
	m_elapsedMs = m_snapshotMs;
	if (!loaded)
	{
		fail(&game, "the save's snapshot is refused: " + why);
	}
	else if (game.frame() != m_target)
	{
		fail(&game, "the loaded game's published frame is " + std::to_string(game.frame()) + ", not the saved frame " + std::to_string(m_target));
	}
	if (m_state == State::Failed)
	{
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	m_reached = m_target;
	m_state = State::Ready;
	return true;
}

// lane UBER-2C: a frame the log must hold the state hash of. A frame run under the Uber profile (the start's, or that of the last profile switch before
// it) needs one only every LiveGameFrameDriver::kUberRecordedHashEvery frames (the Uber recording's cadence); the saved frame always has one
bool CommandLogLoad::hashRequired(UnsignedInt f, UnsignedInt target) const
{
	return f == target || hashRecordedFor(m_log, f);
}

bool CommandLogLoad::hashRecordedFor(const ReplayFile &log, UnsignedInt f)
{
	SimulationProfile p = log.header.game.game.simulationProfile;
	for (const auto &sw : log.profileSwitches)
	{
		if (sw.first < f)
		{
			p = sw.second;
		}
	}
	return p != SimulationProfile::Uber || f % (UnsignedInt)LiveGameFrameDriver::kUberRecordedHashEvery == 0;
}

void CommandLogLoad::checkCoverage(UnsignedInt target)
{
	// review: every frame of the log has its state hash, once (duplicates are refused by ReplayFile::parse), none after F, the hash of F among them; the
	// first one is checked against the loaded game's first frame in begin()
	if (!m_log.hashes.empty() && m_log.hashes.rbegin()->first > target)
	{
		fail(nullptr, "the save's command log has a state hash of frame " + std::to_string(m_log.hashes.rbegin()->first) + ", after its end at frame "
			+ std::to_string(target));
		return;
	}
	if (!m_log.frames.empty() && m_log.frames.rbegin()->first >= target)
	{
		fail(nullptr, "the save's command log has a command record of frame " + std::to_string(m_log.frames.rbegin()->first) + ", at or after its end at frame "
			+ std::to_string(target));
		return;
	}
	if (!m_log.profileSwitches.empty() && m_log.profileSwitches.back().first > target)
	{
		fail(nullptr, "the save's command log switches the execution profile at frame " + std::to_string(m_log.profileSwitches.back().first)
			+ ", after its end at frame " + std::to_string(target));
		return;
	}
	if (m_log.hashes.empty())
	{
		return; // no frame ran (checked against the loaded game's frame in begin)
	}
	for (UnsignedInt f = m_log.hashes.begin()->first; f <= target; ++f)
	{
		if (!m_log.hashes.count(f) && hashRequired(f, target))
		{
			fail(nullptr, "the save's command log has no state hash of frame " + std::to_string(f));
			return;
		}
	}
}

bool CommandLogLoad::begin(LiveGame &game, std::string *error)
{
	if (m_state != State::Idle)
	{
		if (error)
		{
			*error = m_failure.empty() ? "the save's log was not prepared" : m_failure;
		}
		return false;
	}
	m_startFrame = game.frame();
	m_reached = m_startFrame;
	if (m_startFrame > m_target)
	{
		fail(&game, "the loaded game starts at frame " + std::to_string(m_startFrame) + ", after the saved frame " + std::to_string(m_target));
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	// the log's state hashes start with the first frame the loaded game runs (start + 1) and go on without a gap to F (checkCoverage)
	const UnsignedInt firstHash = m_log.hashes.empty() ? m_target : m_log.hashes.begin()->first;
	const UnsignedInt first = m_target == m_startFrame ? m_target : m_startFrame + 1;
	UnsignedInt expected = first; // lane UBER-2C: the first frame that must have a hash (an Uber log skips to its cadence)
	while (expected < m_target && !hashRequired(expected, m_target))
	{
		++expected;
	}
	if ((m_log.hashes.empty() && m_target != m_startFrame) || firstHash < first || firstHash > expected)
	{
		fail(&game, firstHash > expected || m_log.hashes.empty()
				? "the save's command log has no state hash of frame " + std::to_string(expected)
				: "the save's command log has a state hash of frame " + std::to_string(firstHash) + ", before the loaded game's first frame "
					+ std::to_string(expected));
		if (error)
		{
			*error = m_failure;
		}
		return false;
	}
	if (m_mode == Mode::Snapshot)
	{
		return beginSnapshot(game, error);
	}
	m_playback = std::make_unique<ReplayPlayback>(m_log);
	RegisterLogicCRCHandler(game.dispatch());
	m_restoreThread = game.logicThread();
	if (m_restoreThread)
	{
		game.setLogicThread(false); // the frames run here, one per advance (the worker's results are the same, SMOOTH-1)
	}
	game.setFrameDriver(m_playback.get());
	m_state = State::CatchingUp;
	return true;
}

CommandLogLoad::State CommandLogLoad::step(LiveGame &game, std::uint32_t budgetMs)
{
	if (m_state != State::CatchingUp)
	{
		return m_state;
	}
	const double t0 = nowMs();
	while (game.frame() < m_target)
	{
		if (game.advance(0.2) != 1)
		{
			fail(&game, "the catch-up did not run frame " + std::to_string(game.frame()) + " of the save's log");
			return m_state;
		}
		if (m_playback->hasMismatch())
		{
			const ReplayPlayback::Mismatch &m = m_playback->firstMismatch();
			fail(&game, "the saved game does not replay the same in this build: frame " + std::to_string(m.frame) + " has state hash " + std::to_string(m.played)
				+ ", the save recorded " + std::to_string(m.recorded));
			return m_state;
		}
		if (nowMs() - t0 >= (double)budgetMs)
		{
			break;
		}
	}
	m_reached = game.frame();
	m_elapsedMs += nowMs() - t0;
	if (m_reached < m_target)
	{
		return m_state;
	}
	game.drainFrames();
	game.setFrameDriver(nullptr);
	if (m_restoreThread)
	{
		game.setLogicThread(true);
		m_restoreThread = false;
	}
	const std::uint32_t hash = game.logic().computeStateHash();
	if (hash != m_targetHash)
	{
		fail(&game, "the saved game does not replay the same in this build: frame " + std::to_string(m_target) + " has state hash " + std::to_string(hash)
			+ ", the save recorded " + std::to_string(m_targetHash));
		return m_state;
	}
	if (!m_snapshot.empty())
	{
		// lane SAVE-4e, the verify mode: the caught-up game's snapshot is the save's, byte for byte
		std::vector<std::uint8_t> ours;
		std::string why;
		if (!game.logic().saveSnapshot(ours, &why))
		{
			fail(&game, "the caught-up game's snapshot is refused: " + why);
			return m_state;
		}
		if (ours != m_snapshot)
		{
			size_t at = 0;
			while (at < ours.size() && at < m_snapshot.size() && ours[at] == m_snapshot[at])
			{
				++at;
			}
			fail(&game, "the save's snapshot is not the state its log replays to: the caught-up game's snapshot (" + std::to_string(ours.size())
				+ " bytes) differs from the save's (" + std::to_string(m_snapshot.size()) + " bytes) from byte " + std::to_string(at));
			return m_state;
		}
	}
	m_state = State::Ready;
	return m_state;
}
