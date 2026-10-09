// OpenBFME. GPL-3.0.
//
// AudioSettings: the AudioSettings block of AudioSettings.ini.
//
// Port of ZH GameEngine/Include/Common/AudioSettings.h, changed to RotWK (RW 0x44107A, caveat S-001):
//   * ONE block with TWO field tables applied to the same object (RW 0xBD7698, 74 rows, and 0xC04440, 15 rows; the second is
//     added by 0x645132) - the object is TheAudio+0x10 (RW 0x4410B5).
//   * a map.ini may not define it (load type 2: INIException(3, "You cannot define or override the AudioSettings in map.ini"), 0xBD83DC).
//   * post-processing after the fields (RW 0x4410F1-0x441194): the microphone / zoom fields of the Living World group (and a third,
//     INI-less group 0x90 further on) that were not given (NaN) copy the tactical value; the distance-like fields get their squares
//     stored beside them; the four folders are joined to AudioRoot (RW 0x6453B4 / 0x64533E: root + "\" + folder + "\");
//     the defaults of the volume sliders are scaled by 0.01 (RW 0x6450DD; the INI percents are read as percents already).
//   * the field names / values of the retail AudioSettings.ini are the golden (tests/test_audio_ini.cpp).
// DONOR / INFERENCE: the constructor defaults of fields an INI omits are not read from the binary (stop S-241); the values below
// are zeros except where the INI of retail names the intent.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"

#include <cstdint>
#include <string>
#include <vector>

struct MicrophoneSettings
{
	// RW AudioSettings +0x12C.. (tactical), +0x174.. (Living World), +0x1BC.. (third group, no INI names).
	float preferredFractionCameraToGround;
	float preferredFractionCameraToGroundSq; ///< derived
	float minDistanceToCamera;
	float minDistanceToCameraSq;
	float maxDistanceToCamera;
	float maxDistanceToCameraSq;
	float pullTowardsTerrainLookAtPointPercent;
	float zoomMinDistance;
	float zoomMinDistanceSq;
	float zoomMaxDistance;
	float zoomMaxDistanceSq;
	float zoomSoundVolumePercentageAmount;
	float zoomFadeDistanceForMaxEffect;
	float zoomFadeDistanceForMaxEffectSq;
	float zoomFadeZeroEffectEdgeLength;
	float zoomFadeZeroEffectEdgeLengthSq;
	float zoomFadeFullEffectEdgeLength;
	float zoomFadeFullEffectEdgeLengthSq;
};

struct AudioSettings
{
	AudioSettings();

	// table 0xC04440
	std::string audioRoot;
	std::string soundsFolder;
	std::string musicFolder;
	std::string streamingFolder;
	std::string ambientStreamFolder;
	std::string soundsExtension;
	std::string musicScriptLibraryName;
	float defaultSoundVolume;
	float defaultVoiceVolume;
	float defaultMusicVolume;
	float defaultAmbientVolume;
	float defaultMovieVolume;
	unsigned voiceMoveToCampMaxCampnessAtStartPoint;
	unsigned voiceMoveToCampMinCampnessAtEndPoint;
	unsigned minDelayBetweenEnterStateVoiceFrames; ///< parseDurationUnsignedInt: logic frames

	// table 0xBD7698
	bool useDigital;
	bool useMidi;
	int outputRate;
	int outputBits;
	int outputChannels;
	int sampleCount2D;
	int sampleCount3D;
	int streamCount;
	int globalMinRange;
	int globalMaxRange;
	int timeToFadeAudio;
	int ambientStreamHysteresisVolume;
	unsigned audioFootprintInBytes;
	unsigned mixaheadLatency;
	unsigned mixaheadLatencyDuringMovies;
	unsigned loopBufferLengthMS;
	unsigned loopBufferCallbackCallsPerBufferLength;
	int automaticSubtitleDurationMS;
	int automaticSubtitleWindowWidth;
	int automaticSubtitleLines;
	std::uint32_t automaticSubtitleWindowColor; ///< packed ARGB (INI::parseColorInt, RW 0x42F13E)
	std::uint32_t automaticSubtitleTextColor;
	int forceResetTimeSeconds;
	int emergencyResetTimeSeconds;
	bool suppressOcclusion;
	float minOcclusion;
	int millisecondsPriorToPlayingToReadSoundFile;
	float minSampleVolume;
	float positionDeltaForReverbRecheck;
	float reverbMultiplier[25]; ///< Global<Room>ReverbMultiplier in the order of the INI (PaddedCell .. Psychotic)
	MicrophoneSettings microphone[3]; ///< tactical, Living World, third

	// Parsed by INI::initFromINIMulti over both tables, then post-processed.
	static void parse(INI *ini, AudioSettings &settings);
	// RW 0x4410F1-0x441194 (the post-processing alone, for tests).
	void postProcess();
	// the INI field names of both tables (RW 0xBD7698 and 0xC04440)
	static std::vector<std::string> fieldNames();
	// the path of a folder joined to the root as RW 0x64533E does ("<root>\<folder>\"; the trailing separator is what lets AudioEventRTS append a file name)
	static std::string joinFolder(const std::string &root, const std::string &folder);
};

// The names of the reverb room multipliers, in field order (RW 0xBD7698 rows GlobalPaddedCellReverbMultiplier ..).
extern const char *const kReverbRoomNames[25];
