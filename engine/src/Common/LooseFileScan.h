// OpenBFME. GPL-3.0.
//
// Finds LOOSE (not in any .big) map and script-library files under a retail install directory.
//
// Plan rule 7: in the 2.01 baseline any loose file in the install folders that is not part of
// retail is reported as contamination and refused. SAGE itself checks loose files before archives
// (ZH FileSystem.cpp:174-190), so a loose 0-byte "lib_end_mission.map" would hide the real archive
// copy. The map loader never reads loose files (archives only); this scan exists so the finding
// reaches a report instead of being silently skipped or "worked around".

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct LooseFile
{
	std::string relativePath; // forward slashes, relative to the install root
	std::uint64_t size = 0;
};

// One loose map/script file found in one install, with the line the production code reports for it.
struct LooseFileFinding
{
	std::string install; // "rotwk", "bfme2"
	LooseFile file;
	std::string line;    // "S-039 ..." (docs/STOPS.md): what the viewer shows and the report carries
};

namespace LooseFileScan
{
// Every *.map / *.scb under `installRoot` (recursive). Returns false + *error if the directory
// cannot be read.
bool findLooseMaps(const std::string &installRoot, std::vector<LooseFile> &out, std::string *error);

// The report line for one finding (stop S-039): says what the file is, that it is refused and why.
std::string describeFinding(const std::string &install, const LooseFile &file);

// findLooseMaps over several installs ({label, root}), each result turned into a finding. Returns
// false + *error if any root cannot be read (nothing is skipped silently).
bool scanInstalls(const std::vector<std::pair<std::string, std::string>> &installs, std::vector<LooseFileFinding> &out, std::string *error);
} // namespace LooseFileScan
