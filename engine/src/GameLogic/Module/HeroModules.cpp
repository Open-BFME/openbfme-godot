// OpenBFME. GPL-3.0.
// See GameLogic/Module/HeroModules.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Object/PartitionManager.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/NumericState.h"
#include "Common/GameCommon.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
// the indices the binary pushes (names: GameLogic/BitFlagNames.cpp)
constexpr unsigned kStatusUnselectable = 3;  // RW 0x62684D(3, on)
constexpr unsigned kDisabledParalyzed = 4;   // RW 0x692432(4) / 0x692443(4) / 0x6907F1(4, frame)

void parseFlags(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<RespawnUpdateModuleData::Flags *>(store)->data(), 19, TheModelConditionNames); // RW 0x4B8C21
}

// RW 0x73A302: "None" (any case) stores none; any other name must be an FXList of TheFXListStore (INIException 3)
void parseFX(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	*static_cast<std::string *>(store) = name;
}

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

#define RU_OFF(m) (int)offsetof(RespawnUpdateModuleData, m)
const FieldParse kRespawnUpdate[] = { // RW 0xC6CE80, in the binary's order
	{ "DeathAnim", parseFlags, nullptr, RU_OFF(m_deathAnim) },
	{ "DeathFX", parseFX, nullptr, RU_OFF(m_deathFX) },
	{ "DeathAnimationTime", INI::parseDurationUnsignedInt, nullptr, RU_OFF(m_deathAnimationTime) },
	{ "InitialSpawnAnim", parseFlags, nullptr, RU_OFF(m_initialSpawnAnim) },
	{ "InitialSpawnFX", parseFX, nullptr, RU_OFF(m_initialSpawnFX) },
	{ "InitialSpawnAnimationTime", INI::parseDurationUnsignedInt, nullptr, RU_OFF(m_initialSpawnAnimationTime) },
	{ "RespawnAnim", parseFlags, nullptr, RU_OFF(m_respawnAnim) },
	{ "RespawnFX", parseFX, nullptr, RU_OFF(m_respawnFX) },
	{ "RespawnAnimationTime", INI::parseDurationUnsignedInt, nullptr, RU_OFF(m_respawnAnimationTime) },
	{ "AutoRespawnAtObjectFilter", parseFilter, nullptr, RU_OFF(m_autoRespawnAtObjectFilter) },
	{ "RespawnRules", RespawnUpdateModuleData::parseDefaultRule, nullptr, RU_OFF(m_rules) },
	{ "RespawnEntry", RespawnUpdateModuleData::parseRuleForLevel, nullptr, RU_OFF(m_rules) },
	{ "ButtonImage", INI::parseAsciiString, nullptr, RU_OFF(m_buttonImage) },
	{ "RespawnAsTemplate", INI::parseAsciiString, nullptr, RU_OFF(m_respawnAsTemplate) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef RU_OFF

// RW 0x73B22F: the special power template by name (TheSpecialPowerStore; null when unknown or without a store, as RW's lookup answers)
void parseUnpauseTemplate(INI *ini, void *instance, void *, const void *)
{
	UnpauseSpecialPowerUpgradeModuleData *d = static_cast<UnpauseSpecialPowerUpgradeModuleData *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken();
	d->m_specialPowerTemplate = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName) : nullptr;
}
const FieldParse kUnpause[] = { // RW 0xC6EB5C
	{ "SpecialPowerTemplate", parseUnpauseTemplate, nullptr, 0 },
	{ "ObeyRechageOnTrigger", INI::parseBool, nullptr, (int)offsetof(UnpauseSpecialPowerUpgradeModuleData, m_obeyRechargeOnTrigger) },
	{ nullptr, nullptr, nullptr, 0 },
};

// AttributeModifierAuraUpdate's table (RW 0xC67640); the offsets are this port's members
const char *const kRequiredConditionNames[] = { "MOUNTED", "TAINT", "ELVEN_WOOD", nullptr }; // RW 0xDB094C
void parseAntiCategory(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 1, TheAntiCategoryNames); // RW 0x89F32D -> 0x89F02A / 0x89EEDA over RW 0xD9FA40
}
// RW 0x89ECD7: only a debug message ("AffectsKindOf is obsolete, please replace with ObjectFilter."); nothing is read or stored
void parseObsoleteAffectsKindOf(INI *, void *, void *, const void *) {}
#define AU_OFF(m) (int)offsetof(AttributeModifierAuraUpdateModuleData, m)
const FieldParse kAura[] = {
	{ "BonusName", INI::parseAsciiString, nullptr, AU_OFF(m_bonusName) },
	{ "RefreshDelay", INI::parseDurationUnsignedInt, nullptr, AU_OFF(m_refreshDelay) },
	{ "Range", INI::parseReal, nullptr, AU_OFF(m_range) },
	{ "TargetEnemy", INI::parseBool, nullptr, AU_OFF(m_targetEnemy) },
	{ "AllowPowerWhenAttacking", INI::parseBool, nullptr, AU_OFF(m_allowPowerWhenAttacking) },
	{ "ObjectFilter", parseFilter, nullptr, AU_OFF(m_objectFilter) },
	{ "AffectsKindOf", parseObsoleteAffectsKindOf, nullptr, 0 },
	{ "StartsActive", INI::parseBool, nullptr, AU_OFF(m_startsActive) },
	{ "RequiredConditions", INI::parseBitString32, kRequiredConditionNames, AU_OFF(m_requiredConditions) },
	{ "AntiCategory", parseAntiCategory, nullptr, AU_OFF(m_antiCategory) },
	{ "AntiFX", parseFX, nullptr, AU_OFF(m_antiFX) },
	{ "AffectGood", INI::parseBool, nullptr, AU_OFF(m_affectGood) },
	{ "AffectEvil", INI::parseBool, nullptr, AU_OFF(m_affectEvil) },
	{ "RunWhileDead", INI::parseBool, nullptr, AU_OFF(m_runWhileDead) },
	{ "AllowSelf", INI::parseBool, nullptr, AU_OFF(m_allowSelf) },
	{ "AffectContainedOnly", INI::parseBool, nullptr, AU_OFF(m_affectContainedOnly) },
	{ "MaxActiveRank", INI::parseInt, nullptr, AU_OFF(m_maxActiveRank) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AU_OFF

#define AH_OFF(m) (int)offsetof(AutoHealBehaviorModuleData, m)
const FieldParse kAutoHeal[] = { // RW 0xC09F70
	{ "StartsActive", INI::parseBool, nullptr, AH_OFF(m_startsActive) },
	{ "ButtonTriggered", INI::parseBool, nullptr, AH_OFF(m_buttonTriggered) },
	{ "SingleBurst", INI::parseBool, nullptr, AH_OFF(m_singleBurst) },
	{ "HealingAmount", INI::parseInt, nullptr, AH_OFF(m_healingAmount) },
	{ "HealingDelay", INI::parseDurationUnsignedInt, nullptr, AH_OFF(m_healingDelay) },
	{ "Radius", INI::parseInt, nullptr, AH_OFF(m_radius) },
	{ "KindOf", ParseKindOfMask, nullptr, AH_OFF(m_kindOf) },
	{ "UnitHealPulseFX", parseFX, nullptr, AH_OFF(m_unitHealPulseFX) },
	{ "StartHealingDelay", INI::parseDurationUnsignedInt, nullptr, AH_OFF(m_startHealingDelay) },
	{ "AffectsWholePlayer", INI::parseBool, nullptr, AH_OFF(m_affectsWholePlayer) },
	{ "AffectsContained", INI::parseBool, nullptr, AH_OFF(m_affectsContained) },
	{ "HealOnlyIfNotUnderAttack", INI::parseBool, nullptr, AH_OFF(m_healOnlyIfNotUnderAttack) },
	{ "HealOnlyIfNotInCombat", INI::parseBool, nullptr, AH_OFF(m_healOnlyIfNotInCombat) },
	{ "HealOnlyOthers", INI::parseBool, nullptr, AH_OFF(m_healOnlyOthers) },
	{ "NonStackable", INI::parseBool, nullptr, AH_OFF(m_nonStackable) },
	{ "RespawnNearbyHordeMembers", INI::parseBool, nullptr, AH_OFF(m_respawnNearbyHordeMembers) },
	{ "RespawnFXList", parseFX, nullptr, AH_OFF(m_respawnFXList) },
	{ "RespawnMinimumDelay", INI::parseUnsignedInt, nullptr, AH_OFF(m_respawnMinimumDelay) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AH_OFF

bool anyKindOfMask(const Object &o, const std::array<std::uint32_t, 7> &mask)
{
	for (unsigned word = 0; word < mask.size(); ++word)
	{
		for (unsigned bit = 0; bit < 32; ++bit)
		{
			if (((mask[word] >> bit) & 1u) && o.isKindOf(word * 32 + bit))
			{
				return true; // RW 0x70C548
			}
		}
	}
	return false;
}

// RW 0x8554E4: the object's AI, or the AI of the HORDE that contains it, has a live current victim (AI + 0x40, RW 0x668303)
bool inCombat(const Object *o)
{
	if (!o)
	{
		return false;
	}
	if (const AIUpdateInterface *ai = o->getAIUpdateInterface())
	{
		if (ai->currentVictim())
		{
			return true;
		}
	}
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const Object *c = o->getContainedBy();
	if (c && kHorde >= 0 && c->isKindOf((unsigned)kHorde))
	{
		if (const AIUpdateInterface *ai = c->getAIUpdateInterface())
		{
			return ai->currentVictim() != nullptr;
		}
	}
	return false;
}

bool belowMaxHealth(const Object &o)
{
	const BodyModuleInterface *b = o.getBodyModule();
	return b && b->getHealth() < b->getMaxHealth(); // body vslots 0x10 / 0x1C
}

int mcIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

// the key test of both rule parsers: the token case-insensitively, then exactly (RW 0x8B3D9F .. : _strcmpi, then strcmp)
enum KeyMatch
{
	KEY_WRONG,
	KEY_CASE,
	KEY_OK
};
KeyMatch matchKey(const char *token, const char *key)
{
	if (!token || AsciiStringUtil::compareNoCase(token, key) != 0)
	{
		return KEY_WRONG;
	}
	return std::strcmp(token, key) == 0 ? KEY_OK : KEY_CASE;
}

const char *orNull(const char *t) { return t ? t : "(null)"; }

template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

void emitFX(GameLogic &logic, const char *site, const std::string &fx, const Object &obj)
{
	if (FXEventLog::isFXName(fx))
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, obj)); // RW 0x4B1B5A(fx, obj, 0)
	}
}
} // namespace

