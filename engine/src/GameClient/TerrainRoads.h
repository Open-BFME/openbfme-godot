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

struct RoadType
{
	std::string name;
	std::string texture;           // as written, e.g. "TRFourLaneDirt.tga"
	float roadWidth = 0.0f;        // RoadWidth (world units)
	float roadWidthInTexture = 0.0f; // RoadWidthInTexture (fraction of the texture height the road covers)
	bool isBridge = false;

	// Bridge blocks: RotWK 2.01's TerrainRoadType, its Bridge field table RW 0xC19DC0 (22 rows) read by the Bridge block parse
	// RW 0x5DA647. Defaults are the TerrainRoadType constructor's (RW 0x6DC310): names empty, BridgeScale 1.0, the rest 0. `texture`
	// above is the table's Texture row (+0x38). Body states index PRISTINE, DAMAGED, REALLYDAMAGED, RUBBLE (RW 0xDA2038).
	static constexpr int BODY_STATES = 4;
	static constexpr int MAX_BRIDGE_BODY_FX = 3; // TransitionToOCL / TransitionToFX: EffectNum 1..3 (RW 0x6DC1AF)
	float bridgeScale = 1.0f;                  // +0x1C BridgeScale (parseReal RW 0x42ED00)
	std::string scaffoldObjectName;            // +0x20 ScaffoldObjectName (parseAsciiString RW 0x42EE5E)
	std::string scaffoldSupportObjectName;     // +0x24 ScaffoldSupportObjectName (RW 0x42EE5E)
	float radarColor[3] = { 0.0f, 0.0f, 0.0f }; // +0x28 RadarColor (parseRGBColor RW 0x42EF99: R: G: B: 0..255, times 1/255)
	std::string bridgeModelName;               // +0x34 BridgeModelName (RW 0x42EE5E)
	std::string bridgeModelNameDamaged;        // +0x3C BridgeModelNameDamaged
	std::string textureDamaged;                // +0x40 TextureDamaged
	std::string bridgeModelNameReallyDamaged;  // +0x44 BridgeModelNameReallyDamaged
	std::string textureReallyDamaged;          // +0x48 TextureReallyDamaged
	std::string bridgeModelNameBroken;         // +0x4C BridgeModelNameBroken
	std::string textureBroken;                 // +0x50 TextureBroken
	// +0x54 .. +0x60 TowerObjectNameFromLeft, FromRight, ToLeft, ToRight, indexed by BridgeTowerType (getTowerObjectName RW 0x6813E2)
	std::string towerObjectName[4];
	// Transition effects by the body state reached. DamagedToSound / RepairedToSound (parseAudioEventRTS RW 0x73B217) write the
	// DAMAGED entry (+0x68, +0xD8); TransitionToOCL (RW 0x6DC132) and TransitionToFX (RW 0x6DC221) any state and effect.
	std::string damageToSound[BODY_STATES];                       // +0x64
	std::string damageToOCL[BODY_STATES][MAX_BRIDGE_BODY_FX];     // +0x74
	std::string damageToFX[BODY_STATES][MAX_BRIDGE_BODY_FX];      // +0xA4
	std::string repairedToSound[BODY_STATES];                     // +0xD4
	std::string repairedToOCL[BODY_STATES][MAX_BRIDGE_BODY_FX];   // +0xE4
	std::string repairedToFX[BODY_STATES][MAX_BRIDGE_BODY_FX];    // +0x114
	float transitionEffectsHeight = 0.0f;      // +0x144 TransitionEffectsHeight (RW 0x42ED00)
	int numFXPerType = 0;                      // +0x148 NumFXPerType (parseInt RW 0x42EC5E)
};

// BridgeTowerType, the index of RoadType::towerObjectName (getTowerObjectName RW 0x6813E2 reads +0x54 + 4 * type)
enum BridgeTowerType
{
	BRIDGE_TOWER_FROM_LEFT = 0,
	BRIDGE_TOWER_FROM_RIGHT = 1,
	BRIDGE_TOWER_TO_LEFT = 2,
	BRIDGE_TOWER_TO_RIGHT = 3,
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
};

namespace TerrainRoads
{
// false + *error on a Bridge block retail rejects (an unknown field, a bad RadarColor or transition, a road redefined as a bridge).
bool scanText(const std::string &text, RoadTypeIndex &out, std::string *error = nullptr);
bool load(ArchiveFileSystem &fs, RoadTypeIndex &out, std::string *error);

// liftWorldUnits: presentation-only raise above the terrain (ZH uses MAP_HEIGHT_SCALE/8 = 0.005, which
// z-fights in Godot's depth buffer; named choice, reported by the caller).
void buildStrips(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	float liftWorldUnits, std::vector<RoadStrip> &out, RoadBuildReport &report);

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
