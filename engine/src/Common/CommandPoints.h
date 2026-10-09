// OpenBFME. GPL-3.0.
//
// CommandPoints (RotWK Player + 0x60 object, RW 0x6A7B3E .. 0x6A8230; BFME-new: ZH has no command points), lane ECON-1: the army size a player may
// field. Units cost command points while they live; buildings and upgrades raise the limit.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every address was read from the disassembly, the object layout is the binary's):
//   +0x04 base    the starting limit (RW 0x6A8183 reset: 100)       +0x08 usage    the sum of the live units' CommandPoints
//   +0x0C bonus   the sum of the live buildings' CommandPointBonus   +0x10 cap      the hard ceiling
//   +0x14 player index                                               +0x18 / +0x1C  two territory counters (Living World), 0 in a skirmish
//   +0x20 .. +0x24 vector of 12-byte records {int value; ObjectID object; ObjectFilter filter handle}   +0x2C "a script set the limits" byte
//   * limit (RW 0x6A7B9F) = min(cap, bonus + base + the sum of the records whose filter is unset / not `valid` or is satisfied by an object of the
//     player (Player::hasObjectMatching(filter, 1), RW 0x6ABD0B)). available (RW 0x6A7F6A) = limit - usage.
//   * canAfford (RW 0x6A7F79, args template, bool ignored): the template's CommandPoints (tt + 0x628) 0 -> true; ok = usage + cp <= limit; a template whose
//     KindOf has bit 151 (ARMY_OF_DEAD, tt + 0x11A & 0x80) is always affordable.
//   * the usage of one object (RW 0x6A7FAA): 0 unless its template is SELECTABLE (KindOf bit 1), not STRUCTURE (bit 7) and not HORDE (bit 109), else the
//     template's CommandPoints; added / subtracted by RW 0x6A7FDA / 0x6A7FEB. The bonus of one object (RW 0x6A7C01) = the template's CommandPointBonus
//     (tt + 0x62C) + the object's COMMAND_POINT_BONUS attribute modifier (type 0x18, a float sum, not ported: stop S-253), converted with cvttss2si;
//     added / subtracted by RW 0x6A7C3E / 0x6A7C51.
//   * Player::onObjectGained (RW 0x6AA56D) / onObjectLost (RW 0x6AA590): a template with CommandPointBonus > 0 adds / removes its bonus, any other adds /
//     removes its usage. The Object layer drives them (Object::addToPlayerCommandPoints RW 0x68E0C2 / remove RW 0x68E114, the owner change RW 0x6914B7).
//   * init (RW 0x6A7C86, args player index 0..19, evil flag): see CommandPointsSource below. The records are added by CommandPointsUpgrade
//     (RW 0x6A81E3: value, the source object's id, a copy of the RequiredObject filter) and removed by RW 0x6A8033 (the first record with that value and id).
//   * set (RW 0x6A7ACD): a script action stores base and cap and raises the "script set" byte; the next init then only raises them.
//
// Determinism: integers only, the records in a vector in insertion order.

#pragma once

#include "Common/StateHash.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct ObjectFilter;

// The command point arithmetic is the binary's 32-bit `add` / `sub` / `imul`: modulo 2^32 on the bit patterns, with SIGNED compares afterwards (RW 0x6A7B9F cmovg, 0x6A7F79 jle).
// These helpers do the arithmetic in uint32 and reinterpret the bits, so no overflow is undefined behaviour (review ECON-1 r1 item 4).
namespace CpMath
{
inline std::int32_t asInt(std::uint32_t v) { return (std::int32_t)v; } // two's complement bit pattern (modular conversion)
inline std::int32_t add(std::int32_t a, std::int32_t b) { return asInt((std::uint32_t)a + (std::uint32_t)b); }
inline std::int32_t sub(std::int32_t a, std::int32_t b) { return asInt((std::uint32_t)a - (std::uint32_t)b); }
inline std::int32_t mul(std::int32_t a, std::int32_t b) { return asInt((std::uint32_t)a * (std::uint32_t)b); }
} // namespace CpMath

