// OpenBFME. GPL-3.0. See AudioEventRTS.h for the sources (RW AudioEventRTS.cpp, 0x6DA5xx-0x6DC7xx).

#include "Common/Audio/AudioEventRTS.h"

#include "Common/AsciiString.h"

namespace
{
// RW 0x6DA7AA: the folder of a sound type (a getter per type into the AudioSettings strings); type 5 has none.
const std::string &folderFor(const AudioSettings &s, int soundType)
{
	static const std::string none;
	switch (soundType)
	{
	case AT_Music:
		return s.musicFolder;
	case AT_Streaming:
		return s.streamingFolder;
	case AT_SoundEffect:
	case AT_StreamedSound:
		return s.soundsFolder;
	case AT_AmbientStream:
		return s.ambientStreamFolder;
	default:
		return none;
	}
}

// RW 0x6DB4EA: "." + SoundsExtension for sound effects only.
std::string extensionFor(const AudioSettings &s, int soundType)
{
	return soundType == AT_SoundEffect ? "." + s.soundsExtension : std::string();
}
} // namespace

int AudioWeightedChoice(int totalWeight, const WeightedSoundList &list, GameLogicRandom &random)
{
	if (totalWeight <= 0)
	{
		return -1;
	}
	// RW 0x6DA80E: r = GetGameAudioRandomValue(0, total - 1) at AudioEventRTS.cpp line 58, then subtract the weights until r is below one
	unsigned remaining = (unsigned)random.getValue(0, totalWeight - 1, "AudioEventRTS.cpp", 58);
	size_t i = 0;
	for (; i < list.sounds.size(); ++i)
	{
		if (remaining < (unsigned)list.sounds[i].weight)
		{
			return (int)i;
		}
		remaining -= (unsigned)list.sounds[i].weight;
	}
	return 0; // the weights did not cover the draw (cannot happen when total == sum): retail returns index 0 (xor eax, eax)
}

AudioEventRTS::AudioEventRTS() {}

AudioEventRTS::AudioEventRTS(const std::string &eventName, int viewType)
{
	init(eventName);
	m_viewType = viewType;
}

AudioEventRTS::AudioEventRTS(const std::string &eventName, const Coord3D &position, int viewType)
{
	init(eventName);
	m_viewType = viewType;
	m_position = position;
	m_ownerType = OT_Positional;
	m_hasPosition = true;
}

AudioEventRTS AudioEventRTS::forObject(const std::string &eventName, std::uint32_t objectId)
{
	AudioEventRTS e(eventName);
	if (objectId != 0)
	{
		e.m_ownerId = objectId;
		e.m_ownerType = OT_Object;
	}
	return e;
}

AudioEventRTS AudioEventRTS::forDrawable(const std::string &eventName, std::uint32_t drawableId)
{
	AudioEventRTS e(eventName);
	if (drawableId != 0)
	{
		e.m_ownerId = drawableId;
		e.m_ownerType = OT_Drawable;
	}
	return e;
}

void AudioEventRTS::init(const std::string &name)
{
	m_eventName = name;
}

void AudioEventRTS::setEventName(const std::string &name)
{
	if (name != m_eventName)
	{
		m_info.reset(); // the info belongs to the old name
	}
	m_eventName = name;
}

float AudioEventRTS::getVolume() const
{
	// RW 0x6DB221 without the owner mute: (override or info volume or 0.5) * multiplier
	const float base = m_volumeOverride != -1.0f ? m_volumeOverride : (m_info ? m_info->volume : 0.5f);
	return base * m_volumeMultiplier;
}

float AudioEventRTS::getMinVolume() const
{
	if (m_minVolumeOverride != -1.0f)
	{
		return m_volumeMultiplier * m_minVolumeOverride;
	}
	return m_info ? m_info->minVolume * m_volumeMultiplier : 0.0f;
}

