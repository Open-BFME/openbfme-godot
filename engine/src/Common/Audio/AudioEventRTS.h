// OpenBFME. GPL-3.0.
//
// AudioEventRTS: one request to play an audio event (an event name plus where it is played from), and the state of its playing.
//
// Port of ZH GameEngine/Include/Common/AudioEventRTS.h + Source/Common/Audio/AudioEventRTS.cpp, as RotWK does it (RW 0x6DA5xx-0x6DC7xx,
// the AudioEventRTS.cpp of RW; offsets below are RW AudioEventRTS offsets). TARGET FACTS read from the disassembly:
//   * the object is created from a name, a name + position, a name + object / drawable / Living World owner (RW 0x6DA95E, 0x6DA9B2, ...);
//     every constructor starts with the commonInit of RW 0x6DA85B: volume and minVolume overrides -1.0 (unset), volume multiplier 1.0, owner type 6
//     (invalid), pitch / volume rolls 1.0, delay 0, portion Attack(0), the player index -1, `dirty` (regenerate the filename) set.
//   * the filename is generated LAZILY (RW 0x6DB98A getFilename -> 0x6DB821 generateFilename when the dirty flag is set), the delay and the
//     per-file pitch / volume rolls are made by 0x6DAC64 and the pitch / volume shift rolls by generatePlayInfo 0x6DBA01 (AudioManager's
//     copy-and-prepare 0x452072 calls them in that order: 0x6DAC64 then 0x6DBA01).
//   * generateFilename: the control bit 6 (0x40, the placeholder "not used by INI files") means "the Sounds entry is the complete file name":
//     no folder prefix and no extension. Otherwise the file is <folder of the sound type> + name (+ "." + SoundsExtension for type 2 only).
//     Types other than 2 (music, dialog, ambient, streamed) use info.Filename after the folder. Type 2 picks one Sounds entry: SEQUENTIAL control
//     advances the index modulo the count; otherwise a weighted random pick (RW 0x6DA80E, GetGameAudioRandomValue(0, total - 1)) that is
//     repeated while it equals the last played index (kept in the event INFO, +0x40; the first generation of the event reads it, the loop writes it) when
//     more than one entry exists.
//   * generatePlayInfo: pitch = rnd(min * 0.01 + 1, max * 0.01 + 1), volume shift = rnd(volumeShift + 1, 1); for type 2: attack / decay entries are
//     weighted picks too, the first portion is Attack when an attack exists, else Sound when Sounds is not empty, else Decay when a decay exists,
//     else Done; other types start at Sound.
//   * effective pitch = perFilePitch * pitch, effective volume shift = perFileVolumeShift * volumeShift (RW 0x6DA65D, 0x6DA664).
//   * ONLY the audio random stream is used: the RW code has no logical-audio branch (the ZH GameLogicRandomValue draws are gone), so an audio
//     event never consumes the logic RNG (AUDIO-1 acceptance: audio never touches logic state).
//   * RW sound class (0x6DAE8B): Music 1, UI 2, World 4, Streaming 8, Ambient 0x10 (types 4 and 5 map to 2 / 0).
// INFERENCE (stop S-243): the owner dependent mute of RW 0x6DB221 (an object with bit 20 of its status word set, a drawable whose byte +0x44A is clear) is
// not modelled; it needs the live objects.

#pragma once

#include "Common/Audio/AudioEventInfo.h"
#include "Common/Audio/AudioIni.h"
#include "Common/INIDataTypes.h"
#include "Common/RandomValue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

typedef std::uint32_t AudioHandle;

// RW MilesAudioManager +0xD0 starts the handle counter at 5 (RW 0x45C74D `mov [esi+0xD0], 5`).
enum AudioHandleSpecialValues : AudioHandle
{
	AHSV_Error = 0,
	AHSV_NoSound,
	AHSV_Muted,
	AHSV_NotForLocal,
	AHSV_StopTheMusic,
	AHSV_StopTheMusicFade,
	AHSV_FirstHandle = 5 // INFERENCE: ZH has 6; RW's counter starts at 5 so one of the special values is gone (stop S-240)
};

// RW AudioEventRTS +0x38 (0x6DAD89 / 0x6DB46A / 0x6DAE8B switch on it).
enum OwnerType
{
	OT_Positional = 0,
	OT_Drawable = 1,
	OT_Object = 2,
	OT_LivingWorld3 = 3, ///< Living World owners (RW 0xDE4958 / 0xDE4950 registries); not resolved here (stop S-246)
	OT_LivingWorld4 = 4,
	OT_LivingWorld5 = 5,
	OT_Invalid = 6
};

