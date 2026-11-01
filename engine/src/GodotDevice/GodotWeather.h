// OpenBFME. GPL-3.0.
//
// Lane WEATHER-1: the weather of a live game in the Godot scene. GameClient/WeatherClient decides (the RotWK snow manager, cloud effect and weather
// sound, at 30 client frames per second); this draws it at the render frame rate:
//   * the falling snow / rain of TheSnowManager's quad path (RW 0x4936CB): ONE MultiMesh, an instance per tile, its quads placed by a vertex shader
//     that evaluates RW 0x4936CB's per-particle maths from the grid point, the 64 x 64 starting heights (a float texture) and the frame's travel,
//     drift, wind, amplitude and quad size (SnowManager::particleCenter is the C++ reference); the texture through the particle texture cache; the
//     blend of the shader word 0x198B3 (SRC_ALPHA / INV_SRC_ALPHA, LEQUAL without depth write, no fog), raw gamma-space values like the particles;
//   * TheCloudEffect's darkening and lightning: the terrain shader's LightningColor and sun (light 0 + the lightning colour, times 1 - darkening),
//     the terrain vertex lighting relit with the darkening of the last relight frame (RW 0x468CC0 every 10th client frame), the object light
//     environments times 1 - darkening with the lightning colour on light 0 (diffuse, half on the ambient: RW 0x46DEA9);
//   * LightningFX through the effect player (FXList::doFXPos at the extent's centre) and the weather sound through the installed audio manager.
// Never touches the simulation: it reads the presented snapshot's weather only.

#pragma once

#include "Common/RandomValue.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameClient/WeatherClient.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class W3DClientRandom;

namespace godot
{
class FXPlayer;

class GodotWeather
{
public:
	GodotWeather();
	~GodotWeather();

	// The map's weather (WeatherClient::load) under `parent` (the snow node "Weather"); `terrainRoot` holds the terrain materials (terrain.gdshader).
	// `lighting` the map's GlobalLighting (the terrain vertex lights and the object light environment of its time of day); `extent` TerrainLogic's.
	// `randomAlgorithm` the client generator (W3DClientRandom: the caller names it, S-093). false + *error (the weather stays off).
	bool load(Node3D *parent, ArchiveFileSystem &fs, const std::string &mapDir, const WorldHeightMap &heightMap, const GlobalLightingData *lighting, FXPlayer *fx,
		Node3D *terrainRoot, RandomAlgorithm randomAlgorithm, WeatherSettingStore::SoundValidator validSound,
		std::string *error);
	// One render frame: `seconds` of game time (0 while paused), the logic's weather of the presented frame
	void advance(double seconds, int globalWeather);
	// the end of the game (the sound, the snow manager's reset); the node is freed with its parent
	void shutdown();

	bool loaded() const { return m_client.loaded(); }
	const WeatherClient &client() const { return m_client; }
	// { loaded, map_weather (the store's files), snow {...}, cloud {...}, sound {...}, terrain_materials, stops }
	Dictionary report() const;
	// lane WEATHERFX-1: the little the Enhanced look's weather (godot/scripts/weather_fx.gd) reads every render frame, all from the last advance (no
	// simulation state): { loaded, weather, map_snowing, snow_draw, snow_particles, snow_spacing, snow_speed, snow_phase, wind, cloud_active,
	// darkening, lightning, lightning_on, has_lightning, flashes, flash_position }
	Dictionary fxState() const;
	// lane WEATHERFX-1: the height map's ground at SAGE (x, y), bilinear between the samples (the map as loaded: no craters; the Enhanced weather only
	// places its effects with it). 0 before load
	double groundHeight(double x, double y) const;

private:
	void buildSnowMesh(const SnowDrawParams &p);
	void applySnow(const SnowDrawParams &p);
	void applyLight(const WeatherClientFrame &f);
	void collectTerrainMaterials(Node *node);
	void findTerrain(Node *root);
	double m_terrainSearchDelay = 0.0;

	WeatherClient m_client;
	std::unique_ptr<W3DClientRandom> m_random;
	FXPlayer *m_fx = nullptr;
	Node3D *m_node = nullptr;
	ObjectID m_nodeId;
	MultiMeshInstance3D *m_snowNode = nullptr;
	Ref<MultiMesh> m_multi;
	Ref<ArrayMesh> m_mesh;
	Ref<ShaderMaterial> m_snowMat;
	Ref<ImageTexture> m_heightsTex;
	std::string m_texture;
	int m_builtSpacing = -1, m_builtCount = -1, m_builtColumns = -1, m_builtTiles = -1, m_builtStepX = -1, m_builtStepY = -1;
	struct TerrainMat
	{
		Ref<ShaderMaterial> mat;
		Vector3 sun;
	};
	std::vector<TerrainMat> m_terrainMats;
	bool m_haveLighting = false;
	GlobalLightingData m_lighting;
	W3DObjectLighting m_objectLighting;
	bool m_objectLightingOk = false;
	bool m_lightApplied = false; ///< the last applied frame had an effect (the base lights must be restored once it ends)
	float m_lastLight[7] = { -1, -1, -1, -1, -1, -1, -1 };
	unsigned long long m_snowFrames = 0, m_fxPlayed = 0, m_relights = 0;
	int m_lastCount = 0;
	// lane WEATHERFX-1: the last advance, for fxState
	int m_lastWeather = 0;
	const WorldHeightMap *m_heightMap = nullptr; ///< the game's LoadedMap height map (the weather is destroyed before the game)
	bool m_lastSnowDraw = false;
	CloudEffectFrame m_lastCloudFrame;
	unsigned long long m_flashes = 0; ///< the client frames that started LightningFX (played or not)
	Coord3D m_lastFlashPos;
	std::vector<std::string> m_errors;
};

} // namespace godot
