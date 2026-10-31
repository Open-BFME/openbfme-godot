// OpenBFME unit tests. GPL-3.0.
// Retail-gated tests mount the real archives once per process. They run only when ROTWK_INSTALL and
// BFME2_INSTALL are set; otherwise the test prints "SKIP" and returns (it never passes silently:
// the SKIP line is in the output and run_tests.bat reports it).

#pragma once

#include "Common/RetailArchivePolicy.h"

#include <memory>
#include <string>
#include <vector>

class Win32BIGFileSystem;

namespace retailtest
{

struct Mount
{
	std::unique_ptr<Win32BIGFileSystem> fs;
	std::string error;                 // non-empty if mounting failed
	std::vector<std::string> archives; // canonical archive paths in load order, "<install>:<path>"
	std::vector<MountedArchive> identityArchives; // verified hashes and sizes, for acceptance recordings
};

// Pure 2.01 = RotWK 2.01 archives (first) + BFME2 1.06 archives. nullptr if the install variables are unset.
Mount *pureMount();
// A second, unshared pure-2.01 mount: for tests that inject generated archives so they stay out of the shared corpus.
std::unique_ptr<Mount> freshPureMount();
// BFME2 1.06 archives only (asset.dat oracle). nullptr if BFME2_INSTALL is unset.
Mount *bfme2Mount();

// BFME2_INSTALL, or "" when unset.
std::string bfme2Install();

// Directory holding committed test data files (compiled in from CMake).
std::string dataDir();

// Whole file as bytes; false + *error if missing.
bool readLocalFile(const std::string &path, std::vector<unsigned char> &out, std::string *error);

// Prints the SKIP line for a retail-gated test.
void printSkip(const char *testName);

// The language of the install in ROTWK_INSTALL ("English", "German"; RetailArchivePolicy.h "Languages"), "" when unset or none found.
std::string installLanguage();
// A check pinned to the English retail data (its text, fonts, number format or the English audio archive): true, with a SKIP line naming
// `what` and the install's language, when the install is not English (text and voices). The caller returns (or skips the English-only part); an English
// install runs the check as before.
bool skipUnlessEnglish(const char *what);

} // namespace retailtest
