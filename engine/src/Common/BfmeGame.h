// OpenBFME. GPL-3.0.
// Which game the mounted retail data is (docs/BFME1.md): BFME1 ships Data\INI\Default\SubsystemLegend.ini without BFME2's
// SubsystemLegendExpansion1.ini. Loaders that read data BFME1 lays out differently ask here.
#pragma once

#include "Common/ArchiveFileSystem.h"

inline bool isBfme1Data(const ArchiveFileSystem &fs)
{
	return fs.doesFileExist("Data\\INI\\Default\\SubsystemLegend.ini") && !fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini");
}
