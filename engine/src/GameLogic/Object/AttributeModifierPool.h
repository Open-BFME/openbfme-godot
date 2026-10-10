// OpenBFME. GPL-3.0.
//
// AttributeModifierPoolUpdate (lane XP-1): the module every RotWK object carries (data\ini\default\object.ini gives it to every template) that holds the
// ModifierLists active on the object and answers the attribute queries of damage, armour, experience, ... (Object::attributeModifierSum / Product).
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the module is found by its class name key "AttributeModifierPoolUpdate" (RW 0x68C4A6 -> 0x68BDA5); constructor RW 0x8057B0: the entry vector (+0x20),
//     the 15 category "disabled until" frames (+0x30) and the 15 category counts (+0x6C) all 0, the next wake frame (+0x2C) 0x3FFFFFFF;
//   * an entry is 0x10 bytes: the list's store index, its name, the expiry frame (+8), ...;
//   * suppressed (RW 0x804D27, frame, entry, innate): a category in [10, 14] (the INNATE_* ones) when innate is false; else a category in [0, 15) whose
//     "disabled until" frame is >= the current frame (so at frame 0 every entry is suppressed);
//   * sum (RW 0x804F39, type, out, name): out = 0.0f; over the entries in order, every one with frame < expiry, not suppressed (innate true), whose list
//     answers the type (RW 0x6144BF -> ModifierListTemplate::value) adds its value (SSE single); true when one answered;
//   * product (RW 0x804FFF, type, out, name, innate): the same with out = 1.0f and a multiplication;
//   * add (RW 0x805A8E, name, duration): the list (unknown: false); IgnoreIfAnticategoryActive and frame < the category's disabled frame: false; the duration is
//     the argument, or the list's Duration when the argument is negative; 0 (or less) never expires (0x3FFFFFFF), else frame + duration;
//     ReplaceInCategoryIfLongest: another list of the same category active with an expiry >= the new one refuses the add (false), shorter ones are removed
//     first (RW 0x8052FB); an entry of the same list only gets the new expiry (and its FX); a new entry: the list's ModelCondition flags are set and its
//     ClearModelCondition flags cleared (RW 0x5E3BA5 / 0x5E3B79), the list's Upgrade is granted now when its delay is 0 (Object::giveUpgrade RW 0x69388B),
//     HEALTH > 0: body.setMaxHealth(max + HEALTH_MULT product * HEALTH (when the pool has a HEALTH_MULT) or max + HEALTH, 1) (RW 0x805D3A), HEALTH_MULT > 0:
//     body.setMaxHealth(max * HEALTH_MULT, 1) (RW 0x805DB5), then the entry is appended, the category count grows, and SHROUD_CLEARING (type 0x14) > 0 marks
//     the object's shroud record dirty (RW 0x68C213 -> 0xB4E2A0; lane DECOMP-1 r3, earlier read as an auto heal refresh).
// NOT PORTED (stop S-633, counted): the expiry update that removes entries (their HEALTH / ClearModelCondition reversal, EndFX, the category counts; the
// queries' own frame < expiry test already ignores an expired entry), the delayed Upgrade grant, the FX / EndFX of the client and the xfer.

#pragma once

#include "GameLogic/Module/BehaviorModule.h"

#include <array>
#include <string>
#include <vector>

class ModuleFactory;

class AttributeModifierPool : public BehaviorModule
{
public:
	AttributeModifierPool(Thing *thing, const ModuleData *data);
	static void registerClass(ModuleFactory &modules);

	bool add(const std::string &listName, int duration); ///< RW 0x805A8E
	bool sum(int type, const char *name, float &out) const; ///< RW 0x804F39
	bool product(int type, const char *name, bool innate, float &out) const; ///< RW 0x804FFF
	bool hasList(const std::string &listName) const;
	// RW 0x8052FB (Object::removeAttributeModifier RW 0x68F259, lane INTEG-1): the list's entry goes; its removal side effects are not ported (S-633)
	void remove(const std::string &listName) { removeByName(listName); }
	// RW 0x804FCC (lane SPELL-2): every category whose bit is set in `mask` (15 categories) is disabled until `frame` (the suppressed test of RW 0x804D27)
	void disableCategories(unsigned mask, unsigned frame)
	{
		for (unsigned c = 0; c < 15; ++c)
		{
			if ((mask >> c) & 1u)
			{
				m_categoryDisabled[c] = frame;
			}
		}
	}
	unsigned categoryDisabledUntil(int category) const { return (category >= 0 && category < 15) ? m_categoryDisabled[(size_t)category] : 0u; }
	size_t entryCount() const { return m_entries.size(); }
	// lane HERO-1: the names of the lists the pool holds, in entry order (the hero viewer's effect label)
	std::vector<std::string> listNames() const
	{
		std::vector<std::string> out;
		for (const Entry &e : m_entries)
		{
			out.push_back(e.name);
		}
		return out;
	}
	int categoryCount(int category) const { return (category >= 0 && category < 15) ? m_categoryCount[(size_t)category] : 0; }
	void crc(StateHasher &hasher) const override;

	struct Stats
	{
		unsigned long long delayedUpgrades = 0;   ///< Upgrade with a delay: not granted (S-633)
		unsigned long long fxNotShown = 0;        ///< FX of an add or refresh (client)
		unsigned long long expiringAdds = 0;      ///< entries that will expire without the removal side effects
	};
	static Stats &stats();
	static std::vector<std::string> stopLines();

private:
	friend struct XpTestAccess; // lane XP-1 tests: isolated state hash mutations
	struct Entry
	{
		int index = -1;
		std::string name;
		unsigned expire = 0;
	};
	bool suppressed(unsigned frame, const Entry &e, bool innate) const; ///< RW 0x804D27
	void removeByName(const std::string &listName);                   ///< RW 0x8052FB (the entry only: S-633)

	std::vector<Entry> m_entries;                      ///< +0x20
	unsigned m_nextWake = 0x3FFFFFFF;                  ///< +0x2C
	std::array<unsigned, 15> m_categoryDisabled{};     ///< +0x30
	std::array<int, 15> m_categoryCount{};             ///< +0x6C
};
