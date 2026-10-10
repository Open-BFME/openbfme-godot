// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The special power modules. See GameLogic/Module/SpecialPowerModules.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Module/SpellEffectModules.h"

#include "Common/SpecialPower.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/BitFlags.h"
#include "Common/AsciiString.h"
#include "Common/NumericState.h"
#include "Common/GameCommon.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ObjectCreationList.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Upgrade.h"
#include "Common/StateHash.h"
#include "Common/Team.h"

#include <algorithm>
#include <limits>

#include <cstddef>
#include <stdexcept>

namespace
{
#include "Common/SpecialPowerNames.inc"

// RW 0x73B22F: the template by name from TheSpecialPowerStore
void parseSpecialPowerTemplate(INI *ini, void *instance, void *, const void *)
{
	SpecialPowerModuleData *d = static_cast<SpecialPowerModuleData *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken();
	d->m_specialPowerTemplate = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName) : nullptr;
}
// the name lookups of FX lists, OCLs and sounds (RW 0x73A302, 0x73A368, 0x73ACEF): the name is kept (S-520 / S-529)
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}
// SetModelCondition (RW 0x869F22): not ported, the rest of the line is kept (S-529)
void parseLine(INI *ini, void *, void *store, const void *)
{
	std::string line;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		line += line.empty() ? "" : " ";
		line += t;
	}
	*static_cast<std::string *>(store) = line;
}
// ChangeWeather (RW 0x8966F9, lane SPELL-2): the token's index in RW 0xDB0074 (an unknown name throws, RW 0x42B999); an index outside [0, 5] is
// INIException 3 "Invalid Weather setting: '%s'." (RW 0xC64A14)
const char *const kWeatherNames[] = { "NONE", "CLOUDY", "RAINY", "CLOUDYRAINY", "SUNNY", "MOUNTED", "WEAPON_TOGGLE", "MOVING", nullptr };
void parseChangeWeather(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken();
	const int index = INI::scanIndexList(token, kWeatherNames);
	if (index < 0 || index > 5)
	{
		throw INIException(3, "Invalid Weather setting: '%s'.", token);
	}
	*static_cast<int *>(store) = index;
}
void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

#define SP_OFF(member) (int)offsetof(SpecialPowerModuleData, member)
// RW 0xC64DB0, in the binary's order
const FieldParse kSpecialPowerModuleFieldParse[] = {
	{ "SpecialPowerTemplate", parseSpecialPowerTemplate, nullptr, 0 },
	{ "UpdateModuleStartsAttack", INI::parseBool, nullptr, SP_OFF(m_updateModuleStartsAttack) },
	{ "StartsPaused", INI::parseBool, nullptr, SP_OFF(m_startsPaused) },
	{ "InitiateSound", parseName, nullptr, SP_OFF(m_initiateSound) },
	{ "ReEnableAntiCategory", INI::parseBool, nullptr, SP_OFF(m_reEnableAntiCategory) },
	{ "AntiCategory", INI::parseBitString32, TheAntiCategoryNames, SP_OFF(m_antiCategory) }, // RW 0x89F32D (BitFlags<15>::parse, lane SPELL-2)
	{ "AntiFX", parseName, nullptr, SP_OFF(m_antiFX) },
	{ "AttributeModifier", INI::parseAsciiString, nullptr, SP_OFF(m_attributeModifier) },
	{ "AttributeModifierRange", INI::parseReal, nullptr, SP_OFF(m_attributeModifierRange) },
	{ "AttributeModifierAffectsSelf", INI::parseBool, nullptr, SP_OFF(m_attributeModifierAffectsSelf) },
	{ "AttributeModifierAffects", parseFilter, nullptr, SP_OFF(m_attributeModifierAffects) },
	{ "AttributeModifierFX", parseName, nullptr, SP_OFF(m_attributeModifierFX) },
	{ "AttributeModifierWeatherBased", INI::parseBool, nullptr, SP_OFF(m_attributeModifierWeatherBased) },
	{ "WeatherDuration", INI::parseDurationUnsignedInt, nullptr, SP_OFF(m_weatherDuration) },
	{ "RequirementsFilterMPSkirmish", parseFilter, nullptr, SP_OFF(m_requirementsFilterMPSkirmish) },
	{ "RequirementsFilterStrategic", parseFilter, nullptr, SP_OFF(m_requirementsFilterStrategic) },
	{ "TargetEnemy", INI::parseBool, nullptr, SP_OFF(m_targetEnemy) },
	{ "TargetAllSides", INI::parseBool, nullptr, SP_OFF(m_targetAllSides) },
	{ "InitiateFX", parseName, nullptr, SP_OFF(m_initiateFX) },
	{ "TriggerFX", parseName, nullptr, SP_OFF(m_triggerFX) },
	{ "SetModelCondition", parseLine, nullptr, SP_OFF(m_setModelCondition) },
	{ "SetModelConditionTime", INI::parseReal, nullptr, SP_OFF(m_setModelConditionTime) },
	{ "GiveLevels", INI::parseInt, nullptr, SP_OFF(m_giveLevels) },
	{ "DisableDuringAnimDuration", INI::parseBool, nullptr, SP_OFF(m_disableDuringAnimDuration) },
	{ "IdleWhenStartingPower", INI::parseBool, nullptr, SP_OFF(m_idleWhenStartingPower) },
	{ "AffectGood", INI::parseBool, nullptr, SP_OFF(m_affectGood) },
	{ "AffectEvil", INI::parseBool, nullptr, SP_OFF(m_affectEvil) },
	{ "AffectAllies", INI::parseBool, nullptr, SP_OFF(m_affectAllies) },
	{ "AvailableAtStart", INI::parseBool, nullptr, SP_OFF(m_availableAtStart) },
	{ "ChangeWeather", parseChangeWeather, nullptr, SP_OFF(m_changeWeather) },
	{ "AdjustVictim", INI::parseBool, nullptr, SP_OFF(m_adjustVictim) },
	{ "OnTriggerRechargeSpecialPower", INI::parseAsciiString, nullptr, SP_OFF(m_onTriggerRechargeSpecialPower) },
	{ "BurnDecayModifier", INI::parseUnsignedInt, nullptr, SP_OFF(m_burnDecayModifier) },
	{ "UseDistanceFromCommandCenter", INI::parseBool, nullptr, SP_OFF(m_useDistanceFromCommandCenter) },
	{ "DistanceFromCommandCenter", INI::parseReal, nullptr, SP_OFF(m_distanceFromCommandCenter) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SP_OFF

#define PH_OFF(member) (int)offsetof(PlayerHealSpecialPowerModuleData, member)
// RW 0xC74EC0, in the binary's order
const FieldParse kPlayerHealFieldParse[] = {
	{ "HealAmount", INI::parseReal, nullptr, PH_OFF(m_healAmount) },
	{ "HealAsPercent", INI::parseBool, nullptr, PH_OFF(m_healAsPercent) },
	{ "HealAffects", ParseKindOfMask, nullptr, PH_OFF(m_healAffects) },
	{ "HealRadius", INI::parseReal, nullptr, PH_OFF(m_healRadius) },
	{ "HealFX", parseName, nullptr, PH_OFF(m_healFX) },
	{ "HealOCL", parseName, nullptr, PH_OFF(m_healOCL) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PH_OFF

bool anyKindOf(const Object &o, const KindOfMaskType &mask)
{
	for (unsigned word = 0; word < mask.size(); ++word)
	{
		for (unsigned bit = 0; bit < 32; ++bit)
		{
			if ((mask[word] >> bit) & 1u)
			{
				if (o.isKindOf(word * 32 + bit))
				{
					return true;
				}
			}
		}
	}
	return false;
}
// RW 0x8C7A8F (UpgradeOCL = <science> <OCL>), RW 0x8C7ACC (UpgradeName: a list of names, appended)
void parseUpgradeOCL(INI *ini, void *instance, void *, const void *)
{
	OCLSpecialPowerModuleData *d = static_cast<OCLSpecialPowerModuleData *>(instance);
	const std::string science = ini->getNextToken();
	const std::string ocl = ini->getNextToken();
	d->m_upgradeOCL.push_back({ science, ocl });
}
void parseUpgradeNames(INI *ini, void *instance, void *, const void *)
{
	OCLSpecialPowerModuleData *d = static_cast<OCLSpecialPowerModuleData *>(instance);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		d->m_upgradeNames.push_back(t);
	}
}
#define OCL_OFF(member) (int)offsetof(OCLSpecialPowerModuleData, member)
// RW 0xC73A40, in the binary's order
const FieldParse kOCLSpecialPowerFieldParse[] = {
	{ "UpgradeOCL", parseUpgradeOCL, nullptr, 0 },
	{ "OCL", parseName, nullptr, OCL_OFF(m_ocl) },
	{ "CreateLocation", INI::parseIndexList, kOCLCreateLocationNames, OCL_OFF(m_createLocation) },
	{ "UpgradeName", parseUpgradeNames, nullptr, 0 },
	{ "NearestSecondaryObjectFilter", parseFilter, nullptr, OCL_OFF(m_nearestSecondaryObjectFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef OCL_OFF
} // namespace

void OCLSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kOCLSpecialPowerFieldParse);
}

void SpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpecialPowerModuleFieldParse);
}

void PlayerHealSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kPlayerHealFieldParse);
}

