// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Road types (roads.ini), connected straight segments and atlas curve pieces.
//   donor: ZH GameClient/TerrainRoads.cpp (Road <name>: Texture, RoadWidth, RoadWidthInTexture) and
//     W3DRoadBuffer.cpp addMapObjects :1606-1686 (a FLAG_ROAD_POINT1 object immediately followed by a
//     FLAG_ROAD_POINT2 object is one segment; identical locations are nudged by 0.25; identical segments
//     are dropped), preloadRoadSegment :1142-1170 and loadFloat4PtSection :567-780 (corners = locations +-
//     normal * (RoadWidth * RoadWidthInTexture / 2); columns every MAP_XY_FACTOR; u = U/(scale*4),
//     v = 85/512 - V/(scale*4); z = the maximum cell height across the segment's width).
//   higher-priority donor: Open-BFME-2 Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DRoadBuffer.cpp:
//     miter, insertCurveSegmentAt and loadCurve supply the connected
//     degree-two corner rules and atlas pieces. See docs/PATH_RENDERING.md for adaptations.
//     Tee/four-way stubs and authored JOIN caps use the BFME2 atlas layout.
//     Y/H junctions, cross-type alignment and BFME2 stacking remain S-036.
//
// Name-level INI scan, not the INI system (another lane).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/MapObject.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <map>
#include <string>
#include <vector>

// Retail BridgeTowerType: BFME2 0x00280BE6 / RW 0x006813E2 (Bridge::Bridge RW 0x00681420)
enum BridgeTowerType
{
	BRIDGE_TOWER_FROM_LEFT = 0,
	BRIDGE_TOWER_FROM_RIGHT = 1,
	BRIDGE_TOWER_TO_LEFT = 2,
	BRIDGE_TOWER_TO_RIGHT = 3,
	BRIDGE_MAX_TOWERS = 4
};

struct RoadType
{
	std::string name;
	std::string texture;           // as written, e.g. "TRFourLaneDirt.tga"
	float roadWidth = 0.0f;        // RoadWidth (world units)
	float roadWidthInTexture = 0.0f; // RoadWidthInTexture (fraction of the texture height the road covers)
	bool isBridge = false;

	// Bridge fields: retail table RW 0x00C19DC0 / BFME2 0x00803A28
	float bridgeScale = 1.0f;                       // BridgeScale (+0x1C, RW 0x0042ED00)
	std::string scaffoldObjectName;                 // ScaffoldObjectName (+0x20, RW 0x0042EE5E, getter RW 0x008DA9C6)
	std::string scaffoldSupportObjectName;          // ScaffoldSupportObjectName (+0x24, RW 0x0042EE5E, getter RW 0x0096589F)
	float radarColor[3] = { 0.0f, 0.0f, 0.0f };     // RadarColor (+0x28, RW 0x0042EF99, reader RW 0x0044F790)
	float transitionEffectsHeight = 0.0f;           // TransitionEffectsHeight (+0x144, RW 0x0042ED00)
	int numFXPerType = 0;                           // NumFXPerType (+0x148, RW 0x0042EC5E)
	std::string bridgeModelName;                    // BridgeModelName (+0x34, RW 0x0042EE5E)
	std::string bridgeModelNameDamaged;             // BridgeModelNameDamaged (+0x3C, RW 0x0042EE5E)
	std::string textureDamaged;                     // TextureDamaged (+0x40, RW 0x0042EE5E)
	std::string bridgeModelNameReallyDamaged;       // BridgeModelNameReallyDamaged (+0x44, RW 0x0042EE5E)
	std::string textureReallyDamaged;               // TextureReallyDamaged (+0x48, RW 0x0042EE5E)
	std::string bridgeModelNameBroken;              // BridgeModelNameBroken (+0x4C, RW 0x0042EE5E)
	std::string textureBroken;                      // TextureBroken (+0x50, RW 0x0042EE5E)
	std::string towerObjectNameFromLeft;            // TowerObjectNameFromLeft (+0x54, RW 0x0042EE5E)
	std::string towerObjectNameFromRight;           // TowerObjectNameFromRight (+0x58, RW 0x0042EE5E)
	std::string towerObjectNameToLeft;              // TowerObjectNameToLeft (+0x5C, RW 0x0042EE5E)
	std::string towerObjectNameToRight;             // TowerObjectNameToRight (+0x60, RW 0x0042EE5E)
	std::string damagedToSound;                     // DamagedToSound (+0x68, RW 0x0073B217)
	std::string repairedToSound;                    // RepairedToSound (+0xD8, RW 0x0073B217)

