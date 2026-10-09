// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// LiveGameAudio (lane AUDIO-2): what the audio manager needs from a running game, and the object sounds a running game makes on its own. Client
// presentation only: it reads the logic and the drawables and talks to the installed AudioManager; it changes no simulation state.
//   * the owner resolver: an object / drawable attached event follows its owner's position and dies with it (RW 0x6DB31E asks the owner each update);
//   * the player queries of shouldPlayLocally (the local player, whether a player exists, the relationship of two players, ZH Player::getRelationship);
//   * the ambient sound of every drawable (ZH Drawable::startAmbientSound, Drawable.cpp:4461): the template's SoundAmbient for the body damage state
//     (SoundAmbientDamaged / ReallyDamaged / Rubble, falling back to SoundAmbient for the damaged states when unset), started for objects that are complete
//     and alive, restarted when the damage state changes, stopped on death; a non global, non critical sound starts only inside its MaxRange of the listener.
// DONOR (ZH) for the ambient rules; RotWK adds SoundAmbientBattle and per-object custom ambient info (stops S-706 .. S-708). The voice of a selection or a command
// is UnitVoiceResponse (the HUD's).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class AudioManager;
class MusicScripts;
class ThingTemplate;
class LiveGame;
struct ScriptClientRequest;
class Object;

// The header carries no audio type: simulation-input files (GodotGameWorld.cpp) include it, and the audio headers' client arithmetic must not reach the audit.
class LiveGameAudio
{
public:
	// installs itself as `audio`'s owner resolver and world queries; both are removed again by the destructor (which also stops the ambient sounds)
	LiveGameAudio(LiveGame &game, AudioManager &audio);
	~LiveGameAudio();
	// the installed manager (AudioApi::current()) for `game`, and TheEva set to the game's local player; null + *error when no manager is installed.
	// *evaSide receives the side TheEva speaks for ("" without an installed Eva)
	// `fs` (may be null: no music) reads the music script map (AudioSettings MusicScriptLibraryName) and starts the in-game music (MusicScripts)
	static std::unique_ptr<LiveGameAudio> attachToInstalled(LiveGame &game, ArchiveFileSystem *fs, std::string *error, std::string *evaSide);
	void startMusic(ArchiveFileSystem &fs);
	// the in-game music (null when its map could not be read: musicError() says why)
	MusicScripts *music() { return m_music.get(); }
	const std::string &musicError() const { return m_musicError; }
	// AudioApi's counters of calls that found nothing installed (the report of the device layer)
	struct ApiCounters
	{
		std::uint64_t callsWithoutAudio = 0, callsWithoutEva = 0, unitVoicesWithoutHandler = 0;
	};
	static ApiCounters apiCounters();
	LiveGameAudio(const LiveGameAudio &) = delete;
	LiveGameAudio &operator=(const LiveGameAudio &) = delete;

	// once per render frame, after the logic advanced: the ambient sounds follow the objects' states
	void update();
	// lane SCRIPT-2: the audio of a script's client request (S-1182), on the manager of this attachment: PLAY_SOUND_EFFECT (RW 0x7BE0EA: the event by
	// name, 2D, for the local player), SPEECH_PLAY (RW 0x7BE2EC: the same, uninterruptable unless overlap is allowed), SOUND_PLAY_NAMED (RW 0x7BE239: at
	// the object, its owner's), PLAY_SOUND_EFFECT_AT (at the waypoint), MUSIC_SET_TRACK / MUSIC_PLAY_TRACK_FINITE_TIMES (the music track); an unknown
	// event is counted. Returns false for a request that is not audio
	bool applyScriptRequest(const ScriptClientRequest &r);
	struct ScriptAudioStats
	{
		std::uint64_t played = 0, unknownEvents = 0, volumes = 0; ///< volumes: lane SCRIPT-3 (the script volume actions)
		std::vector<std::string> lastEvents; ///< the last few event names played (reports)
	};
	const ScriptAudioStats &scriptAudioStats() const { return m_scriptAudio; }

	// the owner resolver of the manager (object / drawable positions; false: the owner is gone)
	bool objectPosition(std::uint32_t objectId, Coord3D *pos) const;
	bool drawablePosition(std::uint32_t drawableId, Coord3D *pos) const;

	// the ambient sound the object should have now ("" = none): ZH getAmbientSoundByDamage with the pristine fallback
	static std::string ambientSoundFor(const Object &obj);

private:
	// lane PERF-1 r2: ambientSoundFor per template (it reads nothing else); lookups only, never iterated
	const std::string &cachedAmbientSoundFor(const Object &obj);
	std::map<std::pair<const ThingTemplate *, int>, std::string> m_ambientByTemplate;
	// lane AUDIO-4: the members TheLargeGroupAudio's maps hold, from the LargeGroupAudioUpdate modules' calls (LiveGameAudio.cpp)
	struct LgaRuntime;
	std::unique_ptr<LgaRuntime> m_lga;

public:

