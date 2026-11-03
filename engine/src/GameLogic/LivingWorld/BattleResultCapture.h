// OpenBFME. GPL-3.0.
//
// A War of the Ring battle's result read from the tactical game (GameLogic::LivingWorldTacticalBattleComplete RW 0x626662): the survivors and the hero
// records with their army tags, the victor, each battle player's earned spell points, sciences, lasting upgrades and skill points, the reinforcements
// never called in and the region buildings that died. Moved from GodotLivingWorld.cpp (collect_battle_result, WOTR2 milestones 6 / 7) for WOTR2
// milestone 8 (step 3): a LAN war's battle reads it at the end boundary T_end on every peer.
//
// BattleResultCapture (WOTR2 milestone 8, step 3): every peer of a network battle leaves it on its own frame (its end-game timer counts from the snapshot
// that showed it the victory), so a result read at the exit would come from different logic states. The capture reads it at the end of logic frame
// T_end (EndGameController::victoryTimerEndFrame: the victory event's frame + the end-game timer), the same state on every peer, from a GameLogic
// end-of-frame hook on the simulation's thread. Reading only: no logic state changes, the hash is untouched.

#pragma once

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class GameLogic;
class Player;

// one battle player: its tactical player's name, its Living World player index and its garrison army id (side order)
struct BattleSeat
{
	std::string name;
	int player = -1;
	int garrison = 0;
};

// `left`: the tactical player that left by the quit menu before the end (null: none) [S-1329 inference: MSG_SELF_DESTRUCT kills its objects, so it has
// no survivors; the victor is the first other player still undefeated when every undefeated player but the leaver is one alliance and none is the
// leaver's ally]. False + *error when a seat's player is not in the game. Reads the upgrade and science stores (TheUpgradeCenter, TheScienceStore)
bool ReadBattleResult(const GameLogic &logic, const std::vector<BattleSeat> &seats, const Player *left, LivingWorld::BattleResult &out, std::string *error);

class BattleResultCapture
{
public:
	// installs the end-of-frame hook on `logic` (call while the simulation is idle, before T_end); the logic must outlive the capture's use of the hook
	// (the hook holds the shared state, not the capture)
	void arm(GameLogic &logic, const std::vector<BattleSeat> &seats);
	bool armed() const { return m_state != nullptr; }
	// the result read at the end of frame T_end; false before that frame ran (*error says why: not armed, not reached, or the read's refusal)
	bool result(LivingWorld::BattleResult &out, long long *frame = nullptr, std::string *error = nullptr) const;
	bool done() const;

private:
	struct State
	{
		mutable std::mutex mutex;
		std::vector<BattleSeat> seats;
		bool done = false;
		bool ok = false;
		long long frame = -1;
		LivingWorld::BattleResult result;
		std::string error;
	};
	std::shared_ptr<State> m_state;
};
