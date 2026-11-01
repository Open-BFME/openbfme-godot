// OpenBFME. GPL-3.0.
// See GameLogic/LivingWorld/LivingWorldAutoResolveData.h.  Lane WOTR-2.

#include "GameLogic/LivingWorld/LivingWorldAutoResolveData.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace LivingWorld
{

namespace
{
const char *const kUnitTypeNames[] = { "AutoResolveUnit_Soldier", "AutoResolveUnit_Archer", "AutoResolveUnit_Pikemen", "AutoResolveUnit_Cavalry",
	"AutoResolveUnit_Monster", "AutoResolveUnit_Hero", "AutoResolveUnit_Fortress", "AutoResolveUnit_Siege", "AutoResolveUnit_Support",
	"AutoResolveUnit_INVALID", nullptr }; // RW 0xDADBE4

int scanUnitType(const char *token)
{
	return INI::scanIndexList(token, kUnitTypeNames); // RW 0x42B999: an unknown name throws
}

// the entry of the highest key <= k (RW 0x73D1FA lower_bound, then one step back unless the key matches), nullptr when none
template <class M>
const typename M::mapped_type *floorEntry(const M &m, int k)
{
	auto it = m.upper_bound(k);
	if (it == m.begin())
	{
		return nullptr;
	}
	--it;
	return &it->second;
}

// ---- Weapon (RW 0xC51FE8) ----
void parseDamagePerRound(INI *ini, void *, void *store, const void *)
{
	AutoResolveWeapon *w = static_cast<AutoResolveWeapon *>(store);
	const float damage = ini->scanReal(ini->getNextSubToken("Damage"));
	const int type = scanUnitType(ini->getNextSubToken("Against"));
	w->damage[type] = damage;
}

void parseLevelBonus(INI *ini, void *, void *store, const void *)
{
	AutoResolveWeapon *w = static_cast<AutoResolveWeapon *>(store);
	const int level = ini->scanInt(ini->getNextSubToken("Level"));
	const float bonus = ini->scanPercentToReal(ini->getNextSubToken("Bonus"));
	if (!w->levelBonus.insert(std::make_pair(level, bonus)).second)
	{
		throw INIException(1, "Duplicate LevelBonus entries for level %d", level); // RW 0xC51F70
	}
}

#define WP(m) (int)offsetof(AutoResolveWeapon, m)
const FieldParse kWeaponFields[] = {
	{ "DamagePerRound", parseDamagePerRound, nullptr, 0 },
	{ "LevelBonus", parseLevelBonus, nullptr, 0 },
	{ "ReduceAttackWhenHurt", INI::parseBool, nullptr, WP(reduceAttackWhenHurt) },
	{ "MissPercentChance", INI::parseInt, nullptr, WP(missPercentChance) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef WP

// ---- Armor (RW 0xC53A94) ----
void parseArmorRow(INI *ini, void *, void *store, const void *)
{
	AutoResolveArmor *a = static_cast<AutoResolveArmor *>(store);
	const char *typeToken = ini->getNextToken();
	const std::string typeName = typeToken;
	const float value = ini->scanPercentToReal(ini->getNextToken());
	a->scalar[scanUnitType(typeName.c_str())] = value;
}
const FieldParse kArmorFields[] = {
	{ "Armor", parseArmorRow, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// ---- Body (RW 0xC51DD0) ----
void parseHitpointsAtLevel(INI *ini, void *, void *store, const void *)
{
	AutoResolveBody *b = static_cast<AutoResolveBody *>(store);
	const float hp = ini->scanReal(ini->getNextSubToken("Hitpoints"));
	const int level = ini->scanInt(ini->getNextSubToken("Level"));
	if (!b->hitpoints.insert(std::make_pair(level, hp)).second)
	{
		throw INIException(1, "Duplicate hitpoint entries for level %d", level); // RW 0xC51D08
	}
}

void parseRegenerateAtLevel(INI *ini, void *, void *store, const void *)
{
	AutoResolveBody *b = static_cast<AutoResolveBody *>(store);
	const float regen = ini->scanReal(ini->getNextSubToken("Regenerate"));
	const int level = ini->scanInt(ini->getNextSubToken("Level"));
	if (!b->regenerate.insert(std::make_pair(level, regen)).second)
	{
		throw INIException(1, "Duplicate regenerate rate entries for level %d", level); // RW 0xC51D44
	}
}

#define BP(m) (int)offsetof(AutoResolveBody, m)
const FieldParse kBodyFields[] = {
	{ "HitpointsAtLevel", parseHitpointsAtLevel, nullptr, 0 },
	{ "RegenerateAtLevel", parseRegenerateAtLevel, nullptr, 0 },
	{ "CanBeAttacked", INI::parseBool, nullptr, BP(canBeAttacked) },
	{ "LeaveInArmySummary", INI::parseBool, nullptr, BP(leaveInArmySummary) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BP

// ---- CombatChain (RW 0xC51EC8) ----
bool targetLess(const AutoResolveCombatChain::Target &a, const AutoResolveCombatChain::Target &b) // RW 0x8294B7
{
	if (a.priority != b.priority)
	{
		return a.priority < b.priority;
	}
	return a.type < b.type;
}

void parseChainTarget(INI *ini, void *, void *store, const void *)
{
	AutoResolveCombatChain *c = static_cast<AutoResolveCombatChain *>(store);
	const int type = scanUnitType(ini->getNextSubToken("Target"));
	const int priority = ini->scanInt(ini->getNextSubToken("Priority"));
	const std::uint32_t bit = 1u << type;
	if ((c->defaultTargets & bit) == 0)
	{
		throw INIException(3, "Cannot redefine attack priority for %s", kUnitTypeNames[type]); // RW 0xC51E94
	}
	if (priority != 0)
	{
		c->defaultTargets &= ~bit;
		AutoResolveCombatChain::Target t;
		t.type = type;
		t.priority = priority;
		auto at = std::lower_bound(c->targets.begin(), c->targets.end(), t, targetLess);
		c->targets.insert(at, t);
	}
}
const FieldParse kChainFields[] = {
	{ "Target", parseChainTarget, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// ---- Leadership (RW 0xC51C50, BonusForLevel RW 0xC51B78) ----
void parseAffects(INI *ini, void *, void *store, const void *)
{
	std::uint32_t *bits = static_cast<std::uint32_t *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		*bits |= 1u << scanUnitType(t);
	}
}

#define LB(m) (int)offsetof(AutoResolveLeadership::Bonus, m)
const FieldParse kLeadershipBonusFields[] = {
	{ "MinLevel", INI::parseInt, nullptr, LB(minLevel) },
	{ "MaximumUnitsAffected", INI::parseInt, nullptr, LB(maximumUnitsAffected) },
	{ "Priority", INI::parseInt, nullptr, LB(priority) },
	{ "WeaponMultiplier", INI::parsePercentToReal, nullptr, LB(weapon) },
	{ "ExperienceMultiplier", INI::parsePercentToReal, nullptr, LB(experience) },
	{ "ArmorMultiplier", INI::parsePercentToReal, nullptr, LB(armor) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LB

void parseBonusForLevel(INI *ini, void *, void *store, const void *)
{
	std::map<int, AutoResolveLeadership::Bonus> *bonuses = static_cast<std::map<int, AutoResolveLeadership::Bonus> *>(store);
	AutoResolveLeadership::Bonus b;
	ini->initFromINI(&b, kLeadershipBonusFields);
	if (!bonuses->insert(std::make_pair(b.minLevel, b)).second)
	{
		throw INIException(1, "Two BonusForLevel entries with the same MinLevel %d", b.minLevel); // RW 0xC51BE8
	}
}

#define LP(m) (int)offsetof(AutoResolveLeadership, m)
const FieldParse kLeadershipFields[] = {
	{ "Affects", parseAffects, nullptr, LP(affects) },
	{ "AffectsHigherLevelFirst", INI::parseBool, nullptr, LP(affectsHigherLevelFirst) },
	{ "BonusForLevel", parseBonusForLevel, nullptr, LP(bonuses) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LP

// ---- HandicapLevel (RW 0xC50EB0) ----
#define HP(m) (int)offsetof(AutoResolveHandicap, m)
const FieldParse kHandicapFields[] = {
	{ "GUIDisplayedLevel", INI::parseInt, nullptr, HP(guiDisplayedLevel) },
	{ "WeaponMultiplier", INI::parseReal, nullptr, HP(weapon) },
	{ "ArmorMultiplier", INI::parseReal, nullptr, HP(armor) },
	{ "ExperienceMultiplier", INI::parseReal, nullptr, HP(experience) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef HP

// ---- ReinforcementSchedule (RW 0x8244FD: the catch-all row) ----
void parseScheduleRow(INI *ini, void *instance, void *, const void *userData)
{
	AutoResolveReinforcementSchedule *s = static_cast<AutoResolveReinforcementSchedule *>(instance);
	const char *field = static_cast<const char *>(userData);
	const char *value = ini->getNextToken();
	std::string lower;
	for (const char *c = field; *c; ++c)
	{
		lower += (char)((*c >= 'A' && *c <= 'Z') ? *c - 'A' + 'a' : *c);
	}
	if (lower == "eachremaining")
	{
		s->eachRemaining = ini->scanInt(value);
		return;
	}
	if (field[0] == '#')
	{
		throw INIException(5, "Cannot use math operations on AutoResolveReinforcementSchedule army numbers"); // RW 0xC51240
	}
	int n = 0;
	if (std::sscanf(field, "%d", &n) != 1)
	{
		throw INIException(5, "Unknown field '%s' in block AutoResolveReinforcementSchedule", field); // RW 0xC511A0
	}
	if (n <= 0)
	{
		throw INIException(5, "Unacceptable army index %d in AutoResolveReinforcementSchedule", n); // RW 0xC51150
	}
	const int round = ini->scanInt(value);
	if (!s->rounds.insert(std::make_pair(n - 1, round)).second)
	{
		throw INIException(3, "AutoResolveReinforcementSchedule has duplicate entries for army %d", n); // RW 0xC51108
	}
}
const FieldParse kScheduleFields[] = {
	{ nullptr, parseScheduleRow, nullptr, 0 }
};

// ---- bonus schedules (RW 0xC51400 / 0xC51040; Bonus RW 0xC513A0 / 0xC50FC0) ----
#define SB(m) (int)offsetof(AutoResolveBonusSchedule::Bonus, m)
const FieldParse kResourceBonusFields[] = {
	{ "MinResourceBonus", INI::parseInt, nullptr, SB(minimum) },
	{ "WeaponMultiplier", INI::parseReal, nullptr, SB(weapon) },
	{ "ArmorMultiplier", INI::parseReal, nullptr, SB(armor) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kScienceBonusFields[] = {
	{ "MinSciencePurchasePointsForBonus", INI::parseInt, nullptr, SB(minimum) },
	{ "WeaponMultiplier", INI::parseReal, nullptr, SB(weapon) },
	{ "ArmorMultiplier", INI::parseReal, nullptr, SB(armor) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SB

void parseScheduleBonus(INI *ini, void *, void *store, const void *userData)
{
	std::map<int, AutoResolveBonusSchedule::Bonus> *bonuses = static_cast<std::map<int, AutoResolveBonusSchedule::Bonus> *>(store);
	AutoResolveBonusSchedule::Bonus b;
	ini->initFromINI(&b, static_cast<const FieldParse *>(userData));
	if (!bonuses->insert(std::make_pair(b.minimum, b)).second)
	{
		throw INIException(3, "Duplicate minimum %d in a LivingWorldAutoResolve bonus schedule", b.minimum); // RW 0xC51000
	}
}

#define SS(m) (int)offsetof(AutoResolveBonusSchedule, m)
const FieldParse kResourceScheduleFields[] = {
	{ "Sides", INI::parseAsciiStringVector, nullptr, SS(sides) },
	{ "Bonus", parseScheduleBonus, kResourceBonusFields, SS(bonuses) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kScienceScheduleFields[] = {
	{ "Sides", INI::parseAsciiStringVector, nullptr, SS(sides) },
	{ "Bonus", parseScheduleBonus, kScienceBonusFields, SS(bonuses) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SS

std::string blockName(INI *ini, const char *kind)
{
	const char *name = ini->getNextTokenOrNull();
	if (!name || !*name)
	{
		throw INIException(3, "%s block without a name", kind);
	}
	return name;
}

} // namespace

const char *const *autoResolveUnitTypeNames()
{
	return kUnitTypeNames;
}

// ---- templates ----
bool AutoResolveWeapon::canDamage() const
{
	if (missPercentChance >= 100) // RW 0x829DDF
	{
		return false;
	}
	for (int i = 0; i < AR_UNIT_TYPE_COUNT; ++i)
	{
		if (damage[i] > 0.0f)
		{
			return true;
		}
	}
	return false;
}

float AutoResolveWeapon::damageForRound(int level, float hp, float maxHp, int targetType) const
{
	if (targetType < 0 || targetType >= AR_UNIT_TYPE_COUNT || !canDamage())
	{
		return 0.0f;
	}
	float d = damage[targetType];
	if (reduceAttackWhenHurt)
	{
		d = SimMath::mulf32(SimMath::divf32(hp, maxHp), d); // RW 0x829E96 divss, mulss
	}
	const float *bonus = floorEntry(levelBonus, level);
	if (!bonus)
	{
		return d;
	}
	return NumericState::pc24Mul(NumericState::pc24Add(*bonus, 1.0f), d); // RW 0x829ED6 fld, fadd qword 1.0, fmul (PC24)
}

float AutoResolveBody::hitpointsForLevel(int level) const
{
	const float *hp = floorEntry(hitpoints, level);
	return hp ? *hp : 1.0f;
}

float AutoResolveBody::regenForLevel(int level) const
{
	const float *r = floorEntry(regenerate, level);
	return r ? *r : 0.0f;
}

bool AutoResolveCombatChain::highestPriorityTarget(Target *out) const
{
	if (!targets.empty())
	{
		*out = targets.back();
		if (out->priority >= 0 || (defaultTargets & ((1u << AR_UNIT_TYPE_COUNT) - 1)) == 0)
		{
			return true;
		}
		// RW 0x9325B4: some default bit is set: fall through to the default types
	}
	for (int t = 0; t < AR_UNIT_TYPE_COUNT; ++t)
	{
		if (defaultTargets & (1u << t))
		{
			out->type = t;
			out->priority = 0;
			return true;
		}
	}
	return false;
}

bool AutoResolveCombatChain::nextTarget(const Target &current, Target *out) const
{
	// RW 0x8297EE: it = lower_bound(current); the entry below it (when it is not the first) is the candidate
	auto it = std::lower_bound(targets.begin(), targets.end(), current, targetLess);
	const bool atBegin = it == targets.begin();
	const Target *below = atBegin ? nullptr : &*(it - 1);
	if (current.priority >= 0)
	{
		if (below && below->priority >= 0)
		{
			*out = *below;
			return true;
		}
		const int start = current.priority < 1 ? current.type + 1 : 0;
		for (int t = start; t < AR_UNIT_TYPE_COUNT; ++t)
		{
			if (defaultTargets & (1u << t))
			{
				out->type = t;
				out->priority = 0;
				return true;
			}
		}
	}
	if (!below)
	{
		return false;
	}
	*out = *below;
	return true;
}

const AutoResolveLeadership::Bonus *AutoResolveLeadership::bonusForLevel(int level) const
{
	return floorEntry(bonuses, level);
}

int AutoResolveReinforcementSchedule::roundForArmyNumber(int index) const
{
	auto it = rounds.find(index);
	if (it != rounds.end())
	{
		return it->second;
	}
	if (rounds.empty())
	{
		return 0;
	}
	const auto last = std::prev(rounds.end());
	return last->second + eachRemaining * (index - last->first);
}

const AutoResolveBonusSchedule::Bonus *AutoResolveBonusSchedule::bonusFor(int amount) const
{
	return floorEntry(bonuses, amount);
}

// ---- stores ----
const std::vector<std::string> &AutoResolveStores::blockKeywords()
{
	static const std::vector<std::string> k = { "AutoResolveWeapon", "AutoResolveArmor", "AutoResolveBody", "AutoResolveCombatChain",
		"AutoResolveLeadership", "AutoResolveHandicapLevel", "AutoResolveReinforcementSchedule", "LivingWorldAutoResolveResourceBonus",
		"LivingWorldAutoResolveSciencePurchasePointBonus" };
	return k;
}

void AutoResolveStores::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("AutoResolveWeapon", [this](INI *ini) { parseWeapon(ini); });         // RW 0x82A2A2
	registry.registerBlock("AutoResolveArmor", [this](INI *ini) { parseArmor(ini); });           // RW 0x836B35
	registry.registerBlock("AutoResolveBody", [this](INI *ini) { parseBody(ini); });             // RW 0x8293C6
	registry.registerBlock("AutoResolveCombatChain", [this](INI *ini) { parseCombatChain(ini); }); // RW 0x829CE8
	registry.registerBlock("AutoResolveLeadership", [this](INI *ini) { parseLeadership(ini); }); // RW 0x828C70
	registry.registerBlock("AutoResolveHandicapLevel", [this](INI *ini) { parseHandicap(ini); }); // RW 0x823E54
	registry.registerBlock("AutoResolveReinforcementSchedule", [this](INI *ini) { parseReinforcementSchedule(ini); }); // RW 0x824653
	registry.registerBlock("LivingWorldAutoResolveResourceBonus", [this](INI *ini) { parseBonusSchedule(ini, false); }); // RW 0x824B8A
	registry.registerBlock("LivingWorldAutoResolveSciencePurchasePointBonus", [this](INI *ini) { parseBonusSchedule(ini, true); }); // RW 0x824448
}

void AutoResolveStores::clear()
{
	*this = AutoResolveStores();
}

void AutoResolveStores::parseWeapon(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveWeapon");
	AutoResolveWeapon w;
	auto def = weapons.find("AutoResolve_DefaultWeapon");
	if (def != weapons.end())
	{
		w = def->second; // RW 0x829F6C: copy of the default (MissPercentChance, ReduceAttackWhenHurt, the damage table and the LevelBonus map)
	}
	w.name = name;
	if (weapons.count(name))
	{
		throw INIException(8, "Duplicate AutoResolveWeapon entries for %s", name.c_str()); // RW 0xC52060
	}
	ini->initFromINI(&w, kWeaponFields);
	weapons[name] = w;
}

void AutoResolveStores::parseArmor(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveArmor");
	AutoResolveArmor a;
	auto def = armors.find("AutoResolve_DefaultArmor");
	if (def != armors.end())
	{
		a = def->second;
	}
	a.name = name;
	ini->initFromINI(&a, kArmorFields);
	armors.insert(std::make_pair(name, a)); // RW 0x836B35: a second definition is parsed and dropped
}

void AutoResolveStores::parseBody(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveBody");
	AutoResolveBody b;
	auto def = bodies.find("AutoResolve_DefaultBody");
	if (def != bodies.end())
	{
		b = def->second;
	}
	b.name = name;
	if (bodies.count(name))
	{
		throw INIException(8, "Duplicate AutoResolveBody entries for %s", name.c_str()); // RW 0xC51E48
	}
	ini->initFromINI(&b, kBodyFields);
	bodies[name] = b;
}

void AutoResolveStores::parseCombatChain(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveCombatChain");
	AutoResolveCombatChain c;
	auto def = combatChains.find("AutoResolve_DefaultCombatChain");
	if (def != combatChains.end())
	{
		c = def->second;
	}
	c.name = name;
	if (combatChains.count(name))
	{
		throw INIException(8, "Duplicate AutoResolveCombatChain entries for %s", name.c_str()); // RW 0xC51F18
	}
	ini->initFromINI(&c, kChainFields);
	c.definitionIndex = (int)combatChains.size(); // WOTR2 milestone 4: definition order (a duplicate name throws above)
	combatChains[name] = c;
}

void AutoResolveStores::parseLeadership(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveLeadership");
	AutoResolveLeadership l;
	l.name = name;
	if (leaderships.count(name))
	{
		throw INIException(8, "Duplicate AutoResolveLeadership entries for %s", name.c_str()); // RW 0xC51CBC
	}
	ini->initFromINI(&l, kLeadershipFields);
	leaderships[name] = l;
}

void AutoResolveStores::parseHandicap(INI *ini)
{
	AutoResolveHandicap h;
	ini->initFromINI(&h, kHandicapFields);
	handicaps.insert(std::make_pair(h.guiDisplayedLevel, h)); // RW 0x823CCC: map insert (a repeated level keeps the first)
}

void AutoResolveStores::parseReinforcementSchedule(INI *ini)
{
	const std::string name = blockName(ini, "AutoResolveReinforcementSchedule");
	std::string lower;
	for (char ch : name)
	{
		lower += (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
	}
	AutoResolveReinforcementSchedule *s = nullptr;
	if (lower == "attacker")
	{
		s = &attackerSchedule;
	}
	else if (lower == "defender")
	{
		s = &defenderSchedule;
	}
	else
	{
		throw INIException(1, "Unknown AutoResolveReinforcementSchedule %s", name.c_str()); // RW 0xC512C0
	}
	ini->initFromINI(s, kScheduleFields);
}

void AutoResolveStores::parseBonusSchedule(INI *ini, bool science)
{
	AutoResolveBonusSchedule s;
	ini->initFromINI(&s, science ? kScienceScheduleFields : kResourceScheduleFields);
	(science ? sciencePurchasePointBonuses : resourceBonuses).push_back(s);
}

const AutoResolveWeapon *AutoResolveStores::findWeapon(const std::string &name) const
{
	auto it = weapons.find(name);
	return it == weapons.end() ? nullptr : &it->second;
}

const AutoResolveArmor *AutoResolveStores::findArmor(const std::string &name) const
{
	auto it = armors.find(name);
	return it == armors.end() ? nullptr : &it->second;
}

const AutoResolveBody *AutoResolveStores::findBody(const std::string &name) const
{
	auto it = bodies.find(name);
	return it == bodies.end() ? nullptr : &it->second;
}

const AutoResolveCombatChain *AutoResolveStores::findCombatChain(const std::string &name) const
{
	auto it = combatChains.find(name);
	return it == combatChains.end() ? nullptr : &it->second;
}

const AutoResolveLeadership *AutoResolveStores::findLeadership(const std::string &name) const
{
	auto it = leaderships.find(name);
	return it == leaderships.end() ? nullptr : &it->second;
}

namespace
{
template <class T>
const T &requireDefault(const std::map<std::string, T> &m, const char *name)
{
	auto it = m.find(name);
	if (it == m.end())
	{
		throw std::runtime_error(std::string("[S-1323] the AutoResolve data defines no ") + name);
	}
	return it->second;
}
} // namespace

const AutoResolveWeapon &AutoResolveStores::defaultWeapon() const
{
	return requireDefault(weapons, "AutoResolve_DefaultWeapon");
}
const AutoResolveArmor &AutoResolveStores::defaultArmor() const
{
	return requireDefault(armors, "AutoResolve_DefaultArmor");
}
const AutoResolveBody &AutoResolveStores::defaultBody() const
{
	return requireDefault(bodies, "AutoResolve_DefaultBody");
}
const AutoResolveCombatChain &AutoResolveStores::defaultCombatChain() const
{
	return requireDefault(combatChains, "AutoResolve_DefaultCombatChain");
}

AutoResolveHandicap AutoResolveStores::handicapFor(int displayedLevel) const
{
	if (handicaps.empty())
	{
		return AutoResolveHandicap(); // RW 0xDE8B04 {0, 1, 1, 1}
	}
	auto it = handicaps.lower_bound(displayedLevel);
	if (it == handicaps.end())
	{
		--it;
	}
	return it->second;
}

namespace
{
const AutoResolveBonusSchedule *scheduleFor(const std::vector<AutoResolveBonusSchedule> &list, const std::string &lwTemplate)
{
	for (const AutoResolveBonusSchedule &s : list)
	{
		for (const std::string &side : s.sides)
		{
			if (side == lwTemplate)
			{
				return &s;
			}
		}
	}
	return nullptr;
}
} // namespace

const AutoResolveBonusSchedule *AutoResolveStores::resourceScheduleFor(const std::string &lwTemplate) const
{
	return scheduleFor(resourceBonuses, lwTemplate);
}

const AutoResolveBonusSchedule *AutoResolveStores::scienceScheduleFor(const std::string &lwTemplate) const
{
	return scheduleFor(sciencePurchasePointBonuses, lwTemplate);
}

} // namespace LivingWorld
