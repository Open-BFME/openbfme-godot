// OpenBFME. GPL-3.0.
// See GameLogic/Module/InvisibilityModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <variant>

namespace
{
void parseUpgradeMask(INI *ini, void *, void *store, const void *)
{
	UpgradeCenter::parseUpgradeMask(ini, *static_cast<UpgradeMaskType *>(store), nullptr); // RW 0x66F603
}
void parseWeaponConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<WeaponConditionFlags *>(store)->data(), 4, TheWeaponConditionNames); // RW 0x6C9951
}
// RW 0x73B217 -> 0x73AA94: "NoSound" (any case) clears; otherwise TheAudio must know the event (the contain modules' lookup), else INIException 3
void parseAudioEvent(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *static_cast<std::string *>(store);
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		out.clear();
		return;
	}
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.audioEventExists)
	{
		throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed (acceptance stop S-083)", name.c_str());
	}
	if (!hooks.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	out = name;
}
// RW 0x73A302: "None" stores none, else the FXList must exist (TheFXListStore), else INIException 3
void parseFXName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *static_cast<std::string *>(store);
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		out.clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	out = name;
}
// the particle system (RW 0x73AECB), audio (RW 0x73ACEF) and Eva (RW 0x5DE588) names whose registries other lanes own: kept by name (S-1041)
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define IU_OFF(m) (int)offsetof(InvisibilityUpdateModuleData, m)
const FieldParse kInvisibilityUpdate[] = { // RW 0xC68E58
	{ "InvisibilityNugget", InvisibilityNugget::parse, nullptr, IU_OFF(m_nugget) },
	{ "UpdatePeriod", INI::parseDurationUnsignedInt, nullptr, IU_OFF(m_updatePeriod) },
	{ "RequiredUpgrades", parseUpgradeMask, nullptr, IU_OFF(m_requiredUpgrades) },
	{ "ForbiddenUpgrades", parseUpgradeMask, nullptr, IU_OFF(m_forbiddenUpgrades) },
	{ "Broadcast", INI::parseBool, nullptr, IU_OFF(m_broadcast) },
	{ "BroadcastObjectFilter", ParseObjectFilter, nullptr, IU_OFF(m_broadcastObjectFilter) },
	{ "BroadcastRange", INI::parseReal, nullptr, IU_OFF(m_broadcastRange) },
	{ "StartsActive", INI::parseBool, nullptr, IU_OFF(m_startsActive) },
	{ "UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea", INI::parseAsciiString, nullptr, IU_OFF(m_voiceMoveToStealthyArea) },
	{ "UnitSpecificSoundNameToUseAsVoiceEnterStateMoveToStealthyArea", INI::parseAsciiString, nullptr, IU_OFF(m_voiceEnterStateMoveToStealthyArea) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef IU_OFF

#define SD_OFF(m) (int)offsetof(StealthDetectorUpdateModuleData, m)
const FieldParse kStealthDetectorUpdate[] = { // RW 0xC68B78
	{ "DetectionRate", INI::parseDurationUnsignedInt, nullptr, SD_OFF(m_detectionRate) },
	{ "DetectionRange", INI::parseReal, nullptr, SD_OFF(m_detectionRange) },
	{ "InitiallyDisabled", INI::parseBool, nullptr, SD_OFF(m_initiallyDisabled) },
	{ "PingSound", parseAudioEvent, nullptr, SD_OFF(m_pingSound) },
	{ "LoudPingSound", parseAudioEvent, nullptr, SD_OFF(m_loudPingSound) },
	{ "IRBeaconParticleSysName", parseName, nullptr, SD_OFF(m_irBeaconParticleSysName) },
	{ "IRParticleSysName", parseName, nullptr, SD_OFF(m_irParticleSysName) },
	{ "IRBrightParticleSysName", parseName, nullptr, SD_OFF(m_irBrightParticleSysName) },
	{ "IRGridParticleSysName", parseName, nullptr, SD_OFF(m_irGridParticleSysName) },
	{ "IRParticleSysBone", INI::parseAsciiString, nullptr, SD_OFF(m_irParticleSysBone) },
	{ "ExtraRequiredKindOf", ParseKindOfMask, nullptr, SD_OFF(m_extraRequiredKindOf) },
	{ "ExtraForbiddenKindOf", ParseKindOfMask, nullptr, SD_OFF(m_extraForbiddenKindOf) },
	{ "CanDetectWhileGarrisoned", INI::parseBool, nullptr, SD_OFF(m_canDetectWhileGarrisoned) },
	{ "CanDetectWhileContained", INI::parseBool, nullptr, SD_OFF(m_canDetectWhileContained) },
	{ "CancelOneRingEffect", INI::parseBool, nullptr, SD_OFF(m_cancelOneRingEffect) },
	{ "RequiredUpgrade", INI::parseAsciiString, nullptr, SD_OFF(m_requiredUpgrade) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SD_OFF

// RW 0xDA5524 (13 names, NULL-terminated)
const char *const kStealthLevelNames[] = { "ATTACKING", "MOVING", "USING_ABILITY", "FIRING_PRIMARY", "FIRING_SECONDARY", "FIRING_TERTIARY", "FIRING_QUATERNARY",
	"FIRING_QUINARY", "MOUNTED", "AWAY_FROM_TREES", "HORDEBRAIN_NOT_STEALTHED", "PRIMARY_FORMATION", "TAKING_DAMAGE", nullptr };

#define SU_OFF(m) (int)offsetof(StealthUpdateModuleData, m)
const FieldParse kStealthUpdate[] = { // RW 0xC2E968, in the binary's row order
	{ "StealthDelay", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_stealthDelay) },
	{ "MoveThresholdSpeed", INI::parseVelocityReal, nullptr, SU_OFF(m_moveThresholdSpeed) },
	{ "StealthForbiddenConditions", INI::parseBitString32, kStealthLevelNames, SU_OFF(m_stealthForbiddenConditions) },
	{ "RemoveTerrainRestrictionOnUpgrade", INI::parseAsciiStringVector, nullptr, SU_OFF(m_removeTerrainRestrictionOnUpgrade) },
	{ "HintDetectableConditions", ParseObjectStatusMask, nullptr, SU_OFF(m_hintDetectableConditions) },
	{ "FriendlyOpacityMin", INI::parsePercentToReal, nullptr, SU_OFF(m_friendlyOpacityMin) },
	{ "FriendlyOpacityMax", INI::parsePercentToReal, nullptr, SU_OFF(m_friendlyOpacityMax) },
	{ "PulseFrequency", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_pulseFrequency) },
	{ "DisguisesAsTeam", INI::parseBool, nullptr, SU_OFF(m_disguisesAsTeam) },
	{ "RevealDistanceFromTarget", INI::parseReal, nullptr, SU_OFF(m_revealDistanceFromTarget) },
	{ "OrderIdleEnemiesToAttackMeUponReveal", INI::parseBool, nullptr, SU_OFF(m_orderIdleEnemiesToAttackMeUponReveal) },
	{ "DisguiseFX", parseFXName, nullptr, SU_OFF(m_disguiseFX) },
	{ "DisguiseRevealFX", parseFXName, nullptr, SU_OFF(m_disguiseRevealFX) },
	{ "DisguiseTransitionTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_disguiseTransitionTime) },
	{ "DisguiseRevealTransitionTime", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_disguiseRevealTransitionTime) },
	{ "StartsActive", INI::parseBool, nullptr, SU_OFF(m_startsActive) },
	{ "InnateStealth", INI::parseBool, nullptr, SU_OFF(m_innateStealth) },
	{ "DetectedByFriendliesOnly", INI::parseBool, nullptr, SU_OFF(m_detectedByFriendliesOnly) },
	{ "DetectedByAnyoneRange", INI::parseReal, nullptr, SU_OFF(m_detectedByAnyoneRange) },
	{ "BecomeStealthedFX", parseFXName, nullptr, SU_OFF(m_becomeStealthedFX) },
	{ "ExitStealthFX", parseFXName, nullptr, SU_OFF(m_exitStealthFX) },
	{ "RevealWeaponSets", parseWeaponConditions, nullptr, SU_OFF(m_revealWeaponSets) },
	{ "VoiceMoveToStealthyArea", parseName, nullptr, SU_OFF(m_voiceMoveToStealthyArea) },
	{ "VoiceEnterStateMoveToStealthyArea", parseName, nullptr, SU_OFF(m_voiceEnterStateMoveToStealthyArea) },
	{ "OneRingDelayOn", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_oneRingDelayOn) },
	{ "OneRingDelayOff", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_oneRingDelayOff) },
	{ "RingAnimTimeOn", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_ringAnimTimeOn) },
	{ "RingAnimTimeOff", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_ringAnimTimeOff) },
	{ "RingDelayAfterRemoving", INI::parseDurationUnsignedInt, nullptr, SU_OFF(m_ringDelayAfterRemoving) },
	{ "BecomeStealthedOneRingFX", parseFXName, nullptr, SU_OFF(m_becomeStealthedOneRingFX) },
	{ "ExitStealthOneRingFX", parseFXName, nullptr, SU_OFF(m_exitStealthOneRingFX) },
	{ "EvaEventDetectedEnemy", parseName, nullptr, SU_OFF(m_evaEventDetectedEnemy) },
	{ "EvaEventDetectedAlly", parseName, nullptr, SU_OFF(m_evaEventDetectedAlly) },
	{ "EvaEventDetectedOwner", parseName, nullptr, SU_OFF(m_evaEventDetectedOwner) },
	{ "RequiredUpgradeNames", INI::parseAsciiStringVector, nullptr, SU_OFF(m_requiredUpgradeNames) },
	{ "ForbiddenUpgradeNames", INI::parseAsciiStringVector, nullptr, SU_OFF(m_forbiddenUpgradeNames) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SU_OFF

// RW 0x73BE09: every required bit and no forbidden bit (36 words)
bool upgradesAllow(const UpgradeMaskType &have, const UpgradeMaskType &required, const UpgradeMaskType &forbidden)
{
	for (size_t i = 0; i < have.words.size(); ++i)
	{
		if ((have.words[i] & forbidden.words[i]) != 0 || (required.words[i] & have.words[i]) != required.words[i])
		{
			return false;
		}
	}
	return true;
}

float templateVisionRange(const Object &o)
{
	if (const FieldValue *v = o.getTemplate()->getFinalOverride()->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}

bool kindOfMaskAny(const KindOfMaskType &m)
{
	for (std::uint32_t w : m)
	{
		if (w)
		{
			return true;
		}
	}
	return false;
}

void noteStop(const Object &obj, const std::string &what)
{
	obj.logic().noteStop("[S-1041] " + what);
}

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
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
} // namespace

void InvisibilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kInvisibilityUpdate);
}
void StealthDetectorUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kStealthDetectorUpdate);
}
void StealthUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kStealthUpdate);
}
const char *const *StealthUpdateModuleData::forbiddenConditionNames()
{
	return kStealthLevelNames;
}

