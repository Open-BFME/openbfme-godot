// OpenBFME. GPL-3.0.
//
// GdiFontMetrics (lane UI-6): the vertical metrics RotWK's text has. RotWK draws every string with GDI fonts rasterised by the W3D font library, and
// it places a string by those fonts' GDI text metrics, not by the font file's hhea line metrics a TrueType renderer (Godot) uses. Client code only.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * FontLibrary::getFont (RW 0x6186E0; BFME2 decomp FontLibraryFindRecord.cpp, tier A) asks for style 4 unless the font's size record says 1, and the
//     styled lookup (RW 0x61829B) snaps the size to quarters: floor(size * style + 0.5) / style; the style is the supersampling factor ("extra", + 0x40);
//   * FontCharsClass::Create_GDI_Font (RW 0x553FD0; BFME2 0x554630, tier A; donor BFME2 decomp FontCharsClassCreateGDIFont.cpp) makes the font with
//     CreateFontA(-(int)floor(size * extra + 0.5), ...) (a negative height: the em height in pixels, at extra times the size) and keeps from
//     GetTextMetricsA the char height (extra + tmExternalLeading + tmHeight - 1) / extra at + 0x2C and the external leading tmExternalLeading / extra at
//     + 0x30 (integer divisions);
//   * the glyph rasteriser (RW 0x5563F0 at 0x556579 .. 0x5565CF; BFME2 0x556A60, tier B) clears the glyph cell's first (+ 0x30) / 2 rows and its last
//     (+ 0x30) - (+ 0x30) / 2 rows and draws the glyph (ExtTextOutW, RW 0x55669D, top aligned, at extra times the size, then reduced by extra) between
//     them: the baseline is (+ 0x30) / 2 + tmAscent / extra below the cell's top, and a string is one cell (+ 0x2C) high per line;
//   * the Apt text draw (RW 0x4A8F95, AptCanvas.h PlaceAptText) and the list box cell draw (RW 0x4A2226 at 0x4A2611) centre that string height in
//     their boxes.
// DONOR / INFERENCE (stop S-3700): GDI's TrueType text metrics are not in the binary. They are the documented ones (the OpenType OS/2 table's usWinAscent /
// usWinDescent and the hhea line gap rule for tmExternalLeading), scaled to the em height and rounded to the nearest pixel as Wine's GDI does
// (dlls/win32u/freetype.c get_text_metrics); the factor is the default 4 (the font library's size records, which can make it 1, were not traced); no
// RotWK frame was compared. A font file without an OS/2 table, or with zero win metrics, is not measured (an error, never a guess).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct GdiFaceMetrics
{
	int unitsPerEm = 0;  ///< head.unitsPerEm
	int winAscent = 0;   ///< OS/2 usWinAscent
	int winDescent = 0;  ///< OS/2 usWinDescent
	int hheaAscent = 0;  ///< hhea ascender
	int hheaDescent = 0; ///< hhea descender (negative below the baseline)
	int hheaLineGap = 0; ///< hhea lineGap
};

// The cell one line of text takes at an em height of `ppem` pixels.
struct GdiTextCell
{
	int ascent = 0;          ///< tmAscent at the supersampled size
	int descent = 0;         ///< tmDescent at the supersampled size
	int externalLeading = 0; ///< tmExternalLeading at the supersampled size
	int height = 0;          ///< the char height (+ 0x2C): (extra + tmExternalLeading + tmHeight - 1) / extra, the string height a line takes
	int leading = 0;         ///< + 0x30: tmExternalLeading / extra
	float baseline = 0;      ///< from the cell's top: leading / 2 + tmAscent / extra
};

// Reads the head, hhea and OS/2 tables of a TrueType / OpenType (CFF) font file. False with `error` set when a table is missing or short.
bool ReadGdiFaceMetrics(const std::uint8_t *data, std::size_t size, GdiFaceMetrics &out, std::string *error);

// The cell of a font of `ppem` pixels made with the supersampling factor `extra` (RW 0x6186E0's default style 4): GetTextMetrics' answer for
// CreateFont(-ppem * extra) reduced as Create_GDI_Font and the rasteriser reduce it (see the header comment); a zero cell for ppem <= 0, extra <= 0 or an
// unmeasured face.
GdiTextCell GdiTextCellAt(const GdiFaceMetrics &face, int ppem, int extra = 4);
