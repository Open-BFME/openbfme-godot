// OpenBFME. GPL-3.0.
//
// OrdersIntoFog (lane ANTICHEAT-1; Palantír detector D1, "orders on objects the player could not see"): an after-the-fact integrity check that runs while a
// replay is re-simulated. OpenBFME's own code: retail has no such check. It is NOT simulation code: it only reads the logic (ShroudManager's
// clientObjectStatus, which never writes the hashed status cache, hasSeen, InvisibilityManager) and never changes a peer's state hash.
//
// Why a clean client cannot produce what it flags (TARGET FACTS, RotWK 2.01, read in Ghidra for this lane):
//   * RW 0xB4E890 (an object's status for a player): a clear cell over the object -> CLEAR / PARTIAL_CLEAR and its "seen" byte (record + 0xC4) = 1; every
//     cell shrouded -> SHROUDED and seen = 0; only fogged -> FOGGED, seen unchanged, and SHROUDED when the object hides in fog (RW 0x68EDD0: a MOBILE
//     enemy always; an IMMOBILE enemy until the player has seen it once, or always with HIDE_IF_FOGGED).
//   * RW 0x82C167: a human player's action on a target that is FOGGED or worse is refused (stop S-565).
//   * Retail multiplayer.ini says UseShroud = No: the map starts explored (fogged), so "never seen" is a property of an OBJECT, not of a cell.
//   * OpenBFME's client picks only what it draws (S-288: not FOGGED, SHROUDED or invisible), so its object-targeted orders always name an object the
//     player saw CLEAR / PARTIAL_CLEAR and not stealthed when it clicked. Knowing a map (fixed spawns) does not help: a blind click is a MOVE.
// The innocent difference between the click and the check is time: the order executes up to the run-ahead (Network.h maxRunAhead, 25 frames) after the
// click, on a client that shows the state of a frame or two before. The check therefore asks "was the target visible to the player at ANY state in the
// window [J - windowFrames, J]" (J = the state the frame's commands run on); only a NO is judged further.
//
// What is judged (Palantír §5.2's matrix): object-targeted orders on an object of a player who is the issuer's ENEMY when the order runs:
// MSG_DO_ATTACK_OBJECT / MSG_DO_FORCE_ATTACK_OBJECT (arg 0), MSG_DO_WEAPON_AT_OBJECT (arg 1), MSG_DO_SPECIAL_POWER_AT_OBJECT (arg 1),
// MSG_DO_SPECIAL_POWER_AT_LOCATION's object (arg 2, when one is named), MSG_COMBATDROP_AT_OBJECT (arg 0), MSG_CAPTUREBUILDING (arg 0). Ally / own targets,
// position commands and the engine's own continuations (auto-acquire, scripts, the AI) are not judged (the AI's and scripts' orders are not in the stream).
// Reasons (the strongest first):
//   * never_seen: the target was never visible to the player (no sample, and the engine's own seen byte is 0): e.g. a building clicked through the fog
//     that the player could not have known existed;
//   * hidden_unit: the player saw the target before, but not in the window and it is hidden in fog now (a mobile unit moving in fog);
//   * stealthed: the target's cells were visible, but it was stealthed and undetected for the player for the whole window (confidence 0.6: it can
//     put a game in review, never alone in the alert band, until data rules out disguises and detection timing).
// Also judged (lane ANTICHEAT-1, owner 2026-10-21 "clicks on something they cannot see"): every enemy object a selection message names
// (MSG_CREATE_SELECTED_GROUP and its NO_SOUND / IDLE_WORKER_VOICE forms): OpenBFME's client sends the selection of enemy objects too, and picks only
// what it draws (S-288), so a selected enemy the player could not see is the same evidence as an order on it (BWChart's "suspicious action").
// Not flagged: a remembered building (FOGGED, drawn as a ghost), a stale object id (counted), an object without a shroud record (contained or
// ALWAYS_VISIBLE: counted as abstained).
// Allied vision and revealers need no rule of their own: they look into the player's own cells (RW 0x694A45's mask = the owner and its allies).
//
// Evidence rules (Palantír §0 / §3.6): the result is evidence only when the re-simulation reproduced the players' own recorded state hashes; otherwise the
// caller marks the game abstained. Nothing here acts on a player: the band "alert" only asks the owner to review.

#pragma once

#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class GameLogic;

