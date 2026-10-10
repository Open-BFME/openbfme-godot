// OpenBFME. GPL-3.0.
// See GameLogic/Module/SpecialAbilityModules.h. DONOR: ZH GameLogic/Object/Update/SpecialAbilityUpdate.cpp (the state machine: update :192,
// initiateIntentToDoSpecialPower, startPreparation, triggerAbilityEffect, onExit); every step below is RotWK's, from the addresses named.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/PartitionManager.h"

#include "Common/GameCommon.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/FXList.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Construction.h"
#include "Common/ScoreKeeper.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ScriptEngine/LuaScriptEvents.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace
{
// the indices the binary pushes (names: GameLogic/BitFlagNames.cpp)
constexpr unsigned kStatusNoAutoAcquire = 0x17;   // NO_AUTO_ACQUIRE
constexpr unsigned kStatusUsingAbility = 0x18;    // USING_ABILITY
constexpr unsigned kStatusPackingUsing = 0x46;    // SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING
constexpr unsigned kStatusIgnoreAI = 0x4A;        // IGNORE_AI_COMMAND
constexpr int kMcMoving = 0x3D;
constexpr int kMcPacking = 0x5E;
constexpr int kMcPreparing = 0x5F;
constexpr int kMcUnpacking = 0x60;
constexpr int kMcPackingType1 = 0x61, kMcPackingType2 = 0x62, kMcPackingType3 = 0x63;
constexpr int kMcPackingType4 = 0x249, kMcPackingType5 = 0x24A, kMcPackingType6 = 0x24B;
constexpr int kMcRaisingFlag = 0x76;
constexpr int kMcStartCapture = 0x6E;  // lane HERO-2: START_CAPTURE (+ 0x119 bit 6)
constexpr int kMcCancelCapture = 0x6F; // CANCEL_CAPTURE
constexpr int kMcCapturing = 0x70;     // CAPTURING
constexpr int kMcCaptured = 0x77;      // CAPTURED
constexpr int kMcPostCollapse = 60;    // POST_COLLAPSE (+ 0x110 bit 28)
constexpr int kMcUser3 = 0xBA;
constexpr int kMcMounted = 0xD6;
constexpr int kMcHero = 0xDC;
constexpr int kMcWeaponsetToggle1 = 0x12D;
constexpr unsigned kDisabledHeld = 3;
constexpr int kWeaponSetMounted = 0x15;           // RW 0x691059(0x15): WeaponSet MOUNTED
constexpr int kWeaponSetHeroMode = 0x1B;          // WEAPONSET_HERO_MODE
constexpr int kArmorSetMounted = 6;               // RW 0x68BE59(6): ArmorSet MOUNTED
constexpr int kLocomotorSetMounted = 7;           // AI vslot 0x238(7)

// RW 0xDAE0E0: the names of RequiredConditions / RejectedConditions and the model condition each bit tests (RW 0x851551)
const char *const kConditionNames[] = { "MOUNTED", "WEAPON_TOGGLE", "MOVING", nullptr };
const int kConditionBits[] = { kMcMounted, kMcWeaponsetToggle1, kMcMoving };

void parseSpecialPowerTemplate(INI *ini, void *instance, void *, const void *)
{
	SpecialAbilityUpdateModuleData *d = static_cast<SpecialAbilityUpdateModuleData *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken(); // RW 0x73B22F
	d->m_specialPowerTemplate = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName) : nullptr;
}

int modelConditionByName(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	throw INIException(3, "unknown model condition %s", name); // RW 0x4B3B5B
}

// RW 0x851412: AnimState:<model condition> AnimTime:<duration> [TriggerTime:<duration>] into (+0, +4, +8)
template <int Which>
void parseAnimAndDuration(INI *ini, void *instance, void *, const void *)
{
	SpecialAbilityUpdateModuleData *d = static_cast<SpecialAbilityUpdateModuleData *>(instance);
	int &cond = Which == 0 ? d->m_customAnimCondition : d->m_grabAnimCondition;
	unsigned &anim = Which == 0 ? d->m_customAnimFrames : d->m_grabAnimFrames;
	unsigned &trig = Which == 0 ? d->m_customAnimTriggerFrames : d->m_grabTriggerFrames;
	const char *t = ini->getNextTokenOrNull(" \t=:");
	if (!t || std::strcmp(t, "AnimState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdate::CustomAnimAndDuration");
	}
	cond = modelConditionByName(ini->getNextToken(" \t=:"));
	t = ini->getNextTokenOrNull(" \t=:");
	if (!t || std::strcmp(t, "AnimTime") != 0)
	{
		throw INIException(3, "AnimTime expected for SpecialAbilityUpdate::CustomAnimAndDuration");
	}
	INI::parseDurationUnsignedInt(ini, instance, &anim, nullptr); // RW 0x73A429
	t = ini->getNextTokenOrNull(" \t=:");
	if (t && std::strcmp(t, "TriggerTime") == 0)
	{
		INI::parseDurationUnsignedInt(ini, instance, &trig, nullptr);
	}
}

// RW 0x8514ED: ModelConditionState:<name>
void parseTriggerModelCondition(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextTokenOrNull(" \t=:");
	if (!t || std::strcmp(t, "ModelConditionState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdate::TriggerModelCondition");
	}
	*static_cast<int *>(store) = modelConditionByName(ini->getNextToken(" \t=:"));
}

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

