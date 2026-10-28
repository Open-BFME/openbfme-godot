// OpenBFME unit tests: the standing water of the retail maps against the mounted RotWK 2.01 files. GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise). Every expected value was read from the retail files by an
// independent script (tools/render/test_water_binary_facts.py reads the same facts from game.dat and watershader.fxo; the W3D properties and the
// map.ini keys by a W3D chunk walker and a text scan), never from this engine's output:
//   * RotWK's watershader.fxo lists its techniques DefaultTechnique, BumpSpecular, DefaultTechnique_L, BumpSpecular_L, DepthOnly, PCA_U, PCA,
//     PCA_L, PCA_M, BumpSpecular_M (BFME2's file differs; the mount puts RotWK's first);
//   * the water W3Ds: wtr_riv01.w3d BumpSpecular (ReflectionStrength 0.8), wtr_riv01l PCA_L, wtr_riv01m BumpSpecular; wtr_riv02 / wtr_riv04 are
//     BumpSpecular with ReflectionStrength 0; waterplanepca PCA_U (0.2), waterplanepcal PCA_L; wtr_gondor PCA_U with no M variant;
//   * data\ini\water.ini says ReflectionOn = No; 66 of the 123 map.ini files say Yes (Adorn River, Anfalas, Fords of Isen II, Evendim, Harlindon, ...).

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MapWeather.h"
#include "GameClient/WaterGeometry.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

struct WaterFacts
{
	std::string authored;       // the technique name Load_W3D stores
	std::string stem;           // the W3D the WaterLOD reads
	std::string technique;      // what InitializeShader binds
	float reflectionStrength = 0.0f;
};

// The distinct standing water materials of a retail map at a WaterLOD, keyed by the area's shader file.
std::map<std::string, WaterFacts> mapWater(retailtest::Mount &mount, const std::string &map, int lod, const std::vector<std::string> &techniques)
{
	std::map<std::string, WaterFacts> out;
	std::vector<std::uint8_t> bytes;
	std::string error;
	const std::string path = "maps\\" + map + "\\" + map + ".map";
	REQUIRE_MESSAGE(mount.fs->readFile(path, bytes, &error), error);
	LoadedMap loaded;
	MapReadOptions options;
	REQUIRE_MESSAGE(MapReader::load(bytes, path, options, loaded, &error), error);
	ArchiveW3DFileSource source(*mount.fs);
	WW3DAssetManager assets(source);
	for (const StandingWaterArea &area : loaded.chunks.standingWaterAreas)
	{
		if (area.fxShader.empty() || out.count(area.fxShader)) continue;
		std::string stem = area.fxShader;
		if (stem.size() > 4) stem.resize(stem.size() - 4); // ".W3D"
		WaterFacts facts;
		facts.stem = WaterGeometry::standingShaderStem(stem, lod, [&](const std::string &s) { return source.Exists(W3D_Asset_Path(s, source.FlatAssetLayout())); });
		const W3DFileContents *file = assets.Load_File(W3D_Asset_Path(facts.stem, source.FlatAssetLayout()), &error);
		REQUIRE_MESSAGE(file != nullptr, error);
		bool found = false;
		for (const auto &mesh : file->Meshes)
			for (const auto &material : mesh.ShaderMaterials)
			{
				WaterGeometry::StandingMaterial params;
				if (found || !WaterGeometry::readStandingMaterial(material, params, &error)) continue;
				found = true;
				REQUIRE(params.technique < techniques.size()); // tolerance: bounds check of the technique index before it names an entry
				facts.authored = techniques[params.technique];
				facts.reflectionStrength = params.reflectionStrength;
				WaterGeometry::ResolvedTechnique resolved;
				REQUIRE_MESSAGE(WaterGeometry::resolveTechnique(techniques, facts.authored, lod, resolved, &error), error);
				facts.technique = resolved.name;
			}
		REQUIRE_MESSAGE(found, area.fxShader);
		out[area.fxShader] = facts;
	}
	return out;
}

bool reflectionOn(retailtest::Mount &mount, const std::string &map)
{
	bool on = false;
	std::string error;
	REQUIRE_MESSAGE(WaterTransparencyScan::load(*mount.fs, "maps\\" + map, on, &error), error);
	return on;
}

} // namespace

TEST_CASE("water retail: RotWK's watershader.fxo lists its ten techniques in this order")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water retail techniques");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mount->fs->readFile("shaders\\compiled\\watershader.fxo", bytes, &error), error);
	std::vector<std::string> names;
	REQUIRE_MESSAGE(WaterGeometry::readEffectTechniques(bytes, names, &error), error);
	CHECK(names == std::vector<std::string>{ "DefaultTechnique", "BumpSpecular", "DefaultTechnique_L", "BumpSpecular_L", "DepthOnly", "PCA_U", "PCA", "PCA_L", "PCA_M", "BumpSpecular_M" });
}