// ---- InvisibilityUpdate -------------------------------------------------------------------------------------------------------------
// RW 0x8A6EA6
InvisibilityUpdate::InvisibilityUpdate(Thing *thing, const InvisibilityUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_active(data->m_startsActive)
{
}

// RW 0x8A6F84
void InvisibilityUpdate::onObjectCreated()
{
	setWakeFrame(getObject(), m_active ? update() : UPDATE_SLEEP_FOREVER);
}

// RW 0x8A7099
UpdateSleepTime InvisibilityUpdate::update()
{
	Object *obj = getObject();
	if (!m_active || !obj)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (upgradesAllow(obj->getUpgradeMask(), m_data->m_requiredUpgrades, m_data->m_forbiddenUpgrades))
	{
		GameLogic &logic = obj->logic();
		InvisibilityManager &mgr = logic.invisibility();
		if (!m_data->m_broadcast)
		{
			mgr.applyNugget(obj, m_data->m_updatePeriod, m_data->m_nugget);
			++m_applications;
		}
		else
		{
			// RW 0x8A70F1 .. 0x8A716F: ThePartitionManager->iterateObjectsInRange(position, BroadcastRange, distance type 0, the ObjectFilter for the owner (RW
			// 0xBE4CC8, flag 1), near to far) (lane MODULES-2)
			const Player *player = obj->getControllingPlayer(); // RW 0x68B678
			PartitionFilterFn allowed([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_broadcastObjectFilter, o, player); }); // RW 0x66122D
			std::vector<Object *> hits;
			for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_broadcastRange, FROM_CENTER_2D, { &allowed },
			                                                                       ITER_SORTED_NEAR_TO_FAR))
			{
				hits.push_back(hit.object);
			}
			for (Object *o : hits)
			{
				mgr.applyNugget(o, m_data->m_updatePeriod, m_data->m_nugget);
				++m_applications;
			}
		}
	}
	return UPDATE_SLEEP((int)m_data->m_updatePeriod); // RW 0x8A7199
}

