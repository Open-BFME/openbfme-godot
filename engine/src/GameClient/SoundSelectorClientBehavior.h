// OpenBFME. GPL-3.0.
//
// The sound selector ClientBehavior modules (lane DECOMP-2, stop S-701): ModelConditionSoundSelectorClientBehavior, UpgradeSoundSelectorClientBehavior and
// RandomSoundSelectorClientBehavior, the drawable modules that override a unit's voice rows, its UnitSpecificSounds names and its VoicePriority (Theoden's mounted
// voices, a Gondor wall's selection sound per upgrade, the peasants' voice priority). Client presentation: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * Drawable + 0x154 is the drawable's CLIENT_BEHAVIOR module list (the drawable's module construction RW 0x679F80 .. 0x67A248 fills + 0x14C / + 0x150 / + 0x154 with
//     the module types 1 / 2 / 3). RW 0x676A8A (a voice or sound row by index), RW 0x674FFE (a UnitSpecificSounds name) and RW 0x675095 (VoicePriority) walk that list
//     in order and ask each module's vslot 0x34 for its selector interface (null for every other class: RW 0x851E97); the first interface whose slot 0 / 1 / 2
//     answers true gives the record; else the template's (RW 0x645A89 / its name lookup; VoicePriority: template + 0x570). Of the registered classes only these three
//     answer (vslot 0x34 RW 0x4C9323: the interface at module + 0xC).
//   * a record is RW's {Eva event id, audio event} (8 bytes). The rows are the audio table RW 0xC26720 (56 rows, VoiceSelect .. SoundCrushing: voice rows parsed by
//     RW 0x73ACEF, sound rows by RW 0x73AD07, both through RW 0x73AB45: "NoSound" clears both halves, `EVA:<event>` sets the Eva half (an unknown event: INIException 3
//     "Unknown EVA event in EVA:%s"; a sound row refuses it: "This is not a valid place to use the EVA: sound syntax: %s"), `+SOUND:<event>` sets the sound and keeps the
//     Eva half, any other value sets the sound and clears the Eva half; an audio event TheAudio does not know: INIException 3 "Invalid Sound '%s'").
//     UnitSpecificSounds (RW 0x73F684: the map is cleared, then each line `Name = <value>` of the block, table RW 0xC2728C's catch-all row RW 0x73F5CD, parses the
//     value as a voice row into map[Name]).
//   * ModelConditionSoundSelectorClientBehavior (create RW 0x6559D4, data RW 0x6529B4, table RW 0xC75D80): SoundState <model conditions> (RW 0x8CF47D: the header's
//     flags RW 0x4B8B37, then table RW 0xC75D44 = UnitSpecificSounds / VoicePriority (RW 0x8CEF70: parseInt and the "set" byte) plus the audio rows) appends a
//     0x220-byte entry. Slot 0 (RW 0x8CF065 -> 0x8CF01B): the first entry whose flags are all set in the drawable's model condition flags (Drawable + 0x258,
//     RW 0x4CE8CA) AND whose row has an audio event; slot 1 (RW 0x8CF081): the first passing entry whose map has the name (the record as stored, even without an
//     audio event); slot 2 (RW 0x8CEFC1): the first passing entry with VoicePriority set.
//   * UpgradeSoundSelectorClientBehavior (create RW 0x654979, data RW 0x652A05, table RW 0xC75F0C): SoundUpgrade <upgrade> ... (RW 0x8CFF8F -> 0x8CF9AD: the header's
//     names are required upgrades, then table RW 0xC75E20 = UnitSpecificSounds, ExcludedUpgrades (parseAsciiStringVectorAppend), VoicePriority (RW 0x8CF53A),
//     RequiredModelConditions / ExcludedModelConditions (RW 0x4B8C21) plus the audio rows; with TheUpgradeCenter the names become masks at once (RW 0x8CF5BF: a
//     name that is no Upgrade: INIException 3 "An upgrade module references %s, which is not an Upgrade"), and the model condition sets record whether they are
//     non-empty) appends a 0x3A4-byte entry. An entry applies to the drawable's object (none: never; RW 0x8CF769) when, with model conditions, the object's
//     drawable exists and has every required and none of the excluded flags (RW 0x5DF56A), and the object has every required upgrade and none of the excluded ones
//     (Object::hasUpgrade by bit, RW 0x68E040: a horde answers for its members). Slot 0 / 1 (RW 0x8CF833 / 0x8CF8C5): the first applying entry whose record (by
//     row; by name: the map's or an empty one) has an audio event; slot 2 (RW 0x8CF957): the first applying entry with VoicePriority set.
//   * RandomSoundSelectorClientBehavior (create RW 0x652ACB, data RW 0x657640 -> 0x657572, table RW 0xC75F48 plus the audio rows at data + 8): Chance (+ 0x1D4,
//     parsePercentToReal, default 0.5), RerollOnEveryFrame (+ 0x1DD, default yes), UnitSpecificSounds (+ 0x1C8), VoicePriority (+ 0x1D8, RW 0x8D000E). The module
//     (ctor RW 0x8D0066) draws GameClientRandomValueReal(0, 1) (RW 0x6D33AB, RandomSoundSelectorClientBehavior.cpp line 0x62) and stores TheGameClient's frame;
//     before each answer (RW 0x8D0138) it draws again (line 0x74) when RerollOnEveryFrame is set and the client frame changed. The module answers only while
//     roll < Chance (comiss / jb): slot 0 (RW 0x8D0215) the row with an audio event, slot 1 (RW 0x8D0277) a name of the map, slot 2 (RW 0x8D018A) VoicePriority
//     (the value, true only when it was set).
// INFERENCE / not ported (stop S-701, UnitVoiceResponse::acceptanceStops): the drawable's model condition flags are read from the object's (the logic pushes them
// to the drawable); the random roll is drawn when the voice picker first asks for the object (retail: at the drawable's creation) from the voice picker's own
// client random generator (S-093), and "the client frame" is the logic frame of the request; the move / ambient sounds of the logic's move state and the audio
// side (SoundMoveStart ..., S-712) do not ask the selectors.

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ModuleFactory;
class Object;
class ThingTemplate;