PlayerHealSpecialPower::PlayerHealSpecialPower(Thing *thing, const PlayerHealSpecialPowerModuleData *data)
	: SpecialPowerModule(thing, data)
	, m_data(data)
{
}

// RW 0x8CC37B
bool PlayerHealSpecialPower::healObject(Object &victim)
{
	static const int kWebbed = CombatNames::kindOf("WEBBED");       // template + 0x11B bit 3
	static const int kStructure = CombatNames::kindOf("STRUCTURE"); // template + 0x108 bit 7
	Object *healer = getObject();
	if (victim.isKindOf((unsigned)kWebbed) || !anyKindOf(victim, m_data->m_healAffects))
	{
		return false;
	}
	if (victim.getTeam() != healer->getTeam() && victim.getRelationship(*healer) != ALLIES)
	{
		return false;
	}
	if (victim.isKindOf((unsigned)kStructure))
	{
		const float pct = victim.getConstructionPercent();
		if (pct >= 0.0f && pct < 99.0f) // RW 0x8CC3DD .. 0x8CC3F5: [0, 99.0f (RW 0xBE49F0))
		{
			return false;
		}
	}
	BodyModuleInterface *body = victim.getBodyModule();
	if (!body)
	{
		return false;
	}
	float amount = m_data->m_healAmount;
	if (m_data->m_healAsPercent)
	{
		amount = SimMath::pc24Mul(body->getMaxHealth(), amount); // RW 0x8CC417 .. 0x8CC41D: fld (vslot +0x1C) ; fmul ; fstp dword
	}
	if (!(amount > 0.0f))
	{
		return false;
	}
	victim.attemptHealing(amount, nullptr); // RW 0x690532 (source 0)
	if (!m_data->m_healFX.empty())
	{
		++m_fx; // RW 0x4B1B5A: the FX list at the victim (client)
	}
	return true;
}

// RW 0x8CC4B4
int PlayerHealSpecialPower::healAt(const Coord3D &pos)
{
	Object *healer = getObject();
	if (healer->getDisabledMask() != 0 || !healer->getControllingPlayer())
	{
		return 0;
	}
	if (!m_data->m_healOCL.empty())
	{
		// RW 0x5F00CA: the OCL at pos with the healer as the source (lane SPELL-2: the CreateObject port, S-530); counted as before
		++m_ocl;
		if (const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_healOCL) : nullptr)
		{
			ocl->create(healer->logic(), healer, pos);
		}
	}
	// RW 0x8CC505 .. 0x8CC556: ThePartitionManager within HealRadius of pos, distance type 0 (FROM_CENTER_2D), unsorted, with the filter RW 0xC10E20 (not
	// effectively dead); each hit healed in the partition's order (lane MODULES-2)
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	int healed = 0;
	for (const PartitionHit &hit : healer->logic().partition().iterateObjectsInRange(pos, m_data->m_healRadius, FROM_CENTER_2D, { &alive }, ITER_FASTEST))
	{
		if (healObject(*hit.object))
		{
			++healed;
		}
	}
	return healed;
}

// ---- SpecialPowerModule ---------------------------------------------------------------------------------------------------------------
namespace
{
const int kStatusUnderConstruction = 2;  // RW 0x897450 testStatus(2)
const int kStatusSpecialAbility = 0x46;  // SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING (RW 0x896C83)
const std::uint32_t kRespectRechargeDiscount = 1u << 5; // RW 0x896EBA: template Flags bit 5
} // namespace