// RW 0x8A7049
void InvisibilityUpdate::setActive(bool on)
{
	if (m_data->m_startsActive)
	{
		return;
	}
	if (on)
	{
		if (!m_active)
		{
			setWakeFrame(getObject(), UPDATE_SLEEP(1));
			m_active = true;
		}
	}
	else
	{
		m_active = false;
	}
}

void InvisibilityUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_active);
	h.addU32(m_applications);
}

// ---- StealthDetectorUpdate ----------------------------------------------------------------------------------------------------------
// RW 0x8A635B
StealthDetectorUpdate::StealthDetectorUpdate(Thing *thing, const StealthDetectorUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (data->m_initiallyDisabled)
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	}
	else
	{
		const int delay = getObject()->logic().random().getValue(1, (int)data->m_detectionRate, "StealthDetectorUpdate.cpp", 0x4F); // RW 0x6D328E
		setWakeFrame(getObject(), UPDATE_SLEEP(delay));
	}
}

// RW 0x8A641A
UpdateSleepTime StealthDetectorUpdate::update()
{
	Object *obj = getObject();
	static const int kPhantom = CombatNames::status("PHANTOM_STRUCTURE");   // 0x58
	static const int kUnderConstruction = CombatNames::status("UNDER_CONSTRUCTION"); // 2
	static const int kSold = CombatNames::status("SOLD");                   // 0x13
	if (obj->testStatus((unsigned)kPhantom))
	{
		return UPDATE_SLEEP_NONE;
	}
	if (obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (obj->testStatus((unsigned)kUnderConstruction))
	{
		return UPDATE_SLEEP_NONE;
	}
	if (obj->testStatus((unsigned)kSold))
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (Object *container = obj->getContainedBy()) // RW 0x8A6477: + 0x27C, its contain + 0x258, slot 0x10 (garrisonable)
	{
		if (ContainModuleInterface *c = container->getContain())
		{
			// lane GARRISON-1: the contain's slot 0x10 is read (ContainModuleInterface::isGarrisonable: GarrisonContain RW 0x8BD372 true, the others false)
			if (c->isGarrisonable() && !m_data->m_canDetectWhileContained)
			{
				return UPDATE_SLEEP((int)m_data->m_detectionRate);
			}
		}
	}
	if (!m_data->m_requiredUpgrade.empty()) // RW 0x8A64AB
	{
		const UpgradeTemplate *u = Player::resolveUpgrade(m_data->m_requiredUpgrade, false); // RW 0x66F5E5
		if (!u || !obj->hasUpgrade(u))                                                    // RW 0x691421
		{
			return UPDATE_SLEEP_NONE;
		}
	}
	float range = templateVisionRange(*obj); // RW 0x68E43B
	if (m_data->m_detectionRange > 0.0f)     // RW 0x8A64F2: comiss with 0.0
	{
		range = m_data->m_detectionRange;
	}
	++m_scans;
	GameLogic &logic = obj->logic();
	const bool required = kindOfMaskAny(m_data->m_extraRequiredKindOf);
	// RW 0x8A6500 .. 0x8A659B: ThePartitionManager->iterateObjectsInRange(position, range, distance type 0, ITER_FASTEST; a contain's rider out of the world, RW
	// 0x68C18F, is not in the partition: a garrison's occupants are reached through detectOccupants only) through the chain in RW's order: "the
	// detector sees it" (RW 0xC1D66C -> 0x68FA3D(o, range); INFERENCE S-1041: within the range, which the query's centre distance already enforces), the same
	// map status (RW 0xC0F374: always), the KindOf masks (RW 0x445139: every required bit when any, no forbidden bit) and the detector's relationship to it ENEMIES
	// or NEUTRAL (RW 0xC11DC0, mask 3, flag 0)
	PartitionFilterFn kinds([&](Object &o) {
		const KindOfMaskType &k = o.getKindOf();
		for (size_t i = 0; i < k.size(); ++i)
		{
			if ((k[i] & m_data->m_extraForbiddenKindOf[i]) != 0 || (required && (k[i] & m_data->m_extraRequiredKindOf[i]) != m_data->m_extraRequiredKindOf[i]))
			{
				return false;
			}
		}
		return true;
	});
	PartitionFilterFn relationship([&](Object &o) {
		const Relationship rel = obj->getRelationship(o);
		return rel == ENEMIES || rel == NEUTRAL;
	});
	std::vector<Object *> hits;
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), range, FROM_CENTER_2D, { &kinds, &relationship }, ITER_FASTEST))
	{
		hits.push_back(hit.object);
	}
	static const int kCanStealth = CombatNames::status("CAN_STEALTH"); // 0x12
	const unsigned frames = m_data->m_detectionRate + 1;
	for (Object *o : hits)
	{
		if (o->isEffectivelyDead()) // RW 0x8A65C0
		{
			continue;
		}
		// RW 0x8A65F1 .. 0x8A6644 (lane HERO-2): a DISGUISED candidate's SpecialDisguiseUpdate ends the disguise with DisguiseFX (RW 0x8B4702(0))
		if (o->testModelCondition(300))
		{
			if (SpecialDisguiseUpdate *sd = SpecialDisguiseUpdate::of(*o))
			{
				sd->cancelDisguise(false);
			}
		}
		logic.invisibility().markDetected(o, obj, frames, 2); // RW 0x8A6685
		++m_marks;
		if (StealthUpdate *st = StealthUpdate::of(*o)) // RW 0x8A668D: the candidate's StealthUpdate (RW 0x68FBD3) with CAN_STEALTH
		{
			if (o->testStatus((unsigned)kCanStealth))
			{
				st->markAsDetected(frames, 2, obj, true); // RW 0x8A66C0
				if (m_data->m_cancelOneRingEffect && st->ringWorn())
				{
					st->toggleRing(); // RW 0x8A66C5 .. 0x8A66D4: CancelOneRingEffect takes the ring off (lane STEALTH-2)
				}
				continue;
			}
		}
		// RW 0x8A676F .. 0x8A6838: the occupants of a container whose contain answers slot 0x10 (garrisonable: GarrisonContain / HordeGarrisonContain), each with a
		// StealthUpdate, not the detector's player's and not ALLIES to the detector: marked through it and through the manager
		ContainModuleInterface *c = o->getContain();
		if (!c || !c->isGarrisonable() || !c->getContainedItemsList()) // lane GARRISON-1: slot 0x10 (RW 0x8A6789)
		{
			continue;
		}
		detectOccupants(*obj, *c->getContainedItemsList(), m_data->m_detectionRate);
	}
	return UPDATE_SLEEP((int)m_data->m_detectionRate);
}

