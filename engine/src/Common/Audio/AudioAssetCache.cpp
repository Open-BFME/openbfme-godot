// OpenBFME. GPL-3.0. See AudioAssetCache.h.

#include "Common/Audio/AudioAssetCache.h"

#include "Common/AsciiString.h"

#include <chrono>

AudioAssetCache::AudioAssetCache(ArchiveFileSystem *files, std::uint64_t decodedBudgetBytes) : m_files(files), m_budget(decodedBudgetBytes) {}

bool AudioAssetCache::exists(const std::string &virtualPath) const
{
	return m_files && m_files->doesFileExist(virtualPath);
}

std::shared_ptr<const std::vector<std::uint8_t>> AudioAssetCache::readFile(const std::string &virtualPath, std::string *error)
{
	if (!m_files)
	{
		if (error)
		{
			*error = "no archive file system";
		}
		return nullptr;
	}
	auto bytes = std::make_shared<std::vector<std::uint8_t>>();
	std::string e;
	if (!m_files->readFile(virtualPath, *bytes, &e))
	{
		++m_stats.errors;
		if (error)
		{
			*error = "cannot read '" + virtualPath + "': " + e;
		}
		return nullptr;
	}
	return bytes;
}

bool AudioAssetCache::probe(const std::string &virtualPath, AudioDecode::AudioInfo *info, std::string *error)
{
	auto bytes = readFile(virtualPath, error);
	if (!bytes)
	{
		return false;
	}
	try
	{
		*info = AudioDecode::probe(bytes->data(), bytes->size());
	}
	catch (const std::exception &e)
	{
		++m_stats.errors;
		if (error)
		{
			*error = "cannot probe '" + virtualPath + "': " + e.what();
		}
		return false;
	}
	return true;
}

std::shared_ptr<const AudioDecode::DecodedAudio> AudioAssetCache::decoded(const std::string &virtualPath, std::string *error)
{
	const std::string key = AsciiStringUtil::lowered(virtualPath);
	auto it = m_entries.find(key);
	if (it != m_entries.end())
	{
		++m_stats.hits;
		m_lru.erase(it->second.lru);
		m_lru.push_front(key);
		it->second.lru = m_lru.begin();
		return it->second.audio;
	}
	++m_stats.misses;
	auto bytes = readFile(virtualPath, error);
	if (!bytes)
	{
		return nullptr;
	}
	std::shared_ptr<AudioDecode::DecodedAudio> audio;
	const auto t0 = std::chrono::steady_clock::now();
	try
	{
		audio = std::make_shared<AudioDecode::DecodedAudio>(AudioDecode::decode(bytes->data(), bytes->size()));
	}
	catch (const std::exception &e)
	{
		++m_stats.errors;
		if (error)
		{
			*error = "cannot decode '" + virtualPath + "': " + e.what();
		}
		return nullptr;
	}
	m_stats.decodeMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	++m_stats.decodes;
	Entry entry;
	entry.audio = audio;
	entry.bytes = (std::uint64_t)audio->pcm.size() * sizeof(std::int16_t);
	m_lru.push_front(key);
	entry.lru = m_lru.begin();
	m_stats.bytesCached += entry.bytes;
	m_entries.emplace(key, entry);
	// evict the least recently used entries beyond the budget (never the one just inserted)
	while (m_stats.bytesCached > m_budget && m_lru.size() > 1)
	{
		const std::string &victim = m_lru.back();
		auto v = m_entries.find(victim);
		m_stats.bytesCached -= v->second.bytes;
		m_entries.erase(v);
		m_lru.pop_back();
		++m_stats.evictions;
	}
	if (m_stats.bytesCached > m_stats.peakBytesCached)
	{
		m_stats.peakBytesCached = m_stats.bytesCached;
	}
	return audio;
}

double AudioAssetCache::lengthMs(const std::string &virtualPath, std::string *error)
{
	AudioDecode::AudioInfo info;
	if (!probe(virtualPath, &info, error))
	{
		return -1.0;
	}
	return info.durationMs();
}

void AudioAssetCache::clear()
{
	m_entries.clear();
	m_lru.clear();
	m_stats.bytesCached = 0;
}
