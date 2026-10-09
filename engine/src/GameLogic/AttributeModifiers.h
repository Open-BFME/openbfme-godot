// OpenBFME. GPL-3.0.
//
// ModifierListTemplate / AttributeModifierStore (lane XP-1): the ModifierList blocks of data\ini\attributemodifier.ini (and the Create-A-Hero include),
// TheAttributeModifierStore of RotWK (RW 0xDE3C14). The objects' pools that apply them are GameLogic/Object/AttributeModifierPool.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the block parser is RW 0x6149AB (block table RW 0xD9EB8C): the name is the next token, looked up by name key (RW 0x614470, the index of the list in
//     the store's vector, -1 when unknown). A new name makes a 0xD4 byte template (RW 0x6148BE), parses it and appends it (RW 0x614829). An existing name
//     with load type 5 is parsed again into the existing template (RW 0x61490D first); with any other load type the block is parsed into a stack
//     template and THROWN AWAY (the first definition wins; the later block's errors still throw);
//   * the template: +0x00 the modifier vector (entries of 0x14 bytes: type, value, the name list), +0x0C Category, +0x10 the name, +0x14 its name key,
//     +0x18 Duration, +0x1C ModelCondition, +0x68 ClearModelCondition, +0xB4 / +0xB8 / +0xBC FX / FX2 / FX3, +0xC0 / +0xC4 / +0xC8 EndFX / 2 / 3,
//     +0xCC the Upgrade record { UpgradeTemplate *, delay frames }, +0xD0 MultiLevelFX, +0xD2 ReplaceInCategoryIfLongest, +0xD3 IgnoreIfAnticategoryActive;
//   * the field table RW 0xC4EAF0 (15 rows): Modifier RW 0x806264, Duration parseDurationUnsignedInt (RW 0x73A429), ModelCondition / ClearModelCondition
//     RW 0x4B8C21, Category RW 0x804DEA, FX* / EndFX* RW 0x73A302 (FXList names), Upgrade RW 0x8050D3, the three bools parseBool (RW 0x42E558);
//   * Modifier (RW 0x806264): the type name (RW 0x804CAD over RW 0xDA6D28, case-sensitive strcmp, 28 names, ATTRIBUTE_NONE = 0; 0 is INIException 3
//     "Attribute '%s' not found" RW 0xC4EA38), the value (parsePercentToReal when the token contains '%', else scanReal), then every remaining token
//     appended to a name list (parseAsciiStringVectorAppend RW 0x42E59E: the damage type names INVULNERABLE / ARMOR filter on). RW 0x8061D0 adds it: a
//     type the list already has gets the new value (and the new names when any were given), else a new entry is appended;
//   * Category (RW 0x804DEA): _stricmp over RW 0xD9FA40 (15 names); an index that is not in [1, 15) is INIException 3 "Invalid Category encountered by
//     ModifierList::Category" (RW 0xC4E9C0);
//   * Upgrade (RW 0x8050D3): the upgrade by name through TheUpgradeCenter (null when unknown), then optionally `Delay <ms>`: scanInt, unsigned, times
//     0.005f, ceil, kept as 16 bits;
//   * the value query (RW 0x6144BF -> 0x805268): the FIRST entry of the type; when a name is asked and the entry has a name list, the name must be in it
//     (case-sensitive compare), else the list does not answer.

#pragma once

#include "Common/INI.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class UpgradeTemplate;

// RW 0xDA6D28
enum AttributeModifierType
{
	ATTRIBUTE_NONE = 0,
	ATTRIBUTE_ARMOR = 1,
	ATTRIBUTE_DAMAGE_ADD = 2,
	ATTRIBUTE_DAMAGE_MULT = 3,
	ATTRIBUTE_EXPERIENCE = 6,
	ATTRIBUTE_SPELL_DAMAGE = 11,
	ATTRIBUTE_PRODUCTION = 13,
	ATTRIBUTE_HEALTH = 14,
	ATTRIBUTE_HEALTH_MULT = 15,
	ATTRIBUTE_AUTO_HEAL = 19,
	ATTRIBUTE_INVULNERABLE = 27,
	ATTRIBUTE_TYPE_COUNT = 28
};
extern const char *const TheAttributeModifierTypeNames[]; ///< RW 0xDA6D28, 28 names
extern const char *const TheAntiCategoryNames[]; ///< RW 0xD9FA40, the 15 category names (GameLogic/WeaponNames.cpp)
enum
{
	ATTRIBUTE_CATEGORY_LEVEL = 6,
	ATTRIBUTE_CATEGORY_COUNT = 15
};

struct ModifierListTemplate
{
	struct Modifier
	{
		int type = ATTRIBUTE_NONE;
		float value = 0.0f;
		std::vector<std::string> names;
	};
	struct UpgradeGrant
	{
		const UpgradeTemplate *upgrade = nullptr;
		unsigned delayFrames = 0;
	};

	std::vector<Modifier> m_modifiers;                        // +0x00
	int m_category = 0;                                       // +0x0C
	std::string m_name;                                       // +0x10
	unsigned m_duration = 0;                                  // +0x18 (frames)
	std::array<std::uint32_t, 19> m_modelCondition{};         // +0x1C
	std::array<std::uint32_t, 19> m_clearModelCondition{};    // +0x68
	std::string m_fx[3];                                      // +0xB4
	std::string m_endFx[3];                                   // +0xC0
	std::unique_ptr<UpgradeGrant> m_upgrade;                  // +0xCC
	bool m_multiLevelFX = false;                              // +0xD0
	bool m_replaceInCategoryIfLongest = false;                // +0xD2
	bool m_ignoreIfAnticategoryActive = false;                // +0xD3

	// RW 0x805268: the value of `type` (first entry); `name` filters on the entry's name list when both are present
	bool value(int type, const char *name, float &out) const;
	static const FieldParse *getFieldParse(); ///< RW 0xC4EAF0
};

class AttributeModifierStore
{
public:
	AttributeModifierStore();
	~AttributeModifierStore();
	AttributeModifierStore(const AttributeModifierStore &) = delete;
	AttributeModifierStore &operator=(const AttributeModifierStore &) = delete;

	void registerBlock(INIBlockRegistry &registry);
	void parseModifierList(INI *ini); ///< RW 0x6149AB

	int findIndex(const std::string &name) const; ///< RW 0x614470: -1 when unknown
	const ModifierListTemplate *get(int index) const; ///< RW 0x6146E0
	const ModifierListTemplate *find(const std::string &name) const { return get(findIndex(name)); }
	size_t size() const { return m_lists.size(); }

private:
	std::vector<std::unique_ptr<ModifierListTemplate>> m_lists;
	std::map<std::string, int> m_index;
};

// the store the INI block and the pools use (RetailObjectWorld installs its own through a GlobalOwnerChain); nullptr when none
extern thread_local AttributeModifierStore *TheAttributeModifierStore;