// RW 0x8A67C2 .. 0x8A682F: each occupant with a StealthUpdate (RW 0x68FBD3) that is not the detector's player's and that the detector does not see as ALLIES is
// marked through it and through the manager for DetectionRate + 2 frames (RW 0x8A680C / 0x8A6823; an ordinary candidate gets + 1)
void StealthDetectorUpdate::detectOccupants(Object &detector, const ContainModuleInterface::ContainedItemsList &occupants, unsigned detectionRate)
{
	const unsigned frames = detectionRate + 2;
	for (Object *m : occupants)
	{
		StealthUpdate *ms = m ? StealthUpdate::of(*m) : nullptr;
		if (!ms || m->getControllingPlayer() == detector.getControllingPlayer() || detector.getRelationship(*m) == ALLIES)
		{
			continue;
		}
		ms->markAsDetected(frames, 2, &detector, true);
		detector.logic().invisibility().markDetected(m, &detector, frames, 2);
	}
}

void StealthDetectorUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_scans);
	h.addU32(m_marks);
}

// ---- StealthUpdate --------------------------------------------------------------------------------------------------------------------
namespace
{
struct StealthBits
{
	int canStealth = CombatNames::status("CAN_STEALTH");     // 0x12
	int stealthed = CombatNames::status("STEALTHED");        // 0xF
	int detected = CombatNames::status("DETECTED");          // 0x11
	int usingAbility = CombatNames::status("USING_ABILITY"); // 0x18
	int mounted = CombatNames::modelCondition("MOUNTED");    // 0xD6
	int horde = CombatNames::kindOf("HORDE");
	int projectile = CombatNames::kindOf("PROJECTILE");                                // 0x19
	int ignoreForEva = CombatNames::kindOf("IGNORE_FOR_EVA_SPEECH_POSITION");          // 0x9E
	int oneRing = CombatNames::modelCondition("ONE_RING");                             // 0xFD (Object + 0x128 bit 29)
	int puttingOnRing = CombatNames::modelCondition("PUTTING_ON_RING");                // 0x104
	int takingOffRing = CombatNames::modelCondition("TAKING_OFF_RING");                // 0x105
	int ringWeaponSet = CombatNames::weaponSetBit("WEAPONSET_ONE_RING_MODE");          // 0x1C
};
constexpr unsigned kDisabledHeld = 3; // RW 0x6907F1(3, ...)
const StealthBits &stealthBits()
{
	static const StealthBits b;
	return b;
}

template <class F>
void forEachMember(Object &horde, F f)
{
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			for (Object *m : *items)
			{
				if (m)
				{
					f(*m);
				}
			}
		}
	}
}

// RW 0x66F603's resolution at RW 0x776C0C: a name TheUpgradeCenter does not know is skipped
void maskOf(const std::vector<std::string> &names, UpgradeMaskType &mask)
{
	for (const std::string &n : names)
	{
		if (const UpgradeTemplate *u = Player::resolveUpgrade(n, false))
		{
			mask.set((unsigned)u->getMaskBit());
		}
	}
}
} // namespace

StealthUpdate *StealthUpdate::of(const Object &obj)
{
	return dynamic_cast<StealthUpdate *>(obj.findModule("StealthUpdate"));
}

// RW 0x776E3D
StealthUpdate::StealthUpdate(Thing *thing, const StealthUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_enabled(!data->m_disguisesAsTeam)
{
	if (data->m_startsActive)
	{
		setStatusAll(stealthBits().canStealth, true); // RW 0x776ED9
	}
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x776EEC
}

// RW 0x776BF8
void StealthUpdate::onObjectCreated()
{
	maskOf(m_data->m_requiredUpgradeNames, m_requiredUpgrades);
	maskOf(m_data->m_forbiddenUpgradeNames, m_forbiddenUpgrades);
}

// RW 0x77606E
unsigned StealthUpdate::forbiddenConditions() const
{
	return m_forbiddenOverride >= 0 ? (unsigned)m_forbiddenOverride : m_data->m_stealthForbiddenConditions;
}

// RW 0x77670A
void StealthUpdate::setStatusAll(int status, bool on)
{
	Object *obj = getObject();
	if (obj->isKindOf((unsigned)stealthBits().horde))
	{
		forEachMember(*obj, [&](Object &m) { m.setStatus((unsigned)status, on); }); // RW 0x7766F6
	}
	obj->setStatus((unsigned)status, on); // RW 0x62684D
}

// RW 0x776213
void StealthUpdate::extendDetection(unsigned frames)
{
	const unsigned now = getObject()->logic().getFrame();
	if (frames == 0)
	{
		m_detectionExpires = now + m_data->m_stealthDelay;
	}
	else if (now + frames > m_detectionExpires)
	{
		m_detectionExpires = now + frames;
	}
}

void StealthUpdate::playFX(const std::string &fx)
{
	if (!FXEventLog::isFXName(fx))
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "StealthUpdate", logic.getFrame(), fx, *getObject())); // RW 0x4B1B5A
}

