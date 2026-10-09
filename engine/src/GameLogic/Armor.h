// OpenBFME. GPL-3.0.
//
// Armor data: the Armor INI block (ArmorTemplate), ArmorStore and the DamageType coefficient lookup. Port of ZH GameEngine/Include/
// GameLogic/Armor.h as changed by RotWK. Lane WEAPON-1. The damage arithmetic that applies armour is GameLogic/Damage.h
// (AdjustDamage).
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address)
//   * The `Armor` block parse function is RW 0x5D8D1F (node RW 0xD9DB1C), store TheArmorStore RW 0xDE3604. Steps: name =
//     getNextToken; key = nameToKey(name) (case-sensitive strcmp); look up the map. NOT FOUND: new ArmorTemplate (0x80 bytes, ctor
//     RW 0x5D8777 -> clear RW 0x5D86AC, name set), map[key] = it, initFromINI(it, table RW 0xBEFD98). FOUND with load type 5: the
//     existing template gets flag +0x78 = 1, a new template (flag 0) is pushed on the store's override list and parsed into;
//     the map entry is NOT replaced. FOUND with any other load type (every normal load, map.ini's type 2 included): the block is
//     parsed into a throw-away stack template: the FIRST definition WINS, silently, but a syntax error in the duplicate still fires.
//     (Zero Hour: the LAST definition wins; BFME2 1.06 RW-equivalent 0x5D9484 has the same first-wins structure as RotWK.)
//   * ArmorTemplate (0x80 bytes): +0x00 FlankedPenalty (0.0f), +0x04 coefficient[28] (1.0f each, indexed by DamageType), +0x74
//     DamageScalar (1.0f), +0x78 flag (-1; 1 = has an override, 0 = is an override), +0x7C name. BFME2 1.06 had 27 coefficients
//     and no FlankedPenalty / DamageScalar (open-bfme-2 ArmorTemplateParse.cpp: 0x7C bytes); ZH has 38.
//   * Table RW 0xBEFD98 (3 rows): DamageScalar (RW 0x5D875A: scanPercentToReal(getNextToken) into +0x74), FlankedPenalty (parsePercentToReal
//     into +0x00), Armor (RW 0x5D86F1). `Armor = <DamageType|Default> <percent>`: the percent is parsed BEFORE the first token is
//     interpreted (a bad percent errors first); `Default` (stricmp) fills all 28 coefficients; anything else is scanIndexList over
//     the damage type names (RW 0xD9DA08 = 0xDA15A8) and sets one coefficient. Later lines overwrite earlier ones.
//   * percent = float32(float32(text) * 0.01f) with one rounding: "120%" is 0x3F999999, not 1.2f (0x3F99999A).
//   * findArmorTemplate (RW 0x5D88B6): nameToKey then map find; null for an unknown or empty name, no error. isOverridden (RW
//     0x5D891E): flag > 0.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Damage.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class ArmorTemplate
{
public:
	ArmorTemplate(); // RW 0x5D8777
	void clear();    // RW 0x5D86AC

	// Zero Hour's ArmorTemplate::adjustDamage is NOT the RotWK function: see AdjustDamage in GameLogic/Damage.h.
	float getCoefficient(int damageType) const { return m_damageCoefficient[damageType]; }

	static const FieldParse *getFieldParse(); // RW 0xBEFD98
	static void parseDamageScalar(INI *ini, void *instance, void *store, const void *userData);      // RW 0x5D875A
	static void parseArmorCoefficients(INI *ini, void *instance, void *store, const void *userData); // RW 0x5D86F1

	float m_flankedPenalty = 0.0f;                      // +0x00 (percent as a fraction)
	float m_damageCoefficient[DAMAGE_NUM_TYPES];        // +0x04
	float m_damageScalar = 1.0f;                        // +0x74
	int m_flag = -1;                                    // +0x78
	std::string m_name;                                 // +0x7C
};

class ArmorStore
{
public:
	// RW 0x5D88B6: nullptr when absent
	const ArmorTemplate *findArmorTemplate(const std::string &name) const;
	// RW 0x5D891E
	bool isOverridden(const std::string &name) const;

	// RW 0x5D8D1F. Reads the rest of one `Armor <name> ... End` block (header already read).
	void parseArmorDefinition(INI *ini);
	static void parseArmorDefinitionGlobal(INI *ini); // throws code 3 when TheArmorStore is null

	size_t size() const { return m_templates.size(); }
	size_t overrideCount() const { return m_overrides.size(); }
	std::vector<std::string> names() const; ///< lexical order (tests)

private:
	std::map<std::string, std::shared_ptr<ArmorTemplate>> m_templates;
	std::vector<std::shared_ptr<ArmorTemplate>> m_overrides; ///< RW store +0x20
};

extern thread_local ArmorStore *TheArmorStore;
