// OpenBFME. GPL-3.0.
// Lane WEATHER-1. See GodotDevice/GodotWeather.h.

#include "GodotDevice/GodotWeather.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GodotDevice/GodotFXPlayer.h"
#include "GodotDevice/GodotW3DMaterial.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>
#include <cstring>

namespace godot
{

namespace
{
String toGodotW(const std::string &s) { return String::utf8(s.c_str(), (int64_t)s.size()); }

// RW 0x4936CB in a vertex shader (SnowManager::particleCenter is the C++ reference). CUSTOM0 = (grid x, grid y, corner, 0) in SAGE units of tile 0;
// the instance transform is the tile's translation. The camera's right axis is the view's column 0 (RW 0x4936CB reads the camera transform's column 0).
const char *kSnowShader = R"GLSL(shader_type spatial;
render_mode unshaded, cull_disabled, skip_vertex_transform, fog_disabled, shadows_disabled, blend_mix, depth_draw_never;
uniform sampler2D tex : filter_linear_mipmap, repeat_disable;
uniform sampler2D heights : filter_nearest, repeat_disable;
uniform float traveled;
uniform vec2 drift;
uniform vec2 wind_step;
uniform float box = 1.0;
uniform float ceiling;
uniform float amplitude;
uniform vec2 freq;
uniform float quad_half;
uniform vec4 tint = vec4(1.0);
// WWMath::Fast_Sin: the 1024-entry table sin(2 pi i / 1024), lerped (the argument is already scaled by 1024 / (2 pi))
float fast_sin(float x) {
	float fi = floor(x);
	float fr = x - fi;
	float a = sin(mod(fi, 1024.0) * 0.006135923151542565);
	float b = sin(mod(fi + 1.0, 1024.0) * 0.006135923151542565);
	return (1.0 - fr) * a + b * fr;
}
void vertex() {
	vec2 g = CUSTOM0.xy;
	int corner = int(CUSTOM0.z + 0.5);
	ivec2 n = ivec2((int(g.x) - 32) & 63, (int(g.y) - 32) & 63);
	float h = texelFetch(heights, n, 0).r + traveled;
	float cycles = floor(h / box);
	float z = ceiling - (h - box * cycles);
	float fx = g.x;
	float fy = g.y;
	if (amplitude > 0.0) {
		fx = fast_sin((freq.x * z + fx) * 162.97466) * amplitude + fx;
		fy = fast_sin((freq.y * z + fy) * 162.97466) * amplitude + fy;
	}
	vec3 c = vec3((drift.x - cycles * wind_step.x) + fx, (drift.y - cycles * wind_step.y) + fy, z);
	vec3 cr = INV_VIEW_MATRIX[0].xyz;
	vec3 r = vec3(cr.x, -cr.z, cr.y);
	vec3 u = vec3(0.0, 0.0, 1.0);
	vec3 off = corner == 0 ? (-r + u) : (corner == 1 ? (-r - u) : (corner == 2 ? (r - u) : (r + u)));
	vec3 s = c + off * quad_half;
	vec3 world = vec3(s.x, s.z, -s.y) + MODEL_MATRIX[3].xyz;
	VERTEX = (VIEW_MATRIX * vec4(world, 1.0)).xyz;
}
void fragment() {
	// PRELIT_DIFFUSE: texture * vertex colour, raw gamma-space values as the particles (lane RENDER-3, S-831); the blended particle words' implicit
	// alpha test with reference 1 (S-1440)
	vec4 t = texture(tex, UV) * tint;
	if (t.a < (1.0 / 255.0)) discard;
	ALBEDO = t.rgb;
	ALPHA = t.a;
}
)GLSL";

bool isTerrainShader(const Ref<ShaderMaterial> &m)
{
	if (m.is_null() || m->get_shader().is_null())
	{
		return false;
	}
	const String path = m->get_shader()->get_path();
	return path.ends_with("terrain.gdshader");
}
} // namespace

GodotWeather::GodotWeather() = default;

GodotWeather::~GodotWeather()
{
	shutdown();
}

