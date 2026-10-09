// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// DisconnectManager (lane MP-2): the "waiting for players" path of a network game: who has gone silent, the disconnect screen with each player's timeout bar
// and kick votes, and dropping a player in step on every remaining peer. Owned by the Network (GameNetwork/Network.h), protocol owner only: it reads clocks
// and connections, never simulation state; what it decides reaches the logic only as an ordinary frame command (DESTROYPLAYER -> MSG_SELF_DESTRUCT).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the object layout is RotWK's, offsets from `this`):
//   * update RW 0x8D8A90 (called by ConnectionManager::update RW 0x8D77B5 when the manager exists, ConnectionManager + 0x12100):
//       for every slot: a connected slot (RW 0x8D3488: the local slot, or a connection whose first field is -1) that is not in a left state (RW 0x8D34D2:
//       ConnectionManager + 0x12080 in 1 .. 3; disconnectPlayer RW 0x8D57D8 writes 3) and has not been heard for 5000 ms (RW 0x8D33C7, per connection
//       + 0x34C; the first query starts the clock) marks the game "lagging" (only the GameSpy ping test reads it: RW 0x8D8D88);
//       a slot not heard for NetworkDisconnectTime (RW 0x8D335D, GlobalData + 0xC20, times four while the logic frame is below RW 0xBFD298 = 6) that is
//       connected and not in a left state is SILENT: the silent count + 1, and once per silent spell its episode counter + 0x272[slot] (u16) + 1 (the flag
//       + 0x282[slot] holds the spell; the counter goes to the LOCAL slot instead when the ping test RW 0x8D7C78(0xBD83D4) fails); any other slot clears
//       its flag and restarts its player timeout (RW 0x8D7DA6, + 0x14[translated slot]).
//       No silent slot: the local flag clears, + 0x270 = 0, and a shown screen goes (RW 0x8D7CA6: hide, state + 0xC = 1 = SCREENOFF, the local player's
//       votes cleared, + 0x25C = 0). A silent slot: + 0x270 = 1; while the game runs (TheGameLogic + 0x120 == 0) with the screen off: the local player
//       quits (TheNetwork vslot 0x94) when its own episode counter reached 5 or it is the only connected slot, else the screen comes on (RW 0x8D8516:
//       DisconnectScreen.apt, state + 0xC = 0 = SCREENON, the names and votes filled in, every timeout restarted, + 600 = 0, + 0x25C = now); then the
//       DISCONNECTKEEPALIVE every 500 ms (RW 0x8D7CE2, + 0x10).
//       The last frame / its time (+ 4 / + 8) follow TheGameLogic's frame (RW 0x8D8C55, without ZH's timeout reset); with the screen on,
//       updateDisconnectStatus RW 0x8D8735.
//   * updateDisconnectStatus RW 0x8D8735, per connected slot with a translated position t (RW 0x8D7D7E: below the local slot unchanged, above it - 1):
//       remaining = timeouts[t] - now + NetworkPlayerTimeoutTime (GlobalData + 0xC24; unsigned 32-bit arithmetic);
//       (remaining < timeout / 3 unsigned, or voted out) and the logic frame differs from + 600: notifyOthersOfCurrentFrame RW 0x8D3DEC (DISCONNECTFRAME of the
//       current frame to every other slot and to the local one), + 600 = the frame;
//       remaining < 0 (signed), voted out, left state 1, or episodes > 4: remaining = 0, and when every in-game player is on the same frame (RW 0x8D861A),
//       the local player is the next packet router (RW 0x8D86D0) and the slot is not in a left state (or is the router): notify, sendDisconnectCommand
//       RW 0x8D80FC (DISCONNECTPLAYER of the slot at the highest disconnect frame), disconnectPlayer RW 0x8D8553, sendPlayerDestruct (DESTROYPLAYER, RW
//       0x8D8553's caller sequence), and in a game the "Network:PlayerLeftGame" notice;
//       with the screen: SetBarPercent(t, remaining * 100 / timeout) (RW 0x91934F), ShowKickButton below 90 % / HideKickButton above 95 % (RW 0x918FF8,
//       shown flag + 0x286[t] of the menu), and at 0 % a vote for the slot (TheNetwork vslot 0x98 = voteForPlayerDisconnect) unless it is voted out.
//   * votes (RW 0x8D7F77 / 0x8D8233 / 0x8D826F): votes[slot][voter] at + 0x30 + (slot * 8 + voter) * 8 ({bool, frame}); a vote counts when its voter is
//       connected, heard within NetworkDisconnectTime (RW 0x8D7F1E) and not in a left state; voted out = votes >= the number of the other slots that are
//       connected and heard and not in left state 1 (no frame test: ZH counted only votes of the current frame).
//   * voteForPlayerDisconnect RW 0x8D8194, processDisconnectVote RW 0x8D8314 (a vote counts only from an in-game sender), applyDisconnectVote RW 0x8D8078,
//       resetPlayersVotes RW 0x8D7FD2, processDisconnectFrame RW 0x8D836B (+ 0x230 frames, + 0x250 received; ConnectionManager + 0x12060 latest frame via
//       RW 0x8D4884; frame data to a peer that is behind: RW 0x8D4761, at most FRAMES_TO_KEEP = 65 frames, RW 0xDB824C), isPlayerInGame RW 0x8D82BB,
//       hasPlayerTimedOut RW 0x8D7E52, allOnSameFrame RW 0x8D861A (only players heard within NetworkDisconnectTime count), isLocalPlayerNextPacketRouter
//       RW 0x8D86D0 (fallback order ConnectionManager + 0x12030, current router + 0x1202C), disconnectPlayer RW 0x8D8553 -> ConnectionManager RW 0x8D57D8.
//   * the menu (DisconnectMenu): show RW 0x9190F0, hide RW 0x918F97, Kick -> vote, Quit RW 0x9195E1 (a vote for every other slot, then TheNetwork
//       vslot 0x94 = quit), the text providers "DisconnectScreen::PlayerName%d" / "DisconnectScreen::VotesReceived%d" (RW 0x9191EC / 0x91949D).
// DONOR FACTS (ZH GameNetwork/DisconnectManager.cpp): the same members and order of operations (translatedSlotPosition, applyDisconnectVote,
//   processDisconnectFrame with sendFrameDataToPlayer, sendPlayerDestruct at the execution frame + 1, DISCONNECTSCREENOFF / notifyOthersOfNewFrame).
//   ZH's frame gate allowedToContinue (no logic frame while the screen is on) is NOT applied: ZH turned the screen off as soon as a frame's commands were
//   complete (DisconnectManager::allCommandsReady), RotWK turns it off only when no slot is silent (RW 0x8D8A90), so with ZH's gate a peer that is behind
//   could never run the frames relayed to it before the drop (INFERENCE: RotWK's gate, if any, was not located).
// OpenBFME DIFFERENCES / INFERENCE (stop S-1121): ZH's notifyOthersOfNewFrame on screen off is kept (RotWK's sender not located); a peer that announced its
//   disconnect frame F runs F only after a DISCONNECTPLAYER, a later frame of an in-game peer, or the release of its announcement by the next packet router
//   (a DISCONNECTSCREENOFF that lists it): the drop of a player and the late arrival of its commands cannot both happen at frame F (retail's race); the
//   GameSpy ping test (no pinger in a LAN game: always passes), TheGameLogic + 0x2A4 and the left state 1 (never set here) are not ported.

