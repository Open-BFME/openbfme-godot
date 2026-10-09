// OpenBFME. GPL-3.0.
//
// The pathfinder's configuration from the game's INI files (lane PATH-1). The values are read from the files of the
// mounted install, never defaulted: a missing file or key is an error (CLAUDE.md "no fallbacks").
//
// STOP S-160 (docs/STOPS.md): the AIData / GameData blocks are read by a text scan (block name, `Key = value`
// lines, ';' comments), like MapObjectGameData::load; the GlobalData / AIData parsers of the INI system belong to a
// later lane. Later files override earlier ones key by key, in the load order of the retail INI loader
// (default\aidata.ini, then aidata.ini; gamedata.ini as the archives resolve it).

#pragma once

#include "GameLogic/AI/AIPathfind.h"

#include <string>

class ArchiveFileSystem;

namespace PathfindConfigLoader
{
// Fills every field of `out`; false + *error when a file or a key is missing.
bool load(ArchiveFileSystem &fs, PathfindConfig &out, std::string *error);
// The scanning step on already-read text: `blockName` is the block to read, the callback receives each key / value.
// Exposed for the tests.
bool scanBlock(const std::string &text, const std::string &blockName, std::string *error,
	void (*onPair)(void *user, const std::string &key, const std::string &value), void *user);
} // namespace PathfindConfigLoader