// RW 0x73A302: "None" stores none, any other name must be an FXList
void parseFX(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (name == "None" || name == "NONE" || name == "none")
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

#define SA_OFF(m) (int)offsetof(SpecialAbilityUpdateModuleData, m)
const FieldParse kSpecialAbility[] = { // RW 0xC55F40
	{ "SpecialPowerTemplate", parseSpecialPowerTemplate, nullptr, 0 },
	{ "StartAbilityRange", INI::parseReal, nullptr, SA_OFF(m_startAbilityRange) },
	{ "AbilityAbortRange", INI::parseReal, nullptr, SA_OFF(m_abilityAbortRange) },
	{ "PreparationTime", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_preparationFrames) },
	{ "PersistentPrepTime", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_persistentPrepFrames) },
	{ "PersistentCount", INI::parseInt, nullptr, SA_OFF(m_persistentCount) },
	{ "PackTime", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_packFrames) },
	{ "UnpackTime", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_unpackFrames) },
	{ "PreTriggerUnstealthTime", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_preTriggerUnstealthFrames) },
	{ "SkipPackingWithNoTarget", INI::parseBool, nullptr, SA_OFF(m_skipPackingWithNoTarget) },
	{ "PackUnpackVariationFactor", INI::parseReal, nullptr, SA_OFF(m_packUnpackVariationFactor) },
	{ "ParalyzeDurationWhenCompleted", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_paralyzeWhenCompleted) },
	{ "ParalyzeDurationWhenAborted", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_paralyzeWhenAborted) },
	{ "SpecialObject", INI::parseAsciiString, nullptr, SA_OFF(m_specialObject) },
	{ "SpecialObjectAttachToBone", INI::parseAsciiString, nullptr, SA_OFF(m_specialObjectAttachToBone) },
	{ "MaxSpecialObjects", INI::parseUnsignedInt, nullptr, SA_OFF(m_maxSpecialObjects) },
	{ "SpecialObjectsPersistent", INI::parseBool, nullptr, SA_OFF(m_specialObjectsPersistent) },
	{ "EffectDuration", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_effectDuration) },
	{ "EffectValue", INI::parseInt, nullptr, SA_OFF(m_effectValue) },
	{ "EffectRange", INI::parseReal, nullptr, SA_OFF(m_effectRange) },
	{ "UniqueSpecialObjectTargets", INI::parseBool, nullptr, SA_OFF(m_uniqueSpecialObjectTargets) },
	{ "SpecialObjectsPersistWhenOwnerDies", INI::parseBool, nullptr, SA_OFF(m_specialObjectsPersistWhenOwnerDies) },
	{ "AlwaysValidateSpecialObjects", INI::parseBool, nullptr, SA_OFF(m_alwaysValidateSpecialObjects) },
	{ "FlipOwnerAfterPacking", INI::parseBool, nullptr, SA_OFF(m_flipOwnerAfterPacking) },
	{ "FlipOwnerAfterUnpacking", INI::parseBool, nullptr, SA_OFF(m_flipOwnerAfterUnpacking) },
	{ "FleeRangeAfterCompletion", INI::parseReal, nullptr, SA_OFF(m_fleeRangeAfterCompletion) },
	{ "DisableFXParticleSystem", INI::parseAsciiString, nullptr, SA_OFF(m_disableFXParticleSystem) },
	{ "DoCaptureFX", INI::parseBool, nullptr, SA_OFF(m_doCaptureFX) },
	{ "PackSound", INI::parseAsciiString, nullptr, SA_OFF(m_packSound) },
	{ "UnpackSound", INI::parseAsciiString, nullptr, SA_OFF(m_unpackSound) },
	{ "PrepSoundLoop", INI::parseAsciiString, nullptr, SA_OFF(m_prepSoundLoop) },
	{ "TriggerSound", INI::parseAsciiString, nullptr, SA_OFF(m_triggerSound) },
	{ "ActiveLoopSound", INI::parseAsciiString, nullptr, SA_OFF(m_activeLoopSound) },
	{ "LoseStealthOnTrigger", INI::parseBool, nullptr, SA_OFF(m_loseStealthOnTrigger) },
	{ "AwardXPForTriggering", INI::parseInt, nullptr, SA_OFF(m_awardXPForTriggering) },
	{ "SkillPointsForTriggering", INI::parseInt, nullptr, SA_OFF(m_skillPointsForTriggering) },
	{ "ApproachRequiresLOS", INI::parseBool, nullptr, SA_OFF(m_approachRequiresLOS) },
	{ "ChargeAttackSpeedBoost", INI::parseBool, nullptr, SA_OFF(m_chargeAttackSpeedBoost) },
	{ "CustomAnimAndDuration", parseAnimAndDuration<0>, nullptr, 0 },
	{ "GrabPassengerAnimAndDuration", parseAnimAndDuration<1>, nullptr, 0 },
	{ "GrabPassengerHealGainPercent", INI::parsePercentToReal, nullptr, SA_OFF(m_grabPassengerHealGainPercent) },
	{ "UnpackingVariation", INI::parseInt, nullptr, SA_OFF(m_unpackingVariation) },
	{ "MustFinishAbility", INI::parseBool, nullptr, SA_OFF(m_mustFinishAbility) },
	{ "FreezeAfterTriggerDuration", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_freezeAfterTriggerFrames) },
	{ "DisableWhenWearingTheRing", INI::parseBool, nullptr, SA_OFF(m_disableWhenWearingTheRing) },
	{ "RequiredConditions", INI::parseBitString32, kConditionNames, SA_OFF(m_requiredConditions) },
	{ "RejectedConditions", INI::parseBitString32, kConditionNames, SA_OFF(m_rejectedConditions) },
	{ "ContactPointOverride", INI::parseAsciiString, nullptr, SA_OFF(m_contactPointOverride) },
	{ "TriggerAttributeModifier", INI::parseAsciiString, nullptr, SA_OFF(m_triggerAttributeModifier) },
	{ "AttributeModifierDuration", INI::parseDurationUnsignedInt, nullptr, SA_OFF(m_attributeModifierDuration) },
	{ "KillAttributeModifierOnExit", INI::parseBool, nullptr, SA_OFF(m_killAttributeModifierOnExit) },
	{ "KillAttributeModifierOnRejected", INI::parseBool, nullptr, SA_OFF(m_killAttributeModifierOnRejected) },
	{ "Instant", INI::parseBool, nullptr, SA_OFF(m_instant) },
	{ "NeedCollisionBeforeTrigger", INI::parseBool, nullptr, SA_OFF(m_needCollisionBeforeTrigger) },
	{ "ChainedButton", INI::parseAsciiString, nullptr, SA_OFF(m_chainedButton) },
	{ "SuppressForHordes", INI::parseBool, nullptr, SA_OFF(m_suppressForHordes) },
	{ "ApproachUntilMembersInRange", INI::parseBool, nullptr, SA_OFF(m_approachUntilMembersInRange) },
	{ "IgnoreFacingCheck", INI::parseBool, nullptr, SA_OFF(m_ignoreFacingCheck) },
	{ "TriggerModelCondition", parseTriggerModelCondition, nullptr, SA_OFF(m_triggerModelCondition) },
	{ "TriggerModelConditionDuration", INI::parseReal, nullptr, SA_OFF(m_triggerModelConditionDuration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SA_OFF

#define WF_OFF(m) (int)offsetof(WeaponFireSpecialAbilityUpdateModuleData, m)
const FieldParse kWeaponFire[] = { // RW 0xC05408
	{ "SpecialWeapon", INI::parseAsciiString, nullptr, WF_OFF(m_specialWeapon) },
	{ "WhichSpecialWeapon", INI::parseInt, nullptr, WF_OFF(m_whichSpecialWeapon) },
	{ "SkipContinue", INI::parseBool, nullptr, WF_OFF(m_skipContinue) },
	{ "BusyForDuration", INI::parseDurationUnsignedInt, nullptr, WF_OFF(m_busyForFrames) },
	{ "NeedLivingTargets", INI::parseBool, nullptr, WF_OFF(m_needLivingTargets) },
	{ "PlayWeaponPreFireFX", INI::parseBool, nullptr, WF_OFF(m_playWeaponPreFireFX) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef WF_OFF

#define TM_OFF(m) (int)offsetof(ToggleMountedSpecialAbilityUpdateModuleData, m)
const FieldParse kToggleMounted[] = { // RW 0xC05A18
	{ "OpacityTarget", INI::parseReal, nullptr, TM_OFF(m_opacityTarget) },
	{ "TriggerInstantlyOnCreate", INI::parseBool, nullptr, TM_OFF(m_triggerInstantlyOnCreate) },
	{ "CancelDisguiseWhenDismounting", INI::parseBool, nullptr, TM_OFF(m_cancelDisguiseWhenDismounting) },
	{ "MountedTemplate", INI::parseAsciiString, nullptr, TM_OFF(m_mountedTemplate) },
	{ "SynchronizeTimerOnSpecialPower", INI::parseAsciiStringVector, nullptr, TM_OFF(m_synchronizeTimerOnSpecialPower) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef TM_OFF

#define HM_OFF(m) (int)offsetof(HeroModeSpecialAbilityUpdateModuleData, m)
const FieldParse kHeroMode[] = { // RW 0xC052F0
	{ "HeroAttributeModifier", INI::parseAsciiString, nullptr, HM_OFF(m_heroAttributeModifier) },
	{ "HeroEffectDuration", INI::parseDurationUnsignedInt, nullptr, HM_OFF(m_heroEffectFrames) },
	{ "UseUSERModelcondition", INI::parseBool, nullptr, HM_OFF(m_useUserModelCondition) },
	{ "StopUnitBeforeActivating", INI::parseBool, nullptr, HM_OFF(m_stopUnitBeforeActivating) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef HM_OFF

#define LG_OFF(m) (int)offsetof(LevelGrantSpecialPowerModuleData, m)
const FieldParse kLevelGrant[] = { // RW 0xC73550
	{ "Experience", INI::parseInt, nullptr, LG_OFF(m_experience) },
	{ "RadiusEffect", INI::parseReal, nullptr, LG_OFF(m_radiusEffect) },
	{ "LevelFX", parseFX, nullptr, LG_OFF(m_levelFX) },
	{ "AcceptanceFilter", parseFilter, nullptr, LG_OFF(m_acceptanceFilter) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef LG_OFF

#define MC_OFF(m) (int)offsetof(ModelConditionSpecialAbilityUpdateModuleData, m)
// lane HERO-2: RW 0xC05230 (the data constructor RW 0x894467, its factory RW 0x64D384 / 0x64D32E); HERO-1 had read RW 0xC05AF8, which is
// SummonReplacementSpecialAbilityUpdate's table (these five rows and its own five)
const FieldParse kModelCondition[] = { // RW 0xC05230
	{ "WhichSpecialPower", INI::parseInt, nullptr, MC_OFF(m_whichSpecialPower) },
	{ "GenerateTerror", INI::parseBool, nullptr, MC_OFF(m_generateTerror) },
	{ "GenerateUncontrollableFear", INI::parseBool, nullptr, MC_OFF(m_generateUncontrollableFear) },
	{ "EmotionPulseRadius", INI::parseReal, nullptr, MC_OFF(m_emotionPulseRadius) },
	{ "ObjectFilter", parseFilter, nullptr, MC_OFF(m_objectFilter) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef MC_OFF

#define SD_OFF(m) (int)offsetof(SpecialDisguiseUpdateModuleData, m)
const FieldParse kSpecialDisguise[] = { // RW 0xC05C10 (lane HERO-2)
	{ "OpacityTarget", INI::parseReal, nullptr, SD_OFF(m_opacityTarget) },
	{ "TriggerInstantlyOnCreate", INI::parseBool, nullptr, SD_OFF(m_triggerInstantlyOnCreate) },
	{ "DisguiseAsTemplate", INI::parseAsciiString, nullptr, SD_OFF(m_disguiseAsTemplate) },
	{ "DisguisedAsTemplate_EnemyPerspective", INI::parseAsciiString, nullptr, SD_OFF(m_disguisedAsTemplateEnemy) },
	{ "DisguiseFX", parseFX, nullptr, SD_OFF(m_disguiseFX) },
	{ "ForceMountedWhenDisguising", INI::parseBool, nullptr, SD_OFF(m_forceMountedWhenDisguising) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SD_OFF

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

bool isHorde(const Object &o)
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	return kHorde >= 0 && o.isKindOf((unsigned)kHorde);
}

bool isZero(const Coord3D &c)
{
	return c.x == 0.0f && c.y == 0.0f && c.z == 0.0f;
}

// the model condition of UnpackingVariation (RW 0x853C64: 1 .. 3 PACKING_TYPE_1 .. 3, 4 .. 6 PACKING_TYPE_4 .. 6)
int unpackingVariationCondition(int v)
{
	switch (v)
	{
	case 1: return kMcPackingType1;
	case 2: return kMcPackingType2;
	case 3: return kMcPackingType3;
	case 4: return kMcPackingType4;
	case 5: return kMcPackingType5;
	case 6: return kMcPackingType6;
	default: return -1;
	}
}

// RW 0x68D607 Object::clearAndSetModelConditionFlags with the named bits
struct ModelConditionBitsHelper
{
	static void clearAndSet(Object &o, std::initializer_list<int> clear, std::initializer_list<int> set)
	{
		Object::ModelConditionBits c{}, s{};
		for (int b : clear)
		{
			c[(size_t)b >> 5] |= 1u << (b & 31);
		}
		for (int b : set)
		{
			s[(size_t)b >> 5] |= 1u << (b & 31);
		}
		o.clearAndSetModelConditionFlags(c, s);
	}
};

void setCondition(Object &o, int bit, bool on)
{
	if (bit >= 0 && o.testModelCondition(bit) != on)
	{
		o.setModelConditionState(bit, on);
	}
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------------------------------------------------------------
void SpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSpecialAbility);
}

void WeaponFireSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kWeaponFire);
}

void ToggleMountedSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kToggleMounted);
}

void HeroModeSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kHeroMode);
}

void LevelGrantSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kLevelGrant);
}

void ModelConditionSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kModelCondition);
}

void SpecialDisguiseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kSpecialDisguise);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
SpecialAbilityUpdate::SpecialAbilityUpdate(Thing *thing, const SpecialAbilityUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x851D42: RW 0x850C32(object, 0x3FFFFFFF)
}

unsigned SpecialAbilityUpdate::now() const
{
	return getObject()->logic().getFrame();
}

Object *SpecialAbilityUpdate::target() const
{
	return m_targetID != INVALID_ID ? getObject()->logic().findObjectByID(m_targetID) : nullptr;
}

// RW 0x851362: the special power module of the object for the data's template
SpecialPowerModule *SpecialAbilityUpdate::mySpecialPowerModule() const
{
	return dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*getObject(), m_data->m_specialPowerTemplate));
}

void SpecialAbilityUpdate::aiIdleFromAI()
{
	if (AIUpdateInterface *ai = getObject()->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A(2)
	}
}

// RW 0x852E8F
void SpecialAbilityUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *targetObj, const Coord3D *loc, unsigned options, int extra)
{
	if (t != m_data->m_specialPowerTemplate)
	{
		return;
	}
	Object *obj = getObject();
	m_targetID = INVALID_ID;
	m_targetPos = Coord3D{};
	m_effectExpiry = 0;
	m_prepFrames = 0;
	m_triggerCount = 0;
	m_packFramesLeft = 0;
	m_packingState = 3; // PACKED
	m_facingIssued = m_facingDone = m_withinRange = m_chargeSpeed = m_chargeMove = false;
	m_options = options;
	m_persistentLeft = m_data->m_persistentCount;
	if (m_persistentLeft > 0)
	{
		m_persistentLeft -= 1;
	}
	// RW 0x851BCF: the special objects are validated (none are made: S-860)
	if (!targetObj)
	{
		if (!loc)
		{
			if (m_data->m_instant)
			{
				// RW 0x852EEA: an instant ability triggers here, the power module's trigger (vslot 0x38) runs and the ability ends
				triggerAbilityEffect();
				if (SpecialPowerModule *spm = mySpecialPowerModule())
				{
					spm->triggerFromUpdate();
				}
				finishAbility(false, true);
				return;
			}
		}
		else
		{
			m_targetPos = *loc;
			m_extra = extra;
		}
	}
	else
	{
		m_targetID = targetObj->getID();
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	obj->setStatus(kStatusNoAutoAcquire, true); // RW 0x62684D(0x17, 1)
	// RW 0x852F48 .. 0x852FB6: the AI's current goal is dropped (vslot 0x188, RW 0x76A252), AI + 0x3CA = 1, and the AI goes idle by the AI (twice when the last
	// command came from a player who controls a computer player: the same effect here)
	ai->aiIdle(CMD_FROM_AI);
	m_noTarget = !targetObj && !loc;
	if (m_data->m_unpackFrames == 0 || (m_noTarget && m_data->m_skipPackingWithNoTarget))
	{
		m_packingState = 4; // UNPACKED
	}
	m_active = true;
	m_approachCounter = 0;
	// RW 0x852FF9: every other active special power update of the object is finished
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		SpecialAbilityUpdate *other = dynamic_cast<SpecialAbilityUpdate *>(m.get());
		if (other && other != this && other->isActive())
		{
			other->finishAbility(false, true);
		}
	}
	setWakeFrame(obj, (UpdateSleepTime)1); // RW 0x850C32(object, 1)
}

