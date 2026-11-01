// OpenBFME. GPL-3.0.
//
// WOTR2 milestone 8 (step 1): the strategic game's command path. Every strategic order is a GameMessage of type MSG_LW_* (1701 .. 1728, GameMessage.h) and
// reaches the logic here, as RotWK's LivingWorldLogic message switch RW 0x6BE6A6 takes it from the logic's command stream (specs U1 / U2,
// wotr2-work/specs/U1-strategic-messages-a.md and U2-strategic-messages-b.md). Single player and multiplayer use this one path.
//
// TARGET FACTS (RotWK 2.01, caveat S-001):
//   * the sender (the prologue RW 0x6BE6A6 .. 0x6BE777): in a network game (GameLogic + 0x114 1 / 2) the lobby slot whose name key is the sending Player's
//     (+ 0x50) gives the Living World player (slot + 0x4C); otherwise every message is the first Living World player's. Only 0x6A7 (the battle vote) uses
//     the sender; the others act on the ids in their arguments;
//   * retail checks no owner and almost no phase in the handlers (0x6A8 only: phases 0 / 4); its UI offers only the player's own orders.
// OpenBFME [port decision, stricter than retail]: the order functions keep their owner and phase checks, with the sender as the player (a network peer can
// order only its own armies and regions); a refused order changes nothing and is reported. The argument ids: a region is its index, a plot its slot, a
// building its id (+ 0x18), an ArmyToSpawn key its index + 1, a building template its index in the store [INFERENCE: retail sends the template's name key,
// which the port does not keep], an army its id, a member its entry key.

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include "GameLogic/GameMessage.h"

