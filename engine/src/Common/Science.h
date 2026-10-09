// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ScienceInfo / ScienceStore (ZH Include/Common/Science.h, Source/Common/RTS/Science.cpp): the `Science <Name>` INI block and the
// queries the spell book asks of it. Lane SPELL-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * TheScienceStore is RW 0xDE3B20; the store's science vector is store + 0xC (begin) / + 0x10 (end), the replaced list store + 0x18.
//   * ScienceInfo is 0x34 bytes (constructor RW 0x5FF6D9): +0x00 vtable, +0x04 next override, +0x08 isOverride (byte), +0x0C replace flag
//     (-1; 1 = replaced by a load type 5 block, 0 = the replacement), +0x10 the science (its name key), +0x14 DisplayName, +0x18 Description,
//     +0x1C PrerequisiteSciences (a vector of groups, each a vector of sciences: 0xC bytes per group), +0x28 SciencePurchasePointCost (0),
//     +0x2C SciencePurchasePointCostMP (0), +0x30 IsGrantable (TRUE by default, RW 0x5FF70C).
//   * the block parser RW 0x5FF7DA: name = getNextToken; key = nameToKey(name) (TheNameKeyGenerator RW 0xDD90E4); the store's vector is searched
//     for an entry whose +0x10 equals the key (NOT through the overrides). Then by the INI's load type:
//       2 (map.ini): a new info; found: it copies the final override of the found one (RW 0x5FF789), the found one's final override links it
//          (+4) and it is marked an override; not found: it is marked an override and appended;
//       found and 5: the found one goes to the replaced list with flag 1 and leaves the vector (RW 0x5FF2DC), a new info (flag 0) is appended;
//       found and anything else: INIException(3, "duplicate science %s!\n") (RW 0xBF87F8);
//       not found: a new info is appended.
//     The fields are parsed with table RW 0xBF8788 (below), then +0x10 = key.
//   * PrerequisiteSciences (RW 0x73BCBF): the group vector is resized to ONE group (RW 0x73BC98; an override keeps its first copied group); then
//     per token: "None" (stricmp) empties the last group and ends the line; "OR" (stricmp) ends the line when the last group is empty, else
//     appends an empty group; anything else is scanScience (RW 0x73A386 -> 0x5FEEC7: unknown names throw INIException(3, "Science name %s not
//     known! (Did you define it in Science.ini?)")) appended to the last group. At the end, a last group that is empty empties the WHOLE vector
//     (RW 0x73BD7B): "None", a trailing "OR" and "A OR OR B" all leave no prerequisite at all.
//   * findScienceInfo (RW 0x5FEC35): the first entry of the vector whose FINAL OVERRIDE's +0x10 is the science; the final override is returned.
//   * getSciencePurchaseCost (RW 0x5FEC64): +0x2C (the MP cost) when the game is a multiplayer game (RW 0x441B7C), a skirmish (GameLogic + 0x110
//     == 2) or a replay (== 3) of a multiplayer game (RW 0x77D627); +0x28 otherwise; 0 for an unknown science.
//   * isScienceGrantable (RW 0x5FECBA): +0x30; false for an unknown science.
//   * playerHasPrereqsForScience (RW 0x5FED05): unknown science: false; NO group: TRUE (a difference from the BFME1 decompile, which returns
//     false); else true when every science of some group is owned (groups in order, sciences in order, the first missing one ends the group).
//   * playerHasRootPrereqsAndCanPurchase (RW 0x5FED5B): prerequisites AND cost <= the player's purchase points (no cost == 0 test here; the
//     player's purchase test RW 0x6AC8BC refuses cost 0).
// DONOR: BFME1 Common/RTS/Science.cpp (field names, the group layout of the prerequisites); ZH Science.cpp (overrides).

#pragma once

#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

typedef int ScienceType; ///< the science's name key (ZH ScienceType)
enum : int
{
	SCIENCE_INVALID = -1
};
typedef std::vector<ScienceType> ScienceVec;

class ScienceInfo
{
public:
	ScienceInfo *getFinalOverride() { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }
	const ScienceInfo *getFinalOverride() const { return m_nextOverride ? m_nextOverride->getFinalOverride() : this; }