	// Retail W3DBridge::load (RW 0x004DDF7C / BFME2 0x000DE6B5): model and texture by damage state
	// (0 = BODY_PRISTINE, 1 = BODY_DAMAGED, 2 = BODY_REALLYDAMAGED, 3 = BODY_RUBBLE)
	std::string modelForDamage(int state) const;
	std::string textureForDamage(int state) const;

	// Retail TerrainRoadType::getTowerObjectName (RW 0x006813E2 / BFME2 0x00280BE6)
	std::string towerObjectName(BridgeTowerType tower) const;

	// Retail TerrainRoadType getters: RW 0x008DA9C6 / BFME2 0x002D9BC1 and RW 0x0096589F / BFME2 0x0056394A
	const std::string &getScaffoldObjectName() const;
	const std::string &getScaffoldSupportObjectName() const;
};

struct RoadTypeIndex
{
	std::map<std::string, RoadType> byName; // lower-case name
	const RoadType *find(const std::string &name) const;
};

struct RoadStrip
{
	std::string roadName;
	std::string texture;
	std::vector<float> position; // xyz SAGE, 2 vertices (bottom, top) per column
	std::vector<float> uv;
	std::vector<std::uint32_t> index;
};

// Retail BridgeSegment: constructed from FLAG_BRIDGE_POINT1 and FLAG_BRIDGE_POINT2 map objects
// (W3DBridgeBuffer::loadBridges RW 0x00681420..0x006819B0 / BFME2 0x000DFB3C; W3DBridge::getBridgeInfo BFME2 0x000DDB94)
struct BridgeSegment
{
	std::string bridgeName;
	std::string type;
	Coord3D from;
	Coord3D to;
	Coord3D center;
	float angle = 0.0f;
	float scale = 1.0f;
	float radarColor[3] = { 0.0f, 0.0f, 0.0f };
	Coord3D fromLeft;
	Coord3D fromRight;
	Coord3D toLeft;
	Coord3D toRight;
	Coord3D towerPositions[BRIDGE_MAX_TOWERS];
	std::string towerTemplates[BRIDGE_MAX_TOWERS];
	std::string scaffoldTemplate;
	std::string scaffoldSupportTemplate;
	float transitionEffectsHeight = 0.0f;
	int numFXPerType = 0;
	std::string damagedToSound;
	std::string repairedToSound;

	const RoadType *roadType = nullptr;

	std::string modelForDamage(int state) const
	{
		return roadType ? roadType->modelForDamage(state) : "";
	}
	std::string textureForDamage(int state) const
	{
		return roadType ? roadType->textureForDamage(state) : "";
	}
	std::string towerObjectName(BridgeTowerType tower) const
	{
		return roadType ? roadType->towerObjectName(tower) : "";
	}
	const std::string &getScaffoldObjectName() const
	{
		return roadType ? roadType->getScaffoldObjectName() : scaffoldTemplate;
	}
	const std::string &getScaffoldSupportObjectName() const
	{
		return roadType ? roadType->getScaffoldSupportObjectName() : scaffoldSupportTemplate;
	}
};

struct RoadBuildReport
{
	int teeJoins = 0;
	int fourWayJoins = 0;
	int endCaps = 0;
	int specialJunctionsPending = 0;
	int miteredJoins = 0;
	int roundedJoins = 0;
	int pairs = 0;            // valid POINT1/POINT2 pairs
	int orphanPoints = 0;     // road-flagged objects that do not form a pair
	int duplicateSegments = 0;
	int nudgedZeroLength = 0;
	int unknownRoadTypes = 0;
	std::vector<std::string> unknownNames;
	size_t strips = 0;
	size_t vertices = 0;
	size_t triangles = 0;
	int bridgePairs = 0;
	int unknownBridgeTypes = 0;
	int invalidBridgeWidth = 0;
};

