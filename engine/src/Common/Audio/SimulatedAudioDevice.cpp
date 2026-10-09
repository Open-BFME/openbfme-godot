// OpenBFME. GPL-3.0. See SimulatedAudioDevice.h.

#include "Common/Audio/SimulatedAudioDevice.h"

double SimulatedAudioDevice::fileLengthMs(const std::string &file, std::string *error)
{
	const auto it = m_lengths.find(file);
	if (it != m_lengths.end())
	{
		return it->second;
	}
	if (!m_cache)
	{
		if (error)
		{
			*error = "no length known for '" + file + "'";
		}
		return -1.0;
	}
	const double ms = m_cache->lengthMs(file, error);
	if (ms >= 0.0)
	{
		m_lengths[file] = ms;
	}
	return ms;
}

size_t SimulatedAudioDevice::heldVoices() const
{
	size_t n = 0;
	for (const auto &kv : m_voices)
	{
		n += kv.second.stopped ? 0 : 1;
	}
	return n;
}

int SimulatedAudioDevice::startVoice(const VoiceStart &start, std::string *error)
{
	if (m_poolSize && heldVoices() >= m_poolSize)
	{
		if (error)
		{
			*error = "voice pool exhausted (" + std::to_string(m_poolSize) + " slots, none released)";
		}
		return 0;
	}
	const double length = fileLengthMs(start.file, error);
	if (length < 0.0)
	{
		return 0;
	}
	Voice v;
	v.start = start;
	v.params = start.params;
	v.startMs = m_nowMs;
	const float pitch = start.params.pitch > 0.01f ? start.params.pitch : 1.0f;
	v.lengthMs = length / pitch;
	const int id = m_nextVoice++;
	m_voices.emplace(id, v);
	m_log.push_back({ LogEntry::Start, id, start.file, m_nowMs });
	return id;
}

void SimulatedAudioDevice::updateVoice(int voice, const VoiceParams &params)
{
	const auto it = m_voices.find(voice);
	if (it != m_voices.end() && !it->second.stopped)
	{
		it->second.params = params;
		++it->second.updates;
	}
}

void SimulatedAudioDevice::stopVoice(int voice)
{
	const auto it = m_voices.find(voice);
	if (it != m_voices.end() && !it->second.stopped)
	{
		it->second.stopped = true;
		m_log.push_back({ LogEntry::Stop, voice, it->second.start.file, m_nowMs });
	}
}

bool SimulatedAudioDevice::isVoicePlaying(int voice)
{
	const auto it = m_voices.find(voice);
	if (it == m_voices.end() || it->second.stopped)
	{
		return false;
	}
	return it->second.start.loop || m_nowMs < it->second.startMs + it->second.lengthMs;
}

size_t SimulatedAudioDevice::activeVoices() const
{
	size_t n = 0;
	for (const auto &kv : m_voices)
	{
		if (!kv.second.stopped && (kv.second.start.loop || m_nowMs < kv.second.startMs + kv.second.lengthMs))
		{
			++n;
		}
	}
	return n;
}

size_t SimulatedAudioDevice::startsOf(const std::string &file) const
{
	size_t n = 0;
	for (const LogEntry &e : m_log)
	{
		if (e.kind == LogEntry::Start && e.file == file)
		{
			++n;
		}
	}
	return n;
}