// RW 0x6DB821
void AudioEventRTS::generateFilename(const AudioEventEnv &env)
{
	if (!m_dirty || !m_info)
	{
		return;
	}
	const bool firstTime = !m_filenameGenerated;
	m_filenameGenerated = true;
	m_dirty = false;
	const AudioEventInfo &info = *m_info;
	const AudioSettings &settings = env.ini->settings;
	const bool noPrefix = (info.control & AC_NOT_USED_BY_INI) != 0;
	m_filename = noPrefix ? std::string() : folderFor(settings, info.soundType);
	if (info.soundType != AT_SoundEffect)
	{
		m_filename += info.filename;
		return;
	}
	const WeightedSoundList &list = info.sounds;
	if (info.sounds.totalWeight == 0 || list.sounds.empty())
	{
		m_filename.clear();
		return;
	}
	int which;
	if (!(info.control & AC_SEQUENTIAL) && !m_forceSequential)
	{
		if (list.sounds.size() > 1)
		{
			// the last played index lives in the info (RW +0x40): the first generation of an event starts from it and writes it back
			AudioEventInfo &mutableInfo = const_cast<AudioEventInfo &>(info);
			if (firstTime)
			{
				m_playingAudioIndex = mutableInfo.lastPlayedIndex;
			}
			do
			{
				which = AudioWeightedChoice(list.totalWeight, list, *env.random);
			} while (which == m_playingAudioIndex);
			if (firstTime)
			{
				mutableInfo.lastPlayedIndex = which;
			}
		}
		else
		{
			which = 0;
		}
		if (which == -1)
		{
			m_filename.clear();
			return;
		}
		m_playingAudioIndex = which;
	}
	else
	{
		++m_playingAudioIndex;
		which = (int)((unsigned)m_playingAudioIndex % (unsigned)list.sounds.size());
	}
	m_filename += list.sounds[(size_t)which].name;
	if (!noPrefix)
	{
		m_filename += extensionFor(settings, info.soundType);
	}
}

const std::string &AudioEventRTS::getFilename(const AudioEventEnv &env)
{
	if (m_dirty && m_info)
	{
		generateFilename(env);
	}
	return m_filename;
}

// RW 0x6DAC64 (line 413 delay, 415 per-file pitch, 418 per-file volume shift of AudioEventRTS.cpp)
void AudioEventRTS::rollDelayAndPerFile(const AudioEventEnv &env)
{
	if (!m_info)
	{
		return;
	}
	const AudioEventInfo &info = *m_info;
	GameLogicRandom &r = *env.random;
	m_delay = r.getValueReal((float)info.delay[0], (float)info.delay[1], "AudioEventRTS.cpp", 413);
	m_perFilePitch = r.getValueReal(info.perFilePitchShift[0] * 0.01f + 1.0f, info.perFilePitchShift[1] * 0.01f + 1.0f, "AudioEventRTS.cpp", 415);
	m_perFileVolumeShift = r.getValueReal(info.perFileVolumeShift + 1.0f, 1.0f, "AudioEventRTS.cpp", 418);
	if (m_regenerate)
	{
		m_dirty = true;
		m_regenerate = false;
	}
}

// RW 0x6DBA01 (lines 579 / 580)
void AudioEventRTS::generatePlayInfo(const AudioEventEnv &env)
{
	if (!m_info)
	{
		return;
	}
	const AudioEventInfo &info = *m_info;
	GameLogicRandom &r = *env.random;
	m_pitchShift = r.getValueReal(info.pitchShift[0] * 0.01f + 1.0f, info.pitchShift[1] * 0.01f + 1.0f, "AudioEventRTS.cpp", 579);
	m_volumeShift = r.getValueReal(info.volumeShift + 1.0f, 1.0f, "AudioEventRTS.cpp", 580);
	const bool noPrefix = (info.control & AC_NOT_USED_BY_INI) != 0;
	const AudioSettings &settings = env.ini->settings;
	if (info.soundType == AT_SoundEffect)
	{
		m_portion = PP_Attack;
		auto build = [&](const WeightedSoundList &list, std::string &out) {
			const int idx = AudioWeightedChoice(list.totalWeight, list, r);
			if (idx < 0)
			{
				out.clear();
				return false;
			}
			out = noPrefix ? std::string() : folderFor(settings, info.soundType);
			out += list.sounds[(size_t)idx].name;
			if (!noPrefix)
			{
				out += extensionFor(settings, info.soundType);
			}
			return true;
		};
		if (!build(info.attack, m_attackName))
		{
			if (!info.sounds.sounds.empty())
			{
				m_portion = PP_Sound;
			}
			else
			{
				m_portion = info.decay.sounds.empty() ? PP_Done : PP_Decay;
			}
		}
		build(info.decay, m_decayName);
	}
	else
	{
		m_portion = PP_Sound;
	}
}

void AudioEventRTS::regenerateForLoop(const AudioEventEnv &env)
{
	const int portion = m_portion;
	m_regenerate = true;
	generatePlayInfo(env);
	rollDelayAndPerFile(env);
	m_portion = portion;
}

void AudioEventRTS::advanceNextPlayPortion()
{
	switch (m_portion)
	{
	case PP_Attack:
		m_portion = PP_Sound;
		break;
	case PP_Sound:
		if (hasMoreLoops())
		{
			break;
		}
		m_portion = m_decayName.empty() ? PP_Done : PP_Decay;
		break;
	case PP_Decay:
		m_portion = PP_Done;
		break;
	default:
		break;
	}
}