void GodotWeather::collectTerrainMaterials(Node *node)
{
	if (!node)
	{
		return;
	}
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(node))
	{
		auto take = [&](const Ref<Material> &mat) {
			const Ref<ShaderMaterial> sm = mat;
			if (isTerrainShader(sm))
			{
				for (const TerrainMat &t : m_terrainMats)
				{
					if (t.mat == sm)
					{
						return;
					}
				}
				TerrainMat t;
				t.mat = sm;
				t.sun = sm->get_shader_parameter("sun_color");
				m_terrainMats.push_back(t);
			}
		};
		take(mi->get_material_override());
		const Ref<Mesh> mesh = mi->get_mesh();
		if (mesh.is_valid())
		{
			for (int32_t i = 0; i < mesh->get_surface_count(); ++i)
			{
				take(mi->get_surface_override_material(i));
				take(mesh->surface_get_material(i));
			}
		}
	}
	for (int32_t i = 0; i < node->get_child_count(); ++i)
	{
		collectTerrainMaterials(node->get_child(i));
	}
}

// The terrain materials (terrain.gdshader) under `root`, with the vertex lighting inputs of the map's time of day (HeightMapMesh::vertexLight,
// VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS: terrain[0] ambient + lights 1, 2). A viewer builds the terrain beside the world, so the search may run later
// from the scene's root (advance)
void GodotWeather::findTerrain(Node *root)
{
	collectTerrainMaterials(root);
	if (!m_haveLighting || m_lighting.timeOfDay < 1 || m_lighting.timeOfDay > 4)
	{
		return;
	}
	const TimeOfDayLights &tod = m_lighting.tod[m_lighting.timeOfDay - 1];
	for (TerrainMat &t : m_terrainMats)
	{
		t.mat->set_shader_parameter("weather_terrain_ambient", Vector3(tod.terrain[0].ambient[0], tod.terrain[0].ambient[1], tod.terrain[0].ambient[2]));
		for (int i = 1; i < 3; ++i)
		{
			const GlobalLight &L = tod.terrain[i];
			const String k = i == 1 ? "weather_light1" : "weather_light2";
			t.mat->set_shader_parameter(k + String("_to"), Vector3(-L.lightPos[0], -L.lightPos[1], -L.lightPos[2]));
			t.mat->set_shader_parameter(k + String("_diffuse"), Vector3(L.diffuse[0], L.diffuse[1], L.diffuse[2]));
		}
	}
	for (float &v : m_lastLight)
	{
		v = -1.0f; // the new materials get the current light at the next frame
	}
}

