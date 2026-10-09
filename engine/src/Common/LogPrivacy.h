// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: logs and reports a tester attaches name the tester no more than needed. The home folder becomes "~"
// (an install path stays readable: "~/Games/RotWK"), and the user name is replaced by "<user>" wherever it stands as a whole token,
// whatever its length (conservative: a one-letter name also replaces that word). Every sink goes through it: the session log, the console
// (Common/ConsoleFilter.h) and the GDScript reports (ReleaseInfo.redact).

#pragma once

#include <string>

namespace LogPrivacy
{

struct Rules
{
	std::string home; // the home folder (HOME / USERPROFILE); empty = none
	std::string user; // the user name (USER / USERNAME)
	bool caseInsensitive = false; // Windows paths
};

// The host's rules (HOME or USERPROFILE, USER or USERNAME; case-insensitive on Windows).
Rules hostRules();

// `text` with the home folder and the user name replaced (both '/' and '\' spellings of the home folder).
std::string redact(const std::string &text, const Rules &rules);

// For a stream filter that must write part of a text before the rest exists (Common/ConsoleFilter.h, an oversized line):
// a position c such that text[0, c) can be scrubbed on its own: no occurrence of the home folder or the user name spans c, and nothing
// from c on could be the start of one (review r4: a cut through a complete match leaked it).
size_t safeCut(const std::string &text, const Rules &rules);
// The last text of a stream (exit, crash): redact(), except that an end that is only the start of a name ("user=tes") is replaced by "…".
std::string redactFinal(const std::string &text, const Rules &rules);


} // namespace LogPrivacy
