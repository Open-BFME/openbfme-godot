// OpenBFME. GPL-3.0.
//
// Lane MENU-1: the layering check of the front end (the owner's preview.4 bugs, 2026-10-10: Skirmish's profile box drawn through the lobby, the
// profile page opened behind the game setup with its title over the lobby's). Not a retail system: a verifier over what the port draws, used by the
// shell walk tests (engine/tests/test_menu1_layering.cpp) and the Godot walk (AptMenuPlayer::get_layering_report, godot/scripts/menu1_walk.gd).
//
// Retail's rules it checks (TARGET FACTS, RotWK game.dat, caveat S-001):
//   * one screen at a time: Shell::push runs the covered screen's shutdown, which hides its window (Shell.h, ZH Shell::push / doPush);
//   * a screen's sub-movies (a clip a movie loaded with loadMovie: Skirmish.apt's OpenPlay = SkirmishOpenPlay.swf, its SkirmishStats = Stats.swf)
//     take turns: Skirmish.apt's `_close` plays `_root[vScreenName]._close` before `_open_profile` loads Stats.swf (root frames 14 / 23);
//   * the native gadgets are drawn inside the movie's render at their placeholder's depth (RW 0x814BEC -> GameWindowManager::drawWindow RW 0x6C1B52),
//     so a box the movie draws later covers them;
//   * a modal box takes the input: the movie's DisableComponents(path) disables the gadgets under `path` (RW 0x81456F) and DisableComponents()
//     keeps every window message from the gadgets (RW 0x812C89 reads the bits at RW 0xDE8A04).
// INFERENCE (this check's own thresholds, not retail facts): two texts "overprint" when their stage rectangles overlap by more than half of the
// smaller one and their texts differ; a unit is "visible" when it has a drawn command with alpha above zero or a shown gadget window.

#pragma once

#include <string>
#include <vector>

class Shell;
class WindowManager;
class AptRenderList; // lane CONTRIB-1 (GitHub #23): class, as defined (MSVC mangles class and struct differently: a struct here left checkShellLayering unresolved, LNK2019)

struct ShellLayeringReport
{
	std::string topScreen;                    // the shell's top screen file ("" none)
	std::vector<std::string> visibleScreens;  // shell screens with something drawn (file names, stack order)
	std::vector<std::string> visibleMovies;   // the top screen's sub-movies (loadMovie clips) with something drawn (their paths)
	std::vector<std::string> overprints;      // "text A | text B" pairs of overlapping titles / texts
	int shownGadgets = 0;                     // gadget windows shown
	int enabledGadgets = 0;                   // of them, enabled (take input)
	bool componentInputBlocked = false;       // DisableComponents() without a path is in force
	std::vector<std::string> problems;        // empty: the layering holds
};

// `renderList` is this frame's (Apt::buildRenderList). Levels that belong to no shell screen (AptLevel0, the background, the tooltip and GuiFX
// movies) are never counted as screens.
ShellLayeringReport checkShellLayering(WindowManager &windows, const Shell &shell, const AptRenderList &renderList);