	ScienceInfo *m_nextOverride = nullptr;          // +0x04 (owned by the store)
	bool m_isOverride = false;                      // +0x08
	int m_replaceFlag = -1;                         // +0x0C
	ScienceType m_science = SCIENCE_INVALID;        // +0x10 (RW initialises -1)
	std::string m_name;                             // +0x14 DisplayName label (GameText not looked up: S-520)
	std::string m_description;                      // +0x18 Description label (S-520)
	std::vector<ScienceVec> m_prereqSciences;       // +0x1C OR of AND groups
	int m_sciencePurchasePointCost = 0;             // +0x28
	int m_sciencePurchasePointCostMP = 0;           // +0x2C
	bool m_grantable = true;                        // +0x30
};

// What the store asks of a player (RW 0x5FED05 / 0x5FED5B call it through the interface at Player + 4: vslot 0 hasScience, vslot 1 the purchase points)
class ScienceOwner
{
public:
	virtual ~ScienceOwner() {}
	virtual bool hasScience(ScienceType st) const = 0;
	virtual int getSciencePurchasePoints() const = 0;
};

class ScienceStore
{
public:
	explicit ScienceStore(NameKeyGenerator &keys);
	~ScienceStore();
	ScienceStore(const ScienceStore &) = delete;
	ScienceStore &operator=(const ScienceStore &) = delete;

	// RW 0x5FF7DA: the rest of one `Science <name> ... End` block (header keyword already read)
	void parseScienceDefinition(INI *ini);
	static void parseScienceDefinitionGlobal(INI *ini); // nothing when TheScienceStore is null (RW 0x5FF805)
	// drops the load type 2 overrides (ZH ScienceStore::reset): between maps
	void resetOverrides();

	const ScienceInfo *findScienceInfo(ScienceType st) const;    // RW 0x5FEC35
	bool isValidScience(ScienceType st) const { return findScienceInfo(st) != nullptr; }
	// RW 0x5FEC64; `multiplayerCosts` is RW's game mode test (multiplayer, skirmish, multiplayer replay)
	int getSciencePurchaseCost(ScienceType st, bool multiplayerCosts) const;
	bool isScienceGrantable(ScienceType st) const;                // RW 0x5FECBA
	bool playerHasPrereqsForScience(const ScienceOwner &player, ScienceType st) const;                        // RW 0x5FED05
	bool playerHasRootPrereqsAndCanPurchase(const ScienceOwner &player, ScienceType st, bool multiplayerCosts) const; // RW 0x5FED5B

	// RW 0x5FEEC7 (INI::scanScience): the key of a defined science; INIException(3, ...) otherwise
	ScienceType friend_lookupScience(const char *name) const;
	// the key of a name WITHOUT creating it (SCIENCE_INVALID when the name has no key or no science): runtime lookups (scripts, tests)
	ScienceType getScienceFromInternalName(const std::string &name) const;
	std::string getInternalNameForScience(ScienceType st) const;

	size_t size() const { return m_sciences.size(); }
	size_t replacedCount() const { return m_replaced.size(); }
	// the sciences in definition order (the vector at store + 0xC; final overrides)
	std::vector<const ScienceInfo *> sciences() const;

private:
	ScienceInfo *newInfo();
	NameKeyGenerator &m_keys;
	std::vector<std::unique_ptr<ScienceInfo>> m_owned; ///< every info ever made (the vector and the override links point into it)
	std::vector<ScienceInfo *> m_sciences;             ///< store + 0xC
	std::vector<ScienceInfo *> m_replaced;             ///< store + 0x18
};

extern thread_local ScienceStore *TheScienceStore;

namespace ScienceParse
{
// RW 0x73B4A0 (INI::parseScienceVector): clears the ScienceVec at `store`; "None" (stricmp) clears it again and ends the line; every other token
// is scanScience (unknown: INIException 3). Needs TheScienceStore: without one it throws INIException(3, "TheScienceStore==NULL").
void parseScienceVector(INI *ini, void *instance, void *store, const void *userData);
// RW 0x73BCBF: PrerequisiteSciences into a std::vector<ScienceVec> (see the file comment)
void parsePrerequisiteSciences(INI *ini, void *instance, void *store, const void *userData);
} // namespace ScienceParse
