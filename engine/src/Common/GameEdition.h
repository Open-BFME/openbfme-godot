// OpenBFME. GPL-3.0.
//
// Lane BFME2MODE-0 (owner 2026-10-29, AGENTS.md, docs/EDITIONS.md): the game editions. The engine is growing two editions with their own rules,
// BFME2 1.06 (the base) and RotWK 2.01 (BFME2 plus its differences). Phase 0 adds only the developer switch that mounts BFME2 1.06's archives alone
// for gameplay (OPENBFME_EDITION=bfme2; openbfme_peer --edition=bfme2) and the data test below. Every game still runs RotWK's rules: the switch
// changes the mounted data (and so the data identity, which already covers every mounted archive), never a version or a wire format.
//
// Like BFME1 mode (Common/BfmeGame.h), the DATA decides the edition: code that must know asks isBfme2Data(fs), never an environment variable.
#pragma once

#include "Common/ArchiveFileSystem.h"

#include <string>

enum class GameEdition
{
	RotWK201 = 0, // RotWK 2.01 over BFME2 1.06 (every game today)
	BFME2_106 = 1 // BFME2 1.06's archives alone (developer switch)
};

// BFME2 1.06's archives alone: its ini.big holds Data\INI\Default\SubsystemLegend.ini and Data\INI\AwardSystem.ini, but not RotWK's
// SubsystemLegendExpansion1.ini (RotWK 2.01 INI.big). BFME1 1.03 has the legend without AwardSystem.ini (isBfme1Data).
inline bool isBfme2Data(const ArchiveFileSystem &fs)
{
	return fs.doesFileExist("Data\\INI\\Default\\SubsystemLegend.ini") && !fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini") &&
		fs.doesFileExist("Data\\INI\\AwardSystem.ini");
}

inline const char *gameEditionName(GameEdition e)
{
	return e == GameEdition::BFME2_106 ? "bfme2" : "rotwk";
}

// the developer switch's value ("rotwk", "bfme2"; empty = rotwk); any other value is an error, never a quiet default (CLAUDE.md: no fallbacks)
inline bool parseGameEdition(const std::string &value, GameEdition &out, std::string *error)
{
	if (value.empty() || value == "rotwk")
	{
		out = GameEdition::RotWK201;
		return true;
	}
	if (value == "bfme2")
	{
		out = GameEdition::BFME2_106;
		return true;
	}
	if (error)
	{
		*error = "unknown game edition '" + value + "' (OPENBFME_EDITION / --edition takes rotwk or bfme2)";
	}
	return false;
}
