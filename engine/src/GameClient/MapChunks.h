// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The remaining map chunks as plain structs, with their sources. Evidence tags follow the spec
// (maps-and-terrain.md 0.1): TARGET = BFME1/BFME2 decompile body (retail evidence), DONOR = ZH,
// CORPUS = the layout decodes every retail instance with zero leftover bytes (spec 1.6), OS =
// OpenSAGE used only as a cross-check of the layout (LGPL: no code taken).
//
//   WorldInfo v1           DONOR ZH WorldHeightMap.cpp:763-774 (one Dict)
//   MPPositionList/Info    TARGET Open-BFME-2 GameClient/GUI/MapMetaData_MPPositionWriters.cpp:40-75
//   ObjectsList/Object v3  TARGET Open-BFME-2 GameClient/MapObjectWriteObjectsDataChunk.cpp:69-84
//   PolygonTriggers v4/v5  DONOR ZH PolygonTrigger.cpp:135-230; v5 CORPUS+OS
//   TriggerAreas v1        TARGET Open-BFME-2 Common/Rva002E37E5Write.cpp:53-64, Rva0030B2B6Write.cpp:27-54
//   StandingWaterAreas v2  CORPUS+OS (Map/StandingWaterArea.cs layout)
//   RiverAreas v1/v2       CORPUS+OS (Map/RiverArea.cs layout)
//   StandingWaveAreas v1/2 CORPUS+OS (Map/StandingWaveArea.cs layout)
//   PostEffectsChunk v1    CORPUS+OS
//   GlobalLighting v7/v8   TARGET writer Open-BFME-1 GameClient/Rva00748860WriteGlobalLighting.cpp and
//                          reader WorldHeightMap.cpp:904-1048 (light ORDER); v8 tail CORPUS. OpenSAGE's
//                          order and shadow-colour position are wrong for BFME2/RotWK (spec 1.7.17).
//   EnvironmentData v2/v3  TARGET Open-BFME-1 BaseHeightMapWriteEnvironmentData.cpp (v3)
//   NamedCameras v2        TARGET Open-BFME-1 Rva00747E80WriteNamedCameras.cpp
//   CameraAnimationList    CORPUS+OS
//   WaypointsList v1       DONOR ZH TerrainLogic.cpp:1280; TARGET Open-BFME-2 TerrainLogic_parseWaypointData.cpp
//   SkyboxSettings v1      TARGET Open-BFME-1 Water/WaterRenderObjReadSkyBoxSettings.cpp

#pragma once

#include "Common/Dict.h"
#include "Common/MapObject.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class DataChunkInput;

struct MPPositionInfo
{
	bool isHuman = false, isComputer = false, loadAIScript = false;
	std::int32_t team = -1;
	std::vector<std::string> sideRestriction;
};

struct Point2F
{
	float x = 0.0f, y = 0.0f;
};

struct PolygonTriggerPoint
{
	std::int32_t x = 0, y = 0, z = 0;
};

// Legacy (BFME1-era library/base maps, shellmapbackup). ZH PolygonTrigger.cpp.
struct PolygonTrigger
{
	std::string name;
	std::string layer;
	std::int32_t id = 0;
	bool isWaterArea = false;
	bool isRiver = false;
	std::int32_t riverStart = 0;
	// v5 presentation fields
	std::string riverTexture, noiseTexture, alphaEdgeTexture, sparkleTexture, bumpTexture, skyTexture;
	bool additiveBlending = false;
	std::uint8_t colorR = 0, colorG = 0, colorB = 0;
	float uvScrollX = 0.0f, uvScrollY = 0.0f, alpha = 0.0f;
	std::vector<PolygonTriggerPoint> points;
	int version = 0;
};

struct TriggerArea
{
	std::string name;
	std::string layer;
	std::int32_t id = 0;
	std::vector<Point2F> points; // world XY; no Z (2D prism)
	std::int32_t tail = 0;       // 0 in all 577 corpus instances
};

struct StandingWaterArea
{
	std::uint32_t uniqueId = 0;
	std::string name, layer;
	float uvScrollSpeed = 0.0f;
	bool additiveBlending = false;
	std::string bumpMapTexture, skyTexture;
	std::vector<Point2F> points;
	std::int32_t waterHeight = 0; // world Z units (CORPUS: Fords of Isen water at 294)
	std::string fxShader;         // a W3D whose material is WaterShader.FX
	std::string depthColors;      // 1-row LUT texture or ""
};