// RW 0x776B50 (lane STEALTH-2): MSG_ONE_RING's group toggle (RW 0x7729A6), the detector's CancelOneRingEffect (RW 0x8A66D4) and RW 0x89D41B call it
void StealthUpdate::toggleRing()
{
	if (m_data->m_ringAnimTimeOn == 0 && m_data->m_ringDelayAfterRemoving == 0) // + 0x98 / + 0xA0
	{
		return;
	}
	Object *obj = getObject();
	const StealthBits &b = stealthBits();
	const unsigned now = obj->logic().getFrame();
	if (!m_ring)
	{
		if (m_ringReadyFrame > now)
		{
			return;
		}
		m_detectionExpires = 0;
		m_stealthAllowedFrame = m_data->m_oneRingDelayOn + now;
		if (!obj->testModelCondition(b.oneRing))
		{
			obj->setModelConditionState(b.oneRing, true); // Object + 0x128 bit 29, RW 0x68B53C
		}
		obj->setSpecialModelConditionState(b.puttingOnRing, m_data->m_ringAnimTimeOn); // RW 0x68B581(0x104, RingAnimTimeOn)
		obj->setDisabled(kDisabledHeld, m_stealthAllowedFrame);                     // RW 0x6907F1(3, the allowed frame)
		if (ObjectWeapons *w = obj->getWeapons())
		{
			w->setWeaponSetFlag(b.ringWeaponSet, true); // RW 0x691059(0x1C)
		}
		m_ring = true;
	}
	else
	{
		obj->setSpecialModelConditionState(b.takingOffRing, m_data->m_ringAnimTimeOff); // RW 0x68B581(0x105, RingAnimTimeOff)
		m_detectionExpires = m_data->m_oneRingDelayOff + now;                         // the ring comes off then (RW 0x777377)
	}
}

// RW 0x776117 (lane STEALTH-2): SpecialAbilityUpdate's SPECIAL_DISGUISE_AS_VEHICLE trigger (RW 0x8544E8) with the target; markAsDetected (RW 0x7768AF) and
// a second StealthUpdate of the object (RW 0x777A73) with null
void StealthUpdate::disguiseAsObject(Object *target)
{
	Object *obj = getObject();
	if (target && target->getControllingPlayer()) // RW 0x68B678
	{
		const StealthUpdate *ts = of(*target);
		if (!ts || !ts->m_disguiseTemplate)
		{
			m_disguiseTemplate = target->getTemplate(); // Thing + 4
			m_disguisePlayer = target->getControllingPlayer()->getPlayerIndex(); // Player + 0x54
		}
		else
		{
			m_disguiseTemplate = ts->m_disguiseTemplate; // a disguised target: its disguise
			m_disguisePlayer = ts->m_disguisePlayer;
		}
		m_enabled = true;
		m_disguising = true;
		m_transitionFrames = m_data->m_disguiseTransitionTime; // + 0x58
		m_halfway = false;
		setWakeFrame(obj, UPDATE_SLEEP(1));                                // RW 0x850C32(object, 1)
		obj->logic().invisibility().markDetected(obj, nullptr, 0, 1);      // RW 0x81C32C(object, 0, 0, 1)
	}
	else if (m_disguised) // + 0x46
	{
		m_disguiseTemplate = nullptr;
		m_disguisePlayer = 0; // RW 0x776192 (the swap then resets it to -1)
		m_transitionFrames = m_data->m_disguiseRevealTransitionTime; // + 0x5C
		m_disguising = false;
		m_halfway = false;
	}
	// RW 0x7761B7: a selected drawable marks the control bar dirty (client)
}

// RW 0x776F03: the drawable replacement (the disguise template's, RW 0x6CFE8C, in the disguise player's colour; the object's own back) goes to the client as a
// REPLACED event; the EVA events DisguiseStarted / DisguiseRevealedSuccess / DisguiseRevealedFailure, the radar refresh and the transition opacity are not ported
void StealthUpdate::swapDisguise()
{
	Object *obj = getObject();
	ObjectClientHooks *client = obj->clientHooks();
	if (!m_disguiseTemplate)
	{
		if (m_disguisePlayer != -1)
		{
			m_disguisePlayer = -1;
			if (client) // RW 0x776F5C .. 0x776FA6: the object's own drawable back in its own colour (RW 0x68B71F / 0x68B6F5)
			{
				const Player *own = obj->getControllingPlayer();
				client->replaceDrawable(*obj, obj->getTemplate(), own && own->hasTeamColor(), own ? own->getPlayerColor() : 0u);
			}
			playFX(m_data->m_disguiseRevealFX); // RW 0x776FFA: DisguiseRevealFX (data + 0x3C) at the object's position (RW 0x494615)
			m_disguised = false;
		}
	}
	else
	{
		if (client) // RW 0x777073 .. 0x7770C0: the disguise template's drawable in the disguise player's colour (Player + 0x2A0 / + 0x2A4)
		{
			const Player *as = obj->logic().players().getNthPlayer(m_disguisePlayer);
			client->replaceDrawable(*obj, m_disguiseTemplate, as && as->hasTeamColor(), as ? as->getPlayerColor() : 0u);
		}
		playFX(m_data->m_disguiseFX); // RW 0x777103: DisguiseFX (data + 0x40)
		m_disguised = true;
	}
}