enum PortionToPlay
{
	PP_Attack = 0,
	PP_Sound = 1,
	PP_Decay = 2,
	PP_Done = 3
};

// RW AudioEventRTS +0x30 (the "view type": the constructors' extra int, B1 names it timeOfDay): which game view the event belongs to.
enum AudioViewType
{
	VIEW_TACTICAL = 0,
	VIEW_LIVING_WORLD = 1
};

// What an event needs from the world to be positioned: object and drawable positions. Installed by the live game; a missing resolver makes an
// event positional only through its own position (never a silent guess).
class AudioOwnerResolver
{
public:
	virtual ~AudioOwnerResolver() {}
	virtual bool objectPosition(std::uint32_t objectId, Coord3D *pos) const = 0;
	virtual bool drawablePosition(std::uint32_t drawableId, Coord3D *pos) const = 0;
};

// The services AudioEventRTS needs (TheAudio's settings and store, the audio random stream).
struct AudioEventEnv
{
	const AudioIniState *ini = nullptr;
	GameLogicRandom *random = nullptr; ///< the AUDIO stream (RW 0xDA1C8C): an instance of the shared generator class, never the logic one
	const AudioOwnerResolver *owners = nullptr;
};

class AudioEventRTS
{
public:
	AudioEventRTS();
	explicit AudioEventRTS(const std::string &eventName, int viewType = VIEW_TACTICAL);
	// AUDIO-2 (review r2 fix 1): a finite music request; copied with the event, so a Multisound's chosen subsound keeps it
	void setMusicOneShot(bool once) { m_musicOneShot = once; }
	bool isMusicOneShot() const { return m_musicOneShot; }
	AudioEventRTS(const std::string &eventName, const Coord3D &position, int viewType = VIEW_TACTICAL);
	// owner constructors: an id of 0 is "no owner" (RW 0x6DA95E keeps the invalid owner type)
	static AudioEventRTS forObject(const std::string &eventName, std::uint32_t objectId);
	static AudioEventRTS forDrawable(const std::string &eventName, std::uint32_t drawableId);

	void setEventName(const std::string &name);
	const std::string &getEventName() const { return m_eventName; }

	// ---- info --------------------------------------------------------------------------------
	void setAudioEventInfo(std::shared_ptr<const AudioEventInfo> info) { m_info = std::move(info); }
	// null when the name does not resolve
	const AudioEventInfo *getAudioEventInfo() const { return m_info.get(); }
	const std::shared_ptr<const AudioEventInfo> &infoRef() const { return m_info; }

	// ---- generation (RW 0x6DB821, 0x6DAC64, 0x6DBA01) ------------------------------------------
	// The file name to play (generated on first use or after markDirty): the VIRTUAL path inside the archives (backslashes).
	const std::string &getFilename(const AudioEventEnv &env);
	void generateFilename(const AudioEventEnv &env);
	void rollDelayAndPerFile(const AudioEventEnv &env);  ///< RW 0x6DAC64
	void generatePlayInfo(const AudioEventEnv &env);     ///< RW 0x6DBA01
	void markDirty() { m_dirty = true; }
	// RW 0x6DBC27: generatePlayInfo + rolls again keeping the portion (the loop's next play)
	void regenerateForLoop(const AudioEventEnv &env);

	float getPitchShift() const { return m_perFilePitch * m_pitchShift; }
	float getVolumeShift() const { return m_perFileVolumeShift * m_volumeShift; }
	float getDelay() const { return m_delay; }
	void decrementDelay(float ms) { m_delay -= ms; }
	void setDelay(float ms) { m_delay = ms; } ///< AUDIO-3: RW 0x6DAD2E clamps the delay into a range (SoundManager's requeue of a culled loop)
	const std::string &getAttackFilename() const { return m_attackName; }
	const std::string &getDecayFilename() const { return m_decayName; }

	PortionToPlay getNextPlayPortion() const { return (PortionToPlay)m_portion; }
	void setNextPlayPortion(PortionToPlay p) { m_portion = p; }
	void advanceNextPlayPortion();   ///< RW 0x6DB1DE
	void decreaseLoopCount();
	bool hasMoreLoops() const;       ///< RW 0x6DAD59
	void stopLooping() { m_finished = true; } ///< RW +0x4C: after it hasMoreLoops() is false

