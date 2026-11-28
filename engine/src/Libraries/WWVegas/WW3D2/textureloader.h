// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Texture name -> virtual file path (the name half of ZH textureloader.cpp / assetmgr.cpp Get_Texture).
//
// Rules (spec w3d-and-draw.md 1.4; the BFME2 GameFileClass::Set_Name that builds the directory is unmatched, so the
// directory layout is taken from the corpus itself, which fixes it: every compiled texture sits at
// art\compiledtextures\<first two characters>\<name>):
//  1. lower-case the name (ZH assetmgr.cpp:1103-1104);
//  2. compressed first: the last three characters become "dds" (ZH ddsfile.cpp:50-55; the compressed-then-uncompressed
//     order is ZH textureloader.cpp:1222-1240), then the name as written (tga);
//  3. directories: art\compiledtextures\xx\, then art\textures\ (the one APT texture the corpus keeps there).
// The name is used exactly as written: BFME2's name copy (0x4785F1-0x478606) does not strip spaces, and 30 retail archive
// entries carry a space in their file name ("exicemunitionsalpha .dds"). ZH's Set_Name space stripping is a ZH fact the
// target does not share, so there is no second try with the spaces removed.
// A name that resolves to nothing is an error for the caller, and the surface draws W3D_MISSING_TEXTURE_COLOR (below).

#pragma once

#include <functional>
#include <string>
#include <vector>

struct TextureResolution
{
	bool Found = false;
	std::string Path;           // virtual path of the file that exists
	bool IsDDS = false;
	std::vector<std::string> Tried;
	// False when retail's loader opens no file for this name at all (there is no extension to swap), so nothing can be
	// found and nothing is missing: the caller draws retail's 1 x 1 missing texture and reports nothing. RW 0x530D29
	// builds its candidates by locating the dot (strrchr, RW 0x530D45) and writing .dds / .tga / .jpg / .png after it;
	// `test edi, edi / je 0x530DE1` (RW 0x530D59) leaves both file pointers (+0x10, +0x1C) null, and only that state
	// makes the texture creation RW 0x53193E take its "no file bytes" branch (RW 0x531958 -> 0x531B02, then RW 0x531B07
	// .. 0x531B44: a 1 x 1 0xFFFF00FF texture). Retail logs nothing for it: `game.dat` 2.01 holds no "texture not
	// found" string (every string of the image scanned; "Unable to open %s" RW 0xBEAC50 is the file system's own log,
	// whose one caller RW 0x575D79 is not a texture path). This is how the W3D data spells "no texture": the meshes of
	// GBFARM_BIB, ANGPORTER.BUCKET and CAH_skull name the TEXTURE_NAME `None`, and an FX material parameter is `None`
	// too — the same word AsciiString::isNone compares against (RW 0x437720 pushes RW 0xBD3BF8, `None`, and calls
	// RW 0x4372C0's _stricmp). None of those names has a dot, so the loader never looks for a file called that.
	bool Probed = true;
	// Set only when nothing resolved: the file art\terrain\<name as written> exists. Retail's file name builder for a texture
	// (GameFileClass::Set_Name, RW 0x477D1C: .tga / .dds / .png / .jpg names become Art/CompiledTextures/<first two characters>/<name>,
	// or Art/Textures/<name> for apt_ names; nothing else) never looks in Art\Terrain; only the terrain texture loader (RW 0x709F77,
	// prefix "Art\Terrain\") does. A model naming such a file is a missing texture in retail too: the file is reported, not used.
	std::string TerrainFolderPath;
};

// True when retail's compiled-texture loader would open a file for this name. Its only name handling is replacing the
// three characters after the last dot (RW 0x530D29; RW 0x530D45 strrchr, RW 0x530D61 .dds, RW 0x530D82 .tga,
// RW 0x530DA3 .jpg, RW 0x530DC4 .png), so a name without a dot is never opened: its texture is the 1 x 1 0xFFFF00FF
// of RW 0x53193E and no file is missing.
bool W3D_Texture_Name_Is_Probed(const std::string &name);

// flatLayout (community PR #36): the files are BFME1 data (isBfme1Data of the file system `exists` asks): art\textures\<stem>.dds / .<ext> / .jpg / .png
TextureResolution Resolve_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists, bool flatLayout = false);

// Complete loader lookup: after DDS/TGA, try the packed JPEG colour stream
// (RW 0x530D29). A companion PNG is decoded by the device, not a colour fallback.
TextureResolution Resolve_Packed_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists, bool flatLayout = false);

// What retail draws for a texture whose file is missing or does not decode (lane UI-1).  TARGET FACT: the texture load RW 0x53193E (reached
// through RW 0x532847 -> 0x531BF9), when it holds no file bytes (+0x14 null; INFERENCE: a file that is not found leaves it null, the reader that fills it was not
// traced) or no D3D texture came out of them (+0x8 null), creates a 1 x 1
// texture of format 0x15 (D3DFMT_A8R8G8B8, one level: RW 0x531B07 .. 0x531B14 call RW 0x530BB3 with 1, 1, 0x15, 1, 1, 0, 0) and writes the pixel
// (0, 0) as 0xFFFF00FF (RW 0x531B44: push 0xFFFF00FF; RW 0x5165E0 draws one pixel): an opaque magenta texture, sampled like any other.  DONOR
// difference: ZH's MissingTexture (WW3D2 missingtexture.cpp) is a 128 x 128 half-transparent 0x7FFF00FF; RotWK has no such constant.
constexpr unsigned W3D_MISSING_TEXTURE_COLOR = 0xFFFF00FFu; // A8R8G8B8
