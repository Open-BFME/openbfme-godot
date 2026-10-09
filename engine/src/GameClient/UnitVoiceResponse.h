// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// UnitVoiceResponse (lane AUDIO-2): the unit voice a selection or a command makes, ZH CommandXlat.cpp pickAndPlayUnitVoiceResponse as RotWK 2.01 has it.
// Client presentation only: it reads the logic (the selection's objects, their templates, positions, model conditions) and plays through the audio manager
// and Eva; it never changes simulation state. The per-object "recently said" memory retail keeps on the Object (RW obj + 0x384 / + 0x38C) lives here,
// keyed by object id, so the lockstep state stays untouched.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly of RW 0x8DEDBB and its helpers:
//   * the candidates: every selected object in selection order (ordinal 1, 2, ...); an object with a horde contain (RW obj + 0x258, slot 0x7C) gives its members
//     instead (all with the horde's ordinal); objects whose template is IGNORED_IN_GUI (tt + 0x10D bit 7, KindOf 47) are skipped; for the enter-state messages
//     (RW 0x8DD63C: 0x7E6, 0x7E7, 0x7E8, 0x7EC) a lone object with status HORDE_MEMBER (38) is skipped.
//   * per candidate a "choice" (RW 0x8DD819): two voice slots {Eva event id, audio event} (the template's voice and the CrowdResponse's), the object and the
//     candidate's rank {special, hero (tt + 0x110 bit 26, KindOf HERO), level, ordinal, VoicePriority (tt + 0x570, RW 0x675095)}.
//   * the voice rows are the audio table RW 0xC26720 by index (RW 0x676A8A -> template lookup); names (VoiceBombard, VoiceAttackUnit<target> ...) come from the
//     template's UnitSpecificSounds map (tt + 0x388, RW 0x73EDD0). A choice's slot is filled only while it is empty (RW 0x8DDA33): the first rule that finds a
//     voice wins.
//   * rank order RW 0x8DD663: special first; a hero beats a non hero; two heroes: the higher level, then the EARLIER ordinal; two non heroes: the higher VoicePriority
//     (equal priority = equal rank). A better rank clears the collected sounds (RW 0x8DE23A); an equal rank adds its sound under its primary sound
//     (RW 0x8DDD00 / 0x8DE522) and counts it (node + 0x30).
//   * the buckets are keyed by the primary audio event alone (RW 0x8DFF02, the info pointer); the most counted plays (a tie: retail's lowest info pointer,
//     here the name order, S-705; RW 0x8DFF8B); an enter-state message that would repeat a sound the object said within
//     AudioSettings MinDelayBetweenEnterStateVoiceMS (RW 0x6769D9, three slots per object) plays nothing; otherwise every candidate remembers it (RW 0x676A23) and, for
//     an attack whose rule set the charge flag (RW 0x8DDF3B sets choice + 0x20), its VoiceAttackChargeTimeout starts (RW 0x671F16: obj + 0x384 = frame + tt + 0x568).
//   * an Eva id is reported to TheEva at the object's position (RW 0x5DD9EE) when the object's player is the local player or for an enter-state message; the audio
//     event(s) play attached to the object (RW 0x8E00D2 .. 0x8E0121).
// INFERENCE / not ported: acceptance stops S-700 .. S-705 (docs/STOPS.md, acceptanceStops()).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameLogic;
class GameMessage;
class Object;
class ThingTemplate;

class UnitVoiceResponse
{
public:
	// The audio table rows RW 0xC26720 (index = the number RW 0x8DEDBB pushes; the name is the row's)
	enum VoiceRow
	{
		VOICE_SELECT = 0,
		VOICE_SELECT_UNDER_CONSTRUCTION = 1,
		VOICE_SELECT_BATTLE = 2,
		VOICE_MOVE = 3,
		VOICE_MOVE_TO_HIGHER_GROUND = 4,
		VOICE_MOVE_OVER_WALLS = 5,
		VOICE_ATTACK = 6,
		VOICE_ATTACK_CHARGE = 7,
		VOICE_FEAR = 8,
		VOICE_CREATED = 9,
		VOICE_TASK_COMPLETE = 10,
		VOICE_DEFECT = 11,
		VOICE_ATTACK_AIR = 12,
		VOICE_GUARD = 13,
		VOICE_ALERT = 14,
		VOICE_FULLY_CREATED = 15,
		VOICE_RETREAT_TO_CASTLE = 16,
		VOICE_MOVE_TO_CAMP = 17,
		VOICE_ATTACK_STRUCTURE = 18,
		VOICE_ATTACK_MACHINE = 19,
		VOICE_MOVE_WHILE_ATTACKING = 20,
		VOICE_COMBINE_WITH_HORDE = 21,
		VOICE_ENTER_STATE_ATTACK = 22, // 22 .. 32: the EnterState rows in table order
		VOICE_ROW_COUNT = 33
	};
	static const char *rowName(int row);

