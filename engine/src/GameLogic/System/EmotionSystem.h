// OpenBFME. GPL-3.0.
//
// TheEmotionSystem (lane MODULES-2): the EmotionNugget INI blocks, the per-object emotion nuggets the EmotionTrackerUpdate runs, and the list of scary and
// hero objects the fear and hero scans walk. RotWK only (ZH has no emotions); ported from the binary (caveat S-001), every step cites its address.
//
// TARGET FACTS (RotWK game.dat):
//   * TheEmotionSystem (RW global 0xDE8C88): + 0xC .. + 0x10 the nugget templates in definition order (lookup by name RW 0x835C70: the first equal name),
//     + 0x24 .. + 0x28 the ids of the SCARY or HERO objects (KindOf 144 / 90): Object::initObject adds the id unless present (RW 0x693E8E -> 0x835D55,
//     push_back), Object::onDie (RW 0x6990C1) and ~Object (RW 0x69A81A) remove it (RW 0x835B0A: the last id takes its place, then the list shrinks).
//   * block `EmotionNugget <name>` (RW 0x8E0D59): an unknown name makes a new nugget (400 bytes, defaults RW 0x8E0B95) appended to the list; a known name
//     is parsed into a temporary that is thrown away (the first definition stays), except on a RELOAD (load type 5), which replaces it.
//   * the field table RW 0xC77FD0: CopyFrom (RW 0x8E0CA9: every field but the name from the named nugget, RW 0x8E0A54; "Emotion nugget to copy from not
//     found." otherwise), Type (+4, RW 0x8E09CE: the name in RW 0xD9FA08, -1 for an unknown name), IgnoreIfUnitIdle (+8) / IgnoreIfUnitBusy (+9) bool,
//     Duration (+0xC), InactiveDuration (+0x10), InactiveDurationSameObject (+0x14), InactiveDurationSameType (+0x18) parseDurationUnsignedInt,
//     OnlyIfEnemyThreatAbove / Below (+0x1C: RW 0x8E0906 stores (-1, value), RW 0x8E08DC (1, -value)), OnlyIfFriendThreatAbove / Below (+0x24, the same),
//     IgnoreIfAI (+0x2C) / IgnoreIfHuman (+0x2D), StartFXList / UpdateFXList / EndFXList (+0x30 / 0x34 / 0x38, parseFXList), AttributeModifier (+0x3C),
//     AttributeStartDelay (+0x40), AttributeModifierWhileEmotionActive (+0x44), AttributeDuration (+0x48), AIState (+0x4C, RW 0x8E0A11: the name in RW
//     0xD9F9EC, -1 unknown), AILockDuration (+0x50), ModelConditions (+0x54, RW 0x8E092B: the flags are ALSO copied to ModelConditionsClearOnExit +0xA0),
//     ModelConditionsClear (+0xEC, copied to ModelConditionsSetOnExit +0x138), ModelConditionsSetOnExit (+0x138) / ModelConditionsClearOnExit (+0xA0) (RW
//     0x4B8C21), PreventPlayerCommands (+0x184), LuaEvent (+0x188). Defaults: Type, AIState, AttributeDuration -1, the threat pairs (0, -1).
//   * the per-object nugget (RW 0x8E1619, 0x34 bytes): + 0 the object, + 4 the template, + 8 the source id, + 0xC the source's template id (+ 0x5E8),
//     + 0x10 inactive until, + 0x14 the per-source map (object id -> frame), + 0x20 the per-source-template map, + 0x2C the end frame, + 0x30 the start.
//     canApply RW 0x8E12F5, start RW 0x8E0ED7, update RW 0x8E11A1, stop RW 0x8E168A (EmotionNugget below).
//
// The nugget's AIState runs on the AI (lane MODULES-3: RW 0x662FC8 / 0x663053 / 0x66309C -> AIUpdateInterface::emotionEnterAIState / emotionLockElapsed /
// emotionLeaveAIState, the states of GameLogic/AI/AIEmotionStates.h) and PreventPlayerCommands sets / clears AI + 0x3C5 (RW 0x8E0FA9 / 0x8E178B).
// NOT PORTED / INFERENCE (stop S-1021, EmotionNugget::stopLines): the LuaEvent is counted (the script event dispatch of RW 0x735F53); RW 0x70C55D / object + 0x124 bit 8
// (set at a start with a source) and + 0x125 bit 0 (cleared at a stop) are not identified (counted); the member a horde's fear resistance is read from is the
// first contained object (horde interface slot 0x110, as HERO-1 reads it).