const RespawnRule *RespawnUpdateModuleData::ruleFor(unsigned level) const
{
	auto it = m_rules.find(level);
	if (it == m_rules.end())
	{
		it = m_rules.find(1u);
	}
	return it == m_rules.end() ? nullptr : &it->second;
}

void RespawnUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRespawnUpdate);
}

// RW 0x8B3D65
void RespawnUpdateModuleData::parseDefaultRule(INI *ini, void *instance, void *store, const void *)
{
	std::map<unsigned, RespawnRule> &rules = *static_cast<std::map<unsigned, RespawnRule> *>(store);
	RespawnRule rule; // level 1, cost 0, time 0, health 1.0f, AutoSpawn false
	if (rules.count(rule.level))
	{
		throw INIException(3, "RespawnUpdate::iniParseDefaultRule -- Duplicate RespawnRules entry.");
	}
	struct Field
	{
		const char *key;
		const char *label;
	};
	const Field fields[4] = { { "AutoSpawn", "AutoSpawn:Yes' or 'AutoSpawn:No" }, { "Cost", "Cost" }, { "Time", "Time" }, { "Health", "Health" } };
	for (int i = 0; i < 4; ++i)
	{
		const char *token = ini->getNextTokenOrNull(ini->getSepsColon());
		const KeyMatch m = matchKey(token, fields[i].key);
		if (m == KEY_WRONG)
		{
			throw INIException(3, "RespawnUpdate::iniParseDefaultRule -- RespawnRules entry expecting '%s' entry. You specified %s.", fields[i].label, orNull(token));
		}
		if (m == KEY_CASE)
		{
			throw INIException(3, "RespawnUpdate::iniParseDefaultRule -- RespawnRules entry for '%s' is case sensitive. You specified %s.", fields[i].label, token);
		}
		switch (i)
		{
			case 0: INI::parseBool(ini, instance, &rule.autoSpawn, nullptr); break;          // RW 0x42E558
			case 1: INI::parseUnsignedInt(ini, instance, &rule.cost, nullptr); break;        // RW 0x42ECB2 (B2 dup_002EF72 with no bound)
			case 2: INI::parseInt(ini, instance, &rule.timeMs, nullptr); break;              // RW 0x42EC5E
			default: INI::parsePercentToReal(ini, instance, &rule.health, nullptr); break;   // RW 0x42EEFA
		}
	}
	rules.emplace(rule.level, rule); // RW 0x8B3C31 (unique insert)
}