	// RotWK's client voice events: message types above the network range that only the voice picker switches on (RW 0x8DEDBB's switch)
	enum VoiceEvent
	{
		VOICE_EVENT_CREATED = 0x7DA,        ///< VoiceCreatedFrom<producer template>, else VoiceCreated
		VOICE_EVENT_TASK_COMPLETE = 0x7DE,
		VOICE_EVENT_DEFECT = 0x7DF,
		VOICE_EVENT_FEAR = 0x7E0,
		VOICE_EVENT_ALERT = 0x7E1,
		VOICE_EVENT_FULLY_CREATED = 0x7E2,  ///< VoiceFullyCreatedFrom<producer template>, else VoiceFullyCreated
		VOICE_EVENT_ENTER_STATE_ATTACK = 0x7E6,
		VOICE_EVENT_ENTER_STATE_MOVE = 0x7E7,
		VOICE_EVENT_ENTER_STATE_ATTACKMOVE = 0x7E8
	};

	// one voice slot: RW's {Eva event id, audio event}
	struct Voice
	{
		std::string eva;   ///< the Eva event name ("" = none)
		std::string sound; ///< the audio event name ("" = none)
		bool any() const { return !eva.empty() || !sound.empty(); }
	};

	// what the command knows besides the selection (ZH PickAndPlayInfo): the target object, the destination, the air flag of an attack
	struct Info
	{
		ObjectID target = INVALID_ID;
		bool hasPosition = false;
		Coord3D position;
		bool air = false;
		ObjectID producer = INVALID_ID; ///< VOICE_EVENT_CREATED / FULLY_CREATED: the object that made the unit
	};

	// The client services (the device layer implements them). playSound: the audio event attached to the object (AudioApi::playSoundForObject); reportEva:
	// TheEva's event at a position; soundExists: TheAudio->isValidAudioEvent (RW vtable + 0x7C).
	class Sink
	{
	public:
		virtual ~Sink() = default;
		virtual bool soundExists(const std::string &name) = 0;
		virtual void playSound(const std::string &name, ObjectID object, int playerIndex) = 0;
		virtual void reportEva(const std::string &eventName, const Coord3D *position) = 0;
	};

	UnitVoiceResponse(GameLogic &logic, Sink &sink);

	void setLocalPlayerIndex(int index) { m_localPlayer = index; }
	// AudioSettings MinDelayBetweenEnterStateVoiceMS converted by parseDurationUnsignedInt (frames; AudioSettings::minDelayBetweenEnterStateVoiceFrames)
	void setMinDelayBetweenEnterStateVoiceFrames(unsigned frames) { m_minDelayEnterStateFrames = frames; }

	// RW 0x8DEDBB: picks the voice of `selection` (object ids, the client's selection order) for message `msgType` and plays it. true when something played.
	bool pickAndPlay(const std::vector<ObjectID> &selection, int msgType, const Info *info = nullptr);
	// the voice-bearing message types this port handles (the HUD calls pickAndPlay for these)
	static bool handles(int msgType);

	// the template's voice of `row` / of the UnitSpecificSounds name (RW 0x676A8A / 0x674FFE without the draw module overrides, S-701)
	static Voice templateVoice(const ThingTemplate &tt, int row);
	static Voice unitSpecificVoice(const ThingTemplate &tt, const std::string &name);

	struct Stats
	{
		std::uint64_t calls = 0, played = 0, silent = 0, evaReports = 0, soundsPlayed = 0, repeatSuppressed = 0;
		std::uint64_t crowdResponseSkipped = 0;   ///< S-700: candidates whose CrowdResponseKey names no loaded CrowdResponse
		std::uint64_t crowdSounds = 0;            ///< the crowd's extra sounds played
		std::uint64_t unportedMessages = 0;       ///< S-703: voice message types this port does not answer
		std::uint64_t unknownSounds = 0;          ///< voices whose audio event is not valid (TheAudio says no: retail plays nothing for them either)
	};
	const Stats &stats() const { return m_stats; }
	// what the last pickAndPlay chose (tests): the object, the voice, the message
	struct Last
	{
		ObjectID object = INVALID_ID;
		Voice voice;
		int msgType = 0;
		int count = 0;
	};
	const Last &last() const { return m_last; }

