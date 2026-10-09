// OpenBFME. GPL-3.0.
//
// TransportContainModuleData: constructor, field table, custom parse procs. See
// GameLogic/Module/TransportContain.h for the target facts. Lane HORDE-1.

// The FieldParse offsets use offsetof on structs that hold standard library containers; those are
// "conditionally supported" and well defined on GCC, Clang and MSVC (ZH does the same).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/TransportContain.h"

#include "Common/AsciiString.h"
#include "GameLogic/ContainParseHooks.h"

#include <cstddef>
#include <cstring>

// RW 0x86B425, after the OpenContain constructor.
TransportContainModuleData::TransportContainModuleData()
	: m_slotCapacity(0)
	, m_exitPitchRate(0.0f)
	, m_healthRegenPercentPerSec(0.0f)
	, m_exitDelay(0)
	, m_typeOneForWeaponSet{}
	, m_typeTwoForWeaponSet{}
	, m_typeOneForWeaponState{}
	, m_typeTwoForWeaponState{}
	, m_typeThreeForWeaponState{}
	, m_forceOrientationContainer(true)
	, m_canGrabStructure(false)
	, m_scatterNearbyOnExit(true)
	, m_orientLikeContainerOnExit(false)
	, m_goAggressiveOnExit(false)
	, m_resetMoodCheckTimeOnExit(true)
	, m_destroyRidersWhoAreNotFreeToExit(false)
	, m_fireGrabWeaponOnVictim(true)
	, m_conditionForEntry(-1)
	, m_shouldThrowOutPassengers(false)
	, m_throwOutPassengersDelay(0)
	, m_throwOutPassengersVelocity{ 0.0f, 0.0f, 0.0f }
	, m_fadeFilter(ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{}))
	, m_fadePassengerOnEnter(false)
	, m_fadePassengerOnExit(false)
	, m_enterFadeTime(0.0f)
	, m_exitFadeTime(0.0f)
	, m_fadeReverse(false)
	, m_releaseSnappyness(0.7f) // RW 0xBDE0A8
{
	// RW 0x86B5A3 / 0x86B5B9: PassengerFilter = NONE (empty masks), ManualPickUpFilter = ALL
	m_open.m_passengerFilter = ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{});
	m_open.m_manualPickUpFilter = ObjectFilter::all(KindOfMaskType{});
}

// RW 0x86AF0A: `InitialPayload = <name> [<count>]`; the count is 1 when absent.
void TransportContainModuleData::parseInitialPayload(INI *ini, void *, void *store, const void *)
{
	std::vector<InitialPayloadEntry> *list = static_cast<std::vector<InitialPayloadEntry> *>(store);
	InitialPayloadEntry entry;
	entry.name = ini->getNextToken();
	const char *countToken = ini->getNextTokenOrNull();
	entry.count = countToken ? ini->scanInt(countToken) : 1;
	list->push_back(entry);
}

// RW 0x869F22: `ConditionForEntry = ModelConditionState:<name>`; the name is looked up with stricmp over the
// model condition names and an unknown name is -1 (no error).
void TransportContainModuleData::parseConditionForEntry(INI *ini, void *, void *store, const void *)
{
	const char *tag = ini->getNextToken(ini->getSepsColon());
	if (std::strcmp(tag, "ModelConditionState") != 0)
	{
		throw INIException(3, "AnimState expected for TransportContain::iniParseAnim"); // RW 0xC5A644
	}
	const char *name = ini->getNextToken();
	int index = -1;
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
		{
			index = i;
			break;
		}
	}
	*static_cast<int *>(store) = index;
}

// RW 0x73AE79: the weapon template named by the next token (null when unknown, no error).
void TransportContainModuleData::parseWeaponTemplate(INI *ini, void *, void *store, const void *)
{
	StoreReference *ref = static_cast<StoreReference *>(store);
	const std::string name = ini->getNextToken();
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.weaponTemplateExists)
	{
		throw INIException(8, "weapon template '%s' cannot be looked up: no TheWeaponStore lookup is installed (acceptance stop S-083)", name.c_str());
	}
	ref->name = name;
	ref->resolved = hooks.weaponTemplateExists(name);
}

// RW 0x86BA2C: at most four entries (code 1), each `<name1> <name2> <count>`; an entry with a missing or
// empty first two tokens is skipped silently, a missing count is the ordinary "Expected additional data".
void TransportContainModuleData::parseUpgradeCreationTrigger(INI *ini, void *instance, void *store, const void *userData)
{
	std::vector<UpgradeCreationTriggerEntry> *list = static_cast<std::vector<UpgradeCreationTriggerEntry> *>(store);
	if (list->size() >= 4)
	{
		throw INIException(1, "iniParseQuery: Too many triggers, can only have %d.", 4); // RW 0xC5A910
	}
	const char *first = ini->getNextTokenOrNull();
	const char *second = ini->getNextTokenOrNull();
	if (first && *first && second && *second)
	{
		UpgradeCreationTriggerEntry entry;
		entry.first = first;
		entry.second = second;
		INI::parseUnsignedInt(ini, instance, &entry.count, userData);
		list->push_back(entry);
	}
}