// RW 0x8B3F72 (B2 RespawnUpdate::iniParseNewRuleForLevel, the same checks and messages)
void RespawnUpdateModuleData::parseRuleForLevel(INI *ini, void *instance, void *store, const void *)
{
	std::map<unsigned, RespawnRule> &rules = *static_cast<std::map<unsigned, RespawnRule> *>(store);
	auto def = rules.find(1u);
	if (def == rules.end())
	{
		throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- You cannot parse a 'RespawnEntry' before 'RespawnRules'. Please add a 'RespawnRules' -- which represents level 1.");
	}
	const RespawnRule defaults = def->second;
	RespawnRule rule;
	rule.level = 0;
	const char *token = ini->getNextTokenOrNull(ini->getSepsColon());
	const KeyMatch m = matchKey(token, "Level");
	if (m == KEY_WRONG)
	{
		throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry expecting 'Level' entry. You specified %s.", orNull(token));
	}
	if (m == KEY_CASE)
	{
		throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry for 'Level' is case sensitive. You specified %s.", token);
	}
	INI::parseUnsignedInt(ini, instance, &rule.level, nullptr);
	if (rules.count(rule.level))
	{
		throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- Multiple 'RespawnEntry' with the same level of %d. You may only have one!", (int)rule.level);
	}
	rule.autoSpawn = defaults.autoSpawn;
	rule.cost = defaults.cost;
	rule.timeMs = defaults.timeMs;
	rule.health = defaults.health;
	bool gotAuto = false, gotCost = false, gotTime = false, gotHealth = false;
	const int level = (int)rule.level;
	for (token = ini->getNextTokenOrNull(ini->getSepsColon()); token; token = ini->getNextTokenOrNull(ini->getSepsColon()))
	{
		if (AsciiStringUtil::compareNoCase(token, "AutoSpawn") == 0)
		{
			if (gotAuto)
			{
				throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry Level:%d entry for 'AutoSpawn:Yes' or 'AutoSpawn:No' exists multiple times. Please remove one!", level);
			}
			if (std::strcmp(token, "AutoSpawn") != 0)
			{
				throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry Level:%d entry for 'AutoSpawn:Yes' or 'AutoSpawn:No' is case sensitive. You specified %s.", level, token);
			}
			INI::parseBool(ini, instance, &rule.autoSpawn, nullptr);
			gotAuto = true;
		}
		else if (std::strcmp(token, "Cost") == 0)
		{
			if (gotCost)
			{
				throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry Level:%d entry for 'Cost' exists multiple times. Please remove one!", level);
			}
			INI::parseUnsignedInt(ini, instance, &rule.cost, nullptr);
			gotCost = true;
		}
		else if (std::strcmp(token, "Time") == 0)
		{
			if (gotTime)
			{
				// retail's format has two %d and passes one value (the second prints whatever follows on the stack): the port prints the level twice
				throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry Level:%d Level:%d entry for 'Cost' exists multiple times. Please remove one!", level, level);
			}
			INI::parseInt(ini, instance, &rule.timeMs, nullptr);
			gotTime = true;
		}
		else if (std::strcmp(token, "Health") == 0)
		{
			if (gotHealth)
			{
				throw INIException(3, "RespawnUpdate::iniParseNewRuleForLevel -- RespawnEntry Level:%d entry for 'Cost' exists multiple times. Please remove one!", level);
			}
			INI::parsePercentToReal(ini, instance, &rule.health, nullptr);
			gotHealth = true;
		}
		// any other token is skipped (B2: no else branch)
	}
	rules.emplace(rule.level, rule);
}

