// OpenBFME. GPL-3.0.
//
// AudioManager: the device independent core of TheAudio (event instances, selection, priorities, limits, interrupts, delays, loops,
// volumes, distance attenuation, 2D / 3D classification, shroud and player filtering, music, ambient streams, EVA-facing API).
//
// Port of ZH GameEngine/Source/Common/Audio/GameAudio.cpp (AudioManager), GameSounds.cpp (SoundManager::canPlayNow), GameMusic.cpp and the
// DEVICE INDEPENDENT decisions of GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp (playAudioEvent, doesViolateLimit,
// findLowestPrioritySound, killLowestPrioritySoundImmediately, processPlayingList, processFadingList, startNextLoop), with what the RotWK
// binary does differently (RW addresses, caveat S-001). The sound itself is made by an IAudioDevice.
//
// TARGET FACTS (RW), each read from the disassembly (the spec lines are in workspace/rebuild/specs/audio.md):
//   * the play request processor RW 0x46154D: PlayPercent: for sound effects (type 2) a request plays only when
//     GetGameAudioRandomValueReal(0, 1) <= info.PlayPercent (RW 0x461655-0x461687, AudioEventRTS.cpp line 0x1B98); then positional events
//     take a 3D sample, others a 2D sample; when no sample is free a looping event is queued again (RW 0x461786-0x4617A9) instead of
//     being dropped.
//   * getEffectiveVolume RW 0x4591D4: the event must belong to the manager's current view (tactical / Living World, RW +0x678); volume =
//     event volume * shifts; positional: * linear distance factor (RW 0x453227: 1 inside MinRange, (max - d) / (max - min) between,
//     0 at MaxRange; Global events use AudioSettings GlobalMinRange / GlobalMaxRange), floored by MinVolume, off-screen fade by the zoom
//     (RW 0x4592B8-0x459376); times the slider factor of the event's SubmixSlider (RW 0x458825, the table built by 0x4516DE);
//     never above 1.0 in the device (INI: "sounds never actually play above 100% volume").
//   * submix slider default (RW 0x5D90C3): the SubmixSlider field, else Voice for events with the VOICE type bit, else by sound type:
//     Music -> Music, Dialog -> Voice, AudioEvent -> SoundFX, AmbientStream -> Ambient, other -> SoundFX.
//   * the soundmanager audio update interval is 33.33 ms (RW +0x90, constructor RW 0x45C6F5); a request delay shrinks by it per processing
//     step (RW 0x452FD6-0x452FE2). The core here runs fixed 33.33 ms steps off a caller clock (stop S-242 for the exact call rate).
// DONOR (ZH, B1) for the structure the binary shares: request list, playing lists (2D samples, 3D samples, streams, fading), limit /
// priority / interrupt rules, shouldPlayLocally, music playTrack / stopTrack.
// INFERENCE and open items are the acceptance stops S-240 .. S-249 (docs/STOPS.md).

#pragma once

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioDevice.h"
#include "Common/Audio/AudioEventRTS.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/AudioLog.h"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

// RW strings AudioAffect_* (0xBFE198-0xBFE20C).
enum AudioAffect : unsigned
{
	AudioAffect_Music = 0x01,
	AudioAffect_Sound = 0x02,
	AudioAffect_Sound3D = 0x04,
	AudioAffect_Speech = 0x08,
	AudioAffect_AmbientStream = 0x10,
	AudioAffect_SystemSetting = 0x20,
	AudioAffect_All = AudioAffect_Music | AudioAffect_Sound | AudioAffect_Sound3D | AudioAffect_Speech | AudioAffect_AmbientStream
};

// ZH Common/Relationship: the relationship of an owning player to the local player.
enum AudioRelationship
{
	AR_ENEMIES = 0,
	AR_NEUTRAL = 1,
	AR_ALLIES = 2
};

// Queries the live game answers (shouldPlayLocally, the shrouded check, the view). All optional; a feature whose query is missing is counted
// in AudioReport::missingHooks, never silently assumed to pass.
struct AudioWorldQueries
{
	// ZH Player list: the local player's index, whether a player index exists, the relationship of `owner` to the local player
	std::function<int()> localPlayerIndex;
	std::function<bool(int)> playerExists;
	std::function<AudioRelationship(int owner, int local)> relationship;
	// ZH PartitionManager::getShroudStatusForPlayer == CELLSHROUD_CLEAR
	std::function<bool(int playerIndex, const Coord3D &)> isShroudClear;
	// the tactical view: is the point on screen, how far is it off screen (squared world distance), and the zoom fade factor 0..1
	std::function<bool(const Coord3D &)> isOnScreen;
	std::function<float(const Coord3D &)> offscreenDistanceSq;
	std::function<float()> zoomFadeFactor;

