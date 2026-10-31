// OpenBFME unit tests, lane TERRAINSCALE-1 (stop S-033): the map's terrain light scale (GlobalLighting chunk v8, GlobalData + 0x944 / 0x948 /
// 0x94C) and the detail tiers at which retail applies it. The binary facts (the eight readers, their guards, the terrain rendering mode) are
// pinned by tools/render/test_terrain_light_scale_binary_facts.py; the retail map values by the same file and by the retail case below.

#include "doctest.h"
#include "RetailTestMount.h"

#include "GameClient/MapChunks.h"
#include "GameClient/MapStops.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"
#include "GameEngineDevice/W3DDevice/GameClient/TerrainLightScale.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <string>
#include <vector>

namespace
{

void setLight(GlobalLight &l, float a, float d, float px, float py, float pz)
{
	for (int c = 0; c < 3; ++c)
	{
		l.ambient[c] = a;
		l.diffuse[c] = d;
	}
	l.lightPos[0] = px;
	l.lightPos[1] = py;
	l.lightPos[2] = pz;
}

GlobalLightingData v8Lighting()
{
	GlobalLightingData g;
	g.version = 8;
	g.timeOfDay = 1;
	g.hasTerrainLightScale = true;
	g.terrainLightScale[0] = 0.5f;
	g.terrainLightScale[1] = 0.25f;
	g.terrainLightScale[2] = 0.125f;
	return g;
}

TerrainLightScale::Lod lodOf(bool normalMap, int terrainLOD, int shaderLOD)
{
	TerrainLightScale::Lod lod;
	lod.useTerrainNormalMap = normalMap;
	lod.terrainLOD = terrainLOD;
	lod.shaderLOD = shaderLOD;
	return lod;
}

} // namespace

TEST_CASE("terrain light scale: the terrain rendering mode is the terrain.fx technique level (RW 0x4E0E5A, 0x4E0F5D)")
{
	std::string err;
	// UseTerrainNormalMap (GlobalData + 0x49) requests the unsuffixed technique, level 2, at every TerrainLOD
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(true, 0, 0), &err) == 2);
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(true, 1, 0), &err) == 2);
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(true, 2, 0), &err) == 2);
	// without it the request is min(TerrainLOD, 1): "Default_L" (0) or "Default_M" (1)
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(false, 0, 2), &err) == 0);
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(false, 1, 2), &err) == 1);
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(false, 2, 2), &err) == 1);
	// retail would index its request table out of bounds: an error, never a guess
	err.clear();
	CHECK(TerrainLightScale::terrainRenderingMode(lodOf(false, -1, 2), &err) == -1);
	CHECK(err.find("negative") != std::string::npos);
}

TEST_CASE("terrain light scale: the terrain is scaled at rendering mode 0 only, the objects below shader LOD 2")
{
	const GlobalLightingData g = v8Lighting();
	TerrainLightScale::Scales s;
	std::string err;

	// GameLOD.ini High / UltraHigh (UseTerrainNormalMap = Yes, ShaderLOD 2 / 3): nothing is scaled
	REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(true, 2, 2), s, &err), err);
	CHECK(s.terrainMode == 2);
	CHECK_FALSE(s.terrainScaled);
	CHECK_FALSE(s.objectsScaled);
	for (int c = 0; c < 3; ++c)
	{
		CHECK(s.terrain[c] == 1.0f);
		CHECK(s.objects[c] == 1.0f);
		CHECK(s.raw[c] == g.terrainLightScale[c]);
		CHECK(s.scorch[c] == g.terrainLightScale[c]); // the scorch colour (RW 0x4670E1) has no guard
	}
	REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(true, 2, 3), s, &err), err);
	CHECK_FALSE(s.objectsScaled);

	// Medium (distance-dependent textures, ShaderLOD Medium = 1): the terrain keeps rendering mode 1, the object lights are scaled
	REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(false, 1, 1), s, &err), err);
	CHECK(s.terrainMode == 1);
	CHECK_FALSE(s.terrainScaled);
	CHECK(s.objectsScaled);
	CHECK(s.terrain[0] == 1.0f);
	CHECK(s.objects[0] == 0.5f);
	CHECK(s.objects[1] == 0.25f);
	CHECK(s.objects[2] == 0.125f);

	// VeryLow / Low (no normal map, no distance-dependent textures, ShaderLOD Low = 0): everything is scaled
	REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(false, 0, 0), s, &err), err);
	CHECK(s.terrainMode == 0);
	CHECK(s.terrainScaled);
	CHECK(s.objectsScaled);
	CHECK(s.terrain[0] == 0.5f);
	CHECK(s.terrain[1] == 0.25f);
	CHECK(s.terrain[2] == 0.125f);

	// the inputs outside the static levels are errors
	CHECK_FALSE(TerrainLightScale::resolve(g, lodOf(false, -1, 0), s, &err));
	CHECK_FALSE(TerrainLightScale::resolve(g, lodOf(true, 2, -1), s, &err));
}