// ---- RespawnUpdate ------------------------------------------------------------------------------------------------------------------------------
RespawnUpdate::RespawnUpdate(Thing *thing, const RespawnUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x8B32A6: setWakeFrame(obj, UPDATE_SLEEP_FOREVER)
}

unsigned RespawnUpdate::rankOfObject() const
{
	const ExperienceTracker *t = getObject()->getExperienceTracker();
	return t ? (unsigned)t->getRank() : 0u; // tracker + 0x24 (RW reads it through Object + 0x26C unchecked: every object has one)
}

// RW 0x8B3637
unsigned RespawnUpdate::ruleCost() const
{
	const RespawnRule *r = m_data->ruleFor(rankOfObject());
	return r ? r->cost : 1000u;
}

// RW 0x8B36B2
int RespawnUpdate::ruleSeconds() const
{
	const RespawnRule *r = m_data->ruleFor(rankOfObject());
	if (!r)
	{
		return SimMath::truncToInt32(SimMath::mulf32(0.005f, 30000.0f)); // [0xD9F610] * [0xC6C3A0], _ftol
	}
	return r->timeMs / 1000;
}

// RW 0x8B316B
const ThingTemplate *RespawnUpdate::respawnTemplate() const
{
	const Object *obj = getObject();
	const ThingTemplate *tt = m_data->m_respawnAsTemplate.empty() ? nullptr : obj->logic().things().findTemplate(m_data->m_respawnAsTemplate);
	return tt ? tt : obj->getTemplate();
}

// RW 0x8B3744
void RespawnUpdate::onDeath()
{
	if (m_state != STATE_ALIVE && m_state != STATE_SPAWN_ANIMATION)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const RespawnRule *rule = m_data->ruleFor(rankOfObject());
	if (!rule)
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
		return;
	}
	const Object::ModelConditionBits none{};
	obj->clearAndSetModelConditionFlags(none, m_data->m_deathAnim); // RW 0x5E3BA5
	emitFX(logic, "RespawnUpdate DeathFX", m_data->m_deathFX, *obj);
	// (RW 0x8B37D7: TheInGameUI's hero death notice, client)
	obj->setDisabled(kDisabledParalyzed, (UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x692432(4): no end frame
	obj->friend_setEffectivelyDead(true);                                   // RW 0x68D950(1)
	obj->setStatus(kStatusUnselectable, true);                              // RW 0x62684D(3, 1)
	m_notAutoSpawn = !rule->autoSpawn;
	m_time = rule->timeMs;
	m_cost = (std::int32_t)rule->cost;
	m_health = rule->health;
	if (Player *player = obj->getControllingPlayer())
	{
		const int index = HeroSystem::addDeadHero(*player, *obj, *this, rule->autoSpawn); // RW 0x781792
		const ExperienceTracker *t = obj->getExperienceTracker();
		if (t)
		{
			if (HeroRecord *r = player->heroes().at(index))
			{
				r->levelCap = t->getLevelCap(); // RW 0x8B3830: record + 0xD8 = tracker + 0x28
			}
		}
	}
}

// RW 0x8B3349
void RespawnUpdate::onPermanentDeath()
{
	if (m_state == STATE_ALIVE)
	{
		return;
	}
	Object *obj = getObject();
	const Object::ModelConditionBits none{};
	obj->clearAndSetModelConditionFlags(m_data->m_deathAnim, none);   // RW 0x5E3B79
	obj->clearAndSetModelConditionFlags(m_data->m_respawnAnim, none);
	obj->clearDisabled(kDisabledParalyzed);                            // RW 0x692443(4)
	setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
	m_respawnAt = INVALID_ID;
	m_state = STATE_PERMANENTLY_DEAD;
}

// RW 0x8B38A2. RW first asks the producer's exit interface (slot 9, with a position copy and true); the answer is not used afterwards (not ported: S-854)
void RespawnUpdate::onRevived(const Object *producer)
{
	(void)producer;
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Object::ModelConditionBits none{};
	unsigned duration = 0;
	if (!m_initialSpawn)
	{
		obj->clearAndSetModelConditionFlags(none, m_data->m_respawnAnim);
		duration = m_data->m_respawnAnimationTime;
		emitFX(logic, "RespawnUpdate RespawnFX", m_data->m_respawnFX, *obj);
	}
	else
	{
		obj->clearAndSetModelConditionFlags(none, m_data->m_initialSpawnAnim);
		duration = m_data->m_initialSpawnAnimationTime;
		emitFX(logic, "RespawnUpdate InitialSpawnFX", m_data->m_initialSpawnFX, *obj);
	}
	// (RW 0x8B3911 / 0x8B3936: TheInGameUI notices, client)
	m_state = STATE_SPAWN_ANIMATION;
	const RespawnRule *rule = m_data->ruleFor(rankOfObject());
	if (!rule)
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
		return;
	}
	m_health = rule->health;
	// body vslot 0x58 (RW 0x8C47DE) with (health * 100.0f, false): percent * 0.01f (the max health grows only above 100%), then internalChangeHealth(fraction * body
	// + 0x2C - health); module + 0x2C is the revive reference (ActiveBody::getReviveReference: the initial health, newMax after setMaxHealth), not the previous health
	if (ActiveBody *body = dynamic_cast<ActiveBody *>(obj->getBodyModule()))
	{
		const float percent = SimMath::mulf32(m_health, 100.0f);
		if (percent > 100.0f)
		{
			logic.noteStop("[S-854] RespawnUpdate: a respawn health above 100% raises the max health (RW 0x8C47F6): not ported");
		}
		const float fraction = SimMath::mulf32(percent, 0.01f); // [0xBE5600]
		body->internalChangeHealth(SimMath::subf32(SimMath::mulf32(fraction, body->getReviveReference()), body->getHealth()));
	}
	m_34 = -1;
	m_time = -1;
	setWakeFrame(obj, (UpdateSleepTime)duration);
}