struct RiverArea
{
	std::uint32_t uniqueId = 0;
	std::string name, layer;
	float uvScrollSpeed = 0.0f;
	bool additiveBlending = false;
	std::string riverTexture, noiseTexture, alphaEdgeTexture, sparkleTexture;
	std::uint8_t r = 0, g = 0, b = 0, a0 = 0; // 4th byte is 0 in the corpus
	float alpha = 0.0f;
	std::int32_t waterHeight = 0;
	std::string riverType; // v3 only (not in the corpus)
	std::string minimumWaterLod; // "" or "Medium"
	struct Line
	{
		float x0, y0, x1, y1; // cross-section edge, upstream -> downstream
	};
	std::vector<Line> lines;
	int version = 0;
};

struct StandingWaveArea
{
	std::uint32_t uniqueId = 0;
	std::string name, layer;
	float uvScrollSpeed = 0.0f;
	bool additive = false;
	std::vector<Point2F> points;
	std::int32_t zero = 0;
	std::int32_t finalWidth = 0, finalHeight = 0, initialWidthFraction = 0, initialHeightFraction = 0, initialVelocity = 0,
		timeToFadeMs = 0, timeToCompressMs = 0, timeOffset2ndWaveMs = 0, distanceFromShore = 0;
	std::string texture;
	std::int32_t enablePcaWave = 0; // v2
	int version = 0;
};

struct PostEffect
{
	std::string name;
	float blendFactor = 0.0f;
	std::string lookupImage;
};

// ZH GlobalData::TerrainLighting
struct GlobalLight
{
	float ambient[3] = { 0, 0, 0 };
	float diffuse[3] = { 0, 0, 0 };
	float lightPos[3] = { 0, 0, 0 }; // a direction
};

// File order inside one time of day (spec 1.7.17, target-proven): terrain[0], objects[0], objects[1],
// objects[2], terrain[1], terrain[2], infantry[0], infantry[1], infantry[2].
struct TimeOfDayLights
{
	GlobalLight terrain[3];
	GlobalLight objects[3];
	GlobalLight infantry[3];
};

struct GlobalLightingData
{
	int version = 0;
	std::int32_t timeOfDay = 0;       // 1 Morning, 2 Afternoon, 3 Evening, 4 Night (ZH GameType.h:70-77)
	TimeOfDayLights tod[4];           // index 0..3 = time of day 1..4
	float terrainLightingMultiplier = 0.0f; // corpus 2.0 (1.0 in 5 of 181 maps); > 1 sets the overbright byte RW 0xD9B03C (RW 0x4AC97F): MODULATE2X, doubled object lights
	std::int32_t flagDBD = 0;         // corpus 0; drives the BFME "highlight" screen filter (meaning not fully known)
	float vecA[3] = {}, vecB[3] = {}, vecC[3] = {}; // corpus 0.5 each; meaning UNKNOWN
	std::uint32_t shadowColor = 0;    // ARGB
	float v8Extra[3] = { 1, 1, 1 };   // v8 only; OpenSAGE calls it NoCloudFactor (UNKNOWN)
	bool hasV8Extra = false;
};

struct EnvironmentData
{
	int version = 0;
	float waterMaxAlphaDepth = 0.0f, deepWaterAlpha = 0.0f; // v>=3 (names from OS)
	bool isMacroTextureStretched = false;
	std::string macroTexture, cloudTexture;
};

struct NamedCamera
{
	Coord3D position;
	std::string name;
	float values[6] = {}; // the B1 writer emits m_values[2],[3],[1],[4],[5],[0]; semantics INFERRED (pitch,roll,yaw,zoom,fov,?)
};

struct CameraAnimationFrame
{
	std::uint32_t frame = 0;
	std::string interp; // 'catm' | 'line' (the 4 file bytes reversed)
	float pos[3] = {};
	float quat[4] = {}; // free only
	float fovLike = 0.0f;
	float roll = 0.0f; // look only
};

struct CameraLookAtKey
{
	std::uint32_t frame = 0;
	std::string interp;
	float lookAt[3] = {};
};