namespace TerrainRoads
{
void scanText(const std::string &text, RoadTypeIndex &out);
bool load(ArchiveFileSystem &fs, RoadTypeIndex &out, std::string *error);

// Retail parseRGBColor: RW 0x0042EF99 / BFME2 0x0002F259 (R: G: B: ints 0..255 normalized to [0, 1])
bool parseRGBColor(const std::string &val, float outRgb[3]);

// liftWorldUnits: presentation-only raise above the terrain (ZH uses MAP_HEIGHT_SCALE/8 = 0.005, which
// z-fights in Godot's depth buffer; named choice, reported by the caller).
void buildStrips(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	float liftWorldUnits, std::vector<RoadStrip> &out, RoadBuildReport &report);

// Retail W3DBridgeBuffer::loadBridges: builds bridge segments from FLAG_BRIDGE_POINT1/2 map objects
void buildBridges(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	std::vector<BridgeSegment> &out, RoadBuildReport &report);

// ZH getMaxCellHeight: the maximum of the four corner heights of the cell containing (x,y), in world units.
float getMaxCellHeight(const WorldHeightMap &map, float x, float y);
} // namespace TerrainRoads

// How retail shades a road (lane ROADS-1). BFME2 W3DRoadBufferDrawRoads.cpp:349-365 (RotWK RW 0x4d73c4 .. 0x4d7458) draws each road type through the
// terrain's rendering method, terrain.fxo technique "Default", with Terrain.RenderingMode = 2 (setRenderingMode(2), RW 0x4d73cc), the road texture
// as BaseTexture and the road's _nrm companion as NormalTexture. The pixel shader is PSDefault_Array[min(RenderingMode, 4) + 5 * min(NumShadows, 1) +
// 10 * IsResourceTextureEnabled] (index preshader at terrain.fxo 0x1c4bc): element 2 (ps_2_0 blob 37 @0xc160) without shadows. The vertex
// shader is the effect's single vs_1_1 blob (blob 60 @0x1c780). godot/shaders/road.gdshader ports both.
namespace RoadShading
{
constexpr int RENDERING_MODE = 2;
// The road vertex normal in SAGE space: loadFloat4PtSection writes (0, 0, 1.0f) (BFME2 W3DRoadBuffer.cpp:948, RW 0x4d7d32 .. 0x4d7d3c: nx = ny =
// 0.0f, nz = the float 1.0 at RW 0xBD1908), so a road's tangent frame is world-aligned whatever its direction.
constexpr float VERTEX_NORMAL[3] = { 0.0f, 0.0f, 1.0f };
// The normal texture of a road type with no _nrm companion: W3DRoadBuffer::allocateRoadBuffers (RW 0x4d7132 .. 0x4d7176; the holder at
// W3DRoadBuffer + 0x18) creates a 1x1 texture of format 0x15 (D3DFMT_A8R8G8B8) and SurfaceClass::DrawPixel (RW 0x5165e0 stores the dword) paints
// it 0x007F7FFF: R = G = 127, B = 255, A = 0. The alpha is the specular mask of element 2, so a road without a normal map has no specular.
constexpr std::uint32_t FALLBACK_NORMAL_ARGB = 0x007F7FFFu;
constexpr std::uint8_t FALLBACK_NORMAL_RGBA[4] = { 0x7F, 0x7F, 0xFF, 0x00 };

// PSDefault_Array's element for the effect parameters (the preshader at terrain.fxo 0x1c4bc).
int pixelShaderElement(int renderingMode, int numShadows, bool resourceTextureEnabled);
// RoadType::loadTexture (BFME2 W3DRoadBuffer.cpp:294-309, makeNrmTextureName): "X.tga" -> "X_nrm.tga".
std::string normalTextureName(const std::string &roadTexture);
} // namespace RoadShading
