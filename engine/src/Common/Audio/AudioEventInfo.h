// OpenBFME. GPL-3.0.
//
// AudioEventInfo: the INI definition of one audio event (AudioEvent, MusicTrack, DialogEvent, AmbientStream,
// StreamedSound and Multisound blocks) and the store TheAudio keeps them in.
//
// Port of ZH GameEngine/Include/Common/AudioEventInfo.h + Source/Common/INI/INIAudioEventInfo.cpp, changed
// to what the RotWK binary does (RW addresses, caveat S-001; every deviation from ZH below is a TARGET FACT):
//   * ONE parse function serves all six block keywords (RW 0x5D9EFF; the block thunks 0x5DA10F / 0x5DA159 /
//     0x5DA1A3 / 0x5DA1ED / 0x5DA237 pass the sound type 0 / 2 / 1 / 3 / 4 and the default's name; Multisound
//     0x5D9C6D is type 5 and has its own parse). A map.ini may not define any of them (load type 2:
//     INIException(3, "You cannot define or override a %s in map.ini", block), RW 0xBF04C8).
//   * a block copies the DEFAULT event first (RW 0x5D9B08), clears its DEFAULT type bit, then reads its fields:
//     a redefinition therefore resets the event to the defaults before applying the new fields.
//   * the field table is RW 0xBEFDF0 (26 rows). New against ZH: PerFileVolumeShift, PerFilePitchShift,
//     ZoomedInOffscreen* percents, ReverbEffectLevel, DryLevel, SubmixSlider, VolumeSliderMultiplier; no
//     SoundsNight / SoundsEvening / SoundsMorning and no LoopCount row (the loop count is a runtime value).
//   * Control names are BFME's (LOOP SEQUENTIAL RANDOMSTART INTERRUPT FADE_ON_KILL FADE_ON_START <placeholder>
//     PLAY_ONE), Type adds FAKE and DEFAULT (RW 0xD9DB70 / 0xD9DB40).
//   * Sounds / Attack / Decay / Subsounds are weighted lists "name[:weight]" (RW 0x5DA345; default weight 1000).
//   * PitchShift is stored as the INI's two reals (no 1 + x/100 conversion in the parser); the conversion is made
//     when the pitch is drawn (AudioEventRTS::generatePlayInfo, RW 0x6DBA01).

#pragma once

#include "Common/INI.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum AudioType
{
	AT_Music = 0,
	AT_Streaming = 1,   ///< DialogEvent
	AT_SoundEffect = 2, ///< AudioEvent
	AT_AmbientStream = 3,
	AT_StreamedSound = 4,
	AT_Multisound = 5
};

enum AudioPriority
{
	AP_LOWEST,
	AP_LOW,
	AP_NORMAL,
	AP_HIGH,
	AP_CRITICAL
};

// RW 0xD9DB40 (11 names). ZH has the first nine.
enum SoundType : unsigned
{
	ST_UI = 0x0001,
	ST_WORLD = 0x0002,
	ST_SHROUDED = 0x0004,
	ST_GLOBAL = 0x0008,
	ST_VOICE = 0x0010,
	ST_PLAYER = 0x0020,
	ST_ALLIES = 0x0040,
	ST_ENEMIES = 0x0080,
	ST_EVERYONE = 0x0100,
	ST_FAKE = 0x0200,
	ST_DEFAULT = 0x0400
};

// RW 0xD9DB70 (8 names; bit 6 is a placeholder the INI cannot name usefully).
enum AudioControl : unsigned
{
	AC_LOOP = 0x01,
	AC_SEQUENTIAL = 0x02,
	AC_RANDOMSTART = 0x04,
	AC_INTERRUPT = 0x08,
	AC_FADE_ON_KILL = 0x10,
	AC_FADE_ON_START = 0x20,
	AC_NOT_USED_BY_INI = 0x40,
	AC_PLAY_ONE = 0x80
};

// RW 0xD9DB94: the volume sliders of the options screen.
enum AudioSubmixSlider
{
	SLIDER_NONE_SET = -1,
	SLIDER_SOUNDFX = 0,
	SLIDER_VOICE = 1,
	SLIDER_MUSIC = 2,
	SLIDER_AMBIENT = 3,
	SLIDER_MOVIE = 4,
	SLIDER_NONE = 5
};