// RW 0x8B3A35
UpdateSleepTime RespawnUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_state == STATE_AUTO_RESPAWN)
	{
		// no writer of state 3 is located (S-851); retail data never reaches it (no AutoSpawn:Yes rule)
		logic.reportError("RespawnUpdate: state 3 (auto respawn) reached on " + obj->getTemplate()->getName() + ": its port is stop S-851");
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_state == STATE_SPAWN_ANIMATION)
	{
		const Object::ModelConditionBits none{};
		obj->clearAndSetModelConditionFlags(m_data->m_respawnAnim, none);
		obj->clearAndSetModelConditionFlags(m_data->m_initialSpawnAnim, none);
		obj->setStatus(kStatusUnselectable, false); // RW 0x62684D(3, 0)
		obj->clearDisabled(kDisabledParalyzed);     // RW 0x692443(4)
		m_state = STATE_ALIVE;
		Object *producer = logic.findObjectByID(obj->getProducerID()); // RW 0x8B3AF1: Object + 0x78
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (producer && ai && ai->isIdle()) // AI vslot 0x1B8 or RW 0x660AC1 == 16 (S-854: taken as isIdle)
		{
			if (ExitInterface *exit = producer->getObjectExitInterface())
			{
				exit->exitObjectViaDoor(obj, DOOR_1); // slot 8 (obj, 0)
				// (RW 0x8B3B5F: the object's group, Object + 0x47C, is not ported)
			}
		}
	}
	return UPDATE_SLEEP_FOREVER;
}

void RespawnUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addFloat(m_health);
	h.addI32(m_state);
	h.addU32(m_respawnAt);
	h.addI32(m_34);
	h.addI32(m_time);
	h.addI32(m_cost);
	h.addBool(m_notAutoSpawn);
	h.addBool(m_initialSpawn);
}

// ---- UnpauseSpecialPowerUpgrade ---------------------------------------------------------------------------------------------------------------
void UnpauseSpecialPowerUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kUnpause);
}

// RW 0x8B9676
void UnpauseSpecialPowerUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower(); // lane HERO-2: the interface (WeaponModeSpecialPowerUpdate has one too)
		if (!sp || sp->getSpecialPowerTemplate() != m_data->m_specialPowerTemplate)
		{
			continue;
		}
		sp->pauseCountdown(false); // slot 0x24 (false)
		if (!m_data->m_obeyRechargeOnTrigger)
		{
			sp->setReadyFrame(obj->logic().getFrame()); // slot 0x20 (TheGameLogic frame)
		}
	}
}

// RW 0x8B9701 (the mux's slot 0 first: only an executed upgrade pauses again)
void UnpauseSpecialPowerUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded())
	{
		return;
	}
	for (const std::unique_ptr<BehaviorModule> &m : getObject()->modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (sp && sp->getSpecialPowerTemplate() == m_data->m_specialPowerTemplate)
		{
			sp->pauseCountdown(true);
		}
	}
}

// ---- AttributeModifierAuraUpdate --------------------------------------------------------------------------------------------------------------
void AttributeModifierAuraUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAura);         // RW 0x89F354
	buildBaseFieldParse(p); // RW 0x89F33D: the upgrade base table at data + 0x28
}