// RW 0x851551
bool SpecialAbilityUpdate::conditionsAllow() const
{
	const Object *obj = getObject();
	for (int i = 0; i < 3; ++i)
	{
		const bool set = obj->testModelCondition(kConditionBits[i]);
		if ((m_data->m_requiredConditions >> i) & 1u)
		{
			if (!set)
			{
				return false;
			}
		}
	}
	for (int i = 0; i < 3; ++i)
	{
		if (((m_data->m_rejectedConditions >> i) & 1u) && obj->testModelCondition(kConditionBits[i]))
		{
			return false;
		}
	}
	// RW 0x8515E4: DisableWhenWearingTheRing (+ 0xB6) with the object's StealthUpdate (RW 0x68FBD3) wearing the One Ring (+ 0x31, lane STEALTH-2)
	if (m_data->m_disableWhenWearingTheRing)
	{
		if (const StealthUpdate *st = StealthUpdate::of(*obj))
		{
			if (st->ringWorn())
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x85266D
UpdateSleepTime SpecialAbilityUpdate::calcSleep() const
{
	if (m_active || m_data->m_alwaysValidateSpecialObjects || m_effectExpiry != 0) // RW 0x85266D tests the effect expiry (+ 0x2C)
	{
		return (UpdateSleepTime)1;
	}
	if (m_data->m_requiredConditions == 0 && m_data->m_rejectedConditions == 0)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	return (UpdateSleepTime)1;
}

bool SpecialAbilityUpdate::needToUnpack() const // RW 0x85133C
{
	return m_packingState == 3 && (!m_data->m_skipPackingWithNoTarget || !m_noTarget) && m_data->m_unpackFrames != 0;
}

bool SpecialAbilityUpdate::needToPack() const // RW 0x851316
{
	return m_packingState == 4 && (!m_data->m_skipPackingWithNoTarget || !m_noTarget) && m_data->m_packFrames != 0;
}

bool SpecialAbilityUpdate::persistentTrigger() const // RW 0x852465 (the garrison enum 0x27 branch: S-860)
{
	return m_persistentLeft != 0 && m_data->m_persistentPrepFrames != 0;
}

void SpecialAbilityUpdate::restartPersistent() // RW 0x8524BC
{
	if (m_persistentLeft > 0)
	{
		--m_persistentLeft;
	}
	m_prepFrames = m_data->m_persistentPrepFrames;
}

// RW 0x85260A
void SpecialAbilityUpdate::endPreparation()
{
	Object *obj = getObject();
	obj->setStatus(kStatusUsingAbility, false);
	// RW 0x852617: the PrepSoundLoop stops (client)
	setCondition(*obj, kMcPreparing, false); // RW 0x5E3B79(PREPARING)
}

void SpecialAbilityUpdate::setUnpackingVariation()
{
	setCondition(*getObject(), unpackingVariationCondition(m_data->m_unpackingVariation), true);
}

// RW 0x851C48
void SpecialAbilityUpdate::applyCustomAnim()
{
	if (m_data->m_customAnimCondition < 0)
	{
		return;
	}
	if (m_data->m_customAnimFrames == 0)
	{
		setCondition(*getObject(), m_data->m_customAnimCondition, true);
	}
	else
	{
		getObject()->setSpecialModelConditionState(m_data->m_customAnimCondition, m_data->m_customAnimFrames); // RW 0x68B581
	}
}

// RW 0x85207A
bool SpecialAbilityUpdate::withinStartAbilityRange()
{
	if (m_withinRange)
	{
		return true;
	}
	const Object *obj = getObject();
	float dist2 = 0.0f;
	if (m_targetID == INVALID_ID)
	{
		if (isZero(m_targetPos))
		{
			return m_data->m_specialPowerTemplate == nullptr || m_data->m_specialPowerTemplate->m_type != 0x88; // RW 0x852170
		}
		// RW 0x6CA525: max(0, dist2D - own radius)^2 (the enum 0x5B / 0x38 branches use the AI's distance: S-860)
		dist2 = CircleDistanceSquaredToPoint(*obj->getPosition(), CombatQueries::boundingCircleRadius(*obj), m_targetPos);
	}
	else
	{
		const Object *t = target();
		if (!t)
		{
			m_targetID = INVALID_ID; // RW 0x852175
			return false;
		}
		// RW 0x852127: ContactPointOverride (RW 0x690BD2, the bone position: S-860) falls back to the edge distance; a source HORDE with
		// ApproachUntilMembersInRange uses its formation radius (S-860); a structure target the box distance RW 0x68F430 (its bounding circle, S-183)
		dist2 = CombatQueries::edgeDistanceSquared2D(*obj, *obj->getPosition(), *t, *t->getPosition());
	}
	if (dist2 > SimMath::mulf32(m_data->m_startAbilityRange, m_data->m_startAbilityRange))
	{
		return false;
	}
	// RW 0x852244 .. 0x8522D7: NeedCollisionBeforeTrigger (no retail use) and ApproachRequiresLOS (the line of sight RW 0x6613D3 / 0x6616AC: clear, S-859)
	return true;
}

// RW 0x854DF7
UpdateSleepTime SpecialAbilityUpdate::update()
{
	Object *obj = getObject();
	const unsigned frame = now();
	if (obj->isEffectivelyDead()) // RW 0x854E0C: Object + 0x458 bit 0
	{
		finishAbility(true, true);
		return calcSleep();
	}
	if (m_effectExpiry != 0 && m_effectExpiry < frame)
	{
		onEffectExpired(); // slot 0x48
	}
	if (m_customAnimFrame != 0 && m_customAnimFrame < frame)
	{
		applyCustomAnim();
		m_customAnimFrame = 0;
	}
	if (!conditionsAllow())
	{
		finishAbility(false, true);
		if (m_data->m_killAttributeModifierOnRejected && !m_data->m_triggerAttributeModifier.empty())
		{
			obj->removeAttributeModifier(m_data->m_triggerAttributeModifier); // RW 0x68F259
		}
		calcSleep();
		return UPDATE_SLEEP_FOREVER;
	}
	if (!m_active)
	{
		return calcSleep();
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		finishAbility(false, true);
		return calcSleep();
	}
	if (Object *t = target())
	{
		m_targetPos = *t->getPosition();
	}
	// RW 0x854EAA: a command that did not come from the AI ends the ability (Object + 0xA0 bit 6 also keeps it: not identified, S-859)
	if (ai->lastCommandSource() != CMD_FROM_AI)
	{
		finishAbility(false, true);
		return calcSleep();
	}
	// RW 0x854EE0 .. 0x854F2F: the enum 0x2A (charge) branch: S-860
	if (handlePackingProcessing())
	{
		return calcSleep();
	}
	const Object *t = m_targetID != INVALID_ID ? target() : nullptr;
	// RW 0x854FB8 .. 0x855005 (the capture enums, lane HERO-2): a target of the object's team is captured: done; a dead one too unless it is a CAPTURABLE
	// in POST_COLLAPSE (ruins stay capturable); the other abilities end at a dead target. INFERENCE (S-1225): a target that is gone ends it too (HERO-1)
	const int updType = m_data->m_specialPowerTemplate ? m_data->m_specialPowerTemplate->m_type : 0;
	bool targetDone = m_targetID != INVALID_ID && !t;
	if (t)
	{
		if (updType == 0x1D || updType == 0x1A)
		{
			static const int kCapturable = ObjectTemplateInfoBuilder::kindOfIndex("CAPTURABLE");
			targetDone = t->getTeam() == obj->getTeam() ||
				(t->isEffectivelyDead() && !(kCapturable >= 0 && t->isKindOf((unsigned)kCapturable) && t->testModelCondition(kMcPostCollapse)));
		}
		else
		{
			targetDone = t->isEffectivelyDead();
		}
	}
	if (targetDone && !m_data->m_mustFinishAbility)
	{
		// RW 0x854FA0 .. 0x855011: the target is gone or dead (the capture enums' own tests: S-860)
		ai->aiIdle(CMD_FROM_AI);
		finishAbility(false, true);
		return calcSleep();
	}
	if (m_prepFrames == 0)
	{
		if (!withinStartAbilityRange())
		{
			if (ai->isIdle()) // AI vslot 0x1B8
			{
				approachTarget();
			}
			return calcSleep();
		}
		m_withinRange = true;
		if (!m_data->m_ignoreFacingCheck)
		{
			// RW 0x851384 / 0x8513B8 / 0x854D01: the facing. The binary idles the AI, issues the face command (AI command RW 0x7C81B9 / 0x7C821E) and waits for
			// the AI's idle (vslot 0x1B8) once the turn ends. The face command is not ported (S-859): the turn takes no time here, so the check passes on the
			// frame after the idle (waiting for the AI's idle instead never ends for a hero pushed around by the target it touches)
			if (!m_facingIssued)
			{
				ai->aiIdle(CMD_FROM_AI);
				m_facingIssued = true;
				return calcSleep();
			}
			m_facingDone = true;
		}
		if (needToUnpack())
		{
			startUnpacking();
			return calcSleep();
		}
		if (m_packingState != 4)
		{
			return calcSleep();
		}
		startPreparation();
		if (m_prepFrames != 0)
		{
			return calcSleep();
		}
		// RW 0x8551F7 .. 0x855275: the template's + 0x80 (a unit cost of the summoning enums) is S-860
		triggerAbilityEffect();
		endPreparation();
		if (needToPack())
		{
			startPacking(true);
		}
		else
		{
			finishAfterPacking();
		}
		return calcSleep();
	}
	if (--m_prepFrames != 0)
	{
		if (continuePreparation())
		{
			return calcSleep();
		}
		endPreparation();
		if (needToPack())
		{
			startPacking(false);
		}
		else
		{
			finishAfterPacking();
		}
		return calcSleep();
	}
	triggerAbilityEffect();
	if (persistentTrigger())
	{
		restartPersistent();
		return calcSleep();
	}
	endPreparation();
	if (needToPack())
	{
		startPacking(true);
	}
	else
	{
		finishAfterPacking();
	}
	return calcSleep();
}

// RW 0x85393B
void SpecialAbilityUpdate::approachTarget()
{
	Object *obj = getObject();
	if (m_approachCounter > (unsigned)LOGICFRAMES_PER_SECOND * 2u)
	{
		// RW 0x853956: after 2 s of approaches a target that moved less than 5.0 (RW 0xBDAE58) since the last one ends the ability
		Coord3D cur = m_targetPos;
		if (const Object *t = target())
		{
			cur = *t->getPosition();
		}
		const float moved = (float)SimMath::length3d(SimMath::subf32(m_lastTargetPos.x, cur.x), SimMath::subf32(m_lastTargetPos.y, cur.y), SimMath::subf32(m_lastTargetPos.z, cur.z));
		if (moved < 5.0f)
		{
			finishAbility(false, true);
			return;
		}
	}
	++m_approachCounter;
	m_lastTargetPos = m_targetPos;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (m_targetID == INVALID_ID)
	{
		if (isZero(m_targetPos) || !ai)
		{
			return;
		}
		ai->aiMoveToPosition(m_targetPos, CMD_FROM_AI); // RW 0x852D59(pos, 2): AI command 0x47
		return;
	}
	Object *t = target();
	if (!t || !ai)
	{
		return;
	}
	m_lastTargetPos = *t->getPosition();
	// RW 0x853AEE: a structure target, a target on another layer or StartAbilityRange <= 50 (RW 0xBD88C4) moves to the object (AI command 0x48);
	// otherwise the approach point at StartAbilityRange (RW 0x851607, a pathfinder cell walk: S-859) is also the object here
	ai->aiMoveToObject(t, CMD_FROM_AI);
}

// RW 0x853BEC
void SpecialAbilityUpdate::startPreparation()
{
	Object *obj = getObject();
	m_prepFrames = m_data->m_preparationFrames; // RW 0x85241F (the garrison enum 0x27 answers 0: S-860)
	if (m_prepFrames != 0)
	{
		// RW 0x853C1B: the AI faces the target (vslot 0xEC: S-859); PREPARING on, UNPACKING / RAISING_FLAG off, the unpacking variation
		ModelConditionBitsHelper::clearAndSet(*obj, { kMcUnpacking, kMcRaisingFlag }, { kMcPreparing });
		setUnpackingVariation();
	}
	// RW 0x853CE5 .. 0x853E1B: the capture enums (0x15 and 0x1A: S-860)
	const int prepType = m_data->m_specialPowerTemplate ? m_data->m_specialPowerTemplate->m_type : 0;
	if (prepType == 0x1D)
	{
		// lane HERO-2, RW 0x853D31 .. 0x853D9F: SPECIAL_INFANTRY_CAPTURE_BUILDING: a target of the object's own team ends the preparation here (no trigger, no
		// statuses: RW 0x853D59 -> 0x853ED0); else UNPACKING off, RAISING_FLAG on (RW 0x68D607); the drawable's capture colour (RW 0x6726E6) and the radar
		// event of a local target (RW 0x853E1D) are the client's
		Object *t = target();
		if (t && t->getTeam() == obj->getTeam())
		{
			return;
		}
		ModelConditionBitsHelper::clearAndSet(*obj, { kMcUnpacking }, { kMcRaisingFlag });
	}
	else if (prepType == 0x15 || prepType == 0x1A)
	{
		++m_unported; // RW 0x853CF4 / 0x853DB2 (S-860)
	}
	if (SpecialPowerModule *spm = mySpecialPowerModule())
	{
		spm->triggerFromUpdate(); // RW 0x853E30: the power module's vslot 0x38 (RW 0x897D25 -> 0x897987(null))
	}
	aiIdleFromAI();
	obj->setStatus(kStatusUsingAbility, true);  // RW 0x68D440(USING_ABILITY | SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING, 1)
	obj->setStatus(kStatusPackingUsing, true);
	// RW 0x853E6F: PrepSoundLoop (client)
}

// RW 0x8522E3: within AbilityAbortRange of the target (the edge distance) or the location (RW 0x6CA525)
bool SpecialAbilityUpdate::withinAbortRange() const
{
	const Object *obj = getObject();
	float dist2 = 0.0f;
	if (m_targetID == INVALID_ID)
	{
		if (isZero(m_targetPos))
		{
			return true;
		}
		dist2 = CircleDistanceSquaredToPoint(*obj->getPosition(), CombatQueries::boundingCircleRadius(*obj), m_targetPos);
	}
	else if (const Object *t = target())
	{
		dist2 = CombatQueries::edgeDistanceSquared2D(*obj, *obj->getPosition(), *t, *t->getPosition());
	}
	return dist2 <= SimMath::mulf32(m_data->m_abilityAbortRange, m_data->m_abilityAbortRange);
}

// RW 0x852A3E: out of AbilityAbortRange ends it; the capture enums (lane HERO-2: 0x1D / 0x1A) end it without a target or with an ALLIED one (RW 0x68D7AB
// == 2); DoCaptureFX's flash of the target's drawable (the + 0x70 accumulator, RW 0x852B21 ..) is the client's; SPECIAL_MISSILE_DEFENDER 0x15: S-860
bool SpecialAbilityUpdate::continuePreparation()
{
	if (m_data->m_abilityAbortRange < 10000000.0f && !withinAbortRange()) // RW 0x852A5E: comiss 1e7, AbilityAbortRange
	{
		return false;
	}
	const int type = m_data->m_specialPowerTemplate ? m_data->m_specialPowerTemplate->m_type : 0;
	if (type == 0x15)
	{
		++m_unported;
	}
	else if (type == 0x1D || type == 0x1A)
	{
		const Object *t = target();
		if (!t || getObject()->getRelationship(*t) == ALLIES)
		{
			return false;
		}
	}
	return true;
}

// RW 0x853EDF
void SpecialAbilityUpdate::triggerAbilityEffect()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	++m_abilitiesTriggered;
	SpecialPowerModule *spm = mySpecialPowerModule();
	if (spm && m_data->m_chainedButton.empty())
	{
		spm->startPowerRecharge(1.0f); // RW 0x853F2A: vslot 0x3C(1.0)
	}
	++m_triggerCount;
	if (m_data->m_awardXPForTriggering != 0)
	{
		if (ExperienceTracker *xp = obj->getExperienceTracker())
		{
			xp->addExperiencePoints((float)m_data->m_awardXPForTriggering, true, true, true, false); // RW 0x79D833(xp, 1, 1, 1, 0)
		}
	}
	const int skill = m_data->m_skillPointsForTriggering == -1 ? m_data->m_awardXPForTriggering : m_data->m_skillPointsForTriggering;
	if (skill > 0)
	{
		if (Player *p = obj->getControllingPlayer())
		{
			p->science().addSkillPoints((float)skill, false); // RW 0x782AA4(points, 0)
		}
	}
	// RW 0x853F9C: the light points (no manager in a skirmish, S-531); RW 0x853FC0: TriggerSound / ActiveLoopSound (client)
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	const int type = t ? t->m_type : 0;
	if (type == 0x80 || type == 0x8F)
	{
		// RW 0x854841: the object's AutoHealBehavior is triggered by the button (Aragorn's Athelas, RW 0x855431)
		if (AutoHealBehavior *heal = dynamic_cast<AutoHealBehavior *>(obj->findModule("AutoHealBehavior")))
		{
			heal->triggerByButton();
		}
	}
	else if (type == 0x20)
	{
		// RW 0x8544CD .. 0x8544E8 (lane STEALTH-2): SPECIAL_DISGUISE_AS_VEHICLE: the object's StealthUpdate (RW 0x68FBD3) disguises it as the target (RW 0x776117)
		Object *victim = target();
		StealthUpdate *st = victim ? StealthUpdate::of(*obj) : nullptr;
		if (st)
		{
			st->disguiseAsObject(victim);
		}
	}
	else if (type == 0x89)
	{
		// RW 0x85491F .. 0x854977 (lane HERO-2): SPECIAL_SCREECH dispatches the script event BeScary (slot 8 of RW 0x73449A) to the object through RW 0x7379CB
		// with the arguments (the object: kind 3 at args + 0x18, its id at + 0x10; EffectRange: kind 1 at + 0x30, the float at + 0x20); retail's handler
		// RadiateTerrorEx (Scripts.lua) broadcasts BeTerrified to the enemies within it
		LuaEventArgs args;
		args.arg[0] = LuaEventArg::makeObject((int)obj->getID());
		args.arg[1] = LuaEventArg::makeReal(m_data->m_effectRange);
		logic.dispatchScriptEvent(LUAEVENT_BeScary, *obj, args);
	}
	else if (type == 0x85)
	{
		// RW 0x854981 .. 0x8549DD (lane HERO-2): SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE: a DISGUISED object's SpecialDisguiseUpdate ends the disguise (RW 0x8B4702(0))
		if (obj->testModelCondition(300))
		{
			if (SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(*obj))
			{
				sd->cancelDisguise(false);
			}
		}
	}
	else if (type == 0x1A || type == 0x1D)
	{
		if (!captureTarget(spm))
		{
			return; // RW 0x854901: a capture that does not happen skips the common end (stealth, freeze, modifier)
		}
	}
	else if (type == 0x15 || type == 0x27 || type == 0x28 || type == 0x2A)
	{
		++m_unported; // the capture / garrison / charge / SpecialDisguiseUpdate cancel (0x85) / weather enums of the switch: S-860
	}
	if (m_data->m_loseStealthOnTrigger)
	{
		// RW 0x85488A .. 0x8548C0 (lane STEALTH-2): the object's StealthUpdate marks it detected (RW 0x7767A9(0, 1, null, horde)) and the InvisibilityManager
		// too (RW 0x81C32C(object, null, 0, 1))
		if (StealthUpdate *st = StealthUpdate::of(*obj))
		{
			st->markAsDetected(0, 1, nullptr, true);
		}
		logic.invisibility().markDetected(obj, nullptr, 0, 1);
	}
	if (m_data->m_freezeAfterTriggerFrames != 0)
	{
		obj->setDisabled(kDisabledHeld, now() + m_data->m_freezeAfterTriggerFrames); // RW 0x6907F1(3, now + frames)
	}
	if (!m_data->m_triggerAttributeModifier.empty())
	{
		obj->addAttributeModifier(m_data->m_triggerAttributeModifier, (int)m_data->m_attributeModifierDuration); // RW 0x68F1A8
	}
	(void)logic;
}

// lane HERO-2: RW 0x8544F2 .. 0x854772, the capture of SpecialAbilityUpdate's trigger (SPECIAL_INFANTRY_CAPTURE_BUILDING 0x1D, SPECIAL_BLACKLOTUS 0x1A). The
// building is the target when the special power template has an ObjectFilter that allows it (RW 0x762977 / 0x7640C1(target, null)), else the closest
// LINKED_TO_FLAG object within 150 (RW 0xC041F8) of the target (ThePartitionManager RW 0xA39090, distance type 1, the KindOf filter RW 0x445139): the capture
// flag's structure. Nothing happens when there is none or when both are already the object's team's. Then the target (when it is not the building:
// START_CAPTURE off, CAPTURED on) and the building take the default team of the object's player (RW 0x6996DC), the building gets CAPTURED, the target's
// capturer (+ 0x80) clears, and the 0x1A enum restarts the power's recharge. Not ported (S-1225, noted): a garrisonable contain's passengers (vslots 0xA8 /
// 0x54), an UNDER_CONSTRUCTION building's production cancel and score counts (RW 0x85463E .. 0x8546B5), the upgrades reset RW 0x68E083 (S-952), the skirmish
// AI's object events RW 0x6A99CB / 0x6A9D9C; the radar / EVA / drawable calls are the client's. Returns false when nothing was captured
bool SpecialAbilityUpdate::captureTarget(SpecialPowerModule *spm)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Object *t = target();
	if (!t || !spm)
	{
		return false;
	}
	const SpecialPowerTemplate *spt = spm->getSpecialPowerTemplate();
	Object *building = nullptr;
	if (spt && spt->m_objectFilter && ObjectFilterMatch::isValid(spt->m_objectFilter.get()))
	{
		if (!ObjectFilterMatch::allows(logic, *spt->m_objectFilter, *t, nullptr))
		{
			return false;
		}
		building = t;
	}
	else
	{
		static const int kLinked = ObjectTemplateInfoBuilder::kindOfIndex("LINKED_TO_FLAG"); // KindOf 0x32
		PartitionFilterFn linked([](Object &o) { return kLinked >= 0 && o.isKindOf((unsigned)kLinked); });
		building = logic.partition().getClosestObject(*t->getPosition(), 150.0f, FROM_CENTER_3D, { &linked });
		if (!building)
		{
			return false;
		}
	}
	if (t->getTeam() == obj->getTeam() && building->getTeam() == obj->getTeam())
	{
		return false;
	}
	Player *owner = obj->getControllingPlayer();
	Team *team = owner ? owner->getDefaultTeam() : nullptr; // Player + 0x30C
	ContainModuleInterface *contain = building->getContain() ? building->getContain() : t->getContain();
	if (contain && contain->isGarrisonable())
	{
		logic.noteStop("[S-1225] capture: a garrisonable contain's vslots 0xA8 / 0x54 (its passengers) are not called");
	}
	if (t != building)
	{
		if (building->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION")) && building->getControllingPlayer())
		{
			logic.noteStop("[S-1225] capture: an UNDER_CONSTRUCTION building's production cancel and score counts (RW 0x85463E .. 0x8546B5) are not ported");
		}
		ModelConditionBitsHelper::clearAndSet(*t, { kMcStartCapture }, { kMcCaptured }); // RW 0x8546DC
		t->setCapturedTeam(team);                                                       // RW 0x8546F6 / 0x854702
	}
	building->setCapturedTeam(team); // RW 0x854719 / 0x854725
	setCondition(*building, kMcCaptured, true); // RW 0x85473B
	t->setCapturerID(INVALID_ID);    // RW 0x854740
	if (spt && spt->m_type == 0x1A)
	{
		spm->startPowerRecharge(1.0f); // RW 0x854764
	}
	++m_captures;
	return true;
}

// RW 0x8549E7
void SpecialAbilityUpdate::finishAfterPacking()
{
	m_withinRange = false;
	m_packingState = 0;
	const bool hasTarget = !isZero(m_targetPos) || m_targetID != INVALID_ID;
	if (m_data->m_fleeRangeAfterCompletion == 0.0f || !hasTarget)
	{
		if (m_data->m_chainedButton.empty())
		{
			aiIdleFromAI();
		}
		else
		{
			++m_unported; // RW 0x854A6B: the ChainedButton command (S-859)
		}
	}
	else
	{
		++m_unported; // RW 0x854AD8: the flee move (S-859)
	}
	finishAbility(false, false);
}

// RW 0x851E9F
bool SpecialAbilityUpdate::handlePackingProcessing()
{
	if (m_packFramesLeft == 0)
	{
		return false;
	}
	if (--m_packFramesLeft != 0)
	{
		// RW 0x851EC1: LoseStealthOnTrigger's unstealth PreTriggerUnstealthTime before the end (S-859)
		return true;
	}
	Object *obj = getObject();
	ModelConditionBitsHelper::clearAndSet(*obj, { kMcUnpacking, kMcPackingType1, kMcPackingType2, kMcPackingType3, kMcPackingType4, kMcPackingType5, kMcPackingType6, kMcPacking }, {});
	if (m_packingState == 2)
	{
		if (m_data->m_flipOwnerAfterUnpacking)
		{
			obj->setOrientation(SimMath::addf32(obj->getOrientation(), 3.14159274f)); // lane HERO-2, RW 0x851F05: addss PI (RW 0xBDD388), RW 0x70C31E
		}
		m_packingState = 4;
		const int type = m_data->m_specialPowerTemplate ? m_data->m_specialPowerTemplate->m_type : 0;
		if (type == 0x1D)
		{
			// lane HERO-2, RW 0x851F39 .. 0x851FE7: the target no other object is capturing (+ 0x80) loses CANCEL_CAPTURE, records the capturer and plays
			// START_CAPTURE (off first when the teams differ, so the animation restarts); the capturer gets CAPTURING
			Object *t = target();
			if (t && (t->getCapturerID() == INVALID_ID || t->getCapturerID() == obj->getID()))
			{
				setCondition(*t, kMcCancelCapture, false);
				t->setCapturerID(obj->getID());
				if (obj->getTeam() != t->getTeam())
				{
					setCondition(*t, kMcStartCapture, false);
				}
				setCondition(*t, kMcStartCapture, true);
				setCondition(*obj, kMcCapturing, true);
			}
		}
		return false;
	}
	if (m_packingState == 1)
	{
		if (m_data->m_flipOwnerAfterPacking)
		{
			obj->setOrientation(SimMath::addf32(obj->getOrientation(), 3.14159274f)); // lane HERO-2, RW 0x852005
		}
		m_packingState = 3;
		finishAfterPacking();
		return true;
	}
	return false;
}

// RW 0x853468
void SpecialAbilityUpdate::startPacking(bool success)
{
	Object *obj = getObject();
	m_packingState = 1;
	const float f = m_data->m_packUnpackVariationFactor;
	const float r = obj->logic().random().getValueReal(SimMath::subf32(1.0f, f), SimMath::addf32(f, 1.0f), "SpecialAbilityUpdate.cpp", 0x581); // RW 0x6D332C
	m_packFramesLeft = (unsigned)SimMath::ftol2(SimMath::pc24MulW((double)m_data->m_packFrames, (double)r)); // fild PackTime; fmul st(1); RW 0xA3CFA4
	ModelConditionBitsHelper::clearAndSet(*obj, { kMcUnpacking, kMcRaisingFlag }, { kMcPacking });
	obj->setStatus(kStatusPackingUsing, true);
	setUnpackingVariation();
	// RW 0x853596: PackSound (client)
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	if ((!t || t->m_type != 0x2A) && obj->getAIUpdateInterface())
	{
		obj->getAIUpdateInterface()->aiBusy(CMD_FROM_AI); // RW 0x852E2A(0, 2): AI command 0x31
	}
	(void)success; // RW 0x85362A: the voice of a success (client)
}

// RW 0x8536EA
void SpecialAbilityUpdate::startUnpacking()
{
	Object *obj = getObject();
	m_packingState = 2;
	const float f = m_data->m_packUnpackVariationFactor;
	const float r = obj->logic().random().getValueReal(SimMath::subf32(1.0f, f), SimMath::addf32(f, 1.0f), "SpecialAbilityUpdate.cpp", 0x5DA); // RW 0x6D332C
	m_packFramesLeft = (unsigned)SimMath::ftol2(SimMath::pc24MulW((double)m_data->m_unpackFrames, (double)r));
	ModelConditionBitsHelper::clearAndSet(*obj, { kMcPacking }, { kMcUnpacking });
	obj->setStatus(kStatusPackingUsing, true);
	setUnpackingVariation();
	if (m_data->m_customAnimCondition >= 0)
	{
		if (m_data->m_customAnimTriggerFrames == 0)
		{
			applyCustomAnim();
		}
		else
		{
			m_customAnimFrame = m_data->m_customAnimTriggerFrames + now();
		}
	}
	// RW 0x85383E .. 0x8538B6: SPECIAL_GRAB_CHUNK (0x28) and SPECIAL_SMITE_CANCELDISGUISE (0x84); UnpackSound (client)
	const int unpackType = m_data->m_specialPowerTemplate ? m_data->m_specialPowerTemplate->m_type : 0;
	if (unpackType == 0x28)
	{
		++m_unported; // RW 0x85384C: the grab enum's model condition (S-860)
	}
	else if (unpackType == 0x84 && obj->testModelCondition(300))
	{
		// RW 0x853884 .. 0x8538DD (lane HERO-2): a DISGUISED object's SpecialDisguiseUpdate ends the disguise (RW 0x8B4702(0))
		if (SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(*obj))
		{
			sd->cancelDisguise(false);
		}
	}
	if (obj->getAIUpdateInterface())
	{
		obj->getAIUpdateInterface()->aiBusy(CMD_FROM_AI); // RW 0x852E2A(0, 2)
	}
	if (m_data->m_triggerModelCondition != -1)
	{
		// RW 0x8538D9: the special model condition for _ftol(TriggerModelConditionDuration) frames
		obj->setSpecialModelConditionState(m_data->m_triggerModelCondition, (UnsignedInt)SimMath::ftol2((double)m_data->m_triggerModelConditionDuration));
	}
}

// RW 0x8530BA
void SpecialAbilityUpdate::finishAbility(bool ownerDying, bool aborted)
{
	Object *obj = getObject();
	obj->setStatus(kStatusUsingAbility, false); // RW 0x68D440(USING_ABILITY | SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING | NO_AUTO_ACQUIRE, 0)
	obj->setStatus(kStatusPackingUsing, false);
	obj->setStatus(kStatusNoAutoAcquire, false);
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		(void)ai; // RW 0x853105: AI + 0x3CA = 0 and RW 0x6630D7 (the AI's goal flag: S-859)
	}
	// RW 0x85311A: the prep and active loop sounds stop (client)
	endPreparation();
	if (SpecialPowerModule *spm = mySpecialPowerModule())
	{
		(void)spm; // RW 0x853148: the power module's vslot 0x4C (the "update finished" notice, RW 0x896F8E: S-859)
	}
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	const int type = t ? t->m_type : 0;
	if (type == 0x80 || type == 0x8F)
	{
		// RW 0x8531C9: the AutoHealBehavior goes back to waiting for its button (RW 0x85541B)
		if (AutoHealBehavior *heal = dynamic_cast<AutoHealBehavior *>(obj->findModule("AutoHealBehavior")))
		{
			heal->waitForButton();
		}
	}
	if (type == 0x1D || type == 0x1A)
	{
		// lane HERO-2, RW 0x85318F .. 0x8531F0: a target still showing START_CAPTURE for this capturer loses it and the capturer, and shows CANCEL_CAPTURE for
		// LOGICFRAMES_PER_SECOND frames (RW 0xDE8E0C = RW 0xD9F608, RW 0x68B581)
		Object *t = target();
		if (t && t->testModelCondition(kMcStartCapture) && t->getCapturerID() == obj->getID())
		{
			t->setCapturerID(INVALID_ID);
			setCondition(*t, kMcStartCapture, false);
			t->setSpecialModelConditionState(kMcCancelCapture, 5);
		}
	}
	const unsigned paralyze = aborted ? m_data->m_paralyzeWhenAborted : m_data->m_paralyzeWhenCompleted;
	if (paralyze != 0)
	{
		++m_unported; // RW 0x852E2A(frames, 2): the AI's paralysis command 0x31 with a duration (S-859)
	}
	(void)ownerDying; // RW 0x85251A: the special objects (S-860)
	if (m_chargeSpeed)
	{
		aiIdleFromAI();
		m_chargeSpeed = false;
	}
	if (!m_keepActive)
	{
		m_active = false;
	}
	m_packingState = 0;
	m_withinRange = false;
	m_chargeMove = false;
	if (m_data->m_killAttributeModifierOnExit && !m_data->m_triggerAttributeModifier.empty())
	{
		obj->removeAttributeModifier(m_data->m_triggerAttributeModifier); // RW 0x68F259
	}
	// RW 0x8533C2: a player's object reports the ability's EffectDuration to the player (RW 0x7A1C0D, the special power timer display: S-850's HUD)
}

void SpecialAbilityUpdate::crc(StateHasher &h) const
{
	h.addI32(m_triggerCount);
	h.addU32(m_packFramesLeft);
	h.addU32(m_effectExpiry);
	h.addI32(m_packingState);
	h.addU32(m_prepFrames);
	h.addU32(m_targetID);
	h.addFloat(m_targetPos.x);
	h.addFloat(m_targetPos.y);
	h.addFloat(m_targetPos.z);
	h.addI32(m_persistentLeft);
	h.addU32(m_options);
	h.addBool(m_active);
	h.addU32(m_approachCounter);
	h.addBool(m_noTarget);
	h.addBool(m_facingIssued);
	h.addBool(m_facingDone);
	h.addBool(m_withinRange);
	h.addU32(m_customAnimFrame);
	h.addU32(m_abilitiesTriggered);
	h.addU32(m_captures); // lane HERO-2
}

// ---------------------------------------------------------------------------------------------------------------------------------
// WeaponFireSpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
WeaponFireSpecialAbilityUpdate::WeaponFireSpecialAbilityUpdate(Thing *thing, const WeaponFireSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_wf(data)
{
}

// RW 0x895DF2 (the constructor): the SpecialWeapon from TheWeaponStore (RW 0x6CC5DF), made and loaded; an unknown name leaves no weapon (the trigger then fires
// nothing). Here the object's weapon host (ObjectWeapons) exists only after the modules are built, so the weapon is made at its first use (a fresh weapon is
// loaded either way)
Weapon *WeaponFireSpecialAbilityUpdate::specialWeapon()
{
	if (!m_weaponMade)
	{
		m_weaponMade = true;
		const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_wf->m_specialWeapon) : nullptr;
		ObjectWeapons *w = getObject()->getWeapons();
		if (wt && w)
		{
			m_weapon = w->makeExtraWeapon(wt);
		}
	}
	return m_weapon.get();
}

WeaponFireSpecialAbilityUpdate::~WeaponFireSpecialAbilityUpdate() = default;

// the model condition of WhichSpecialWeapon (RW 0x895F10: 1 .. 3 SPECIAL_WEAPON_ONE .. THREE at Object + 0x128, 4 .. 6 FOUR .. SIX at + 0x154)
int WeaponFireSpecialAbilityUpdate::specialWeaponCondition() const
{
	switch (m_wf->m_whichSpecialWeapon)
	{
	case 1: return 244;
	case 2: return 245;
	case 3: return 246;
	case 4: return 588;
	case 5: return 589;
	case 6: return 590;
	default: return -1;
	}
}

// RW 0x895E60
bool WeaponFireSpecialAbilityUpdate::continuePreparation()
{
	if (m_wf->m_skipContinue && m_triggerCount == 2)
	{
		return false;
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x895ECE
void WeaponFireSpecialAbilityUpdate::startUnpacking()
{
	SpecialAbilityUpdate::startUnpacking();
	if (!(m_wf->m_skipContinue && m_triggerCount != 0))
	{
		setCondition(*getObject(), specialWeaponCondition(), true);
		if (isHorde(*getObject()))
		{
			++m_unported; // RW 0x6944A5: the horde passes the condition to its members (S-859)
		}
	}
	if (m_wf->m_playWeaponPreFireFX)
	{
		++m_unported; // RW 0x895FC4: the weapon's PreAttackFX at the target (client)
	}
}

// RW 0x8961F7
void WeaponFireSpecialAbilityUpdate::triggerAbilityEffect()
{
	Object *obj = getObject();
	ObjectWeapons *weapons = obj->getWeapons();
	// RW 0x896210: the FiringTracker's RW 0x8E302A(1) (the object's + 0x248) is COMBAT-1's: not called here
	if (!(isHorde(*obj) && m_data->m_suppressForHordes))
	{
		Object *victim = target();
		if (victim && isHorde(*victim))
		{
			// RW 0x896263: a horde target is shot through one of its members (HordeContain vslot 0x110)
			if (ContainModuleInterface *c = victim->getContain())
			{
				const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList();
				if (items && !items->empty())
				{
					victim = items->front();
				}
			}
		}
		if (specialWeapon() && weapons && weapons->extraWeaponStatus(*m_weapon) == 0) // RW 0x6CDCE7 == READY_TO_FIRE
		{
			m_weapon->setOwnerID(obj->getID());
			if (!victim)
			{
				const Coord3D at = (isZero(m_targetPos) || m_wf->m_needLivingTargets) ? *obj->getPosition() : m_targetPos;
				weapons->fireExtraWeaponAt(*m_weapon, at); // RW 0x6CF3D2
			}
			else
			{
				weapons->fireExtraWeapon(*m_weapon, *victim); // RW 0x6CF328
			}
			++m_shots;
			obj->setStatus(kStatusIgnoreAI, true); // RW 0x6907BD(now + BusyForDuration): IGNORE_AI_COMMAND and its expiry (Object + 0x448)
			obj->setIgnoreAICommandUntil(now() + m_wf->m_busyForFrames);
		}
	}
	SpecialAbilityUpdate::triggerAbilityEffect();
	if (SpecialPowerModule *spm = mySpecialPowerModule())
	{
		spm->startPowerRecharge(1.0f); // RW 0x896354
	}
}

// RW 0x896033
void WeaponFireSpecialAbilityUpdate::finishAbility(bool ownerDying, bool aborted)
{
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted);
	setCondition(*getObject(), specialWeaponCondition(), false);
}

void WeaponFireSpecialAbilityUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addBool(m_weaponMade);
	h.addBool(m_weapon != nullptr);
	if (m_weapon)
	{
		m_weapon->crc(h); // the owned special weapon's ammo and timers (RW module + 0x88)
	}
	h.addU32(m_shots);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ToggleMountedSpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
ToggleMountedSpecialAbilityUpdate::ToggleMountedSpecialAbilityUpdate(Thing *thing, const ToggleMountedSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_tm(data)
{
}

// RW 0x8B11FE
void ToggleMountedSpecialAbilityUpdate::onBuildComplete()
{
	if (m_tm->m_triggerInstantlyOnCreate)
	{
		triggerAbilityEffect();
	}
}

// RW 0x8B42F3
bool ToggleMountedSpecialAbilityUpdate::continuePreparation()
{
	if (m_triggerCount == 2)
	{
		return false;
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x8B1690
void ToggleMountedSpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	m_replaceAfterPacking = false;
	if (!ai || !ai->isIdle() || m_triggerCount != 1) // AI vslot 0x1B8
	{
		return;
	}
	if (!m_tm->m_mountedTemplate.empty())
	{
		replaceWithMounted(); // RW 0x8B16D1 .. : the object is replaced by MountedTemplate (lane HERO-2)
		return;
	}
	ObjectWeapons *weapons = obj->getWeapons();
	if (!obj->testModelCondition(kMcMounted))
	{
		setCondition(*obj, kMcMounted, true);           // Object + 0x124 bit 22
		ai->chooseLocomotorSet(kLocomotorSetMounted);   // AI vslot 0x238(7)
		if (weapons)
		{
			weapons->setWeaponSetFlag(kWeaponSetMounted, true); // RW 0x691059(0x15)
		}
		obj->setArmorSetFlag(kArmorSetMounted, true);   // RW 0x68BE59(6)
	}
	else
	{
		if (ContainModuleInterface *c = obj->getContain())
		{
			if (c->getContainCount() != 0)
			{
				++m_unported; // RW 0x8B1722: the contain ejects its riders (vslot 0xA8: S-861)
			}
		}
		setCondition(*obj, kMcMounted, false);
		ai->chooseLocomotorSet(0);
		if (weapons)
		{
			weapons->setWeaponSetFlag(kWeaponSetMounted, false); // RW 0x691106(0x15)
		}
		obj->setArmorSetFlag(kArmorSetMounted, false);  // RW 0x68BE6B(6)
		if (m_tm->m_cancelDisguiseWhenDismounting && obj->testModelCondition(300))
		{
			// RW 0x8B175F .. 0x8B17C2 (lane HERO-2): a DISGUISED object's SpecialDisguiseUpdate ends the disguise, keeping the look (RW 0x8B4702(1))
			if (SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(*obj))
			{
				sd->cancelDisguise(true);
			}
		}
	}
	++m_toggles;
	// RW 0x8B1803: the control bar of the selected object is marked dirty (client)
}

// lane HERO-2: RW 0x8B140D. MountedTemplate (RW 0x6D1305) is built now by the build assistant (vslot 0x38, RW 0x7978A1: Construction::buildObjectNow) at the
// object's position and angle for its controlling player; the old object's score count goes (RW 0x79F0E1(old, -1)); the new one takes the name (+ 0x88),
// the experience (setExperienceAndLevel with the tracker's +0x3C cleared around it, RW 0x8B14C2 .. 0x8B14EC), the health (body vslot 0xAC with the old
// body's vslot 0x10), the team (RW 0x69954A) and the synchronised power timers (SynchronizeTimerOnSpecialPower, RW 0x8B12BF -> 0x897368); the old one is
// removed after its packing (RW 0x8B1E9A). Not ported (S-861): + 0x488 (RW 0x5EA74E), the AI's group (RW 0x68B36F -> 0x6682B1), the script engine's name
// record (RW 0x60A1D5), the last AI command given again (RW 0x66D7C9 / 0x66B4DF); the selection, the hotkey group and the drawable are the client's
void ToggleMountedSpecialAbilityUpdate::replaceWithMounted()
{
	Object *old = getObject();
	GameLogic &logic = old->logic();
	const ThingTemplate *tmpl = logic.things().findTemplate(m_tm->m_mountedTemplate);
	Player *owner = old->getControllingPlayer();
	if (!tmpl || !owner)
	{
		if (!tmpl)
		{
			logic.reportError("ToggleMountedSpecialAbilityUpdate: MountedTemplate '" + m_tm->m_mountedTemplate + "' is not a template (RW 0x6D1305)");
		}
		return;
	}
	Object *n = Construction::buildObjectNow(logic, old, *tmpl, *old->getPosition(), old->getOrientation(), *owner);
	if (!n)
	{
		return;
	}
	owner->getScoreKeeper().addObjectBuilt(logic, *old, -1); // RW 0x8B1499
	n->setName(old->getName());                              // RW 0x8B14AB
	if (ExperienceTracker *nt = n->getExperienceTracker())
	{
		if (const ExperienceTracker *ot = old->getExperienceTracker())
		{
			nt->setDefaultFeedback(false);
			nt->setExperienceAndLevel(ot->getExperience(), true); // RW 0x79D8EF(experience, 1)
			nt->setDefaultFeedback(true);
		}
	}
	ActiveBody *nb = dynamic_cast<ActiveBody *>(n->getBodyModule());
	if (nb && old->getBodyModule())
	{
		nb->friend_setHealthRaw(old->getBodyModule()->getHealth()); // body vslot 0xAC (vslot 0x10 of the old body)
	}
	if (n->getAIUpdateInterface())
	{
		n->setTeam(old->getTeam()); // RW 0x8B156D
	}
	n->setName(old->getName());     // RW 0x8B157F
	// RW 0x8B12BF: SynchronizeTimerOnSpecialPower
	for (const std::string &name : m_tm->m_synchronizeTimerOnSpecialPower)
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(name) : nullptr; // RW 0x69C146
		SpecialPowerModule *src = t ? dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*old, t)) : nullptr; // RW 0x68C26D
		SpecialPowerModule *dst = t ? dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*n, t)) : nullptr;
		if (src && dst)
		{
			dst->copyTimerFrom(*src); // RW 0x897368
		}
	}
	++m_unported; // + 0x488, the AI group, the script name record and the last command (S-861)
	m_replaceAfterPacking = true; // + 0x8C
	m_replacement = n->getID();
	++m_toggles;
}

