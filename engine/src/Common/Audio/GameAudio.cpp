// OpenBFME. GPL-3.0. See GameAudio.h for the sources.

#include "Common/Audio/GameAudio.h"

#include "Common/AsciiString.h"
#include "Common/Audio/MilesMix.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cmath>

namespace
{
const float kTick = (float)AudioManager::kTickIntervalNumerator / (float)AudioManager::kTickIntervalDenominator; // RW +0x90

float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float length3(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// RW 0x5D90C3: the submix slider of an event.
int submixSliderOf(const AudioEventInfo &info)
{
	if (info.submixSlider != SLIDER_NONE_SET)
	{
		return info.submixSlider;
	}
	if (info.type & ST_VOICE)
	{
		return SLIDER_VOICE;
	}
	switch (info.soundType)
	{
	case AT_Music:
		return SLIDER_MUSIC;
	case AT_Streaming:
		return SLIDER_VOICE;
	case AT_AmbientStream:
		return SLIDER_AMBIENT;
	default:
		return SLIDER_SOUNDFX;
	}
}

bool isStreamType(int soundType)
{
	return soundType == AT_Music || soundType == AT_Streaming || soundType == AT_AmbientStream;
}
} // namespace

AudioManager::AudioManager(AudioIniState &ini, AudioAssetCache &cache, IAudioDevice &device, RandomAlgorithm algorithm, std::uint32_t seed)
	: m_ini(ini), m_cache(cache), m_device(device), m_random(algorithm)
{
	m_random.seedRandom(seed);
	m_env.ini = &m_ini;
	m_env.random = &m_random;
	const AudioSettings &s = m_ini.settings;
	m_systemVolume[SLIDER_SOUNDFX] = s.defaultSoundVolume;
	m_systemVolume[SLIDER_VOICE] = s.defaultVoiceVolume;
	m_systemVolume[SLIDER_MUSIC] = s.defaultMusicVolume;
	m_systemVolume[SLIDER_AMBIENT] = s.defaultAmbientVolume;
	m_systemVolume[SLIDER_MOVIE] = s.defaultMovieVolume;
	m_systemVolume[SLIDER_NONE] = 1.0f;
	for (int i = 0; i < 6; ++i)
	{
		m_scriptVolume[i] = 1.0f;
		m_sliderScriptFactor[i] = 1.0f;
		m_duck[i] = 1.0f;
	}
}

AudioManager::~AudioManager()
{
	enableEventLog(false); // AUDIO-3 r2: give back this manager's count of the log gate
	for (auto *list : { &m_playing2D, &m_playing3D, &m_playingStreams, &m_fading })
	{
		for (auto &p : *list)
		{
			if (p->voice)
			{
				m_device.stopVoice(p->voice);
			}
		}
	}
}

std::vector<std::string> AudioManager::unverified()
{
	return {
		"S-240: AudioManager request / limit / priority / interrupt / loop logic is ported from ZH MilesAudioManager.cpp with the RotWK differences read so far (PlayPercent at play time, looping events re-queued, "
		"linear distance falloff, submix sliders); the RW routines for killLowestPriority / limits / stream pools and the first special handle value are not individually verified",
		"S-241: AudioSettings constructor defaults of fields an INI omits are not read from the binary",
		"S-242: the processing step is 33.33 ms off a caller clock; the exact rate retail calls the manager update at is not decoded",
		"S-243: volume model: view state defaults, VolumeSliderMultiplier ducking ramp, owner dependent mute (RW 0x6DB221) are inferred from the INI documentation",
		"S-244: the IMA ADPCM decoder uses the reference step arithmetic (inferred from Miles' encoder and the retail block predictors) and the MP3 decoder is not Miles' mssmp3.asi (within 2 LSB peak of mpg123 / ffmpeg)",
		"S-245: Miles specifics (reverb / EAX, occlusion low pass) are not modelled; Godot's MP3 decoder plays the streams",
		"S-246: Living World owners (OwnerType 3-5), the music script system (AR_PushMusic / AR_PopMusic ...) are not ported",
		"S-247: the Eva announcer implements the rules EA documents in Eva.ini; the binary's queue code is not decoded",
		"S-248: LargeGroupAudio, LivingWorldSound and CrowdResponse: the LargeGroupAudio INI and its members (lane AUDIO-4: the LargeGroupAudioUpdate modules' calls and the maps' qualification) are ported; the Sound blocks' per-cell runtime is the INI's documented rules (LiveGameAudio), the other two blocks are recording stubs",
		"S-249: the decoded sound cache budget (a multiple of AudioFootprintInBytes) is inferred; retail caches compressed bytes",
		"S-1930: the stereo image is Miles' Fast 2D provider (a stereo Windows speaker setup); RotWK's Dolby Surround provider (headphone / surround speaker setups on High audio LOD) and its FB pan filter are not modelled",
		"S-1931: a panned stereo voice mixes its left channel into the right in the Godot device",
	};
}

// ---------------------------------------------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------------------------------------------
void AudioManager::setOn(bool on, unsigned affect)
{
	if (affect & AudioAffect_Music)
	{
		m_musicOn = on;
	}
	if (affect & AudioAffect_Sound)
	{
		m_soundOn = on;
	}
	if (affect & AudioAffect_Sound3D)
	{
		m_sound3DOn = on;
	}
	if (affect & AudioAffect_Speech)
	{
		m_speechOn = on;
	}
	if (affect & AudioAffect_AmbientStream)
	{
		m_ambientOn = on;
	}
}

bool AudioManager::isOn(unsigned affect) const
{
	if (affect & AudioAffect_Music)
	{
		return m_musicOn;
	}
	if (affect & AudioAffect_Sound)
	{
		return m_soundOn;
	}
	if (affect & AudioAffect_Sound3D)
	{
		return m_sound3DOn;
	}
	if (affect & AudioAffect_AmbientStream)
	{
		return m_ambientOn;
	}
	return m_speechOn;
}

void AudioManager::setVolume(float volume, unsigned affect)
{
	float *target = (affect & AudioAffect_SystemSetting) ? m_systemVolume : m_scriptVolume;
	if (affect & AudioAffect_Music)
	{
		target[SLIDER_MUSIC] = volume;
	}
	if (affect & (AudioAffect_Sound | AudioAffect_Sound3D))
	{
		target[SLIDER_SOUNDFX] = volume;
	}
	if (affect & AudioAffect_Speech)
	{
		target[SLIDER_VOICE] = volume;
	}
	if (affect & AudioAffect_AmbientStream)
	{
		target[SLIDER_AMBIENT] = volume;
	}
}

float AudioManager::getVolume(unsigned affect) const
{
	int slider = SLIDER_SOUNDFX;
	if (affect & AudioAffect_Music)
	{
		slider = SLIDER_MUSIC;
	}
	else if (affect & AudioAffect_Speech)
	{
		slider = SLIDER_VOICE;
	}
	else if (affect & AudioAffect_AmbientStream)
	{
		slider = SLIDER_AMBIENT;
	}
	return (affect & AudioAffect_SystemSetting) ? m_systemVolume[slider] : m_systemVolume[slider] * m_scriptVolume[slider];
}

void AudioManager::setSliderScriptFactor(int slider, float factor)
{
	if (slider >= 0 && slider < 6)
	{
		m_sliderScriptFactor[slider] = factor;
	}
}

void AudioManager::setListenerPosition(const Coord3D &position, const Coord3D &forward)
{
	m_listenerPosition = position;
	m_listenerForward = forward;
	m_device.setListener(position, forward);
}

// RW MilesAudioManager::recalculateMicrophone 0x45235B (BFME2 0x452B53, tier A; its decomp attempt is not byte-matching, this reads the
// RotWK disassembly). The microphone group is the current view's (RW +0x678 x 0x48 into AudioSettings +0x12C). With d = camera - lookAt:
//   fraction^2 |d|^2 >= max^2 -> s = max / |d|; else |d|^2 <= min^2 -> s = 1; else min^2 >= fraction^2 |d|^2 -> s = min / |d|; else s = fraction
// (the squares are the post-processed *Sq fields), microphone = camera - d s, then, when the look-at is valid (RW +0x48), its x / y are
// pulled towards the look-at by PullTowardsTerrainLookAtPointPercent (z kept). The face is the horizontal (-d.x, -d.y, 0) normalised, kept
// from the last call when d has no horizontal part (RW 0x45255B). Then the zoom volume (RW 0x45264A -> 0x451946, BFME2 0x45213E tier A):
// with e = camera - microphone and the current view's Zoom* fields, only when ZoomSoundVolumePercentageAmount > 0 (else it keeps its value):
//   |e|^2 < ZoomMin^2 -> 1; |e|^2 < ZoomMax^2 -> 1 - (|e| - ZoomMin) / (ZoomMax - ZoomMin) x amount; else 1 - amount.
// RW refreshPair 0x4516DE multiplies it into the slider volume of positional sounds (sliderFactor).
void AudioManager::updateMicrophone(const Coord3D &cameraPos, const Coord3D &lookAt, bool lookAtValid)
{
	const MicrophoneSettings &mic = m_ini.settings.microphone[std::max(0, std::min(2, m_currentView))];
	const float dx = cameraPos.x - lookAt.x, dy = cameraPos.y - lookAt.y, dz = cameraPos.z - lookAt.z;
	const float lenSq = dz * dz + dy * dy + dx * dx;
	float s;
	if (mic.preferredFractionCameraToGroundSq * lenSq >= mic.maxDistanceToCameraSq)
	{
		s = mic.maxDistanceToCamera / std::sqrt(lenSq);
	}
	else if (mic.minDistanceToCameraSq >= lenSq)
	{
		s = 1.0f;
	}
	else if (mic.minDistanceToCameraSq >= mic.preferredFractionCameraToGroundSq * lenSq)
	{
		s = mic.minDistanceToCamera / std::sqrt(lenSq);
	}
	else
	{
		s = mic.preferredFractionCameraToGround;
	}
	Coord3D mike{ cameraPos.x - dx * s, cameraPos.y - dy * s, cameraPos.z - dz * s };
	if (lookAtValid)
	{
		mike.x = (lookAt.x - mike.x) * mic.pullTowardsTerrainLookAtPointPercent + mike.x;
		mike.y = (lookAt.y - mike.y) * mic.pullTowardsTerrainLookAtPointPercent + mike.y;
	}
	Coord3D face = m_listenerForward;
	if (dx != 0.0f || dy != 0.0f)
	{
		const float len = std::sqrt(dx * dx + dy * dy);
		face = Coord3D{ -dx / len, -dy / len, 0.0f };
	}
	setListenerPosition(mike, face);
	const float amount = mic.zoomSoundVolumePercentageAmount;
	if (amount > 0.0f)
	{
		const float ex = cameraPos.x - mike.x, ey = cameraPos.y - mike.y, ez = cameraPos.z - mike.z;
		const float eSq = ex * ex + ey * ey + ez * ez;
		if (mic.zoomMinDistanceSq > eSq)
		{
			m_zoomVolume = 1.0f;
		}
		else if (mic.zoomMaxDistanceSq > eSq)
		{
			m_zoomVolume = 1.0f - (std::sqrt(eSq) - mic.zoomMinDistance) / (mic.zoomMaxDistance - mic.zoomMinDistance) * amount;
		}
		else
		{
			m_zoomVolume = 1.0f - amount;
		}
	}
}

void AudioManager::setAudioEventVolumeOverride(const std::string &eventName, float volume)
{
	if (eventName.empty())
	{
		m_adjustedVolumes.clear();
		return;
	}
	for (auto it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it)
	{
		if (it->first == eventName)
		{
			if (volume == -1.0f)
			{
				m_adjustedVolumes.erase(it);
			}
			else
			{
				it->second = volume;
			}
			return;
		}
	}
	if (volume != -1.0f)
	{
		m_adjustedVolumes.insert(m_adjustedVolumes.begin(), { eventName, volume });
	}
}

void AudioManager::setAudioEventEnabled(const std::string &eventName, bool enable)
{
	setAudioEventVolumeOverride(eventName, enable ? -1.0f : 0.0f);
}

void AudioManager::noteMissingHook(const char *name)
{
	++m_report.missingHooks[name];
}

void AudioManager::reportFailure(const AudioEventRTS &event, const std::string &file, const std::string &reason)
{
	++m_report.playFailures;
	if (m_report.errors.size() < 200)
	{
		const std::string line = event.getEventName() + ": " + file + ": " + reason;
		if (std::find(m_report.errors.begin(), m_report.errors.end(), line) == m_report.errors.end())
		{
			m_report.errors.push_back(line);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lists
// ---------------------------------------------------------------------------------------------------------------------------------
std::list<std::unique_ptr<AudioManager::PlayingAudio>> &AudioManager::listFor(VoiceKind kind)
{
	return kind == VoiceKind::Sample2D ? m_playing2D : (kind == VoiceKind::Sample3D ? m_playing3D : m_playingStreams);
}

const std::list<std::unique_ptr<AudioManager::PlayingAudio>> &AudioManager::listFor(VoiceKind kind) const
{
	return kind == VoiceKind::Sample2D ? m_playing2D : (kind == VoiceKind::Sample3D ? m_playing3D : m_playingStreams);
}

size_t AudioManager::channelLimit(VoiceKind kind) const
{
	const AudioSettings &s = m_ini.settings;
	return (size_t)std::max(0, kind == VoiceKind::Sample2D ? s.sampleCount2D : (kind == VoiceKind::Sample3D ? s.sampleCount3D : s.streamCount));
}

size_t AudioManager::playingCount(VoiceKind kind) const
{
	return listFor(kind).size();
}

std::vector<std::string> AudioManager::playingEventNames() const
{
	std::vector<std::string> names;
	for (const auto *list : { &m_playing2D, &m_playing3D, &m_playingStreams })
	{
		for (const auto &p : *list)
		{
			names.push_back(p->event->getEventName());
		}
	}
	return names;
}

std::vector<std::string> AudioManager::fadingEventNames() const
{
	std::vector<std::string> names;
	for (const auto &p : m_fading)
	{
		names.push_back(p->event->getEventName());
	}
	return names;
}

bool AudioManager::isValidAudioEvent(const std::string &eventName) const
{
	return !eventName.empty() && m_ini.infos.contains(eventName);
}

bool AudioManager::setEventPosition(AudioHandle handle, const Coord3D &position)
{
	// AUDIO-3 r2: a multisound group's handle moves every leaf (playing or queued)
	bool found = false;
	auto group = m_multisoundGroups.find(handle);
	if (group != m_multisoundGroups.end())
	{
		for (AudioHandle h : group->second)
		{
			found |= setLeafPosition(h, position);
		}
	}
	return setLeafPosition(handle, position) || found;
}

bool AudioManager::setLeafPosition(AudioHandle handle, const Coord3D &position)
{
	bool found = false;
	for (auto *list : { &m_playing2D, &m_playing3D, &m_playingStreams })
	{
		for (auto &p : *list)
		{
			if (p->event->getPlayingHandle() == handle && p->event->getOwnerType() == OT_Positional)
			{
				p->event->setPosition(position);
				found = true;
			}
		}
	}
	for (Request &r : m_requests)
	{
		if (!r.cancelled && r.type == RQ_Play && r.pending && r.pending->getPlayingHandle() == handle)
		{
			r.pending->setPosition(position);
			found = true;
		}
	}
	return found;
}

bool AudioManager::isCurrentlyPlaying(AudioHandle handle) const
{
	auto group = m_multisoundGroups.find(handle);
	if (group != m_multisoundGroups.end())
	{
		for (AudioHandle h : group->second)
		{
			if (isHandleAlive(h))
			{
				return true;
			}
		}
	}
	return isHandleAlive(handle);
}

bool AudioManager::isHandleAlive(AudioHandle handle) const
{
	for (const auto *list : { &m_playing2D, &m_playing3D, &m_playingStreams })
	{
		for (const auto &p : *list)
		{
			if (p->event->getPlayingHandle() == handle && p->status == PS_Playing)
			{
				return true;
			}
		}
	}
	for (const Request &r : m_requests)
	{
		if (!r.cancelled && r.type == RQ_Play && r.pending && r.pending->getPlayingHandle() == handle)
		{
			return true;
		}
	}
	return m_ambientMarkers.count(handle) != 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the request side (GameAudio.cpp addAudioEvent, GameSounds.cpp canPlayNow)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
thread_local std::string t_logOrigin;
}

const std::string &AudioLog::origin()
{
	return t_logOrigin;
}

namespace
{
std::atomic<int> g_logEnabled{ 0 };
}

bool AudioLog::enabled()
{
	return g_logEnabled.load(std::memory_order_relaxed) > 0;
}

void AudioLog::addEnabled(int delta)
{
	g_logEnabled.fetch_add(delta, std::memory_order_relaxed);
}

AudioLog::Scope::Scope(const char *tag) : m_previousSize(t_logOrigin.size()), m_active(enabled())
{
	if (m_active)
	{
		push(tag);
	}
}

AudioLog::Scope::Scope(const std::string &tag) : m_previousSize(t_logOrigin.size()), m_active(enabled())
{
	if (m_active)
	{
		push(tag);
	}
}

void AudioLog::Scope::push(const std::string &tag)
{
	if (tag.empty())
	{
		return;
	}
	if (!t_logOrigin.empty())
	{
		t_logOrigin += '/';
	}
	t_logOrigin += tag;
}

AudioLog::Scope::~Scope()
{
	if (m_active)
	{
		t_logOrigin.resize(m_previousSize);
	}
}

void AudioManager::enableEventLog(bool on)
{
	if (on != m_logOn)
	{
		AudioLog::addEnabled(on ? 1 : -1);
	}
	m_logOn = on;
}

void AudioManager::logLine(const AudioEventRTS &event, AudioHandle handle, bool play, const char *outcome, const char *detail, const char *suffix)
{
	if (!m_logOn)
	{
		return; // AUDIO-3 r2: the parts are only joined when the log is on
	}
	if (m_log.size() >= kEventLogCap)
	{
		++m_logDropped;
		return;
	}
	AudioLogEntry e;
	e.clockMs = m_clockMs;
	e.frame = m_logFrame;
	e.event = event.getEventName();
	e.origin = AudioLog::origin();
	e.objectId = event.getObjectID();
	e.drawableId = event.getDrawableID();
	e.positional = event.isPositionalAudio() && event.getCurrentPosition(m_env, &e.position);
	e.player = event.getPlayerIndex();
	e.handle = handle;
	e.play = play;
	e.outcome = outcome;
	if (detail)
	{
		e.outcome += detail;
	}
	if (suffix)
	{
		e.outcome += suffix;
	}
	m_log.push_back(std::move(e));
}

std::vector<AudioLogEntry> AudioManager::takeEventLog()
{
	std::vector<AudioLogEntry> out;
	out.swap(m_log);
	return out;
}

AudioHandle AudioManager::playSound(const std::string &eventName)
{
	return addAudioEvent(AudioEventRTS(eventName));
}

AudioHandle AudioManager::playSoundAt(const std::string &eventName, const Coord3D &position)
{
	return addAudioEvent(AudioEventRTS(eventName, position));
}

AudioHandle AudioManager::playSoundForObject(const std::string &eventName, std::uint32_t objectId, int owningPlayerIndex)
{
	AudioEventRTS e = AudioEventRTS::forObject(eventName, objectId);
	e.setPlayerIndex(owningPlayerIndex);
	return addAudioEvent(e);
}

AudioHandle AudioManager::playSoundForDrawable(const std::string &eventName, std::uint32_t drawableId, int owningPlayerIndex)
{
	AudioEventRTS e = AudioEventRTS::forDrawable(eventName, drawableId);
	e.setPlayerIndex(owningPlayerIndex);
	return addAudioEvent(e);
}

AudioHandle AudioManager::playSoundForPlayer(const std::string &eventName, int owningPlayerIndex)
{
	AudioEventRTS e(eventName);
	e.setPlayerIndex(owningPlayerIndex);
	return addAudioEvent(e);
}

AudioHandle AudioManager::addAudioEvent(const AudioEventRTS &event)
{
	return addInternal(event, 0);
}

AudioHandle AudioManager::addInternal(const AudioEventRTS &event, int depth)
{
	const std::string &name = event.getEventName();
	if (name.empty() || AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		return AHSV_NoSound;
	}
	std::shared_ptr<AudioEventInfo> info = m_ini.infos.find(name);
	if (!info)
	{
		// lane AUDIO-4 (QA-1 U10): RotWK's addAudioEvent returns 1 (AHSV_NoSound) for an event without info (RW 0x45CEA7 .. 0x45CEB4; the release
		// binary logs nothing). Retail data does ask for such names (QuitMenu.apt's exit confirmation plays "Gui_ShellMapSelect1", which no INI
		// defines), so the name is the data's, not a port failure: it is counted by name in unknownEventNames, apart from the play failures
		++m_report.unknownEvents;
		++m_report.unknownEventNames[name];
		logLine(event, AHSV_NoSound, false, "refused: unknown event");
		return AHSV_NoSound;
	}
	++m_report.eventsAdded;

	// Multisound: the subsounds play instead (all of them, or one when PLAY_ONE)
	if (info->soundType == AT_Multisound)
	{
		if (depth >= 8)
		{
			if (m_report.errors.size() < 200)
			{
				m_report.errors.push_back("Multisound '" + name + "' nests deeper than 8 levels");
			}
			return AHSV_Error;
		}
		if (info->control & AC_PLAY_ONE)
		{
			WeightedSoundList weights;
			for (const Subsound &s : info->subsounds)
			{
				WeightedSound w;
				w.name = s.name;
				w.weight = s.weight;
				weights.sounds.push_back(w);
			}
			weights.totalWeight = info->subsoundsTotalWeight;
			const int idx = AudioWeightedChoice(weights.totalWeight, weights, m_random);
			if (idx < 0 || !info->subsounds[(size_t)idx].info)
			{
				logLine(event, AHSV_NoSound, false, "refused: multisound pick has no info");
				return AHSV_NoSound;
			}
			AudioEventRTS sub = event;
			sub.setEventName(info->subsounds[(size_t)idx].name);
			if ((info->control & AC_LOOP) && !event.isMusicOneShot())
			{
				m_musicMultisound = name; // the looping playlist: the next track is picked when this one ends
			}
			logLine(event, 0, false, "multisound -> ", sub.getEventName().c_str());
			return addInternal(sub, depth + 1);
		}
		// AUDIO-3 r2: the group's handle is its first LIVE leaf (a refused child returns a sentinel), and a nested all-multisound's group is flattened into
		// this one (its leaves join, its own entry goes), so one handle reaches every leaf whatever the nesting
		AudioHandle first = AHSV_NoSound;
		std::vector<AudioHandle> leaves;
		for (const Subsound &s : info->subsounds)
		{
			if (!s.info)
			{
				continue;
			}
			AudioEventRTS sub = event;
			sub.setEventName(s.name);
			const AudioHandle h = addInternal(sub, depth + 1);
			if (first == AHSV_NoSound)
			{
				first = h; // the result when no child is live (a sentinel)
			}
			if (h < AHSV_FirstHandle)
			{
				continue;
			}
			leaves.push_back(h);
			auto nested = m_multisoundGroups.find(h);
			if (nested != m_multisoundGroups.end())
			{
				leaves.insert(leaves.end(), nested->second.begin(), nested->second.end());
				m_multisoundGroups.erase(nested);
			}
		}
		if (!leaves.empty())
		{
			first = leaves.front();
			if (leaves.size() > 1)
			{
				m_multisoundGroups[first] = std::vector<AudioHandle>(leaves.begin() + 1, leaves.end());
			}
		}
		logLine(event, first, false, "multisound (all)");
		return first;
	}

	bool off = false;
	switch (info->soundType)
	{
	case AT_Music:
		off = !isOn(AudioAffect_Music);
		break;
	case AT_SoundEffect:
	case AT_StreamedSound:
		off = !isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D);
		break;
	case AT_Streaming:
		off = !isOn(AudioAffect_Speech);
		break;
	case AT_AmbientStream:
		off = !isOn(AudioAffect_AmbientStream);
		break;
	}
	if (off)
	{
		logLine(event, AHSV_NoSound, false, "refused: its audio kind is off");
		return AHSV_NoSound;
	}
	if (m_disallowSpeech && info->soundType == AT_Streaming)
	{
		logLine(event, AHSV_NoSound, false, "refused: speech disallowed");
		return AHSV_NoSound;
	}

	// RW 0x452072: copy, a new handle, the delay and per-file rolls (0x6DAC64), then generatePlayInfo (0x6DBA01)
	auto copy = std::make_unique<AudioEventRTS>(event);
	copy->setAudioEventInfo(info);
	copy->setPlayingHandle(allocateNewHandle());
	copy->rollDelayAndPerFile(m_env);
	copy->generatePlayInfo(m_env);
	for (const auto &adj : m_adjustedVolumes)
	{
		if (adj.first == name)
		{
			copy->setVolume(adj.second);
			break;
		}
	}
	if (!copy->getUninterruptable() && !shouldPlayLocally(*copy))
	{
		++m_report.culledNotForLocal;
		logLine(*copy, AHSV_NotForLocal, false, "refused: not for the local player");
		return AHSV_NotForLocal;
	}
	if (copy->getVolume() < m_ini.settings.minSampleVolume)
	{
		++m_report.culledMuted;
		logLine(*copy, AHSV_Muted, false, "refused: below MinSampleVolume");
		return AHSV_Muted;
	}
	const AudioHandle handle = copy->getPlayingHandle();

	if (info->soundType == AT_AmbientStream)
	{
		logLine(*copy, handle, false, "ambient stream marker");
		AmbientMarker marker;
		marker.event = std::move(copy);
		m_ambientMarkers.emplace(handle, std::move(marker));
		return handle;
	}
	bool recheck = false;
	if (info->soundType != AT_Music && !canPlayNow(*copy))
	{
		// AUDIO-3, RotWK's SoundManager::addAudioEvent RW 0x45CC91 (ZH refuses here): an event that cannot play now is refused only when it is due within one
		// audio step (delay < 33.33, RW 0xD9F620) and does not loop (hasMoreLoops RW 0x6DAD59). A due loop rolls its delay again (RW 0x6DAC64) and, still
		// looping, skips its attack (RW 0x6DB1DE) and waits at least 34.33 ms (RW 0x6DAD2E: clamp(delay, 33.33 + 1, inf)); a delayed event (the INI's
		// "Delay = 40 40 ... the out of range checks are delayed 1 frame") is queued as it is. Either is checked again when it comes due (RW 0x461A83).
		const float tick = (float)tickIntervalMs();
		if (copy->getDelay() < tick && copy->hasMoreLoops())
		{
			copy->rollDelayAndPerFile(m_env);
			if (copy->hasMoreLoops())
			{
				copy->advanceNextPlayPortion();
				copy->setDelay(copy->getDelay() < tick + 1.0f ? tick + 1.0f : copy->getDelay());
			}
		}
		if (copy->getDelay() < tick && !copy->hasMoreLoops())
		{
			logLine(*copy, AHSV_NoSound, false, "refused: ", m_refusal);
			return AHSV_NoSound;
		}
		recheck = true;
		logLine(*copy, handle, false, "queued for a later check (", m_refusal, ")");
	}
	else
	{
		logLine(*copy, handle, false, "queued");
	}
	Request req;
	req.type = RQ_Play;
	req.pending = std::move(copy);
	req.requiresCheckForSample = recheck;
	pushRequest(std::move(req));
	return handle;
}

AudioWorldQueries AudioWorldQueries::noGamePlayerList()
{
	AudioWorldQueries q;
	q.localPlayerIndex = [] { return -1; };
	q.playerExists = [](int) { return false; };
	q.relationship = [](int, int) { return AR_NEUTRAL; };
	return q;
}

// Lane AUDIO-4 (QA-1 U21): RotWK's shouldPlayLocally, RW 0x4533A9 (called by addAudioEvent RW 0x45D01B unless the event is uninterruptable, + 0x4A, and
// by RW 0x458CCE). TARGET FACTS (caveat S-001):
//   * the info's sound type (+ 0xB0) 0 (music) or 3 (ambient stream): true; no restriction bit (type + 0x48 & 0x1E0): true; EVERYONE (0x100): true;
//   * the event's view type (+ 0x30): 0 (tactical) is the player test below; 1 (Living World) asks the Living World's army owner (RW 0x6DADCC,
//     0x6B3557, 0xDE4950 + 0x98, 0x6E1EC4: not ported, S-1462); any other value: true;
//   * tactical: ThePlayerList (RW 0xDE4928) null: true. owner = getNthPlayer(the event's player index) (RW 0x6DB566 -> 0x6A844E), local = the local
//     player (RW 0x6A8839). PLAYER (0x20) and UI (0x01) without an owner: true; any other event without an owner: false. Without a local player or
//     its default team (+ 0x30C): false. owner == local: the PLAYER bit; else the owner's relationship to the local player's default team
//     (RW 0x6ADBEB) ALLIES (2): the ALLIES bit (0x40); else (neutral or enemies): the ENEMIES bit (0x80).
// DONOR difference: ZH answers PLAYER events with owner == local before looking at ALLIES / ENEMIES and needs ENEMIES for the ENEMIES bit; RotWK
// (like BFME1 GameAudio.cpp) picks the bit by the owner's relation, and a neutral owner counts as an enemy.
// Without the player queries (no game and no shell list installed) the test cannot be made: the event plays and the missing hook is counted.
bool AudioManager::shouldPlayLocally(const AudioEventRTS &event)
{
	const AudioEventInfo *ei = event.getAudioEventInfo();
	if (!ei)
	{
		return false;
	}
	if (ei->soundType == AT_Music || ei->soundType == AT_AmbientStream)
	{
		return true;
	}
	if (!(ei->type & (ST_PLAYER | ST_ALLIES | ST_ENEMIES | ST_EVERYONE)) || (ei->type & ST_EVERYONE))
	{
		return true;
	}
	if (event.getViewType() == VIEW_LIVING_WORLD)
	{
		noteMissingHook("Living World owner filter (S-1462)");
		return true;
	}
	if (event.getViewType() != VIEW_TACTICAL)
	{
		return true;
	}
	if (!m_queries.localPlayerIndex || !m_queries.relationship || !m_queries.playerExists)
	{
		noteMissingHook("player filter (localPlayerIndex / relationship / playerExists)");
		return true;
	}
	const int owner = event.getPlayerIndex();
	const bool ownerExists = owner >= 0 && m_queries.playerExists(owner);
	if (!ownerExists)
	{
		return (ei->type & ST_PLAYER) && (ei->type & ST_UI);
	}
	const int local = m_queries.localPlayerIndex();
	if (local < 0)
	{
		return false; // no local player (or no default team: the queries answer -1)
	}
	if (owner == local)
	{
		return (ei->type & ST_PLAYER) != 0;
	}
	if (m_queries.relationship(owner, local) == AR_ALLIES)
	{
		return (ei->type & ST_ALLIES) != 0;
	}
	return (ei->type & ST_ENEMIES) != 0;
}

bool AudioManager::isInterrupting(const AudioEventRTS &event) const
{
	return (event.getAudioEventInfo()->control & AC_INTERRUPT) != 0;
}

bool AudioManager::violatesVoice(const AudioEventRTS &event) const
{
	if (event.getAudioEventInfo()->type & ST_VOICE)
	{
		return event.getObjectID() != 0 && isObjectPlayingVoice(event.getObjectID());
	}
	return false;
}

bool AudioManager::isObjectPlayingVoice(std::uint32_t objectId) const
{
	if (objectId == 0)
	{
		return false;
	}
	for (const auto *list : { &m_playing2D, &m_playing3D })
	{
		for (const auto &p : *list)
		{
			if (p->event->getObjectID() == objectId && (p->event->getAudioEventInfo()->type & ST_VOICE))
			{
				return true;
			}
		}
	}
	return false;
}

// ZH SoundManager::canPlayNow (GameSounds.cpp), with the INI documented MinVolume exception to the MaxRange cull.
bool AudioManager::canPlayNow(AudioEventRTS &event)
{
	const AudioEventInfo &info = *event.getAudioEventInfo();
	const bool positional = event.isPositionalAudio();
	if (positional && !(info.type & ST_GLOBAL) && info.priority != AP_CRITICAL)
	{
		Coord3D pos;
		if (event.getCurrentPosition(m_env, &pos))
		{
			const float dist = length3(m_listenerPosition, pos);
			if (dist >= info.maxRange && !(info.minVolume > 0.0f))
			{
				++m_report.culledDistance;
				m_refusal = "beyond MaxRange";
				return false;
			}
			// RotWK: RW 0x45CBC6 calls vslot 0x1B0, RW 0x452003: only an event of the tactical view (+ 0x30 == 0) that is SHROUDED and has a position asks
			// the local player's status at it (RW 0xB4D9A0 -> 0xB4FB20, not CLEAR: shrouded); any other view is never shrouded (review r1)
			if ((info.type & ST_SHROUDED) && event.getViewType() == VIEW_TACTICAL)
			{
				if (!m_queries.isShroudClear || !m_queries.localPlayerIndex)
				{
					noteMissingHook("shroud query (isShroudClear / localPlayerIndex)");
				}
				else if (!m_queries.isShroudClear(m_queries.localPlayerIndex(), pos))
				{
					++m_report.culledShroud;
					m_refusal = "shrouded";
					return false;
				}
			}
		}
	}
	if (violatesVoice(event))
	{
		if (isInterrupting(event))
		{
			return true;
		}
		++m_report.culledVoice;
		m_refusal = "the object is speaking";
		return false;
	}
	if (doesViolateLimit(event))
	{
		++m_report.culledLimit;
		m_refusal = "Limit";
		return false;
	}
	if (isInterrupting(event))
	{
		return true;
	}
	if (info.soundType != AT_SoundEffect && info.soundType != AT_StreamedSound)
	{
		return true; // streams use their own channel pool (the stream count), not the sample pools
	}
	const VoiceKind kind = positional ? VoiceKind::Sample3D : VoiceKind::Sample2D;
	if (listFor(kind).size() < channelLimit(kind))
	{
		return true;
	}
	if (isPlayingLowerPriority(event))
	{
		return true;
	}
	++m_report.culledNoChannel;
	m_refusal = "no free channel";
	return false;
}

bool AudioManager::doesViolateLimit(AudioEventRTS &event) const
{
	const AudioEventInfo &info = *event.getAudioEventInfo();
	const int limit = info.limit;
	if (limit == 0)
	{
		return false;
	}
	int totalCount = 0;
	int totalRequestCount = 0;
	const auto &list = listFor(event.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	for (const auto &p : list)
	{
		if (p->event->getEventName() == event.getEventName())
		{
			if (totalCount == 0)
			{
				event.setHandleToKill(p->event->getPlayingHandle()); // the oldest of this event
			}
			++totalCount;
		}
	}
	for (const Request &r : m_requests)
	{
		if (!r.cancelled && r.pending && r.pending->getEventName() == event.getEventName())
		{
			++totalRequestCount;
			++totalCount;
		}
	}
	if (info.control & AC_INTERRUPT)
	{
		if (totalRequestCount < limit)
		{
			// an interrupting event is always added; when the limit is reached the kill handle removes the oldest playing instance
			if (totalCount - totalRequestCount + totalRequestCount < limit)
			{
				event.setHandleToKill(0);
			}
			return false;
		}
	}
	if (totalCount < limit)
	{
		event.setHandleToKill(0);
		return false;
	}
	return true;
}

bool AudioManager::isPlayingLowerPriority(const AudioEventRTS &event) const
{
	const int priority = event.getAudioEventInfo()->priority;
	if (priority == AP_LOWEST)
	{
		return false;
	}
	const auto &list = listFor(event.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	for (const auto &p : list)
	{
		if (p->event->getAudioEventInfo()->priority < priority)
		{
			return true;
		}
	}
	return false;
}

bool AudioManager::isPlayingAlready(const AudioEventRTS &event) const
{
	const auto &list = listFor(event.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	for (const auto &p : list)
	{
		if (p->event->getEventName() == event.getEventName())
		{
			return true;
		}
	}
	return false;
}

AudioEventRTS *AudioManager::findLowestPrioritySound(const AudioEventRTS &event)
{
	const int priority = event.getAudioEventInfo()->priority;
	if (priority == AP_LOWEST)
	{
		return nullptr;
	}
	AudioEventRTS *lowest = nullptr;
	int lowestPriority = AP_CRITICAL + 1;
	auto &list = listFor(event.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	for (auto &p : list)
	{
		const int pr = p->event->getAudioEventInfo()->priority;
		if (pr < priority && (!lowest || lowestPriority > pr))
		{
			lowest = p->event.get();
			lowestPriority = pr;
			if (pr == AP_LOWEST)
			{
				return lowest;
			}
		}
	}
	return lowest;
}

// ZH MilesAudioManager::killLowestPrioritySoundImmediately (its 2D branch erases from the 3D list: a ZH bug; here the right list)
bool AudioManager::killLowestPrioritySoundImmediately(const AudioEventRTS &event)
{
	AudioEventRTS *lowest = findLowestPrioritySound(event);
	if (!lowest)
	{
		return false;
	}
	auto &list = listFor(event.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	for (auto it = list.begin(); it != list.end(); ++it)
	{
		if ((*it)->event.get() == lowest)
		{
			releasePlaying(**it);
			list.erase(it);
			++m_report.stolenChannels;
			return true;
		}
	}
	return false;
}

void AudioManager::releasePlaying(PlayingAudio &p)
{
	if (p.voice)
	{
		m_device.stopVoice(p.voice);
		p.voice = 0;
	}
	p.status = PS_Stopped;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the per-step processing
// ---------------------------------------------------------------------------------------------------------------------------------
void AudioManager::update(double nowMs)
{
	if (!m_clockStarted)
	{
		m_clockStarted = true;
		m_clockMs = nowMs;
		return;
	}
	const double step = tickIntervalMs();
	int steps = 0;
	while (m_clockMs + step <= nowMs)
	{
		if (steps >= 8)
		{
			// a stall longer than the catch-up cap: skip ahead, count the dropped steps
			const double behind = nowMs - m_clockMs;
			m_report.droppedTicks += (std::uint64_t)(behind / step);
			m_clockMs = nowMs;
			break;
		}
		m_clockMs += step;
		++steps;
		++m_report.ticks;
		if ((m_report.ticks % 150) == 0)
		{
			// AUDIO-3: forget the multisound groups none of whose sounds is alive any more
			for (auto it = m_multisoundGroups.begin(); it != m_multisoundGroups.end();)
			{
				it = isCurrentlyPlaying(it->first) ? std::next(it) : m_multisoundGroups.erase(it);
			}
		}
		processRequestList();
		refreshDucking();
		processPlayingList();
		processFadingList();
		processAmbientMarkers();
		processMusicChain();
	}
}

bool AudioManager::shouldProcessRequestThisFrame(const Request &req) const
{
	return req.type != RQ_Play || !req.pending || req.pending->getDelay() < kTick;
}

size_t AudioManager::pendingRequests() const
{
	size_t n = 0;
	for (const Request &r : m_requests)
	{
		n += r.cancelled ? 0 : 1;
	}
	return n;
}

void AudioManager::pushRequest(Request &&req)
{
	req.deferred = m_requestPassActive;
	m_requests.push_back(std::move(req));
}

// Cancels the pending requests the predicate selects. While the request list is being processed the entries are only marked (the pass owns the
// iterators); otherwise they are erased.
template <class Pred> void AudioManager::cancelRequests(Pred pred, bool firstOnly)
{
	for (auto it = m_requests.begin(); it != m_requests.end();)
	{
		if (it->cancelled || !pred(*it))
		{
			++it;
			continue;
		}
		if (m_requestPassActive)
		{
			it->cancelled = true;
			++it;
		}
		else
		{
			it = m_requests.erase(it);
		}
		if (firstOnly)
		{
			return;
		}
	}
}

// Every request that was in the list when the tick began runs at most once; whatever the processing queues (a looping event that rolled its
// PlayPercent out, or found no channel) waits for the next tick, behind the entries that are still counting their delay down.
void AudioManager::processRequestList()
{
	m_requestPassActive = true;
	for (auto it = m_requests.begin(); it != m_requests.end();)
	{
		Request &req = *it;
		if (req.cancelled)
		{
			it = m_requests.erase(it);
			continue;
		}
		if (req.deferred)
		{
			++it;
			continue;
		}
		if (!shouldProcessRequestThisFrame(req))
		{
			req.pending->decrementDelay(kTick); // RW 0x6DA66B per step
			req.requiresCheckForSample = true;
			++it;
			continue;
		}
		// the request leaves the list before it runs: playing may add requests (re-queued loops, deferred) or cancel others (marked)
		Request local = std::move(req);
		it = m_requests.erase(it);
		processRequest(local);
	}
	for (auto it = m_requests.begin(); it != m_requests.end();)
	{
		if (it->cancelled)
		{
			it = m_requests.erase(it);
			continue;
		}
		it->deferred = false;
		++it;
	}
	m_requestPassActive = false;
}

void AudioManager::processRequest(Request &req)
{
	switch (req.type)
	{
	case RQ_Play:
	{
		if (!req.pending)
		{
			return;
		}
		if (req.requiresCheckForSample)
		{
			const AudioEventInfo *info = req.pending->getAudioEventInfo();
			if (info && info->soundType == AT_SoundEffect && !canPlayNow(*req.pending))
			{
				// AUDIO-3, RW 0x461A83: a loop that still cannot play rolls its delay again (RW 0x6DAC64) and stays queued; anything else is dropped. The port
				// also drops a loop whose owner is gone (RW's canPlayNow refuses an event without a position, RW 0x45CB92, so such a loop would wait forever)
				Coord3D ownerPos;
				const bool ownerGone = (req.pending->getOwnerType() == OT_Drawable || req.pending->getOwnerType() == OT_Object) && !req.pending->getCurrentPosition(m_env, &ownerPos);
				if (req.pending->hasMoreLoops() && !ownerGone)
				{
					req.pending->rollDelayAndPerFile(m_env);
					if (req.pending->hasMoreLoops())
					{
						Request again;
						again.type = RQ_Play;
						again.pending = std::move(req.pending);
						again.requiresCheckForSample = true;
						pushRequest(std::move(again));
						return;
					}
				}
				logLine(*req.pending, req.pending->getPlayingHandle(), true, "refused after its delay: ", m_refusal);
				return;
			}
		}
		playAudioEvent(std::move(req.pending));
		break;
	}
	case RQ_Stop:
		stopAudioEvent(req.handle);
		break;
	case RQ_Pause:
		break;
	}
}

VoiceParams AudioManager::voiceParamsFor(AudioEventRTS &event, VoiceKind kind)
{
	VoiceParams p;
	p.volume = clamp01(getEffectiveVolume(event));
	p.pitch = event.getPitchShift();
	// lane AUDIO-5: the channel gains Miles gives the voice (MilesMix.h): a 3D sample through the Fast 2D provider, anything else at the
	// default pan
	MilesChannelGains g = milesSampleGains(p.volume);
	if (kind == VoiceKind::Sample3D)
	{
		Coord3D pos;
		if (event.getCurrentPosition(m_env, &pos))
		{
			g = milesFast2DGains(p.volume, m_listenerPosition, m_listenerForward, pos, milesMaxDistance(*event.getAudioEventInfo(), event.getMinVolume(), m_ini.settings));
		}
		else
		{
			// the owner is gone: Miles keeps the sample's last position; the port has none and plays it straight ahead (INFERENCE)
			const Coord3D ahead{ m_listenerPosition.x + m_listenerForward.x, m_listenerPosition.y + m_listenerForward.y, m_listenerPosition.z };
			g = milesFast2DGains(p.volume, m_listenerPosition, m_listenerForward, ahead, 2.0f);
		}
	}
	p.gainLeft = g.left;
	p.gainRight = g.right;
	return p;
}

// Starts the voice of the portion the event is in. Returns false when no voice could be started (reported).
bool AudioManager::startVoice(PlayingAudio &p, const std::string &file, bool firstPortion)
{
	AudioEventRTS &ev = *p.event;
	const AudioEventInfo &info = *ev.getAudioEventInfo();
	if (file.empty())
	{
		return false;
	}
	VoiceStart start;
	start.kind = p.kind;
	start.file = file;
	start.eventName = ev.getEventName();
	start.params = voiceParamsFor(ev, p.kind);
	if (p.kind == VoiceKind::Stream)
	{
		const bool chain = (info.soundType == AT_Music) && !m_musicMultisound.empty();
		start.loop = (info.soundType == AT_Music && !chain && !ev.isMusicOneShot()) || info.soundType == AT_AmbientStream || (info.soundType == AT_StreamedSound && (info.control & AC_LOOP));
		if (firstPortion && (info.control & AC_RANDOMSTART))
		{
			start.startFraction = m_random.getValueReal(0.0f, 1.0f, "GameAudio.cpp", 1);
		}
		if (firstPortion && ((info.control & AC_FADE_ON_START) || ev.getShouldFade())) // lane SCRIPT-3: a script's fade-in (RW 0x6DA774) too
		{
			start.fadeInMs = (float)m_ini.settings.timeToFadeAudio;
		}
	}
	std::string error;
	if (!m_cache.exists(file))
	{
		reportFailure(ev, file, "file not found in the mounted archives");
		return false;
	}
	p.voice = m_device.startVoice(start, &error);
	if (!p.voice)
	{
		reportFailure(ev, file, error.empty() ? "the device could not start the voice" : error);
		return false;
	}
	p.startedMs = m_clockMs;
	return true;
}

void AudioManager::playAudioEvent(std::unique_ptr<AudioEventRTS> evp)
{
	AudioEventRTS &ev = *evp;
	const AudioEventInfo *info = ev.getAudioEventInfo();
	if (!info)
	{
		return;
	}
	const bool loops = ev.hasMoreLoops();
	// PlayPercent (RW 0x461655): a sound effect plays only when a roll in [0, 1] is not above PlayPercent
	if (info->soundType == AT_SoundEffect)
	{
		const float roll = m_random.getValueReal(0.0f, 1.0f, "MilesAudioManager.cpp", 0x1B98);
		if (roll > info->playPercent)
		{
			++m_report.culledPlayPercent;
			logLine(ev, ev.getPlayingHandle(), true, loops ? "skipped: PlayPercent roll (loop requeued)" : "skipped: PlayPercent roll");
			if (loops)
			{
				Request again;
				again.type = RQ_Play;
				again.pending = std::move(evp);
				again.requiresCheckForSample = true;
				pushRequest(std::move(again));
				++m_report.requeuedLoops;
			}
			return;
		}
	}
	const bool isStream = isStreamType(info->soundType);
	VoiceKind kind = isStream ? VoiceKind::Stream : (ev.isPositionalAudio() ? VoiceKind::Sample3D : VoiceKind::Sample2D);
	if (info->soundType == AT_StreamedSound)
	{
		kind = VoiceKind::Stream; // "acts much like a 2D sound, only it is streamed in" (soundeffects.ini)
	}

	auto &list = listFor(kind);
	// an interrupting event replaces the oldest instance of itself (the kill handle set by doesViolateLimit)
	const AudioHandle handleToKill = ev.getHandleToKill();
	bool foundToReplace = false;
	if (handleToKill)
	{
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if ((*it)->event->getPlayingHandle() == handleToKill)
			{
				releasePlaying(**it);
				list.erase(it);
				foundToReplace = true;
				++m_report.interrupted;
				break;
			}
		}
	}
	if (handleToKill && !foundToReplace)
	{
		logLine(ev, ev.getPlayingHandle(), true, "dropped: the instance it interrupts is gone");
		return; // the instance to replace is gone (another request already killed it): ZH plays nothing
	}

	if (kind == VoiceKind::Stream)
	{
		if (info->soundType == AT_Music)
		{
			// a new music track replaces the playing one (fading it out when the old track asks for FADE_ON_KILL)
			for (auto it = list.begin(); it != list.end();)
			{
				if ((*it)->event->getAudioEventInfo()->soundType == AT_Music)
				{
					if ((*it)->event->getAudioEventInfo()->control & AC_FADE_ON_KILL)
					{
						(*it)->fading = true;
						(*it)->framesFaded = 0;
						m_fading.push_back(std::move(*it));
					}
					else
					{
						releasePlaying(**it);
					}
					it = list.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		else if (info->soundType == AT_Streaming && ev.getUninterruptable())
		{
			stopAudio(AudioAffect_Speech);
			setDisallowSpeech(true);
		}
		if (list.size() >= channelLimit(kind))
		{
			// the stream pool is full: the lowest priority stream that is not above the new one makes room
			auto victim = list.end();
			for (auto it = list.begin(); it != list.end(); ++it)
			{
				const int pr = (*it)->event->getAudioEventInfo()->priority;
				if (pr <= info->priority && (victim == list.end() || pr < (*victim)->event->getAudioEventInfo()->priority))
				{
					victim = it;
				}
			}
			if (victim == list.end())
			{
				++m_report.culledNoChannel;
				logLine(ev, ev.getPlayingHandle(), true, "dropped: no stream channel");
				return;
			}
			releasePlaying(**victim);
			list.erase(victim);
			++m_report.stolenChannels;
		}
	}
	else if (list.size() >= channelLimit(kind))
	{
		if (!killLowestPrioritySoundImmediately(ev))
		{
			++m_report.culledNoChannel;
			logLine(ev, ev.getPlayingHandle(), true, loops ? "dropped: no channel (loop requeued)" : "dropped: no channel");
			if (loops)
			{
				Request again; // RW 0x461786: a looping event that found no free channel is queued again
				again.type = RQ_Play;
				again.pending = std::move(evp);
				again.requiresCheckForSample = true;
				pushRequest(std::move(again));
				++m_report.requeuedLoops;
			}
			return;
		}
	}

	auto playing = std::make_unique<PlayingAudio>();
	playing->kind = kind;
	playing->event = std::move(evp);
	AudioEventRTS &pe = *playing->event;
	// the first file to play: the Attack when there is one, else the Sound (a looping event re-queued skips the attack, as the INI documents)
	std::string file;
	if (kind == VoiceKind::Stream)
	{
		file = pe.getFilename(m_env);
		pe.setNextPlayPortion(PP_Sound);
	}
	else if (pe.getNextPlayPortion() == PP_Attack && !pe.getAttackFilename().empty())
	{
		file = pe.getAttackFilename();
	}
	else
	{
		if (pe.getNextPlayPortion() == PP_Attack)
		{
			pe.setNextPlayPortion(PP_Sound);
		}
		file = pe.getFilename(m_env);
	}
	if (file.empty() && pe.getNextPlayPortion() == PP_Sound && !pe.getDecayFilename().empty() && kind != VoiceKind::Stream)
	{
		pe.setNextPlayPortion(PP_Decay);
		file = pe.getDecayFilename();
	}
	if (!startVoice(*playing, file, true))
	{
		logLine(pe, pe.getPlayingHandle(), true, "failed: ", file.c_str());
		return;
	}
	++m_report.played;
	logLine(pe, pe.getPlayingHandle(), true, "started ", file.c_str());
	if (kind == VoiceKind::Stream && info->soundType == AT_Music)
	{
		m_currentTrackEvent = pe.getEventName();
		m_currentMusicHandle = pe.getPlayingHandle();
	}
	listFor(kind).push_back(std::move(playing));
}

// A voice finished by itself: the next portion, the next loop or the end (RW 0x452E4E, startNextLoop RW 0x4594AF).
void AudioManager::completeVoice(PlayingAudio &p)
{
	if (p.voice)
	{
		m_device.stopVoice(p.voice); // the device recycles the voice (a finished one still holds its pool slot until it is released)
		p.voice = 0;
	}
	AudioEventRTS &ev = *p.event;
	if (p.requestStop)
	{
		p.status = PS_Stopped;
		return;
	}
	const AudioEventInfo &info = *ev.getAudioEventInfo();
	if (p.kind == VoiceKind::Stream)
	{
		if (info.soundType == AT_Music)
		{
			++m_trackCompletions[ev.getEventName()];
		}
		p.status = PS_Stopped;
		if (info.soundType == AT_Streaming)
		{
			setDisallowSpeech(false);
		}
		return;
	}
	switch (ev.getNextPlayPortion())
	{
	case PP_Attack:
		ev.advanceNextPlayPortion();
		if (startVoice(p, ev.getFilename(m_env), false))
		{
			return;
		}
		p.status = PS_Stopped;
		return;
	case PP_Sound:
		if (ev.hasMoreLoops())
		{
			ev.m_regenerate = true;
			ev.rollDelayAndPerFile(m_env); // a new file and delay for the next loop
			if (ev.getDelay() > kTick)
			{
				// park the event as a delayed request (ZH startNextLoop): the sound appears done but its event lives on
				Request again;
				again.type = RQ_Play;
				again.pending = std::move(p.event);
				again.requiresCheckForSample = true;
				pushRequest(std::move(again));
				p.status = PS_Stopped;
				return;
			}
			if (startVoice(p, ev.getFilename(m_env), false))
			{
				return;
			}
			p.status = PS_Stopped;
			return;
		}
		ev.advanceNextPlayPortion();
		if (ev.getNextPlayPortion() == PP_Decay && startVoice(p, ev.getDecayFilename(), false))
		{
			return;
		}
		p.status = PS_Stopped;
		return;
	default:
		p.status = PS_Stopped;
		return;
	}
}

void AudioManager::processPlayingList()
{
	for (VoiceKind kind : { VoiceKind::Sample2D, VoiceKind::Sample3D, VoiceKind::Stream })
	{
		auto &list = listFor(kind);
		for (auto it = list.begin(); it != list.end();)
		{
			PlayingAudio &p = **it;
			if (p.status == PS_Playing && p.voice && !m_device.isVoicePlaying(p.voice))
			{
				completeVoice(p);
			}
			if (p.status == PS_Stopped)
			{
				it = list.erase(it);
				continue;
			}
			if (!p.voice)
			{
				++it;
				continue;
			}
			AudioEventRTS &ev = *p.event;
			if (kind == VoiceKind::Sample3D)
			{
				Coord3D pos;
				if (!ev.getCurrentPosition(m_env, &pos))
				{
					releasePlaying(p);
					it = list.erase(it);
					continue;
				}
				if (ev.isDead(m_env))
				{
					// stopAudioEvent may erase this node (or keep it playing its Decay): step first
					auto next = std::next(it);
					stopAudioEvent(ev.getPlayingHandle());
					it = next;
					continue;
				}
				const AudioEventInfo &info = *ev.getAudioEventInfo();
				const float consider = getEffectiveVolume(ev);
				const bool playAnyway = (info.type & ST_GLOBAL) || info.priority == AP_CRITICAL;
				if (consider < m_ini.settings.minSampleVolume && !playAnyway)
				{
					releasePlaying(p); // inaudible: the voice is released (the world adds it again when it becomes audible)
					it = list.erase(it);
					continue;
				}
			}
			m_device.updateVoice(p.voice, voiceParamsFor(ev, kind));
			++it;
		}
	}
}

void AudioManager::processFadingList()
{
	const int fadeTicks = std::max(1, (int)std::ceil((double)m_ini.settings.timeToFadeAudio / tickIntervalMs()));
	for (auto it = m_fading.begin(); it != m_fading.end();)
	{
		PlayingAudio &p = **it;
		if (p.framesFaded >= fadeTicks || !p.voice)
		{
			releasePlaying(p);
			it = m_fading.erase(it);
			continue;
		}
		++p.framesFaded;
		VoiceParams params = voiceParamsFor(*p.event, p.kind);
		params.volume *= 1.0f - (float)p.framesFaded / (float)fadeTicks;
		m_device.updateVoice(p.voice, params);
		++it;
	}
}

// The ambient system (soundeffects.ini): requests make markers; the loudest streams (at most MaximumAmbientStreams of the audio LOD and the
// stream pool) play; a quieter marker takes over only when it is AmbientStreamHysteresisVolume louder than the one it replaces.
void AudioManager::processAmbientMarkers()
{
	if (m_ambientMarkers.empty())
	{
		return;
	}
	struct Candidate
	{
		AudioHandle handle;
		float volume;
	};
	std::vector<Candidate> cands;
	for (auto &kv : m_ambientMarkers)
	{
		cands.push_back({ kv.first, getEffectiveVolume(*kv.second.event) });
	}
	std::stable_sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) { return a.volume > b.volume; });
	const AudioLODSettings &lod = m_ini.audioLOD[1];
	size_t maxStreams = lod.defined ? (size_t)std::max(0, lod.maximumAmbientStreams) : channelLimit(VoiceKind::Stream);
	maxStreams = std::min(maxStreams, channelLimit(VoiceKind::Stream));
	const float hysteresis = (float)m_ini.settings.ambientStreamHysteresisVolume * 0.01f;

	// the wanted set: the top maxStreams, but a marker that is playing keeps its place unless beaten by more than the hysteresis
	std::vector<AudioHandle> want;
	for (const Candidate &c : cands)
	{
		if (want.size() >= maxStreams)
		{
			break;
		}
		if (c.volume > 0.0f)
		{
			want.push_back(c.handle);
		}
	}
	for (auto &kv : m_ambientMarkers)
	{
		AmbientMarker &m = kv.second;
		if (m.playing && std::find(want.begin(), want.end(), kv.first) == want.end())
		{
			// playing but not in the top set: keep it while no waiting marker beats it by the hysteresis
			float mine = 0.0f;
			for (const Candidate &c : cands)
			{
				if (c.handle == kv.first)
				{
					mine = c.volume;
				}
			}
			bool beaten = false;
			for (AudioHandle w : want)
			{
				if (m_ambientMarkers[w].playing == 0)
				{
					for (const Candidate &c : cands)
					{
						if (c.handle == w && c.volume > mine + hysteresis)
						{
							beaten = true;
						}
					}
				}
			}
			if (!beaten && want.size() > 0 && mine > 0.0f)
			{
				// stays: drop the quietest waiting marker from the wanted set instead
				for (auto wit = want.rbegin(); wit != want.rend(); ++wit)
				{
					if (m_ambientMarkers[*wit].playing == 0)
					{
						want.erase(std::next(wit).base());
						break;
					}
				}
				want.push_back(kv.first);
			}
		}
	}
	// stop the streams that are no longer wanted
	for (auto it = m_playingStreams.begin(); it != m_playingStreams.end();)
	{
		PlayingAudio &p = **it;
		if (p.event->getAudioEventInfo()->soundType == AT_AmbientStream)
		{
			const AudioHandle h = p.event->getPlayingHandle();
			if (std::find(want.begin(), want.end(), h) == want.end() || m_ambientMarkers.count(h) == 0)
			{
				releasePlaying(p);
				if (m_ambientMarkers.count(h))
				{
					m_ambientMarkers[h].playing = 0;
				}
				it = m_playingStreams.erase(it);
				continue;
			}
		}
		++it;
	}
	// start the wanted streams that are not playing
	for (AudioHandle h : want)
	{
		AmbientMarker &m = m_ambientMarkers[h];
		if (m.playing || m_playingStreams.size() >= channelLimit(VoiceKind::Stream))
		{
			continue;
		}
		auto playing = std::make_unique<PlayingAudio>();
		playing->kind = VoiceKind::Stream;
		playing->event = std::make_unique<AudioEventRTS>(*m.event);
		playing->event->setNextPlayPortion(PP_Sound);
		if (startVoice(*playing, playing->event->getFilename(m_env), true))
		{
			++m_report.played;
			m.playing = h;
			m_playingStreams.push_back(std::move(playing));
		}
		else
		{
			m_ambientMarkers.erase(h); // a marker whose file cannot play is dropped (reported)
			break;
		}
	}
}

void AudioManager::stopAudioEvent(AudioHandle handle)
{
	// an ambient marker
	auto marker = m_ambientMarkers.find(handle);
	if (marker != m_ambientMarkers.end())
	{
		m_ambientMarkers.erase(marker);
	}
	// pending requests of the handle
	cancelRequests([&](const Request &r) { return r.type == RQ_Play && r.pending && r.pending->getPlayingHandle() == handle; });
	for (VoiceKind kind : { VoiceKind::Sample2D, VoiceKind::Sample3D, VoiceKind::Stream })
	{
		auto &list = listFor(kind);
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			PlayingAudio &p = **it;
			if (p.event->getPlayingHandle() != handle)
			{
				continue;
			}
			const AudioEventInfo &info = *p.event->getAudioEventInfo();
			if (kind == VoiceKind::Stream && (info.control & AC_FADE_ON_KILL))
			{
				p.fading = true;
				p.framesFaded = 0;
				m_fading.push_back(std::move(*it));
				list.erase(it);
				return;
			}
			p.event->stopLooping();
			// a stopped sound plays its Decay (INI: "a list of wavs to play when killing the sound")
			if (kind != VoiceKind::Stream && p.event->getNextPlayPortion() != PP_Decay && p.event->getNextPlayPortion() != PP_Done && !p.event->getDecayFilename().empty())
			{
				if (p.voice)
				{
					m_device.stopVoice(p.voice);
					p.voice = 0;
				}
				p.event->setNextPlayPortion(PP_Decay);
				if (startVoice(p, p.event->getDecayFilename(), false))
				{
					return;
				}
			}
			releasePlaying(p);
			list.erase(it);
			return;
		}
	}
}

void AudioManager::removeAudioEvent(AudioHandle handle)
{
	if (handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade)
	{
		stopMusic(handle == AHSV_StopTheMusicFade);
		return;
	}
	if (handle < AHSV_FirstHandle)
	{
		return;
	}
	stopAudioEvent(handle);
	auto group = m_multisoundGroups.find(handle);
	if (group != m_multisoundGroups.end())
	{
		const std::vector<AudioHandle> members = std::move(group->second);
		m_multisoundGroups.erase(group);
		for (AudioHandle h : members)
		{
			stopAudioEvent(h);
		}
	}
}

void AudioManager::killAudioEventImmediately(AudioHandle handle)
{
	auto group = m_multisoundGroups.find(handle);
	if (group != m_multisoundGroups.end())
	{
		const std::vector<AudioHandle> members = std::move(group->second);
		m_multisoundGroups.erase(group);
		for (AudioHandle h : members)
		{
			killAudioEventImmediately(h);
		}
	}
	bool found = false;
	cancelRequests(
		[&](const Request &r) {
			if (!found && r.type == RQ_Play && r.pending && r.pending->getPlayingHandle() == handle)
			{
				found = true;
				return true;
			}
			return false;
		},
		true);
	if (found)
	{
		return;
	}
	m_ambientMarkers.erase(handle);
	for (VoiceKind kind : { VoiceKind::Sample3D, VoiceKind::Sample2D, VoiceKind::Stream })
	{
		auto &list = listFor(kind);
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if ((*it)->event->getPlayingHandle() == handle)
			{
				releasePlaying(**it);
				list.erase(it);
				return;
			}
		}
	}
}

// The AudioAffect bit that governs an event (the lists it plays in, or would play in when it is still pending).
unsigned AudioManager::affectOf(const AudioEventRTS &event)
{
	const AudioEventInfo *info = event.getAudioEventInfo();
	if (!info)
	{
		return 0;
	}
	switch (info->soundType)
	{
	case AT_Music:
		return AudioAffect_Music;
	case AT_Streaming:
		return AudioAffect_Speech;
	case AT_AmbientStream:
		return AudioAffect_AmbientStream;
	case AT_StreamedSound:
		return AudioAffect_Sound;
	default:
		return event.isPositionalAudio() ? AudioAffect_Sound3D : AudioAffect_Sound;
	}
}

// Stopping silences what is playing AND what is about to play: pending requests (a delayed sound, a re-queued loop), ambient markers, fading
// streams and the music playlist chain all go, otherwise the next tick would start them again.
void AudioManager::stopAudio(unsigned affect)
{
	auto stopIf = [&](std::list<std::unique_ptr<PlayingAudio>> &list, auto pred) {
		for (auto it = list.begin(); it != list.end();)
		{
			if (pred(**it))
			{
				releasePlaying(**it);
				it = list.erase(it);
			}
			else
			{
				++it;
			}
		}
	};
	stopIf(m_playing2D, [&](PlayingAudio &) { return (affect & AudioAffect_Sound) != 0; });
	stopIf(m_playing3D, [&](PlayingAudio &) { return (affect & AudioAffect_Sound3D) != 0; });
	stopIf(m_playingStreams, [&](PlayingAudio &p) { return (affectOf(*p.event) & affect) != 0; });
	stopIf(m_fading, [&](PlayingAudio &p) { return (affectOf(*p.event) & affect) != 0; });
	cancelRequests([&](const Request &r) { return r.type == RQ_Play && r.pending && (affectOf(*r.pending) & affect) != 0; });
	if (affect & AudioAffect_AmbientStream)
	{
		m_ambientMarkers.clear();
	}
	if (affect & AudioAffect_Music)
	{
		m_musicMultisound.clear();
		m_currentTrackEvent.clear();
		m_currentMusicHandle = 0;
	}
	if (affect & AudioAffect_Speech)
	{
		m_disallowSpeech = false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// music
// ---------------------------------------------------------------------------------------------------------------------------------
AudioHandle AudioManager::playMusic(const std::string &eventName, bool fadeIn)
{
	m_musicMultisound.clear();
	AudioEventRTS e(eventName);
	e.setShouldFade(fadeIn);
	return addAudioEvent(e);
}

AudioHandle AudioManager::playMusicOnce(const std::string &eventName)
{
	m_musicMultisound.clear();
	AudioEventRTS e(eventName);
	e.setMusicOneShot(true);
	return addAudioEvent(e);
}

void AudioManager::stopMusic(bool fade)
{
	m_musicMultisound.clear();
	for (auto it = m_playingStreams.begin(); it != m_playingStreams.end();)
	{
		if ((*it)->event->getAudioEventInfo()->soundType == AT_Music)
		{
			if (fade)
			{
				(*it)->fading = true;
				(*it)->framesFaded = 0;
				m_fading.push_back(std::move(*it));
			}
			else
			{
				releasePlaying(**it);
			}
			it = m_playingStreams.erase(it);
		}
		else
		{
			++it;
		}
	}
	cancelRequests([](const Request &r) { return r.type == RQ_Play && r.pending && r.pending->getAudioEventInfo() && r.pending->getAudioEventInfo()->soundType == AT_Music; });
	m_currentTrackEvent.clear();
	m_currentMusicHandle = 0;
}

bool AudioManager::isMusicPlaying() const
{
	for (const auto &p : m_playingStreams)
	{
		if (p->event->getAudioEventInfo()->soundType == AT_Music && p->status == PS_Playing)
		{
			return true;
		}
	}
	for (const Request &r : m_requests)
	{
		if (!r.cancelled && r.pending && r.pending->getAudioEventInfo() && r.pending->getAudioEventInfo()->soundType == AT_Music)
		{
			return true;
		}
	}
	return false;
}

std::string AudioManager::getMusicTrackName() const
{
	return isMusicPlaying() ? m_currentTrackEvent : std::string();
}

int AudioManager::musicTrackCompletions(const std::string &trackName) const
{
	const auto it = m_trackCompletions.find(trackName);
	return it == m_trackCompletions.end() ? 0 : it->second;
}

// A looping PLAY_ONE multisound plays one track at a time: when the track ends the next one is picked.
void AudioManager::processMusicChain()
{
	if (m_musicMultisound.empty() || isMusicPlaying())
	{
		return;
	}
	const std::string name = m_musicMultisound;
	addAudioEvent(AudioEventRTS(name));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// volume (RW getEffectiveVolume 0x4591D4)
// ---------------------------------------------------------------------------------------------------------------------------------
// While an event with VolumeSliderMultiplier entries plays, every OTHER event on the named slider is quieter by the multiplier; an event with
// entries is immune (soundeffects.ini). The slider NONE is never adjusted.
void AudioManager::refreshDucking()
{
	float duck[6] = { 1, 1, 1, 1, 1, 1 };
	for (const auto *list : { &m_playing2D, &m_playing3D, &m_playingStreams })
	{
		for (const auto &p : *list)
		{
			for (const SliderMultiplier &m : p->event->getAudioEventInfo()->volumeSliderMultipliers)
			{
				if (m.slider >= 0 && m.slider < SLIDER_NONE)
				{
					duck[m.slider] *= m.multiplier;
				}
			}
		}
	}
	for (int i = 0; i < 6; ++i)
	{
		m_duck[i] = duck[i];
	}
}

float AudioManager::sliderFactor(const AudioEventInfo &info, bool positionalOrAmbient) const
{
	const int slider = submixSliderOf(info);
	if (slider == SLIDER_NONE)
	{
		return 1.0f; // RW 0x4516F5: the slider NONE is all 1.0
	}
	float v = m_systemVolume[slider] * m_scriptVolume[slider];
	if (positionalOrAmbient)
	{
		v *= m_zoomVolume;
	}
	if (info.volumeSliderMultipliers.empty())
	{
		v *= m_sliderScriptFactor[slider] * m_duck[slider]; // RW variant 2/3: events without multiplier entries are subject to them
	}
	return clamp01(v);
}

float AudioManager::getEffectiveVolume(AudioEventRTS &event)
{
	const AudioEventInfo *info = event.getAudioEventInfo();
	if (!info)
	{
		return 0.0f;
	}
	// the view gate (RW 0x4591DF)
	const int view = event.getViewType();
	if ((view == VIEW_TACTICAL && m_currentView != VIEW_TACTICAL) || (view == VIEW_LIVING_WORLD && m_currentView != VIEW_LIVING_WORLD))
	{
		return 0.0f;
	}
	float volume = event.getVolume() * event.getVolumeShift();
	const bool positional = event.isPositionalAudio();
	if (positional)
	{
		float minVolume = event.getMinVolume();
		Coord3D pos;
		const bool haveZoomFade = m_queries.zoomFadeFactor && m_queries.isOnScreen && m_queries.offscreenDistanceSq;
		float distanceFactor = 0.0f;
		if (event.getCurrentPosition(m_env, &pos))
		{
			float minD, maxD;
			if (info->type & ST_GLOBAL)
			{
				minD = (float)m_ini.settings.globalMinRange;
				maxD = (float)m_ini.settings.globalMaxRange;
			}
			else
			{
				minD = info->minRange;
				maxD = info->maxRange;
			}
			const float dist = length3(m_listenerPosition, pos);
			if (dist >= maxD)
			{
				distanceFactor = 0.0f;
			}
			else if (dist > minD && maxD > minD)
			{
				distanceFactor = (maxD - dist) / (maxD - minD); // RW 0x4532DE: linear
			}
			else
			{
				distanceFactor = 1.0f;
			}
			if (haveZoomFade)
			{
				const float zoom = m_queries.zoomFadeFactor();
				if (zoom > 0.0f && !m_queries.isOnScreen(pos) && ((volume > 0.0f && info->zoomedInOffscreenVolumePercent != 1.0f) || (minVolume > 0.0f && info->zoomedInOffscreenMinVolumePercent != 1.0f)))
				{
					const MicrophoneSettings &mic = m_ini.settings.microphone[m_currentView == VIEW_LIVING_WORLD ? 1 : 0];
					const float dsq = m_queries.offscreenDistanceSq(pos);
					const float f = (dsq >= mic.zoomFadeDistanceForMaxEffectSq ? 1.0f : std::sqrt(dsq) / mic.zoomFadeDistanceForMaxEffect) * zoom;
					volume *= 1.0f - (1.0f - info->zoomedInOffscreenVolumePercent) * f; // RW 0x459351-0x45935F
					minVolume *= 1.0f - (1.0f - info->zoomedInOffscreenMinVolumePercent) * f;
				}
			}
		}
		if (volume > 0.0f)
		{
			volume *= distanceFactor;
		}
		if (minVolume > volume)
		{
			volume = minVolume;
		}
	}
	volume *= sliderFactor(*info, positional || info->soundType == AT_AmbientStream);
	return volume;
}

double AudioManager::getAudioLengthMS(const AudioEventRTS &event, std::string *error)
{
	AudioEventRTS copy = event;
	if (!copy.getAudioEventInfo())
	{
		copy.setAudioEventInfo(m_ini.infos.find(copy.getEventName()));
	}
	if (!copy.getAudioEventInfo())
	{
		if (error)
		{
			*error = "no info for '" + copy.getEventName() + "'";
		}
		return -1.0;
	}
	const std::string file = copy.getFilename(m_env);
	return m_device.fileLengthMs(file, error);
}
