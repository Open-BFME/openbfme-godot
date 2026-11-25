// OpenBFME. GPL-3.0.
//
// TheLightPointSystem (PORT-113): the light point levels of the War of the Ring and the special power test they feed. TARGET FACTS
// (RotWK 2.01 game.dat):
//   * TheLightPointSystem is RW 0xDE8C04, vtable RW 0xC5324C, the levels (pointers) at + 0xC. The engine start creates it with no condition
//     (RW 0x63BB7B .. 0x63BBB9, constructor RW 0x830B85) and nothing clears the global afterwards: it exists in a skirmish too. Its INI comes from the
//     subsystem legend (the initSubsystem call passes no path), after TheSpecialPowerStore's (Common/System/SubsystemInitOrder.cpp).
//   * slot 1 RW 0x5FFC33 (init) empties the level list; slot 9 RW 0x830A13 (reset) deletes every level's overrides (RW 0x5E2E2E); slot 10 is a bare ret.
//   * LightPointLevel is one `LightPointLevel` block: size 0x24, constructor RW 0x830C27, field table RW 0xC532C0. + 4 the next override, + 8 the
//     override flag, + 0xC an int the constructor sets to -1 (no reader found), + 0x10 the block's name, + 0x14 `Name` (a GameText label, RW 0x73B192),
//     + 0x18 `SpecialAbilities` (RW 0x73B4FF: the ids of the named special powers; a name TheSpecialPowerStore does not know is skipped).
//   * the block parser is RW 0x830EB5; load type 2 (map.ini) makes an override of a level that exists and throws for one that does not.
#pragma once
#include "Common/LightPointTracker.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class INI;
class SpecialPowerTemplate;

class LightPointLevel
{
public:
	// RW 0x465AF4
	LightPointLevel *getFinalOverride() { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }
	const LightPointLevel *getFinalOverride() const { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }
	const std::string &getName() const { return m_name; }
	const std::vector<unsigned> &getSpecialAbilities() const { return m_specialAbilities; }

	LightPointLevel *m_nextOverride = nullptr; // + 0x04
	bool m_isOverride = false;                 // + 0x08
	std::string m_name;                        // + 0x10
	std::string m_displayName;                 // + 0x14 `Name`: the label as written (retail stores the GameText string and throws for an unknown label, S-520)
	std::vector<unsigned> m_specialAbilities;  // + 0x18 `SpecialAbilities`: special power ids (SpecialPowerTemplate + 0x14)
};

class LightPointSystem
{
public:
	LightPointSystem() = default;
	~LightPointSystem();
	LightPointSystem(const LightPointSystem &) = delete;
	LightPointSystem &operator=(const LightPointSystem &) = delete;

	void init();  // slot 1 RW 0x5FFC33: the levels go
	void reset(); // slot 9 RW 0x830A13: every level's overrides are deleted (RW 0x5E2E2E), the levels stay

	// RW 0x830EB5, the `LightPointLevel` block
	void parseLightPointLevelDefinition(INI *ini);
	// RW 0x830EC2: without a system the function returns before it reads anything
	static void parseLightPointLevelDefinitionGlobal(INI *ini);

	// RW 0x830A3B: the index of the first level whose own SpecialAbilities list (the level as stored, not its override) holds the id of the power's
	// final override; -1 when no level names it
	int findLevelIndexOfSpecialPower(const SpecialPowerTemplate *t) const;
	// RW 0x830AE3: a power no level names passes; a named one passes when the tracker's points reach its LightPointCost (final override + 0x30).
	// The tracker's levels are not read.
	bool canUseSpecialPower(const LightPointTracker &tracker, const SpecialPowerTemplate *t) const;
	// RW 0x830E63 / 0x830BD5: the index of the first level whose final override has this name (case-sensitive) is added to / removed from the tracker;
	// no such level: nothing
	void giveLevel(LightPointTracker &tracker, const std::string &name) const;
	void removeLevel(LightPointTracker &tracker, const std::string &name) const;

	size_t getNumLevels() const { return m_levels.size(); }
	// the level as stored (its overrides are reached through getFinalOverride); null past the end
	const LightPointLevel *getLevelByIndex(size_t i) const { return i < m_levels.size() ? m_levels[i] : nullptr; }

private:
	LightPointLevel *findLevel(const std::string &name) const; // RW 0x830BA6: the stored level with this name (memcmp, then the lengths)
	int findLevelIndexByFinalName(const std::string &name) const;
	void addLevel(LightPointLevel *level);                     // RW 0x830E06: throws for a name that exists
	LightPointLevel *newLevel();

	std::vector<std::unique_ptr<LightPointLevel>> m_owned;
	std::vector<LightPointLevel *> m_levels; ///< system + 0xC
};

extern thread_local LightPointSystem *TheLightPointSystem; // RW 0xDE8C04