extern const char *const theAudioPriorityNames[];   ///< 5 names + nullptr
extern const char *const theSoundTypeNames[];       ///< 11 names + nullptr
extern const char *const theAudioControlNames[];    ///< 8 names + nullptr
extern const char *const theSubmixSliderNames[];    ///< 6 names + nullptr

struct WeightedSound
{
	std::string name;
	int weight = 1000;
};

// RW: a vector of {name, weight} entries and the sum of the weights (the +0x0C word of each list).
struct WeightedSoundList
{
	std::vector<WeightedSound> sounds;
	int totalWeight = 0;
};

struct SliderMultiplier
{
	int slider = SLIDER_NONE_SET;
	float multiplier = 1.0f; ///< the INI percent * 0.01
};

struct AudioEventInfo;

// One Multisound entry: the event named (resolved against the store at parse time) and its weight.
struct Subsound
{
	std::string name;
	int weight = 1000;
	std::shared_ptr<AudioEventInfo> info;
};

struct AudioEventInfo
{
	// RW constructor 0x5D992D (defaults); every block of the family starts from the DEFAULT event instead.
	std::string audioName;
	std::string filename;

	float volume = 100.0f;
	float volumeShift = 0.0f;
	float perFileVolumeShift = 0.0f;
	float minVolume = 0.0f;
	float pitchShift[2] = { 0.0f, 0.0f };        ///< percent, min/max
	float perFilePitchShift[2] = { 0.0f, 0.0f }; ///< percent, min/max
	float playPercent = 1.0f;
	int delay[2] = { 0, 0 };                     ///< ms, min/max
	int limit = 25;
	int lastPlayedIndex = -1;                  ///< not an INI field (RW +0x40): the index of the Sounds entry played last (the random pick never repeats it)
	int priority = AP_NORMAL;
	unsigned type = 0;                           ///< SoundType bits
	unsigned control = 0;                        ///< AudioControl bits

	WeightedSoundList sounds;
	WeightedSoundList attack;
	WeightedSoundList decay;
	std::vector<Subsound> subsounds; ///< Multisound
	int subsoundsTotalWeight = 0;

	float lowPassCutoff = 0.0f;
	float minRange = 100.0f;
	float maxRange = 1000.0f;
	float zoomedInOffscreenVolumePercent = 1.0f;
	float zoomedInOffscreenMinVolumePercent = 1.0f;
	float zoomedInOffscreenOcclusionPercent = 0.0f;
	float reverbEffectLevel = 1.0f;
	float dryLevel = 1.0f;

	int soundType = AT_SoundEffect;
	int submixSlider = SLIDER_NONE_SET;
	std::vector<SliderMultiplier> volumeSliderMultipliers;

	// (ZH isPermanentSound has no counterpart: RW has no LoopCount field)
	
	bool isDefaultEvent() const { return (type & ST_DEFAULT) != 0; }

	static const FieldParse *getFieldParse();
};

// TheAudio's table of event infos (ZH AudioManager::m_allAudioEventInfo; RW MilesAudioManager +0xBC, case-SENSITIVE name key,
// B1 AudioManagerFindAudioEventInfo.cpp). Insertion order is kept so every walk is deterministic.
class AudioEventInfoStore
{
public:
	typedef std::shared_ptr<AudioEventInfo> InfoPtr;

	// RW vtbl 0x12C (findAudioEventInfo): null when the name is unknown.
	InfoPtr find(const std::string &name) const;
	bool contains(const std::string &name) const { return m_byName.count(name) != 0; }
	// RW vtbl 0x124 (newAudioEventInfo): the existing info of the name, or a new default-constructed one added to the table.
	InfoPtr findOrCreate(const std::string &name);

	size_t size() const { return m_ordered.size(); }
	const std::vector<InfoPtr> &all() const { return m_ordered; }

	void clear();

private:
	std::unordered_map<std::string, InfoPtr> m_byName;
	std::vector<InfoPtr> m_ordered;
};

// The default each block keyword copies (the names are hard coded in the RW block thunks).
const char *AudioDefaultNameFor(int soundType);

// The INI side. `ini` has consumed the block keyword. `blockName` is the keyword (error text only).
// ACCEPTANCE STOP S-240 covers the parts of the runtime semantics that are not recovered from the binary.
void ParseAudioEventInfoBlock(INI *ini, AudioEventInfoStore &store, int soundType, const char *blockName);
void ParseMultisoundBlock(INI *ini, AudioEventInfoStore &store);