#pragma once

#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkSettings.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Network;
struct NetCommandMsg;

class DisconnectManager
{
public:
	enum State
	{
		SCREENON = 0, ///< RotWK + 0xC == 0 (ZH DISCONNECTSTATETYPE_SCREENON)
		SCREENOFF = 1
	};

	// what DisconnectScreen.apt shows, by translated position (the other slots in slot order, the local one skipped); the device layer draws it
	struct Screen
	{
		bool visible = false;
		struct Row
		{
			bool used = false;     ///< a slot of the game at this position (its player name is known)
			int slot = -1;         ///< the untranslated slot
			std::u16string name;
			int votes = 0;         ///< DisconnectScreen::VotesReceived%d
			int barPercent = 100;  ///< SetBarPercent (the player's remaining timeout)
			bool kickShown = false; ///< ShowKickButton / HideKickButton
			bool removed = false;  ///< disconnected (removePlayer)
		};
		std::array<Row, MAX_SLOTS - 1> rows{};
	};

	DisconnectManager();
	void init(const NetworkSettings &settings);

	// RW 0x8D8A90; `now` in ms (the Network's clock), `logicFrame` TheGameLogic's frame (the next frame to run)
	void update(Network &net, std::uint64_t now, std::uint32_t logicFrame);
	// ZH DisconnectManager::processDisconnectCommand: DISCONNECTKEEPALIVE / PLAYER / VOTE / FRAME / SCREENOFF
	void processDisconnectCommand(Network &net, const NetCommandMsg &c, std::uint64_t now, std::uint32_t logicFrame);
	// ZH playerHasAdvancedAFrame
	void playerHasAdvancedAFrame(int slot, std::uint32_t frame);

	// the frame gate (the announced-frame rule above; ZH's screen gate is not applied, see the header)
	bool allowedToContinue(Network &net, std::uint32_t frame) const;

	// the menu's Kick button (RW 0x8D8194 through TheNetwork vslot 0x98): a vote of the local player for `slot` (untranslated)
	void voteForPlayerDisconnect(Network &net, int slot, std::uint32_t logicFrame);