bool GodotWeather::load(Node3D *parent, ArchiveFileSystem &fs, const std::string &mapDir, const WorldHeightMap &hm, const GlobalLightingData *lighting,
	FXPlayer *fx, Node3D *terrainRoot, RandomAlgorithm randomAlgorithm, WeatherSettingStore::SoundValidator validSound, std::string *error)
{
	shutdown();
	m_errors.clear();
	m_fx = fx;
	m_heightMap = &hm;
	m_random = std::make_unique<W3DClientRandom>(randomAlgorithm);
	// TerrainLogic slot 0x20 (TerrainLogic::getExtent): lo (0, 0), hi = the active boundary 0 of the height map times MAP_XY_FACTOR (the client's copy)
	if (hm.m_boundaries.empty())
	{
		if (error)
		{
			*error = "the map's height map has no boundary (the TerrainLogic extent of LightningFX)";
		}
		return false;
	}
	const float extentHiX = (float)hm.m_boundaries[0].x * MAP_XY_FACTOR;
	const float extentHiY = (float)hm.m_boundaries[0].y * MAP_XY_FACTOR;
	if (!m_client.load(fs, mapDir, std::move(validSound), hm.m_width, hm.m_height, hm.m_borderSize, extentHiX, extentHiY, error))
	{
		return false;
	}
	WeatherClient::Audio audio;
	audio.start = [](const std::string &n) { return (std::uint32_t)AudioApi::playUiSound(n); };
	audio.stop = [](std::uint32_t h) { AudioApi::stopSound((AudioHandle)h); };
	m_client.setAudio(audio);
	m_client.setFXExists([this](const std::string &n) { return m_fx && m_fx->playback() && m_fx->playback()->fxLists().findFXList(n) != nullptr; });

	m_haveLighting = lighting != nullptr;
	if (lighting)
	{
		m_lighting = *lighting;
		std::string lightError;
		m_objectLightingOk = W3DObjectLightingUtil::fromMap(m_lighting, m_objectLighting, &lightError);
		if (!m_objectLightingOk)
		{
			m_errors.push_back(lightError);
		}
	}
	m_terrainMats.clear();
	m_terrainSearchDelay = 0.0;
	findTerrain(terrainRoot);

	m_node = memnew(Node3D);
	m_node->set_name("Weather");
	parent->add_child(m_node);
	m_nodeId = m_node->get_instance_id();
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(kSnowShader);
	m_snowMat.instantiate();
	m_snowMat->set_shader(shader);
	// the 64 x 64 starting heights (updateIniSettings ran at init; a map override is in the final setting the store gave init)
	PackedByteArray hb;
	hb.resize(64 * 64 * 4);
	std::memcpy(hb.ptrw(), m_client.snow().startingHeights().data(), 64 * 64 * 4);
	Ref<Image> img = Image::create_from_data(64, 64, false, Image::FORMAT_RF, hb);
	m_heightsTex = ImageTexture::create_from_image(img);
	m_snowMat->set_shader_parameter("heights", m_heightsTex);
	m_texture = m_client.store().finalSetting().snowTexture;
	if (m_fx)
	{
		m_snowMat->set_shader_parameter("tex", m_fx->particleTexture(m_texture));
	}
	else
	{
		m_errors.push_back("no effect player: the snow texture " + m_texture + " cannot be loaded");
	}
	m_multi.instantiate();
	m_multi->set_transform_format(MultiMesh::TRANSFORM_3D);
	m_snowNode = memnew(MultiMeshInstance3D);
	m_snowNode->set_name("Snow");
	m_snowNode->set_multimesh(m_multi);
	m_snowNode->set_material_override(m_snowMat);
	m_snowNode->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
	m_snowNode->set_visible(false);
	m_node->add_child(m_snowNode);
	m_builtSpacing = m_builtCount = m_builtColumns = m_builtTiles = m_builtStepX = m_builtStepY = -1;
	m_lightApplied = false;
	for (float &v : m_lastLight)
	{
		v = -1.0f;
	}
	return true;
}

void GodotWeather::buildSnowMesh(const SnowDrawParams &p)
{
	const int n = std::max(0, p.count);
	PackedVector3Array vtx;
	PackedVector2Array uv;
	PackedFloat32Array custom;
	PackedInt32Array idx;
	vtx.resize((int64_t)n * 4);
	uv.resize((int64_t)n * 4);
	custom.resize((int64_t)n * 16);
	idx.resize((int64_t)n * 6);
	Vector3 *pv = vtx.ptrw();
	Vector2 *pu = uv.ptrw();
	float *pc = custom.ptrw();
	int32_t *pi = idx.ptrw();
	const Vector2 quadUv[4] = { Vector2(0, 0), Vector2(0, 1), Vector2(1, 1), Vector2(1, 0) };
	for (int k = 0; k < n; ++k)
	{
		// RW 0x4936CB: the first `count` points of the tile in row order (y outer, x inner, step `spacing`, `columns` per row)
		const int gx = p.columns > 0 ? k % p.columns : 0;
		const int gy = p.columns > 0 ? k / p.columns : 0;
		const float x = (float)(p.originX + gx * p.spacing);
		const float y = (float)(p.originY + gy * p.spacing);
		for (int c = 0; c < 4; ++c)
		{
			const int64_t v = (int64_t)k * 4 + c;
			pv[v] = Vector3(x, 0.0f, -y); // the vertex shader replaces it; the position only bounds nothing (custom AABB)
			pu[v] = quadUv[c];
			pc[v * 4 + 0] = x;
			pc[v * 4 + 1] = y;
			pc[v * 4 + 2] = (float)c;
			pc[v * 4 + 3] = 0.0f;
		}
		const int32_t b = k * 4;
		const int32_t q[6] = { b + 3, b + 0, b + 2, b + 2, b + 0, b + 1 }; // RW 0x9390F's index pattern
		for (int i = 0; i < 6; ++i)
		{
			pi[(int64_t)k * 6 + i] = q[i];
		}
	}
	m_mesh.instantiate();
	if (n > 0)
	{
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vtx;
		arrays[Mesh::ARRAY_TEX_UV] = uv;
		arrays[Mesh::ARRAY_CUSTOM0] = custom;
		arrays[Mesh::ARRAY_INDEX] = idx;
		m_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT);
		m_mesh->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
	}
	m_multi->set_instance_count(0);
	m_multi->set_mesh(m_mesh);
	m_multi->set_instance_count(p.tiles * p.tiles);
	for (int i = 0; i < p.tiles; ++i)
	{
		for (int j = 0; j < p.tiles; ++j)
		{
			// the tile's translation (j * stepX, i * stepY) in SAGE, Godot (x, z, -y)
			m_multi->set_instance_transform(i * p.tiles + j, Transform3D(Basis(), Vector3((float)(j * p.stepX), 0.0f, -(float)(i * p.stepY))));
		}
	}
	m_builtSpacing = p.spacing;
	m_builtCount = p.count;
	m_builtColumns = p.columns;
	m_builtTiles = p.tiles;
	m_builtStepX = p.stepX;
	m_builtStepY = p.stepY;
}