// RW 0x8B125F
void ToggleMountedSpecialAbilityUpdate::finishAfterPacking()
{
	SpecialAbilityUpdate::finishAfterPacking();
	// RW 0x8B126B: the drawable's opacity back to 1.0 (client)
	getObject()->setStatus(kStatusIgnoreAI, false); // RW 0x62684D(0x4A, 0)
	if (m_replaceAfterPacking && !m_tm->m_mountedTemplate.empty())
	{
		// lane HERO-2, RW 0x8B1E9A: the replaced object leaves the world (RW 0x68C18F), its drawable hides (client), the pathfinder forgets it (RW 0x6E85FB)
		// and TheGameLogic destroys it (RW 0x62BBAB): GameLogic::destroyObject does the three
		Object *obj = getObject();
		obj->logic().destroyObject(obj);
	}
}

// RW 0x8B1219
void ToggleMountedSpecialAbilityUpdate::finishAbility(bool ownerDying, bool aborted)
{
	// RW 0x8B121C: the drawable's opacity back to 1.0 (client)
	getObject()->setStatus(kStatusIgnoreAI, false);
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted);
}

void ToggleMountedSpecialAbilityUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_toggles);
	h.addBool(m_replaceAfterPacking); // lane HERO-2
	h.addU32(m_replacement);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// HeroModeSpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