TEST_CASE("water retail: ReflectionOn is No in water.ini and Yes in the map.ini of Adorn River, Anfalas, Fords of Isen II, Evendim and Harlindon")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water retail reflection");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	for (const char *map : { "map mp adorn river", "map mp anfalas", "map mp fords of isen ii", "map mp evendim", "map mp harlindon" })
	{
		CAPTURE(map);
		CHECK(reflectionOn(*mount, map));
	}
	for (const char *map : { "map mp the heubris", "map mp grey mountains", "map mp paths of the dead" }) // no WaterTransparency block in their map.ini
	{
		CAPTURE(map);
		CHECK_FALSE(reflectionOn(*mount, map));
	}
}

TEST_CASE("water retail: Adorn River, Fords of Isen II, Harlindon, Anfalas and Evendim read their file and technique by WaterLOD")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water retail LOD");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mount->fs->readFile("shaders\\compiled\\watershader.fxo", bytes, &error), error);
	std::vector<std::string> names;
	REQUIRE_MESSAGE(WaterGeometry::readEffectTechniques(bytes, names, &error), error);

	struct Row { const char *map; const char *areaFile; int lod; const char *stem; const char *authored; const char *technique; float strength; };
	const Row rows[] = {
		// Adorn River: wtr_riv01 (BumpSpecular, ReflectionStrength 0.8); its L file is PCA_L, its M file BumpSpecular
		{ "map mp adorn river", "wtr_Riv01.W3D", 0, "wtr_Riv01L", "PCA_L", "PCA_L", 0.5f },
		{ "map mp adorn river", "wtr_Riv01.W3D", 1, "wtr_Riv01M", "BumpSpecular", "BumpSpecular_M", 0.8f },
		{ "map mp adorn river", "wtr_Riv01.W3D", 2, "wtr_Riv01", "BumpSpecular", "BumpSpecular", 0.8f },
		{ "map mp adorn river", "wtr_Riv01.W3D", 3, "wtr_Riv01", "BumpSpecular", "BumpSpecular", 0.8f },
		// Fords of Isen II: wtr_riv01 and wtr_riv02 (ReflectionStrength 0; no M file: Medium reads the base file, whose BumpSpecular resolves to _M)
		{ "map mp fords of isen ii", "wtr_Riv01.W3D", 0, "wtr_Riv01L", "PCA_L", "PCA_L", 0.5f },
		{ "map mp fords of isen ii", "wtr_Riv01.W3D", 1, "wtr_Riv01M", "BumpSpecular", "BumpSpecular_M", 0.8f },
		{ "map mp fords of isen ii", "wtr_Riv01.W3D", 2, "wtr_Riv01", "BumpSpecular", "BumpSpecular", 0.8f },
		{ "map mp fords of isen ii", "wtr_Riv02.W3D", 0, "wtr_Riv02L", "PCA_L", "PCA_L", 0.5f },
		{ "map mp fords of isen ii", "wtr_Riv02.W3D", 1, "wtr_Riv02", "BumpSpecular", "BumpSpecular_M", 0.0f },
		{ "map mp fords of isen ii", "wtr_Riv02.W3D", 2, "wtr_Riv02", "BumpSpecular", "BumpSpecular", 0.0f },
		{ "map mp fords of isen ii", "wtr_Riv02.W3D", 3, "wtr_Riv02", "BumpSpecular", "BumpSpecular", 0.0f },
		// Harlindon: waterplanepca authors PCA_U; High runs PCA, UltraHigh PCA_U, Medium PCA_M (no M file), Low reads waterplanepcal (PCA_L)
		{ "map mp harlindon", "WaterPlanePCA.W3D", 0, "WaterPlanePCAL", "PCA_L", "PCA_L", 0.5f },
		{ "map mp harlindon", "WaterPlanePCA.W3D", 1, "WaterPlanePCA", "PCA_U", "PCA_M", 0.2f },
		{ "map mp harlindon", "WaterPlanePCA.W3D", 2, "WaterPlanePCA", "PCA_U", "PCA", 0.2f },
		{ "map mp harlindon", "WaterPlanePCA.W3D", 3, "WaterPlanePCA", "PCA_U", "PCA_U", 0.2f },
		// Anfalas: wtr_arnor (BumpSpecular, 0.7)
		{ "map mp anfalas", "Wtr_Arnor.W3D", 1, "Wtr_ArnorM", "BumpSpecular", "BumpSpecular_M", 0.7f },
		{ "map mp anfalas", "Wtr_Arnor.W3D", 2, "Wtr_Arnor", "BumpSpecular", "BumpSpecular", 0.7f },
		// Evendim: wtr_gondor (PCA_U, no M file)
		{ "map mp evendim", "Wtr_Gondor.W3D", 0, "Wtr_GondorL", "PCA_L", "PCA_L", 0.5f },
		{ "map mp evendim", "Wtr_Gondor.W3D", 1, "Wtr_Gondor", "PCA_U", "PCA_M", 0.5f },
		{ "map mp evendim", "Wtr_Gondor.W3D", 2, "Wtr_Gondor", "PCA_U", "PCA", 0.5f },
		{ "map mp evendim", "Wtr_Gondor.W3D", 3, "Wtr_Gondor", "PCA_U", "PCA_U", 0.5f },
	};
	for (const Row &row : rows)
	{
		CAPTURE(row.map);
		CAPTURE(row.areaFile);
		CAPTURE(row.lod);
		const std::map<std::string, WaterFacts> water = mapWater(*mount, row.map, row.lod, names);
		const auto it = water.find(row.areaFile);
		REQUIRE(it != water.end());
		CHECK(it->second.stem == row.stem);
		CHECK(it->second.authored == row.authored);
		CHECK(it->second.technique == row.technique);
		CHECK(it->second.reflectionStrength == row.strength);
	}
}