namespace LivingWorld
{

namespace
{
bool argInt(const GameMessage &m, int i, int &out)
{
	const GameMessageArgument *a = m.getArgument(i);
	if (!a || a->type != ARGUMENTDATATYPE_INTEGER)
	{
		return false;
	}
	out = a->integer;
	return true;
}

bool fail(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}
} // namespace

bool LivingWorldLogic::dispatch(const ::GameMessage &m, int sender, std::string *error)
{
	const bool ok = dispatchOrder(m, sender, error);
	m_refusedOrders += ok ? 0u : 1u;
	return ok;
}

bool LivingWorldLogic::dispatchOrder(const ::GameMessage &m, int sender, std::string *error)
{
	const std::string name = GameMessageTypeName(m.getType());
	auto bad = [&](const std::string &why) { return fail(error, name + ": " + why); };
	if (sender < 0 || sender >= (int)m_players.size())
	{
		return bad("the sender " + std::to_string(sender) + " is no player of the war");
	}
	int a0 = 0, a1 = 0, a2 = 0;
	switch (m.getType())
	{
	case MSG_LW_DONE_PHASE: // RW 0x6BEC57: (turn, phase, player); a message of another turn or phase is stale and ignored
	{
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1) || !argInt(m, 2, a2))
		{
			return bad("the arguments are not (turn, phase, player)");
		}
		if (a0 != m_turn || a1 != m_phase)
		{
			return true;
		}
		if (a2 != sender)
		{
			return bad("a player can end only its own phase"); // port decision (retail flags the argument's player)
		}
		if (m_phase == PHASE_ORDERS)
		{
			return endTurn(a2, error);
		}
		if (m_phase == PHASE_OWNERSHIP)
		{
			return endPhase(a2, error);
		}
		return true; // the other phases pass on their own (update)
	}
	case MSG_LW_SET_CONFLICT_RESOLUTION_METHOD: // RW 0x6BE9A5: (method 1 auto-resolve / 2 retreat / 3 real time, battle)
	{
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (method, battle)");
		}
		if (m_networked)
		{
			// WOTR2 milestone 8 (step 3): every participant's vote, then the decision (RW 0x6BE9A5 .. 0x6BEC30)
			if (a0 < 1 || a0 > 3)
			{
				return bad("the method " + std::to_string(a0) + " is not 1, 2 or 3");
			}
			return networkVote(a1, sender, a0, error);
		}
		if (a0 == 1)
		{
			return chooseAutoResolve(a1, error);
		}
		if (a0 == 2)
		{
			return chooseRetreat(a1, sender, error);
		}
		if (a0 == 3)
		{
			BattleSetup setup;
			if (!chooseRts(a1, &setup, error))
			{
				return false;
			}
			m_pendingBattleSetups.push_back(setup); // the host takes it (takeBattleSetup) and launches the battle
			return true;
		}
		return bad("the method " + std::to_string(a0) + " is not 1, 2 or 3");
	}
	case MSG_LW_ARMY_MOVE_REQUEST: // RW 0x6B5847: (army, region, location: the port moves region to region)
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (army, region, location)");
		}
		return orderMove(sender, a0, a1, error);
	case MSG_LW_CANCEL_ARMY_MOVE_REQUEST: // RW 0x6B32B3: (army)
		if (!argInt(m, 0, a0))
		{
			return bad("the argument is not an army");
		}
		return cancelMove(sender, a0, error);
	case MSG_LW_BUILD_BUILDING: // RW 0x7F32A8: (region, plot, building template)
	{
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1) || !argInt(m, 2, a2) || a2 < 0 || a2 >= (int)m_stores.buildings.size())
		{
			return bad("the arguments are not (region, plot, building)");
		}
		return orderBuild(sender, a0, m_stores.buildings[(size_t)a2].name, error, a1);
	}
	case MSG_LW_DESTROY_BUILDING: // RW 0x7F32F4: (region, plot)
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (region, plot)");
		}
		return orderDestroyBuilding(sender, a0, a1, error);
	case MSG_LW_QUEUE_UNIT:   // the SpawnArmy nugget's queue (vslot + 0x14): (building id, ArmyToSpawn key)
	case MSG_LW_DEQUEUE_UNIT: // (vslot + 0x18): the LAST queued entry of that army
	case MSG_LW_DEQUEUE_UNIT_AT_INDEX: // RW 0x8FF6F8: (building id, queue index)
	{
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (building, key)");
		}
		int region = -1, slot = -1;
		for (int r = 0; r < (int)m_regions.size() && region < 0; ++r)
		{
			for (const Building &b : m_regions[(size_t)r].buildings)
			{
				if (b.id == a0 && a0 != 0)
				{
					region = r;
					slot = b.slot;
				}
			}
		}
		if (region < 0)
		{
			return bad("no building has the id " + std::to_string(a0));
		}
		if (m.getType() == MSG_LW_DEQUEUE_UNIT_AT_INDEX)
		{
			return orderDequeueAt(sender, region, slot, a1, error);
		}
		if (a1 <= 0)
		{
			return bad("the key " + std::to_string(a1) + " is not an ArmyToSpawn's"); // RW: a key of 0 does nothing
		}
		return m.getType() == MSG_LW_QUEUE_UNIT ? orderRecruit(sender, region, slot, a1 - 1, error) : orderDequeue(sender, region, slot, a1 - 1, error);
	}
	case MSG_LW_SWAP_ARMY_MEMBERS: // RW 0x6BB70D: (army A, nA, nA keys, army B, nB, nB keys)
	{
		std::vector<int> keysA, keysB;
		int armyA = 0, armyB = 0, n = 0, at = 0;
		if (!argInt(m, at++, armyA) || !argInt(m, at++, n) || n < 0 || n > (int)m.getArgumentCount())
		{
			return bad("the arguments are not (army, count, keys, army, count, keys)");
		}
		for (int i = 0; i < n; ++i)
		{
			int k = 0;
			if (!argInt(m, at++, k))
			{
				return bad("a key is missing");
			}
			keysA.push_back(k);
		}
		if (!argInt(m, at++, armyB) || !argInt(m, at++, n) || n < 0 || n > (int)m.getArgumentCount())
		{
			return bad("the second army is missing");
		}
		for (int i = 0; i < n; ++i)
		{
			int k = 0;
			if (!argInt(m, at++, k))
			{
				return bad("a key is missing");
			}
			keysB.push_back(k);
		}
		const Army *a = findArmy(armyA);
		if (!a || a->owner != sender)
		{
			return bad("the first army is not the sender's"); // port decision
		}
		return swapArmyMembers(armyA, keysA, armyB, keysB) || bad("the members cannot move there (CanSwapArmyMembers)");
	}
	case MSG_LW_CANCEL_ARMY_MEMBER_MOVE: // RW 0x6B8C33: (army, key)
	{
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (army, key)");
		}
		const Army *a = findArmy(a0);
		if (!a || a->owner != sender)
		{
			return bad("the army is not the sender's");
		}
		return cancelArmyMemberMove(a0, a1) || bad("the member cannot go back to its army");
	}
	case MSG_LW_DISBAND_ARMY_MEMBER: // RW 0x6B4933 + RW 0x71AAEC: (army, key)
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (army, key)");
		}
		return disbandArmyMember(sender, a0, a1, error);
	case MSG_LW_MARK_UNIT_FOR_UPGRADE:   // RW 0x6B7BAA: (army, key)
	case MSG_LW_UNMARK_UNIT_FOR_UPGRADE: // RW 0x6B605F: (army, key)
		if (!argInt(m, 0, a0) || !argInt(m, 1, a1))
		{
			return bad("the arguments are not (army, key)");
		}
		return m.getType() == MSG_LW_MARK_UNIT_FOR_UPGRADE ? orderUpgradeArmyMember(sender, a0, a1, error) : cancelUpgradeArmyMember(sender, a0, a1, error);
	case MSG_LW_PLAYER_SURRENDERED: // RW 0x6BECB5: (player); step 2: the quit menu's own surrender or the network's drop (StrategicSession)
		if (!argInt(m, 0, a0))
		{
			return bad("the argument is not a player");
		}
		if (a0 != sender)
		{
			return bad("a player can surrender only itself"); // port decision (retail removes the argument's player)
		}
		surrender(a0);
		return true;
	case MSG_LW_OPENBFME_SAVE: // WOTR2 milestone 8 (step 4): RotWK's NETCOMMANDTYPE_SAVE_GAME (RW 0x629750) as a strategic order [S-1344]
		return saveMessage(m, sender, error);
	case MSG_LW_REQUEST_ENDGAME: // 0x6B9: the leave-for-now vote after a LAN war's save: (yes, request)
		return leaveVoteMessage(m, sender, error);
	default:
		// 0x6A5 / 0x6B9 .. 0x6BB (sessions, the exit vote, the battle barriers) come with the network steps; 0x6B2 .. 0x6B5, 0x6B7, 0x6BD / 0x6BE
		// are not ported (S-1336); 0x6B6 / 0x6BC have no case in retail either
		return bad("not ported (S-1336)");
	}
}