HeroModeSpecialAbilityUpdate::HeroModeSpecialAbilityUpdate(Thing *thing, const HeroModeSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_hm(data)
{
}

// RW 0x8959B5
void HeroModeSpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *obj = getObject();
	if (m_hm->m_stopUnitBeforeActivating)
	{
		aiIdleFromAI();
	}
	if (!m_hm->m_heroAttributeModifier.empty())
	{
		obj->addAttributeModifier(m_hm->m_heroAttributeModifier, -1); // RW 0x68F1A8(name, -1)
	}
	const int cond = m_hm->m_useUserModelCondition ? kMcUser3 : kMcHero;
	if (!obj->testModelCondition(cond))
	{
		obj->setSpecialModelConditionState(cond, m_hm->m_heroEffectFrames); // RW 0x68B581
	}
}

// RW 0x8958F8
void HeroModeSpecialAbilityUpdate::onEffectExpired()
{
	m_effectExpiry = 0;
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		w->setWeaponSetFlag(kWeaponSetHeroMode, false); // RW 0x691106(0x1B)
	}
}

// RW 0x8958BD
void HeroModeSpecialAbilityUpdate::startUnpacking()
{
	SpecialAbilityUpdate::startUnpacking();
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		// RW 0x8958CA: RW 0x691014(0x1B) asks the template for a weapon set with WEAPONSET_HERO_MODE (RW 0x73F6B9) before setting the flag
		if (w->hasWeaponSetFor(kWeaponSetHeroMode))
		{
			w->setWeaponSetFlag(kWeaponSetHeroMode, true);
		}
	}
	m_effectExpiry = now() + m_hm->m_heroEffectFrames;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// LevelGrantSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