// RW 0x8973EE
SpecialPowerModule::SpecialPowerModule(Thing *thing, const SpecialPowerModuleData *data)
	: BehaviorModule(thing, data)
	, m_spData(data)
{
	const SpecialPowerTemplate *t = data->m_specialPowerTemplate;
	if (!getObject()->testStatus(kStatusUnderConstruction) && t && !t->isSharedNSync())
	{
		startPowerRecharge(1.0f); // RW 0x897471
	}
	if (data->m_startsPaused)
	{
		pauseCountdown(true); // RW 0x897482
	}
	if (data->m_availableAtStart)
	{
		m_readyFrame = now(); // RW 0x897497
	}
}

unsigned SpecialPowerModule::now() const
{
	return getObject()->logic().getFrame(); // TheGameLogic + 0x40
}

// RW 0x896C72
bool SpecialPowerModule::isReady() const
{
	return readyImpl(true);
}

float SpecialPowerModule::getPercentReady() const
{
	return percentImpl(true);
}

// the display reads (the HUD, GameWorld::get_spellbook): the same answers without inserting a shared timer (Sol review r2: a read must not change the
// state hash). RW's getters insert an absent entry at `now` (RW 0x6AD26F), and `now` is also what a peek answers for it.
bool SpecialPowerModule::isReadyForDisplay() const
{
	return readyImpl(false);
}

float SpecialPowerModule::getPercentReadyForDisplay() const
{
	return percentImpl(false);
}

unsigned SpecialPowerModule::sharedReadyFrame(bool insert) const
{
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	Player *p = getObject()->getControllingPlayer();
	return insert ? p->science().getSharedReadyFrame(t->getID(), now()) : p->science().peekSharedReadyFrame(t->getID(), now());
}

bool SpecialPowerModule::readyImpl(bool insert) const
{
	if (m_updateDisabled)
	{
		return false;
	}
	Object *obj = getObject();
	if (obj->testStatus(kStatusSpecialAbility))
	{
		return false;
	}
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	Player *p = obj->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		return now() >= sharedReadyFrame(insert); // RW 0x896CC8
	}
	return m_pauseCount == 0 && now() >= m_readyFrame;
}

// lane HERO-2: RW 0x896F99
void SpecialPowerModule::refreshTimer()
{
	if (m_pauseCount > 0 || m_updateDisabled || getObject()->testStatus(kStatusSpecialAbility))
	{
		return;
	}
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	if (t && t->isSharedNSync())
	{
		return;
	}
	m_readyFrame = now();
}

// lane HERO-2: RW 0x897368
void SpecialPowerModule::copyTimerFrom(const SpecialPowerModule &src)
{
	const SpecialPowerTemplate *a = src.getSpecialPowerTemplate();
	const SpecialPowerTemplate *b = getSpecialPowerTemplate();
	if (!a || !b || a->getID() != b->getID() || a->isSharedNSync() || src.m_updateDisabled || src.m_pauseCount > 0)
	{
		return;
	}
	m_pauseCount = src.m_pauseCount;       // + 0x1C
	m_rechargeLength = src.m_rechargeLength; // + 0x14
	m_pauseFrame = src.m_pauseFrame;       // + 0x20
	m_readyFrame = src.m_readyFrame;       // + 0x18
	m_pausedPercent = src.m_pausedPercent; // + 0x24
}

// RW 0x896CF2 (x87 at the game's 24-bit precision: (1 - remaining / length))
float SpecialPowerModule::percentImpl(bool insert) const
{
	if (m_updateDisabled)
	{
		return 0.0f;
	}
	if (readyImpl(insert))
	{
		return 1.0f;
	}
	if (m_pauseCount > 0)
	{
		return m_pausedPercent;
	}
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	if (!t)
	{
		return 0.0f;
	}
	unsigned ready = m_readyFrame;
	Player *p = getObject()->getControllingPlayer();
	if (p && t->isSharedNSync())
	{
		ready = sharedReadyFrame(insert);
	}
	const unsigned remaining = ready - now(); // unsigned: fild + the 2^32 correction (RW 0x896D84 .. 0x896D89)
	const float ratio = SimMath::pc24DivD((double)remaining, (double)m_rechargeLength);
	return SimMath::pc24Sub(1.0f, ratio);
}

// RW 0x89724B
unsigned SpecialPowerModule::getReadyFrame() const
{
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	Player *p = getObject()->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		return p->science().getSharedReadyFrame(t->getID(), now());
	}
	if (m_pauseCount <= 0 && getObject()->getDisabledMask() == 0)
	{
		return m_readyFrame;
	}
	return now() - m_pauseFrame + m_readyFrame;
}

// RW 0x896756
void SpecialPowerModule::pauseCountdown(bool pause)
{
	if (pause)
	{
		if (m_pauseCount == 0)
		{
			m_pauseFrame = now();
			m_pausedPercent = getPercentReady();
		}
		++m_pauseCount;
		return;
	}
	if (m_pauseCount > 0 && --m_pauseCount == 0)
	{
		m_readyFrame += now() - m_pauseFrame;
	}
}

// RW 0x896E31
void SpecialPowerModule::startPowerRecharge(float percentOfCurrent)
{
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	Object *obj = getObject();
	Player *p = obj->getControllingPlayer();
	if (!t || !p)
	{
		return;
	}
	if (t->isSharedNSync())
	{
		p->science().resetOrStartSharedReadyFrame(t->getID(), t->getReloadTime(), now()); // RW 0x896E7D
		m_updateDisabled = false;
		return;
	}
	// RW 0x896EA0: the object's RECHARGE_TIME attribute modifier scale (attribute modifiers not ported: 1.0, S-529)
	const float modifierScale = 1.0f;
	float playerScale = 1.0f;
	if (t->getFlags() & kRespectRechargeDiscount)
	{
		playerScale = SimMath::pc24Add(p->science().rechargeDiscount(), 1.0f); // RW 0x896EC7 .. 0x896ED2: fld ; fadd 1.0 ; fstp
	}
	// RW 0x896EE3 .. 0x896EFC: fild reload ; fmul playerScale ; fmul modifierScale ; _ftol2 (the product stays in the register between the multiplies)
	const double wide = SimMath::pc24MulW(SimMath::pc24MulW((double)t->getReloadTime(), playerScale), modifierScale);
	unsigned reload = SimMath::ftol2Low32(wide);
	if (reload == 0)
	{
		reload = 1;
	}
	if (1.0f > percentOfCurrent)
	{
		const float pct = getPercentReady();
		if (pct == 0.0f)
		{
			return; // RW 0x896F2D: neither the frame nor the flag change
		}
		float rest = SimMath::sseSub(pct, percentOfCurrent);
		if (0.0f > rest)
		{
			rest = 0.0f;
		}
		const double scaled = SimMath::pc24MulW(SimMath::pc24SubW(1.0, rest), (double)reload); // fld1 ; fsub ; fild ; fmulp
		m_readyFrame = now() + SimMath::ftol2Low32(scaled);
	}
	else
	{
		m_readyFrame = now() + reload;
		m_rechargeLength = m_readyFrame - now();
	}
	m_updateDisabled = false;
}