bool LivingWorldLogic::takeBattleSetup(BattleSetup &out)
{
	if (m_pendingBattleSetups.empty())
	{
		return false;
	}
	out = m_pendingBattleSetups.front();
	m_pendingBattleSetups.erase(m_pendingBattleSetups.begin());
	return true;
}

int LivingWorldLogic::buildingIdAt(int region, int slot) const
{
	if (region < 0 || region >= (int)m_regions.size())
	{
		return 0;
	}
	for (const Building &b : m_regions[(size_t)region].buildings)
	{
		if (b.slot == slot)
		{
			return b.id;
		}
	}
	return 0;
}

bool LivingWorldLogic::liveHuman(int player) const
{
	return player >= 0 && player < (int)m_players.size() && m_players[(size_t)player].human && !m_players[(size_t)player].defeated;
}

// ProcessSaveGameCommand RW 0x629750 (spec wotr2-work/specs/V-lan-war-save.md 1): kind 1 from the asker opens the request (GameLogic + 0x11D) and every
// other human gets the Yes / No box; kind 2 from one of them refuses it; kind 5 (the first Yes) saves on every peer at this frame under the asker's name,
// then the leave-for-now vote opens. A war with no other live human saves at once (retail saves locally). Only on a network war's map
bool LivingWorldLogic::saveMessage(const ::GameMessage &m, int sender, std::string *error)
{
	const std::string name = GameMessageTypeName(m.getType());
	auto bad = [&](const std::string &why) { return fail(error, name + ": " + why); };
	int kind = 0;
	if (!m_networked || !m_started || m_finished)
	{
		return bad("a save request needs a running network war");
	}
	if (!argInt(m, 0, kind))
	{
		return bad("the first argument is not the request kind");
	}
	if (!liveHuman(sender))
	{
		return bad("the sender " + std::to_string(sender) + " is not a live human of the war");
	}
	if (kind == SAVE_ASK)
	{
		int n = 0;
		if (!argInt(m, 1, n) || n < 1 || (size_t)n > kSaveNameMax || (int)m.getArgumentCount() != 2 + n)
		{
			return bad("the save name must be 1 .. " + std::to_string(kSaveNameMax) + " characters");
		}
		std::u16string saveName;
		for (int i = 0; i < n; ++i)
		{
			int c = 0;
			if (!argInt(m, 2 + i, c) || c <= 0 || c > 0xFFFF)
			{
				return bad("character " + std::to_string(i) + " of the save name is not a UTF-16 unit");
			}
			saveName.push_back((char16_t)c);
		}
		std::string why;
		if (!saveNameValid(saveName, &why))
		{
			return bad(why);
		}
		if (m_saveAsker >= 0)
		{
			return bad(m_players[(size_t)m_saveAsker].name + " has asked to save already");
		}
		if (m_leaveVoteOpen)
		{
			return bad("the leave-for-now vote of the last save is open");
		}
		bool others = false;
		for (int p = 0; p < (int)m_players.size(); ++p)
		{
			others = others || (p != sender && liveHuman(p));
		}
		if (!others)
		{
			m_saveOrders.push_back(NetSaveOrder{ saveName, sender, m_frame });
			log(m_players[(size_t)sender].name + " saves the war (no other human to ask)");
			return true;
		}
		m_saveAsker = sender;
		m_saveName = saveName;
		++m_saveRequestSerial; // (Sol wotr2m8s4 follow-up 1) the answers and the leave vote after it name this request
		for (int p = 0; p < (int)m_players.size(); ++p)
		{
			if (p != sender && liveHuman(p))
			{
				StrategicNotice notice;
				notice.kind = StrategicNotice::SAVE_REQUESTED;
				notice.player = p;
				notice.defeated = sender;
				m_notices.push_back(notice);
			}
		}
		log(m_players[(size_t)sender].name + " asks to save the war");
		return true;
	}
	if (kind != SAVE_REFUSE && kind != SAVE_YES)
	{
		return bad("unknown request kind " + std::to_string(kind));
	}
	int request = 0;
	if (m.getArgumentCount() != 2u || !argInt(m, 1, request))
	{
		return bad("an answer is (kind, request)");
	}
	if (m_saveAsker < 0)
	{
		return bad("no save request is pending");
	}
	if ((std::uint32_t)request != m_saveRequestSerial)
	{
		return bad("the answer is for request " + std::to_string(request) + ", the pending one is " + std::to_string(m_saveRequestSerial)); // a late answer
	}
	if (sender == m_saveAsker)
	{
		return bad("the asker cannot answer its own request");
	}
	const int asker = m_saveAsker;
	m_saveAsker = -1;
	if (kind == SAVE_REFUSE)
	{
		StrategicNotice notice;
		notice.kind = StrategicNotice::SAVE_REFUSED;
		notice.player = asker;
		notice.defeated = sender;
		m_notices.push_back(notice);
		log(m_players[(size_t)sender].name + " refuses to save the war");
		m_saveName.clear();
		return true;
	}
	m_saveOrders.push_back(NetSaveOrder{ m_saveName, asker, m_frame });
	m_saveName.clear();
	log(m_players[(size_t)sender].name + " agrees: the war is saved");
	// after the save every live human is asked to leave for now (MSG 0x6B9: unanimous)
	m_leaveVoteOpen = true;
	m_leaveVotes.assign(m_players.size(), -1);
	for (int p = 0; p < (int)m_players.size(); ++p)
	{
		if (liveHuman(p))
		{
			StrategicNotice notice;
			notice.kind = StrategicNotice::LEAVE_VOTE;
			notice.player = p;
			m_notices.push_back(notice);
		}
	}
	return true;
}