LevelGrantSpecialPower::LevelGrantSpecialPower(Thing *thing, const LevelGrantSpecialPowerModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_lg(data)
{
}

// RW 0x8C6E82 -> RW 0x8C6D43: Experience points to the object (RW 0x79D833(xp, 1, 1, 1, 0) when its tracker can still level, RW 0x79D322) with LevelFX;
// a horde's members share them (Experience / member count each, HordeContain vslot 0x110)
void LevelGrantSpecialPower::grant(Object &o)
{
	if (m_lg->m_experience <= 0)
	{
		return;
	}
	auto one = [this](Object &victim, float xp) {
		ExperienceTracker *t = victim.getExperienceTracker();
		if (!t || !t->isAcceptingExperiencePoints())
		{
			return;
		}
		t->addExperiencePoints(xp, true, true, true, false);
		++m_granted;
		if (FXEventLog::isFXName(m_lg->m_levelFX))
		{
			GameLogic &logic = victim.logic();
			logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "LevelGrantSpecialPower LevelFX", logic.getFrame(), m_lg->m_levelFX, victim)); // RW 0x4B1B5A
		}
	};
	if (!isHorde(o))
	{
		one(o, (float)m_lg->m_experience);
		return;
	}
	ContainModuleInterface *c = o.getContain();
	const ContainModuleInterface::ContainedItemsList *items = c ? c->getContainedItemsList() : nullptr;
	const unsigned n = c ? c->getContainCount() : 0;
	if (!items || n == 0)
	{
		return;
	}
	const float share = SimMath::divf32((float)m_lg->m_experience, (float)n);
	std::vector<Object *> members(items->begin(), items->end());
	for (Object *m : members)
	{
		one(*m, share);
	}
}

