// OpenBFME. GPL-3.0.
//
// The named audio entry points the rest of the engine calls (ZH reaches TheAudio through a global; here the global is one installed pointer and the
// calls are named after their callers, so every call site is easy to find and to wire):
//
//   APT shell PlaySound fscommand        -> playUiSound
//   FXList sound nuggets (FX-1)          -> playSoundAtPosition
//   logic event sites (voices, weapon / impact / ambient sounds of an object) -> playSoundForObject
//   draw / model condition sounds, Lua CurDrawablePlaySound (S-124) -> playSoundForDrawable
//   Lua ObjectPlaySound (RW 0x736FCD)    -> playSoundForObject
//   music scripts, the shell             -> playMusic / stopMusic
//   unit voices with an Eva id, FX Eva nuggets, the logic's announcements -> reportEva (AUDIO-2: TheEva is installed next to the manager)
//
// No fallbacks: when no AudioManager is installed a call returns AHSV_Error and is counted (callsWithoutAudio()), so a missing wiring shows up in a report
// instead of as silence.

#pragma once

#include "Common/Audio/AudioRequests.h"
#include "Common/Audio/GameAudio.h"

#include <functional>

class Eva;

namespace AudioApi
{
// playSoundForObject, playSoundForDrawable, isValidEvent and postUnitVoice are declared in AudioRequests.h (the simulation side)
void install(AudioManager *manager); // nullptr uninstalls; the installing thread owns the manager (SMOOTH-1)
// SMOOTH-1 (S-814): the logic's audio calls made on another thread than the owner's (the logic worker: playSoundForObject / ForDrawable, postUnitVoice,
// postWeaponFireSound) are queued in call order; the owner runs them here, at a point where the logic is idle (they read the live game)
// review r4: a queued request carries the generation of the manager / voice registration it was made under; one whose registration was removed or replaced
// before the drain is dropped (counted in staleDeferredDropped), never run against the replacement
void drainDeferred();
std::uint64_t deferredCalls();
std::uint64_t staleDeferredDropped();
AudioManager *current();
std::uint64_t callsWithoutAudio();

AudioHandle playUiSound(const std::string &eventName);
AudioHandle playSoundAtPosition(const std::string &eventName, const Coord3D &position);
AudioHandle playMusic(const std::string &eventName);
void stopMusic(bool fade);
void stopSound(AudioHandle handle);

// AUDIO-2: TheEva (RW 0xDE3670), installed by the owner of the audio manager. reportEva is RW 0x5DD9EE by name (the event at a position, or none); false when the
// event is unknown, Eva is disabled or the report was dropped on the spot. No Eva installed: false, counted in callsWithoutEva().
void installEva(Eva *eva); // nullptr uninstalls
Eva *currentEva();
bool reportEva(const std::string &eventName, const Coord3D *position);
std::uint64_t callsWithoutEva();

// AUDIO-2: the logic's unit voice events (retail calls the voice picker RW 0x8DEDBB from logic code with the client voice message types: ProductionUpdate
// RW 0x8A2A48 posts 0x7DA (VoiceCreated) for the unit it made, GettingBuiltBehavior RW 0x857DB4 0x7E2 (VoiceFullyCreated), Object RW 0x69987C 0x7DF ...).
// Fire-and-forget: the installed handler (the HUD's UnitVoiceResponse) plays the voice; nothing comes back into the logic. No handler: counted.
typedef std::function<void(int voiceEvent, std::uint32_t objectId, std::uint32_t producerId)> UnitVoiceHandler;
// `owner` identifies the registration (review r1 fix 1): uninstallUnitVoiceHandler(owner) removes the handler only when it is still that owner's, so a
// second HUD's handler survives the first HUD's destruction. An empty function uninstalls whatever is installed.
void installUnitVoiceHandler(UnitVoiceHandler handler, const void *owner = nullptr);
void uninstallUnitVoiceHandler(const void *owner);
std::uint64_t unitVoicesWithoutHandler();
// the looping fire sounds (postWeaponFireSound): stops those whose FireSoundLoopTime ran out (RW FiringTracker::update 0x8E30DD); returns how many loop
void updateWeaponFireSounds(std::uint32_t frame);
size_t loopingWeaponFireSounds();
std::uint64_t weaponFireSoundsPosted();
} // namespace AudioApi