	// ---- owner and position ----------------------------------------------------------------------
	void setPosition(const Coord3D &pos);
	void setObjectID(std::uint32_t id);
	void setDrawableID(std::uint32_t id);
	std::uint32_t getObjectID() const { return m_ownerType == OT_Object ? m_ownerId : 0; }
	std::uint32_t getDrawableID() const { return m_ownerType == OT_Drawable ? m_ownerId : 0; }
	OwnerType getOwnerType() const { return (OwnerType)m_ownerType; }
	// RW 0x6DAD89
	bool isPositionalAudio() const;
	// RW 0x6DB31E: false when the owner cannot be resolved. Positional events return their own position.
	bool getCurrentPosition(const AudioEventEnv &env, Coord3D *pos) const;
	// RW 0x6DB46A (isDead: an owner that no longer exists)
	bool isDead(const AudioEventEnv &env) const;
	// RW 0x6DAE8B
	unsigned getSoundClass() const;
	enum { SC_MUSIC = 1, SC_UI = 2, SC_WORLD = 4, SC_STREAMING = 8, SC_AMBIENT = 0x10 };

	// ---- state -----------------------------------------------------------------------------------
	void setPlayingHandle(AudioHandle h) { m_playingHandle = h; }
	AudioHandle getPlayingHandle() const { return m_playingHandle; }
	void setHandleToKill(AudioHandle h) { m_killThisHandle = h; }
	AudioHandle getHandleToKill() const { return m_killThisHandle; }
	void setPlayerIndex(int i) { m_playerIndex = i; }
	int getPlayerIndex() const { return m_playerIndex; }
	void setViewType(int v) { m_viewType = v; }
	int getViewType() const { return m_viewType; }
	void setVolume(float v) { m_volumeOverride = v; }
	// the override when set (-1.0 = unset), else the INFO's volume (0.5 without an info: ZH getVolume, B1 AudioEventRTS.cpp)
	float getVolume() const;
	// RW 0x6DB2A5 getMinVolume
	float getMinVolume() const;
	void setUninterruptable(bool u) { m_uninterruptable = u; }
	bool getUninterruptable() const { return m_uninterruptable; }
	void setShouldFade(bool f) { m_shouldFade = f; }
	bool getShouldFade() const { return m_shouldFade; }
	int getPlayingAudioIndex() const { return m_playingAudioIndex; }

private:
	void init(const std::string &name);

	std::string m_filename;
	std::shared_ptr<const AudioEventInfo> m_info;
	std::string m_eventName;
	std::string m_attackName;
	std::string m_decayName;
	AudioHandle m_playingHandle = 0;
	AudioHandle m_killThisHandle = 0;
	float m_volumeOverride = -1.0f;    // RW +0x24
	float m_minVolumeOverride = -1.0f; // RW +0x28
	float m_volumeMultiplier = 1.0f;   // RW +0x2C
	int m_viewType = VIEW_TACTICAL;    // RW +0x30
	std::uint32_t m_ownerId = 0;       // RW +0x34
	int m_ownerType = OT_Invalid;      // RW +0x38
	Coord3D m_position;                // RW +0x3C
	bool m_hasPosition = false;        // RW +0x48
	bool m_shouldFade = false;
	bool m_uninterruptable = false;
	bool m_finished = false;           // RW +0x4C
	bool m_dirty = true;               // RW +0x4D
	bool m_filenameGenerated = false;  // RW +0x4E (the first generation)
	bool m_regenerate = false;         // RW +0x50
	bool m_forceSequential = false;    // RW +0x53
	bool m_musicOneShot = false;       // AUDIO-2: a finite music request (the music scripts' 476 / 478): the track plays once, no backend loop, no playlist chain
	float m_pitchShift = 1.0f;         // RW +0x54
	float m_perFilePitch = 1.0f;       // RW +0x58
	float m_volumeShift = 1.0f;        // RW +0x5C
	float m_perFileVolumeShift = 1.0f; // RW +0x60
	float m_delay = 0.0f;              // RW +0x64
	int m_playingAudioIndex = -1;      // RW +0x68
	int m_playerIndex = -1;            // RW +0x70
	int m_portion = PP_Attack;         // RW +0x74
	int m_loopCount = 1;               // RW +0x78

	friend class AudioManager;
};

// RW 0x6DA80E: the index of the weighted pick over `list` (-1 when the total weight is 0 or the list is empty).
int AudioWeightedChoice(int totalWeight, const WeightedSoundList &list, GameLogicRandom &random);

// Every virtual file path an event can play (Sounds, Attack and Decay entries of an AudioEvent; the Filename of the other types), in list order,
// with the folder of its sound type and the extension rule of RW 0x6DB4EA. Default / FAKE events and Multisounds have no files.
std::vector<std::string> AudioEventFiles(const AudioSettings &settings, const AudioEventInfo &info);