TEST_CASE("water retail: the techniques the port draws at each WaterLOD of those maps, and the ones it reports")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water retail ported techniques");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mount->fs->readFile("shaders\\compiled\\watershader.fxo", bytes, &error), error);
	std::vector<std::string> names;
	REQUIRE_MESSAGE(WaterGeometry::readEffectTechniques(bytes, names, &error), error);
	// High and UltraHigh water of those maps is entirely a ported technique; Low is PCA_L (not ported: reported); Medium is BumpSpecular_M, or PCA_M (not ported)
	for (const char *map : { "map mp adorn river", "map mp fords of isen ii", "map mp harlindon", "map mp anfalas", "map mp evendim" })
	{
		for (int lod = 0; lod < WaterGeometry::kWaterLodCount; ++lod)
		{
			for (const auto &entry : mapWater(*mount, map, lod, names))
			{
				CAPTURE(map);
				CAPTURE(lod);
				CAPTURE(entry.second.technique);
				const bool ported = WaterGeometry::standingShaderTraits(entry.second.technique) != nullptr;
				const bool expectedPorted = lod >= 2 || entry.second.technique == "BumpSpecular_M";
				CHECK(ported == expectedPorted);
			}
		}
	}
	// the PCA tier: Harlindon at High runs PCA (two basis textures, 0.15 steepness), at UltraHigh PCA_U (three, noise, 0.33)
	const auto *pca = WaterGeometry::standingShaderTraits(mapWater(*mount, "map mp harlindon", 2, names).begin()->second.technique);
	const auto *pcaU = WaterGeometry::standingShaderTraits(mapWater(*mount, "map mp harlindon", 3, names).begin()->second.technique);
	REQUIRE((pca && pcaU));
	CHECK(pca->pcaMode == 1);
	CHECK(pcaU->pcaMode == 2);
}

TEST_CASE("water retail: Adorn River's one standing water area (a 97-point river polygon) is rejected by the ear clipper and so not drawn (S-035)")
{
	// Pinned stop, not an accepted behaviour: retail fills every standing water area from a grid (W3DWater.cpp AllocateAndFillBuffers, used here for PCA water
	// only), so Adorn River's water is missing until the bump water takes that path too. The builder reports it as water.triangulation_failed.
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("water retail adorn river triangulation");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<std::uint8_t> bytes;
	std::string error;
	const std::string path = "maps\\map mp adorn river\\map mp adorn river.map";
	REQUIRE_MESSAGE(mount->fs->readFile(path, bytes, &error), error);
	LoadedMap loaded;
	MapReadOptions options;
	REQUIRE_MESSAGE(MapReader::load(bytes, path, options, loaded, &error), error);
	REQUIRE(loaded.chunks.standingWaterAreas.size() == 1);
	const StandingWaterArea &area = loaded.chunks.standingWaterAreas[0];
	CHECK(area.points.size() == 97);
	CHECK(area.waterHeight == 13);
	std::vector<std::uint32_t> triangles;
	CHECK_FALSE(WaterGeometry::triangulate(area.points, triangles));
}