// RW 0x896B56
void SpecialPowerModule::onScienceAcquired()
{
	startPowerRecharge(1.0f);
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	Player *p = getObject()->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		p->science().setSharedReadyFrame(t->getID(), now());                // RW 0x896BA5
		m_readyFrame = p->science().getSharedReadyFrame(t->getID(), now()); // RW 0x896BB4
	}
	if (m_spData->m_startsPaused)
	{
		pauseCountdown(true); // RW 0x896BCB
	}
	// RW 0x896BD9 ..: a PublicTimer power of a STRUCTURE adds the in-game UI timer (client)
}

// RW 0x8969E5 (GameLogic + 0x114 == 3, S-525: the MP / skirmish filter in a skirmish or multiplayer game, no filter otherwise)
bool SpecialPowerModule::requirementsMet() const
{
	Player *p = getObject()->getControllingPlayer();
	if (!p || !p->science().mode().skirmishOrMultiplayer)
	{
		return true;
	}
	const std::shared_ptr<const ObjectFilter> &f = m_spData->m_requirementsFilterMPSkirmish;
	if (!f)
	{
		return true; // RW 0x896A0E: an unset filter (S-523 for its default)
	}
	return p->hasObjectMatching(*f, true); // RW 0x6ABD0B
}

// RW 0x897E87 / 0x897987 (the ported part)
namespace
{
const unsigned kOptionUngated = 0x40000u; // RW 0x8980A6 `test byte [options + 2], 4`: skips the pause / disabled gate (the option's name is not identified)
const int kSpecialPowerTypeDisguise = 0x85; // RW 0x8977E5: SpecialPowerTemplate + 0x1C (the type) 0x85 with the object's model condition 300 DISGUISED (SpecialDisguiseUpdate)

void emitPowerFX(GameLogic &logic, const char *site, const std::string &fx, const Object &at)
{
	if (FXEventLog::isFXName(fx))
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, at)); // RW 0x4B1B5A
	}
}
void emitPowerFXAt(GameLogic &logic, const char *site, const std::string &fx, const Object &source, const Coord3D &pos)
{
	if (FXEventLog::isFXName(fx))
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, site, logic.getFrame(), fx, source); // RW 0x494615 (no matrix)
		e.primary = INVALID_ID;
		e.position = pos;
		e.hasTransform = false;
		logic.fxEvents().emit(e);
	}
}
bool isEvilPlayer(const Player *p)
{
	return p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_evil; // PlayerTemplate + 0x1BC (Player + 0x34 null reads false)
}
} // namespace

// RW 0x897987 (the trigger part of every base do*): see the header comment
void SpecialPowerModule::triggerSpecialPower(const Coord3D *loc, const Object *)
{
	++m_triggers;
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const SpecialPowerModuleData *d = m_spData;
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	++m_clientNotices; // RW 0x89713F: the script engine's power notices, the EVA / radar events (S-920)
	createViewObject(loc); // RW 0x896FD9
	if (!d->m_updateModuleStartsAttack)
	{
		startPowerRecharge(1.0f); // RW 0x8979C2
	}
	// RW 0x8979C5 .. 0x897A3F: OnTriggerRechargeSpecialPower names a template other than this one: every special power module of the object whose
	// template has that name restarts its recharge
	if (!d->m_onTriggerRechargeSpecialPower.empty() && (!t || t->getName() != d->m_onTriggerRechargeSpecialPower))
	{
		for (const auto &m : obj->modules())
		{
			SpecialPowerModuleInterface *sp = m->getSpecialPower();
			const SpecialPowerTemplate *other = sp ? sp->getSpecialPowerTemplate() : nullptr;
			if (other && other->getName() == d->m_onTriggerRechargeSpecialPower)
			{
				sp->startPowerRecharge(1.0f);
			}
		}
	}
	Player *player = obj->getControllingPlayer();
	if (player && t && t->m_lightPointCost != 0)
	{
		++m_unported; // RW 0x6AA91D: Player + 0x33C -= LightPointCost (the light point system is not ported, S-531)
	}
	// lane HERO-1: SetModelCondition (`ModelConditionState:<name>`, RW 0x869F22: unknown -1) with SetModelConditionTime > 0: the special model condition for
	// _ftol(LOGICFRAMES_PER_SECOND * time) frames (fild [0xD9F608]; fmul; RW 0x68B581), and with DisableDuringAnimDuration DISABLED_HELD until then (RW 0x6907F1(3))
	int condition = -1;
	if (!d->m_setModelCondition.empty())
	{
		const std::string &line = d->m_setModelCondition;
		const size_t colon = line.find(':');
		if (colon != std::string::npos && line.compare(0, colon, "ModelConditionState") == 0)
		{
			const std::string name = line.substr(colon + 1);
			for (int i = 0; TheModelConditionNames[i]; ++i)
			{
				if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
				{
					condition = i;
					break;
				}
			}
		}
	}
	if (condition != -1 && d->m_setModelConditionTime > 0.0f)
	{
		const int frames = SimMath::truncToInt32(NumericState::pc24Mul((float)LOGICFRAMES_PER_SECOND, d->m_setModelConditionTime));
		obj->setSpecialModelConditionState(condition, (UnsignedInt)frames);
		if (d->m_disableDuringAnimDuration)
		{
			obj->setDisabled(3, logic.getFrame() + (UnsignedInt)frames); // RW 0x6907F1(3, now + frames)
		}
	}
	// RW 0x897A92 .. 0x897AB5: TriggerFX at the location, else on the object
	if (loc)
	{
		emitPowerFXAt(logic, "SpecialPower TriggerFX", d->m_triggerFX, *obj, *loc);
	}
	else
	{
		emitPowerFX(logic, "SpecialPower TriggerFX", d->m_triggerFX, *obj);
	}
	if (d->m_attributeModifierWeatherBased)
	{
		// RW 0x897AD5 .. 0x897B4A: TheGlobalWeatherSystem
		GlobalWeatherSystem &weather = logic.weather();
		if (!d->m_reEnableAntiCategory)
		{
			const int kind = d->m_affectEvil ? GlobalWeatherSystem::AFFECT_EVIL : d->m_affectGood ? GlobalWeatherSystem::AFFECT_GOOD : GlobalWeatherSystem::AFFECT_ALL;
			if (!d->m_attributeModifierAffects)
			{
				++m_unported; // no AttributeModifierAffects: retail hands an unset filter handle to RW 0x7640C1 (S-922)
			}
			weather.setWeatherModifier(kind, d->m_attributeModifierAffects, player, d->m_attributeModifier, d->m_antiCategory, d->m_weatherDuration);
		}
		else
		{
			weather.clear(); // RW 0x897AF1 (0x71A04F)
		}
		if (d->m_changeWeather != GlobalWeatherSystem::WEATHER_NO_CHANGE)
		{
			weather.setWeather(d->m_changeWeather, d->m_burnDecayModifier, d->m_weatherDuration); // RW 0x897B45
		}
		return;
	}
	// RW 0x897B4F .. 0x897C47: the relationship mask (bits 1 << Relationship: 1 ENEMIES, 2 NEUTRAL, 4 ALLIES); an AntiCategory power swaps 1 and 4
	unsigned relMask = d->m_targetEnemy ? 1u : 4u;
	if (d->m_targetAllSides)
	{
		relMask = 7u;
	}
	else if (d->m_reEnableAntiCategory)
	{
		relMask = 5u;
	}
	else if (d->m_antiCategory != 0)
	{
		relMask = relMask == 1u ? 4u : 1u;
	}
	const Coord3D center = loc ? *loc : *obj->getPosition();
	static const int kSpellBook = CombatNames::kindOf("SPELL_BOOK"); // template + 0x117 bit 3
	const bool sameAreaFilter = !obj->isKindOf((unsigned)kSpellBook);
	if (sameAreaFilter)
	{
		++m_unported; // RW 0xC0F374: the victim must share the caster's "outside the playable area" flag (Object + 0x458 bit 3, not modelled): S-920
	}
	// RW 0x897BAB .. 0x897C47: ThePartitionManager within AttributeModifierRange of the centre, distance type 0 (FROM_CENTER_2D), sort mode 1 (near to far, the
	// STLport introsort), with the filter chain (RW 0xA394C0 appends) RW 0xBE4CC8 AttributeModifierAffects for the caster's player (flag 1), RW 0xC11DC0 the
	// relationship mask (flag 0: the caster's relationship toward the victim), RW 0xC10E20 alive, then RW 0xC0F374 unless SPELL_BOOK (counted above); lane MODULES-2
	std::vector<Object *> victims;
	PartitionFilterFn affects([&](Object &o) {
		// RW 0x66122D (flag 1); RW 0x89693A / 0x89693D installs NONE with empty masks (0x763D11): an omitted filter rejects the scan.
		if (!d->m_attributeModifierAffects || !ObjectFilterMatch::allows(logic, *d->m_attributeModifierAffects, o, player))
		{
			return false;
		}
		if ((relMask & (1u << (unsigned)obj->getRelationship(o))) == 0) // RW 0x660B85
		{
			return false;
		}
		return !o.isEffectivelyDead();
	});
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(center, d->m_attributeModifierRange, FROM_CENTER_2D, { &affects }, ITER_SORTED_NEAR_TO_FAR))
	{
		victims.push_back(hit.object);
	}
	if (d->m_attributeModifierAffectsSelf)
	{
		// RW 0x897C5A .. 0x897CF7: a horde adds its members, anything else adds itself when the query missed it
		static const int kHorde = CombatNames::kindOf("HORDE"); // template + 0x115 bit 5
		if (obj->isKindOf((unsigned)kHorde))
		{
			if (ContainModuleInterface *c = obj->getContain())
			{
				if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
				{
					for (Object *m : *items)
					{
						if (m)
						{
							victims.push_back(m);
						}
					}
				}
			}
		}
		else if (std::find(victims.begin(), victims.end(), obj) == victims.end())
		{
			victims.push_back(obj);
		}
	}
	applyToVictims(victims);
}