	// Lane AUDIO-4 (QA-1 U21): the player list outside a live game (the shell, the lobby, the score screen). The port has no player list there; the
	// sounds requested there (APT PlaySound, the shell's gadgets: RW 0x72937E -> 0x815772) carry no owner (player index -1), and for an ownerless event
	// RotWK's filter (RW 0x4533A9: getNthPlayer(-1) is null) answers PLAYER + UI: play, anything else: refuse, whatever players the shell map's list
	// holds. So the shell's list answers "no such player" for every index, no local player, and has no shroud (isShroudClear stays unset: a shrouded
	// positional sound outside a game is reported, not guessed).
	static AudioWorldQueries noGamePlayerList();
};

struct AudioReport
{
	std::uint64_t eventsAdded = 0;
	std::uint64_t unknownEvents = 0;       ///< addAudioEvent of a name with no info
	std::map<std::string, std::uint64_t> unknownEventNames; ///< lane AUDIO-4: those names (retail refuses them silently, RW 0x45CEA7)
	std::uint64_t culledNotForLocal = 0;
	std::uint64_t culledMuted = 0;
	std::uint64_t culledDistance = 0;
	std::uint64_t culledShroud = 0;
	std::uint64_t culledVoice = 0;
	std::uint64_t culledLimit = 0;
	std::uint64_t culledNoChannel = 0;
	std::uint64_t culledPlayPercent = 0;
	std::uint64_t played = 0;
	std::uint64_t playFailures = 0;        ///< the device could not start a voice (missing file, undecodable data)
	std::uint64_t stolenChannels = 0;
	std::uint64_t interrupted = 0;
	std::uint64_t requeuedLoops = 0;
	std::uint64_t ticks = 0;
	std::uint64_t droppedTicks = 0;        ///< steps skipped after a stall longer than the catch-up cap
	std::map<std::string, std::uint64_t> missingHooks; ///< a query a processed event needed that is not installed
	std::vector<std::string> errors;       ///< the first 200 distinct play failures (event, file, reason)
};

// AUDIO-3: one line of the request log (a diagnostic: off unless enableEventLog; client only, no simulation data). A request line (play == false) is written
// by addAudioEvent with what the request did (queued, or why it was refused); a play line (play == true) by the request processor when the queued event
// started a voice or was dropped there. `origin` names the caller (AudioLog::Scope, Common/Audio/AudioLog.h).
struct AudioLogEntry
{
	double clockMs = 0.0;
	std::uint32_t frame = 0; ///< the logic frame the owner set last (setLogFrame)
	std::string event;
	std::string origin;
	std::uint32_t objectId = 0, drawableId = 0;
	bool positional = false;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	int player = -1;
	AudioHandle handle = 0;
	bool play = false;
	std::string outcome;
};


class AudioManager
{
public:
	enum { kTickIntervalNumerator = 100, kTickIntervalDenominator = 3 }; // 33.33.. ms (RW +0x90)
	static double tickIntervalMs() { return (double)kTickIntervalNumerator / (double)kTickIntervalDenominator; }

	// `ini` and `cache` and `device` outlive the manager. `algorithm` names the generator of the AUDIO random stream explicitly (S-080).
	AudioManager(AudioIniState &ini, AudioAssetCache &cache, IAudioDevice &device, RandomAlgorithm algorithm, std::uint32_t seed);
	~AudioManager();