// What CommandPoints::init chooses a (start, cap) pair from (the GameData values and the game's state), resolved by the caller (EconomySystem), so this
// class stays independent of GameData and the player list. RW 0x6A7C86's four branches:
//   Branch::Multiplayer (GameLogic + 0x114 == 3 and the game is a multiplayer / skirmish game): the MPn pair of the live player count n (n >= 8 -> MP8, 7, 6, 5, 4, 3, else MP2);
//                       base = start + territoryGood * goodStart + territoryEvil * evilStart (the counters are 0 outside Living World), cap = pair cap
//   Branch::Campaign    (a human player of the other game kinds): the GoodCommandPoints / EvilCommandPoints pair (+ the Living World bonus, a hook)
//   Branch::AI          (a computer player of those): the GoodCommandPointsAI / EvilCommandPointsAI pair
//   Branch::LivingWorld (the strategic game): the MPn path with the world's own state (not ported: stop S-254)
struct CommandPointsSource
{
	int start = 0;
	int cap = 0;
	int territoryGoodTerm = 0;   ///< counters[good] * the good pair's start
	int territoryEvilTerm = 0;
	int lobbyPercent = 100;      ///< TheGameInfo + 0x6C when TheGameInfo exists and GameLogic + 0x114 == 3 (applied to the cap), else 100
	bool applyLobbyPercent = false;
};

class CommandPoints
{
public:
	struct Record
	{
		int value = 0;
		std::uint32_t objectId = 0;
		std::shared_ptr<const ObjectFilter> filter; ///< null = the handle -1 (unset): the record always counts
	};

	// RW 0x6A8183: usage 0, bonus 0, base 100, territory counters 0, records cleared, "script set" cleared
	void reset();
	// RW 0x6A7C86 after the branch chose its numbers: base / cap are stored when no script set them or when they are larger; the cap is then scaled by the
	// lobby percentage (integer: cap * percent / 100, signed division)
	void init(int playerIndex, const CommandPointsSource &source);
	// RW 0x6A7ACD (a script): both values replace the current ones and the byte is raised
	void setFromScript(int base, int cap);

	// RW 0x6A7B9F; `recordFilterSatisfied(filter)` answers Player::hasObjectMatching(filter, true) for the owner
	int getLimit(const std::function<bool(const ObjectFilter &)> &recordFilterSatisfied) const;
	// the same with the records that carry a filter treated as unsatisfied (a caller without a world); only for diagnostics, never gameplay
	int getUsage() const { return m_usage; }
	int getBase() const { return m_base; }
	int getBonus() const { return m_bonus; }
	int getCap() const { return m_cap; }
	int getPlayerIndex() const { return m_playerIndex; }
	int getTerritoryGood() const { return m_territoryGood; }
	int getTerritoryEvil() const { return m_territoryEvil; }
	bool scriptSet() const { return m_scriptSet; }
	// not a retail field: whether init() ran (RW 0x6A7C86). Production refuses to ask an uninitialised pool (no silent default): Economy::canAffordCommandPoints throws
	bool initialized() const { return m_initialized; }
	void setTerritoryCounters(int good, int evil)
	{
		m_territoryGood = good;
		m_territoryEvil = evil;
	}

	// RW 0x6A7FDA / 0x6A7FEB (usage) and 0x6A7C3E / 0x6A7C51 (bonus): the amounts are what RW 0x6A7FAA / 0x6A7C01 computed for the object
	void addUsage(int amount) { m_usage = CpMath::add(m_usage, amount); }
	void removeUsage(int amount) { m_usage = CpMath::sub(m_usage, amount); }
	void addBonus(int amount) { m_bonus = CpMath::add(m_bonus, amount); }
	void removeBonus(int amount) { m_bonus = CpMath::sub(m_bonus, amount); }

	// RW 0x6A7F79 for a template with `templateCommandPoints` and `armyOfDead` (KindOf bit 151): usage + cp <= limit
	bool canAfford(int templateCommandPoints, bool armyOfDead, int limit) const;
	// RW 0x6A7F6A: limit - usage
	int getAvailable(int limit) const { return CpMath::sub(limit, m_usage); }

	// RW 0x6A81E3 / 0x6A8033: the records of CommandPointsUpgrade
	void addRecord(int value, std::uint32_t objectId, std::shared_ptr<const ObjectFilter> filter);
	// removes the FIRST record with this value and object id (RW 0x6A8033); false when there is none
	bool removeRecord(int value, std::uint32_t objectId);
	const std::vector<Record> &records() const { return m_records; }

	void crc(StateHasher &h) const;

private:
	int m_base = 0;      // +4
	int m_usage = 0;     // +8
	int m_bonus = 0;     // +0xC
	int m_cap = 0;       // +0x10
	int m_playerIndex = 0; // +0x14
	int m_territoryGood = 0; // +0x18
	int m_territoryEvil = 0; // +0x1C
	std::vector<Record> m_records; // +0x20
	bool m_scriptSet = false; // +0x2C
	bool m_initialized = false; // not retail: init() ran
};
