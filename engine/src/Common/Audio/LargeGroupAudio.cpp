// OpenBFME. GPL-3.0. See LargeGroupAudio.h.

#include "Common/Audio/LargeGroupAudio.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <algorithm>

namespace
{
struct SoundCtx
{
	LargeGroupAudioSound *sound;
	const AudioEventInfoStore *audio;
};

void parseKey(INI *ini, void *instance, void *, const void *)
{
	LargeGroupAudioSound *s = static_cast<SoundCtx *>(instance)->sound;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		s->keys.push_back(t);
	}
}

void parseSoundEvent(INI *ini, void *instance, void *, const void *)
{
	SoundCtx *c = static_cast<SoundCtx *>(instance);
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		c->sound->sound.clear();
		return;
	}
	if (!c->audio->contains(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	c->sound->sound = name;
}

// RW 0x972338: AudioMap:<name> Sound:<name> Multiplier:<percent>
void parseDuck(INI *ini, void *instance, void *, const void *)
{
	LargeGroupAudioSound *s = static_cast<SoundCtx *>(instance)->sound;
	LargeGroupAudioDuck d;
	const char *seps = ini->getSepsColon();
	const char *t = ini->getNextToken(seps);
	if (AsciiStringUtil::compareNoCase(t, "AudioMap") != 0)
	{
		throw INIException(8, "Expected %s:<name> next", "AudioMap");
	}
	d.audioMap = ini->getNextToken(seps);
	t = ini->getNextToken(seps);
	if (AsciiStringUtil::compareNoCase(t, "Sound") != 0)
	{
		throw INIException(8, "Expected %s:<name> next", "Sound");
	}
	d.sound = ini->getNextToken(seps);
	t = ini->getNextToken(seps);
	if (AsciiStringUtil::compareNoCase(t, "Multiplier") != 0)
	{
		throw INIException(8, "Expected %s:<volume percent> next", "Multiplier");
	}
	d.multiplier = ini->scanPercentToReal(ini->getNextToken(seps));
	s->ducks.push_back(d);
}

struct MapCtx
{
	LargeGroupAudioMap *map;
	const AudioEventInfoStore *audio;
	LargeGroupAudioStore *store;
	int *autoName;
};

// the nested `Sound [name]` block (RW 0x7EF63F)
void parseSoundBlock(INI *ini, void *instance, void *, const void *)
{
	MapCtx *c = static_cast<MapCtx *>(instance);
	const char *nameToken = ini->getNextTokenOrNull();
	LargeGroupAudioSound sound;
	sound.name = nameToken ? nameToken : "";
	const bool mapIni = ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES || ini->getLoadType() == INI_LOAD_RELOAD;
	SoundCtx sc = { &sound, c->audio };
	const FieldParse table[] = { { "Sound", parseSoundEvent, nullptr, 0 }, { "Key", parseKey, nullptr, 0 }, { "Duck", parseDuck, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
	ini->initFromINI(&sc, table);
	if (sound.name.empty())
	{
		sound.name = "<unnamed " + std::to_string((*c->autoName)++) + ">"; // INFERENCE: unnamed blocks never clash
	}
	for (LargeGroupAudioSound &existing : c->map->sounds)
	{
		if (existing.name == sound.name)
		{
			if (!mapIni)
			{
				throw INIException(3, "LargeGroupAudio: You cannot use the same name(%s) for two Sound blocks within the same LargeGroupAudioMap(%s)", sound.name.c_str(), c->map->name.c_str());
			}
			existing = sound; // a map.ini replaces the earlier block
			return;
		}
	}
	c->map->sounds.push_back(sound);
}

void parseModelConditions(INI *ini, void *instance, void *, const void *which)
{
	LargeGroupAudioMap *m = static_cast<MapCtx *>(instance)->map;
	ParseModelConditionFlags(ini, nullptr, which ? static_cast<void *>(&m->excludedModelConditionFlags) : static_cast<void *>(&m->requiredModelConditionFlags), nullptr);
}

void parseStatusBits(INI *ini, void *instance, void *, const void *which)
{
	LargeGroupAudioMap *m = static_cast<MapCtx *>(instance)->map;
	ParseObjectStatusMask(ini, nullptr, which ? static_cast<void *>(&m->excludedObjectStatusBits) : static_cast<void *>(&m->requiredObjectStatusBits), nullptr);
}

void parseSize(INI *ini, void *instance, void *, const void *) { INI::parseReal(ini, nullptr, &static_cast<MapCtx *>(instance)->map->size, nullptr); }
void parseSpeed(INI *ini, void *instance, void *, const void *) { INI::parseVelocityReal(ini, nullptr, &static_cast<MapCtx *>(instance)->map->maximumAudioSpeed, nullptr); }
void parseStart(INI *ini, void *instance, void *, const void *) { INI::parseUnsignedShort(ini, nullptr, &static_cast<MapCtx *>(instance)->map->startThreshold, nullptr); }
void parseStop(INI *ini, void *instance, void *, const void *) { INI::parseUnsignedShort(ini, nullptr, &static_cast<MapCtx *>(instance)->map->stopThreshold, nullptr); }
void parseHandOff(INI *ini, void *instance, void *, const void *) { INI::parseDurationUnsignedInt(ini, nullptr, &static_cast<MapCtx *>(instance)->map->handOffModeDurationFrames, nullptr); }
void parseStealth(INI *ini, void *instance, void *, const void *) { INI::parseBool(ini, nullptr, &static_cast<MapCtx *>(instance)->map->ignoreStealthedUnits, nullptr); }
} // namespace

const std::vector<std::string> &LargeGroupAudioStore::loadOrder()
{
	static const std::vector<std::string> files = { "Data\\INI\\LargeGroupAudio.ini" };
	return files;
}

const LargeGroupAudioMap *LargeGroupAudioStore::find(const std::string &name) const
{
	for (const LargeGroupAudioMap &m : m_maps)
	{
		if (m.name == name)
		{
			return &m;
		}
	}
	return nullptr;
}

void LargeGroupAudioStore::parseMap(INI *ini, const AudioEventInfoStore &audio)
{
	const std::string name = ini->getNextToken();
	const bool mapIni = ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES;
	LargeGroupAudioMap *map = nullptr;
	for (LargeGroupAudioMap &m : m_maps)
	{
		if (m.name == name)
		{
			map = &m;
			break;
		}
	}
	if (!map)
	{
		m_maps.emplace_back();
		map = &m_maps.back();
		map->name = name;
		if (mapIni)
		{
			m_mapAdded.push_back(name);
		}
	}
	else if (mapIni)
	{
		m_savedOriginals.push_back(*map); // restored by resetMapOverrides()
	}
	MapCtx ctx = { map, &audio, this, &m_autoName };
	// RW table 0xC4C5D0 (the model condition / status rows tell Required from Excluded through userData)
	const FieldParse table[] = {
		{ "Size", parseSize, nullptr, 0 },
		{ "Sound", parseSoundBlock, nullptr, 0 },
		{ "StartThreshold", parseStart, nullptr, 0 },
		{ "StopThreshold", parseStop, nullptr, 0 },
		{ "HandOffModeDuration", parseHandOff, nullptr, 0 },
		{ "MaximumAudioSpeed", parseSpeed, nullptr, 0 },
		{ "RequiredModelConditionFlags", parseModelConditions, nullptr, 0 },
		{ "ExcludedModelConditionFlags", parseModelConditions, (const void *)1, 0 },
		{ "RequiredObjectStatusBits", parseStatusBits, nullptr, 0 },
		{ "ExcludedObjectStatusBits", parseStatusBits, (const void *)1, 0 },
		{ "IgnoreStealthedUnits", parseStealth, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(&ctx, table);
}

void LargeGroupAudioStore::parseUnusedKnownKeys(INI *ini)
{
	struct Parsers
	{
		static void key(INI *ini2, void *instance, void *, const void *)
		{
			std::vector<std::string> *keys = static_cast<std::vector<std::string> *>(instance);
			for (const char *t = ini2->getNextTokenOrNull(); t; t = ini2->getNextTokenOrNull())
			{
				keys->push_back(t);
			}
		}
	};
	const FieldParse table[] = { { "Key", Parsers::key, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
	ini->initFromINI(&m_unusedKnownKeys, table);
}

void LargeGroupAudioStore::resetMapOverrides()
{
	for (const std::string &name : m_mapAdded)
	{
		m_maps.erase(std::remove_if(m_maps.begin(), m_maps.end(), [&](const LargeGroupAudioMap &m) { return m.name == name; }), m_maps.end());
	}
	m_mapAdded.clear();
	// restore in reverse so the first saved original wins when a map was edited twice
	for (auto it = m_savedOriginals.rbegin(); it != m_savedOriginals.rend(); ++it)
	{
		for (LargeGroupAudioMap &m : m_maps)
		{
			if (m.name == it->name)
			{
				m = *it;
			}
		}
	}
	m_savedOriginals.clear();
}