// RW 0x896FD9 (lane SPELL-2, review r1): with a target location, a template, a non-zero ViewObjectRange (+ 0x50) and ViewObjectDuration (+ 0x4C), a GlobalData
// SpecialPowerViewObject name and a template of that name (RW 0x6D1305): the object is made on the caster's player's default team (Player + 0x30C, RW
// 0x6D165E: its constructor and sendObjectCreated draw), put at the location (RW 0x70C201), its shroud clearing range set to ViewObjectRange (RW 0x68C234,
// Object + 0x1B4) and its look forced (RW 0x68C7E9); then its DeletionUpdate's lifetime becomes [duration, duration] (RW 0x88B830: one more draw). The retail
// SuperweaponPing (DeletionUpdate) draws three logic random numbers here. The clearing range and the forced look run since lane DECOMP-1 (BFME2 decomp
// SpecialPowerModuleCreateViewObject.cpp, tier B same-shape for RW 0x896FD9)
void SpecialPowerModule::createViewObject(const Coord3D *loc)
{
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	if (!loc || !t || !(t->m_viewObjectRange != 0.0f) || t->m_viewObjectDuration == 0)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const std::string &name = logic.settings().specialPowerViewObject;
	const ThingTemplate *tt = name.empty() ? nullptr : logic.things().findTemplate(name);
	Player *player = obj->getControllingPlayer();
	if (!tt)
	{
		return;
	}
	if (!player)
	{
		++m_unported; // RW reads Player + 0x30C of a null player (a crash): a power without a controlling player makes nothing here
		return;
	}
	Object *view = logic.newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
	if (!view)
	{
		return;
	}
	view->setPosition(loc);
	view->setShroudClearingRange(t->m_viewObjectRange); // RW 0x68C234 (lane DECOMP-1)
	view->updateShroudNow();                            // RW 0x68C7E9
	if (DeletionUpdate *del = dynamic_cast<DeletionUpdate *>(view->findModule("DeletionUpdate")))
	{
		del->setLifetimeRange(t->m_viewObjectDuration, t->m_viewObjectDuration); // RW 0x896F95 .. (0x88B830)
	}
	++m_viewObjects;
}