	void setWorldQueries(const AudioWorldQueries &q) { m_queries = q; m_queriesOwner = nullptr; }
	void setOwnerResolver(const AudioOwnerResolver *r) { m_env.owners = r; }
	// AUDIO-2 (review r1 fix 1): owner-specific bindings. `owner` binds the queries; clearWorldQueries / clearOwnerResolver remove a binding only when it is
	// still that owner's (a later attachment replaced it otherwise), so an attachment never erases another one's bindings
	void bindWorldQueries(const AudioWorldQueries &q, const void *owner) { m_queries = q; m_queriesOwner = owner; }
	void clearWorldQueries(const void *owner)
	{
		if (m_queriesOwner == owner)
		{
			m_queries = m_idleQueries; // lane AUDIO-4: back to the queries outside a game (empty unless setIdleWorldQueries installed some)
			m_queriesOwner = nullptr;
		}
	}
	// Lane AUDIO-4 (QA-1 U21): the queries in force while no live game has bound its own (the shell: AudioWorldQueries::noGamePlayerList()); installs
	// them now unless an owner's binding is in force
	void setIdleWorldQueries(const AudioWorldQueries &q)
	{
		m_idleQueries = q;
		if (!m_queriesOwner)
		{
			m_queries = q;
		}
	}
	void clearOwnerResolver(const AudioOwnerResolver *r)
	{
		if (m_env.owners == r)
		{
			m_env.owners = nullptr;
		}
	}
	const AudioOwnerResolver *ownerResolver() const { return m_env.owners; }
	bool hasWorldQueriesOf(const void *owner) const { return m_queriesOwner == owner && owner != nullptr; }
	// expires when this manager is destroyed: an attachment that outlives the manager can tell (never compare manager addresses for liveness)
	std::weak_ptr<const int> lifetimeToken() const { return m_alive; }

	// ---- the API every caller uses (logic event sites, draw / model-condition sounds, FXList sound nuggets, Lua, APT) ----------------------
	// ZH AudioManager::addAudioEvent: returns the handle of the playing instance, or one of AudioHandleSpecialValues.
	AudioHandle addAudioEvent(const AudioEventRTS &event);
	// convenience entry points: a UI sound by name, a sound at a position, attached to an object / drawable, for an owning player
	AudioHandle playSound(const std::string &eventName);
	AudioHandle playSoundAt(const std::string &eventName, const Coord3D &position);
	AudioHandle playSoundForObject(const std::string &eventName, std::uint32_t objectId, int owningPlayerIndex = -1);
	AudioHandle playSoundForDrawable(const std::string &eventName, std::uint32_t drawableId, int owningPlayerIndex = -1);
	AudioHandle playSoundForPlayer(const std::string &eventName, int owningPlayerIndex);
	// ZH removeAudioEvent: stop (with the Decay portion and the fade the event asks for)
	void removeAudioEvent(AudioHandle handle);
	// AUDIO-2 (review r2 fix 2): moves a positional event that is pending or playing (a sound placed at a position, not one owned by an object / drawable);
	// false when no such event exists
	bool setEventPosition(AudioHandle handle, const Coord3D &position);
	// ZH killAudioEventImmediately
	void killAudioEventImmediately(AudioHandle handle);
	bool isCurrentlyPlaying(AudioHandle handle) const;
	bool isValidAudioEvent(const std::string &eventName) const;
	// ZH getAudioLengthMS: the length of the file the event would play (-1 + *error when unavailable)
	double getAudioLengthMS(const AudioEventRTS &event, std::string *error = nullptr);

	// ---- music ---------------------------------------------------------------------------------------------------------------------------
	AudioHandle playMusic(const std::string &eventName, bool fadeIn = false); ///< a MusicTrack or Multisound (lane SCRIPT-3: `fadeIn` sets the event's shouldFade)
	// AUDIO-2 (review r2 fix 1): the track (or the Multisound's chosen track) plays ONCE: no backend loop, no LOOP playlist chain; the caller (the music
	// scripts' finite actions) counts the completions and repeats
	AudioHandle playMusicOnce(const std::string &eventName);
	void stopMusic(bool fade);                                ///< AHSV_StopTheMusic / AHSV_StopTheMusicFade
	bool isMusicPlaying() const;
	std::string getMusicTrackName() const;
	int musicTrackCompletions(const std::string &trackName) const;
	// MiscAudio names (the engine's own sounds): "" when the slot is NoSound
	const std::string &miscAudio(const std::string &fieldName) const { return m_ini.miscAudio.get(fieldName); }

	// ---- per-frame --------------------------------------------------------------------------------------------------------------------------
	// Advances the manager to `nowMs` (a monotonic client clock) in fixed 33.33 ms steps (at most 8 per call; more are dropped and counted).
	void update(double nowMs);
	// ZH AudioManager::update: the microphone from the camera. groundPos: where the camera looks at the terrain; cameraPos; angle: the
	// camera heading in radians; groundHeight at the look-at point. INFERENCE from the INI comments of the Microphone* fields (stop S-243).
	void updateMicrophone(const Coord3D &lookAtGround, const Coord3D &cameraPos, float cameraAngleRadians, int view = 0);
	void setListenerPosition(const Coord3D &position, const Coord3D &forward);
	const Coord3D &getListenerPosition() const { return m_listenerPosition; }

