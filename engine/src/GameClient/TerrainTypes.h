// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// terrain.ini terrain types, as far as map rendering needs them: name -> texture file.
//   donor: ZH GameClient/TerrainTypes.cpp (TerrainTypeCollection::findTerrain, Terrain block with
//     "Texture = X.tga"), texture path = "Art/Terrain/" + Texture (TERRAIN_TGA_DIR_PATH).
//   target fact (Open-BFME-2 WorldHeightMapReadTexClass.cpp): the BFME2 normal map name is the
//     texture name with "_nrm" inserted before the extension (makeNrmTextureName).
//
// This is a name-level scan, not the INI system (another lane owns engine/src/Common/INI*): it reads
// "Terrain <name>" ... "End" blocks and the Texture key, nothing else.

#pragma once

#include "Common/ArchiveFileSystem.h"

#include <map>
#include <string>
#include <vector>

struct TerrainTypeIndex
{
	std::map<std::string, std::string> textureByName; // lower-case terrain type name -> texture file as written
	int duplicates = 0;                               // names defined more than once (the later block wins)
	size_t blocksScanned = 0;

	// nullptr when the terrain type is unknown (case-insensitive).
	const std::string *findTexture(const std::string &terrainName) const;

	static std::string textureArchivePath(const std::string &texture); // "art\\terrain\\<texture>"
	static std::string normalMapName(const std::string &texture);       // "X.tga" -> "X_nrm.tga"
};

namespace TerrainTypes
{
void scanText(const std::string &text, TerrainTypeIndex &out);
// Reads data\ini\terrain.ini from the mounted archives. False + *error if it is missing.
bool load(ArchiveFileSystem &fs, TerrainTypeIndex &out, std::string *error);
} // namespace TerrainTypes
