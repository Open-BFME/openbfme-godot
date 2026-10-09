// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The rebuild uses std::string in place of ZH's AsciiString. These free functions port the
// AsciiString members whose exact behaviour the file system depends on
// (ZH GameEngine/Source/Common/System/AsciiString.cpp).

#pragma once

#include <string>

namespace AsciiStringUtil
{

// AsciiString::toLower. ASCII only, like the original (it uses tolower on a char buffer).
void toLower(std::string &s);
std::string lowered(const std::string &s);

// AsciiString::nextToken. Skips leading separators, copies the next token into *tok and
// leaves the remainder (starting at the separator after the token) in s. When s is empty it
// returns false and leaves *tok UNCHANGED; when only separators remain it clears both. The
// callers in ArchiveFileSystem rely on both behaviours.
bool nextToken(std::string &s, std::string *tok, const char *seps);

// AsciiString operator< : strcmp ordering.
inline bool lessStrcmp(const std::string &a, const std::string &b) { return a.compare(b) < 0; }

// AsciiString::compareNoCase : _stricmp ordering (ASCII tolower on both sides).
int compareNoCase(const std::string &a, const std::string &b);

} // namespace AsciiStringUtil