	struct Stats
	{
		std::uint64_t ambientStarted = 0, ambientStopped = 0, ambientOutOfRange = 0, ambientUnknownEvent = 0;
		std::uint64_t largeGroupStarted = 0, largeGroupStopped = 0; ///< S-248: LargeGroupAudio cell sounds
		std::uint64_t largeGroupEvents = 0;  ///< lane AUDIO-4: the modules' add / update / remove calls applied
		size_t largeGroupMembers = 0;        ///< lane AUDIO-4: members held by the Sound blocks (a member counts once per block)
		size_t largeGroupPlaying = 0;   ///< qualifying cells whose sound is queued or playing
		size_t largeGroupQualified = 0; ///< cells above their thresholds (with or without a sound)
		std::uint64_t evaDamaged = 0, evaDeaths = 0; ///< S-709: the Eva events the body watch reported
		std::uint64_t battleAmbientTemplates = 0; ///< S-706: objects whose template has SoundAmbientBattle (not ported)
		size_t ambientPlaying = 0;
	};
	const Stats &stats() const { return m_stats; }
	// object id -> the ambient event playing for it (tests, the viewer)
	std::map<ObjectID, std::string> playingAmbients() const;
	// AUDIO-3: the manager's request log (AudioManager::enableEventLog) as tab separated lines with the requesting object's template resolved:
	// eventLogHeader() names the columns
	void enableEventLog(bool on);
	std::vector<std::string> takeEventLogLines();
	static const char *eventLogHeader();
	// enables the log and writes it to `path` (the header first), appending the new lines at every update(); false + *error when it cannot be opened
	bool openEventLogFile(const std::string &path, std::string *error);

	static std::vector<std::string> acceptanceStops();
	// SMOOTH-1 (S-814): runs the logic's audio calls the logic worker queued (AudioApi::drainDeferred), at a worker-idle point on the audio owner's thread
	static void drainDeferredAudio();

private:
	struct Ambient
	{
		std::string event;
		std::uint32_t handle = 0;
		bool tried = false; ///< started (or refused out of range) for `event`
	};
	void stopAmbient(ObjectID id, Ambient &a);
	struct EvaWatch
	{
		bool known = false, dead = false;
		float health = 0.0f;
	};
	void evaWatch(const Object &o);
	void largeGroupUpdate();
	struct Group
	{
		Coord3D position;
		std::uint32_t handle = 0;  ///< 0: no sound queued or playing
		bool startedOnce = false;  ///< a one-shot sound plays once per qualification
	};
public:
	// tests: the group sounds (key "map/sound/cellx,celly" -> position, handle)
	std::map<std::string, std::pair<Coord3D, std::uint32_t>> largeGroups() const
	{
		std::map<std::string, std::pair<Coord3D, std::uint32_t>> out;
		for (const auto &kv : m_groups)
		{
			out[kv.first] = std::make_pair(kv.second.position, kv.second.handle);
		}
		return out;
	}
	// tests: one LargeGroupAudio pass now (normally once per logic frame from update())
	void runLargeGroupAudio() { largeGroupUpdate(); }

private:
	std::map<std::string, Group> m_groups; ///< "map/sound/cellx,celly" -> its playing sound
	unsigned m_lgaFrame = 0xffffffffu;
	static std::string evaField(const Object &obj, const char *field);
	std::map<ObjectID, EvaWatch> m_eva;
	class Resolver; // the AudioOwnerResolver forwarding to objectPosition / drawablePosition

	LiveGame &m_game;
	AudioManager &m_audio;
	std::map<ObjectID, Ambient> m_ambients;
	std::map<ObjectID, bool> m_battleCounted;
	Stats m_stats;
	ScriptAudioStats m_scriptAudio;
	std::unique_ptr<Resolver> m_resolver;
	std::unique_ptr<MusicScripts> m_music;
	std::string m_musicError;
	unsigned m_musicFrame = 0xffffffffu;
	std::weak_ptr<const int> m_audioAlive; ///< AudioManager::lifetimeToken(): expired once the manager is destroyed
	std::FILE *m_logFile = nullptr;        ///< AUDIO-3: openEventLogFile
	void flushEventLog();
	bool audioAlive() const;
public:
	// tests: the attachment's manager still exists
	bool attachedManagerAlive() const { return audioAlive(); }
};
