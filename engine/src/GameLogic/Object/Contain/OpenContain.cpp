// OpenBFME. GPL-3.0.
//
// OpenContainModuleData: constructor, field table and the custom parse procs. See
// GameLogic/Module/OpenContain.h for the target facts. Lane HORDE-1.

// The FieldParse offsets use offsetof on structs that hold standard library containers; those are
// "conditionally supported" and well defined on GCC, Clang and MSVC (ZH does the same).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/OpenContain.h"

#include "Common/AsciiString.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/ObjectFilter.h"

#include <cstddef>
#include <cstring>

// RW 0x867E1B (the field defaults are the stores in that function; frame defaults are literal).
OpenContainModuleData::OpenContainModuleData()
	: m_passengerFilter(ObjectFilter::parserDefault())
	, m_manualPickUpFilter(ObjectFilter::parserDefault())
	, m_damagePercentToUnits(0.0f)                // RW 0x867EB5 stores 0 at +0x6C
	, m_passengersTestCollisionHeight(-1000.0f)   // RW 0x867EA1 stores -1000 at +0x68
	, m_containMax(-1)
	, m_numberOfExitPaths(1)
	, m_doorOpenTime(1)
	, m_allowOwnPlayerInsideOverride(false)
	, m_allowAlliesInside(true)
	, m_allowEnemiesInside(true)
	, m_allowNeutralInside(true)
	, m_showPips(true)
	, m_collidePickup(true)
	, m_passengersInTurret(false)
	, m_ejectPassengersOnDeath(true)
	, m_killPassengersOnDeath(false)
	, m_enabled(true)
	, m_modifierRequiredTime(100)
{
}

// RW 0x866FF0: the mask is parsed into a zeroed temporary and copied; the "set" flag is raised even for
// an empty line.
void OpenContainModuleData::parseObjectStatusOfContained(INI *ini, void *, void *store, const void *)
{
	ObjectStatusOfContainedField *field = static_cast<ObjectStatusOfContainedField *>(store);
	ObjectStatusMaskType tmp{};
	ParseBitFlags(ini, tmp.data(), tmp.size(), TheObjectStatusNames);
	field->mask = tmp;
	field->set = true;
}

// RW 0x8677D2: PassengerBone:<prefix> KindOf:<mask...>
void OpenContainModuleData::parsePassengerBonePrefix(INI *ini, void *, void *store, const void *)
{
	std::vector<PassengerBonePrefixEntry> *list = static_cast<std::vector<PassengerBonePrefixEntry> *>(store);
	const char *tok = ini->getNextToken(ini->getSepsColon());
	if (std::strcmp(tok, "PassengerBone") != 0)
	{
		throw INIException(3, "PassengerBone expected"); // RW 0xC59A74
	}
	std::string name;
	INI::parseAsciiString(ini, nullptr, &name, nullptr);
	tok = ini->getNextToken(ini->getSepsColon());
	if (std::strcmp(tok, "KindOf") != 0)
	{
		throw INIException(3, "KindOf expected"); // RW 0xC59A8C
	}
	PassengerBonePrefixEntry entry;
	ParseBitFlags(ini, entry.kindOf.data(), entry.kindOf.size(), TheKindOfNames);
	entry.bonePrefix = name;
	list->push_back(entry);
}

// RW 0x8678EB: <unsigned> then a model condition flags line; map[n] = flags.
void OpenContainModuleData::parseBoneSpecificConditionState(INI *ini, void *, void *store, const void *)
{
	std::map<unsigned, ModelConditionMask> *map = static_cast<std::map<unsigned, ModelConditionMask> *>(store);
	const unsigned n = ini->scanUnsignedInt(ini->getNextToken());
	ModelConditionMask flags{};
	ParseBitFlags(ini, flags.data(), flags.size(), TheModelConditionNames);
	(*map)[n] = flags;
}

