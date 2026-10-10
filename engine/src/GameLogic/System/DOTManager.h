// OpenBFME. GPL-3.0.
//
// DOTManager (lane DECOMP-1): RotWK's damage over time, TheGameLogic + 0x174. A DOTNugget's hit registers a copy of its DamageInfo per victim here, and the
// manager re-applies it every DamageInterval frames until DamageDuration has passed. Zero Hour has no counterpart.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra / capstone):
//   * DOTNugget (ctor RW 0x911494: DamageNugget's ctor, then + 0x1C4 / + 0x1C8 = 0, vtable RW 0xC7BAE8). Its table RW 0xC7BAB8: DamageInterval + 0x1C4,
//     DamageDuration + 0x1C8, both parseDurationUnsignedInt (RW 0x73A429). The vtable equals DamageNugget's (RW 0xC7AE78) in slots 1 .. 13: shouldDeliver,
//     the radius damage (RW 0x90DEF0), asDamageNugget (slot 10 RW 0x8CEF91 returns `this`), ...; only slot 14, the per-victim apply, differs.
//   * Slot 14 (RW 0x911407, victim, source, centre): when TheGameLogic + 0x174 exists and there is a victim, a DOT record (ctor RW 0x8209BB: a DamageInfo
//     (RW 0x66365E) then + 0x7C interval, + 0x80 end frame, + 0x84 next frame) is filled by the nugget's fillDamageInfo (RW 0x90E28C, the same arguments
//     as the plain hit); when that answers true the record gets interval = + 0x1C4, end = frame + 0x1C8, next = frame + 0x1C4 and goes to RW 0x821073 with
//     the victim's id (Object + 0x74). Then, whatever happened, DamageNugget's slot 14 (RW 0x90E683) deals the plain hit and its answer is returned.
//   * add (RW 0x821073, id, record): the map at + 4 (an STL map keyed by object id). No entry: one is made (RW 0x821017) and the record copied in (RW 0x82092E).
//     An entry: the record replaces it when RW 0x8208D4 says the new one is stronger, then RW 0x820A2B tells the victim's drawable (below).
//   * RW 0x8208D4 (x87 under the game's PC24): (new.end - frame) loaded unsigned (fild, + 2^32 when negative) times new.amount (D + 0x20), against the same
//     product of the current record; the new record wins when its product is strictly greater (an unordered compare keeps the current one).
//   * update (RW 0x820EF0, from GameLogic::update phase 1 at RW 0x62E8CF: after TheGlobalWeatherSystem, before the InvisibilityManager at + 0x178): with
//     now = the frame, in id order: a missing object drops its entry; an entry whose end is 0 is dropped (and the drawable told); else when next != 0 and
//     now >= next the object's attemptDamage (RW 0x698E7D) gets the STORED record and next = interval + now; then an effectively dead object (+ 0x458 bit 0)
//     drops the entry, else an end != 0 with now >= end drops it (and the drawable told). The drops run after the walk (RW 0x820EE0, erase by key).
//   * The drawable notices (RW 0x820A2B on a replacement, RW 0x820A57 on a drop): a POISON (0x1A) record sets / clears bit 2 of Drawable + 0x118 (RW 0x671F62
//     / 0x671F73), the poisoned look. A first record does NOT set it (only a replacement does: retail order).
// DONOR (Open-BFME-2, BFME2 1.06, matched): DOTManager::update 0x0043B73D and the add 0x0043B8C0 have the same structure (Object + 0x438 there).
// NOT PORTED (stop S-1959, reported): the drawable notices (client), the record's save / load (no saved games here).

#pragma once

#include "GameLogic/Damage.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <string>

class GameLogic;
class StateHasher;

class DOTManager
{
public:
	// RW 0x8209BB: a DamageInfo and the three frames
	struct Record
	{
		DamageInfo info;              // + 0x00 (0x7C bytes in RW)
		std::uint32_t interval = 0;   // + 0x7C
		std::uint32_t endFrame = 0;   // + 0x80
		std::uint32_t nextFrame = 0;  // + 0x84
	};

	explicit DOTManager(GameLogic &logic);

	// RW 0x821073
	void add(ObjectID victim, const Record &record);
	// RW 0x820EF0
	void update();
	void reset();
	void crc(StateHasher &h) const;
	bool empty() const { return m_entries.empty(); }
	const std::map<ObjectID, Record> &entries() const { return m_entries; }
	// RW 0x8208D4: the incoming record is stronger than the current one at `now`
	static bool stronger(const Record &current, const Record &incoming, std::uint32_t now);
	static const char *stopLine();

	struct Counters
	{
		std::uint64_t added = 0;
		std::uint64_t replaced = 0;
		std::uint64_t kept = 0;
		std::uint64_t ticks = 0;
		std::uint64_t expired = 0;
	};
	const Counters &counters() const { return m_counters; }

private:
	GameLogic &m_logic;
	std::map<ObjectID, Record> m_entries; // RW + 4, ascending id
	Counters m_counters;
};