struct CameraAnimation
{
	std::string type; // "free" or "look"
	std::string name;
	std::uint32_t numFrames = 0, startOffset = 0;
	std::vector<CameraAnimationFrame> keys;
	std::vector<CameraLookAtKey> lookAtKeys;
	int version = 0;
};

struct WaypointLink
{
	std::int32_t from = 0, to = 0; // directed
};

struct SkyboxSettings
{
	float position[3] = {};
	float scale = 0.0f;
	float rotation = 0.0f;
	std::string textureScheme;
};

// .scb-only chunks (spec 1.7.8)
struct ScbExtras
{
	bool hasImportSize = false;
	std::uint32_t importSize0 = 0, importSize1 = 0;
	bool hasScriptsPlayers = false;
	std::vector<std::string> playerNames;
	std::vector<Dict> playerDicts;
	bool hasScriptTeams = false;
	std::vector<Dict> scriptTeams;
};

// Everything in a map file that is not heights, blend data, sides or scripts.
// CastleTemplates (RotWK: the base layout files of Bases.big / bases.big, `.bse`; lane BUILD-1), TARGET RW 0x731010 (the chunk parser; RW 0x7311FB opens
// "Bases\\<name>\\<name>.bse" and parses its CastleTemplates chunk): per template { nameKey; i32 n; n * entry; v >= 2: i32 m; m * line }.
//   entry: asciiString (first: read, kept by the store as the entry's first string, empty in 5442 of 5447 retail entries: the AI bases use it), asciiString (the object
//          template name), real x, y, z (relative to the castle centre), real angle, v >= 4: i32, i32 (read and dropped by RW 0x731010; retail values such as 40/1, 500/2)
//   line:  v >= 5: asciiString (read and dropped), i32 k, k points (v >= 3: real x, y; v < 3: i32 x, y, z (z dropped)).
// Retail versions: 1 (1 file), 2 (28), 3 (52), 4 (2), 5 (124). All 207 retail .bse files parse to the last byte (tools/ python census).
struct CastleTemplateEntry
{
	std::string firstName;     ///< the first string of the entry
	std::string templateName;
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float angle = 0.0f;
	std::int32_t value1 = 0, value2 = 0; ///< v >= 4 (dropped by retail's store)
};
struct CastleTemplateLine
{
	std::vector<Point2F> points;
};
struct CastleTemplate
{
	std::string name;
	int version = 0;
	std::vector<CastleTemplateEntry> entries;
	std::vector<CastleTemplateLine> lines;
};

struct MapChunks
{
	bool hasWorldInfo = false;
	Dict worldInfo;

	bool hasMPPositionList = false;
	std::vector<MPPositionInfo> mpPositions;

	bool hasObjectsList = false;
	std::vector<MapObject> objects;

	bool hasPolygonTriggers = false;
	std::vector<PolygonTrigger> polygonTriggers;

	bool hasTriggerAreas = false;
	std::vector<TriggerArea> triggerAreas;

	bool hasStandingWaterAreas = false;
	std::vector<StandingWaterArea> standingWaterAreas;
	bool hasRiverAreas = false;
	std::vector<RiverArea> riverAreas;
	bool hasStandingWaveAreas = false;
	std::vector<StandingWaveArea> standingWaveAreas;

	bool hasPostEffects = false;
	std::vector<PostEffect> postEffects;

	bool hasGlobalLighting = false;
	GlobalLightingData lighting;

	bool hasEnvironmentData = false;
	EnvironmentData environment;

	bool hasNamedCameras = false;
	std::vector<NamedCamera> namedCameras;

	bool hasCameraAnimations = false;
	std::vector<CameraAnimation> cameraAnimations;

	bool hasWaypointsList = false;
	std::vector<WaypointLink> waypointLinks;

	bool hasSkybox = false;
	SkyboxSettings skybox;

	ScbExtras scb;

	bool hasCastleTemplates = false;
	std::vector<CastleTemplate> castleTemplates;

	// Objects ZH ParseObjectData would drop for z outside [-1000, 1593.75]; kept here, see MapObject.h.
	int objectsOutsideZhZRange = 0;
};

namespace MapChunkParse
{
void registerParsers(DataChunkInput &file, MapChunks *chunks);
}
