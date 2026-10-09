// OpenBFME. GPL-3.0.
//
// Portable ports of the three shlwapi calls RotWK's TextFile uses to resolve an #include path
// (RW 0xA15C87 PathRemoveFileSpecA, 0xA15C97 PathAppendA, 0xA15CCB PathCanonicalizeA), so include
// resolution is identical on every platform the engine runs on.
//
// TARGET FACTS: the retail binary calls the real shlwapi functions (import slots 0xBD07B0,
// 0xBD07B4, 0xBD07B8). The behaviour here is DERIVED, not copied: it was fitted against the real
// calls on Windows (60,000 random paths per function) and is re-checked by tests/test_win32path.cpp
// on every Windows run, with shlwapi as the oracle.
//
// SUPPORTED DOMAIN: ASCII text where '\' is the separator, an optional prefix of "", "X:", "X:\" or
// "\", components made of any other bytes ('/', '.', '..', trailing dots and spaces included), and
// single separators. UNC ("\\server\share"), device ("\\?\") and doubled-separator paths follow
// different shlwapi rules that are not ported: isSupportedPathForm() reports them and
// resolveInclude callers must treat them as an error (no retail include uses them).
//
// MAX_PATH (the retail code uses 260-byte buffers and ignores the return values, RW 0xA15C97):
//   * PathCanonicalize FAILS (returns false, output cleared) when a component is >= 257 characters,
//     when the running output reaches 260 characters before ".." removals, or when the result does;
//   * PathAppend FAILS (returns false, buffer cleared) when either part is >= 260 characters or the
//     canonicalised join fails. Retail carries on with the cleared buffer, which canonicalises to "\".
//   Both rules were fitted to the real calls with 80,000 random long paths and are exact outside the
//   band where any length is 250-262: right at the boundary shlwapi truncates and drops a trailing
//   separator in ways not ported, and isSupportedPathForm() reports that band as unsupported.
//
// Behaviours worth knowing (all observed from shlwapi): PathCanonicalize turns "" into "\", drops a
// leading ".." that has nothing to pop, roots the result ("\x") once a ".." empties a relative
// path, strips trailing dots from the last component ("a.", "a\." -> "a"), turns a bare "X:" into
// "X:\", and does NOT treat '/' as a separator. PathAppend strips one leading '\' from the
// appended part and lets a drive-qualified part replace the directory.

#pragma once

#include <string>

namespace Win32Path
{

// false for forms outside the supported domain (UNC, device paths, doubled separators, non-ASCII,
// a ':' anywhere except as the drive prefix "X:" at index 1, and any length / component / running
// length in the 250-262 MAX_PATH boundary band).
// Embedded drive components ("a\C:\..\x", ".\C:\..\x") are refused: the real shlwapi treats the
// inner "C:" as a drive in some positions (PathCanonicalize roots the result at it) and as a name
// in others, depending on the surrounding ".." folding. The port does not reproduce that;
// ACCEPTANCE STOP S-011 (docs/STOPS.md), pinned by test_win32path.cpp.
bool isSupportedPathForm(const std::string &path);

// true when the length, running length, longest component or canonical length of `path` lies in the
// 250-262 MAX_PATH boundary band that is not ported. This is the band half of isSupportedPathForm,
// for a joined path that legitimately contains a drive-qualified operand ("dir\C:\x").
bool inLengthBand(const std::string &path);

// PathRemoveFileSpecA: returns whether the string changed
bool removeFileSpec(std::string &path);

// PathAppendA (PathCombine semantics for a relative `more`). On failure (MAX_PATH) returns false and
// leaves `path` EMPTY, exactly like the real call.
bool append(std::string &path, const std::string &more);

// PathCanonicalizeA. On failure (MAX_PATH) returns false and `out` is empty.
bool canonicalize(const std::string &path, std::string &out);

} // namespace Win32Path
