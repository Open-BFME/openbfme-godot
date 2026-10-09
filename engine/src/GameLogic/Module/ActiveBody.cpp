// OpenBFME. GPL-3.0.
// See GameLogic/Module/ActiveBody.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/Object/Object.h"

#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <stdexcept>

namespace
{
// RW 0x73A302 (parseFXList): the token is stored; retail looks it up and rejects an unknown name other than None (the FXList store is
// not loaded: stop S-149)
void parseFXName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0x8C3F1F: one `DamageCreationList = OCL DamageType [BONE_STATE]` line per row (the tokens are kept; the OCL store is not loaded, S-149)
void parseDamageCreation(INI *ini, void *, void *store, const void *)
{
	std::vector<std::vector<std::string>> *list = static_cast<std::vector<std::vector<std::string>> *>(store);
	std::vector<std::string> tokens;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		tokens.push_back(t);
	}
	list->push_back(std::move(tokens));
}

#define AB_OFF(member) (int)offsetof(ActiveBodyModuleData, member)
const FieldParse kActiveBodyFieldParse[] = {
	{ "MaxHealth", INI::parseReal, nullptr, AB_OFF(m_maxHealth) },
	{ "MaxHealthDamaged", INI::parseReal, nullptr, AB_OFF(m_maxHealthDamaged) },
	{ "MaxHealthReallyDamaged", INI::parseReal, nullptr, AB_OFF(m_maxHealthReallyDamaged) },
	{ "InitialHealth", INI::parseReal, nullptr, AB_OFF(m_initialHealth) },
	{ "RecoveryTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_recoveryTime) },
	{ "DodgePercent", INI::parsePercentToReal, nullptr, AB_OFF(m_dodgePercent) },
	{ "EnteringDamagedTransitionTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_enteringDamagedTransitionTime) },
	{ "EnteringReallyDamagedTransitionTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_enteringReallyDamagedTransitionTime) },
	{ "GrabObject", INI::parseAsciiString, nullptr, AB_OFF(m_grabObject) },
	{ "GrabFX", parseFXName, nullptr, AB_OFF(m_grabFX) },
	{ "GrabDamage", INI::parseReal, nullptr, AB_OFF(m_grabDamage) },
	{ "GrabOffset", INI::parseCoord2D, nullptr, AB_OFF(m_grabOffset) },
	{ "UseDefaultDamageSettings", INI::parseBool, nullptr, AB_OFF(m_useDefaultDamageSettings) },
	{ "DamageCreationList", parseDamageCreation, nullptr, AB_OFF(m_damageCreationList) },
	{ "HealingBuffFx", parseFXName, nullptr, AB_OFF(m_healingBuffFx) },
	{ "DamagedAttributeModifier", INI::parseAsciiString, nullptr, AB_OFF(m_damagedAttributeModifier) },
	{ "ReallyDamagedAttributeModifier", INI::parseAsciiString, nullptr, AB_OFF(m_reallyDamagedAttributeModifier) },
	{ "CheerRadius", INI::parseReal, nullptr, AB_OFF(m_cheerRadius) },
	{ "RemoveUpgradesOnDeath", INI::parseBool, nullptr, AB_OFF(m_removeUpgradesOnDeath) },
	{ "BurningDeathBehavior", INI::parseBool, nullptr, AB_OFF(m_burningDeathBehavior) },
	{ "BurningDeathFX", parseFXName, nullptr, AB_OFF(m_burningDeathFX) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AB_OFF
} // namespace

void ActiveBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kActiveBodyFieldParse);
}

// RW 0x8C3841
ActiveBody::ActiveBody(Thing *thing, const ActiveBodyModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
	m_initialHealth = data->m_initialHealth; // +0x1C = data + 0xC
	m_maxHealth = data->m_maxHealth;         // +0x20 = data + 0x8
	const float inverse = SimMath::divf32(1.0f, m_maxHealth); // RW 0x8C392B divss
	m_damagedFraction = SimMath::mulf32(data->m_maxHealthDamaged, inverse); // RW 0x8C3930 mulss
	m_reallyDamagedFraction = SimMath::mulf32(data->m_maxHealthReallyDamaged, inverse); // RW 0x8C393E mulss
	m_currentHealth = data->m_initialHealth == -1.0f ? data->m_maxHealth : data->m_initialHealth;
	m_previousHealth = m_currentHealth;
	m_reviveReference = m_currentHealth; // lane HERO-1: module + 0x2C starts at the actual initial health
	m_curDamageState = BODY_PRISTINE; // RW 0x8C3841 ends with setCorrectDamageState: its effects need the finished object, so applyInitialDamageState runs after Object::buildModules
	if (data->m_useDefaultDamageSettings)
	{
		const GameLogicSettings &settings = getObject()->logic().settings();
		if ((m_damagedFraction == 0.0f || m_reallyDamagedFraction == 0.0f) && !settings.bodyThresholdsLoaded)
		{
			throw std::logic_error("ActiveBody: the template uses the default damage thresholds but GameLogicSettings has none (GameData UnitDamagedThreshold / UnitReallyDamagedThreshold not loaded)");
		}
		if (m_damagedFraction == 0.0f)
		{
			m_damagedFraction = settings.unitDamagedThreshold;
		}
		if (m_reallyDamagedFraction == 0.0f)
		{
			m_reallyDamagedFraction = settings.unitReallyDamagedThreshold;
		}
	}
}

// RW 0x8C1B79
BodyDamageType ActiveBody::calcDamageState() const
{
	if (m_currentHealth == 0.0f)
	{
		return BODY_RUBBLE;
	}
	if (SimMath::mulf32(m_reallyDamagedFraction, m_maxHealth) >= m_currentHealth)
	{
		return BODY_REALLYDAMAGED;
	}
	if (SimMath::mulf32(m_damagedFraction, m_maxHealth) >= m_currentHealth)
	{
		return BODY_DAMAGED;
	}
	return BODY_PRISTINE;
}

// ZH ActiveBody::setInitialHealth (ActiveBody.cpp): the initial health is a percent of the max health; the current and previous health follow
void ActiveBody::setInitialHealth(int initialPercent)
{
	m_initialHealth = SimMath::divf32(SimMath::mulf32(m_maxHealth, (float)initialPercent), 100.0f); // ZH order: (max * percent) / 100
	m_currentHealth = m_initialHealth;
	m_previousHealth = m_currentHealth;
	m_reviveReference = m_currentHealth;
	updateDamageState();
}

void ActiveBody::crc(StateHasher &h) const
{
	h.addFloat(m_currentHealth);
	h.addFloat(m_previousHealth);
	h.addFloat(m_reviveReference); // lane HERO-1
	if (m_indestructible)
	{
		h.addU32(0x1D35u); // lane SCRIPT-2: hashed only when set, so a game that never sets it keeps its hash
	}
	h.addFloat(m_initialHealth);
	h.addFloat(m_maxHealth);
	h.addFloat(m_damagedFraction);
	h.addFloat(m_reallyDamagedFraction);
	h.addFloat(m_damageScalar);
	h.addU32(m_lastDamageFrame);
	h.addU32(m_lastDamager);
	h.addU32((std::uint32_t)m_curDamageState);
}

// RW 0x8C2A5D (setCorrectDamageState). TARGET FACTS read from the binary: the damage state field (+0x30) is rewritten from calcDamageState (RW 0x8C1B79); when it changed vslot 0x34 runs
// (RW 0x8C283C: the Damaged / ReallyDamaged attribute modifiers, UPGRADE-1's). For a template that is KindOf STRUCTURE (tt KindOf mask byte 0x108 bit 0x80 = bit 7) the state RUBBLE does:
//   entering (new > old, new == 3): the structure's footprint leaves the pathfinder and is entered again (RW 0x6E85FB remove, 0x6E85E9 add: the add sees the RUBBLE state), the object
//   gets the status NO_COLLISIONS (RW 0x8C2E3B: 0x62684D(4, 1)), the geometry height becomes the template's StructureRubbleHeight (RW tt + 0x607, a byte; <= 0 takes GlobalData
//   DefaultStructureRubbleHeight, RW + 0xAE4) through RW 0x68B2CB, and RW 0x8C1BB7 runs: production is reset (RW 0x79DA66 on object + 0x26C), the upgrades go when RemoveUpgradesOnDeath
//   (data + 0x50) and the command points are released (0x68E114 = removeFromPlayerCommandPoints);
//   leaving it (new < 3, old == 3): the template geometry returns, the footprint is re-entered, NO_COLLISIONS is cleared (RW 0x8C2D87..0x8C2DCC).
// A WALK_ON_TOP_OF_WALL template (KindOf 60, byte 0x10F bit 0x10) runs a "Bookend" variant (BOOKENDING status 0x53, CAN_NOT_WALK_ON 0x4D) that is a wall's (S-342).
// NOT PORTED (S-342): the geometry height (the geometry is the template's, an Object has none of its own), the production reset, the upgrade removal, the attribute modifiers, the damage FX
// transitions of the tail (RW 0x8C2C64..0x8C2EF1) and the Bookend of walls.
// INFERENCE: the model conditions DAMAGED / REALLYDAMAGED / RUBBLE: the call site in RW that tells the drawable was not found; ZH Drawable::reactToBodyDamageStateChange sets them
// (clearing the other two); that is what the object's model condition mask gets here.
void ActiveBody::updateDamageState(bool initial)
{
	const BodyDamageType old = m_curDamageState;
	const BodyDamageType now = calcDamageState();
	m_curDamageState = now;
	if (now == old)
	{
		return;
	}
	Object &obj = *getObject();
	Object::ModelConditionBits clear{}, set{};
	const int damaged = CombatNames::modelCondition("DAMAGED"), really = CombatNames::modelCondition("REALLYDAMAGED"), rubble = CombatNames::modelCondition("RUBBLE");
	for (int bit : { damaged, really, rubble })
	{
		clear[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	const int on = now == BODY_DAMAGED ? damaged : now == BODY_REALLYDAMAGED ? really : now == BODY_RUBBLE ? rubble : -1;
	if (on >= 0)
	{
		set[(size_t)on >> 5] |= 1u << (on & 31);
	}
	obj.clearAndSetModelConditionFlags(clear, set);
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (DamageModuleInterface *d = m->getDamage())
		{
			d->onBodyDamageStateChange(old, now); // ZH setCorrectDamageState tells the damage modules (RW CastleMemberBehavior RW 0x79A0ED)
		}
	}
	if (!obj.isKindOf((unsigned)CombatNames::kinds().structure))
	{
		return;
	}
	AIWorld *world = obj.logic().aiWorld();
	const bool mapReady = world && world->mapReady() && !initial;
	if (now == BODY_RUBBLE && old != BODY_RUBBLE)
	{
		if (mapReady)
		{
			world->removeObjectFromPathfindMap(obj);
			world->addObjectToPathfindMap(obj);
		}
		obj.setStatus((unsigned)CombatNames::statuses().noCollisions, true);
		obj.removeFromPlayerCommandPoints(); // RW 0x8C1BB7 -> 0x68E114
		++obj.logic().combat().counters().rubbleEntered;
	}
	else if (old == BODY_RUBBLE && now != BODY_RUBBLE)
	{
		if (mapReady)
		{
			world->removeObjectFromPathfindMap(obj);
			world->addObjectToPathfindMap(obj);
		}
		obj.setStatus((unsigned)CombatNames::statuses().noCollisions, false);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lane COMBAT-1: the damage pipeline
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x5D893C reads these four through the victim (scratch/weapon1/damage.md A5). The flank test (RW 0x5D89E6: the source object found by id, then
// Object::isFlankedBy RW 0x68FB63 on the victim) is lane HORDE-2's HordeFlank (S-582); an estimate (no victim / source pair) and a source that is gone answer
// "not flanked". Lane XP-1: INVULNERABLE and ARMOR are the victim's AttributeModifierPoolUpdate queries with the damage type's name (Damage.h: RW 0x68C818 ->
// 0x804F39); in the available binary both sites (RW 0x5D8A64, 0x5D8AF1) call into the community-added .danetta section, so the retail 2.01 form of these two
// queries is not verified (stop S-636, caveat S-001)
class ActiveBody::DamageHost : public AdjustDamageHost
{
public:
	explicit DamageHost(GameLogic &logic, Object *victim = nullptr, unsigned sourceId = 0)
		: m_logic(logic)
		, m_victim(victim)
		, m_sourceId(sourceId)
	{
	}
	bool victimFlankedByAttacker() override
	{
		Object *source = m_victim && m_sourceId != 0 ? m_logic.findObjectByID(m_sourceId) : nullptr;
		if (!source)
		{
			return false;
		}
		return HordeFlank::isFlankedBy(*m_victim, *source);
	}
	bool invulnerableTo(int damageType) override
	{
		float v = 0.0f;
		return m_victim && damageType >= 0 && damageType < DAMAGE_NUM_TYPES && m_victim->attributeModifierSum(ATTRIBUTE_MODIFIER_INVULNERABLE, TheDamageNames[damageType], v);
	}
	float armorModifierSum(int damageType) override
	{
		float v = 0.0f;
		if (m_victim && damageType >= 0 && damageType < DAMAGE_NUM_TYPES)
		{
			m_victim->attributeModifierSum(ATTRIBUTE_MODIFIER_ARMOR, TheDamageNames[damageType], v);
		}
		return v;
	}
	float armorMaxBonus() override { return 0.75f; } // GlobalData AttributeModifierArmorMaxBonus (RW +0xAE8, retail 75%): the table is GameData's, not loaded (S-322)

private:
	GameLogic &m_logic;
	Object *m_victim;
	unsigned m_sourceId;
};

const ArmorTemplate *ActiveBody::currentArmor() const
{
	const Object &obj = *getObject();
	const ThingTemplate *tt = obj.getTemplate();
	const ArmorTemplateSet *set = FindArmorTemplateSet(tt->armorTemplateSets(), obj.armorSetFlags());
	if (!set || set->m_armorName.empty())
	{
		return nullptr;
	}
	if (!TheArmorStore)
	{
		throw std::logic_error("ActiveBody: no ArmorStore is installed (the damage pipeline runs outside a world context)");
	}
	const ArmorTemplate *armor = TheArmorStore->findArmorTemplate(set->m_armorName);
	if (!armor && !m_armorReported)
	{
		m_armorReported = true;
		obj.logic().reportError("ActiveBody of " + tt->getName() + ": the armor '" + set->m_armorName + "' is not in the ArmorStore (RW looks the name up per hit and takes no armour)");
	}
	return armor;
}

float ActiveBody::estimateDamage(const DamageInfoInput &input) const
{
	DamageHost host(getObject()->logic(), getObject());
	const ArmorTemplate *armor = currentArmor();
	return AdjustDamage(armor, armor, input, true, host);
}

// RW 0x8C31A5
void ActiveBody::internalChangeHealth(float delta)
{
	BodyHealth h;
	h.health = m_currentHealth;
	h.maxHealth = m_maxHealth;
	h.previousHealth = m_previousHealth;
	InternalChangeHealth(h, delta);
	m_currentHealth = h.health;
	m_previousHealth = h.previousHealth;
	updateDamageState(); // RW 0x8C31A5 calls vslot 0x54 = setCorrectDamageState(false)
}

// RW 0x8C4AB4
void ImmortalBody::internalChangeHealth(float delta)
{
	BodyHealth h;
	h.health = m_currentHealth;
	h.maxHealth = getMaxHealth();
	h.previousHealth = m_previousHealth;
	ImmortalInternalChangeHealth(h, delta);
	m_currentHealth = h.health;
	m_previousHealth = h.previousHealth;
	updateDamageState();
}

// RW 0x8C49C7
void HighlanderBody::attemptDamage(DamageInfo &info)
{
	info.m_input.m_amount = HighlanderClampAmount(info.m_input.m_amount, getHealth(), info.m_input.m_damageType);
	ActiveBody::attemptDamage(info);
}

// RW 0x8C3FA3, in the order of scratch/weapon1/damage.md 2.7
void ActiveBody::attemptDamage(DamageInfo &info)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	const CombatNames::Status &st = CombatNames::statuses();
	const int type = info.m_input.m_damageType;
	// lane SCRIPT-2: RW 0x8C3FDA, before every other test: an indestructible body takes nothing
	if (m_indestructible)
	{
		return;
	}
	// (3) UNATTACKABLE only takes UNRESISTABLE
	if (obj.testStatus((unsigned)st.unattackable) && type != DAMAGE_UNRESISTABLE)
	{
		return;
	}
	// (5)
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	if (obj.isEffectivelyDead())
	{
		return;
	}
	// (6)
	DamageHost host(logic, &obj, info.m_input.m_sourceID);
	const ArmorTemplate *armor = currentArmor();
	float amount = AdjustDamage(armor, armor, info.m_input, false, host);
	if (type == DAMAGE_HEALING)
	{
		if (!info.m_input.m_kill)
		{
			attemptHealing(info);
		}
		return;
	}
	// (7) the body scalar (not for UNRESISTABLE), (9) the gate
	if (type != DAMAGE_UNRESISTABLE)
	{
		amount = SimMath::mulf32(amount, m_damageScalar);
	}
	const float previous = m_currentHealth;
	if (amount > 0.0f || info.m_input.m_kill)
	{
		// (10) the burning death fire cap is stop S-322 (the burn module and the status masks are not ported); (11) kill takes the whole health
		if (info.m_input.m_kill)
		{
			amount = m_currentHealth;
		}
		// (13) internalChangeHealth(-amount) (the info goes down with it in RW: DelayedDeathBody's override reads and clears its kill flag)
		m_currentInfo = &info;
		internalChangeHealth(SimMath::subf32(0.0f, amount));
		m_currentInfo = nullptr;
		// (15)
		info.m_output.m_actualDamageDealt = amount;
		info.m_output.m_actualDamageClipped = SimMath::subf32(m_previousHealth, m_currentHealth);
		// (16)
		m_lastDamageFrame = logic.getFrame();
		m_lastDamager = info.m_input.m_sourceID;
		++logic.combat().counters().damageApplications;
		// ZH: the damage modules hear of the hit (RW 0x79B757 CastleMemberBehavior::onDamage reads the amount dealt)
		if (info.m_output.m_actualDamageDealt > 0.0f)
		{
			for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
			{
				if (DamageModuleInterface *d = m->getDamage())
				{
					d->onDamage(info);
				}
			}
		}
		// (18) death: health <= 0 and the hit took health
		if (m_currentHealth <= 0.0f && previous > 0.0f)
		{
			if (Object *killer = logic.findObjectByID(info.m_input.m_sourceID))
			{
				killer->scoreTheKill(obj);
			}
			DieModuleInterface::Event event;
			event.sourceId = info.m_input.m_sourceID;
			event.deathType = info.m_input.m_deathType;
			event.damageType = type;
			event.damageSubType = info.m_input.m_damageSubType; // lane HERO-2
			event.damageAmount = info.m_input.m_amount;
			event.actualDamageDealt = info.m_output.m_actualDamageDealt;
			event.actualDamageClipped = info.m_output.m_actualDamageClipped;
			obj.friend_onDie(event);
		}
		// lane XP-1: RW 0x8C46A9 .. 0x8C46EA: the attacker's player earns skill points for the share of the victim's maximum health the hit took (capped at
		// 1.0), after the kill credit and the death (ExperienceWorld::awardSkillPointsForDamage)
		if (Object *attacker = logic.findObjectByID(info.m_input.m_sourceID))
		{
			float fraction = SimMath::divf32(info.m_output.m_actualDamageClipped, m_maxHealth);
			if (fraction > 1.0f)
			{
				fraction = 1.0f;
			}
			if (fraction > 0.0f)
			{
				logic.experience().awardSkillPointsForDamage(*attacker, obj, fraction);
			}
		}
	}
	// (19) doDamageFX (RW 0x8C2F02, lane FX-2)
	doDamageFX(info);
}

// RW 0x8C2F02 doDamageFX (lane FX-2). The body's DamageFX (+0xFC) is the one of the armour set chosen for the object's armour flags (the ArmorSet's DamageFX
// field, resolved by name in TheDamageFXStore; RW keeps the pointer of the set in use); none: no FX. A hit of the type played last (+0x3C) while the frame is
// below +0x38 is throttled. Else RW 0x76256A: the FXList is getDamageFX(type, actual damage dealt, source) (RW 0x76226B: row 0, amount 0 gives none,
// amount >= AmountForMajorFX the major list) and plays as doFXObj(fx, this object, the source) through RW 0x4B1B5A; when one played, +0x3C = the type and
// +0x38 = now + ThrottleTime(type) (RW 0x76225D, row 0). NOT PORTED (stop S-681): the loop after it (body + 0xE0: per damage type ObjectCreationLists created
// through RW 0x5F0126) - no OCL store exists yet.
void ActiveBody::doDamageFX(const DamageInfo &info)
{
	Object &obj = *getObject();
	const ThingTemplate *tt = obj.getTemplate();
	const ArmorTemplateSet *set = tt ? FindArmorTemplateSet(tt->armorTemplateSets(), obj.armorSetFlags()) : nullptr;
	if (!set || !TheDamageFXStore || !FXEventLog::isFXName(set->m_damageFXName))
	{
		return;
	}
	const DamageFX *dfx = TheDamageFXStore->findDamageFX(set->m_damageFXName);
	if (!dfx)
	{
		return; // RW 0x73AF3F: an unknown DamageFX name is NULL
	}
	GameLogic &logic = obj.logic();
	const int type = info.m_input.m_damageFXOverride;
	if (type < 0 || type >= DAMAGEFX_TYPE_COUNT)
	{
		return;
	}
	const UnsignedInt now = logic.getFrame();
	if (type == m_lastDamageFXType && now < m_damageFXThrottleUntil)
	{
		return;
	}
	const std::string &fx = dfx->getDamageFX(type, info.m_output.m_actualDamageDealt);
	if (!FXEventLog::isFXName(fx))
	{
		return;
	}
	FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "DamageFX", now, fx, obj);
	e.secondary = logic.findObjectByID(info.m_input.m_sourceID) ? info.m_input.m_sourceID : (ObjectID)INVALID_ID;
	logic.fxEvents().emit(e);
	m_lastDamageFXType = type;
	m_damageFXThrottleUntil = now + dfx->getThrottleTime(type);
}

// lane XP-1: RW 0x8C1CD5 setMaxHealth: the maximum and the initial health become newMax; type 1 changes the health by (health / oldMax) * newMax - health,
// type 2 by newMax - oldMax (SSE single, through internalChangeHealth); then a health above the new maximum comes down to it
void ActiveBody::setMaxHealth(float newMax, int changeType)
{
	const float oldMax = m_maxHealth;
	m_maxHealth = newMax;
	m_initialHealth = newMax;
	m_reviveReference = newMax; // lane HERO-1: RW 0x8C1CD5 stores newMax at module + 0x2C, the revive's reference
	if (changeType == 1)
	{
		internalChangeHealth(SimMath::subf32(SimMath::mulf32(SimMath::divf32(m_currentHealth, oldMax), newMax), m_currentHealth));
	}
	else if (changeType == 2)
	{
		internalChangeHealth(SimMath::subf32(newMax, oldMax));
	}
	if (m_currentHealth > newMax)
	{
		internalChangeHealth(SimMath::subf32(newMax, m_currentHealth));
	}
}

// RW 0x8C2FC1 attemptHealing
void ActiveBody::attemptHealing(DamageInfo &info)
{
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	if (getObject()->isEffectivelyDead())
	{
		return;
	}
	const float amount = info.m_input.m_amount; // adjustDamage returns the amount for HEALING
	if (amount > 0.0f)
	{
		internalChangeHealth(amount);
		info.m_output.m_actualDamageDealt = amount;
		info.m_output.m_actualDamageClipped = SimMath::subf32(m_previousHealth, m_currentHealth);
	}
}