// RW 0x89ED5C
AttributeModifierAuraUpdate::AttributeModifierAuraUpdate(Thing *thing, const AttributeModifierAuraUpdateModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(static_cast<Object *>(thing), data)
	, m_data(data)
{
	friend_setNextCallFrame(getObject()->logic().getFrame() + 1u); // RW 0x89EDAB: setWakeFrame(obj, 1)
	if (m_data->m_startsActive)
	{
		giveSelfUpgrade(); // RW 0x89EDBC -> 0x855388
	}
}

// RW 0x8554D6 (mux slot 10): wake next frame
void AttributeModifierAuraUpdate::upgradeImplementation()
{
	setWakeFrame(getObject(), (UpdateSleepTime)1);
}

// RW 0x89F42D
UpdateSleepTime AttributeModifierAuraUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const AttributeModifierAuraUpdateModuleData *d = m_data;
	if ((obj->isEffectivelyDead() && !d->m_runWhileDead) || !isAlreadyUpgraded())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const UpdateSleepTime next = (UpdateSleepTime)((obj->getID() % 5u) + d->m_refreshDelay); // RW 0x89F6BD: id % 5 + RefreshDelay
	static const int kMounted = mcIndex("MOUNTED");     // RW 0x89F49A: 0xD6
	static const int kAttacking = mcIndex("ATTACKING"); // RW 0x89F4AE: 0x25
	if ((d->m_requiredConditions & 1u) && !obj->testModelCondition(kMounted))
	{
		return next;
	}
	if (!d->m_allowPowerWhenAttacking && obj->testModelCondition(kAttacking))
	{
		return next;
	}
	Pulse pulse;
	pulse.source = obj;
	if (d->m_antiCategory != 0 && !d->m_bonusName.empty())
	{
		// RW 0x89F4F8 .. 0x89F537: now + the list's Duration (RW 0x614495), 999999 for a list without one
		const ModifierListTemplate *list = TheAttributeModifierStore ? TheAttributeModifierStore->find(d->m_bonusName) : nullptr;
		const unsigned duration = list ? list->m_duration : 0u;
		pulse.antiExpire = logic.getFrame() + (duration != 0 ? duration : 999999u);
	}
	static const int kWalkOnWall = ObjectTemplateInfoBuilder::kindOfIndex("WALK_ON_TOP_OF_WALL");
	if (kWalkOnWall >= 0 && obj->isKindOf((unsigned)kWalkOnWall))
	{
		logic.noteStop("[S-856] AttributeModifierAuraUpdate: a WALK_ON_TOP_OF_WALL source (geometry radius, no relationship filter; RW 0x89F5F8) is not ported: Range is used");
	}
	++m_pulses;
	if (!d->m_affectContainedOnly)
	{
		// RW 0x89F676: ThePartitionManager within Range (or the geometry radius of a WALK_ON_TOP_OF_WALL source, S-856), distance type 0 (FROM_CENTER_2D), sort mode 1
		// (near to far, the STLport introsort: lane MODULES-2), with the filters RW 0xC10E20 (not effectively dead), RW 0xC1D660 (not the source), RW 0xC0F374 (the
		// same Object + 0x458 bit 3 as the source: never set here, S-856), the relationship filter RW 0xC11DC0 (enemies 1 / allies 4, RW 0x89F610) and the ObjectFilter
		// for the owner (RW 0xBE4CC8); an applied modifier can grant an upgrade at once (a CostModifierUpgrade appends to the player's ordered list), so the order is state
		const Player *owner = obj->getControllingPlayer();
		PartitionFilterFn targets([&](Object &o) {
			if (o.isEffectivelyDead() || &o == obj)
			{
				return false;
			}
			if (!d->m_affectGood && !d->m_affectEvil)
			{
				const Relationship rel = obj->getRelationship(o);
				if (d->m_targetEnemy ? rel != ENEMIES : rel != ALLIES)
				{
					return false;
				}
			}
			return !d->m_objectFilter || ObjectFilterMatch::allows(logic, *d->m_objectFilter, o, owner);
		});
		std::vector<Object *> hits;
		for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), d->m_range, FROM_CENTER_2D, { &targets }, ITER_SORTED_NEAR_TO_FAR))
		{
			if (!hit.object->isDestroyed())
			{
				hits.push_back(hit.object);
			}
		}
		if (d->m_allowSelf)
		{
			hits.push_back(obj); // RW 0x89F66E: RW 0xA3A3A0(obj, 0) adds the object itself
		}
		for (Object *o : hits)
		{
			affect(*o, pulse);
		}
	}
	else if (ContainModuleInterface *contain = obj->getContain())
	{
		pulse.containedMode = true; // RW 0x89F69A: the contain iterates its objects (vslot 0x110)
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			const std::vector<Object *> list(items->begin(), items->end());
			for (Object *o : list)
			{
				affect(*o, pulse);
			}
		}
	}
	return next;
}

// RW 0x89F114
void AttributeModifierAuraUpdate::affect(Object &o, const Pulse &pulse)
{
	const AttributeModifierAuraUpdateModuleData *d = m_data;
	GameLogic &logic = o.logic();
	static const int kIgnored = ObjectTemplateInfoBuilder::kindOfIndex("IGNORED_IN_GUI"); // RW 0x89F12E: KindOf 0x2F
	static const int kArmyOfDead = ObjectTemplateInfoBuilder::kindOfIndex("ARMY_OF_DEAD"); // and 0x97
	if ((kIgnored >= 0 && o.isKindOf((unsigned)kIgnored)) || (kArmyOfDead >= 0 && o.isKindOf((unsigned)kArmyOfDead)))
	{
		return;
	}
	if (!d->m_allowSelf && &o == pulse.source)
	{
		return;
	}
	const Player *player = o.getControllingPlayer();
	const bool evil = player && player->getPlayerTemplate() && player->getPlayerTemplate()->m_evil; // template + 0x1BC
	if (d->m_affectGood && evil)
	{
		return;
	}
	if (d->m_affectEvil && !evil)
	{
		return;
	}
	if (const ExperienceTracker *t = o.getExperienceTracker())
	{
		if (d->m_maxActiveRank != 0 && d->m_maxActiveRank < t->getRank())
		{
			return;
		}
	}
	if (d->m_requiredConditions & 6u)
	{
		// RW 0x89F1F8 .. 0x89F271: TAINT / ELVEN_WOOD need the terrain area at the object (RW 0xAD49B0) to be of that kind and owned by an ally; the areas are
		// not ported, so no object is in one (S-856)
		logic.noteStop("[S-856] AttributeModifierAuraUpdate: RequiredConditions TAINT / ELVEN_WOOD need the terrain areas (RW 0xAD49B0), not ported: no object is in one");
		return;
	}
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	if (pulse.containedMode && kHorde >= 0 && o.isKindOf((unsigned)kHorde))
	{
		if (ContainModuleInterface *c = o.getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				const std::vector<Object *> list(items->begin(), items->end());
				for (Object *m : list)
				{
					affect(*m, pulse); // RW 0x89F27F: the horde's contain iterates with this step
				}
			}
		}
	}
	if (pulse.antiExpire != 0)
	{
		++m_anti; // RW 0x89F2A1: pool.disableCategory(AntiCategory, expiry) (RW 0x804FCC, XP-1's S-633)
		emitFX(logic, "AttributeModifierAuraUpdate AntiFX", d->m_antiFX, o);
	}
	if (!d->m_bonusName.empty())
	{
		o.addAttributeModifier(d->m_bonusName, -1); // RW 0x68F1A8
	}
}

void AttributeModifierAuraUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	crcMux(h);
	h.addU32(m_pulses);
}

// ---- AutoHealBehavior --------------------------------------------------------------------------------------------------------------------------
void AutoHealBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAutoHeal);       // RW 0x656626
	buildBaseFieldParse(p); // RW 0x656636: the upgrade base at data + 8
}

// RW 0x85562D
AutoHealBehavior::AutoHealBehavior(Thing *thing, const AutoHealBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(static_cast<Object *>(thing), data)
	, m_data(data)
{
	m_waitingForButton = data->m_buttonTriggered;
	if (data->m_startsActive)
	{
		giveSelfUpgrade(); // RW 0x8556A4 -> 0x855388
		const int delay = getObject()->logic().random().getValue(1, (int)data->m_healingDelay, "AutoHealBehavior.cpp", 0xA5); // RW 0x8556BB (RW 0x6D328E)
		friend_setNextCallFrame(getObject()->logic().getFrame() + (UnsignedInt)delay);
	}
	else
	{
		friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
	}
}

void AutoHealBehavior::upgradeImplementation()
{
	setWakeFrame(getObject(), (UpdateSleepTime)1); // RW 0x8554D6
}

void AutoHealBehavior::processUpgradeRemoval()
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER); // RW 0x85544E (the slot 0x24 sleep; + 0xC cleared)
}

// RW 0x85541B: + 0x30 = 1, + 0x2C = 0x3FFFFFFF, sleep forever
void AutoHealBehavior::waitForButton()
{
	m_waitingForButton = true;
	m_nextHealFrame = (UnsignedInt)UPDATE_SLEEP_FOREVER;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x855431
void AutoHealBehavior::triggerByButton()
{
	m_waitingForButton = false;
	giveSelfUpgrade();
	setWakeFrame(getObject(), (UpdateSleepTime)1);
}

int AutoHealBehavior::amount() const
{
	float sum = 0.0f;
	getObject()->attributeModifierSum(ATTRIBUTE_AUTO_HEAL, nullptr, sum); // RW 0x68C818(0x13, &sum, 0)
	return SimMath::truncToInt32(NumericState::pc24Add((float)m_data->m_healingAmount, sum)); // fild; fadd; _ftol (x87 PC24)
}

// RW 0x855533
bool AutoHealBehavior::eligible(Object &target) const
{
	const Object *healer = getObject();
	if (m_data->m_healOnlyOthers && &target == healer)
	{
		return false;
	}
	// RW 0x855533 reads the HEALER (the context's + 0x1C, loaded into ESI before RW 0x8554E4; the target is the stack argument) for both the combat and the
	// recent-damage tests; the target's identity, death flags, KindOf and health are tested below
	if (m_data->m_healOnlyIfNotInCombat && inCombat(healer)) // RW 0x8555A0
	{
		return false;
	}
	if (m_data->m_healOnlyIfNotUnderAttack)
	{
		const UnsignedInt now = target.logic().getFrame();
		UnsignedInt last = 0xFFFFFFFFu;
		bool known = false;
		if (healer->getContain() && healer->getContain()->getHordeContainInterface())
		{
			// RW 0x68C866: a healer whose contain answers the horde interface (contain vslot 0x7C) reads the horde's damage frame (vslot 0x26C); that reader
			// is not identified (S-858): counted, the horde healer's own body is read instead
			const_cast<GameLogic &>(target.logic()).noteStop("[S-858] AutoHealBehavior: a horde healer's damage frame reader (RW 0x68C866 -> vslot 0x26C) is not identified; its body's is used");
		}
		if (const ActiveBody *b = dynamic_cast<const ActiveBody *>(healer->getBodyModule()))
		{
			last = b->lastDamageFrame(); // body vslot 0x44 (-1 = never)
			known = true;
		}
		// RW 0x8555A2 .. 0x8555D8: a last damage frame at or after now skips the test; else a damage within LOGICFRAMES_PER_SECOND frames (last + 5 > now) is an attack
		if (known && now > last && last + (UnsignedInt)LOGICFRAMES_PER_SECOND > now)
		{
			return false;
		}
	}
	if (target.isEffectivelyDead())
	{
		return false; // + 0x458 bit 0 (bit 3 is not identified: S-858)
	}
	return anyKindOfMask(target, m_data->m_kindOf) && belowMaxHealth(target);
}

// RW 0x855761
void AutoHealBehavior::healOne(Object &target, bool fx)
{
	if (m_waitingForButton)
	{
		return;
	}
	GameLogic &logic = target.logic();
	if (m_data->m_nonStackable && target.getBodyModule())
	{
		logic.noteStop("[S-858] AutoHealBehavior NonStackable: the body's last heal frame (body vslot 0x48) is not ported; the heal stacks");
	}
	const float heal = (float)amount();
	if (m_data->m_radius == 0)
	{
		target.attemptHealing(heal, getObject()); // RW 0x690532
	}
	else
	{
		target.attemptHealingFromSoleBenefactor(heal, getObject(), m_data->m_healingDelay); // RW 0x690584
	}
	if (fx)
	{
		emitFX(logic, "AutoHealBehavior UnitHealPulseFX", m_data->m_unitHealPulseFX, target);
	}
	m_nextHealFrame = logic.getFrame() + m_data->m_healingDelay;
	++m_heals;
}

// RW 0x8558C0
UpdateSleepTime AutoHealBehavior::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const AutoHealBehaviorModuleData *d = m_data;
	if (m_waitingForButton || !isAlreadyUpgraded() || obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (d->m_healOnlyIfNotInCombat && inCombat(obj))
	{
		return (UpdateSleepTime)(d->m_startHealingDelay != 0 ? d->m_startHealingDelay : (unsigned)LOGICFRAMES_PER_SECOND);
	}
	if (amount() < 1)
	{
		return (UpdateSleepTime)1;
	}
	if (d->m_affectsWholePlayer)
	{
		logic.noteStop("[S-858] AutoHealBehavior AffectsWholePlayer (RW 0x8559A6) is not ported (no retail object uses it)");
		return (UpdateSleepTime)d->m_healingDelay;
	}
	if (d->m_affectsContained)
	{
		if (ContainModuleInterface *c = obj->getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				const std::vector<Object *> list(items->begin(), items->end());
				for (Object *o : list)
				{
					healOne(*o, true); // RW 0x855D91 (the contain's flag that turns the FX off is not identified: S-858)
				}
			}
		}
		return d->m_singleBurst ? UPDATE_SLEEP_FOREVER : (UpdateSleepTime)d->m_healingDelay;
	}
	if (d->m_radius != 0)
	{
		const bool respawnDue = logic.getFrame() >= d->m_respawnMinimumDelay + m_lastRespawnFrame; // RW 0x855A6E
		// RW 0x855B8F: ThePartitionManager within Radius (an int, cvtsi2ss), distance type 0 (FROM_CENTER_2D), unsorted, with the filters RW 0xC11DC0 (allies, 4),
		// RW 0xC10E20 (not effectively dead) and RW 0xC0F374 (the same Object + 0x458 bit 3 as the healer: never set here, S-858): the healer itself is a target (lane MODULES-2)
		const float range = SimMath::sseFromInt32((int)d->m_radius);
		PartitionFilterFn allies([&](Object &o) { return obj->getRelationship(o) == ALLIES && !o.isEffectivelyDead(); });
		std::vector<Object *> hits;
		for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), range, FROM_CENTER_2D, { &allies }, ITER_FASTEST))
		{
			if (!hit.object->isDestroyed())
			{
				hits.push_back(hit.object);
			}
		}
		for (Object *o : hits)
		{
			if (anyKindOfMask(*o, d->m_kindOf) && belowMaxHealth(*o) && eligible(*o))
			{
				healOne(*o, true);
			}
			if (respawnDue && d->m_respawnNearbyHordeMembers)
			{
				logic.noteStop("[S-858] AutoHealBehavior RespawnNearbyHordeMembers (RW 0x855C83: a horde's missing members) is not ported");
			}
		}
		if (respawnDue)
		{
			m_lastRespawnFrame = logic.getFrame();
		}
		return d->m_singleBurst ? UPDATE_SLEEP_FOREVER : (UpdateSleepTime)d->m_healingDelay;
	}
	if (!belowMaxHealth(*obj))
	{
		return UPDATE_SLEEP_FOREVER;
	}
	healOne(*obj, true);
	return (UpdateSleepTime)d->m_healingDelay;
}

