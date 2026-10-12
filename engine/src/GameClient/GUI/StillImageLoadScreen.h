// OpenBFME. GPL-3.0.
//
// StillImageLoadScreen (lane MENU-1): which loading screen a game start shows, and the picture loading screen of the campaign missions.
// Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * GameLogic::getLoadScreen (RW 0x626A87, called by the new game's start RW 0x62DC3E, which stores the screen at GameLogic + 0x120 and runs its
//     init) switches on the game mode (GameLogic + 0x110):
//       - 0 (a campaign mission), 3, 6: unless a save game is loading or the mode is 3, a non-empty picture name (GameLogic + 0x84) makes the picture
//         screen (RW 0x757CA0) with that picture, + 0x88 and + 0x8C (the mission's LoadScreenMusicTrack and voice track; the tutorial INI fields
//         *LoadScreenStillImage / *MusicTrack / *VoiceTrack, RW 0xC01964 ..., are the same triple); otherwise the picture screen with the mapped
//         image "StillImage_Film%02d" of a client random draw 1 .. 21 (RW 0x626362, BFME2 decomp Rva0023D614Film.cpp);
//       - 1, 2, 5 (LAN, skirmish, internet): with GameLogic + 0x114 == 0 the picture screen as above, else the players' screen LoadScreen.apt
//         (AptLoadScreen, RW 0x81D356; the skirmish lobby's + 0x114 is 3: AptMapPreview's start spots, BFME2 decomp AptMapPreview.cpp);
//       - 4, 7: other screens (the shell's), not handled here.
//   * the picture screen's init (vtable RW 0xC2B51C slot 2, RW 0x7583CC): the display's three image slots (RW 0x65C3E2(image, slot, x0, y0, x1, y1),
//     screen fractions): slot 2 the mapped image "LoadScreenForeground" over the whole screen (0, 0, 1, 1); slot 0 the picture from x 0 to 1 and
//     y 0.1640625 to 0.1640625 + its image height / 768 (RW 0xC2B618, RW 0xC11188 = 1/768, image + 0x28); the spinning ring: the Video "SmallRing"
//     (RW 0xC2B5D0) in the rectangle 0.468262, 0.875651 .. 0.530762, 0.958984 (RW 0xC2B608 .. 0xC2B614, RW 0x6B2F35), looping; the text of the game
//     text label "GUI:Loading" (RW 0xC2B5F0) in the font "SachaWynter" (RW 0xC2B5FC) of size 18 (RW 0xBE5270) at 0.5, 0.03125 (RW 0xBD869C,
//     0xC2B5EC) in the colour 0xFFFFD554 (RW 0x7585D9).
// INFERENCE (stop S-3505): the slots are drawn in slot order (0, 1, 2) over the 1024 x 768 stage as the shell's backdrop is; the text is centred on its
// x; the music / voice tracks (+ 0x14 / + 0x18, RW 0x757DC7) are the caller's (campaign_flow.gd plays LoadScreenMusicTrack); the ring's flags
// (0x14, 0x1000014 without music and voice) were not read.

#pragma once

#include <functional>
#include <string>

class MappedImageCollection;

enum class LoadScreenKind
{
	StillImage, // RW 0x757CA0
	Players,    // LoadScreen.apt (RW 0x81D356)
	Other       // modes 4 / 7
};

struct LoadScreenChoice
{
	LoadScreenKind kind = LoadScreenKind::Other;
	std::string picture; // StillImage: the mapped image of slot 0
	std::string error;   // a film still was needed and no client random source was given (no picture is picked)
};

// RW 0x626A87. `clientRandom(lo, hi)` is GetGameClientRandomValue; it is called only when a film still is picked.
LoadScreenChoice chooseLoadScreen(int gameMode, int screenKind, bool loadingSaveGame, const std::string &picture, const std::function<int(int, int)> &clientRandom);

struct StillImageSlot
{
	std::string image;
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // screen fractions
	bool on = false;
};

struct StillImageLoadScreenLayout
{
	StillImageSlot slots[3];
	std::string movie = "SmallRing";
	float movieRect[4] = { 0.46826171875f, 0.8756510615348816f, 0.53076171875f, 0.958984375f };
	std::string textLabel = "GUI:Loading";
	std::string textFont = "SachaWynter";
	int textSize = 18;
	unsigned int textColor = 0xFFFFD554u;
	float textX = 0.5f, textY = 0.03125f;
	// what the port infers (reported in the shell report's unverified_device)
	std::string unverified = "[S-3505] picture load screen: the display's image slots drawn in slot order over the 1024 x 768 stage, the text centred on its "
							 "x, the ring looping on the wall clock; the music / voice tracks are the caller's, the ring's flags (RW 0x758502) were not read";
};

// RW 0x7583CC: the layout of the picture screen for `picture` (a mapped image name). False + *error when the picture or the foreground is not a
// mapped image of `images` (retail draws nothing for a missing one; the port reports it).
bool layoutStillImageLoadScreen(const MappedImageCollection &images, const std::string &picture, StillImageLoadScreenLayout &out, std::string *error);