	// ---- state and volumes ---------------------------------------------------------------------------------------------------------------------
	void setOn(bool on, unsigned affect);
	bool isOn(unsigned affect) const;
	// system (the options sliders) when AudioAffect_SystemSetting is in `affect`, else the script volume (ZH setVolume)
	void setVolume(float volume, unsigned affect);
	float getVolume(unsigned affect) const;
	void setCurrentView(int view) { m_currentView = view; }
	void stopAudio(unsigned affect);
	void setDisallowSpeech(bool d) { m_disallowSpeech = d; }
	// ZH setAudioEventVolumeOverride / setAudioEventEnabled
	void setAudioEventVolumeOverride(const std::string &eventName, float volume);
	void setAudioEventEnabled(const std::string &eventName, bool enable);
	// RW +0x34 per slider factor (script fades): `slider` is an AudioSubmixSlider value
	void setSliderScriptFactor(int slider, float factor);

	// ---- introspection (tests, the viewer, the report) ----------------------------------------------------------------------------------
	size_t playingCount(VoiceKind kind) const;
	size_t pendingRequests() const;
	std::vector<std::string> playingEventNames() const;
	const AudioReport &report() const { return m_report; }
	const AudioEventEnv &env() const { return m_env; }
	GameLogicRandom &audioRandom() { return m_random; }
	AudioIniState &ini() { return m_ini; }
	// The effective volume the manager would give `event` now (RW getEffectiveVolume), for tests and the viewer.
	float getEffectiveVolume(AudioEventRTS &event);
	double nowMs() const { return m_clockMs; }
	// the acceptance stops that apply to this core (docs/STOPS.md); every consumer must surface them
	static std::vector<std::string> unverified();
	// ---- AUDIO-3: the request log (diagnostics) ---------------------------------------------------------------------------------------------
	void enableEventLog(bool on); // also counts in AudioLog::enabled() (the destructor gives it back)
	bool eventLogEnabled() const { return m_logOn; }
	void setLogFrame(std::uint32_t frame) { m_logFrame = frame; }
	// the lines since the last call (at most kEventLogCap are kept between two calls; more are counted in eventLogDropped)
	std::vector<AudioLogEntry> takeEventLog();
	std::uint64_t eventLogDropped() const { return m_logDropped; }
	enum { kEventLogCap = 200000 };

private:
	enum PlayingStatus
	{
		PS_Playing,
		PS_Stopped
	};
	struct PlayingAudio
	{
		std::unique_ptr<AudioEventRTS> event;
		VoiceKind kind = VoiceKind::Sample2D;
		PlayingStatus status = PS_Playing;
		int voice = 0;
		bool requestStop = false;
		int framesFaded = 0;
		bool fading = false;
		int portionVoiceStarted = -1;
		double startedMs = 0.0;
	};
	enum RequestType
	{
		RQ_Play,
		RQ_Stop,
		RQ_Pause
	};
	struct Request
	{
		RequestType type = RQ_Play;
		std::unique_ptr<AudioEventRTS> pending;
		AudioHandle handle = 0;
		bool requiresCheckForSample = false;
		bool deferred = false;  ///< queued while the request list was being processed: waits for the next tick
		bool cancelled = false; ///< cancelled while the request list was being processed: the pass erases it
	};
	void pushRequest(Request &&req);
	template <class Pred> void cancelRequests(Pred pred, bool firstOnly = false);
	static unsigned affectOf(const AudioEventRTS &event);

