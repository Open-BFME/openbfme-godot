// OpenBFME. GPL-3.0.
//
// SimulatedAudioDevice: an IAudioDevice that makes no sound. It knows the length of every file (the asset cache probes the real file) and
// a clock the test or the headless run advances, so the manager's whole behaviour (limits, loops, attack / decay chains, music chains,
// fades) can be driven and asserted deterministically. It also records every voice it was asked to start.

#pragma once

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioDevice.h"

#include <map>
#include <string>
#include <vector>

class SimulatedAudioDevice : public IAudioDevice
{
public:
	explicit SimulatedAudioDevice(AudioAssetCache *cache) : m_cache(cache) {}

	struct Voice
	{
		VoiceStart start;
		VoiceParams params;
		double startMs = 0.0;
		double lengthMs = 0.0;
		bool stopped = false;
		int updates = 0;
	};
	struct LogEntry
	{
		enum Kind { Start, Stop } kind;
		int voice;
		std::string file;
		double atMs;
	};

	void setTime(double ms) { m_nowMs = ms; }
	double now() const { return m_nowMs; }
	// a file length override for tests that run without retail files (ms, at pitch 1)
	void setFileLength(const std::string &file, double ms) { m_lengths[file] = ms; }

	int startVoice(const VoiceStart &start, std::string *error) override;
	void updateVoice(int voice, const VoiceParams &params) override;
	void stopVoice(int voice) override;
	bool isVoicePlaying(int voice) override;
	double fileLengthMs(const std::string &file, std::string *error) override;
	void setListener(const Coord3D &position, const Coord3D &forward) override
	{
		m_listener = position;
		m_forward = forward;
	}

	const std::map<int, Voice> &voices() const { return m_voices; }
	const std::vector<LogEntry> &log() const { return m_log; }
	size_t activeVoices() const;
	// Models a fixed voice pool (the Godot device): a voice holds its slot from startVoice until stopVoice, even after it finished naturally.
	// With a pool size set, startVoice fails (reported through `error`) once every slot is held. 0 = unlimited.
	void setPoolSize(size_t n) { m_poolSize = n; }
	size_t heldVoices() const;
	const Voice *find(int voice) const
	{
		const auto it = m_voices.find(voice);
		return it == m_voices.end() ? nullptr : &it->second;
	}
	size_t startsOf(const std::string &file) const;

private:
	AudioAssetCache *m_cache;
	double m_nowMs = 0.0;
	int m_nextVoice = 1;
	size_t m_poolSize = 0;
	std::map<int, Voice> m_voices;
	std::map<std::string, double> m_lengths;
	std::vector<LogEntry> m_log;
	Coord3D m_listener;
	Coord3D m_forward;
};
