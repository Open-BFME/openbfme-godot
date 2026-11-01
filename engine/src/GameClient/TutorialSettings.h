// OpenBFME. GPL-3.0.
//
// TutorialSettings (lane BFME2CAMP1): GameData's two tutorial records, what AptMainMenu::OnTutorial starts.
//
// TARGET FACTS (RotWK game.dat GlobalData = RW 0xDE4364, caveat S-001; the GameData field table rows RW 0xC00E00 .. 0xC00ED0: { name, parse proc, user
// data, offset }): the Basic record at + 0x1114, the Advanced one at + 0x1138, each { + 0 Map, + 4 LoadMovie, + 8 LoadScreenStillImage, + 0xC
// LoadScreenVoiceTrack, + 0x10 LoadScreenMusicTrack (parseAsciiString RW 0x42EE5E), + 0x14 Objective (parseAsciiStringVectorAppend RW 0x42E59E),
// + 0x20 MillisecondsAfterStartToStartFadeUp (parseDurationUnsignedInt RW 0x73A429: logic frames) }, fields "BasicTutorialMap" .. and "AdvancedTutorialMap" ...
// AptMainMenu::OnTutorial (RW 0x91B825, registered at RW 0x91D151): "Strategic" starts the War of the Ring tutorial (not here); "Advanced" / "Basic" keep the
// record (GlobalData + 0x115C); an empty Map does nothing; else the map becomes the pending map (GlobalData + 0xAC0), the LoadMovie is played (RW 0x6269CD),
// the voice and music tracks are queued for the load screen, the still image set (RW 0x626A0D), TheGameLogic + 0x90 = the record's + 0x20, and a new game
// of mode 6 starts (RW 0x7111E5(6)).
// A field missing from GameData leaves the record empty (GlobalData's constructor zeroes it); a tutorial whose Map is empty is not started (as retail).

#pragma once

#include <string>
#include <vector>

class ArchiveFileSystem;

struct TutorialRecord
{
	std::string map;                    ///< + 0
	std::string loadMovie;              ///< + 4
	std::string loadScreenStillImage;   ///< + 8
	std::string loadScreenVoiceTrack;   ///< + 0xC
	std::string loadScreenMusicTrack;   ///< + 0x10
	std::vector<std::string> objective; ///< + 0x14
	unsigned fadeUpFrames = 0;          ///< + 0x20 MillisecondsAfterStartToStartFadeUp, in logic frames
};

struct TutorialSettings
{
	TutorialRecord basic;    ///< GlobalData + 0x1114
	TutorialRecord advanced; ///< GlobalData + 0x1138
	bool loaded = false;

	// OnTutorial's argument: "Basic" / "Advanced" (strcmp, case kept); null for any other (the WOTR "Strategic" one included)
	const TutorialRecord *record(const std::string &which) const;
	static bool load(ArchiveFileSystem &fs, TutorialSettings &out, std::string *error);
	static bool scan(const std::string &text, TutorialSettings &out, std::string *error);
};