const FieldParse *TransportContainModuleData::getFieldParse()
{
	static const FieldParse table[] = {
		{ "Slots", INI::parseInt, nullptr, offsetof(TransportContainModuleData, m_slotCapacity) },
		{ "ScatterNearbyOnExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_scatterNearbyOnExit) },
		{ "OrientLikeContainerOnExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_orientLikeContainerOnExit) },
		{ "GoAggressiveOnExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_goAggressiveOnExit) },
		{ "ResetMoodCheckTimeOnExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_resetMoodCheckTimeOnExit) },
		{ "DestroyRidersWhoAreNotFreeToExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_destroyRidersWhoAreNotFreeToExit) },
		{ "ExitBone", INI::parseAsciiString, nullptr, offsetof(TransportContainModuleData, m_exitBone) },
		{ "ExitPitchRate", INI::parseAngularVelocityReal, nullptr, offsetof(TransportContainModuleData, m_exitPitchRate) },
		{ "InitialPayload", TransportContainModuleData::parseInitialPayload, nullptr, offsetof(TransportContainModuleData, m_initialPayload) },
		{ "HealthRegen%PerSec", INI::parseReal, nullptr, offsetof(TransportContainModuleData, m_healthRegenPercentPerSec) },
		{ "ExitDelay", INI::parseDurationUnsignedInt, nullptr, offsetof(TransportContainModuleData, m_exitDelay) },
		{ "TypeOneForWeaponSet", ParseKindOfMask, nullptr, offsetof(TransportContainModuleData, m_typeOneForWeaponSet) },
		{ "TypeTwoForWeaponSet", ParseKindOfMask, nullptr, offsetof(TransportContainModuleData, m_typeTwoForWeaponSet) },
		{ "TypeOneForWeaponState", ParseKindOfMask, nullptr, offsetof(TransportContainModuleData, m_typeOneForWeaponState) },
		{ "TypeTwoForWeaponState", ParseKindOfMask, nullptr, offsetof(TransportContainModuleData, m_typeTwoForWeaponState) },
		{ "TypeThreeForWeaponState", ParseKindOfMask, nullptr, offsetof(TransportContainModuleData, m_typeThreeForWeaponState) },
		{ "ForceOrientationContainer", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_forceOrientationContainer) },
		{ "CanGrabStructure", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_canGrabStructure) },
		{ "GrabWeapon", TransportContainModuleData::parseWeaponTemplate, nullptr, offsetof(TransportContainModuleData, m_grabWeapon) },
		{ "FireGrabWeaponOnVictim", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_fireGrabWeaponOnVictim) },
		{ "ConditionForEntry", TransportContainModuleData::parseConditionForEntry, nullptr, offsetof(TransportContainModuleData, m_conditionForEntry) },
		{ "ShouldThrowOutPassengers", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_shouldThrowOutPassengers) },
		{ "ThrowOutPassengersDelay", INI::parseDurationUnsignedInt, nullptr, offsetof(TransportContainModuleData, m_throwOutPassengersDelay) },
		{ "ThrowOutPassengersVelocity", INI::parseCoord3D, nullptr, offsetof(TransportContainModuleData, m_throwOutPassengersVelocity) },
		{ "ThrowOutPassengersLandingWarhead", TransportContainModuleData::parseWeaponTemplate, nullptr, offsetof(TransportContainModuleData, m_throwOutPassengersLandingWarhead) },
		{ "FadeFilter", ParseObjectFilter, nullptr, offsetof(TransportContainModuleData, m_fadeFilter) },
		{ "FadePassengerOnEnter", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_fadePassengerOnEnter) },
		{ "FadePassengerOnExit", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_fadePassengerOnExit) },
		{ "EnterFadeTime", INI::parseReal, nullptr, offsetof(TransportContainModuleData, m_enterFadeTime) },
		{ "ExitFadeTime", INI::parseReal, nullptr, offsetof(TransportContainModuleData, m_exitFadeTime) },
		{ "FadeReverse", INI::parseBool, nullptr, offsetof(TransportContainModuleData, m_fadeReverse) },
		{ "ReleaseSnappyness", INI::parseReal, nullptr, offsetof(TransportContainModuleData, m_releaseSnappyness) },
		{ "UpgradeCreationTrigger", TransportContainModuleData::parseUpgradeCreationTrigger, nullptr, offsetof(TransportContainModuleData, m_upgradeCreationTrigger) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// ZH style: the base table first (RW 0x86BB03 calls OpenContain's builder RW 0x867F34, which adds the
// OpenContain table 0xC59F30 and the ModuleData base table (OBJ-1's), then adds the TransportContain
// table 0xC5ABD8). The spec text lists the order the other way round; no field name is in two tables, so
// the lookup result does not depend on it.
void TransportContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset)
{
	OpenContainModuleData::buildFieldParse(p, extraOffset + (unsigned)offsetof(TransportContainModuleData, m_open));
	p.add(getFieldParse(), extraOffset);
}
