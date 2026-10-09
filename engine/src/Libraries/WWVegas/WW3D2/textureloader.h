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
	// Set only when nothing resolved: the file art\terrain\<name as written> exists. Retail's file name builder for a texture
	// (GameFileClass::Set_Name, RW 0x477D1C: .tga / .dds / .png / .jpg names become Art/CompiledTextures/<first two characters>/<name>,
	// or Art/Textures/<name> for apt_ names; nothing else) never looks in Art\Terrain; only the terrain texture loader (RW 0x709F77,
	// prefix "Art\Terrain\") does. A model naming such a file is a missing texture in retail too: the file is reported, not used.
	std::string TerrainFolderPath;
};

TextureResolution Resolve_W3D_Texture(const std::string &name, const std::function<bool(const std::string &)> &exists);

// What retail draws for a texture whose file is missing or does not decode (lane UI-1).  TARGET FACT: the texture load RW 0x53193E (reached
// through RW 0x532847 -> 0x531BF9), when it holds no file bytes (+0x14 null; INFERENCE: a file that is not found leaves it null, the reader that fills it was not
// traced) or no D3D texture came out of them (+0x8 null), creates a 1 x 1
// texture of format 0x15 (D3DFMT_A8R8G8B8, one level: RW 0x531B07 .. 0x531B14 call RW 0x530BB3 with 1, 1, 0x15, 1, 1, 0, 0) and writes the pixel
// (0, 0) as 0xFFFF00FF (RW 0x531B44: push 0xFFFF00FF; RW 0x5165E0 draws one pixel): an opaque magenta texture, sampled like any other.  DONOR
// difference: ZH's MissingTexture (WW3D2 missingtexture.cpp) is a 128 x 128 half-transparent 0x7FFF00FF; RotWK has no such constant.
constexpr unsigned W3D_MISSING_TEXTURE_COLOR = 0xFFFF00FFu; // A8R8G8B8