#pragma once

#include "Common/INI.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
class INIBlockRegistry;
class Object;
class StateHasher;

// RW 0xD9FA08
enum EmotionType
{
	EMOTION_TAUNT = 0,
	EMOTION_CHEER = 1,
	EMOTION_HERO_CHEER = 2,
	EMOTION_POINT = 3,
	EMOTION_FEAR = 4,
	EMOTION_UNCONTROLLABLE_FEAR = 5,
	EMOTION_TERROR = 6,
	EMOTION_DOOM = 7,
	EMOTION_QUARRELSOME = 8,
	EMOTION_ALERT = 9,
	EMOTION_BRACE_FOR_BEING_CRUSHED = 10,
	EMOTION_CHEER_FOR_ABOUT_TO_CRUSH = 11,
	EMOTION_TYPE_COUNT = 12
};

// RW 0xD9F9EC
enum EmotionAIState
{
	EMOTION_AI_BACK_AWAY = 0,
	EMOTION_AI_AVOID_SCARER = 1,
	EMOTION_AI_IDLE = 2,
	EMOTION_AI_RUN_AWAY_PANIC = 3,
	EMOTION_AI_FACE_OBJECT = 4,
	EMOTION_AI_QUARREL = 5
};

// the 400 byte nugget template (RW 0x8E0B95)
struct EmotionNuggetTemplate
{
	typedef std::array<std::uint32_t, 19> Flags;
	std::string name;                          ///< + 0
	int type = -1;                             ///< + 4
	bool ignoreIfUnitIdle = false;             ///< + 8
	bool ignoreIfUnitBusy = false;             ///< + 9
	unsigned duration = 0;                     ///< + 0xC (frames)
	unsigned inactiveDuration = 0;             ///< + 0x10
	unsigned inactiveDurationSameObject = 0;   ///< + 0x14
	unsigned inactiveDurationSameType = 0;     ///< + 0x18
	int enemyThreatScale = 0, enemyThreatOffset = -1;   ///< + 0x1C / + 0x20
	int friendThreatScale = 0, friendThreatOffset = -1; ///< + 0x24 / + 0x28
	bool ignoreIfAI = false, ignoreIfHuman = false;     ///< + 0x2C / + 0x2D
	std::string startFX, updateFX, endFX;      ///< + 0x30 / + 0x34 / + 0x38 (empty: none)
	std::string attributeModifier;             ///< + 0x3C
	unsigned attributeStartDelay = 0;          ///< + 0x40
	bool attributeModifierWhileActive = false; ///< + 0x44
	int attributeDuration = -1;                ///< + 0x48
	int aiState = -1;                          ///< + 0x4C
	unsigned aiLockDuration = 0;               ///< + 0x50
	Flags modelConditions{};                   ///< + 0x54 set at the start
	Flags modelConditionsClearOnExit{};        ///< + 0xA0 cleared at the stop
	Flags modelConditionsClear{};              ///< + 0xEC cleared at the start
	Flags modelConditionsSetOnExit{};          ///< + 0x138 set at the stop
	bool preventPlayerCommands = false;        ///< + 0x184
	std::string luaEvent;                      ///< + 0x188
	bool overridden = false;                   ///< + 0x18D: an AddEmotion OVERRIDE copy (the module keeps it; otherwise the system's template is used)

	// RW 0x8E0A54: every field but the name (and the override flag)
	void copyFrom(const EmotionNuggetTemplate &o);
	static const FieldParse *fieldParse(); ///< RW 0xC77FD0
};

