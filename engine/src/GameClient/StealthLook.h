// OpenBFME. GPL-3.0.
//
// StealthLook (lane STEALTH-1): the client side of RotWK invisibility, what the drawable shows for the local player. Presentation only: it reads the logic
// snapshot (ObjectSnapshot::stealthLook, InvisibilityManager::clientLook RW 0x81AA03) and never writes simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the drawable state setter RW 0x6760F9(state, min, max, cycle) with min / max GameData InvisibilityOpacityMin / Max
// and cycle InvisibilityOpacityCycleFrames / LOGICFRAMES_PER_SECOND (RW 0x81AAC0 .. 0x81AAFF) for an invisible object, the StealthUpdate's FriendlyOpacityMin /
// Max and PulseFrequency * 0.2f for a STEALTHED one (RW 0x777AAF .. 0x777AF7, look RW 0x77661E): states 0 / 2 normal; 3 normal with the +0x358 flag 1.0 (the
// detected look); 5 hidden (+0x43E = !(+0x3AB)); 1 / 4 an opacity between min and max: middle (min + max) / 2 (+0xC4), half range (max - min) / 2 (+0xC8),
// a client random phase (RW 0x6D33AB, +0xCC); the period is the cycle clamped to RW 0xBD83D4 .. 0xBDC1F8 times the client frame rate (RW 0xD9F60C).
// INFERENCE / presentation (stop S-1042): the opacity is middle + half range * sin(2 pi t / period + phase) clamped to 0 .. 1 in client time (the step of
// RW's drawable update is not read); the phase is the object id's, not a client random draw; the detected look (+0x358) is drawn as normal.

#pragma once

struct ObjectSnapshot;
struct LogicSnapshot;

namespace StealthLook
{
// the object is not drawn for the local player (state 5)
bool hidden(const ObjectSnapshot &rec);
// the opacity factor the drawable's own fade is multiplied by (1 outside states 1 / 4), at client time `clockMs`
float opacity(const ObjectSnapshot &rec, const LogicSnapshot &snap, double clockMs);
// `base` times opacity(...)
float drawOpacity(float base, const ObjectSnapshot *rec, const LogicSnapshot *snap, double clockMs);
// the client clock: clockMs + deltaMs
double advanceClock(double clockMs, double deltaMs);
} // namespace StealthLook
