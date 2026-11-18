// OpenBFME. GPL-3.0. BFME1 road-endpoint Bridge producer (1.03 0x6DA8A0).
#pragma once
#include "Common/MapObject.h"
#include <string>
#include <vector>
class ArchiveFileSystem;class GameLogic;class TerrainLogic;class WW3DAssetManager;
namespace Bfme1MapBridges {
bool create(ArchiveFileSystem &,WW3DAssetManager &,GameLogic &,TerrainLogic &,const std::vector<MapObject> &,std::vector<std::string> &stops,std::string *error);
}
