// OpenBFME. GPL-3.0.
//
// StrategicSession (WOTR2 milestone 8, step 2): a War of the Ring's strategic map in a network game. RotWK runs the map inside the normal lockstep: the
// MSG_LW_* orders travel in the logic command stream, LivingWorldLogic::update (RW 0x6BE50E) runs once per lockstep logic frame after GameLogic::update
// (RW 0x632A92), and the frame CRC holds CHUNK_LivingWorldLogic while the map is up (GameLogic::getCRC RW 0x625886). Spec:
// wotr2-work/specs/V-lan-war-of-the-ring.md sections 2, 3 and 5.
//
// The port keeps the lockstep (GameNetwork/Network.h: frames, run-ahead, the CRC exchange, the disconnect path) and drives the strategic logic with it
// directly: each frame's commands go through LivingWorldLogic::dispatch with the sending slot's player as the sender, then the logic's update, the phase
// timer's automatic DONE_PHASE (UpdateTurnPhase RW 0x6BE283, the local player's only) and the frame clock (L + 0x100); the CRC is the strategic state hash.
// [S-1337, port decision] retail ticks an empty GameLogic under the map (frames, an empty object list); the port has no tactical world on the map, so the
// strategic state is the whole CRC. Not observable: the frame rate, the run-ahead and the CRC cadence are the lockstep's.
//
// The disconnect path's MSG_SELF_DESTRUCT (a dropped player, on every peer at one frame) becomes MSG_LW_PLAYER_SURRENDERED 0x6B8 of that player: RotWK's
// next packet router appends the 0x6B8 itself after it destroys the player (DisconnectManager::updateDisconnectStatus RW 0x8D8735), which lands it in the
// same frame's commands on every peer; the handler removes the player and its whole team (LivingWorldLogic::surrender).
//
// Transport-agnostic: the session takes a NetTransport (the LAN Transport on the lobby socket, or the relay transport of an online game); no socket and no
// address reaches it or the strategic logic.

#pragma once

#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/Network.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The war every peer builds from the START's GameInfo (LivingWorldCampaign::CreatePlayers RW 0x932EB8): the occupied slots in slot order, each one a
// Living World player (retail writes the player's id into the slot, + 0x4C); the start position is a region index (RW 0x9759BB); the faction is the slot's
// PlayerTemplate; the AI level the slot state (RW 0x6E189C); the handicap level the negated lobby handicap (-(slot + 0x20)); the seed the lobby seed.
struct WarStart
{
	LivingWorld::GameSetup setup;
	std::array<int, MAX_SLOTS> slotPlayer{}; ///< slot -> Living World player index (-1: no player)
	int phaseTimerSeconds = 0;               ///< RULE_STRATEGIC_PHASE_TIMER (TheGameInfo + 0x78), 0 none
	int battleChoice = 0;                    ///< RULE_BATTLE_CHOICE (TheGameInfo + 0x80): 0 auto-resolve first, 1 real time first
};
// `playerTemplateNames`: the RTS PlayerTemplate store's names in index order (a slot's playerTemplate indexes it). False + *error when the GameInfo is not a
// War of the Ring, names an unknown scenario or region, or a slot has no faction or no start region (the host's GUI:DisabledFaction / GUI:NeedStartSpots
// rules: retail gives such a slot no player, RW 0x932EB8; the port refuses the game by name)
bool BuildWarStart(const SkirmishGameInfo &g, const LivingWorld::LivingWorldStores &stores, const std::vector<std::string> &playerTemplateNames, WarStart &out,
	std::string *error);

class StrategicSession
{
public:
	// `config.slotPlayerIndex` must hold every network slot's Living World player (WarStart::slotPlayer); the session sets the Strategic message family.
	// `logic` must be started; it is made networked with `phaseTimerSeconds`. The load barrier's LOADCOMPLETE is sent at once (the map needs no load)
	StrategicSession(NetTransport &transport, NetworkConfig config, LivingWorld::LivingWorldLogic &logic, int phaseTimerSeconds, int battleChoice = 0);

	// the local player's order (an MSG_LW_* message; its player index is set to the local player's): goes to every peer, runs on its execution frame. False
	// + *error for another message type
	bool post(const GameMessage &m, std::string *error = nullptr);
	// one protocol step: services the network; runs the next strategic frame when every player's commands for it are in. True when a frame ran
	bool step();
	// the local player leaves (Network::leave): the others drop it at its leave frame
	void leave();

	std::uint32_t frame() const { return m_frame; } ///< the next frame to run (frames run so far)
	int localPlayer() const { return m_localPlayer; }
	Network &network() { return *m_network; }
	const Network &network() const { return *m_network; }
	// every command run, in order (the strategic command stream; a refused order is kept with its reason)
	struct Executed
	{
		std::uint32_t frame = 0;
		int sender = -1;
		int type = 0;
		bool ok = false;
		std::string error;
	};
	const std::vector<Executed> &executed() const { return m_executed; }
	// the state hash after every frame (frame N + 1 -> the state after frame N ran): tests compare peers
	const std::vector<std::uint32_t> &hashes() const { return m_hashes; }
	// protocol errors of the session and its Network (never silent)
	std::vector<std::string> errors() const;

private:
	std::unique_ptr<Network> m_network;
	LivingWorld::LivingWorldLogic &m_logic;
	CommandList m_local;
	std::uint32_t m_frame = 0;
	int m_localPlayer = -1;
	int m_doneSentTurn = -1, m_doneSentPhase = -1; ///< L + 0x10A: the local player's DONE_PHASE went out for this turn's phase
	std::vector<Executed> m_executed;
	std::vector<std::uint32_t> m_hashes;
	std::vector<std::string> m_errors;
	void run(const GameMessage &m, int sender);
};
