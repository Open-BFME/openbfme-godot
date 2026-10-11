// OpenBFME. GPL-3.0.
//
// Lane OPTS-2: the CommandMap's hot keys as plain rows, for the OpenBFME screen's read-only Controls tab (GodotAptPlayer get_command_map). It reads the
// files the HUD reads (Data\INI\CommandMap.ini, then CommandMap.ini: MetaMap::load, parseMetaMap RW 0x5DAD46) without pulling MetaEvent.h's key enums
// into a translation unit that has the gadget layer's (GameClient/GUI/Gadget.h names the same KEY_* constants).

#pragma once

#include <string>
#include <vector>

class ArchiveFileSystem;

struct CommandMapEntry
{
	std::string name;         // the meta message (ClientMessageMetaName)
	std::string key;          // MetaMap::keyName
	int transition = 0;       // DOWN 0, UP 1, DOUBLEDOWN 2
	bool ctrl = false, shift = false, alt = false;
	bool game = false, shell = false; // UseableIn GAME / SHELL
	int category = 0;
	std::string displayName, description; // the game text labels
};

// the records in file order; false + *error when a file throws
bool ListCommandMap(ArchiveFileSystem &fs, std::vector<CommandMapEntry> &out, std::string *error);