TEST_CASE("terrain light scale: a chunk older than version 8 stores 1.0 (RW 0x4ACAD1)")
{
	GlobalLightingData g;
	g.version = 7;
	TerrainLightScale::Scales s;
	std::string err;
	REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(false, 0, 0), s, &err), err);
	for (int c = 0; c < 3; ++c)
	{
		CHECK(s.raw[c] == 1.0f);
		CHECK(s.terrain[c] == 1.0f);
		CHECK(s.scorch[c] == 1.0f);
	}
	CHECK(s.terrainScaled); // the tier still says "scaled": by 1
}

TEST_CASE("terrain light scale: the scale multiplies each terrain light's diffuse in the vertex colour (RW 0x4690C1)")
{
	TimeOfDayLights L;
	setLight(L.terrain[0], 0.25f, 0.5f, 0.0f, 0.0f, -1.0f); // ambient 0.25, diffuse 0.5, toward the light (0, 0, 1)
	setLight(L.terrain[1], 0.9f, 0.5f, 0.0f, 0.0f, -1.0f);  // ambient of the accent lights is not read
	setLight(L.terrain[2], 0.9f, 0.0f, 0.0f, 0.0f, -1.0f);
	const float up[3] = { 0.0f, 0.0f, 1.0f };
	float rgb[3];

	HeightMapMesh::vertexLight(L, VERTEX_COLOR_ALL_GLOBAL_LIGHTS, up, rgb);
	CHECK(rgb[0] == 1.0f); // 0.25 + 0.5 + 0.5 = 1.25, clamped
	const float scale[3] = { 0.5f, 0.25f, 0.125f };
	HeightMapMesh::vertexLight(L, VERTEX_COLOR_ALL_GLOBAL_LIGHTS, up, rgb, scale);
	CHECK(rgb[0] == 0.75f);          // 0.25 + 0.5 * 0.5 + 0.5 * 0.5
	CHECK(rgb[1] == 0.5f);           // 0.25 + 0.125 + 0.125
	CHECK(rgb[2] == 0.375f);         // 0.25 + 0.0625 + 0.0625
	// the default sun-per-pixel mode lights only the accent lights
	HeightMapMesh::vertexLight(L, VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS, up, rgb);
	CHECK(rgb[0] == 0.75f);
	HeightMapMesh::vertexLight(L, VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS, up, rgb, scale);
	CHECK(rgb[0] == 0.5f);
	CHECK(rgb[1] == 0.375f);
	CHECK(rgb[2] == 0.3125f);
	// a null scale is the identity
	const float one[3] = { 1.0f, 1.0f, 1.0f };
	float rgb1[3];
	HeightMapMesh::vertexLight(L, VERTEX_COLOR_ALL_GLOBAL_LIGHTS, up, rgb, nullptr);
	HeightMapMesh::vertexLight(L, VERTEX_COLOR_ALL_GLOBAL_LIGHTS, up, rgb1, one);
	for (int c = 0; c < 3; ++c)
	{
		CHECK(rgb[c] == rgb1[c]);
	}
	HeightMapMeshOptions options;
	for (int c = 0; c < 3; ++c)
	{
		CHECK(options.lightScale[c] == 1.0f);
	}
}

TEST_CASE("terrain light scale: the object light sets scale the directional colours, not the ambient (RW 0x46E009)")
{
	GlobalLightingData g = v8Lighting();
	g.terrainLightingMultiplier = 1.0f; // no SAS doubling
	setLight(g.tod[0].objects[0], 0.25f, 0.5f, 0.0f, 0.0f, -1.0f);
	setLight(g.tod[0].objects[1], 0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
	setLight(g.tod[0].objects[2], 0.0f, 0.5f, 0.0f, 1.0f, 0.0f);
	setLight(g.tod[0].infantry[0], 0.5f, 0.5f, 0.0f, 0.0f, -1.0f);
	setLight(g.tod[0].infantry[1], 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	setLight(g.tod[0].infantry[2], 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	const float scale[3] = { 0.5f, 0.25f, 0.125f };

	W3DObjectLighting plain, scaled;
	std::string err;
	REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(g, plain, &err), err);
	REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(g, scaled, &err, scale), err);
	CHECK(plain.objects.color[0][0] == 0.5f);
	CHECK(scaled.objects.color[0][0] == 0.25f);
	CHECK(scaled.objects.color[0][1] == 0.125f);
	CHECK(scaled.objects.color[0][2] == 0.0625f);
	CHECK(scaled.infantry.color[0][2] == 0.0625f);
	for (int c = 0; c < 3; ++c)
	{
		CHECK(scaled.objects.ambient[c] == plain.objects.ambient[c]);
		CHECK(scaled.infantry.ambient[c] == plain.infantry.ambient[c]);
		CHECK(scaled.objects.toLight[0][c] == plain.objects.toLight[0][c]);
	}

	// ZH Add_Light drops a light below 0.05 in every channel; retail sets the scaled diffuse first, so the test sees the scaled colour
	setLight(g.tod[0].objects[1], 0.0f, 0.3f, 1.0f, 0.0f, 0.0f);
	const float dim[3] = { 0.125f, 0.125f, 0.125f };
	REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(g, plain, &err), err);
	REQUIRE_MESSAGE(W3DObjectLightingUtil::fromMap(g, scaled, &err, dim), err);
	CHECK(plain.objects.count == 3);
	CHECK(scaled.objects.count == 2); // 0.3 * 0.125 = 0.0375 < 0.05
}