void AudioEventRTS::decreaseLoopCount()
{
	if (m_loopCount == 1)
	{
		m_loopCount = -1;
	}
	else if (m_loopCount > 1)
	{
		--m_loopCount;
	}
}

bool AudioEventRTS::hasMoreLoops() const
{
	if (m_finished)
	{
		return false;
	}
	if (!m_info)
	{
		return true;
	}
	const int t = m_info->soundType;
	return t == AT_Music || t == AT_AmbientStream || (m_info->control & AC_LOOP) != 0;
}

void AudioEventRTS::setPosition(const Coord3D &pos)
{
	if (m_ownerType != OT_Positional && m_ownerType != OT_Invalid)
	{
		return;
	}
	m_position = pos;
	m_ownerType = OT_Positional;
	m_hasPosition = true;
}

void AudioEventRTS::setObjectID(std::uint32_t id)
{
	if (m_ownerType != OT_Object && m_ownerType != OT_Invalid)
	{
		return;
	}
	m_ownerId = id;
	m_ownerType = OT_Object;
}

void AudioEventRTS::setDrawableID(std::uint32_t id)
{
	if (m_ownerType != OT_Drawable && m_ownerType != OT_Invalid)
	{
		return;
	}
	m_ownerId = id;
	m_ownerType = OT_Drawable;
}

// RW 0x6DAD89 (and the first half of getSoundClass)
bool AudioEventRTS::isPositionalAudio() const
{
	if (m_info)
	{
		const int t = m_info->soundType;
		if (t == AT_SoundEffect)
		{
			if (!(m_info->type & ST_WORLD))
			{
				return false;
			}
		}
		else if (t != AT_AmbientStream)
		{
			return false;
		}
	}
	switch (m_ownerType)
	{
	case OT_Positional:
		return true;
	case OT_Drawable:
	case OT_Object:
	case OT_LivingWorld3:
	case OT_LivingWorld4:
	case OT_LivingWorld5:
		return m_ownerId != 0;
	default:
		return false;
	}
}

// RW 0x6DB31E: resolves the owner's position and caches it (+0x3C, flag +0x48); a vanished owner keeps the cached position.
bool AudioEventRTS::getCurrentPosition(const AudioEventEnv &env, Coord3D *pos) const
{
	AudioEventRTS *self = const_cast<AudioEventRTS *>(this);
	Coord3D found;
	bool resolved = false;
	if (env.owners)
	{
		if (m_ownerType == OT_Drawable)
		{
			resolved = env.owners->drawablePosition(m_ownerId, &found);
		}
		else if (m_ownerType == OT_Object)
		{
			resolved = env.owners->objectPosition(m_ownerId, &found);
		}
	}
	if (resolved)
	{
		self->m_position = found;
		self->m_hasPosition = true;
	}
	*pos = m_hasPosition ? m_position : Coord3D();
	return m_hasPosition;
}

bool AudioEventRTS::isDead(const AudioEventEnv &env) const
{
	Coord3D p;
	switch (m_ownerType)
	{
	case OT_Drawable:
		return !(env.owners && env.owners->drawablePosition(m_ownerId, &p));
	case OT_Object:
		return !(env.owners && env.owners->objectPosition(m_ownerId, &p));
	default:
		return false; // Living World owners are not resolved here (S-246): never dead
	}
}

unsigned AudioEventRTS::getSoundClass() const
{
	if (!m_info)
	{
		return 0;
	}
	switch (m_info->soundType)
	{
	case AT_Music:
		return SC_MUSIC;
	case AT_Streaming:
		return SC_STREAMING;
	case AT_SoundEffect:
		return isPositionalAudio() ? SC_WORLD : SC_UI;
	case AT_AmbientStream:
		return SC_AMBIENT;
	case AT_StreamedSound:
		return SC_UI;
	default:
		return 0;
	}
}

std::vector<std::string> AudioEventFiles(const AudioSettings &settings, const AudioEventInfo &info)
{
	std::vector<std::string> files;
	if ((info.type & (ST_DEFAULT | ST_FAKE)) || info.soundType == AT_Multisound)
	{
		return files;
	}
	const bool noPrefix = (info.control & AC_NOT_USED_BY_INI) != 0;
	const std::string folder = noPrefix ? std::string() : folderFor(settings, info.soundType);
	if (info.soundType != AT_SoundEffect)
	{
		if (!info.filename.empty())
		{
			files.push_back(folder + info.filename);
		}
		return files;
	}
	for (const WeightedSoundList *list : { &info.sounds, &info.attack, &info.decay })
	{
		for (const WeightedSound &s : list->sounds)
		{
			files.push_back(folder + s.name + (noPrefix ? std::string() : extensionFor(settings, info.soundType)));
		}
	}
	return files;
}