// RW 0x42E59E: every token goes through preprocessMacro; a macro's value is split on " \n\r\t" and each
// piece is appended (RW 0x42D8EE); plain tokens are appended as written.
void OpenContainModuleData::parseModifierToGiveOnExit(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *list = static_cast<std::vector<std::string> *>(store);
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const char *expanded = ini->preprocessMacro(token);
		if (expanded == token)
		{
			list->push_back(token);
			continue;
		}
		const std::string value = expanded;
		static const char *kWs = " \n\r\t";
		size_t pos = 0;
		while (pos < value.size())
		{
			const size_t b = value.find_first_not_of(kWs, pos);
			if (b == std::string::npos)
			{
				break;
			}
			size_t e = value.find_first_of(kWs, b);
			if (e == std::string::npos)
			{
				e = value.size();
			}
			list->push_back(value.substr(b, e - b));
			pos = e;
		}
	}
}

// RW 0x73B217 -> 0x73AA94: "NoSound" clears; otherwise TheAudio must know the event.
void OpenContainModuleData::parseAudioEvent(INI *ini, void *, void *store, const void *)
{
	StoreReference *ref = static_cast<StoreReference *>(store);
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name.c_str(), "NoSound") == 0)
	{
		ref->name.clear();
		ref->resolved = false;
		return;
	}
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.audioEventExists)
	{
		throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed (acceptance stop S-083)", name.c_str());
	}
	if (!hooks.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str()); // RW 0xC2500C
	}
	ref->name = name;
	ref->resolved = true;
}

const FieldParse *OpenContainModuleData::getFieldParse()
{
	static const FieldParse table[] = {
		{ "ContainMax", INI::parseInt, nullptr, offsetof(OpenContainModuleData, m_containMax) },
		{ "EnterSound", OpenContainModuleData::parseAudioEvent, nullptr, offsetof(OpenContainModuleData, m_enterSound) },
		{ "ExitSound", OpenContainModuleData::parseAudioEvent, nullptr, offsetof(OpenContainModuleData, m_exitSound) },
		{ "DamagePercentToUnits", INI::parsePercentToReal, nullptr, offsetof(OpenContainModuleData, m_damagePercentToUnits) },
		{ "PassengerFilter", ParseObjectFilter, nullptr, offsetof(OpenContainModuleData, m_passengerFilter) },
		{ "ManualPickUpFilter", ParseObjectFilter, nullptr, offsetof(OpenContainModuleData, m_manualPickUpFilter) },
		{ "PassengersTestCollisionHeight", INI::parseReal, nullptr, offsetof(OpenContainModuleData, m_passengersTestCollisionHeight) },
		{ "PassengersInTurret", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_passengersInTurret) },
		{ "NumberOfExitPaths", INI::parseInt, nullptr, offsetof(OpenContainModuleData, m_numberOfExitPaths) },
		{ "DoorOpenTime", INI::parseDurationUnsignedInt, nullptr, offsetof(OpenContainModuleData, m_doorOpenTime) },
		{ "AllowOwnPlayerInsideOverride", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_allowOwnPlayerInsideOverride) },
		{ "AllowAlliesInside", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_allowAlliesInside) },
		{ "AllowEnemiesInside", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_allowEnemiesInside) },
		{ "AllowNeutralInside", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_allowNeutralInside) },
		{ "ShowPips", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_showPips) },
		{ "CollidePickup", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_collidePickup) },
		{ "PassengerBonePrefix", OpenContainModuleData::parsePassengerBonePrefix, nullptr, offsetof(OpenContainModuleData, m_passengerBonePrefix) },
		{ "BoneSpecificConditionState", OpenContainModuleData::parseBoneSpecificConditionState, nullptr, offsetof(OpenContainModuleData, m_boneSpecificConditionState) },
		{ "EjectPassengersOnDeath", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_ejectPassengersOnDeath) },
		{ "KillPassengersOnDeath", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_killPassengersOnDeath) },
		{ "Enabled", INI::parseBool, nullptr, offsetof(OpenContainModuleData, m_enabled) },
		{ "ObjectStatusOfContained", OpenContainModuleData::parseObjectStatusOfContained, nullptr, offsetof(OpenContainModuleData, m_objectStatusOfContained) },
		{ "ModifierToGiveOnExit", OpenContainModuleData::parseModifierToGiveOnExit, nullptr, offsetof(OpenContainModuleData, m_modifierToGiveOnExit) },
		{ "ModifierRequiredTime", INI::parseDurationUnsignedInt, nullptr, offsetof(OpenContainModuleData, m_modifierRequiredTime) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

void OpenContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset)
{
	p.add(getFieldParse(), extraOffset);
}