class OrdersIntoFog
{
public:
	struct Settings
	{
		unsigned windowFrames = 27;     ///< the run-ahead ceiling (Network.h maxRunAhead, 25) + 2 frames of client presentation
		unsigned encounterFrames = 150; ///< events of one player within 30 s of an encounter's first event are that encounter (Palantír §5.3)
		double alertConfidence = 0.9;   ///< an encounter counts toward the alert at this confidence or more
		unsigned alertEncounters = 3;   ///< ... and the alert needs this many of them
		unsigned alertSpanFrames = 600; ///< ... the first and the last at least 2 minutes of game time apart (3 encounters are 60 s apart at least anyway)
	};

	enum class Reason
	{
		NeverSeen,
		HiddenUnit,
		Stealthed,
	};
	static const char *reasonName(Reason r);

	struct Event
	{
		UnsignedInt frame = 0; ///< the state the order ran on (the frame whose command list carried it)
		int player = -1;
		int messageType = 0;
		ObjectID target = 0;
		std::string targetTemplate;
		int targetOwner = -1;
		Reason reason = Reason::NeverSeen;
		long long lastVisibleFrame = -1; ///< the last state the target was visible to the player in (-1 never)
		float x = 0.0f, y = 0.0f;        ///< the target's position when the order ran
		double confidence = 0.0;
		int encounter = 0; ///< 1-based, per player
	};

	struct PlayerResult
	{
		int player = -1;
		int slot = -1; ///< the game slot (the lobby's roster), -1 unknown
		std::string name;
		std::string band = "none"; ///< none | review | alert
		double score = 0.0;        ///< 1 - prod(1 - encounter confidence)
		int encounters = 0;
		int strongEncounters = 0;          ///< encounters at Settings::alertConfidence or more
		unsigned long long judged = 0;     ///< object-targeted orders on enemy objects
		unsigned long long remembered = 0; ///< ... on a remembered (FOGGED, seen) building: never flagged
		unsigned long long stale = 0;      ///< ... naming an object that does not exist (any relationship): not flagged
		unsigned long long abstained = 0;  ///< ... the detector cannot judge (no shroud record, malformed argument)
		std::vector<Event> events;
	};

	// `players`: the logic player indices whose orders are judged (the game's human slots)
	OrdersIntoFog(GameLogic &logic, std::vector<int> players, Settings settings);
	OrdersIntoFog(GameLogic &logic, std::vector<int> players);

	// Before frame `frame` runs, with its complete command list: samples what every judged player sees in the current state, then judges the orders. Call
	// it for EVERY frame, also without commands (a missing sample can only widen what counts as seen, never flag more).
	void beforeFrame(UnsignedInt frame, const std::vector<GameMessage> &commands);

	// the game cannot be judged (the re-simulation does not reproduce the recorded hashes, the replay is incomplete ...): every result says so
	// the game slot of a judged player (the roster the lobby / Orthanc maps to an install), for the report
	void setSlot(int player, int slot);
	void abstain(const std::string &reason);
	bool abstained() const { return !m_abstain.empty(); }
	const std::string &abstainReason() const { return m_abstain; }

	std::vector<PlayerResult> results() const;
	// the report (schema in tools/integrity/README.md); `gameId` "" -> null, `crcParity` "match" | "mismatch" | "none"
	std::string json(const std::string &gameId, const std::string &crcParity, UnsignedInt frames) const;

	unsigned long long samples() const { return m_samples; }
	// lane ANTICHEAT-1 (D2, ViewChecks.h): the last batch frame whose state showed `object` to `player` (-1 never, or a player not judged)
	long long lastVisibleFrame(int player, ObjectID object) const;
	const Settings &settings() const { return m_settings; }
	// the argument index of the target object of a judged message type, -1 for a type that is not judged
	static int targetArgument(int messageType);
	// MSG_CREATE_SELECTED_GROUP (and its NO_SOUND / IDLE_WORKER_VOICE forms): every enemy object it names is judged
	static bool isSelection(int messageType);

private:
	struct Tracked
	{
		int player = -1;
		std::unordered_map<ObjectID, UnsignedInt> lastVisible; ///< enemy objects the player saw, with the last batch frame whose state showed them
		PlayerResult result;
	};

	void sample(UnsignedInt label);
	void judge(UnsignedInt batch, const GameMessage &m);
	void judgeTarget(UnsignedInt batch, const GameMessage &m, ObjectID id, Tracked &t);
	Tracked *trackedFor(int player);

	GameLogic &m_logic;
	Settings m_settings;
	std::vector<Tracked> m_tracked;
	std::string m_abstain;
	unsigned long long m_samples = 0;
	bool m_sampledAny = false;
	UnsignedInt m_lastSample = 0, m_lastBatch = 0;
};
