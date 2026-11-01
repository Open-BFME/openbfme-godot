// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameNetwork/GameInfo.cpp GameInfoToAsciiString,
// ParseAsciiStringToGameInfo).
//
// SkirmishGameRecord (lane UI-6, GitGut's report on preview.6: "the selected map, rules and other settings are not kept when you leave Skirmish for the
// main menu and come back"): the lobby's game as RotWK writes it into Skirmish.ini and reads it back. Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * GameInfoToAsciiString (RW 0x8023C1; BFME2 0x800AF8, tier A; donor BFME2 decomp GameNetwork/GameSlotApparent.cpp:1047, byte-matched):
//     "M=%3.3x%s;MC=%X;MS=%d;SD=%d;GSID=%X;GT=%d;SI=%d;" (the map contents mask, the map path, its CRC and size, the seed, + 0x88, the mode + 0x5C, the
//     scenario + 0x58), then "GR=" and the ten rules each as "%d " (RW 0x960B4F; BFME2 0x95A087, tier A) and ";", then "S=" and per slot
//     "H<name>,<ip %X>,<port %d>,<accepted T/F><has map T/F>,<colour>,<template>,<start>,<team>,<+0x20>,<+0x40>,<hero>:" for a human,
//     "C<E|M|H|B>,<colour>,<template>,<start>,<team>,<+0x20>,<hero>:" for a computer (E easy 2, M medium 3, H hard 4, anything else, the brutal 5, B),
//     "O:" open, "X:" closed, and a final ";";
//   * ParseAsciiStringToGameInfo (RW 0x802DBA; BFME2 0x80116B, tier B same-shape; the decomp's :860) takes the pairs apart at ';' and '=' (an empty
//     value or an unknown key refuses the whole string), reads GR as ten atoi()s, each from just past the next space (RW 0x9609D9: past the end, 0),
//     requires every key, refuses a colour outside -1 .. colours - 1, a template outside -2 .. templates
//     - 1, a start below -1, a team outside -1 .. 3, + 0x20 outside -100 .. 0, + 0x40 outside 0 .. 256, a slot letter it does not know, and sets the game
//     only when all of it held.
// lane OWNERFIX-2: + 0x20 is the slot's handicap (SkirmishGameSlot::handicap), written and read back.
// INFERENCE / NOT PORTED (stop S-3701): SkirmishGameInfo has no + 0x88 (GSID), scenario (SI), + 0x40, address or port: they are written
// as 0 (SI -1) and read only to be checked; the hero byte (GameSlot::encodeHero, RW 0x800A25; decodeHero RW 0x801289) is written 0 (no hero): the lobby keeps a slot's
// Create-a-Hero and the Legendary Heroes factor in OpenBFME keys of their own (AptSkirmish::keepGame); the map path is written as the map cache's name with '\\' (retail's portable map path, RW 0x80204C, was not read); the
// player template count is the store's.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <string>

// The lobby's game as RotWK's GameInfoToAsciiString writes it (with the slots' names: retail's preferences pass true). `gameMode`: GT (0 skirmish,
// 1 War of the Ring).
std::string SkirmishGameInfoToString(const SkirmishGameInfo &info, int gameMode);

// What ParseAsciiStringToGameInfo checks a record against
struct SkirmishGameRecordLimits
{
	int numColors = 0;    ///< TheMultiplayerSettings->getNumColors()
	int numTemplates = 0; ///< ThePlayerTemplateStore->getPlayerTemplateCount()
};

// RotWK's parse of a record into `out` (only when the whole record held; `out` is untouched otherwise). `gameMode` receives GT. A computer slot's name is
// left empty (retail's setState with the empty string; the lobby gives it its state's text).
bool ParseSkirmishGameInfo(const std::string &text, const SkirmishGameRecordLimits &limits, SkirmishGameInfo &out, int *gameMode, std::string *error);

// RW 0x821D55: the "HeroIndexes" value, ":" and each slot's hero index (+ 0x5C) as "%d:" (no hero kept: S-3701, -1 each)
std::string SkirmishHeroIndexesString();
