// OpenBFME. GPL-3.0.
//
// VisionSettings (lane VIS-1): the GameData and MultiplayerSettings values the shroud reads.
//
// TARGET FACTS (RotWK game.dat, GameData table RW 0xBFF700 .. 0xC01000 = { name, parser, user data, GlobalData offset }, caveat S-001):
//   PartitionCellSize + 0xD4 (parseReal RW 0x42ED00; TheShroudManager's cell size, RW 0x62CFBB), UnlookPersistDuration + 0xBD0 (parseDurationUnsignedInt
//   RW 0x73A429: milliseconds to logic frames; constructor 30, RW 0x6432A0), ShroudColor + 0xBDC (parseRGBColor RW 0x42EF99), ClearAlpha + 0xBE8, FogAlpha
//   + 0xBE9, ShroudAlpha + 0xBEA (parseUnsignedByte RW 0x42EB75; constructor 255 / 127 / 0, RW 0x6432CC .. 0x6432E5), StealthFriendlyOpacity + 0xC8
//   (parsePercentToReal RW 0x42EEFA). MultiplayerSettings (data\ini\multiplayer.ini, table RW 0xC2F708, object RW 0xDE7D3C): UseShroud + 0x1C (parseBool).
// The retail values: PartitionCellSize 40, UnlookPersistDuration 1 (1 frame), ShroudColor 255 255 255, FogAlpha 127, ShroudAlpha 0, ClearAlpha absent (255),
// StealthFriendlyOpacity 50%, UseShroud No.
// A key the retail files carry is required (PLAN rule 10); ClearAlpha keeps the constructor's 255 when absent (retail does not set it).

#pragma once

#include <string>

class ArchiveFileSystem;

struct VisionSettings
{
	bool loaded = false;
	float partitionCellSize = 0.0f;
	unsigned unlookPersistFrames = 30;
	float shroudRed = 0.0f, shroudGreen = 0.0f, shroudBlue = 0.0f; ///< parseRGBColor: 0 .. 1
	unsigned clearAlpha = 255, fogAlpha = 127, shroudAlpha = 0;
	float stealthFriendlyOpacity = 0.0f;
	bool useShroud = false; ///< MultiplayerSettings UseShroud
	// lane HUD-5: MultiplayerSettings ShowRandomPlayerTemplate / ShowRandomStartPos / ShowRandomColor (GameSlot::getApparent*: an enemy's random choice is shown
	// as random). Default true: MultiplayerSettings' constructor (RW 0x7836C0, BFME2 decomp MultiplayerSettingsCtor.cpp, tier B same-shape); optional fields
	bool showRandomPlayerTemplate = true, showRandomStartPos = true, showRandomColor = true;

	static bool load(ArchiveFileSystem &fs, VisionSettings &out, std::string *error);
	// the two files' texts (tests)
	static bool scan(const std::string &gameData, const std::string &multiplayer, VisionSettings &out, std::string *error);
};