void GodotWeather::applySnow(const SnowDrawParams &p)
{
	const bool draw = p.draw && p.count > 0;
	if (m_snowNode)
	{
		m_snowNode->set_visible(draw);
	}
	if (!draw)
	{
		return;
	}
	if (p.spacing != m_builtSpacing || p.count != m_builtCount || p.columns != m_builtColumns || p.tiles != m_builtTiles || p.stepX != m_builtStepX ||
		p.stepY != m_builtStepY)
	{
		buildSnowMesh(p);
	}
	if (p.texture != m_texture && m_fx)
	{
		m_texture = p.texture;
		m_snowMat->set_shader_parameter("tex", m_fx->particleTexture(m_texture));
	}
	m_snowMat->set_shader_parameter("traveled", p.heightTraveled);
	m_snowMat->set_shader_parameter("drift", Vector2(p.driftX, p.driftY));
	m_snowMat->set_shader_parameter("wind_step", Vector2(p.windStepX, p.windStepY));
	m_snowMat->set_shader_parameter("box", p.box);
	m_snowMat->set_shader_parameter("ceiling", p.ceiling);
	m_snowMat->set_shader_parameter("amplitude", p.amplitude);
	m_snowMat->set_shader_parameter("freq", Vector2(p.frequencyX, p.frequencyY));
	m_snowMat->set_shader_parameter("quad_half", p.quadHalf);
	const float gray = (float)((p.color >> 16) & 0xFFu) / 255.0f;
	m_snowMat->set_shader_parameter("tint", Color(gray, gray, gray, 1.0f));
	m_lastCount = p.count * p.tiles * p.tiles;
	++m_snowFrames;
}

void GodotWeather::applyLight(const WeatherClientFrame &f)
{
	const CloudEffectFrame &c = f.cloud;
	const float d[3] = { c.active ? c.darkening[0] : 0.0f, c.active ? c.darkening[1] : 0.0f, c.active ? c.darkening[2] : 0.0f };
	const float li = c.active ? c.lightningIntensity : 0.0f;
	const float key[7] = { d[0], d[1], d[2], li, f.terrainDarkened ? f.terrainDarkening[0] : 0.0f, f.terrainDarkened ? f.terrainDarkening[1] : 0.0f,
		f.terrainDarkened ? f.terrainDarkening[2] : 0.0f };
	if (std::equal(key, key + 7, m_lastLight))
	{
		return;
	}
	std::copy(key, key + 7, m_lastLight);
	const float k[3] = { 1.0f - d[0], 1.0f - d[1], 1.0f - d[2] };
	// the terrain: LightningColor (SAS RW 0x4E26D7), the sun = scene light 0 with the lightning colour, times 1 - darkening (RW 0x470F44), the vertex
	// lighting of the last relight (RW 0x468CC0)
	for (TerrainMat &t : m_terrainMats)
	{
		t.mat->set_shader_parameter("lightning_color", Vector3(li, li, li));
		t.mat->set_shader_parameter("sun_color", Vector3((t.sun.x + c.lightColor[0]) * k[0], (t.sun.y + c.lightColor[1]) * k[1], (t.sun.z + c.lightColor[2]) * k[2]));
		t.mat->set_shader_parameter("weather_relit", f.terrainDarkened);
		t.mat->set_shader_parameter("weather_terrain_dark", Vector3(key[4], key[5], key[6]));
	}
	if (f.terrainDarkened)
	{
		++m_relights;
	}
	// the models: every light environment's colours times 1 - darkening, the lightning colour on light 0 (RW 0x46DEA9: diffuse + c, ambient + c / 2)
	if (m_objectLightingOk)
	{
		W3DObjectLighting env = m_objectLighting;
		for (W3DLightSet *set : { &env.objects, &env.infantry })
		{
			for (int ch = 0; ch < 3; ++ch)
			{
				set->ambient[ch] = (set->ambient[ch] + c.lightColor[ch] * 0.5f * env.colorScale) * k[ch];
				for (int l = 0; l < 3; ++l)
				{
					const float add = l == 0 ? c.lightColor[ch] * env.colorScale : 0.0f;
					set->color[l][ch] = (set->color[l][ch] + add) * k[ch];
				}
			}
		}
		W3D_Apply_Object_Lighting(env);
		m_lightApplied = std::any_of(key, key + 4, [](float v) { return v != 0.0f; });
	}
}

