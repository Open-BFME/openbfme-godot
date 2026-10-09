// OpenBFME. GPL-3.0.
//
// IAudioDevice: the boundary between the audio manager core (which decides WHAT plays, how loud and when it stops) and a backend that
// makes sound (the Godot device) or only simulates it (SimulatedAudioDevice, the tests and the headless checks).
//
// It replaces the Miles calls of ZH MilesAudioManager.cpp (AIL_open_stream / AIL_start_sample / AIL_set_3D_position ...): the core owns the
// decisions, the device owns the channel and the sample data. The core computes the final volume and the pan of every voice (RW
// getEffectiveVolume 0x4591D4, including the linear distance attenuation), so a backend only has to play a stream at a volume, a pan and a
// pitch.

#pragma once

#include "Common/INIDataTypes.h"

#include <string>

enum class VoiceKind
{
	Sample2D,
	Sample3D,
	Stream
};

struct VoiceParams
{
	float volume = 1.0f; ///< final linear volume 0..1 (sliders, distance and shifts applied)
	float pan = 0.0f;    ///< -1 (left) .. +1 (right); 0 for 2D sounds
	float pitch = 1.0f;  ///< playback rate multiplier
};

struct VoiceStart
{
	VoiceKind kind = VoiceKind::Sample2D;
	std::string file;      ///< the virtual path inside the archives
	std::string eventName; ///< for the device's own diagnostics
	VoiceParams params;
	bool loop = false;       ///< the device loops the file by itself (a music track, an ambient stream)
	float startFraction = 0.0f; ///< 0..1: where in the file to start (RANDOMSTART)
	float fadeInMs = 0.0f;   ///< FADE_ON_START
};

class IAudioDevice
{
public:
	virtual ~IAudioDevice() {}
	// A voice id > 0, or 0 with *error set (missing file, undecodable data): the caller reports it, nothing is substituted.
	virtual int startVoice(const VoiceStart &start, std::string *error) = 0;
	virtual void updateVoice(int voice, const VoiceParams &params) = 0;
	virtual void stopVoice(int voice) = 0;
	// false once the voice has finished (or was stopped)
	virtual bool isVoicePlaying(int voice) = 0;
	// Playing time of a file at pitch 1.0; -1 + *error when unavailable
	virtual double fileLengthMs(const std::string &file, std::string *error) = 0;
	virtual void setListener(const Coord3D &position, const Coord3D &forward) = 0;
};
