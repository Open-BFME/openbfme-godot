// OpenBFME. GPL-3.0.
// Ported from RotWK 2.01 (game.dat) with the BFME2 decomp as the shape (decomp-progress bfme2 Code/GameEngine/Source/GameLogic/Object/Behavior/
// ModelConditionAudioLoopClientBehavior{,Ctor,Dtor,Slots,Update}.cpp, Object/Update/ModelConditionAudioLoopClientBehavior.cpp). ZH has no such class.
//
// ModelConditionAudioLoopClientBehavior: a ClientBehavior module (ModuleFactory name string RW 0xC0A5CC, registered at RW 0x65BBF2 with the create procs
// RW 0x652B03 / data RW 0x652B3B, type 3, interface mask 0x1000; module vtable RW 0xC76088) that keeps one sound playing while its drawable's model condition
// flags select it: the units' taunts, cheers and pointing voices (EMOTION_TAUNTING / EMOTION_CELEBRATING / EMOTION_POINTING / RAISING_FLAG), a structure's
// collapse sound (RUBBLE). Client presentation: no simulation state, nothing in the hash or the save (the vtable's crc / loadPostProcess are RW 0x63F3BF;
// slot +0x0C is the shared empty ClientBehavior::xfer RW 0x008CE37F: DrawableModule::xfer 0x7082E2 + xferVersion(1) 0x404F15, no own fields; getModuleName
// RW 0x008D0380, getModuleNameKey RW 0x008D0386, scalar deleting dtor RW 0x008D05D7 -> ~ModelConditionAudioLoopClientBehavior RW 0x008D055C).
//
// Editions: both RotWK 2.01 and BFME2 1.06 (the class is in both).
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data (size 0x14, ctor RW 0x8D0668, vtable RW 0xC760D8, field table RW 0xC761A4): ModelCondition (RW 0x8D07CF) appends an entry to the vector at + 8.
//   * an entry (0x9C bytes, ctor RW 0x8D0323): + 0 the sound (a reference to TheAudio's event, null for none), + 4 the required flags, + 0x50 the excluded flags.
//   * a ModelCondition line (RW 0x8D07CF, tokens split by " \n\r\t=:", RW 0x42CB02), repeated in any order until the line ends: `Required` (_strcmpi) or `Excluded`
//     then flag names (RW 0x4B5DA2) while they are flags; the first token that is no flag is read as the next keyword. `Sound` then a token that must exist (RW
//     0x42DC9F) and is looked up (TheAudio vslot 0x12C): none: INIException 3 "Unknown sound %s". Any other keyword: INIException 5 "Expected one of Required, Excluded,
//     or Sound next". `Sound:NoSound` skips the lookup and leaves "NoSound" as the next keyword (RW 0x8D08FB -> 0x8D0964), so retail refuses it with that same exception.
//   * the selection (RW 0x8D02D9): the first entry whose required flags are all set and whose excluded flags are all clear (RW 0x5DF56A) gives its sound, even
//     when it has none; no entry: none.
//   * module (size 0x1C, ctor RW 0x8D033D): + 0x14 the handle = 1 (AHSV_NoSound), + 0x18 the sound it holds = none. The ctor does not select.
//   * the change (RW 0x8D0425): the selection for the flags given; the same sound (the event reference): nothing. Else the clear below, the sound is stored and,
//     when there is one, an AudioEventRTS for the drawable (RW 0x6DB62D, owner type 1) is added (TheAudio vslot 0x64) and its handle kept.
//   * the clear (RW 0x8D03F8): with TheAudio and a handle >= 5: removed (TheAudio vslot 0x6C), handle = 1, the held sound dropped (RW 0x4A86CB). A lower handle
//     keeps the held sound. The destructor runs it (RW 0x8D055C).
//   * the second interface + 0xC (vtable RW 0xC76080, given by vslot 0x30 RW 0x4C9323): slot 0 (RW 0x8D04D9) the change with the drawable's flags (+ 0x258), slot 1
//     (RW 0x8D04EB) the clear. These are the audible flag's changes (+ 0x44A: RW 0x674727 / 0x67896F, lane PORT-21 through Drawable::setAudible),
//     as AnimationSoundClientBehavior's.
//   * the third interface + 0x10 (vtable RW 0xC7607C, given by vslots 0x24 / 0x28 RW 0x4CE965): slot 0 (RW 0x8D04CA) the change with the flags it is handed. The
//     drawable's flush (RW 0x67449C) calls it for every entry of its list + 0x158, after the dependency flags (RW 0x671F84).
// WHAT IS INFERENCE (OpenBFME, stop S-6024):
//   * retail's TheAudio exists before any drawable and for the whole game; the port's audio attaches later and can go. The module asks the attachment
//     (AnimationSoundModuleManager::audio). A change without audio holds the sound and plays nothing; when audio arrives a module that has played nothing on it selects
//     again from the drawable's flags, so a loop that should be playing starts. A detach forgets the handles: LiveGameAudio's teardown stops its own sounds.
//   * a loaded game: retail keeps no module state (the xfer is empty) and how its load flushes the drawable was not read. The port's load makes the flags reach the
//     drawable as for a new one, so a selected loop starts again.