// RW 0x8977BF
void SpecialPowerModule::applyToVictims(const std::vector<Object *> &victims)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const SpecialPowerModuleData *d = m_spData;
	const SpecialPowerTemplate *t = getSpecialPowerTemplate();
	if (t && t->getSpecialPowerType() == kSpecialPowerTypeDisguise && obj->testModelCondition(300))
	{
		// RW 0x8977E5 .. 0x897840 (lane HERO-2): SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE on a DISGUISED (model condition 300) object: its SpecialDisguiseUpdate ends
		// the disguise with DisguiseFX (RW 0x8B4702(0); Eowyn's Shield Maiden)
		if (SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(*obj))
		{
			sd->cancelDisguise(false);
		}
	}
	unsigned until = 0;
	if (d->m_antiCategory != 0)
	{
		// RW 0x897859 .. 0x8978BB: frame + the ModifierList's Duration (999999 for an unknown list or a Duration < 1); with no AttributeModifier name the frame
		// itself (lane DECOMP-1: BFME2 decomp SpecialPowerModuleRva0049402B.cpp, tier A for RW 0x8977BF: the duration is added only for a non-empty name)
		until = logic.getFrame();
		if (!d->m_attributeModifier.empty())
		{
			const ModifierListTemplate *list = TheAttributeModifierStore ? TheAttributeModifierStore->find(d->m_attributeModifier) : nullptr;
			const int duration = list ? (int)list->m_duration : 0;
			until += duration >= 1 ? (unsigned)duration : 999999u;
		}
	}
	static const int kIgnoredInGui = CombatNames::kindOf("IGNORED_IN_GUI"); // template + 0x10D bit 7
	const Player *casterPlayer = obj->getControllingPlayer();
	for (Object *v : victims)
	{
		if (v->isKindOf((unsigned)kIgnoredInGui) || (!d->m_attributeModifierAffectsSelf && v == obj))
		{
			continue;
		}
		const bool evil = isEvilPlayer(v->getControllingPlayer());
		if ((d->m_affectGood && evil) || (d->m_affectEvil && !evil))
		{
			continue;
		}
		if (!d->m_affectAllies && v->getControllingPlayer() != casterPlayer)
		{
			continue;
		}
		applyToVictim(*v, until, d->m_antiCategory); // vslot 0x34
		++m_victims;
		++m_applied; // lane HERO-1's counter of the same calls (hashed)
	}
}

// RW 0x89763B (module vslot 0x34)
void SpecialPowerModule::applyToVictim(Object &victim, unsigned until, unsigned antiMask)
{
	GameLogic &logic = victim.logic();
	const SpecialPowerModuleData *d = m_spData;
	if (ExperienceTracker *t = victim.getExperienceTracker())
	{
		// lane HERO-1: RW 0x89765C .. 0x897684: GiveLevels levels while the tracker can still gain one
		for (int n = d->m_giveLevels; n > 0; --n)
		{
			if (t->experienceForNextLevel(nullptr) <= 0) // RW 0x79D11D: the tracker can still gain a level (INFERENCE: the next level exists)
			{
				break;
			}
			t->gainExpForLevel(1, true, false); // RW 0x79DA0A(1, 1, 0)
		}
	}
	if (!d->m_attributeModifier.empty())
	{
		victim.addAttributeModifier(d->m_attributeModifier, -1); // RW 0x68F1A8
	}
	if (until != 0 && antiMask != 0)
	{
		AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(victim.findModule("AttributeModifierPoolUpdate")); // RW 0x68C4A6
		bool reEnableNow = false;
		if (d->m_reEnableAntiCategory)
		{
			const bool evil = isEvilPlayer(victim.getControllingPlayer());
			if (d->m_affectEvil && evil)
			{
				reEnableNow = true;
			}
			if (d->m_affectGood && !evil)
			{
				reEnableNow = true;
			}
			if (!d->m_affectEvil && !d->m_affectGood && getObject()->getRelationship(victim) == ALLIES) // RW 0x68D7AB == 2
			{
				reEnableNow = true;
			}
		}
		if (pool)
		{
			pool->disableCategories(antiMask, reEnableNow ? logic.getFrame() : until); // RW 0x804FCC
		}
		emitPowerFX(logic, "SpecialPower AntiFX", d->m_antiFX, victim);
	}
	static const int kHorde = CombatNames::kindOf("HORDE");
	if (!victim.isKindOf((unsigned)kHorde))
	{
		emitPowerFX(logic, "SpecialPower AttributeModifierFX", d->m_attributeModifierFX, victim); // RW 0x89778F
	}
}

void SpecialPowerModule::effectAtLocation(const Coord3D &, const Object *)
{
}

// lane HERO-1: RW 0x8980A3 / 0x8980EF / 0x89816C: nothing unless forced (options bit 0x40000) or the module is not paused (+ 0x1C < 1) and the object not
// disabled (RW 0x9325B4); then the initiate (RW 0x897E87: the object's first update module whose special power update interface drives the template gets
// it, slot 0, with the target / location and the options); true when the do* goes on to the trigger (no UpdateModuleStartsAttack)
bool SpecialPowerModule::initiate(Object *target, const Coord3D *loc, unsigned options)
{
	Object *obj = getObject();
	if ((options & kOptionUngated) == 0 && (m_pauseCount >= 1 || obj->getDisabledMask() != DISABLEDMASK_NONE))
	{
		return false;
	}
	++m_initiates;
	if (SpecialPowerUpdateInterface *u = SpecialAbilityModules::findUpdate(*obj, getSpecialPowerTemplate()))
	{
		u->initiateIntentToDoSpecialPower(getSpecialPowerTemplate(), target, loc, options, 0);
	}
	// RW 0x897FDE: InitiateFX at the object and the voice (client)
	return !m_spData->m_updateModuleStartsAttack;
}

// the base part of every do* (RW 0x8980A3 calls RW 0x897987 with a null location; the subclass effect follows whatever the gate decided, RW 0x8CC5BE)
bool SpecialPowerModule::baseDo(const Coord3D *loc, const Object *target, unsigned options)
{
	if (!initiate(const_cast<Object *>(target), loc, options))
	{
		return false;
	}
	triggerSpecialPower(loc, target);
	return true;
}

// RW 0x8980A3 + the subclass (e.g. RW 0x8CC5B7: at the object's own position)
void SpecialPowerModule::doSpecialPower(unsigned options)
{
	const Coord3D pos = *getObject()->getPosition();
	baseDo(nullptr, nullptr, options);
	effectAtLocation(pos, nullptr);
}

// RW 0x8980EF + the subclass (at the target's position, RW 0x8CC5A6)
void SpecialPowerModule::doSpecialPowerAtObject(Object *target, unsigned options)
{
	if (!target)
	{
		return;
	}
	if (m_spData->m_adjustVictim)
	{
		++m_unported; // RW 0x898115 .. 0x89813A: AdjustVictim with the target's status 0x26 picks another victim (RW 0x68D30E): S-920
	}
	const Coord3D pos = *target->getPosition();
	baseDo(&pos, target, options);
	effectAtLocation(pos, target);
}

// RW 0x89816C + the subclass
void SpecialPowerModule::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	baseDo(&loc, nullptr, options);
	effectAtLocation(loc, nullptr);
}

