// OpenBFME. GPL-3.0.
//
// AIStatCollector (lane DECOMP-3): the player info record of TheSkirmishAIManager (0x44 bytes, ctor RW 0x8E46D9; one per playable player, the map at manager
// + 0xA3C keyed by Player + 0x54). It holds the player's objects the skirmish AI reads, filled by the manager's object events rather than rebuilt: the lists
// keep the order in which objects were registered. Donor: BFME2 decomp AIStatCollector.cpp:AIStatCollector (tier B for RW 0x8E46D9) and
// Rva004E0425Dispatch.cpp:AIStatCollector::Register (tier B for RW 0x8E4591); its UnRegister counterpart has no tier in RotWK (RotWK's own RW 0x8E480C is read
// from the disassembly). The decomp names the sub objects by address only.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly, spot-checked in Ghidra):
//   * layout (ctor RW 0x8E46D9): + 0 the army stats (0x64 bytes, ctor RW 0x99E5B6), + 4 the ship stats (0x10, RW 0x99E429), + 8 the structure stats (0x1C,
//     RW 0x99E989), + 0xC the economy stats (0x1C, RW 0x99E738(player)), + 0x10 a position sampler (0x24, RW 0x8E4042(GameData + 0x984, RW 0x8E3D16)), + 0x14
//     a vector of hero ids (RotWK-only), + 0x20 the per-template counts (a hash map NameKeyType -> int), + 0x34 the player, + 0x38 a listener vector.
//   * Register RW 0x8E4591 (called only from the manager's object event RW 0x6A9D9C): the economy add RW 0x99E84E (filter RW 0x99E818: not LINKED_TO_FLAG, not
//     UNATTACKABLE, STRUCTURE, ECONOMY_STRUCTURE or SUPPLY_GATHERING_CENTER -> push_back to + 0xC -> + 4); the structure add RW 0x99EA19 (filter RW 0x99E8C8:
//     not UNATTACKABLE unless EXPANSION_PAD, not NOT_AUTOACQUIRABLE unless REBUILD_HOLE, STRUCTURE, not ECONOMY_STRUCTURE, not LINKED_TO_FLAG, not BASE_SITE;
//     EXPANSION_PAD (template + 0x123 bit 0) -> + 8 -> + 0x10, else + 8 -> + 4); when either took it, the position sampler (RW 0x8E3EF8 / 0x8E4456, not ported);
//     the army add RW 0x99E6C9 (filter RW 0x99E4C9: not UNATTACKABLE, not IMMOBILE, not STRUCTURE, not SHIP, and CAN_ATTACK, HERO, SUPPORT or a DOZER that is not
//     a worker (RW 0x88B897: the AI's vslot 0x174 and 0x190, the supply interface); CAN_ATTACK / HERO / SUPPORT -> + 0 -> + 4, else the dozer -> + 0 -> + 0x58),
//     else the ship add RW 0x99E493 (filter RW 0x99E467: not UNATTACKABLE, not STRUCTURE, SHIP -> + 4 -> + 4). An add that took the object calls the listeners
//     whose mask has its bit (RW 0x8E3D4C: 2 economy, 1 structures, 4 army / ships). When any list took it, the count of its template's name (template + 0x64,
//     TheNameKeyGenerator RW 0x49F474) at + 0x20 is incremented (a new key starts at 1, RW 0x75702E); else a HERO (template + 0x113 bit 2) goes to + 0x14.
//     The filters read only the template's KindOf (and the dozer's AI), and every add is a plain push_back (RW 0x5487BB): no duplicate test.
//   * UnRegister RW 0x8E480C (from the manager's object event RW 0x6A99CB): the same order; each remove (RW 0x99E871 / 0x99E924 / 0x99E523 / 0x99E4B2) asks its
//     filter again and erases the first entry with the object's id (RW 0x65BC83 / RW 0x6B4E3C + 0x8FF64D); a removed structure or economy entry also leaves
//     the position sampler (RW 0x8E46A5); when any list lost it, the template's count is decremented when the key exists (no erase); else a HERO leaves + 0x14.
//   * update RW 0x8E3CF3 (every record in player index order, each manager update): the army list drops the ids that no longer resolve (RW 0x65BD07: + 4 only,
//     the dozers + 0x58 are not pruned) and then makes its idle army record (RW 0x99E63B, AITacticalAI::updateIdleArmy); the ship, structure (+ 4 only) and
//     economy lists drop unresolved ids (RW 0x65BD07).
//   * the count RW 0x8E3FD6(name): the + 0x20 entry of the name, 0 when absent.
// NOT PORTED (stop S-416 / S-418): the position sampler (+ 0x10) and its test RW 0x8E3EF8 against the AI's + 0x160 -> + 0x98 names, the listener vector (+ 0x38;
// no listener is ported). TheNameKeyGenerator's key is the template name here (the map is only looked up, never walked, so its order is not observable).

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <map>
#include <string>
#include <vector>

class GameLogic;
class Object;
class StateHasher;

class AIStatCollector
{
public:
	explicit AIStatCollector(int playerIndex) : m_playerIndex(playerIndex) {}

	int playerIndex() const { return m_playerIndex; }

	// RW 0x8E4591 / RW 0x8E480C
	void registerObject(const Object &obj);
	void unRegisterObject(const Object &obj);
	// RW 0x8E3CF3 without the idle army record (AITacticalAI::updateIdleArmy reads army() right after)
	void update(GameLogic &logic);
	// RW 0x8E3FD6
	int countOf(const std::string &templateName) const;

	const std::vector<ObjectID> &army() const { return m_army; }               ///< + 0 -> + 4
	const std::vector<ObjectID> &dozers() const { return m_dozers; }           ///< + 0 -> + 0x58
	const std::vector<ObjectID> &ships() const { return m_ships; }             ///< + 4 -> + 4
	const std::vector<ObjectID> &structures() const { return m_structures; }   ///< + 8 -> + 4
	const std::vector<ObjectID> &expansionPads() const { return m_pads; }      ///< + 8 -> + 0x10
	const std::vector<ObjectID> &economy() const { return m_economy; }         ///< + 0xC -> + 4
	const std::vector<ObjectID> &heroes() const { return m_heroes; }           ///< + 0x14

	void crc(StateHasher &h) const;

private:
	int m_playerIndex;
	std::vector<ObjectID> m_army;
	std::vector<ObjectID> m_dozers;
	std::vector<ObjectID> m_ships;
	std::vector<ObjectID> m_structures;
	std::vector<ObjectID> m_pads;
	std::vector<ObjectID> m_economy;
	std::vector<ObjectID> m_heroes;
	std::map<std::string, int> m_counts;
};
