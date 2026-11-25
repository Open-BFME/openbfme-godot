// OpenBFME. GPL-3.0.
//
// HordeContainBehaviorData: the typed ModuleData the ModuleFactory makes for a `Behavior = HordeContain` / `HorseHordeContain`
// declaration (lane MAPOBJ-1 binds it; HORDE-1's HordeContainModuleData is a plain struct, "a derived module data embeds this struct
// ... the INI parse procs use `store`", GameLogic/Module/OpenContain.h). The struct sits after the ModuleData base, so the field table
// is added with the struct's offset as the extra offset (MultiIniFieldParse::add(table, extra), ZH style).
//
// This header pulls in GameLogic/BitFlags.h (its raw array alias is ModelConditionMask, distinct from Common/ModelState.h's ModelConditionFlags, so both may be included).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/HordeContain.h"

#include <cstddef>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

class HordeContainBehaviorData : public ModuleData
{
public:
	HordeContainModuleData horde; ///< RW 0x878EE5 constructor, table RW 0x878B63

	static void buildFieldParse(MultiIniFieldParse &p)
	{
		HordeContainModuleData::buildFieldParse(p, (unsigned)offsetof(HordeContainBehaviorData, horde));
	}
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// AODHordeContain's data: HordeContain's chain, then the table RW 0xC5CE50 (20 rows, FrequencyRandomness and FrequencyRandomnessZ twice as in the binary; offsets RW
// + 0x284 .. + 0x2C8). Defaults verified against both RotWK 2.01 (createData RW 0x64B845 -> 0x87D55F, buildFieldParse RW 0x87D62E) and BFME2 1.06 (0x0047A2E1).
// Consumed by AODHordeContain (AODHordeContain.cpp: member sway, rank path history, large-unit height lift, and oath-fulfilled scatter).
class AODHordeContainBehaviorData : public HordeContainBehaviorData
{
public:
	float frequencyScale = 0.0f;
	float frequencyRandomness = 0.0f;
	float amplitudeScale = 0.0f;
	float amplitudeRandomness = 0.0f;
	float stillAmplitude = 0.0f;
	float frequencyScaleZ = 0.0f;
	float frequencyRandomnessZ = 0.0f;
	float amplitudeScaleZ = 0.0f;
	float amplitudeRandomnessZ = 0.0f;
	float stillAmplitudeZ = 0.0f;
	float oathFulfilledZFactor = 1.0f;
	float largeUnitHeightFactor = 0.45f;
	float largeUnitMinHeight = 0.0f;
	float largeUnitMaxHeight = 999999.0f;
	unsigned largeUnitTimeout = 2;
	float largeUnitTailOff = 1.0f;
	float scatterSpeedFactor = 0.3f;
	float scatterRandomness = 0.4f;

	static void buildFieldParse(MultiIniFieldParse &p)
	{
		HordeContainBehaviorData::buildFieldParse(p);
		static const FieldParse table[] = {
			{ "FrequencyScale", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyScale) },
			{ "FrequencyRandomness", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyRandomness) },
			{ "AmplitudeScale", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, amplitudeScale) },
			{ "FrequencyRandomness", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyRandomness) },
			{ "AmplitudeRandomness", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, amplitudeRandomness) },
			{ "StillAmplitude", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, stillAmplitude) },
			{ "FrequencyScaleZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyScaleZ) },
			{ "FrequencyRandomnessZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyRandomnessZ) },
			{ "AmplitudeScaleZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, amplitudeScaleZ) },
			{ "FrequencyRandomnessZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, frequencyRandomnessZ) },
			{ "AmplitudeRandomnessZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, amplitudeRandomnessZ) },
			{ "StillAmplitudeZ", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, stillAmplitudeZ) },
			{ "OathFulfilledZFactor", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, oathFulfilledZFactor) },
			{ "LargeUnitHeightFactor", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, largeUnitHeightFactor) },
			{ "LargeUnitMinHeight", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, largeUnitMinHeight) },
			{ "LargeUnitMaxHeight", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, largeUnitMaxHeight) },
			{ "LargeUnitTimeout", INI::parseDurationUnsignedInt, nullptr, offsetof(AODHordeContainBehaviorData, largeUnitTimeout) },
			{ "LargeUnitTailOff", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, largeUnitTailOff) },
			{ "ScatterSpeedFactor", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, scatterSpeedFactor) },
			{ "ScatterRandomness", INI::parseReal, nullptr, offsetof(AODHordeContainBehaviorData, scatterRandomness) },
			{ nullptr, nullptr, nullptr, 0 }
		};
		p.add(table);
	}
};