// MSG 0x6B9 after a save: (yes, request: the save request it follows). The war ends on every peer once every live human said yes; one no closes the vote and the war goes on
bool LivingWorldLogic::leaveVoteMessage(const ::GameMessage &m, int sender, std::string *error)
{
	const std::string name = GameMessageTypeName(m.getType());
	auto bad = [&](const std::string &why) { return fail(error, name + ": " + why); };
	int yes = 0, request = 0;
	if (!m_leaveVoteOpen)
	{
		return bad("no leave-for-now vote is open");
	}
	if (!argInt(m, 0, yes) || (yes != 0 && yes != 1) || !argInt(m, 1, request) || m.getArgumentCount() != 2u)
	{
		return bad("the arguments must be (1 leave or 0 stay, request)");
	}
	if ((std::uint32_t)request != m_saveRequestSerial)
	{
		return bad("the vote is for request " + std::to_string(request) + ", the open one follows " + std::to_string(m_saveRequestSerial)); // a late vote
	}
	if (!liveHuman(sender))
	{
		return bad("the sender " + std::to_string(sender) + " is not a live human of the war");
	}
	if (m_leaveVotes[(size_t)sender] >= 0)
	{
		return bad(m_players[(size_t)sender].name + " has voted already");
	}
	m_leaveVotes[(size_t)sender] = yes;
	settleLeaveVote();
	return true;
}

