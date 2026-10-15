// OpenBFME. GPL-3.0.
//
// AptHoverInventory (lane HOVER-1): every place of an Apt movie the pointer can rest on, read from the movie files (no running movie): the buttons the
// root timeline and every sprite timeline place, through the movie's imports (a button imported from MenuExport.apt is the importer's), and the buttons
// under each export (what ActionScript can attach at run time, attachMovie: their path is the export's, the attach name being the script's).
//
// TARGET FACT (AptPlayer::HandleOverButtons RW 0x624C68, see CursorTooltip.h): the hover target is a button; its path below its level, the button's own
// instance name last (empty for an unnamed button), keys the over-button handlers and the tooltip label "TOOLTIP:<movie>/<path>". INFERENCE (S-4610): a
// static walk of every frame's placements finds the buttons a movie can show; a path a script makes (attachMovie / createEmptyMovieClip names) is only
// known by its export.

#pragma once

#include <memory>
#include <string>
#include <vector>

class AptLoader;
struct AptFile;

struct AptHoverArea
{
	std::string movie;   ///< the movie walked ("MainMenu")
	std::string path;    ///< "<movie>/<instance names ...>/<button name>" ("" last name: the path ends with '/'); an export's: "<movie>:<export>/..."
	bool fromExport = false;
	std::string frames;  ///< the timeline frame (0-based) that places each instance of the path, "0/9/9": evidence of when a button exists
};

class AptHoverInventory
{
public:
	// the hover areas of `movieName`, sorted, without duplicates; false (and *error) when the movie or one of its imports does not load
	static bool walk(AptLoader &loader, const std::string &movieName, std::vector<AptHoverArea> &out, std::string *error);
	// "MainMenu/SoloPlayNav/Skirmish/bttn" -> "bttn"; "Options/tooltipsEAX3" -> "tooltipsEAX3"
	static std::string leafName(const std::string &path);
};