void SpecialPowerModule::crc(StateHasher &h) const
{
	h.addU32(m_rechargeLength);
	h.addU32(m_readyFrame);
	h.addI32(m_pauseCount);
	h.addU32(m_pauseFrame);
	h.addFloat(m_pausedPercent);
	h.addBool(m_updateDisabled);
	h.addU32(m_triggers);
	h.addU32(m_applied); // lane HERO-1
}

// ---- OCLSpecialPower ----------------------------------------------------------------------------------------------------------------------
OCLSpecialPower::OCLSpecialPower(Thing *thing, const OCLSpecialPowerModuleData *data)
	: SpecialPowerModule(thing, data)
	, m_data(data)
{
}

// RW 0x8C7393: the first UpgradeOCL whose science the player owns, else OCL
const ObjectCreationList *OCLSpecialPower::pickOCL() const
{
	Player *p = getObject()->getControllingPlayer();
	std::string oclName = m_data->m_ocl;
	if (p && TheScienceStore)
	{
		for (const auto &u : m_data->m_upgradeOCL) // RW 0x8C73A8 .. 0x8C73C5
		{
			const ScienceType st = TheScienceStore->getScienceFromInternalName(u.first);
			if (st != SCIENCE_INVALID && p->science().hasScience(st))
			{
				oclName = u.second;
				break;
			}
		}
	}
	return TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(oclName) : nullptr;
}

void OCLSpecialPower::createOCL(const ObjectCreationList *ocl, const Coord3D &where, bool hasSecondary)
{
	if (!ocl)
	{
		return; // RW 0x8C7690: no list, nothing made
	}
	if (hasSecondary)
	{
		++m_unported; // the OCL's secondary position (RW 0x5F00CA's third argument) is not handed to the CreateObject port (S-530)
	}
	Object *obj = getObject();
	for (Object *o : ocl->create(obj->logic(), obj, where))
	{
		m_created.push_back(o->getID());
	}
}

// RW 0x8C73EF: the base at the object's own position, then the OCL at it through RW 0x5F006B (the primary-object form; the port's create is the same call)
void OCLSpecialPower::doSpecialPower(unsigned options)
{
	m_created.clear();
	const Coord3D pos = *getObject()->getPosition();
	baseDo(&pos, nullptr, options);
	createOCL(pickOCL(), pos, false);
}

// RW 0x8C73D7: doSpecialPowerAtLocation at the target's position (vslot 0x30)
void OCLSpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	if (target)
	{
		const Coord3D pos = *target->getPosition();
		doSpecialPowerAtLocation(pos, options);
	}
}

// RW 0x8C75D8: the base at the location, the OCL (RW 0x8C7393), the CreateLocation switch (RW 0x8C7623, table RW 0xDB32D8), then the UpgradeName grant
void OCLSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	m_created.clear();
	Object *obj = getObject();
	baseDo(&loc, nullptr, options);
	const ObjectCreationList *ocl = pickOCL();
	switch (m_data->m_createLocation)
	{
	case 3: // CREATE_AT_LOCATION
		createOCL(ocl, loc, false);
		break;
	case 4: // USE_OWNER_OBJECT (RW 0x8C7660: RW 0x5F006B with the object, the location twice)
		createOCL(ocl, loc, true);
		break;
	case 5: // CREATE_ABOVE_LOCATION: z + 300.0f (RW 0xBD9E90; an SSE single add)
	{
		Coord3D p = loc;
		p.z = SimMath::addf32(p.z, 300.0f);
		createOCL(ocl, p, false);
		break;
	}
	case 8: // USE_SECONDARY_OBJECT_LOCATION (RW 0x8C78A4)
	{
		if (!m_data->m_nearestSecondaryObjectFilter)
		{
			createOCL(ocl, loc, false); // RW 0x8C78B6: no valid filter: at the location
			break;
		}
		// RW 0x8C7988 .. 0x8C79BB: ThePartitionManager's closest object to the location (range FLT_MAX, RW 0xBD1910; distance type 0) that the filter allows
		// for this player (wrapper RW 0xBE4CC8, flag 1); lane MODULES-2
		Player *p = obj->getControllingPlayer();
		PartitionFilterFn allowed([&](Object &o) { return ObjectFilterMatch::allows(obj->logic(), *m_data->m_nearestSecondaryObjectFilter, o, p); });
		Object *best = obj->logic().partition().getClosestObject(loc, std::numeric_limits<float>::max(), FROM_CENTER_2D, { &allowed });
		createOCL(ocl, loc, best != nullptr); // RW 0x8C76A0: at the location, the found object's position as the secondary
		m_secondaryUsed = best ? best->getID() : (ObjectID)INVALID_ID;
		break;
	}
	default: // 0, 1, 2, 6: the map edge points (TerrainLogic vslots 0x34 / 0x38, RW 0x6EF173); 7: the army spawn waypoints. Not ported (S-530)
		++m_unported;
		break;
	}
	SpecialPowerModules::grantPlayerUpgrades(*obj, m_data->m_upgradeNames);
}

// RW 0x8C76A5 .. 0x8C770A (and PlayerUpgradeSpecialPower RW 0x8CC0E5 .. 0x8CC158): each name through TheUpgradeCenter (RW 0x66F5E5); an unknown name ends
// the loop; a PLAYER upgrade (UpgradeTemplate + 4 == 0) is added COMPLETE to the object's controlling player (RW 0x6AEE22, not silent); OBJECT ones are skipped
void SpecialPowerModules::grantPlayerUpgrades(Object &obj, const std::vector<std::string> &names)
{
	for (const std::string &name : names)
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
		if (!u)
		{
			break;
		}
		if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			if (Player *p = obj.getControllingPlayer())
			{
				p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false);
			}
		}
	}
}

// ---- the object / player side ---------------------------------------------------------------------------------------------------------
SpecialPowerModuleInterface *SpecialPowerModules::findModule(const Object &obj, const SpecialPowerTemplate *t)
{
	if (!t)
	{
		return nullptr; // RW 0x68C272
	}
	for (const auto &m : obj.modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (sp && sp->getSpecialPowerTemplate() == t) // RW 0x8969D1 (the data's template pointer)
		{
			return sp;
		}
	}
	return nullptr;
}

