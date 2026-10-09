// OpenBFME. GPL-3.0.
//
// Lane RELEASE-1: the console is a report sink too (a tester pastes the terminal, the crash handler dumps its backtrace there). Godot's own
// console logger cannot be filtered from an extension (Engine.print_to_stdout = false also silences every custom logger), so the process's
// stdout and stderr are routed through Common/LogPrivacy.h instead: on POSIX each descriptor is replaced by a pipe whose reader thread writes
// to the original stream. Strictly line-buffered: nothing reaches the console until its line is complete and has been scrubbed as a whole;
// a partial line is written (scrubbed) only at uninstall() or by the crash path (review r3: a time-based flush leaked split paths).
// On a crash the reader thread may not get to run before the process dies: crashWrite() writes the held partial lines and the crash text,
// scrubbed, straight to the original streams and makes the thread drop what follows (the session log has the whole crash either way).
// Windows: not available (install() returns false with the reason; the release executable has no console). See docs/RELEASE.md.

#pragma once

#include "Common/LogPrivacy.h"

#include <string>

namespace ConsoleFilter
{

// outFd / errFd: where the redacted streams go (-1 = the process's original stdout / stderr; tests pass files). maxLine: the longest line
// held whole; beyond it the line is written in parts cut by LogPrivacy::safeCut (tests pass a small value).
bool install(const LogPrivacy::Rules &rules, std::string *error, int outFd = -1, int errFd = -1, size_t maxLine = 8u << 20);
// Flushes stdio, restores the original descriptors and writes what the pipes still hold. Safe to call when not installed.
void uninstall();
bool installed();
// The crash path: `text` (redacted here) goes to the original stderr at once; the pipes' further contents are dropped.
void crashWrite(const std::string &text);

} // namespace ConsoleFilter
