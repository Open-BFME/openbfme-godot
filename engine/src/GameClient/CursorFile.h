// OpenBFME. GPL-3.0.
//
// The retail mouse cursors (lane HUD-1): Data\INI\Mouse.ini names each `MouseCursor <Name>` with an Image (`SCCMove`) and a HotSpot; the image is a Windows cursor file in the install's
// loose Data\Cursors folder: `<Image>.ani` (a RIFF ACON animated cursor whose `icon` chunks are .cur files) or `<Image>.cur`. This reads both into RGBA frames with the hot spot and the frame
// rates, no OS cursor API: the device layer shows frame N as the pointer image.
//
// FACTS: .cur = ICONDIR (type 2) + ICONDIRENTRY[n] (width, height, hotspot x / y in the planes / bit count fields); the image is a BITMAPINFOHEADER (height doubled), a palette for 1 / 4 / 8
// bits, the XOR bitmap bottom-up, then the 1-bit AND mask; a 32-bit XOR bitmap with any non-zero alpha is used as it is, otherwise the mask makes the pixel transparent. .ani = RIFF `ACON`:
// `anih` (frame count, steps, default jiffies of 1/60 s), optional `rate` (jiffies per step) and `seq ` (frame index per step), `LIST fram` of `icon` chunks. Mouse.ini's `Directions`
// (the eight scroll cursors) and the retail default cursors are the device layer's.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CursorImage
{
	int width = 0, height = 0;
	int hotX = 0, hotY = 0;
	std::vector<std::vector<std::uint8_t>> frames; ///< RGBA8, row 0 on top, one entry per distinct image
	std::vector<int> sequence;                     ///< the animation: frame index per step (a single cursor: { 0 })
	std::vector<int> jiffies;                      ///< 1/60 s each step is shown
};

// false + *error for a file that is not a cursor / animated cursor
bool DecodeCursorFile(const std::vector<std::uint8_t> &bytes, CursorImage &out, std::string *error);

// `MouseCursor <Name>` blocks of Mouse.ini: name -> image file stem and the INI's hot spot (-1 -1: the cursor file's own)
struct MouseCursorEntry
{
	std::string image;
	int hotX = -1, hotY = -1;
	int directions = 0;
};
bool ParseMouseCursors(const std::string &iniText, std::map<std::string, MouseCursorEntry> &out, std::string *error);

// A file of `dir` whose name equals `name` ignoring case (the install is a Windows tree; a Linux file system is case sensitive): the real path, "" when absent
std::string FindFileNoCase(const std::string &dir, const std::string &name);

// Stops [S-295] of the cursor path (registered in docs/STOPS.md).
std::vector<std::string> CursorAcceptanceStops();