	// request path
	AudioHandle addInternal(const AudioEventRTS &event, int depth);
	bool shouldPlayLocally(const AudioEventRTS &event);
	bool canPlayNow(AudioEventRTS &event);
	bool violatesVoice(const AudioEventRTS &event) const;
	bool isInterrupting(const AudioEventRTS &event) const;
	bool doesViolateLimit(AudioEventRTS &event) const;
	bool isPlayingLowerPriority(const AudioEventRTS &event) const;
	bool isPlayingAlready(const AudioEventRTS &event) const;
	bool isObjectPlayingVoice(std::uint32_t objectId) const;
	AudioEventRTS *findLowestPrioritySound(const AudioEventRTS &event);
	bool killLowestPrioritySoundImmediately(const AudioEventRTS &event);
	void processRequestList();
	void processRequest(Request &req);
	bool shouldProcessRequestThisFrame(const Request &req) const;
	void playAudioEvent(std::unique_ptr<AudioEventRTS> event);
	// playing
	void processPlayingList();
	void processFadingList();
	void processAmbientMarkers();
	void processMusicChain();
	void stopAudioEvent(AudioHandle handle);
	bool startVoice(PlayingAudio &p, const std::string &file, bool firstPortion);
	void completeVoice(PlayingAudio &p);
	VoiceParams voiceParamsFor(AudioEventRTS &event, VoiceKind kind);
	float panFor(const Coord3D &pos) const;
	void releasePlaying(PlayingAudio &p);
	void reportFailure(const AudioEventRTS &event, const std::string &file, const std::string &reason);
	void noteMissingHook(const char *name);
	// volume
	float sliderFactor(const AudioEventInfo &info, bool positionalOrAmbient) const;
	float duckFactor(int slider, const AudioEventInfo *self) const;
	void refreshDucking();
	AudioHandle allocateNewHandle() { return m_nextHandle++; }
	size_t channelLimit(VoiceKind kind) const;
	std::list<std::unique_ptr<PlayingAudio>> &listFor(VoiceKind kind);
	const std::list<std::unique_ptr<PlayingAudio>> &listFor(VoiceKind kind) const;

	AudioIniState &m_ini;
	AudioAssetCache &m_cache;
	IAudioDevice &m_device;
	GameLogicRandom m_random;
	AudioEventEnv m_env;
	AudioWorldQueries m_queries;
	AudioWorldQueries m_idleQueries; ///< lane AUDIO-4: what clearWorldQueries restores
	const void *m_queriesOwner = nullptr;
	std::shared_ptr<const int> m_alive = std::make_shared<const int>(0);
	AudioReport m_report;

	std::list<Request> m_requests;
	bool m_requestPassActive = false; ///< processRequestList is running: erasures are marked, additions are deferred
	std::list<std::unique_ptr<PlayingAudio>> m_playing2D;
	std::list<std::unique_ptr<PlayingAudio>> m_playing3D;
	std::list<std::unique_ptr<PlayingAudio>> m_playingStreams;
	std::list<std::unique_ptr<PlayingAudio>> m_fading;
	// ambient stream markers (the INI: the system plays the loudest streams, hysteresis AmbientStreamHysteresisVolume)
	struct AmbientMarker
	{
		std::unique_ptr<AudioEventRTS> event;
		AudioHandle playing = 0; ///< the handle of the stream playing for this marker, 0 when none
	};
	std::map<AudioHandle, AmbientMarker> m_ambientMarkers;

	Coord3D m_listenerPosition;
	Coord3D m_listenerForward{ 0.0f, 1.0f, 0.0f };
	AudioHandle m_nextHandle = AHSV_FirstHandle;
	double m_clockMs = 0.0;
	bool m_clockStarted = false;
	int m_currentView = VIEW_TACTICAL;
	bool m_disallowSpeech = false;

	bool m_musicOn = true, m_soundOn = true, m_sound3DOn = true, m_speechOn = true, m_ambientOn = true;
	// system volumes (the options sliders) and script volumes, per slider SoundFX / Voice / Music / Ambient / Movie
	float m_systemVolume[6];
	float m_scriptVolume[6];
	float m_sliderScriptFactor[6];
	float m_duck[6];
	float m_zoomVolume = 1.0f;
	std::vector<std::pair<std::string, float>> m_adjustedVolumes;
	std::map<std::string, int> m_trackCompletions;
	std::string m_currentTrackEvent;
	AudioHandle m_currentMusicHandle = 0;
	std::string m_musicMultisound; ///< a looping PLAY_ONE multisound being played
	// AUDIO-3: the subsounds of a multisound that plays all of them, by the handle addAudioEvent returned (the first subsound's): RotWK gives the group one
	// handle (RW 0x459B55 -> 0x458AE3), so removing it removes every subsound (a Porter's MenBuilderMoveLoopMS: the move loop and the cart rattles)
	std::map<AudioHandle, std::vector<AudioHandle>> m_multisoundGroups;
	bool isHandleAlive(AudioHandle handle) const;
	bool setLeafPosition(AudioHandle handle, const Coord3D &position);
	// AUDIO-3: the request log
	void logLine(const AudioEventRTS &event, AudioHandle handle, bool play, const char *outcome, const char *detail = nullptr, const char *suffix = nullptr);
	bool m_logOn = false;
	std::uint32_t m_logFrame = 0;
	std::uint64_t m_logDropped = 0;
	std::vector<AudioLogEntry> m_log;
	const char *m_refusal = ""; ///< why canPlayNow refused the event it checked last
};