// RW 0x8C6FA4
void LevelGrantSpecialPower::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	const Coord3D center = m_targetPos; // RW 0x8C6FF8: the module's location (+ 0x44)
	const float r2 = SimMath::mulf32(m_lg->m_radiusEffect, m_lg->m_radiusEffect);
	// RW 0xA39340 with the filters AcceptanceFilter (owner), RW 0xC0F374 (not the object itself), RW 0xC10E20 (the partition's map status, S-856), RW 0xC11DC0
	// relationship ALLIES (4) and RW 0xC1D660 (alive); the partition's order is the object list here
	std::vector<Object *> hits;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o == obj || o->isDestroyed() || o->isEffectivelyDead() || !o->isInWorld())
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
		}
		if (CombatQueries::centerDistanceSquared2D(*o->getPosition(), center) > r2)
		{
			continue;
		}
		if (obj->getRelationship(*o) != ALLIES)
		{
			continue;
		}
		if (m_lg->m_acceptanceFilter && !ObjectFilterMatch::allows(logic, *m_lg->m_acceptanceFilter, *o, owner))
		{
			continue;
		}
		hits.push_back(o);
	}
	CombatQueries::sortByCenterDistance2D(hits, center); // RW 0xA39340 with sort mode 1 (RW 0x8C7068: push 1; equal distances: S-856)
	std::vector<ObjectID> hordesDone;
	for (Object *o : hits)
	{
		// RW 0x8C6F32: a horde member grants through its horde, once per horde
		Object *who = o;
		if (Object *by = o->getContainedBy())
		{
			if (isHorde(*by))
			{
				who = by;
			}
		}
		if (isHorde(*who))
		{
			if (std::find(hordesDone.begin(), hordesDone.end(), who->getID()) != hordesDone.end())
			{
				continue;
			}
			hordesDone.push_back(who->getID());
		}
		grant(*who);
	}
}

void LevelGrantSpecialPower::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_granted);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ModelConditionSpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
ModelConditionSpecialAbilityUpdate::ModelConditionSpecialAbilityUpdate(Thing *thing, const ModelConditionSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_mc(data)
{
}

// WhichSpecialPower (RW 0x89455B: 0 USER_... at Object + 0x134 bit 10, 1 .. 3 SPECIAL_POWER_1 .. 3 at + 0x124 bits 19 .. 21)
int ModelConditionSpecialAbilityUpdate::specialPowerCondition() const
{
	switch (m_mc->m_whichSpecialPower)
	{
	case 0: return (0x134 - 0x10C) * 8 + 10;
	case 1: return (0x124 - 0x10C) * 8 + 19;
	case 2: return (0x124 - 0x10C) * 8 + 20;
	case 3: return (0x124 - 0x10C) * 8 + 21;
	default: return -1;
	}
}

bool ModelConditionSpecialAbilityUpdate::continuePreparation()
{
	if (m_triggerCount == 2)
	{
		return false; // RW 0x8B42F3 (the same slot as ToggleMounted's)
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x89455B
void ModelConditionSpecialAbilityUpdate::startUnpacking()
{
	SpecialAbilityUpdate::startUnpacking();
	if (m_triggerCount != 0)
	{
		return;
	}
	setCondition(*getObject(), specialPowerCondition(), true);
}

// RW 0x8945C1
void ModelConditionSpecialAbilityUpdate::finishAbility(bool ownerDying, bool aborted)
{
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted);
	setCondition(*getObject(), specialPowerCondition(), false);
}

// RW 0x89463A
void ModelConditionSpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	if (!m_mc->m_generateTerror && !m_mc->m_generateUncontrollableFear)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	// RW 0x894701: ThePartitionManager within EmotionPulseRadius, distance type 0, unsorted, with the filters RW 0xC1676C (the owner's ENEMIES: mask 4, flag 1;
	// RW 0x66110A), RW 0xC0F374 (the same Object + 0x458 bit 3: never set) and the ObjectFilter for the owner (RW 0xBE4CC8); every hit gets TERROR then
	// UNCONTROLLABLE_FEAR from the object with delay 1 (RW 0x89472D / 0x894745 -> RW 0x68F37F; lane MODULES-2: EmotionTrackerUpdate::requestEmotion)
	PartitionFilterFn victims([&](Object &o) {
		if (!owner || owner->getRelationship(o.getTeam()) != ENEMIES)
		{
			return false;
		}
		return !m_mc->m_objectFilter || ObjectFilterMatch::allows(logic, *m_mc->m_objectFilter, o, owner);
	});
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_mc->m_emotionPulseRadius, FROM_CENTER_2D, { &victims }, ITER_FASTEST))
	{
		if (m_mc->m_generateTerror)
		{
			EmotionTrackerUpdate::requestEmotion(*hit.object, EMOTION_TERROR, obj, 1);
			++m_emotions;
		}
		if (m_mc->m_generateUncontrollableFear)
		{
			EmotionTrackerUpdate::requestEmotion(*hit.object, EMOTION_UNCONTROLLABLE_FEAR, obj, 1);
			++m_emotions;
		}
	}
}

void ModelConditionSpecialAbilityUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_emotions);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SpecialDisguiseUpdate (lane HERO-2)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
constexpr int kMcDisguised = 300; // Object + 0x130 bit 12 (RW 0x46E918(300))
}

