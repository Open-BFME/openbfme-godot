// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: logs and reports a tester attaches name the tester no more than needed. The home folder becomes "<user>"
// (an install path stays readable: "<user>/Games/RotWK"), and the user name is replaced by "<user>" wherever it stands as a whole token,
// whatever its length (conservative: a one-letter name also replaces that word). Every sink goes through it: the session log, the console
// (Common/ConsoleFilter.h) and the GDScript reports (ReleaseInfo.redact).

#pragma once

#include <string>
#include <cstdio>

namespace LogPrivacy
{

inline constexpr size_t kMaxFieldBytes = 16u << 10;
inline constexpr size_t kMaxLogBytes = 1u << 20;
inline constexpr size_t kHeadBytes = 64u << 10;
inline constexpr const char *kOmitted = "[private diagnostic omitted]";
inline constexpr const char *kOversized = "[oversized diagnostic omitted]";

// Preserve the initial build/context records and a rolling tail, at complete line boundaries.
struct Budget
{
	size_t written = 0;
	bool rewrite = false;
	bool headClosed = false;
	std::string head, tail;
	std::string record(const std::string &text, const struct Rules &rules);
	std::string snapshot() const { return head + tail; }
	bool write(std::FILE *file, const std::string &text, const struct Rules &rules);
};

struct Rules
{
	std::string home; // the home folder (HOME / USERPROFILE); empty = none
	std::string user; // the user name (USER / USERNAME)
	bool caseInsensitive = false; // Windows paths
};

// The host's rules (HOME or USERPROFILE, USER or USERNAME; case-insensitive on Windows).
Rules hostRules();

// Bounded diagnostic text: home/user names, addresses, credential values and player/chat content scrubbed.
// This is defense in depth for text logs, NOT a sanitizer for arbitrary file contents or binary minidumps.
std::string redact(const std::string &text, const Rules &rules);
std::string errorMessage(const std::string &code, const std::string &rationale, const Rules &rules);

// For a stream's final partial text (never use this to split an oversized credential-bearing line):
// a position c such that text[0, c) can be scrubbed on its own: no occurrence of the home folder or the user name spans c, and nothing
// from c on could be the start of one (review r4: a cut through a complete match leaked it).
size_t safeCut(const std::string &text, const Rules &rules);
// The last text of a stream (exit, crash): redact(), except that an end that is only the start of a name ("user=tes") is replaced by "…".
std::string redactFinal(const std::string &text, const Rules &rules);


} // namespace LogPrivacy