	const Screen &screen() const { return m_screen; }
	State state() const { return m_state; }
	bool anySilent() const { return m_anySilent; }
	int silentEpisodes(int slot) const { return m_silentEpisodes[(size_t)slot]; }
	int countVotesForPlayer(Network &net, int slot, std::uint64_t now) const;
	// the local player has to leave the game (TheNetwork vslot 0x94): its own connection failed five times, or it is the last connected slot
	bool quitRequested() const { return !m_quitReason.empty(); }
	const std::string &quitReason() const { return m_quitReason; }
	// what happened, one line each (screen on / off, votes, drops), for the peer reports
	const std::vector<std::string> &log() const { return m_log; }
	// the slots this peer dropped as the next packet router
	const std::vector<int> &dropped() const { return m_dropped; }

	// RW 0x8D86D0's search: the slot that acts for the remaining players (the local one when it returns the local slot); -1 none
	int nextPacketRouter(Network &net, std::uint64_t now, std::uint32_t logicFrame) const;

	// the acceptance stops of lane MP-2's network side (docs/STOPS.md S-1121 .. S-1124), one line each; every peer report carries them
	static std::vector<std::string> stopLines();

	static int translatedSlotPosition(int slot, int localSlot);
	static int untranslatedSlotPosition(int slot, int localSlot);

private:
	struct Vote
	{
		bool vote = false;
		std::uint32_t frame = 0;
	};
	bool heardWithin(Network &net, int slot, std::uint32_t ms, std::uint64_t now) const;
	bool heard(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const; // RW 0x8D335D
	bool voterAbsent(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const; // RW 0x8D7F1E
	bool isPlayerVotedOut(Network &net, int t, std::uint64_t now, std::uint32_t logicFrame) const;
	bool hasPlayerTimedOut(int t, std::uint64_t now) const;
	bool isPlayerInGame(Network &net, int t, std::uint64_t now, std::uint32_t logicFrame) const;
	bool allOnSameFrame(Network &net, std::uint64_t now, std::uint32_t logicFrame) const;
	bool isLocalPlayerNextPacketRouter(Network &net, std::uint64_t now, std::uint32_t logicFrame) const;
	int countVotes(Network &net, int slot, std::uint64_t now, std::uint32_t logicFrame) const;
	void updateDisconnectStatus(Network &net, std::uint64_t now, std::uint32_t logicFrame);
	void turnOnScreen(Network &net, std::uint64_t now);
	void turnOffScreen(Network &net, std::uint32_t logicFrame);
	void sendKeepAlive(Network &net, std::uint64_t now);
	void notifyOthersOfCurrentFrame(Network &net, std::uint32_t logicFrame);
	void applyDisconnectVote(Network &net, int slot, std::uint32_t frame, int fromSlot);
	void resetPlayersVotes(Network &net, int playerID, std::uint32_t frame);
	void resetPlayerTimeouts(Network &net, std::uint64_t now);
	void disconnectPlayer(Network &net, int slot, std::uint32_t frame);
	std::uint32_t getMaxDisconnectFrame() const;
	void populateScreen(Network &net);
	void note(const std::string &line);

	NetworkSettings m_settings;
	State m_state = SCREENOFF;
	std::uint32_t m_lastFrame = 0;           ///< + 4
	long long m_lastFrameTime = -1;          ///< + 8
	long long m_lastKeepAliveSendTime = -1;  ///< + 0x10
	std::array<std::uint64_t, MAX_SLOTS - 1> m_playerTimeouts{}; ///< + 0x14 (translated)
	Vote m_playerVotes[MAX_SLOTS][MAX_SLOTS]; ///< + 0x30
	std::array<std::uint32_t, MAX_SLOTS> m_disconnectFrames{};   ///< + 0x230
	std::array<bool, MAX_SLOTS> m_disconnectFramesReceived{};    ///< + 0x250
	std::uint32_t m_lastNotifiedFrame = 0;   ///< + 600
	bool m_notifiedValid = false;
	std::uint64_t m_timeOfDisconnectScreenOn = 0; ///< + 0x25C
	bool m_anySilent = false;                ///< + 0x270
	std::array<std::uint16_t, MAX_SLOTS> m_silentEpisodes{}; ///< + 0x272
	std::array<bool, MAX_SLOTS> m_silentFlag{};              ///< + 0x282
	// OpenBFME's announced-frame gate (S-1121): the frame this peer announced in the current spell, and whether the router released it
	bool m_announced = false;
	std::uint32_t m_announcedFrame = 0;
	bool m_announcementReleased = false;
	// lane MP-2 (review): DISCONNECTPLAYERs whose claimed leaves / drops have not all been applied here yet (Network::verifyDropClaims)
	struct PendingDrop
	{
		int sender = -1, target = -1;
		std::uint32_t frame = 0;
		std::uint8_t gone = 0;
	};
	std::vector<PendingDrop> m_pendingDrops;
	void applyPendingDrops(Network &net, std::uint32_t logicFrame);
	std::uint64_t m_lastReannounce = 0;
	Screen m_screen;
	std::string m_quitReason;
	std::vector<std::string> m_log;
	std::vector<int> m_dropped;
};