class EmotionSystem
{
public:
	void registerBlock(INIBlockRegistry &registry);
	void parseEmotionNugget(INI *ini); ///< RW 0x8E0D59
	const EmotionNuggetTemplate *find(const std::string &name) const; ///< RW 0x835C70
	const std::vector<std::unique_ptr<EmotionNuggetTemplate>> &nuggets() const { return m_nuggets; }

private:
	std::vector<std::unique_ptr<EmotionNuggetTemplate>> m_nuggets; ///< + 0xC, definition order
};

// the store the EmotionNugget block and the modules use (RetailObjectWorld installs its own); nullptr when none
extern thread_local EmotionSystem *TheEmotionSystem;

// the per-object nugget (RW 0x8E1619)
class EmotionNugget
{
public:
	EmotionNugget(Object &object, const EmotionNuggetTemplate &tmpl) : m_object(&object), m_template(&tmpl) {}
	const EmotionNuggetTemplate &tmpl() const { return *m_template; }
	int type() const { return m_template->type; }
	ObjectID sourceID() const { return m_sourceID; }
	UnsignedInt endFrame() const { return m_endFrame; }
	UnsignedInt startFrame() const { return m_startFrame; }

	bool canApply(int enemyThreat, unsigned friendThreat, Object *source); ///< RW 0x8E12F5
	void start(Object *source, int lockFrames);                             ///< RW 0x8E0ED7
	bool update();                                                          ///< RW 0x8E11A1: false when it ends
	void stop();                                                            ///< RW 0x8E168A
	void crc(StateHasher &h) const;
	static std::vector<std::string> stopLines();

private:
	Object *fxObject() const;                  ///< the horde's interface slot 0x4C object, else the object (RW 0x8E0F2A)
	void applyConditions(const EmotionNuggetTemplate::Flags &clear, const EmotionNuggetTemplate::Flags &set); ///< RW 0x68D607 / 0x6944A5
	void emitFX(const std::string &fx, const char *site, Object *other);    ///< RW 0x4B1B5A

	Object *m_object;                          ///< + 0
	const EmotionNuggetTemplate *m_template;   ///< + 4
	ObjectID m_sourceID = INVALID_ID;          ///< + 8
	unsigned short m_sourceTemplateID = 0;     ///< + 0xC
	UnsignedInt m_inactiveUntil = 0;           ///< + 0x10
	std::map<ObjectID, UnsignedInt> m_sameObject;         ///< + 0x14 (looked up, never iterated for logic)
	std::map<unsigned short, UnsignedInt> m_sameType;     ///< + 0x20
	UnsignedInt m_endFrame = 0;                ///< + 0x2C
	UnsignedInt m_startFrame = 0;              ///< + 0x30
};

// the game's side of TheEmotionSystem: the scary / hero object ids (RW + 0x24) and the counters of the unported parts (lane MODULES-2)
class EmotionWorld
{
public:
	explicit EmotionWorld(GameLogic &logic) : m_logic(logic) {}
	void objectInitialized(Object &obj); ///< RW 0x693E79 .. 0x693E95
	void objectDied(Object &obj);        ///< RW 0x6990AC .. 0x6990C8 / 0x69A808 .. 0x69A821
	const std::vector<ObjectID> &scaryOrHeroes() const { return m_ids; }
	void reset() { m_ids.clear(); m_unportedAI = m_unportedLua = m_unportedFlags = m_tauntScans = 0; }

	// the unported parts, counted where retail would act (reported by stopLines' S-1021); since lane MODULES-3 nothing counts AI calls (0)
	void countUnportedAI() { ++m_unportedAI; }
	void countUnportedLua() { ++m_unportedLua; }
	void countUnportedFlags() { ++m_unportedFlags; }
	void countTauntScan() { ++m_tauntScans; } // lane MODULES-3: the taunt / point / alert scans that ran (RW 0x8B58B1; a ported scan, not an unported part)
	unsigned long long unportedAI() const { return m_unportedAI; }
	unsigned long long tauntScans() const { return m_tauntScans; }

	void crc(StateHasher &h) const;
	std::vector<std::string> report() const;

private:
	GameLogic &m_logic;
	std::vector<ObjectID> m_ids;
	unsigned long long m_unportedAI = 0, m_unportedLua = 0, m_unportedFlags = 0, m_tauntScans = 0;
};