TEST_CASE("terrain light scale: the stop is reported and registered")
{
	const std::vector<std::string> lines = TerrainLightScale::stopLines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].find("[S-033]") == 0);
	CHECK(lines[0].find("scorch marks") != std::string::npos);
	CHECK(lines[0].find("RW 0x4E44FB") != std::string::npos);
	// the consumers of the object factor: MapObjectBuilder applies it, MapTerrainBuilder only reports it, GameWorld stays at the High tier (review of PR #147)
	CHECK(lines[0].find("MapObjectBuilder (object lights)") != std::string::npos);
	CHECK(lines[0].find("GameWorld lights its objects at the High tier") != std::string::npos);
	CHECK(lines[0].find("object_light_scale_retail without applying it") != std::string::npos);
	const MapStop *s033 = MapStops::find("S-033");
	REQUIRE(s033 != nullptr);
	CHECK(std::string(s033->gap).find("terrain light scale") != std::string::npos);
	CHECK(std::string(s033->gap).find("GameWorld lights its objects at the High tier (scale 1)") != std::string::npos);
}

TEST_CASE("terrain light scale: retail maps store the scale (GlobalLighting v8) and the guards pick the tier")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("terrain light scale retail maps");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	struct Expect
	{
		const char *path;
		int version;
		float scale[3];
	};
	// read independently by tools/render/test_terrain_light_scale_binary_facts.py (the spec author's map parser, not the engine)
	const Expect cases[] = {
		{ "maps\\map wor mirkwood\\map wor mirkwood.map", 8, { 0.5f, 0.5f, 0.45f } },
		{ "maps\\map wor isengard\\map wor isengard.map", 8, { 0.85f, 0.85f, 0.85f } },
		{ "maps\\map wor ang north downs\\map wor ang north downs.map", 8, { 0.58f, 0.68f, 0.83f } },
		{ "maps\\map mp fords of isen ii\\map mp fords of isen ii.map", 8, { 0.85f, 0.8f, 0.8f } },
		{ "maps\\map mp harlindon\\map mp harlindon.map", 8, { 1.0f, 1.0f, 1.0f } },
		{ "bases\\camps\\camps.map", 7, { 1.0f, 1.0f, 1.0f } },
	};
	for (const Expect &e : cases)
	{
		CAPTURE(e.path);
		std::vector<std::uint8_t> bytes;
		std::string err;
		REQUIRE_MESSAGE(mount->fs->readFile(e.path, bytes, &err), err);
		LoadedMap m;
		REQUIRE_MESSAGE(MapReader::load(bytes, e.path, MapReadOptions(), m, &err), err);
		REQUIRE(m.chunks.hasGlobalLighting);
		const GlobalLightingData &g = m.chunks.lighting;
		CHECK(g.version == e.version);
		CHECK(g.hasTerrainLightScale == (e.version == 8));
		for (int c = 0; c < 3; ++c)
		{
			CHECK(g.terrainLightScale[c] == e.scale[c]);
		}

		// the High tier (the renderers' default) leaves the sun alone, the lowest tier scales it
		const GlobalLight &sun = g.tod[g.timeOfDay - 1].terrain[0];
		TerrainLightScale::Scales high, low;
		REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(true, 2, 2), high, &err), err);
		REQUIRE_MESSAGE(TerrainLightScale::resolve(g, lodOf(false, 0, 0), low, &err), err);
		for (int c = 0; c < 3; ++c)
		{
			CHECK(high.terrain[c] * sun.diffuse[c] == sun.diffuse[c]);
			CHECK(low.terrain[c] * sun.diffuse[c] == e.scale[c] * sun.diffuse[c]);
		}
	}
}
