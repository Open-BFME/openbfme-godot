// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The file-reading half of ZH Libraries/Source/WWVegas/WW3D2/part_ldr.h / part_ldr.cpp
// (ParticleEmitterDefClass::Load_W3D and its Read_* helpers): W3D_CHUNK_EMITTER into raw structs and
// keyframe arrays. The run-time conversion (Vector3 colours, key times) is left to the particle system
// port (spec step 19).
//
// ZH reads ZH-style version 1 emitters through Convert_To_Ver2; all four retail emitters are version
// 0x20000, so anything older is refused here instead of being converted untested.
//
// Differences from ZH, all failures made explicit: every chunk must be consumed exactly, an unknown
// emitter sub-chunk is an error (ZH logs "Unhandled Chunk!" and goes on).

#pragma once

#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <string>
#include <vector>

class ChunkLoadClass;

class ParticleEmitterDefClass
{
public:
	// Called with the W3D_CHUNK_EMITTER chunk open.
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	std::string Name;
	std::uint32_t Version = 0;

	std::uint32_t UserType = 0;
	std::string UserString;

	W3dEmitterInfoStruct Info = {};
	W3dEmitterInfoStructV2 InfoV2 = {};

	// W3D_CHUNK_EMITTER_PROPS: the struct, then ColorKeyframes colour keys, OpacityKeyframes opacity keys and
	// SizeKeyframes size keys (the first of each is the start value; ZH Read_Props).
	W3dEmitterPropertyStruct Props = {};
	std::vector<W3dEmitterColorKeyframeStruct> ColorKeyframes;
	std::vector<W3dEmitterOpacityKeyframeStruct> OpacityKeyframes;
	std::vector<W3dEmitterSizeKeyframeStruct> SizeKeyframes;

	// Optional sections.
	bool HasLineProperties = false;
	W3dEmitterLinePropertiesStruct LineProperties = {};
	bool HasRotation = false;
	W3dEmitterRotationHeaderStruct RotationHeader = {};
	std::vector<W3dEmitterRotationKeyframeStruct> RotationKeyframes; // KeyframeCount + 1: the start key first
	bool HasFrames = false;
	W3dEmitterFrameHeaderStruct FrameHeader = {};
	std::vector<W3dEmitterFrameKeyframeStruct> FrameKeyframes;
	bool HasBlurTime = false;
	W3dEmitterBlurTimeHeaderStruct BlurTimeHeader = {};
	std::vector<W3dEmitterBlurTimeKeyframeStruct> BlurTimeKeyframes;
	bool HasExtraInfo = false;
	W3dEmitterExtraInfoStruct ExtraInfo = {};
};