SpecialDisguiseUpdate::SpecialDisguiseUpdate(Thing *thing, const SpecialDisguiseUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_sd(data)
{
}

SpecialDisguiseUpdate *SpecialDisguiseUpdate::of(Object &obj)
{
	return dynamic_cast<SpecialDisguiseUpdate *>(obj.findModule("SpecialDisguiseUpdate")); // RW 0x68BDA5(NameKey "SpecialDisguiseUpdate")
}

// RW 0x8B4410
void SpecialDisguiseUpdate::onBuildComplete()
{
	if (m_sd->m_triggerInstantlyOnCreate)
	{
		triggerAbilityEffect();
	}
}

// RW 0x8B42F3 (the slot ToggleMounted / ModelCondition share)
bool SpecialDisguiseUpdate::continuePreparation()
{
	if (m_triggerCount == 2)
	{
		return false;
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x8B4760
void SpecialDisguiseUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai || !ai->isIdle() || m_triggerCount != 1) // AI vslot 0x1B8 (HERO-1's reading, S-854)
	{
		return;
	}
	if (!obj->testModelCondition(kMcDisguised))
	{
		if (ContainModuleInterface *c = obj->getContain())
		{
			if (c->getContainCount() != 0)
			{
				++m_unported; // RW 0x8B478F: the contain's vslot 0x114(0) / 0xA8(0) (its riders: S-1221)
			}
		}
		// RW 0x8B47A7: the drawable's fade (RW 0x6760F9, client)
		setCondition(*obj, kMcDisguised, true); // Object + 0x130 bit 12
		// RW 0x8B47CE: the drawable is the object's own for its local owner (ThePlayerList + 0x10 is the controlling player), DisguiseAsTemplate's for the
		// others; the owner's EVA DisguiseStarted (RW 0x8B4808 ..) is the client's
		const Player *local = obj->logic().players().getLocalPlayer();
		applyLook(local != nullptr && local == obj->getControllingPlayer());
		++m_disguises;
	}
	else
	{
		setCondition(*obj, kMcDisguised, false); // RW 0x8B486F
		applyLook(true);
	}
}

// RW 0x8B45BF: ForceMountedWhenDisguising mounts a DISGUISED object (MOUNTED, locomotor set 7, weapon set MOUNTED, armor set MOUNTED: the ToggleMounted
// mount of RW 0x8B1690); then, when DisguiseAsTemplate names a template (RW 0x8B442B -> 0x6D1305), the drawable is replaced (TheGameClient vslot 0x74,
// RW 0x6CFE8C, RW 0x625AA4) by one of the object's own template (`ownLook`) or of DisguiseAsTemplate, at the object's position and orientation, with its
// model conditions, the old drawable's colour (+ 0x44C, RW 0x672C32) and the module's opacity (+ 0x8C): a client event here (ObjectClientHooks)
void SpecialDisguiseUpdate::applyLook(bool ownLook)
{
	Object *obj = getObject();
	if (m_sd->m_forceMountedWhenDisguising && obj->testModelCondition(kMcDisguised))
	{
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		setCondition(*obj, kMcMounted, true);         // Object + 0x124 bit 22
		if (ai)
		{
			ai->chooseLocomotorSet(kLocomotorSetMounted); // AI vslot 0x238(7)
		}
		if (ObjectWeapons *weapons = obj->getWeapons())
		{
			weapons->setWeaponSetFlag(kWeaponSetMounted, true); // RW 0x691059(0x15)
		}
		obj->setArmorSetFlag(kArmorSetMounted, true); // RW 0x68BE59(6)
	}
	const ThingTemplate *disguise = m_sd->m_disguiseAsTemplate.empty() ? nullptr : obj->logic().things().findTemplate(m_sd->m_disguiseAsTemplate);
	if (!disguise)
	{
		return;
	}
	if (ObjectClientHooks *client = obj->clientHooks())
	{
		const Player *own = obj->getControllingPlayer();
		client->replaceDrawable(*obj, ownLook ? obj->getTemplate() : disguise, own && own->hasTeamColor(), own ? own->getPlayerColor() : 0u);
	}
}

// RW 0x8B4702
void SpecialDisguiseUpdate::cancelDisguise(bool keepLook)
{
	Object *obj = getObject();
	if (!obj->testModelCondition(kMcDisguised))
	{
		return;
	}
	setCondition(*obj, kMcDisguised, false);
	if (!keepLook)
	{
		applyLook(true);
		if (FXEventLog::isFXName(m_sd->m_disguiseFX))
		{
			GameLogic &logic = obj->logic();
			logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "SpecialDisguiseUpdate", logic.getFrame(), m_sd->m_disguiseFX, *obj)); // RW 0x494615
		}
	}
}

// RW 0x8B4455: the packing frames also fade the drawable (+ 0x8C): unpacking from 1.0 to OpacityTarget over UnpackTime, packing back over PackTime, in the
// x87 chain fild left / fild total, 1.0 - q, (to - from) * that + from under PC24
bool SpecialDisguiseUpdate::handlePackingProcessing()
{
	const bool busy = SpecialAbilityUpdate::handlePackingProcessing();
	if (!busy)
	{
		return busy;
	}
	float from, to;
	unsigned total;
	if (m_packingState == 2)
	{
		from = 1.0f;
		to = m_sd->m_opacityTarget;
		total = m_sd->m_unpackFrames; // data + 0x88
	}
	else
	{
		from = m_sd->m_opacityTarget;
		to = 1.0f;
		total = m_sd->m_packFrames; // data + 0x84
	}
	const double q = SimMath::pc24SubW(1.0, SimMath::pc24DivW(SimMath::fildU32(m_packFramesLeft), SimMath::fildU32(total)));
	m_opacity = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24SubW((double)to, (double)from), q), (double)from));
	// RW 0x8B44F0: the drawable's opacity (+ 0xB0) follows (client)
	return busy;
}

// RW 0x8B4521
void SpecialDisguiseUpdate::finishAbility(bool ownerDying, bool aborted)
{
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted);
	m_opacity = 1.0f; // RW 0x8B4545: the drawable's opacity back to 1.0 (client)
}

void SpecialDisguiseUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addFloat(m_opacity);
	h.addU32(m_disguises);
}

// ---------------------------------------------------------------------------------------------------------------------------------
SpecialPowerUpdateInterface *SpecialAbilityModules::findUpdate(const Object &obj, const SpecialPowerTemplate *t)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		SpecialPowerUpdateInterface *u = dynamic_cast<SpecialPowerUpdateInterface *>(m.get());
		if (u && u->drivesSpecialPower(t))
		{
			return u;
		}
	}
	return nullptr;
}

void SpecialAbilityModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<SpecialAbilityUpdate, SpecialAbilityUpdateModuleData>(modules, "SpecialAbilityUpdate");
	bindRuntime<WeaponFireSpecialAbilityUpdate, WeaponFireSpecialAbilityUpdateModuleData>(modules, "WeaponFireSpecialAbilityUpdate");
	bindRuntime<ToggleMountedSpecialAbilityUpdate, ToggleMountedSpecialAbilityUpdateModuleData>(modules, "ToggleMountedSpecialAbilityUpdate");
	bindRuntime<HeroModeSpecialAbilityUpdate, HeroModeSpecialAbilityUpdateModuleData>(modules, "HeroModeSpecialAbilityUpdate");
	bindRuntime<LevelGrantSpecialPower, LevelGrantSpecialPowerModuleData>(modules, "LevelGrantSpecialPower");
	bindRuntime<ModelConditionSpecialAbilityUpdate, ModelConditionSpecialAbilityUpdateModuleData>(modules, "ModelConditionSpecialAbilityUpdate");
	bindRuntime<SpecialDisguiseUpdate, SpecialDisguiseUpdateModuleData>(modules, "SpecialDisguiseUpdate"); // lane HERO-2
	HeroAbilityModules::registerAll(modules);                                                              // lane HERO-2
}

std::vector<std::string> SpecialAbilityModules::stopLines()
{
	return {
		"[S-859] SpecialAbilityUpdate details not ported: the face command before the trigger (AI command RW 0x7C81B9 / 0x7C821E: the check passes the frame after the AI's idle, no turn) "
		"only), the line of sight of ApproachRequiresLOS (RW 0x6613D3 / 0x6616AC: always clear), the approach point RW 0x851607 (the move goes to the object), "
		"Object + 0xA0 bit 6 in the command source test, PreTriggerUnstealthTime (DisableWhenWearingTheRing and LoseStealthOnTrigger: lane STEALTH-2), the paralysis "
		"commands, FlipOwnerAfterPacking / Unpacking, FleeRangeAfterCompletion, ChainedButton, a horde's SPECIAL_WEAPON condition to its members, the power "
		"module's finish notice (vslot 0x4C), sounds and voices (client): each counted (unported())",
		"[S-860] SpecialAbilityUpdate ability enums not ported: SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES (0x15), the black lotus capture's special objects "
		"(0x1A; SPECIAL_INFANTRY_CAPTURE_BUILDING 0x1D runs: lane HERO-2, S-1225), "
		"SPECIAL_GRAB_PASSENGER / SPECIAL_GRAB_CHUNK (0x27 / 0x28) and SPECIAL_CHARGE_ATTACK (0x2A) in the trigger / preparation / packing (the 0x20 disguise trigger "
		"RW 0x8544CD runs: lane STEALTH-2; SPECIAL_SCREECH 0x89, the disguise cancels 0x84 / 0x85: lane HERO-2), the special objects (SpecialObject: no retail use), "
		"GrabPassengerAnimAndDuration, ContactPointOverride, the unit cost of the summoning templates (+ 0x80): counted",
		"[S-861] ToggleMountedSpecialAbilityUpdate: the toggle, CancelDisguiseWhenDismounting and MountedTemplate (the replacement RW 0x8B140D: Construction::"
		"buildObjectNow, name, experience, health, team, SynchronizeTimerOnSpecialPower; the old object destroyed after its packing, RW 0x8B1E9A) run (lane "
		"HERO-2); not ported: the replacement's + 0x488 (RW 0x5EA74E), the AI group (RW 0x68B36F -> 0x6682B1), the script engine's name record (RW 0x60A1D5) "
		"and the last AI command given again (RW 0x66D7C9), the riders' eject on dismount, the opacity fade (client): counted",
		"[S-863] the retail ability run (test_hero_retail.cpp checkAbilities) reports, per faction, the abilities whose effect it cannot see; Screech, the fell beasts' landing and the Horn of Gondor are measured since lane HERO-2. Explained by the run's conditions: Train Archers (LevelGrantSpecialPower: none of its AcceptanceFilter's archers is near), Glorfindel's Wind Rider and the Goblin King's Poisoned Stinger (RequiredConditions MOUNTED: cast unmounted, refused as retail refuses them), Thranduil's Move Unseen (no ally in its radius), Gandalf's Part the Heavens (OCLSpecialPower: the OCL's objects are not measured), the passive / fake leadership buttons, the level attack (S-1223), ActivateModuleSpecialPower's dummy power, Sharku's Man-Eater Drop (FlingPassengerSpecialAbilityUpdate with TransportContain: lane GARRISON-2). Explained too: Faramir's mount toggle in the faction run (he is attacking at both triggers and the retail idle gate, RW 0x8B1690, the AI's isIdle, refuses the mount; alone and idle he mounts)",
	};
}

void SpecialAbilityModules::parseFXField(INI *ini, void *instance, void *store, const void *userData)
{
	parseFX(ini, instance, store, userData);
}

void SpecialAbilityModules::parseObjectFilterField(INI *ini, void *instance, void *store, const void *userData)
{
	parseFilter(ini, instance, store, userData);
}

std::vector<std::string> SpecialAbilityModules::hero2StopLines()
{
	std::vector<std::string> lines = {
		"[S-1221] SpecialDisguiseUpdate (lane HERO-2): the disguise toggle (DISGUISED, ForceMountedWhenDisguising's mount), the drawable swap (a client event: the "
		"local owner's client keeps the own look, RW 0x8B47CE), DisguiseFX and the five cancels run; not ported: the riders of a contain at the disguise (RW "
		"0x8B478F: contain vslots 0x114 / 0xA8, no retail disguiser has one: counted), the drawable fade and opacity (+ 0x8C is kept, the drawable's + 0xB0 is the "
		"client's), the owner's EVA DisguiseStarted and the enemy-perspective portrait / name (DisguisedAsTemplate_EnemyPerspective, RW 0x8B4440 from RW 0x69194E: "
		"client)",
		"[S-1225] the capture (lane HERO-2): SPECIAL_INFANTRY_CAPTURE_BUILDING's preparation, unpack, trigger (RW 0x8544F2: the closest LINKED_TO_FLAG within 150, "
		"RW 0x6996DC setCapturedTeam), end and the update's captured / ruined target tests run; not ported (noted): a garrisonable contain's passengers (vslots "
		"0xA8 / 0x54), an UNDER_CONSTRUCTION building's production cancel and score counts, the upgrades reset RW 0x68E083 (S-952), the skirmish AI's object "
		"events RW 0x6A99CB / 0x6A9D9C, RW 0x6996DC's production cancel, score counts, RW 0x68B303 / 0x8E3ABB and a captured container's passengers; "
		"SPECIAL_BLACKLOTUS_CAPTURE_BUILDING (0x1A)'s special objects; DoCaptureFX's flash (client); INFERENCE: a target that is gone ends any ability (HERO-1's "
		"reading; retail only tests a live target)",
	};
	for (const std::string &l : HeroAbilityModules::stopLines())
	{
		lines.push_back(l);
	}
	return lines;
}