// RW 0x7B1D79
bool SpecialPowerModules::canUseSpecialPower(const Object &obj, const SpecialPowerTemplate *t)
{
	SpecialPowerModuleInterface *sp = findModule(obj, t);
	if (!sp || !sp->requirementsMet())
	{
		return false;
	}
	Player *p = obj.getControllingPlayer();
	if (p && !t->getRequiredSciences().empty())
	{
		bool any = false; // RW 0x6AC22F: one of them is enough
		for (ScienceType st : t->getRequiredSciences())
		{
			any = any || p->science().hasScience(st);
		}
		if (!any)
		{
			return false;
		}
	}
	for (unsigned w = 0; w < 4; ++w) // RW 0x75CDC4: any PreventActivationConditions status on the object
	{
		for (unsigned b = 0; b < 32; ++b)
		{
			if (((t->m_preventActivationConditions[w] >> b) & 1u) && obj.testStatus(w * 32 + b))
			{
				return false;
			}
		}
	}
	return true;
}

namespace
{
SpecialPowerModuleInterface *usable(Object &obj, const SpecialPowerTemplate *t, bool force)
{
	if (obj.getDisabledMask() != 0) // RW 0x68E760
	{
		return nullptr;
	}
	if (!force && !SpecialPowerModules::canUseSpecialPower(obj, t))
	{
		return nullptr;
	}
	return SpecialPowerModules::findModule(obj, t); // lane HERO-2: any module with the interface (WeaponModeSpecialPowerUpdate's base RW 0x991660)
}
} // namespace

bool SpecialPowerModules::doSpecialPowerAtLocation(Object &obj, const SpecialPowerTemplate *t, const Coord3D &loc, unsigned options, bool force)
{
	SpecialPowerModuleInterface *m = usable(obj, t, force);
	if (!m)
	{
		return false;
	}
	m->doSpecialPowerAtLocation(loc, options);
	return true;
}

bool SpecialPowerModules::doSpecialPowerAtObject(Object &obj, const SpecialPowerTemplate *t, Object *target, unsigned options, bool force)
{
	SpecialPowerModuleInterface *m = usable(obj, t, force);
	if (!m)
	{
		return false;
	}
	m->doSpecialPowerAtObject(target, options);
	return true;
}

bool SpecialPowerModules::doSpecialPower(Object &obj, const SpecialPowerTemplate *t, unsigned options, bool force)
{
	SpecialPowerModuleInterface *m = usable(obj, t, force);
	if (!m)
	{
		return false;
	}
	m->doSpecialPower(options);
	return true;
}

// RW 0x6B183D (part)
Object *SpecialPowerModules::createSpellBook(GameLogic &logic, Player &player)
{
	const PlayerTemplate *pt = player.getPlayerTemplate();
	Team *team = player.getDefaultTeam();
	if (!pt || !team)
	{
		return nullptr;
	}
	const std::string &name = player.science().mode().skirmishOrMultiplayer ? pt->m_spellBookMp : pt->m_spellBook; // RW 0x6B1872 -> 0x625456
	if (name.empty())
	{
		return nullptr;
	}
	const ThingTemplate *tt = logic.things().findTemplate(name);
	if (!tt)
	{
		return nullptr; // RW 0x6B18B3
	}
	Object *book = logic.newObject(tt, team, ObjectStatusMaskType{});
	if (!book)
	{
		return nullptr;
	}
	player.science().setSpellBookId(book->getID()); // RW 0x6B18F1
	book->friend_onBuildComplete();                 // RW 0x6B18F7 -> 0x68D252
	return book;
}

Object *SpecialPowerModules::findSpellBookObject(const GameLogic &logic, const Player &player)
{
	if (player.science().spellBookId() != 0)
	{
		return logic.findObjectByID(player.science().spellBookId());
	}
	static const int kSpellBook = CombatNames::kindOf("SPELL_BOOK");
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOf((unsigned)kSpellBook) && o->getControllingPlayer() == &player)
		{
			return o;
		}
	}
	return nullptr;
}

// RW 0x6AD0F8
Object *SpecialPowerModules::getSpellBookObject(GameLogic &logic, Player &player)
{
	if (player.science().spellBookId() == 0)
	{
		static const int kSpellBook = CombatNames::kindOf("SPELL_BOOK"); // template + 0x117 bit 3 (RW 0x6AAE58)
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->isKindOf((unsigned)kSpellBook) && o->getControllingPlayer() == &player)
			{
				player.science().setSpellBookId(o->getID());
				break;
			}
		}
	}
	return player.science().spellBookId() ? logic.findObjectByID(player.science().spellBookId()) : nullptr;
}

void SpecialPowerModules::installScienceHooks(GameLogic &logic)
{
	PlayerList &players = logic.players();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		Player *p = players.getNthPlayer(i);
		p->science().setScienceAddedHook([&logic, p](ScienceType st) {
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() != p)
				{
					continue;
				}
				for (const auto &m : o->modules())
				{
					SpecialPowerModuleInterface *sp = m->getSpecialPower();
					const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
					if (!t)
					{
						continue;
					}
					const ScienceVec &req = t->getRequiredSciences();
					if (std::find(req.begin(), req.end(), st) == req.end())
					{
						continue;
					}
					sp->onScienceAcquired();                 // RW 0x6AE268 (slot 0x1C)
					sp->setReadyFrame(logic.getFrame());     // RW 0x6AE29F (GameLogic + 0x114 == 3, S-525)
				}
			}
		});
	}
}

void SpecialPowerModules::registerAll(ModuleFactory &modules)
{
	modules.bindTypedData<SpecialPowerModuleData>("SpecialPowerModule", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<PlayerHealSpecialPowerModuleData>("PlayerHealSpecialPower", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<OCLSpecialPowerModuleData>("OCLSpecialPower", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SpecialPowerModule", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SpecialPowerModuleData *typed = dynamic_cast<const SpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("SpecialPowerModule: the module data is not typed");
		}
		return std::make_unique<SpecialPowerModule>(thing, typed);
	});
	modules.bindModuleProc("PlayerHealSpecialPower", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const PlayerHealSpecialPowerModuleData *typed = dynamic_cast<const PlayerHealSpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("PlayerHealSpecialPower: the module data is not typed (PlayerHealSpecialPowerModuleData)");
		}
		return std::make_unique<PlayerHealSpecialPower>(thing, typed);
	});
	modules.bindModuleProc("OCLSpecialPower", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const OCLSpecialPowerModuleData *typed = dynamic_cast<const OCLSpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("OCLSpecialPower: the module data is not typed");
		}
		return std::make_unique<OCLSpecialPower>(thing, typed);
	});
	SpellBookPowers::registerAll(modules); // lane SPELL-2: the other spell book power classes
	SpellEffectModules::registerAll(modules); // lane SPELL-2: DeletionUpdate, FireWeaponUpdate (the effect objects)
}