// RW 0x7767A9
void StealthUpdate::markAsDetected(unsigned frames, int mode, Object *detector, bool horde)
{
	(void)mode;     // the client notification's mode
	(void)detector; // the client notification's detector
	Object *obj = getObject();
	const StealthBits &b = stealthBits();
	if (obj->testStatus((unsigned)b.detected))
	{
		extendDetection(frames); // RW 0x776B34
		return;
	}
	if (horde)
	{
		if (Object *h = obj->getHordeObject(false))
		{
			if ((forbiddenConditions() & StealthUpdateModuleData::HORDEBRAIN_NOT_STEALTHED) || h == obj)
			{
				if (!h->getContain())
				{
					return;
				}
				if (StealthUpdate *hs = of(*h))
				{
					hs->markAsDetected(frames, mode, detector, false);
				}
				forEachMember(*h, [&](Object &m) {
					if (StealthUpdate *ms = of(m))
					{
						ms->markAsDetected(frames, mode, detector, false);
					}
				});
				return;
			}
		}
	}
	setStatusAll(b.detected, true); // RW 0x776896
	if (m_disguiseTemplate) // RW 0x7768A2: a disguise ends (lane STEALTH-2)
	{
		disguiseAsObject(nullptr);
	}
	if (m_data->m_orderIdleEnemiesToAttackMeUponReveal)
	{
		obj->logic().invisibility().wakeEnemiesThatSee(obj); // RW 0x7768B4 .. 0x77690B
	}
	extendDetection(frames);
}

// RW 0x7765EC
bool StealthUpdate::allowedToStealth() const
{
	const Object *obj = getObject();
	const Object *h = obj->getHordeObject(false);
	return allowedAt(*obj->getPosition(), h ? *h->getPosition() : *obj->getPosition());
}

// RW 0x776294
bool StealthUpdate::allowedAt(const Coord3D &own, const Coord3D &hordePos) const
{
	const Object *obj = getObject();
	const StealthBits &b = stealthBits();
	const unsigned flags = forbiddenConditions();
	if (!obj->testStatus((unsigned)b.canStealth))
	{
		return false;
	}
	if (!m_data->m_innateStealth && !obj->testStatus((unsigned)b.stealthed))
	{
		return false;
	}
	if ((flags & StealthUpdateModuleData::MOUNTED) && obj->testModelCondition(b.mounted))
	{
		return false;
	}
	if (flags & StealthUpdateModuleData::TAKING_DAMAGE)
	{
		// RW 0x7762FE .. 0x77637F: with a ToggleHiddenSpecialAbilityUpdate (RW 0x68BDA5), a last damage frame (body slot 0x44) before its hide frame (slot 0x64)
		// is not "taking damage"; else the body's last damage info (slot 0x40) with a non-zero amount, not HEALING; INFERENCE (S-1041): the last damaging hit
		if (const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj->getBodyModule()))
		{
			const ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*obj);
			const bool beforeHide = th && body->lastDamageFrame() < th->hideFrame();
			if (!beforeHide && body->lastDamageFrame() != 0xFFFFFFFFu)
			{
				obj->logic().noteStop("[S-1041] StealthUpdate TAKING_DAMAGE: the body's last damaging hit forbids stealth (RW reads the last damage info)");
				return false;
			}
		}
	}
	if ((flags & StealthUpdateModuleData::ATTACKING) && InvisibilityManager::isFiring(*obj))
	{
		return false;
	}
	if ((flags & StealthUpdateModuleData::USING_ABILITY) && obj->testStatus((unsigned)b.usingAbility))
	{
		return false;
	}
	const unsigned firing = flags & StealthUpdateModuleData::FIRING_ANY_SLOT;
	if (firing && InvisibilityManager::isFiring(*obj))
	{
		if (firing == StealthUpdateModuleData::FIRING_ANY_SLOT)
		{
			return false;
		}
		const unsigned now = obj->logic().getFrame();
		const ObjectWeapons *w = obj->getWeapons();
		for (int slot = 0; slot < 5 && w; ++slot)
		{
			if (flags & (StealthUpdateModuleData::FIRING_PRIMARY << slot))
			{
				const Weapon *weapon = w->weaponInSlot(slot);
				if (weapon && weapon->lastFireFrame() >= now - 1u)
				{
					return false;
				}
			}
		}
	}
	if (flags & StealthUpdateModuleData::MOVING) // RW 0x776474: speed (RW 0x68B34C) above MoveThresholdSpeed
	{
		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
		const float speed = loco && loco->speed() > 0.0f ? loco->speed() : 0.0f;
		if (speed > m_data->m_moveThresholdSpeed)
		{
			return false;
		}
	}
	if (flags & StealthUpdateModuleData::PRIMARY_FORMATION)
	{
		obj->logic().noteStop("[S-1041] StealthUpdate PRIMARY_FORMATION: the horde contain's slot 0xEC (RW 0x7764B9) is not read (taken as false)");
	}
	// RW 0x7764C7: the script's stealth switch (Object + 0x457 bit 8) is not ported (S-1040)
	if (flags & StealthUpdateModuleData::AWAY_FROM_TREES)
	{
		if (!obj->logic().invisibility().treesNear(own, obj)) // RW 0x7764DE .. 0x776566
		{
			if (m_data->m_removeTerrainRestrictionOnUpgrade.empty()) // RW 0x776568
			{
				return false;
			}
			for (const std::string &n : m_data->m_removeTerrainRestrictionOnUpgrade)
			{
				if (!obj->hasUpgrade(Player::resolveUpgrade(n, false))) // RW 0x691421 (a NULL template: false)
				{
					return false;
				}
			}
		}
	}
	if (flags & StealthUpdateModuleData::HORDEBRAIN_NOT_STEALTHED) // RW 0x7765A8
	{
		const Object *h = obj->getHordeObject(false);
		const StealthUpdate *hs = h ? of(*h) : nullptr; // RW 0x776081
		if (hs != this)
		{
			if (!hs || !hs->allowedAt(hordePos, hordePos))
			{
				return false;
			}
		}
	}
	return true;
}