#pragma once

#include "Common/Audio/AudioEventRTS.h"
#include "Common/INI.h"
#include "Common/ModelState.h"
#include "Common/Module.h"

#include <cstdint>
#include <string>
#include <vector>

class AnimationSoundModuleManager;
class AudioManager;
class ModuleFactory;

struct ModelConditionAudioLoopEntry
{
	std::string sound;              ///< + 0, empty for none
	ModelConditionFlags required;   ///< + 4
	ModelConditionFlags excluded;   ///< + 0x50
};

class ModelConditionAudioLoopClientBehaviorModuleData : public ModuleData
{
public:
	std::vector<ModelConditionAudioLoopEntry> m_entries; ///< + 8
	static void buildFieldParse(MultiIniFieldParse &p);
	static void parseModelCondition(INI *ini, void *instance, void *store, const void *userData); // RW 0x8D07CF
	const std::string &selectSound(const ModelConditionFlags &flags) const;                       // RW 0x8D02D9
};

class ModelConditionAudioLoopClientBehavior : public DrawableModule
{
public:
	ModelConditionAudioLoopClientBehavior(Thing *thing, const ModelConditionAudioLoopClientBehaviorModuleData *data); // RW 0x8D033D
	~ModelConditionAudioLoopClientBehavior() override;                                                                 // RW 0x8D055C

	void modelConditionFlagsChanged(const ModelConditionFlags &flags); // the third interface's slot 0 (RW 0x8D04CA -> 0x8D0425)
	// the second interface's slot 0 (RW 0x8D04D9) and slot 1 (RW 0x8D04EB -> 0x8D03F8): the drawable's audible flag changed (RW 0x674727 / 0x67896F).
	// No Drawable asks for them yet (stop S-6024: the audible flag's mechanism is not ported); the audio's arrival (audioAttached) calls the first.
	void becameAudible();
	void becameInaudible();

	// the audio the manager plays on changed (stop S-6024); `audioGone`: the attachment is going and stops its own sounds
	void audioAttached();
	void audioGone();

	const ModelConditionAudioLoopClientBehaviorModuleData *data() const { return m_data; }
	AudioHandle handle() const { return m_handle; }
	const std::string &heldSound() const { return m_held; }
	std::size_t managerSlot() const { return m_managerSlot; }
	void setManagerSlot(std::size_t slot) { m_managerSlot = slot; }

	static void registerClass(ModuleFactory &modules);

private:
	void clear(); // RW 0x8D03F8
	AudioManager *audio() const;
	const ModelConditionAudioLoopClientBehaviorModuleData *m_data;
	AnimationSoundModuleManager *m_manager = nullptr;
	std::size_t m_managerSlot = 0;   ///< this module's place in the manager's list (port)
	AudioHandle m_handle = AHSV_NoSound; // + 0x14
	std::string m_held;                  // + 0x18
	const AudioManager *m_playedOn = nullptr; ///< the audio m_handle belongs to (port, stop S-6024)
};