	static std::vector<std::string> acceptanceStops();

private:
	struct Rank
	{
		int special = 0;
		bool hero = false;
		int level = (int)0x80000000;
		int ordinal = 0x7fffffff;
		int priority = (int)0x80000000;
	};
	static bool rankLess(const Rank &a, const Rank &b);
public:
	// CrowdResponse.ini (S-700): per name a Weight (default 100) and Threshold entries, each with voice rows and UnitSpecificSounds names (the voice
	// parser of the template rows)
	struct CrowdThreshold
	{
		int threshold = 0;
		std::map<std::string, std::string> voices; ///< row or UnitSpecificSounds name -> the raw value (NoSound / EVA: / +SOUND: / event)
	};
	struct CrowdResponse
	{
		std::string name;
		int weight = 100;
		std::vector<CrowdThreshold> thresholds;
	};
	// parses CrowdResponse.ini text (the blocks EA documents at the top of the file); false + *error on a malformed block
	bool parseCrowdResponses(const std::string &text, std::string *error);
	const std::map<std::string, CrowdResponse> &crowdResponses() const { return m_crowd; }

private:
	struct Choice
	{
		Voice voice;
		Voice crowd;                          ///< RW choice + 8: the crowd response's voice
		const CrowdThreshold *crowdEntry = nullptr; ///< RW choice + 0x18
		bool charge = false; ///< RW choice + 0x20
	};
	struct Collected
	{
		Voice voice;
		Voice crowd;
		Object *object = nullptr;
		int count = 0;
		bool charge = false;
	};
	struct Memory
	{
		std::string said[3];
		unsigned frame[3] = { 0, 0, 0 };
		bool used[3] = { false, false, false };
		unsigned chargeUntil = 0; ///< RW obj + 0x384
	};

	bool fill(Choice &c, const Voice &v) const;
	// RW 0x8DDA66 / 0x8DDB2A: the template's voice of a row / name into slot 0, the crowd entry's of the same row / name into slot 1 (each only while empty)
	bool fillRow(Choice &c, const ThingTemplate &tt, int row) const;
	bool fillName(Choice &c, const ThingTemplate &tt, const std::string &name) const;
	// RW 0x8DD876: no further rule is needed (an Eva id, or a sound and, with a crowd entry, its crowd sound)
	static bool done(const Choice &c);
	const CrowdThreshold *pickCrowd(const std::vector<std::pair<Object *, int>> &candidates) const;
	void selectVoices(Object &obj, int msgType, const Info *info, Choice &c, Rank &rank) const;
	void moveVoices(Object &obj, int msgType, const Info *info, Choice &c, Rank &rank) const;
	void attackVoices(Object &obj, int msgType, const Info *info, Choice &c) const;
	bool inBattle(const Object &obj) const;
	bool recentlySaid(ObjectID id, const std::string &key) const;
	void rememberSaid(ObjectID id, const std::string &key);

	GameLogic &m_logic;
	Sink &m_sink;
	int m_localPlayer = -1;
	unsigned m_minDelayEnterStateFrames = 0;
	std::map<ObjectID, Memory> m_memory;
	std::map<std::string, CrowdResponse> m_crowd;
	Stats m_stats;
	Last m_last;
};

// The production sink: the installed audio manager and Eva (Common/Audio/AudioEntryPoints.h). Without an installed manager the calls are counted by AudioApi
// (callsWithoutAudio / callsWithoutEva), never silently dropped.
class AudioApiVoiceSink : public UnitVoiceResponse::Sink
{
public:
	bool soundExists(const std::string &name) override;
	void playSound(const std::string &name, ObjectID object, int playerIndex) override;
	void reportEva(const std::string &eventName, const Coord3D *position) override;
};

// The HUD's voice: what HudInput's logic-message observer calls (the message's target object and location become the Info) with the client selection.
void PlayUnitVoiceForMessage(UnitVoiceResponse &voice, const std::vector<ObjectID> &selection, const GameMessage &msg);