// RW 0x777310
UpdateSleepTime StealthUpdate::evaluate()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const unsigned now = logic.getFrame();
	const StealthBits &b = stealthBits();
	auto sleep = [&]() { return m_enabled ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER; }; // RW 0x7779C3
	if (!m_enabled && !m_ring)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	// RW 0x777351: both branches below need the object's drawable (RW 0x70E013); INFERENCE (S-1041): every logic object has one in the retail game (lane STEALTH-2)
	if (m_ring) // RW 0x77735D .. 0x7773D0: the One Ring worn
	{
		if (m_stealthAllowedFrame < now)
		{
			setStatusAll(b.stealthed, true);
		}
		if (m_detectionExpires != 0 && m_detectionExpires < now) // the ring taken off (RW 0x776B50 set the expiry to now + OneRingDelayOff)
		{
			if (obj->testModelCondition(b.oneRing))
			{
				obj->setModelConditionState(b.oneRing, false); // Object + 0x12B bit 5, RW 0x68B53C
			}
			setStatusAll(b.stealthed, false);
			if (ObjectWeapons *w = obj->getWeapons())
			{
				w->setWeaponSetFlag(b.ringWeaponSet, false); // RW 0x691106(0x1C)
			}
			m_ring = false;
			m_ringReadyFrame = m_data->m_ringDelayAfterRemoving + now;
		}
	}
	else if (m_transitionFrames != 0) // RW 0x777587 .. 0x777655: the disguise transition
	{
		--m_transitionFrames;
		// the progress 1 - left / total (RW 0x7775E2, x87; total = DisguiseTransitionTime while disguising, else DisguiseRevealTransitionTime) reaches 0.5
		// (RW 0xBD869C) exactly when 2 * left <= total for these integer frame counts; the progress itself is the drawable's opacity |1 - 2p| (client)
		const unsigned total = m_disguising ? m_data->m_disguiseTransitionTime : m_data->m_disguiseRevealTransitionTime;
		if (2u * m_transitionFrames <= total && !m_halfway)
		{
			swapDisguise(); // RW 0x777606
			m_halfway = true;
		}
		if (m_transitionFrames == 0 && !m_disguising) // RW 0x777639: the reveal is over
		{
			m_enabled = false;
			setStatusAll(b.stealthed, false);
			setStatusAll(b.detected, false);
			return sleep();
		}
	}
	if (m_data->m_revealDistanceFromTarget > 0.0f && !m_ring) // RW 0x7773D3
	{
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			if (const Object *goal = ai->currentVictim()) // RW 0x668303 (INFERENCE S-1041: the current victim)
			{
				const Coord3D &a = *obj->getPosition(), &g = *goal->getPosition();
				const float d2 = SimMath::sumSquares3(SimMath::subf32(g.x, a.x), SimMath::subf32(g.y, a.y), SimMath::subf32(g.z, a.z));
				if (SimMath::mulf32(m_data->m_revealDistanceFromTarget, m_data->m_revealDistanceFromTarget) >= d2)
				{
					markAsDetected(0, 1, nullptr, true); // RW 0x7776A8
					return sleep();
				}
			}
		}
	}
	bool anyReveal = false;
	for (std::uint32_t w : m_data->m_revealWeaponSets)
	{
		anyReveal = anyReveal || w != 0;
	}
	if (anyReveal && !m_ring) // RW 0x777672
	{
		if (const ObjectWeapons *w = obj->getWeapons())
		{
			const WeaponSetFlags &f = w->weaponSetFlags();
			for (size_t i = 0; i < f.size(); ++i)
			{
				if (f[i] & m_data->m_revealWeaponSets[i])
				{
					markAsDetected(0, 1, nullptr, true);
					return sleep();
				}
			}
		}
	}
	if (m_data->m_detectedByAnyoneRange > 0.0f && !m_ring) // RW 0x777443
	{
		// RW 0x777477 .. 0x77753F: ThePartitionManager within DetectedByAnyoneRange, distance type 2 (FROM_BOUNDINGSPHERE_2D), near to far, with the filter
		// chain RW 0xC1D660 (not the object), RW 0xC10E20 (alive), RW 0xC2E65C (an AI), RW 0xC11DC0 (flag 1: the candidate's relationship toward the object),
		// RW 0xC0F374 (the same Object + 0x458 bit 3: never set) and the KindOf filter RW 0x445139 (none of PROJECTILE, IGNORE_FOR_EVA_SPEECH_POSITION); the
		// nearest hit detects (lane MODULES-2)
		const unsigned relMask = m_data->m_detectedByFriendliesOnly ? (1u << ALLIES) : (1u << ENEMIES);
		PartitionFilterFn detectors([&](Object &c) {
			if (&c == obj || c.isEffectivelyDead() || !c.getAIUpdateInterface())
			{
				return false;
			}
			if (!(relMask & (1u << c.getRelationship(*obj))))
			{
				return false;
			}
			return !c.isKindOf((unsigned)b.projectile) && !c.isKindOf((unsigned)b.ignoreForEva);
		});
		const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_detectedByAnyoneRange, FROM_BOUNDINGSPHERE_2D, { &detectors },
		                                                                   ITER_SORTED_NEAR_TO_FAR);
		if (!hits.empty())
		{
			markAsDetected(0, 2, hits[0].object, true); // RW 0x7776BC
			return sleep();
		}
	}
	if (allowedToStealth()) // RW 0x7776F5
	{
		if (now < m_stealthAllowedFrame)
		{
			return sleep();
		}
		if (!obj->testStatus((unsigned)b.stealthed))
		{
			setStatusAll(b.stealthed, true); // RW 0x777799
		}
	}
	else
	{
		if (!m_ring)
		{
			m_stealthAllowedFrame = now + m_data->m_stealthDelay; // RW 0x7777A8
		}
		if (obj->testStatus((unsigned)b.stealthed))
		{
			setStatusAll(b.stealthed, false); // RW 0x777842
			if (ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*obj)) // RW 0x777849 -> 0x7760B7 (lane STEALTH-2)
			{
				th->unhide(); // slot 0x5C
			}
			// RW 0x77784E: the client's hint look
			markAsDetected(0, 1, nullptr, true); // RW 0x77785D
		}
	}
	// RW 0x777862: the detection
	if (now > m_detectionExpires && obj->testStatus((unsigned)b.detected))
	{
		setStatusAll(b.detected, false); // RW 0x777993; the garrison notice (contain slot 0x50) is not ported
	}
	return sleep();
}