void GodotWeather::advance(double seconds, int globalWeather)
{
	if (!m_client.loaded())
	{
		return;
	}
	if (m_terrainMats.empty() && m_node && m_node->is_inside_tree())
	{
		// no terrain under the world: look in the scene once a second (a viewer adds its own terrain next to the world)
		m_terrainSearchDelay -= seconds > 0.0 ? seconds : 1.0 / 60.0;
		if (m_terrainSearchDelay <= 0.0)
		{
			m_terrainSearchDelay = 1.0;
			findTerrain(m_node->get_tree()->get_root());
		}
	}
	const WeatherClientFrame f = m_client.advance(seconds, globalWeather, *m_random);
	applySnow(f.snow);
	applyLight(f);
	m_lastWeather = globalWeather; // lane WEATHERFX-1
	m_lastSnowDraw = f.snow.draw;
	m_lastCloudFrame = f.cloud;
	for (const CloudEffectFrame &e : f.fx)
	{
		++m_flashes;
		m_lastFlashPos = e.fxPosition;
		if (m_fx && m_fx->playback() && m_fx->playback()->playFXList(e.fxName, e.fxPosition, false)) // RW 0x494615: FXList::doFXPos
		{
			++m_fxPlayed;
		}
	}
}

void GodotWeather::shutdown()
{
	if (m_client.loaded())
	{
		m_client.shutdown();
		if (m_objectLightingOk && m_lightApplied)
		{
			W3D_Apply_Object_Lighting(m_objectLighting);
		}
	}
	m_terrainMats.clear();
	if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(m_nodeId)))
	{
		if (n->is_inside_tree())
		{
			n->queue_free(); // the "Weather" node goes with the weather (a clear_scene may have freed it already; outside the tree its parent frees it)
		}
	}
	m_nodeId = ObjectID();
	m_snowNode = nullptr;
	m_node = nullptr;
}

Dictionary GodotWeather::report() const
{
	Dictionary d;
	d["loaded"] = m_client.loaded();
	Array files;
	for (const std::string &f : m_client.store().files())
	{
		files.push_back(toGodotW(f));
	}
	d["files"] = files;
	if (m_client.store().hasWeatherSetting())
	{
		const WeatherSetting &s = m_client.store().finalSetting();
		Dictionary snow;
		snow["enabled"] = s.snowEnabled;
		snow["is_snowing"] = s.isSnowing;
		snow["texture"] = toGodotW(s.snowTexture);
		snow["box_height"] = s.snowBoxHeight;
		snow["spacing"] = m_client.snow().spacing();
		snow["speed"] = m_client.snow().velocity();
		snow["phase"] = m_client.snow().phase();
		snow["snowing"] = m_client.snow().snowing();
		snow["particles"] = (int64_t)m_lastCount;
		snow["frames_drawn"] = (int64_t)m_snowFrames;
		snow["overrides"] = (int64_t)m_client.store().overrideCount();
		snow["lightning_enabled"] = s.lightningEnabled;
		d["snow"] = snow;
	}
	Dictionary cloud;
	cloud["phase"] = m_client.cloud().phase();
	cloud["counter"] = m_client.cloud().counter();
	cloud["followed_weather"] = m_client.cloud().followedWeather();
	cloud["lightning_on"] = m_client.cloud().lightningOn();
	cloud["lightning_fx_played"] = (int64_t)m_fxPlayed;
	cloud["terrain_relights"] = (int64_t)m_relights;
	d["cloud"] = cloud;
	Dictionary sound;
	sound["weather"] = m_client.soundWeather();
	sound["changes"] = (int64_t)m_client.soundChanges();
	sound["handle"] = (int64_t)m_client.soundHandle();
	d["sound"] = sound;
	d["client_frame"] = (int64_t)m_client.clientFrame();
	d["terrain_materials"] = (int64_t)m_terrainMats.size();
	Array errors;
	for (const std::string &e : m_errors)
	{
		errors.push_back(toGodotW(e));
	}
	d["errors"] = errors;
	Array stops;
	for (const std::string &l : WeatherClient::stopLines())
	{
		stops.push_back(toGodotW(l));
	}
	d["stops"] = stops;
	return d;
}

