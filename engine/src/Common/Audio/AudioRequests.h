// OpenBFME. GPL-3.0.
//
// AudioRequests (lane AUDIO-2): the part of the audio entry points (AudioEntryPoints.h) that simulation code calls. Fire-and-forget requests only: the
// logic posts them and never reads an audio result into its state. This header includes no audio type on purpose: the audio headers carry client float
// arithmetic, and a simulation translation unit must not pull them into the simulation audit. An AudioHandle is the std::uint32_t returned here.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace AudioApi
{
std::uint32_t playSoundForObject(const std::string &eventName, std::uint32_t objectId, int owningPlayerIndex = -1);
std::uint32_t playSoundForDrawable(const std::string &eventName, std::uint32_t drawableId, int owningPlayerIndex = -1);
// true when the event exists (the "Invalid Sound" check of the INI field parsers and Lua's audio lookups)
bool isValidEvent(const std::string &eventName);
// the logic's unit voice events (see AudioEntryPoints.h installUnitVoiceHandler)
void postUnitVoice(int voiceEvent, std::uint32_t objectId, std::uint32_t producerId = 0);
// a weapon's FireSound for the shooter (RW FiringTracker::shotFired 0x8E3411): loopFrames 0 = one shot; else the sound loops (re-added when it stopped)
// until `frame + loopFrames` passes without another shot (stopped by updateWeaponFireSounds, which the client calls once per logic frame)
void postWeaponFireSound(const std::string &eventName, std::uint32_t objectId, std::uint32_t loopFrames, std::uint32_t frame);
// lane AUDIO-3: a looping sound a logic module keeps the handle of (retail stores the AudioHandle in the module; here the handle lives on the audio side,
// keyed by the slot and the holder's object id, one per holder and slot). postHeldSound removes the holder's sound of that slot, then adds `eventName` for
// `objectId` (the object the sound plays on) and keeps its handle (an empty name or NoSound only removes); stopHeldSound removes it.
//   HELD_BUILDING_LOOP: GettingBuiltBehavior's Self*Loop (RW 0x8566DF adds, module + 0x24; RW 0x856644 / 0x856992 / 0x85750D remove; holder = the structure)
//                       and the dozer's building sound (startBuildingSound RW 0x88C4A3 / finishBuildingSound RW 0x88BFA9; holder = the dozer, sound on the site)
//   HELD_MOVE_LOOP:     AIInternalMoveToState's move loop (startMoveSound RW 0x748C0B keeps it at state + 0x40; onEnter RW 0x74DAE4 / onExit RW 0x748E06 remove)
enum HeldSoundSlot
{
	HELD_BUILDING_LOOP = 0,
	HELD_MOVE_LOOP = 1,
	HELD_SOUND_SLOTS = 2
};
void postHeldSound(int slot, const std::string &eventName, std::uint32_t holderId, std::uint32_t objectId);
void stopHeldSound(int slot, std::uint32_t holderId);
// the sounds of a slot that are still kept (the report, the tests); the handle `holderId` keeps in `slot` (0: none)
size_t heldSounds(int slot);
std::uint32_t heldSound(int slot, std::uint32_t holderId);
} // namespace AudioApi