// RW 0x855706
void AutoHealBehavior::onDamage(const DamageInfo &info)
{
	if (m_waitingForButton || !isAlreadyUpgraded() || m_data->m_radius != 0)
	{
		return;
	}
	if (info.m_input.m_damageSubType != 2 && m_data->m_startHealingDelay > 0)
	{
		setWakeFrame(getObject(), (UpdateSleepTime)m_data->m_startHealingDelay);
		return;
	}
	if (getObject()->logic().getFrame() > m_nextHealFrame)
	{
		setWakeFrame(getObject(), (UpdateSleepTime)1);
	}
}

void AutoHealBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	crcMux(h);
	h.addU32(m_nextHealFrame);
	h.addBool(m_waitingForButton);
	h.addU32(m_lastRespawnFrame);
	h.addU32(m_heals);
}

// ---- BuildableHeroListUpgrade -----------------------------------------------------------------------------------------------------------------
// RW 0x8BC4C6
void BuildableHeroListUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	Player *player = obj->getControllingPlayer();
	if (!player || !player->getPlayerTemplate())
	{
		return; // RW reads the player and its template unchecked (no retail object reaches this without them)
	}
	GameLogic &logic = obj->logic();
	for (const std::string &name : player->getPlayerTemplate()->m_buildableRingHeroesMP) // template + 0x198
	{
		const ThingTemplate *tt = logic.things().findTemplate(name);
		if (!tt)
		{
			// RW 0x781801 -> 0x780713 dereferences the template: a name that is no template crashes retail
			logic.reportError("BuildableHeroListUpgrade: BuildableRingHeroesMP name '" + name + "' is no template (retail dereferences null, RW 0x780713)");
			continue;
		}
		player->heroes().addPurchase(*tt);
	}
	// (RW 0x8BC4F3: TheControlBar + 0x28 = 1, the control bar refresh; RW 0x8D28F1 the custom anim of the mux)
}

void HeroModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<RespawnUpdate, RespawnUpdateModuleData>(modules, "RespawnUpdate");
	bindRuntime<BuildableHeroListUpgrade, BuildableHeroListUpgradeModuleData>(modules, "BuildableHeroListUpgrade");
	bindRuntime<UnpauseSpecialPowerUpgrade, UnpauseSpecialPowerUpgradeModuleData>(modules, "UnpauseSpecialPowerUpgrade");
	bindRuntime<AttributeModifierAuraUpdate, AttributeModifierAuraUpdateModuleData>(modules, "AttributeModifierAuraUpdate");
	bindRuntime<AutoHealBehavior, AutoHealBehaviorModuleData>(modules, "AutoHealBehavior");
}