double GodotWeather::groundHeight(double x, double y) const
{
	if (!m_heightMap || m_heightMap->getXExtent() < 2 || m_heightMap->getYExtent() < 2)
	{
		return 0.0;
	}
	const WorldHeightMap &hm = *m_heightMap;
	// sample (i, j) is at ((i - border) * MAP_XY_FACTOR, (j - border) * MAP_XY_FACTOR), its height h * MAP_HEIGHT_SCALE (WorldHeightMap.h)
	const double gx = std::clamp(x / MAP_XY_FACTOR + hm.getBorderSize(), 0.0, (double)(hm.getXExtent() - 1));
	const double gy = std::clamp(y / MAP_XY_FACTOR + hm.getBorderSize(), 0.0, (double)(hm.getYExtent() - 1));
	const int i = std::min((int)gx, hm.getXExtent() - 2);
	const int j = std::min((int)gy, hm.getYExtent() - 2);
	const double fx = gx - i, fy = gy - j;
	const double h0 = hm.getHeight(i, j) * (1.0 - fx) + hm.getHeight(i + 1, j) * fx;
	const double h1 = hm.getHeight(i, j + 1) * (1.0 - fx) + hm.getHeight(i + 1, j + 1) * fx;
	return (h0 * (1.0 - fy) + h1 * fy) * MAP_HEIGHT_SCALE;
}

// lane WEATHERFX-1: presentation inputs of the Enhanced weather (see the header); positions in Godot's frame (x, z, -y)
Dictionary GodotWeather::fxState() const
{
	Dictionary d;
	d["loaded"] = m_client.loaded();
	if (!m_client.loaded())
	{
		return d;
	}
	d["weather"] = m_lastWeather;
	const bool haveSetting = m_client.store().hasWeatherSetting();
	d["map_snowing"] = haveSetting && m_client.store().finalSetting().isSnowing;
	d["snow_draw"] = m_lastSnowDraw;
	d["snow_particles"] = (int64_t)m_lastCount;
	d["snow_spacing"] = m_client.snow().spacing();
	d["snow_speed"] = m_client.snow().velocity();
	d["snow_phase"] = m_client.snow().phase();
	d["wind"] = haveSetting ? Vector2(m_client.store().finalSetting().snowXYSpeed.x, m_client.store().finalSetting().snowXYSpeed.y) : Vector2();
	d["cloud_active"] = m_lastCloudFrame.active;
	d["darkening"] = Color(m_lastCloudFrame.darkening[0], m_lastCloudFrame.darkening[1], m_lastCloudFrame.darkening[2]);
	d["lightning"] = m_lastCloudFrame.lightningIntensity;
	d["lightning_on"] = m_client.cloud().lightningOn();
	d["has_lightning"] = m_client.store().weatherData(m_lastWeather).hasLightning; // WeatherData HasLightning (RAINY / CLOUDYRAINY in Weather.ini)
	d["flashes"] = (int64_t)m_flashes;
	d["flash_position"] = Vector3(m_lastFlashPos.x, m_lastFlashPos.z, -m_lastFlashPos.y);
	return d;
}

} // namespace godot