namespace SoundSelector
{
enum
{
	AUDIO_ROW_COUNT = 56 ///< the rows of the audio table RW 0xC26720
};
// the row names in RW 0xC26720's order (VoiceSelect = 0 .. SoundCrushing = 55)
const char *rowName(int row);
int rowIndex(const std::string &name); ///< -1 when the name is not a row

// RW's 8-byte record {Eva event id, audio event}, by name ("" = none)
struct Record
{
	std::string eva, sound;
};

// the sound part every selector entry / module carries: the audio rows, the UnitSpecificSounds map, VoicePriority
struct SoundSet
{
	std::array<Record, AUDIO_ROW_COUNT> rows;
	std::map<std::string, Record> unitSpecific;
	int voicePriority = 0;
	bool hasVoicePriority = false;
};

typedef std::array<std::uint32_t, 19> ConditionWords; ///< model condition flags (Drawable + 0x258 / Object + 0x10C)

// what a selector is asked about: the object, its drawable's model condition flags (null: the object has no drawable), the client frame, the roll source
struct Query
{
	const Object *object = nullptr;
	const ConditionWords *drawableFlags = nullptr;
	unsigned clientFrame = 0;
};
} // namespace SoundSelector

class ModelConditionSoundSelectorClientBehaviorModuleData : public ModuleData
{
public:
	struct Entry
	{
		SoundSelector::ConditionWords conditions{}; ///< + 0 (the SoundState header)
		SoundSelector::SoundSet sounds;              ///< + 0x4C rows, + 0x20C map, + 0x218 / + 0x21C VoicePriority
	};
	std::vector<Entry> m_entries; ///< + 8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class UpgradeSoundSelectorClientBehaviorModuleData : public ModuleData
{
public:
	struct Entry
	{
		std::vector<std::string> requiredNames, excludedNames; ///< + 0x120 / + 0x12C (cleared once resolved)
		std::vector<int> requiredBits, excludedBits;           ///< + 0 / + 0x90 (the masks, as bit lists)
		bool resolved = false;
		SoundSelector::ConditionWords requiredConditions{}, excludedConditions{}; ///< + 0x138 / + 0x184
		bool hasRequiredConditions = false, hasExcludedConditions = false;        ///< + 0x3A1 / + 0x3A2
		SoundSelector::SoundSet sounds;                                           ///< + 0x1D0 rows, + 0x390 map, + 0x39C / + 0x3A0 VoicePriority
	};
	std::vector<Entry> m_entries; ///< + 8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RandomSoundSelectorClientBehaviorModuleData : public ModuleData
{
public:
	SoundSelector::SoundSet m_sounds; ///< + 8 rows, + 0x1C8 map, + 0x1D8 / + 0x1DC VoicePriority
	float m_chance = 0.5f;            ///< + 0x1D4 (RW 0x6575A4: 0.5, RW 0xBD869C)
	bool m_rerollOnEveryFrame = true; ///< + 0x1DD
	static void buildFieldParse(MultiIniFieldParse &p);
};

// the drawable's module of each class: it only carries its data (the voice picker asks the template's ClientBehavior list, SoundSelectors below)
class SoundSelectorClientBehavior : public DrawableModule
{
public:
	SoundSelectorClientBehavior(Thing *thing, const ModuleData *data) : DrawableModule(thing, data) {}
	static void registerClasses(ModuleFactory &modules);
};

// The selectors of a template's ClientBehavior list, asked in list order (RW 0x676A8A / 0x674FFE / 0x675095). The random modules' state lives with the caller
// (one per object, see the file comment): `RandomRoll` draws GameClientRandomValueReal(0, 1).
class SoundSelectors
{
public:
	struct RandomState
	{
		bool made = false;
		float roll = 0.0f;   ///< module + 0x10
		unsigned frame = 0;  ///< module + 0x14
	};
	typedef std::vector<RandomState> RandomStates; ///< one per RandomSoundSelectorClientBehavior of the list, in list order
	class RandomRoll
	{
	public:
		virtual ~RandomRoll() = default;
		virtual float roll() = 0;
	};

	// true and *out when a selector answers (else the caller asks the template)
	static bool row(const ThingTemplate &tt, const SoundSelector::Query &q, int row, RandomStates &rs, RandomRoll &rnd, SoundSelector::Record *out);
	static bool name(const ThingTemplate &tt, const SoundSelector::Query &q, const std::string &name, RandomStates &rs, RandomRoll &rnd, SoundSelector::Record *out);
	static bool voicePriority(const ThingTemplate &tt, const SoundSelector::Query &q, RandomStates &rs, RandomRoll &rnd, int *out);
	// whether the template has any selector (the picker keeps no state for the others)
	static bool any(const ThingTemplate &tt);
};