void LivingWorldLogic::settleLeaveVote()
{
	if (!m_leaveVoteOpen)
	{
		return;
	}
	bool all = true, anyNo = false;
	for (int p = 0; p < (int)m_players.size(); ++p)
	{
		if (liveHuman(p))
		{
			all = all && m_leaveVotes[(size_t)p] == 1;
			anyNo = anyNo || m_leaveVotes[(size_t)p] == 0;
		}
	}
	if (anyNo)
	{
		m_leaveVoteOpen = false;
		m_leaveVotes.clear();
		log("the war goes on");
	}
	else if (all)
	{
		m_leaveVoteOpen = false;
		m_leaveVotes.clear();
		m_warEndAgreed = true;
		log("every player leaves the war for now");
	}
}

bool LivingWorldLogic::saveNameValid(const std::u16string &name, std::string *why)
{
	auto refuse = [&](const std::string &text) {
		if (why)
		{
			*why = text;
		}
		return false;
	};
	if (name.empty() || name.size() > kSaveNameMax)
	{
		return refuse("the save name must be 1 .. " + std::to_string(kSaveNameMax) + " characters");
	}
	for (size_t i = 0; i < name.size(); ++i)
	{
		const char16_t c = name[i];
		if (c < 0x20 || c == 0x7F)
		{
			return refuse("character " + std::to_string(i) + " of the save name is a control character");
		}
		if (c == u'/' || c == u'\\' || c == u':' || c == u'*' || c == u'?' || c == u'"' || c == u'<' || c == u'>' || c == u'|' || c == u'%')
		{
			return refuse("character " + std::to_string(i) + " of the save name is not allowed in a file name");
		}
	}
	if (name == u"." || name == u"..")
	{
		return refuse("the save name is not a file name");
	}
	if (name.front() == u' ' || name.back() == u' ' || name.back() == u'.')
	{
		return refuse("the save name starts or ends with a space, or ends with a dot");
	}
	return true;
}

} // namespace LivingWorld
