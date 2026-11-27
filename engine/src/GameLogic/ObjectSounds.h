// OpenBFME. GPL-3.0.
// The object sounds the logic posts (stop S-707, lanes OBJSOUND-1 and OBJSOUND-2): a sound row (the audio table RW 0xC26720) as an AudioEventRTS (RW 0x6DA95E)
// attached to the object (setObjectID RW 0x6DA6A7) and added to TheAudio (RW 0xDE42FC vslot 0x64). Fire-and-forget client audio: no simulation state, no random
// draw of the logic. The row is RW 0x676A8A's: the drawable's sound selectors (S-701), then the template. INFERENCE (as S-1041): every object has the drawable
// the call sites test for. The definitions are in GameClient/ObjectSounds.cpp (this header includes no audio or client type on purpose, as AudioRequests.h).

#pragma once

#include <string>

class Object;
class Player;
class ThingTemplate;

namespace ObjectSounds
{
// the template's row; empty when it has none or NoSound (RW 0x73AB5D); a +SOUND: prefix is dropped (RW 0x73AC14)
std::string templateRow(const ThingTemplate &tt, const char *row);
// RW 0x676A8A for a sound row: the object's selectors, then its template. INFERENCE: the drawable's model condition flags are the object's (S-701); a
// RandomSoundSelectorClientBehavior does not answer here (its roll belongs to the client; no retail one sets a row posted through this file)
std::string objectRow(const Object &obj, const char *row);
// `owner`: the event's player. An object event without one is its object's controlling player's (RW 0x6DB566)
void playRow(const Object &obj, const char *row, const Player *owner);
void playRow(const Object &obj, const char *row);
} // namespace ObjectSounds