// RW 0x7779E3
UpdateSleepTime StealthUpdate::update()
{
	Object *obj = getObject();
	if (of(*obj) != this) // RW 0x777A30 .. 0x777A90: only the object's StealthUpdate runs
	{
		return m_enabled ? UPDATE_SLEEP_NONE : UPDATE_SLEEP_FOREVER;
	}
	m_active = true;
	const UpdateSleepTime sleep = evaluate();
	// RW 0x777AA9 .. 0x777AF7: the client's opacity pulse (FriendlyOpacityMin / Max, PulseFrequency) is the client's
	const bool stealthed = obj->testStatus((unsigned)stealthBits().stealthed);
	if (stealthed != m_wasStealthed) // RW 0x777B09
	{
		if (!m_first)
		{
			// RW 0x777B13: leaving: ExitStealthOneRingFX when the ring was worn at the last FX (+ 0x33), else ExitStealthFX; entering: BecomeStealthedOneRingFX
			// when the ring is worn (+ 0x31), else BecomeStealthedFX
			if (m_wasStealthed)
			{
				playFX(m_ringAtLastFX ? m_data->m_exitStealthOneRingFX : m_data->m_exitStealthFX);
			}
			else
			{
				playFX(m_ring ? m_data->m_becomeStealthedOneRingFX : m_data->m_becomeStealthedFX);
			}
		}
		if (m_wasStealthed)
		{
			if (ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*obj)) // RW 0x777B4E -> 0x7760B7 (lane STEALTH-2)
			{
				th->unhide();
			}
		}
		m_wasStealthed = stealthed;
	}
	else if (m_ringAtLastFX != m_ring && stealthed) // RW 0x777B62 .. 0x777BAC: the ring changed while stealthed: the exit FX of the old mode, the enter FX of the new
	{
		if (m_ringAtLastFX)
		{
			playFX(m_data->m_exitStealthOneRingFX);
			playFX(m_data->m_becomeStealthedFX);
		}
		else
		{
			playFX(m_data->m_exitStealthFX);
			playFX(m_data->m_becomeStealthedOneRingFX);
		}
	}
	m_ringAtLastFX = m_ring; // RW 0x777BB7
	m_first = false;
	return sleep;
}

void StealthUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_stealthAllowedFrame);
	h.addU32(m_detectionExpires);
	h.addI32(m_forbiddenOverride);
	h.addBool(m_enabled);
	h.addU32(m_ringReadyFrame);
	h.addBool(m_ring);
	h.addBool(m_wasStealthed);
	h.addBool(m_ringAtLastFX);
	h.addBool(m_first);
	h.addI32(m_disguisePlayer);
	h.addString(m_disguiseTemplate ? m_disguiseTemplate->getName() : std::string());
	h.addU32(m_transitionFrames);
	h.addBool(m_halfway);
	h.addBool(m_disguising);
	h.addBool(m_disguised);
	for (std::uint32_t w : m_requiredUpgrades.words)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : m_forbiddenUpgrades.words)
	{
		h.addU32(w);
	}
	h.addBool(m_active);
}

void InvisibilityModules::registerAll(ModuleFactory &modules)
{
	bind<InvisibilityUpdate, InvisibilityUpdateModuleData>(modules, "InvisibilityUpdate");
	bind<StealthDetectorUpdate, StealthDetectorUpdateModuleData>(modules, "StealthDetectorUpdate");
	bind<StealthUpdate, StealthUpdateModuleData>(modules, "StealthUpdate");
	StealthAbilityModules::registerAll(modules); // lane STEALTH-2: ToggleHiddenSpecialAbilityUpdate, InvisibilitySpecialPower
}

// lane STEALTH-2: MSG_ONE_RING (1108): the player's group (RW 0x76ED16, none: nothing) -> RW 0x7729A6: each member's AI idles (RW 0x5E821A(2)) and its
// StealthUpdate (RW 0x68FBD3) toggles the One Ring (RW 0x776B50). No retail command button issues the message.
void InvisibilityModules::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_ONE_RING, "STEALTH-2", [](GameLogic &logic, const GameMessage &m) {
		if (!logic.players().getNthPlayer(m.getPlayerIndex()))
		{
			return false;
		}
		AIGroup group(logic, AICommands::selection(logic, m.getPlayerIndex()));
		for (Object *o : group.members())
		{
			StealthUpdate *st = StealthUpdate::of(*o);
			if (!st)
			{
				continue;
			}
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiIdle(CMD_FROM_AI);
			}
			st->toggleRing();
		}
		return true;
	});
}

std::vector<std::string> InvisibilityModules::stopLines()
{
	std::vector<std::string> out = { "[S-1041] invisibility modules (lane STEALTH-1): InvisibilityUpdate (RW 0x8A7099) and StealthDetectorUpdate (RW 0x8A641A) run; the detector's (RW "
		"0x8A659B), the broadcast (RW 0x8A711F) and DetectedByAnyoneRange (RW 0x77753F) queries run on ThePartitionManager (lanes MODULES-2 / STEALTH-2); not ported: "
		"the detector's vision filter (RW 0xC1D66C -> 0x68FA3D) is the query's 2D centre distance, the detector's SpecialDisguiseUpdate reveal and client "
		"pings; StealthUpdate (RW 0x7779E3 / 0x777310) runs with its One Ring mode (RW 0x776B50, MSG_ONE_RING RW 0x7729A6, CancelOneRingEffect) and disguise "
		"(RW 0x776117 / 0x776F03, the transition RW 0x777587; lane STEALTH-2), the drawable taken as present for both, the disguise's drawable swap a client event; not "
		"ported: the disguise's transition opacity and EVA events, the client opacity / notifications and garrison notice, a second StealthUpdate's reveal (RW 0x777A44); "
		"TAKING_DAMAGE reads the last damaging hit, RevealDistanceFromTarget uses the AI's current victim; particle / audio / Eva names of these tables are kept "
		"unresolved" };
	for (const std::string &l : StealthAbilityModules::stopLines())
	{
		out.push_back(l);
	}
	return out;
}
