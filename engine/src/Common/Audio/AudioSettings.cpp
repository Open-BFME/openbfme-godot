// OpenBFME. GPL-3.0. See AudioSettings.h for the sources.

#include "Common/Audio/AudioSettings.h"

#include "Common/INIException.h"

#include <cmath>
#include <deque>
#include <cstddef>
#include <limits>

const char *const kReverbRoomNames[25] = { "PaddedCell", "Room", "Bathroom", "LivingRoom", "StoneRoom", "Auditorium", "ConcertHall", "Cave", "Arena", "Hangar", "CarpetedHallway", "Hallway",
	"StoneCorridor", "Alley", "Forest", "City", "Mountains", "Quarry", "Plain", "ParkingLot", "SewerPipe", "Underwater", "Drugged", "Dizzy", "Psychotic" };

namespace
{
const float kNaN = std::numeric_limits<float>::quiet_NaN();

#define AS(member) (int)offsetof(AudioSettings, member)
#define MIC(group, member) (int)(offsetof(AudioSettings, microphone) + (group) * sizeof(MicrophoneSettings) + offsetof(MicrophoneSettings, member))

// Rows of the table at RW 0xBD7698 that are not generated (reverb multipliers and microphone groups are appended in buildTable1).
const FieldParse kTable2[] = {
	{ "AudioRoot", INI::parseAsciiString, nullptr, AS(audioRoot) },
	{ "SoundsFolder", INI::parseAsciiString, nullptr, AS(soundsFolder) },
	{ "MusicFolder", INI::parseAsciiString, nullptr, AS(musicFolder) },
	{ "StreamingFolder", INI::parseAsciiString, nullptr, AS(streamingFolder) },
	{ "AmbientStreamFolder", INI::parseAsciiString, nullptr, AS(ambientStreamFolder) },
	{ "SoundsExtension", INI::parseAsciiString, nullptr, AS(soundsExtension) },
	{ "MusicScriptLibraryName", INI::parseAsciiString, nullptr, AS(musicScriptLibraryName) },
	{ "DefaultSoundVolume", INI::parsePercentToReal, nullptr, AS(defaultSoundVolume) },
	{ "DefaultVoiceVolume", INI::parsePercentToReal, nullptr, AS(defaultVoiceVolume) },
	{ "DefaultMusicVolume", INI::parsePercentToReal, nullptr, AS(defaultMusicVolume) },
	{ "DefaultMovieVolume", INI::parsePercentToReal, nullptr, AS(defaultMovieVolume) },
	{ "DefaultAmbientVolume", INI::parsePercentToReal, nullptr, AS(defaultAmbientVolume) },
	{ "VoiceMoveToCampMaxCampnessAtStartPoint", INI::parseUnsignedInt, nullptr, AS(voiceMoveToCampMaxCampnessAtStartPoint) },
	{ "VoiceMoveToCampMinCampnessAtEndPoint", INI::parseUnsignedInt, nullptr, AS(voiceMoveToCampMinCampnessAtEndPoint) },
	{ "MinDelayBetweenEnterStateVoiceMS", INI::parseDurationUnsignedInt, nullptr, AS(minDelayBetweenEnterStateVoiceFrames) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW table 0xBD7698 in the binary's row order.
const FieldParse *table1()
{
	static std::vector<FieldParse> rows;
	static std::deque<std::string> reverbNames; // stable c_str() pointers
	static std::deque<std::string> names;
	if (rows.empty())
	{
		auto add = [&](const char *name, INIFieldParseProc proc, int offset) { rows.push_back({ name, proc, nullptr, offset }); };
		add("UseDigital", INI::parseBool, AS(useDigital));
		add("UseMidi", INI::parseBool, AS(useMidi));
		add("OutputRate", INI::parseInt, AS(outputRate));
		add("OutputBits", INI::parseInt, AS(outputBits));
		add("OutputChannels", INI::parseInt, AS(outputChannels));
		add("SampleCount2D", INI::parseInt, AS(sampleCount2D));
		add("SampleCount3D", INI::parseInt, AS(sampleCount3D));
		add("StreamCount", INI::parseInt, AS(streamCount));
		add("MixaheadLatency", INI::parseUnsignedInt, AS(mixaheadLatency));
		add("MixaheadLatencyDuringMovies", INI::parseUnsignedInt, AS(mixaheadLatencyDuringMovies));
		add("LoopBufferLengthMS", INI::parseUnsignedInt, AS(loopBufferLengthMS));
		add("LoopBufferCallbackCallsPerBufferLength", INI::parseUnsignedInt, AS(loopBufferCallbackCallsPerBufferLength));
		add("AutomaticSubtitleDurationMS", INI::parseInt, AS(automaticSubtitleDurationMS));
		add("AutomaticSubtitleWindowWidth", INI::parseInt, AS(automaticSubtitleWindowWidth));
		add("AutomaticSubtitleLines", INI::parseInt, AS(automaticSubtitleLines));
		add("AutomaticSubtitleWindowColor", INI::parseColorInt, AS(automaticSubtitleWindowColor));
		add("AutomaticSubtitleTextColor", INI::parseColorInt, AS(automaticSubtitleTextColor));
		add("ForceResetTimeSeconds", INI::parseInt, AS(forceResetTimeSeconds));
		add("EmergencyResetTimeSeconds", INI::parseInt, AS(emergencyResetTimeSeconds));
		add("SuppressOcclusion", INI::parseBool, AS(suppressOcclusion));
		add("MinOcclusion", INI::parsePercentToReal, AS(minOcclusion));
		add("MillisecondsPriorToPlayingToReadSoundFile", INI::parseInt, AS(millisecondsPriorToPlayingToReadSoundFile));
		add("MinSampleVolume", INI::parsePercentToReal, AS(minSampleVolume));
		add("PositionDeltaForReverbRecheck", INI::parseReal, AS(positionDeltaForReverbRecheck));
		add("GlobalMinRange", INI::parseInt, AS(globalMinRange));
		add("GlobalMaxRange", INI::parseInt, AS(globalMaxRange));
		add("TimeToFadeAudio", INI::parseInt, AS(timeToFadeAudio));
		add("AmbientStreamHysteresisVolume", INI::parseInt, AS(ambientStreamHysteresisVolume));
		add("AudioFootprintInBytes", INI::parseUnsignedInt, AS(audioFootprintInBytes));
		for (int i = 0; i < 25; ++i)
		{
			reverbNames.push_back(std::string("Global") + kReverbRoomNames[i] + "ReverbMultiplier");
			add(reverbNames.back().c_str(), INI::parsePercentToReal, AS(reverbMultiplier) + i * (int)sizeof(float));
		}
		for (int g = 0; g < 2; ++g)
		{
			const std::string prefix = g == 0 ? "" : "LivingWorld";
			struct Row
			{
				const char *name;
				INIFieldParseProc proc;
				int member;
			};
			const Row group[] = {
				{ "MicrophonePreferredFractionCameraToGround", INI::parsePercentToReal, MIC(g, preferredFractionCameraToGround) },
				{ "MicrophonePullTowardsTerrainLookAtPointPercent", INI::parsePercentToReal, MIC(g, pullTowardsTerrainLookAtPointPercent) },
				{ "MicrophoneMinDistanceToCamera", INI::parseReal, MIC(g, minDistanceToCamera) },
				{ "MicrophoneMaxDistanceToCamera", INI::parseReal, MIC(g, maxDistanceToCamera) },
				{ "ZoomMinDistance", INI::parseReal, MIC(g, zoomMinDistance) },
				{ "ZoomMaxDistance", INI::parseReal, MIC(g, zoomMaxDistance) },
				{ "ZoomSoundVolumePercentageAmount", INI::parsePercentToReal, MIC(g, zoomSoundVolumePercentageAmount) },
				{ "ZoomFadeDistanceForMaxEffect", INI::parseReal, MIC(g, zoomFadeDistanceForMaxEffect) },
				{ "ZoomFadeZeroEffectEdgeLength", INI::parseReal, MIC(g, zoomFadeZeroEffectEdgeLength) },
				{ "ZoomFadeFullEffectEdgeLength", INI::parseReal, MIC(g, zoomFadeFullEffectEdgeLength) },
			};
			// the binary lists the tactical rows (Preferred, PullTowards, MinDistance, MaxDistance, ZoomMin ...) before the Living World ones
			// in two interleaved blocks; the row ORDER does not change what an INI means (fields are looked up by name)
			for (const Row &r : group)
			{
				names.push_back(prefix + r.name);
				add(names.back().c_str(), r.proc, r.member);
			}
		}
		rows.push_back({ nullptr, nullptr, nullptr, 0 });
	}
	return rows.data();
}

void fixNaN(float &tactical, float &livingWorld, float &third)
{
	if (std::isnan(tactical))
	{
		tactical = 0.0f;
	}
	if (std::isnan(livingWorld))
	{
		livingWorld = tactical;
	}
	if (std::isnan(third))
	{
		third = tactical;
	}
}
} // namespace

AudioSettings::AudioSettings()
	: defaultSoundVolume(0.0f), defaultVoiceVolume(0.0f), defaultMusicVolume(0.0f), defaultAmbientVolume(0.0f), defaultMovieVolume(0.0f), voiceMoveToCampMaxCampnessAtStartPoint(0),
	  voiceMoveToCampMinCampnessAtEndPoint(0), minDelayBetweenEnterStateVoiceFrames(0), useDigital(true), useMidi(false), outputRate(44100), outputBits(16), outputChannels(2), sampleCount2D(0),
	  sampleCount3D(0), streamCount(0), globalMinRange(0), globalMaxRange(0), timeToFadeAudio(0), ambientStreamHysteresisVolume(0), audioFootprintInBytes(0), mixaheadLatency(0),
	  mixaheadLatencyDuringMovies(0), loopBufferLengthMS(0), loopBufferCallbackCallsPerBufferLength(0), automaticSubtitleDurationMS(0), automaticSubtitleWindowWidth(0), automaticSubtitleLines(0),
	  automaticSubtitleWindowColor(0), automaticSubtitleTextColor(0), forceResetTimeSeconds(0), emergencyResetTimeSeconds(0), suppressOcclusion(false), minOcclusion(0.0f),
	  millisecondsPriorToPlayingToReadSoundFile(0), minSampleVolume(0.0f), positionDeltaForReverbRecheck(0.0f)
{
	for (float &m : reverbMultiplier)
	{
		m = 1.0f;
	}
	for (int g = 0; g < 3; ++g)
	{
		MicrophoneSettings &m = microphone[g];
		m = MicrophoneSettings();
		// the tactical group starts at 0, the other groups start as NaN = "not given" (the post-processing copies the tactical value)
		const float init = g == 0 ? 0.0f : kNaN;
		m.preferredFractionCameraToGround = init;
		m.minDistanceToCamera = init;
		m.maxDistanceToCamera = init;
		m.pullTowardsTerrainLookAtPointPercent = init;
		m.zoomMinDistance = init;
		m.zoomMaxDistance = init;
		m.zoomSoundVolumePercentageAmount = init;
		m.zoomFadeDistanceForMaxEffect = init;
		m.zoomFadeZeroEffectEdgeLength = init;
		m.zoomFadeFullEffectEdgeLength = init;
	}
}

std::vector<std::string> AudioSettings::fieldNames()
{
	std::vector<std::string> names;
	for (const FieldParse *t : { table1(), kTable2 })
	{
		for (const FieldParse *f = t; f->token; ++f)
		{
			names.push_back(f->token);
		}
	}
	return names;
}

std::string AudioSettings::joinFolder(const std::string &root, const std::string &folder)
{
	return root + "\\" + folder + "\\";
}

void AudioSettings::postProcess()
{
	MicrophoneSettings &t = microphone[0];
	MicrophoneSettings &l = microphone[1];
	MicrophoneSettings &x = microphone[2];
	fixNaN(t.preferredFractionCameraToGround, l.preferredFractionCameraToGround, x.preferredFractionCameraToGround);
	fixNaN(t.pullTowardsTerrainLookAtPointPercent, l.pullTowardsTerrainLookAtPointPercent, x.pullTowardsTerrainLookAtPointPercent);
	fixNaN(t.zoomSoundVolumePercentageAmount, l.zoomSoundVolumePercentageAmount, x.zoomSoundVolumePercentageAmount);
	fixNaN(t.minDistanceToCamera, l.minDistanceToCamera, x.minDistanceToCamera);
	fixNaN(t.maxDistanceToCamera, l.maxDistanceToCamera, x.maxDistanceToCamera);
	fixNaN(t.zoomMinDistance, l.zoomMinDistance, x.zoomMinDistance);
	fixNaN(t.zoomMaxDistance, l.zoomMaxDistance, x.zoomMaxDistance);
	fixNaN(t.zoomFadeDistanceForMaxEffect, l.zoomFadeDistanceForMaxEffect, x.zoomFadeDistanceForMaxEffect);
	fixNaN(t.zoomFadeZeroEffectEdgeLength, l.zoomFadeZeroEffectEdgeLength, x.zoomFadeZeroEffectEdgeLength);
	fixNaN(t.zoomFadeFullEffectEdgeLength, l.zoomFadeFullEffectEdgeLength, x.zoomFadeFullEffectEdgeLength);
	for (MicrophoneSettings &m : microphone)
	{
		// RW 0x44104C: the square is stored beside the value
		m.preferredFractionCameraToGroundSq = m.preferredFractionCameraToGround * m.preferredFractionCameraToGround;
		m.minDistanceToCameraSq = m.minDistanceToCamera * m.minDistanceToCamera;
		m.maxDistanceToCameraSq = m.maxDistanceToCamera * m.maxDistanceToCamera;
		m.zoomMinDistanceSq = m.zoomMinDistance * m.zoomMinDistance;
		m.zoomMaxDistanceSq = m.zoomMaxDistance * m.zoomMaxDistance;
		m.zoomFadeDistanceForMaxEffectSq = m.zoomFadeDistanceForMaxEffect * m.zoomFadeDistanceForMaxEffect;
		m.zoomFadeZeroEffectEdgeLengthSq = m.zoomFadeZeroEffectEdgeLength * m.zoomFadeZeroEffectEdgeLength;
		m.zoomFadeFullEffectEdgeLengthSq = m.zoomFadeFullEffectEdgeLength * m.zoomFadeFullEffectEdgeLength;
	}
	// RW 0x6453B4: soundsFolder, musicFolder, streamingFolder, ambientStreamFolder are joined to the root
	soundsFolder = joinFolder(audioRoot, soundsFolder);
	musicFolder = joinFolder(audioRoot, musicFolder);
	streamingFolder = joinFolder(audioRoot, streamingFolder);
	ambientStreamFolder = joinFolder(audioRoot, ambientStreamFolder);
}

void AudioSettings::parse(INI *ini, AudioSettings &settings)
{
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		throw INIException(3, "You cannot define or override the AudioSettings in map.ini");
	}
	// a block merges over the earlier values: the joined folders must not be joined twice
	// (retail parses into the live object; a second AudioSettings block would see the already joined folders, RW 0x4410F1-0x441198)
	MultiIniFieldParse tables;
	tables.add(table1(), 0);
	tables.add(kTable2, 0);
	ini->initFromINIMulti(&settings, tables);
	settings.postProcess();
}
