// OpenBFME. GPL-3.0.
//
// AudioIniState: everything the audio INI blocks of RotWK define, parsed through the shared INI pipeline, and the load sequence of
// TheAudio's init (RW 0x4538DA-0x453A34, spec ini-and-object-model.md 9.5).
//
// Blocks owned here (the keyword is registered with INIBlockRegistry; pass AudioIniState::blockKeywords() as the `skip` list of
// RegisterRecordingBlockStubs so the recording stub of the same name is not registered twice):
//   AudioEvent MusicTrack DialogEvent AmbientStream StreamedSound Multisound AudioSettings MiscAudio AudioLOD AudioLowMHz
//   AnimationSoundClientBehaviorGlobalSetting, PredefinedEvaEvent NewEvaEvent EvaEventForwardReference MiscEvaData ScoredKillEvaAnnouncer,
//   LargeGroupAudioMap LargeGroupAudioUnusedKnownKeys.
// NOT owned: LivingWorldSound (read by the Living World subsystem with its flags) and CrowdResponse (the crowd system) keep their recording stubs.

#pragma once

#include "Common/Audio/AudioEventInfo.h"
#include "Common/Audio/AudioSettings.h"
#include "Common/Audio/EvaEvents.h"
#include "Common/Audio/LargeGroupAudio.h"
#include "Common/Audio/MiscAudio.h"
#include "Common/INI.h"

#include <string>
#include <vector>

// RW 0x602905: AudioLOD <Low|High> { MaximumAmbientStreams AllowDolby AllowReverb } (the table lives in TheGameLODManager + 0x218 + 8 * level).
struct AudioLODSettings
{
	int maximumAmbientStreams = 0;
	bool allowDolby = false;
	bool allowReverb = false;
	bool defined = false;
};

class AudioIniState
{
public:
	AudioEventInfoStore infos;
	AudioSettings settings;
	MiscAudio miscAudio;
	AudioLODSettings audioLOD[2]; ///< Low, High (RW 0xD9E760)
	int audioLowMHz = 0;          ///< `AudioLowMHz = n` (RW 0x6017FA stores it at TheGameLODManager + 0x17F0)
	float minMicrophoneDistanceToDirty = 0.0f; ///< AnimationSoundClientBehaviorGlobalSetting (RW 0x83F3EA, table 0xC53E8C)
	EvaEventStore eva;                         ///< Eva.ini (TheEva)
	LargeGroupAudioStore largeGroupAudio;      ///< LargeGroupAudio.ini (TheLargeGroupAudio)

	// The keywords registerBlocks() registers.
	static const std::vector<std::string> &blockKeywords();
	void registerBlocks(INIBlockRegistry &registry);

	// TheAudio's init: the files in retail order (a missing file is an error: INI::load throws). `Data\INI\Voice.ini` is loaded
	// but `Data\INI\Default\Voice.ini` is not (spec 9.5).
	static const std::vector<std::string> &loadOrder();
	void loadAll(INI &ini);
	// Eva.ini (Default first, RW 0x5DE837) and LargeGroupAudio.ini (RW 0x60D70E): loaded by their own subsystems, after the audio files
	void loadEva(INI &ini);
	void loadLargeGroupAudio(INI &ini);
};
