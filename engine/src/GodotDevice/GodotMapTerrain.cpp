// OpenBFME. GPL-3.0.
// See GodotDevice/GodotMapTerrain.h.

#include "Common/BfmeGame.h"
#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotMapTerrain.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapStops.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MapWeather.h"
#include "GameClient/TerrainAtlas.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/TerrainTypes.h"
#include "GameClient/UserDataFolder.h"
#include "GameClient/WaterGeometry.h"
#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"
#include "GameEngineDevice/W3DDevice/GameClient/TerrainLightScale.h"
#include "GameEngineDevice/W3DDevice/GameClient/TerrainComposite.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/ObjectNameIndex.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DLookupTablePostEffect.h"
#include "GodotDevice/GodotPostEffects.h"
#include "GodotDevice/GodotW3DMaterial.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>

namespace godot
{

namespace
{

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

std::string lowerStr(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

// lane RENDER-4 (S-1650): the lookup texture of a LookupTablePostEffect as the 32^3 volume. The name (the chunk's LookupTexture, e.g.
// VRivendell_vol.tga) is looked up in the compiled texture folder under its own extension (the retail files are TGAs there), then as the
// compiled DDS, then in art\textures (the order of the other texture loads here); a missing or malformed file is the error `why`.
bool loadLookupVolume(RetailFileSystem &fs, const std::string &name, std::vector<std::uint8_t> &volume, std::string &why)
{
	if (name.empty())
	{
		why = "no lookup texture named";
		return false;
	}
	const std::string dds = W3D_Compiled_Texture_Path(name);
	const std::string lower = lowerStr(name);
	const size_t dot = lower.rfind('.');
	const std::string ext = dot == std::string::npos ? std::string() : lower.substr(dot);
	std::vector<std::string> candidates;
	if (!ext.empty())
	{
		candidates.push_back(dds.substr(0, dds.size() - 4) + ext);
	}
	candidates.push_back(dds);
	candidates.push_back("art\\textures\\" + lower);
	for (const std::string &path : candidates)
	{
		if (!fs.existsNative(path))
		{
			continue;
		}
		std::vector<uint8_t> bytes;
		std::string error;
		if (!fs.readBytes(path, bytes, &error))
		{
			why = error;
			return false;
		}
		PackedByteArray buffer;
		buffer.resize((int64_t)bytes.size());
		memcpy(buffer.ptrw(), bytes.data(), bytes.size());
		Ref<Image> image;
		image.instantiate();
		const std::string pext = path.substr(path.size() - 4);
		const Error err = pext == ".dds" ? image->load_dds_from_buffer(buffer) : image->load_tga_from_buffer(buffer);
		if (err != OK || image->is_empty())
		{
			why = "could not decode " + path;
			return false;
		}
		if (image->is_compressed())
		{
			image->decompress();
		}
		image->convert(Image::FORMAT_RGBA8); // top row first (Godot's TGA loader honours the origin bit)
		const PackedByteArray data = image->get_data();
		return W3DLookupTablePostEffect::VolumeFromStrip(data.ptr(), image->get_width(), image->get_height(), volume, &why);
	}
	why = "lookup texture not found: " + name;
	return false;
}

// SAGE world space (x east, y north, z up) -> Godot (x, z, -y) (spec 2.1).
inline Vector3 toG(float x, float y, float z)
{
	return Vector3(x, z, -y);
}

Ref<Shader> loadShader(const char *path, Array &errors)
{
	W3D_Ensure_Light_Globals(); // lane PLAY-1: the terrain shader names the global w3d_fog_shift; it must exist before the shader compiles
	Ref<Shader> sh = ResourceLoader::get_singleton()->load(path);
	if (sh.is_null())
	{
		errors.push_back(String("cannot load shader ") + path);
	}
	return sh;
}

double nowMs()
{
	return (double)Time::get_singleton()->get_ticks_usec() / 1000.0;
}

bool optBool(const Dictionary &o, const char *key, bool def)
{
	return o.has(key) ? (bool)o[key] : def;
}

int optInt(const Dictionary &o, const char *key, int def)
{
	return o.has(key) ? (int)o[key] : def;
}

String optStr(const Dictionary &o, const char *key, const char *def)
{
	return o.has(key) ? (String)o[key] : String(def);
}

// Draw order, as retail: the terrain (base layer + blend layers, composited in ONE opaque pass at priority 0)
// first, then standing water, rivers and roads, which are alpha-blended over it and never write depth. The
// former three-draw terrain gave its blend layers priorities above the water, so they painted over it.
const int TERRAIN_RENDER_PRIORITY = 0;
const int WATER_RENDER_PRIORITY = 1;
const int RIVER_RENDER_PRIORITY = 2;
const int ROAD_RENDER_PRIORITY = 3;

Ref<ImageTexture> makeRecordsTexture(const TerrainComposite::Records &records)
{
	PackedByteArray pb;
	pb.resize((int64_t)(records.texels.size() * sizeof(float)));
	memcpy(pb.ptrw(), records.texels.data(), records.texels.size() * sizeof(float));
	Ref<Image> img = Image::create_from_data(records.width, records.height, false, Image::FORMAT_RGBAF, pb);
	return ImageTexture::create_from_image(img);
}

// One opaque surface: the base layer's vertices; UV2 = cell-local coordinates, CUSTOM0 = base class wrap rect,
// CUSTOM1.x = record index (terrain.gdshader documents the inputs).
Ref<ArrayMesh> makeCompositeMesh(const TerrainComposite::Chunk &c)
{
	const int64_t n = (int64_t)c.vertexCount();
	PackedVector3Array vtx, nrm;
	PackedColorArray col;
	PackedVector2Array uv, uv2;
	PackedFloat32Array custom0, custom1;
	PackedInt32Array idx;
	vtx.resize(n);
	nrm.resize(n);
	col.resize(n);
	uv.resize(n);
	uv2.resize(n);
	custom0.resize(n * 4);
	custom1.resize(n * 4);
	idx.resize((int64_t)c.index.size());
	Vector3 *pv = vtx.ptrw();
	Vector3 *pn = nrm.ptrw();
	Color *pc = col.ptrw();
	Vector2 *pu = uv.ptrw();
	Vector2 *pu2 = uv2.ptrw();
	float *p0 = custom0.ptrw();
	float *p1 = custom1.ptrw();
	for (int64_t i = 0; i < n; ++i)
	{
		const size_t k = (size_t)i;
		pv[i] = toG(c.position[k * 3], c.position[k * 3 + 1], c.position[k * 3 + 2]);
		pn[i] = toG(c.normal[k * 3], c.normal[k * 3 + 1], c.normal[k * 3 + 2]);
		pc[i] = Color(c.color[k * 4] / 255.0f, c.color[k * 4 + 1] / 255.0f, c.color[k * 4 + 2] / 255.0f, c.color[k * 4 + 3] / 255.0f);
		pu[i] = Vector2(c.uv[k * 2], c.uv[k * 2 + 1]);
		pu2[i] = Vector2(c.local[k * 2], c.local[k * 2 + 1]);
		for (int j = 0; j < 4; ++j)
		{
			p0[i * 4 + j] = c.wrap[k * 4 + (size_t)j];
			p1[i * 4 + j] = j == 0 ? c.record[k] : 0.0f;
		}
	}
	memcpy(idx.ptrw(), c.index.data(), c.index.size() * sizeof(int32_t));
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vtx;
	arrays[Mesh::ARRAY_NORMAL] = nrm;
	arrays[Mesh::ARRAY_COLOR] = col;
	arrays[Mesh::ARRAY_TEX_UV] = uv;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2;
	arrays[Mesh::ARRAY_CUSTOM0] = custom0;
	arrays[Mesh::ARRAY_CUSTOM1] = custom1;
	arrays[Mesh::ARRAY_INDEX] = idx;
	const uint64_t format = ((uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT)
		| ((uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT);
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), format);
	return mesh;
}

} // namespace

void MapTerrainBuilder::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("build_map", "fs", "map_name", "options"), &MapTerrainBuilder::build_map, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_report"), &MapTerrainBuilder::get_report);
	ClassDB::bind_method(D_METHOD("build_test_patch", "spec"), &MapTerrainBuilder::build_test_patch);
}

Ref<Texture2D> MapTerrainBuilder::loadTexture(RetailFileSystem &fs, const std::string &name, Array &errors, bool mipmaps)
{
	if (name.empty())
	{
		return Ref<Texture2D>();
	}
	const std::string key = lowerStr(name);
	auto cached = m_textureCache.find(key);
	if (cached != m_textureCache.end())
	{
		return cached->second;
	}
	// lane SDK-1: an asset pack's replacement first (the sky, cloud and macro textures, water, rivers and roads load here; the terrain tile
	// textures go through the atlas instead and are not replaced: GameClient/AssetPacks.h)
	{
		std::string packError;
		Ref<Image> packImage = Load_Asset_Pack_Image(fs.assetPacks().empty() ? nullptr : &fs.assetPacks(), name, packError);
		if (!packError.empty())
		{
			errors.push_back(toGodot("texture " + name + ": " + packError));
			return Ref<Texture2D>();
		}
		if (packImage.is_valid())
		{
			if (mipmaps && !packImage->has_mipmaps() && !packImage->is_compressed())
			{
				packImage->generate_mipmaps();
			}
			Ref<ImageTexture> tex = ImageTexture::create_from_image(packImage);
			tex->set_meta("retail_path", packImage->get_meta("retail_path"));
			m_textureCache[key] = tex;
			return tex;
		}
	}
	// BFME keeps textures compiled under a two-letter folder (W3D_Compiled_Texture_Path): usually DDS, but some
	// are a colour JPG with a separate PNG that carries the alpha (e.g. TRWagonTraveled, TSCracks02). Loose
	// art\textures and the terrain archive are checked next.
	const std::string dds = W3D_Compiled_Texture_Path(name);
	const std::string stem = dds.substr(0, dds.size() - 4);
	auto readImage = [&](const std::string &path, Ref<Image> &image) -> bool {
		std::vector<uint8_t> bytes;
		std::string error;
		if (!fs.readBytes(path, bytes, &error))
		{
			errors.push_back(toGodot(error));
			return false;
		}
		PackedByteArray buffer;
		buffer.resize((int64_t)bytes.size());
		memcpy(buffer.ptrw(), bytes.data(), bytes.size());
		image.instantiate();
		const std::string ext = path.substr(path.size() - 4);
		Error err = ext == ".dds" ? image->load_dds_from_buffer(buffer)
			: ext == ".jpg" ? image->load_jpg_from_buffer(buffer)
			: ext == ".png" ? image->load_png_from_buffer(buffer)
			: image->load_tga_from_buffer(buffer);
		if (err != OK || image->is_empty())
		{
			errors.push_back(toGodot("could not decode texture " + path));
			return false;
		}
		return true;
	};
	Ref<Image> image;
	std::string used;
	if (fs.existsNative(dds))
	{
		if (!readImage(dds, image))
		{
			return Ref<Texture2D>();
		}
		used = dds;
	}
	else if (fs.existsNative(stem + ".tga"))
	{
		// RW 0x530D29: uncompressed compiled TGA precedes packed JPEG.
		// PCA basis textures ship in this form instead of DDS.
		if (!readImage(stem + ".tga", image)) return Ref<Texture2D>();
		used = stem + ".tga";
	}
	else if (fs.existsNative(stem + ".jpg"))
	{
		if (!readImage(stem + ".jpg", image))
		{
			return Ref<Texture2D>();
		}
		used = stem + ".jpg";
		if (fs.existsNative(stem + ".png"))
		{
			Ref<Image> alpha;
			if (readImage(stem + ".png", alpha) && alpha->get_width() == image->get_width() && alpha->get_height() == image->get_height())
			{
				image->convert(Image::FORMAT_RGBA8);
				alpha->convert(Image::FORMAT_RGBA8);
				PackedByteArray c = image->get_data();
				PackedByteArray a = alpha->get_data();
				uint8_t *pc = c.ptrw();
				const uint8_t *pa = a.ptr();
				for (int64_t i = 0; i + 3 < c.size(); i += 4)
				{
					pc[i + 3] = pa[i + 3];
				}
				image = Image::create_from_data(image->get_width(), image->get_height(), false, Image::FORMAT_RGBA8, c);
				used += "+png alpha";
			}
		}
	}
	else
	{
		for (const std::string &path : { "art\\textures\\" + key, "art\\terrain\\" + key })
		{
			if (fs.existsNative(path))
			{
				if (!readImage(path, image))
				{
					return Ref<Texture2D>();
				}
				used = path;
				break;
			}
		}
	}
	if (image.is_valid() && !image->is_empty())
	{
		if (mipmaps && !image->has_mipmaps() && !image->is_compressed())
		{
			image->generate_mipmaps();
		}
		Ref<ImageTexture> tex = ImageTexture::create_from_image(image);
		tex->set_meta("retail_path", toGodot(used));
		m_textureCache[key] = tex;
		return tex;
	}
	errors.push_back(toGodot("texture not found: " + name));
	return Ref<Texture2D>();
}

Node3D *MapTerrainBuilder::build_test_patch(const Dictionary &spec)
{
	auto bad = [&](const char *what) -> Node3D * {
		UtilityFunctions::push_error("MapTerrainBuilder.build_test_patch: ", what);
		return nullptr;
	};
	Ref<Image> atlas = spec.get("base_atlas", Variant());
	PackedVector2Array uv0 = spec.get("uv0", PackedVector2Array());
	if (atlas.is_null() || atlas->is_empty() || uv0.size() != 4)
	{
		return bad("spec needs base_atlas (Image) and uv0 (4 corners SW,SE,NE,NW)");
	}
	auto rectOf = [](const Variant &v, ClassWrapRect &r) {
		if (v.get_type() == Variant::RECT2)
		{
			Rect2 rc = v;
			r.x0 = rc.position.x;
			r.y0 = rc.position.y;
			r.w = rc.size.x;
			r.h = rc.size.y;
		}
	};

	// one cell, 10 x 10 world units, flat, normal up (SAGE space), vertex order SW, SE, NE, NW
	const float pos[4][3] = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 10, 0 }, { 0, 10, 0 } };
	auto addLayer = [&](TerrainLayerMesh &L, const PackedVector2Array &uv, const PackedFloat32Array &alpha, bool flip, const ClassWrapRect &wrap) {
		L.cellId.push_back(0);
		L.cellFlip.push_back(flip ? 1 : 0);
		for (int k = 0; k < 4; ++k)
		{
			L.position.insert(L.position.end(), pos[k], pos[k] + 3);
			L.normal.insert(L.normal.end(), { 0.0f, 0.0f, 1.0f });
			L.color.insert(L.color.end(), { 255, 255, 255, (std::uint8_t)std::lround(std::max(0.0f, std::min(1.0f, alpha[k])) * 255.0f) });
			L.uv.push_back(uv[k].x);
			L.uv.push_back(uv[k].y);
			L.wrap.insert(L.wrap.end(), { wrap.x0, wrap.y0, wrap.w, wrap.h });
		}
		static const std::uint32_t kUnflipped[6] = { 0, 3, 2, 0, 2, 1 };
		static const std::uint32_t kFlipped[6] = { 1, 0, 3, 1, 3, 2 };
		const std::uint32_t *p = flip ? kFlipped : kUnflipped;
		L.index.insert(L.index.end(), p, p + 6);
	};
	TerrainChunk tc;
	ClassWrapRect baseWrap;
	rectOf(spec.get("base_wrap", Variant()), baseWrap);
	PackedFloat32Array opaque;
	opaque.resize(4);
	opaque.fill(1.0f);
	addLayer(tc.layer[0], uv0, opaque, (bool)spec.get("flip", false), baseWrap);
	Array layers = spec.get("layers", Array());
	if (layers.size() > 2)
	{
		return bad("at most two blend layers");
	}
	for (int64_t i = 0; i < layers.size(); ++i)
	{
		Dictionary d = layers[i];
		PackedVector2Array uv = d.get("uv", PackedVector2Array());
		PackedFloat32Array alpha = d.get("alpha", PackedFloat32Array());
		if (uv.size() != 4 || alpha.size() != 4)
		{
			return bad("a layer needs uv and alpha with 4 corners");
		}
		ClassWrapRect wrap;
		rectOf(d.get("wrap", Variant()), wrap);
		addLayer(tc.layer[1 + i], uv, alpha, (bool)d.get("flip", false), wrap);
	}
	std::vector<TerrainChunk> chunks = { tc };
	std::vector<TerrainComposite::Chunk> composite;
	TerrainComposite::Records records;
	TerrainComposite::Stats cstats;
	std::string error;
	if (!TerrainComposite::build(chunks, composite, records, cstats, &error))
	{
		return bad(error.c_str());
	}
	Array shaderErrors;
	Ref<Shader> shader = loadShader("res://shaders/terrain.gdshader", shaderErrors);
	if (shader.is_null())
	{
		return bad("res://shaders/terrain.gdshader cannot be loaded");
	}
	Ref<ImageTexture> baseTex = ImageTexture::create_from_image(atlas);
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(shader);
	mat->set_shader_parameter("base_atlas", baseTex);
	mat->set_shader_parameter("records", makeRecordsTexture(records));
	mat->set_shader_parameter("atlas_size", Vector2((float)atlas->get_width(), (float)atlas->get_height()));
	mat->set_shader_parameter("debug_mode", 7);
	Ref<ArrayMesh> mesh = makeCompositeMesh(composite[0]);
	mesh->surface_set_material(0, mat);
	MeshInstance3D *mi = memnew(MeshInstance3D);
	mi->set_name("TestPatch");
	mi->set_mesh(mesh);
	return mi;
}

Node3D *MapTerrainBuilder::build_map(const Ref<RetailFileSystem> &fsRef, const String &map_name, const Dictionary &options)
{
	m_report = Dictionary();
	Array errors, assumptions, stops;
	Dictionary timings;
	m_report["errors"] = errors;
	m_report["assumptions"] = assumptions;
	m_report["stops"] = stops;
	m_report["timings_ms"] = timings;
	m_report["map"] = map_name;

	auto fail = [&](const std::string &message) -> Node3D * {
		errors.push_back(toGodot(message));
		UtilityFunctions::push_error("MapTerrainBuilder: ", toGodot(message));
		return nullptr;
	};
	if (fsRef.is_null() || !fsRef->is_mounted())
	{
		return fail("retail file system is not mounted");
	}
	RetailFileSystem &rfs = *fsRef.ptr();
	ArchiveFileSystem *arch = rfs.archive();
	if (!arch)
	{
		return fail("no archive file system");
	}

	const bool wantTerrain = optBool(options, "terrain", true);
	const bool wantWater = optBool(options, "water", true);
	const bool wantRivers = optBool(options, "rivers", true);
	const bool wantRoads = optBool(options, "roads", true);
	const bool wantMarkers = optBool(options, "markers", true);
	const bool resolveObjects = optBool(options, "resolve_objects", false);
	const bool cliffAtlas = optStr(options, "cliff_uv", "hypothesis") == String("atlas");
	const bool vertexAll = optStr(options, "vertex_color", "accent") == String("all");

	const std::string name = lowerStr(toNative(map_name));
	// lane MOD-5: a user map is <user maps folder>\<name>\<name>.map (RW 0x70254F), read from disk; any other map is the archives' maps\<name>
	const bool userMap = optBool(options, "user_map", false);
	std::string mapsFolder, error;
	if (!rfs.mapsFolder(userMap, mapsFolder, &error))
	{
		return fail(error);
	}
	const std::string mapDir = MapClassification::mapDirectory(name, mapsFolder);
	const std::string path = MapClassification::mapPath(name, mapsFolder);
	if (userMap)
	{
		stops.push_back(UserDataFolder::kUserMapStop);
	}
	double t0 = nowMs();

	// ---- 1. load the map ------------------------------------------------------------------------
	std::vector<uint8_t> bytes;
	if (!rfs.readBytes(path, bytes, &error))
	{
		return fail(error);
	}
	LoadedMap map;
	{
		MapReadOptions mro; // strict: unknown chunks and unread bytes are errors
		mro.bfme1 = rfs.archive_fs() && isBfme1Data(*rfs.archive_fs()); // community PR #36
		if (!MapReader::load(bytes, path, mro, map, &error))
		{
			return fail(error);
		}
	}
	timings["load_map"] = nowMs() - t0;
	for (const std::string &id : map.stops)
	{
		stops.push_back(toGodot(id));
	}
	// Post effects (PostEffectsChunk, 57 maps). Lane RENDER-4 (S-1650): LookupTablePostEffect is applied (W3DLookupTablePostEffect.h, GodotPostEffects.h);
	// another effect name is reported and not applied (none in the retail corpus). Nothing is dropped silently.
	Ref<LookupTablePostEffect> lookupEffect;
	{
		Array posts;
		for (const PostEffect &e : map.chunks.postEffects)
		{
			Dictionary d;
			d["name"] = toGodot(e.name);
			d["blend_factor"] = e.blendFactor;
			d["lookup_image"] = toGodot(e.lookupImage);
			std::string why;
			if (lowerStr(e.name) != "lookuptableposteffect")
			{
				why = "unknown post effect";
			}
			else if (lookupEffect.is_valid())
			{
				why = "a second LookupTablePostEffect on the map (the first is applied)";
			}
			else
			{
				std::vector<std::uint8_t> volume;
				if (loadLookupVolume(rfs, e.lookupImage, volume, why))
				{
					lookupEffect.instantiate();
					lookupEffect->setLookup(volume, e.blendFactor);
				}
			}
			d["applied"] = why.empty();
			if (!why.empty())
			{
				d["error"] = toGodot(why);
				errors.push_back(toGodot("post effect " + e.name + " (" + e.lookupImage + "): " + why));
				UtilityFunctions::push_warning("MapTerrainBuilder: post effect '", toGodot(e.name), "' (lookup ", toGodot(e.lookupImage), ", blend ", e.blendFactor,
					") is not applied: ", toGodot(why));
			}
			posts.push_back(d);
		}
		m_report["post_effects"] = posts;
		if (lookupEffect.is_valid())
		{
			stops.push_back("S-1650");
		}
	}
	// S-039 (PLAN rule 7): loose map/script files in the install folders, found when the archives were
	// mounted. They are never read (this loader opens archives only); every build reports them.
	{
		Array looseFiles = rfs.get_loose_files();
		m_report["loose_files"] = looseFiles;
		if (!looseFiles.is_empty())
		{
			stops.push_back("S-039");
			for (int64_t i = 0; i < looseFiles.size(); ++i)
			{
				Dictionary d = looseFiles[i];
				UtilityFunctions::push_warning("MapTerrainBuilder: ", (String)d["line"]);
			}
		}
	}
	const WorldHeightMap &hm = map.heightMap;
	if (!map.hasHeightMap || !hm.m_hasBlendTileData)
	{
		return fail("map has no height map / blend tile data");
	}
	TerrainLogic terrain;
	std::vector<std::string> logicProblems;
	terrain.init(hm, map.chunks, &logicProblems);

	Dictionary info;
	info["archive"] = toGodot(arch->getArchiveFilenameForFile(path)); // "" for a user map
	info["stored_bytes"] = (int64_t)map.storedSize;
	info["decoded_bytes"] = (int64_t)map.decodedSize;
	info["width"] = hm.m_width;
	info["height"] = hm.m_height;
	info["border"] = hm.m_borderSize;
	info["boundaries"] = (int64_t)hm.m_boundaries.size();
	info["objects"] = (int64_t)map.chunks.objects.size();
	info["waypoints"] = (int64_t)terrain.waypoints().size();
	info["blend_version"] = hm.m_blendTileVersion;
	info["texture_classes"] = (int64_t)hm.m_textureClasses.size();
	info["time_of_day"] = map.chunks.hasGlobalLighting ? map.chunks.lighting.timeOfDay : 0;
	Array problems;
	for (const std::string &p : logicProblems)
	{
		problems.push_back(toGodot(p));
	}
	info["terrain_logic_problems"] = problems;
	for (const std::string &w : map.warnings)
	{
		problems.push_back(toGodot(w));
	}
	m_report["map_info"] = info;

	// lane RENDER-3 (S-831): the map root installs the gamma-space transparent pass on the camera that shows it (roads, rivers and standing water hand
	// over gamma-space values), with or without a GameWorld or an instancer next to it
	Node3D *root = memnew(GammaCompositeHost);
	root->set_name("Map");
	if (lookupEffect.is_valid())
	{
		// lane RENDER-4 (S-1650): the colour grade follows the map root onto the camera that shows it
		MapPostEffectsHost *post = memnew(MapPostEffectsHost);
		post->set_name("PostEffects");
		post->setEffect(lookupEffect);
		root->add_child(post);
	}
	const GlobalLightingData *lighting = map.chunks.hasGlobalLighting ? &map.chunks.lighting : nullptr;
	const TimeOfDayLights *tod = lighting ? &lighting->tod[std::max(0, std::min(3, lighting->timeOfDay - 1))] : nullptr;
	const float effectColorScale = lighting ? W3DObjectLightingUtil::effectColorScale(*lighting) : 1.0f;
	const bool overbright = effectColorScale > 1.0f;

	// stop S-033: the map's terrain light scale (GlobalLighting v8) applies at the low terrain tiers only (TerrainLightScale.h). The viewer / tests
	// may name the tier ("use_terrain_normal_map", "terrain_lod", "shader_lod"); the default is the High tier this renderer draws (scale 1).
	TerrainLightScale::Lod lod;
	lod.useTerrainNormalMap = optBool(options, "use_terrain_normal_map", lod.useTerrainNormalMap);
	lod.terrainLOD = optInt(options, "terrain_lod", lod.terrainLOD);
	lod.shaderLOD = optInt(options, "shader_lod", lod.shaderLOD);
	TerrainLightScale::Scales lightScales;
	if (lighting)
	{
		std::string scaleError;
		if (!TerrainLightScale::resolve(*lighting, lod, lightScales, &scaleError))
		{
			memdelete(root);
			return fail(scaleError);
		}
		for (const std::string &line : TerrainLightScale::stopLines())
		{
			stops.push_back(toGodot(line));
		}
	}

	if (lighting)
	{
		Dictionary lit;
		lit["time_of_day"] = lighting->timeOfDay;
		lit["terrain_lighting_multiplier"] = lighting->terrainLightingMultiplier;
		lit["flag_dbd"] = lighting->flagDBD;
		lit["shadow_color"] = (int64_t)lighting->shadowColor;
		lit["overbright_assumed"] = overbright;
		lit["effect_color_scale"] = effectColorScale;
		Array terrainLights;
		for (int i = 0; i < 3; ++i)
		{
			Dictionary l;
			l["ambient"] = Vector3(tod->terrain[i].ambient[0], tod->terrain[i].ambient[1], tod->terrain[i].ambient[2]);
			l["diffuse"] = Vector3(tod->terrain[i].diffuse[0], tod->terrain[i].diffuse[1], tod->terrain[i].diffuse[2]);
			l["light_pos"] = Vector3(tod->terrain[i].lightPos[0], tod->terrain[i].lightPos[1], tod->terrain[i].lightPos[2]);
			terrainLights.push_back(l);
		}
		lit["terrain_lights"] = terrainLights;
		lit["terrain_light_scale"] = Vector3(lightScales.raw[0], lightScales.raw[1], lightScales.raw[2]);
		lit["terrain_rendering_mode"] = lightScales.terrainMode;
		lit["terrain_light_scale_applied"] = Vector3(lightScales.terrain[0], lightScales.terrain[1], lightScales.terrain[2]);
		// the factor retail's object lights get at this tier (RW 0x46E009). This builder draws no objects and lights no model: MapObjectBuilder
		// applies it (its report's lighting.object_light_scale_applied); the water here is not a reader of the scale (stop S-033)
		lit["object_light_scale_retail"] = Vector3(lightScales.objects[0], lightScales.objects[1], lightScales.objects[2]);
		m_report["lighting"] = lit;
	}

	// camera helper: the playable area in Godot space
	{
		float maxX = (float)(hm.m_width - 2 * hm.m_borderSize) * MAP_XY_FACTOR;
		float maxY = (float)(hm.m_height - 2 * hm.m_borderSize) * MAP_XY_FACTOR;
		terrain.getExtent(0, maxX, maxY);
		Dictionary cam;
		cam["playable_min"] = toG(0, 0, 0);
		cam["playable_max"] = toG(maxX, maxY, 0);
		cam["center"] = toG(maxX * 0.5f, maxY * 0.5f, terrain.getGroundHeight(maxX * 0.5f, maxY * 0.5f));
		cam["size_x"] = maxX;
		cam["size_y"] = maxY;
		m_report["camera"] = cam;
	}

	// The map's lighting inputs of the terrain effect (terrain.fxo): the sun, the overbright switch, the cloud and macro textures, the map size and the fog.
	// Retail builds one light environment for the terrain and hands it to the roads too (FlatHeightMap.cpp:768-791, :816), so the terrain mesh and the roads
	// take them from here, whether or not the terrain mesh itself is built (lane ROADS-1).
	MapWeather weather;
	{
		std::string werr;
		if (!MapWeatherScan::load(*arch, mapDir, weather, &werr))
		{
			errors.push_back(toGodot("weather: " + werr));
		}
		if (!optBool(options, "fog", true))
		{
			weather.fogEnabled = false; // viewer option: the retail camera never sees the far map, an overview does
		}
	}
	Ref<Texture2D> cloudTex, macroTex;
	if ((wantTerrain || wantRoads) && map.chunks.hasEnvironmentData)
	{
		Array texErrors;
		cloudTex = loadTexture(rfs, map.chunks.environment.cloudTexture, texErrors);
		macroTex = loadTexture(rfs, map.chunks.environment.macroTexture, texErrors);
		for (int64_t i = 0; i < texErrors.size(); ++i)
		{
			errors.push_back(texErrors[i]);
		}
	}
	auto applyMapLighting = [&](const Ref<ShaderMaterial> &m) {
		if (cloudTex.is_valid()) m->set_shader_parameter("cloud_tex", cloudTex);
		if (macroTex.is_valid()) m->set_shader_parameter("macro_tex", macroTex);
		if (tod)
		{
			const GlobalLight &sun = tod->terrain[0];
			// stop S-033: the shader wants the direction TOWARD the light = -lightPos (ZH lightRay)
			m->set_shader_parameter("sun_dir_sage", Vector3(-sun.lightPos[0], -sun.lightPos[1], -sun.lightPos[2]));
			// terrain.fxo MainDirectionalLight binds Sas.DirectionalLight[0]. The
			// SAS callback RW 0x54CFD0 doubles its colour before the shader's overbright half-scale.
			// RW 0x4E2B3A: the sun's diffuse is scaled by GlobalData + 0x944 .. when the terrain rendering mode is below 1, before the SAS doubling
			m->set_shader_parameter("sun_color", Vector3(lightScales.terrain[0] * sun.diffuse[0], lightScales.terrain[1] * sun.diffuse[1],
				lightScales.terrain[2] * sun.diffuse[2]) * effectColorScale);
		}
		m->set_shader_parameter("overbright", overbright);
		m->set_shader_parameter("cloud_scale", Vector2(1.0f / weather.cloudSize[0], 1.0f / weather.cloudSize[1]));
		m->set_shader_parameter("cloud_offset_per_second", Vector2(weather.cloudOffsetPerSecond[0], weather.cloudOffsetPerSecond[1]));
		if (map.chunks.hasEnvironmentData)
		{
			m->set_shader_parameter("macro_stretched", map.chunks.environment.isMacroTextureStretched);
		}
		m->set_shader_parameter("map_size", Vector2((float)hm.m_width * MAP_XY_FACTOR, (float)hm.m_height * MAP_XY_FACTOR));
		m->set_shader_parameter("map_border_width", (float)hm.m_borderSize * MAP_XY_FACTOR);
		m->set_shader_parameter("fog_enabled", weather.fogEnabled ? 1.0f : 0.0f);
		m->set_shader_parameter("fog_color", Vector3(weather.fogColor[0], weather.fogColor[1], weather.fogColor[2]));
		m->set_shader_parameter("fog_range_start", weather.fogStart);
		m->set_shader_parameter("fog_range_end", weather.fogEnd);
	};

	// ---- 2. the terrain: atlas, mesh, materials ----------------------------------------------------
	if (wantTerrain)
	{
		double t1 = nowMs();
		TerrainTypeIndex types;
		if (!TerrainTypes::load(*arch, types, &error))
		{
			memdelete(root);
			return fail(error);
		}
		TerrainAtlasImages images;
		TerrainAtlasReport arep;
		if (!TerrainAtlas::build(const_cast<WorldHeightMap &>(hm), *arch, types, images, arep, &error))
		{
			memdelete(root);
			return fail(error);
		}
		timings["atlas"] = nowMs() - t1;
		auto strArray = [](const std::vector<std::string> &v) {
			Array a;
			for (const std::string &s : v)
			{
				a.push_back(toGodot(s));
			}
			return a;
		};
		Dictionary atlas;
		atlas["width"] = images.width;
		atlas["height"] = images.height;
		atlas["classes"] = arep.classes;
		atlas["classes_placed"] = arep.classesPlaced;
		atlas["tiles_placed"] = arep.tilesPlaced;
		atlas["class_width_mismatches"] = arep.classWidthMismatches;
		atlas["unknown_terrain_type"] = strArray(arep.unknownTerrainType);
		atlas["missing_texture_file"] = strArray(arep.missingTextureFile);
		atlas["bad_texture"] = strArray(arep.badTexture);
		atlas["undersized_texture"] = strArray(arep.undersizedTexture);
		atlas["unplaced_class"] = strArray(arep.unplacedClass);
		atlas["missing_normal_map"] = strArray(arep.missingNormalMap);
		atlas["bad_normal_map"] = strArray(arep.badNormalMap);
		atlas["top_down_textures"] = strArray(arep.topDownTextures);
		atlas["tga_origin_flag_ignored"] = arep.tgaOriginFlagIgnored;
		atlas["grown_beyond_zh_grid"] = arep.grownBeyondZhGrid;
		atlas["source_tiles"] = arep.sourceTiles;
		atlas["flat_normal_texel"] = Color(FLAT_NORMAL_TEXEL_R / 255.0f, FLAT_NORMAL_TEXEL_G / 255.0f, FLAT_NORMAL_TEXEL_B / 255.0f);
		m_report["atlas"] = atlas;
		if (arep.grownBeyondZhGrid)
		{
			assumptions.push_back(toGodot("S-030 the map has " + std::to_string(arep.sourceTiles) + " source tiles; ZH's 28x28 atlas grid holds 784, so the atlas grows downward to "
				+ std::to_string(images.width) + "x" + std::to_string(images.height) + " (retail BFME2 layout UNKNOWN)"));
		}
		for (const std::string &s : arep.unplacedClass)
		{
			errors.push_back(toGodot("terrain class '" + s + "' did not fit in the atlas"));
		}
		assumptions.push_back("atlas is RGBA8 (retail quantises tiles to 16 bit); mips: ZH-style 3 levels via a clamped shader LOD");
		for (const std::string &s : arep.unknownTerrainType)
		{
			errors.push_back(toGodot("terrain class '" + s + "' is not in terrain.ini"));
		}
		for (const std::string &s : arep.missingTextureFile)
		{
			errors.push_back(toGodot("terrain texture missing: " + s));
		}
		for (const std::string &s : arep.badTexture)
		{
			errors.push_back(toGodot("terrain texture undecodable: " + s));
		}
		for (const std::string &s : arep.undersizedTexture)
		{
			errors.push_back(toGodot("terrain texture undersized: " + s));
		}

		double t2 = nowMs();
		HeightMapMeshOptions mo;
		mo.uv.cliffUnit = cliffAtlas ? CLIFF_UV_ATLAS_2048 : CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP;
		mo.colorMode = vertexAll ? VERTEX_COLOR_ALL_GLOBAL_LIGHTS : VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS;
		std::copy(lightScales.terrain, lightScales.terrain + 3, mo.lightScale); // RW 0x4690C1
		std::vector<TerrainChunk> chunks;
		TerrainMeshStats mstats;
		if (!HeightMapMesh::build(hm, lighting, mo, chunks, mstats, &error))
		{
			memdelete(root);
			return fail(error);
		}
		timings["mesh_arrays"] = nowMs() - t2;
		for (const std::string &a : mstats.assumptions)
		{
			assumptions.push_back(toGodot(a));
		}

		double t3 = nowMs();
		// textures and materials
		auto makeTexture = [](const std::vector<std::uint8_t> &rgba, int w, int h) {
			PackedByteArray pb;
			pb.resize((int64_t)rgba.size());
			memcpy(pb.ptrw(), rgba.data(), rgba.size());
			Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, pb);
			img->generate_mipmaps();
			return ImageTexture::create_from_image(img);
		};
		Ref<ImageTexture> baseTex = makeTexture(images.base, images.width, images.height);
		Ref<ImageTexture> normalTex = makeTexture(images.normal, images.width, images.height);
		// the layers are composited per pixel in one opaque pass (TerrainComposite.h): a single shader, a single material
		TerrainComposite::Stats cstats;
		std::vector<TerrainComposite::Chunk> composite;
		TerrainComposite::Records records;
		if (!TerrainComposite::build(chunks, composite, records, cstats, &error))
		{
			memdelete(root);
			return fail(error);
		}
		Ref<ImageTexture> recordsTex = makeRecordsTexture(records);
		Ref<Shader> terrainShader = loadShader("res://shaders/terrain.gdshader", errors);
		if (terrainShader.is_null())
		{
			memdelete(root);
			return fail("terrain shader is missing (res://shaders/terrain.gdshader)");
		}

		// lane RENDER-4 (S-1651): the same hardware fog on the W3D models and the CPU particles (W3DHardwareFog.h)
		W3D_Apply_Fog(weather.fogEnabled, weather.fogColor, weather.fogStart, weather.fogEnd);
		m_report["object_fog"] = toGodot(W3DHardwareFog::StopText());

		auto makeMat = [&](const Ref<Shader> &shader, int priority) {
			Ref<ShaderMaterial> m;
			m.instantiate();
			m->set_shader(shader);
			m->set_render_priority(priority);
			m->set_shader_parameter("base_atlas", baseTex);
			m->set_shader_parameter("normal_atlas", normalTex);
			m->set_shader_parameter("atlas_size", Vector2((float)images.width, (float)images.height));
			applyMapLighting(m);
			m->set_shader_parameter("debug_mode", (int)(options.has("debug_mode") ? (int)options["debug_mode"] : 0));
			return m;
		};
		Ref<ShaderMaterial> terrainMat = makeMat(terrainShader, TERRAIN_RENDER_PRIORITY);
		terrainMat->set_shader_parameter("records", recordsTex);

		Node3D *terrainRoot = memnew(Node3D);
		terrainRoot->set_name("Terrain");
		root->add_child(terrainRoot);
		for (size_t ci = 0; ci < chunks.size(); ++ci)
		{
			const TerrainChunk &ch = chunks[ci];
			Ref<ArrayMesh> mesh = makeCompositeMesh(composite[ci]);
			mesh->surface_set_material(0, terrainMat);
			const Vector3 gmin = toG(ch.boundsMin[0], ch.boundsMax[1], ch.boundsMin[2]);
			const Vector3 gmax = toG(ch.boundsMax[0], ch.boundsMin[1], ch.boundsMax[2]);
			mesh->set_custom_aabb(AABB(gmin, gmax - gmin));
			MeshInstance3D *mi = memnew(MeshInstance3D);
			mi->set_name(String("Chunk_") + String::num_int64(ch.cellX0 / VERTEX_BUFFER_TILE_LENGTH) + "_" + String::num_int64(ch.cellY0 / VERTEX_BUFFER_TILE_LENGTH));
			mi->set_mesh(mesh);
			mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			// lane UI-4: the terrain also sits on visual layer 20, the only layer the selection markers' decals project onto (RotWK's projected shadow decals
			// are drawn on the terrain, not on the units: W3DProjectedShadowManager)
			mi->set_layer_mask(1u | (1u << 19));
			terrainRoot->add_child(mi);
		}
		timings["godot_nodes"] = nowMs() - t3;

		Dictionary ms;
		ms["cells"] = (int64_t)mstats.cells;
		ms["base_cells"] = (int64_t)mstats.baseCells;
		ms["blend_cells"] = (int64_t)mstats.blendCells;
		ms["extra_blend_cells"] = (int64_t)mstats.extraCells;
		ms["flipped_cells"] = (int64_t)mstats.flippedCells;
		ms["extra_flipped_cells"] = (int64_t)mstats.extraFlippedCells;
		ms["cliff_uv_cells"] = (int64_t)mstats.cliffUvCells;
		ms["cliff_forced_flips"] = (int64_t)mstats.cliffForcedFlips;
		ms["missing_texture_cells"] = (int64_t)mstats.missingTextureCells;
		ms["triangles"] = (int64_t)mstats.triangles;
		ms["vertices"] = (int64_t)mstats.vertices;
		ms["chunks"] = (int64_t)mstats.chunks;
		ms["wrap_cells"] = (int64_t)mstats.wrapCells;
		ms["straddling_cells"] = (int64_t)mstats.straddlingCells;
		ms["composite_record_cells"] = (int64_t)cstats.recordCells;
		ms["composite_layer1_cells"] = (int64_t)cstats.layer1Cells;
		ms["composite_layer2_cells"] = (int64_t)cstats.layer2Cells;
		ms["composite_layer2_flip_differs"] = (int64_t)cstats.layer2FlipDiffers;
		ms["draw_passes"] = 1; // one opaque composite pass; water, rivers and roads draw after it
		ms["cliff_uv_unit"] = cliffAtlas ? "atlas_2048 (donor)" : "256px_wrap (hypothesis)";
		ms["vertex_color_mode"] = vertexAll ? "all_global_lights" : "ambient_plus_accent";
		ms["overbright"] = overbright;
		ms["fog_enabled"] = weather.fogEnabled;
		ms["fog_from_map_ini"] = weather.fogKeysSeen;
		ms["cloud_texture"] = map.chunks.hasEnvironmentData ? toGodot(map.chunks.environment.cloudTexture) : String();
		ms["macro_texture"] = map.chunks.hasEnvironmentData ? toGodot(map.chunks.environment.macroTexture) : String();
		ms["cloud_texture_loaded"] = cloudTex.is_valid();
		ms["macro_texture_loaded"] = macroTex.is_valid();
		ms["terrain_render_priority"] = TERRAIN_RENDER_PRIORITY;
		ms["water_render_priority"] = WATER_RENDER_PRIORITY;
		ms["river_render_priority"] = RIVER_RENDER_PRIORITY;
		ms["road_render_priority"] = ROAD_RENDER_PRIORITY;
		m_report["mesh"] = ms;
	}

	// ---- 3. standing water ---------------------------------------------------------------------------
	{
		Dictionary w;
		int64_t areas = 0, tris = 0, failed = 0;
		std::vector<bool> renderedWaves(map.chunks.standingWaveAreas.size(), false);
		Array waterMaterials, materialStops;
		if (wantWater && !map.chunks.standingWaterAreas.empty())
		{
			Ref<Shader> sh = loadShader("res://shaders/water_standing.gdshader", errors);
			Node3D *waterRoot = memnew(Node3D);
			waterRoot->set_name("StandingWater");
			root->add_child(waterRoot);
			ArchiveW3DFileSource waterSource(*rfs.archive_fs());
			WW3DAssetManager waterAssets(waterSource);
			std::vector<std::string> waterTechniques;
			bool techniquesRead = false;
			PackedByteArray heightBytes;
			heightBytes.resize((int64_t)hm.m_width * hm.m_height * sizeof(float));
			for (int y = 0; y < hm.m_height; ++y)
				for (int x = 0; x < hm.m_width; ++x)
				{
					const float z = hm.getHeight(x, y) * MAP_HEIGHT_SCALE;
					std::memcpy(heightBytes.ptrw() + ((int64_t)y * hm.m_width + x) * sizeof(float), &z, sizeof(float));
				}
			Ref<Image> heights = Image::create_from_data(hm.m_width, hm.m_height, false, Image::FORMAT_RF, heightBytes);
			Ref<ImageTexture> heightTexture = ImageTexture::create_from_image(heights);
			W3DObjectLighting waterLights;
			std::string lightError;
			if (lighting && !W3DObjectLightingUtil::fromMap(*lighting, waterLights, &lightError))
				errors.push_back(toGodot(lightError));
			for (const StandingWaterArea &a : map.chunks.standingWaterAreas)
			{
				std::vector<std::uint32_t> tri;
				if (a.points.size() < 3 || !WaterGeometry::triangulate(a.points, tri))
				{
					++failed;
					continue;
				}
				WaterGeometry::StandingMaterial params;
				bool authored = false;
				if (!a.fxShader.empty())
				{
					std::string stem = a.fxShader;
					if (stem.size() > 4 && AsciiStringUtil::compareNoCase(stem.substr(stem.size() - 4), ".w3d") == 0) stem.resize(stem.size() - 4);
					std::string error;
					const W3DFileContents *fx = waterAssets.Load_File(W3D_Asset_Path(stem, waterSource.FlatAssetLayout()), &error);
					if (!fx) errors.push_back(toGodot("water material " + a.fxShader + ": " + error));
					else for (const auto &mesh : fx->Meshes)
						for (const auto &material : mesh.ShaderMaterials)
							if (!authored)
							{
								std::string materialError;
								authored = WaterGeometry::readStandingMaterial(material, params, &materialError);
								if (!materialError.empty()) errors.push_back(toGodot("water material " + a.fxShader + ": " + materialError));
							}
				}
				if (!authored) materialStops.push_back(toGodot("[S-035] " + a.name + ": no WaterShader.FX material; effect defaults with map textures are an inferred legacy-water approximation"));
				std::string technique;
				int pcaMode = 0;
				WaterGeometry::PcaBasis pcaBasis;
				std::string pcaPath;
				if (authored)
				{
					if (!techniquesRead)
					{
						techniquesRead = true;
						std::vector<std::uint8_t> bytes;
						std::string error;
						if (!rfs.readBytes("shaders\\compiled\\watershader.fxo", bytes, &error) || !WaterGeometry::readEffectTechniques(bytes, waterTechniques, &error)) errors.push_back(toGodot("water techniques: " + error));
					}
					if (params.technique >= waterTechniques.size()) errors.push_back(toGodot("water material " + a.fxShader + ": technique index outside compiled effect"));
					else technique = waterTechniques[params.technique];
					if (technique == "PCA" || technique == "PCA_U")
					{
						pcaMode = technique == "PCA_U" ? 2 : 1;
						if (params.pcaTexture[0].size() < 3 || AsciiStringUtil::compareNoCase(params.pcaTexture[0], "None") == 0)
							errors.push_back(toGodot("water material " + a.fxShader + ": PCA technique lacks WaterPCATexture1"));
						else
						{
							pcaPath = "art\\textures\\" + params.pcaTexture[0].substr(0, params.pcaTexture[0].size() - 3) + "basis";
							std::vector<std::uint8_t> bytes;
							std::string error;
							if (!rfs.readBytes(pcaPath, bytes, &error) || !WaterGeometry::readPcaBasis(std::string(bytes.begin(), bytes.end()), pcaBasis, &error)) errors.push_back(toGodot(pcaPath + ": " + error));
						}
						materialStops.push_back(toGodot("[S-035] " + a.name + ": PCA normals and shoreline Gerstner geometry follow authored data; Godot clock, high-detail grid selection, planar reflections and shadow-map filtering remain unverified"));
					}
					else if (technique.compare(0, 3, "PCA") == 0) materialStops.push_back(toGodot("[S-035] " + a.name + ": unsupported PCA LOD technique " + technique + "; using reported bump approximation"));
					else if (!technique.empty() && technique != "BumpSpecular_M") materialStops.push_back(toGodot("[S-035] " + a.name + ": technique " + technique + " uses the BumpSpecular_M approximation; its reflection/pass differences remain unported"));
				}
				Ref<ShaderMaterial> m;
				if (sh.is_valid())
				{
					m.instantiate();
					m->set_shader(sh);
					m->set_render_priority(WATER_RENDER_PRIORITY);
					m->set_shader_parameter("output_gamma", 1.0f);
					Array texErrors;
					const std::string bumpName = params.bumpTexture.empty() ? a.bumpMapTexture : params.bumpTexture;
					const std::string envName = params.envTexture.empty() ? a.skyTexture : params.envTexture;
					if (!bumpName.empty()) { auto tex = loadTexture(rfs, bumpName, texErrors); if (tex.is_valid()) m->set_shader_parameter("bump_tex", tex); }
					if (!envName.empty()) { auto tex = loadTexture(rfs, envName, texErrors); if (tex.is_valid()) m->set_shader_parameter("sky_tex", tex); }
					if (!params.waveTexture.empty()) { auto tex = loadTexture(rfs, params.waveTexture, texErrors); if (tex.is_valid()) m->set_shader_parameter("wave_tex", tex); }
					if (pcaMode && !pcaBasis.frames.empty())
					{
						PackedByteArray coefficients;
						coefficients.resize((int64_t)pcaBasis.frames.size() * 10 * 4 * sizeof(float));
						for (size_t row = 0; row < 10; ++row)
							for (size_t frame = 0; frame < pcaBasis.frames.size(); ++frame)
								std::memcpy(coefficients.ptrw() + (row * pcaBasis.frames.size() + frame) * 4 * sizeof(float), pcaBasis.frames[frame][row].data(), 4 * sizeof(float));
						Ref<Image> image = Image::create_from_data((int)pcaBasis.frames.size(), 10, false, Image::FORMAT_RGBAF, coefficients);
						m->set_shader_parameter("pca_coefficients", ImageTexture::create_from_image(image));
						m->set_shader_parameter("pca_frame_rate", pcaBasis.speed * 30.0f); // BFME2 callbacks: SyncTime/1000 * client FPS, then basis speed
						m->set_shader_parameter("pca_mode", pcaMode);
						m->set_shader_parameter("wave_geometry_enabled", true);
						for (int g = 0; g < 3; ++g)
							m->set_shader_parameter(String("gerstner") + String::num_int64(g), Vector4(params.gerstner[g][0], params.gerstner[g][1], params.gerstner[g][2], params.gerstner[g][3]));
						m->set_shader_parameter("pca_uv_scale", Vector2(params.pcaUV[0], params.pcaUV[1]));
						m->set_shader_parameter("pca_wave_uv_scale", Vector2(params.pcaWaveUV[0], params.pcaWaveUV[1]));
						auto pcaTexture = [&](const std::string &name, const String &uniform) {
							if (name.empty() || AsciiStringUtil::compareNoCase(name, "None") == 0)
								texErrors.push_back(String("PCA required texture is unset: ") + uniform);
							else { auto tex = loadTexture(rfs, name, texErrors); if (tex.is_valid()) m->set_shader_parameter(uniform, tex); }
						};
						pcaTexture(params.pcaFrothTexture, "pca_froth_tex");
						for (int texIndex = 0; texIndex < (pcaMode == 2 ? 3 : 2); ++texIndex)
							pcaTexture(params.pcaTexture[texIndex], String("pca_tex") + String::num_int64(texIndex));
						if (pcaMode == 2) pcaTexture(params.pcaNoiseTexture, "pca_noise_tex");
						m->set_shader_parameter("specular_color", Vector3(params.specular[0], params.specular[1], params.specular[2]));
						m->set_shader_parameter("reflection_perturbation", params.reflectionPerturbation);
					}
					m->set_shader_parameter("material_diffuse", Vector4(params.diffuse[0], params.diffuse[1], params.diffuse[2], params.diffuse[3]));
					m->set_shader_parameter("dimming", params.dimming);
					m->set_shader_parameter("bump_scale", params.bumpScale);
					m->set_shader_parameter("specular_exponent", params.specularExponent);
					m->set_shader_parameter("wind_angle", params.windAngle);
					m->set_shader_parameter("wind_speed", params.windSpeed);
					m->set_shader_parameter("wave_angle", params.waveAngle);
					m->set_shader_parameter("wave_speed", params.waveSpeed);
					m->set_shader_parameter("bump_uv_scale", Vector2(params.bumpUV[0], params.bumpUV[1]));
					m->set_shader_parameter("env_uv_scale", Vector2(params.envUV[0], params.envUV[1]));
					m->set_shader_parameter("wave_uv_scale", Vector2(params.waveUV[0], params.waveUV[1]));
					if (waterLights.objects.count)
					{
						m->set_shader_parameter("light_direction", Vector3(waterLights.objects.toLight[0][0], waterLights.objects.toLight[0][1], waterLights.objects.toLight[0][2]));
						m->set_shader_parameter("light_color", Vector3(waterLights.objects.color[0][0], waterLights.objects.color[0][1], waterLights.objects.color[0][2]));
					}
					else m->set_shader_parameter("light_color", Vector3());
					m->set_shader_parameter("terrain_height_tex", heightTexture);
					m->set_shader_parameter("terrain_origin", Vector2(-hm.m_borderSize * MAP_XY_FACTOR, -hm.m_borderSize * MAP_XY_FACTOR));
					m->set_shader_parameter("terrain_size", Vector2(hm.m_width, hm.m_height));
					m->set_shader_parameter("water_height", (float)a.waterHeight);
					m->set_shader_parameter("max_alpha_depth", map.chunks.environment.waterMaxAlphaDepth);
					m->set_shader_parameter("deep_water_alpha", map.chunks.environment.deepWaterAlpha);
					m->set_shader_parameter("blend_terrain_alpha", params.blendTerrainAlpha);
					for (int64_t j = 0; j < texErrors.size(); ++j) errors.push_back(texErrors[j]);
				}
				Dictionary materialInfo;
				materialInfo["area"] = toGodot(a.name);
				materialInfo["effect"] = toGodot(a.fxShader);
				materialInfo["authored"] = authored;
				materialInfo["technique_index"] = (int64_t)params.technique;
				materialInfo["technique"] = toGodot(technique);
				materialInfo["pca_enabled"] = pcaMode != 0 && !pcaBasis.frames.empty();
				materialInfo["pca_basis"] = toGodot(pcaPath);
				materialInfo["pca_frames"] = (int64_t)pcaBasis.frames.size();
				materialInfo["pca_frame_rate"] = pcaBasis.speed * 30.0f;
				materialInfo["color"] = Vector4(params.diffuse[0], params.diffuse[1], params.diffuse[2], params.diffuse[3]);
				materialInfo["bump_uv_scale"] = Vector2(params.bumpUV[0], params.bumpUV[1]);
				waterMaterials.push_back(materialInfo);
				PackedVector3Array vtx;
				PackedVector2Array uv;
				PackedInt32Array idx;

				WaterGeometry::StandingGrid grid;
				bool geometry = pcaMode && !pcaBasis.frames.empty();
				PackedFloat32Array shore, direction;
				if (geometry) {
					if (!WaterGeometry::buildStandingGrid(a, map.chunks.standingWaveAreas,
						[&](float x,float y) { return terrain.getGroundHeight(x,y); }, grid)) {
						errors.push_back(toGodot("water grid has no cells: " + a.name)); ++failed; continue;
					}
					tri = grid.indices;
					vtx.resize(grid.vertices.size()); uv.resize(grid.vertices.size());
					shore.resize(grid.vertices.size()*4); direction.resize(grid.vertices.size()*4);
					int64_t active = 0;
					for (size_t i=0;i<grid.vertices.size();++i) {
						const auto &v=grid.vertices[i];
						vtx.set(i,toG(v.point.x,v.point.y,float(a.waterHeight)));
						uv.set(i,Vector2(v.point.x/100,v.point.y/100));
						shore.set(i*4,v.nearest.x); shore.set(i*4+1,v.nearest.y);
						shore.set(i*4+2,v.ground); shore.set(i*4+3,v.halfLength);
						direction.set(i*4,v.direction.x); direction.set(i*4+1,v.direction.y);
						direction.set(i*4+2,100); direction.set(i*4+3,0);
						if (float(a.waterHeight)-v.ground<100 &&
							std::hypot(v.point.x-v.nearest.x,v.point.y-v.nearest.y)<v.halfLength) ++active;
					}
					materialInfo["wave_vertices"] = active;
					materialInfo["grid_spacing"] = grid.spacing;
				} else {
					vtx.resize(a.points.size()); uv.resize(a.points.size());
					for (size_t i=0;i<a.points.size();++i) {
						vtx.set(i,toG(a.points[i].x,a.points[i].y,float(a.waterHeight)));
						uv.set(i,Vector2(a.points[i].x/100,a.points[i].y/100));
					}
				}
				materialInfo["vertices"] = vtx.size();
				waterMaterials[waterMaterials.size()-1] = materialInfo;
				idx.resize(tri.size());
				memcpy(idx.ptrw(),tri.data(),tri.size()*sizeof(int32_t));
				Array arrays; arrays.resize(Mesh::ARRAY_MAX);
				arrays[Mesh::ARRAY_VERTEX]=vtx; arrays[Mesh::ARRAY_TEX_UV]=uv; arrays[Mesh::ARRAY_INDEX]=idx;
				uint64_t format = 0;
				if (geometry) {
					arrays[Mesh::ARRAY_CUSTOM0]=shore; arrays[Mesh::ARRAY_CUSTOM1]=direction;
					format = (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT)<<Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT)
						   | (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT)<<Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT);
				}
				Ref<ArrayMesh> mesh;
				mesh.instantiate();
				mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), format);
				if (m.is_valid())
				{
					mesh->surface_set_material(0, m);
				}
				MeshInstance3D *mi = memnew(MeshInstance3D);
				mi->set_name(toGodot(a.name.empty() ? "water" : a.name));
				mi->set_mesh(mesh);
				if (geometry) {
					float margin=0;
					for (auto &g:params.gerstner) {
						margin += std::abs(g[0]*.075f);
						if(g[1]!=0) margin += std::abs(g[3]*.33f/g[1]);
					}
					mi->set_extra_cull_margin(margin);
				}
				mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				waterRoot->add_child(mi);
				if (geometry) for (const auto &v : grid.vertices)
					if (v.waveArea >= 0 && float(a.waterHeight)-v.ground < 100 &&
						std::hypot(v.point.x-v.nearest.x,v.point.y-v.nearest.y) < v.halfLength)
						renderedWaves[size_t(v.waveArea)] = true;
				++areas;
				tris += (int64_t)tri.size() / 3;
			}
		}
		w["max_alpha_depth"] = map.chunks.environment.waterMaxAlphaDepth;
		w["deep_water_alpha"] = map.chunks.environment.deepWaterAlpha;
		w["materials"] = waterMaterials;
		w["material_stops"] = materialStops;
		w["areas"] = areas;
		w["triangles"] = tris;
		w["triangulation_failed"] = failed;
		w["areas_in_map"] = (int64_t)map.chunks.standingWaterAreas.size();
		w["pca_wave_areas"] = (int64_t)std::count_if(map.chunks.standingWaveAreas.begin(), map.chunks.standingWaveAreas.end(), [](const StandingWaveArea &a) { return a.enablePcaWave != 0; });
		w["wave_areas_not_rendered"] = (int64_t)std::count(renderedWaves.begin(), renderedWaves.end(), false);
		m_report["water"] = w;
	}

	// ---- 4. rivers -----------------------------------------------------------------------------------
	{
		Dictionary rv;
		int64_t rendered = 0, tris = 0;
		if (wantRivers && !map.chunks.riverAreas.empty())
		{
			Ref<Shader> sh = loadShader("res://shaders/river.gdshader", errors);
			// RiverWater.fx blends a river onto the frame with SRCALPHA / INVSRCALPHA, or SRCALPHA / ONE when the map marks it additive
			// (RiverArea::additiveBlending). Godot fixes the blend in the shader's render mode, so additive rivers get a copy with blend_add.
			Ref<Shader> additive;
			if (sh.is_valid())
			{
				const String mixMode = "render_mode unshaded, blend_mix";
				if (sh->get_code().contains(mixMode))
				{
					additive.instantiate();
					additive->set_code(sh->get_code().replace(mixMode, "render_mode unshaded, blend_add"));
				}
				else
				{
					errors.push_back("river.gdshader: no \"" + mixMode + "\" render mode to make the additive copy from");
				}
			}
			int64_t additiveRendered = 0;
			float waterDiffuse[4];
			std::string waterError;
			if (!WaterSetScan::load(*arch, name, lighting ? lighting->timeOfDay : 2, waterDiffuse, &waterError))
			{
				errors.push_back(toGodot("water.ini: " + waterError));
			}
			float colour[4];
			Node3D *riverRoot = memnew(Node3D);
			riverRoot->set_name("Rivers");
			root->add_child(riverRoot);
			for (const RiverArea &r : map.chunks.riverAreas)
			{
				WaterGeometry::RiverStrip strip;
				if (!WaterGeometry::buildRiverStrip(r, strip) || sh.is_null())
				{
					continue;
				}
				const int64_t n = (int64_t)strip.position.size() / 3;
				PackedVector3Array vtx;
				PackedVector2Array uv0, uv1;
				PackedColorArray col;
				PackedInt32Array idx;
				vtx.resize(n);
				uv0.resize(n);
				uv1.resize(n);
				col.resize(n);
				WaterGeometry::riverVertexColour(r, tod, waterDiffuse, colour);
				for (int64_t i = 0; i < n; ++i)
				{
					vtx.set(i, toG(strip.position[(size_t)i * 3], strip.position[(size_t)i * 3 + 1], strip.position[(size_t)i * 3 + 2]));
					uv0.set(i, Vector2(strip.uv0[(size_t)i * 2], strip.uv0[(size_t)i * 2 + 1]));
					uv1.set(i, Vector2(strip.uv1[(size_t)i * 2], strip.uv1[(size_t)i * 2 + 1]));
					col.set(i, Color(colour[0], colour[1], colour[2], colour[3]));
				}
				idx.resize((int64_t)strip.index.size());
				memcpy(idx.ptrw(), strip.index.data(), strip.index.size() * sizeof(int32_t));
				Array arrays;
				arrays.resize(Mesh::ARRAY_MAX);
				arrays[Mesh::ARRAY_VERTEX] = vtx;
				arrays[Mesh::ARRAY_COLOR] = col;
				arrays[Mesh::ARRAY_TEX_UV] = uv0;
				arrays[Mesh::ARRAY_TEX_UV2] = uv1;
				arrays[Mesh::ARRAY_INDEX] = idx;
				Ref<ArrayMesh> mesh;
				mesh.instantiate();
				mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
				Ref<ShaderMaterial> m;
				m.instantiate();
				m->set_shader(r.additiveBlending ? additive : sh);
				m->set_render_priority(RIVER_RENDER_PRIORITY);
				// lane RENDER-3 (S-831): the river blends in the transparent pass, which the map root (a GammaCompositeHost) composites in retail's gamma
				// space: the gamma-space colour is handed over unconverted
				m->set_shader_parameter("output_gamma", 1.0f);
				Array texErrors;
				auto setTex = [&](const char *param, const std::string &texName) {
					Ref<Texture2D> t = loadTexture(rfs, texName, texErrors);
					if (t.is_valid())
					{
						m->set_shader_parameter(param, t);
					}
				};
				setTex("river_tex", r.riverTexture);
				setTex("sparkle_tex", r.sparkleTexture);
				setTex("noise_tex", r.noiseTexture);
				setTex("edge_tex", r.alphaEdgeTexture);
				m->set_shader_parameter("uv_scroll_per_second", Vector2(r.uvScrollSpeed, r.uvScrollSpeed));
				m->set_shader_parameter("opacity", r.alpha); // RiverWater.fx Opacity.w
				for (int64_t i = 0; i < texErrors.size(); ++i)
				{
					errors.push_back(texErrors[i]);
				}
				mesh->surface_set_material(0, m);
				MeshInstance3D *mi = memnew(MeshInstance3D);
				mi->set_name(toGodot(r.name.empty() ? "river" : r.name));
				mi->set_mesh(mesh);
				mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				riverRoot->add_child(mi);
				++rendered;
				additiveRendered += r.additiveBlending ? 1 : 0;
				tris += (int64_t)strip.index.size() / 3;
			}
			rv["additive"] = additiveRendered;
		}
		rv["rendered"] = rendered;
		rv["triangles"] = tris;
		rv["areas_in_map"] = (int64_t)map.chunks.riverAreas.size();
		m_report["rivers"] = rv;
	}

	// ---- 5. roads ------------------------------------------------------------------------------------
	{
		Dictionary rd;
		RoadBuildReport rrep;
		std::vector<RoadStrip> strips;
		RoadTypeIndex roadTypes;
		const float lift = 0.15f; // presentation-only: ZH raises roads by MAP_HEIGHT_SCALE/8, which z-fights here
		int64_t withNormalMap = 0, withFallbackNormal = 0; // road types with their _nrm texture / with the 1x1 fallback (RoadShading)
		Array normalMapNames;
		if (wantRoads)
		{
			if (!TerrainRoads::load(*arch, roadTypes, &error))
			{
				errors.push_back(toGodot("roads: " + error));
			}
			else
			{
				TerrainRoads::buildStrips(map.chunks.objects, roadTypes, hm, lift, strips, rrep);
			}
		}
		if (!strips.empty())
		{
			Ref<Shader> sh = loadShader("res://shaders/road.gdshader", errors);
			// Lane ROADS-1: retail draws a road with terrain.fxo "Default", RenderingMode 2 (RoadShading), so it takes the terrain material's effect inputs.
			// A road type without a _nrm texture binds a 1x1 (127, 127, 255, 0) texel, W3DRoadBuffer + 0x18 (RoadShading::FALLBACK_NORMAL_*).
			Ref<Texture2D> fallbackNormal;
			{
				PackedByteArray texel;
				texel.resize(4);
				memcpy(texel.ptrw(), RoadShading::FALLBACK_NORMAL_RGBA, 4);
				fallbackNormal = ImageTexture::create_from_image(Image::create_from_data(1, 1, false, Image::FORMAT_RGBA8, texel));
			}
			Node3D *roadRoot = memnew(Node3D);
			roadRoot->set_name("Roads");
			root->add_child(roadRoot);
			std::map<std::string, std::vector<const RoadStrip *>> byTexture;
			for (const RoadStrip &s : strips)
			{
				byTexture[lowerStr(s.texture)].push_back(&s);
			}
			const VertexColorMode vcm = vertexAll ? VERTEX_COLOR_ALL_GLOBAL_LIGHTS : VERTEX_COLOR_AMBIENT_PLUS_ACCENT_LIGHTS;
			for (const auto &g : byTexture)
			{
				PackedVector3Array vtx;
				PackedVector2Array uv;
				PackedColorArray col;
				PackedVector3Array nrm;
				PackedInt32Array idx;
				for (const RoadStrip *s : g.second)
				{
					const int32_t base = (int32_t)vtx.size();
					const int64_t n = (int64_t)s->position.size() / 3;
					for (int64_t i = 0; i < n; ++i)
					{
						const float x = s->position[(size_t)i * 3], y = s->position[(size_t)i * 3 + 1], z = s->position[(size_t)i * 3 + 2];
						vtx.push_back(toG(x, y, z));
						nrm.push_back(toG(RoadShading::VERTEX_NORMAL[0], RoadShading::VERTEX_NORMAL[1], RoadShading::VERTEX_NORMAL[2]));
						uv.push_back(Vector2(s->uv[(size_t)i * 2], s->uv[(size_t)i * 2 + 1]));
						std::uint8_t rgb[3];
						HeightMapMesh::staticDiffuseAt(hm, lighting, vcm, (int)std::lround(x / MAP_XY_FACTOR) + hm.m_borderSize,
							(int)std::lround(y / MAP_XY_FACTOR) + hm.m_borderSize, rgb, lightScales.terrain);
						col.push_back(Color(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f));
					}
					for (std::uint32_t ix : s->index)
					{
						idx.push_back(base + (int32_t)ix);
					}
				}
				Array arrays;
				arrays.resize(Mesh::ARRAY_MAX);
				arrays[Mesh::ARRAY_VERTEX] = vtx;
				arrays[Mesh::ARRAY_NORMAL] = nrm;
				arrays[Mesh::ARRAY_COLOR] = col;
				arrays[Mesh::ARRAY_TEX_UV] = uv;
				arrays[Mesh::ARRAY_INDEX] = idx;
				Ref<ArrayMesh> mesh;
				mesh.instantiate();
				mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
				if (sh.is_valid())
				{
					Ref<ShaderMaterial> m;
					m.instantiate();
					m->set_shader(sh);
					m->set_render_priority(ROAD_RENDER_PRIORITY);
					// lane RENDER-3 (S-831): the road blends in the transparent pass, which the map root (a GammaCompositeHost) composites in retail's gamma
					// space: the gamma-space colour is handed over unconverted
					m->set_shader_parameter("output_gamma", 1.0f);
					Array texErrors;
					Ref<Texture2D> t = loadTexture(rfs, g.second.front()->texture, texErrors);
					if (t.is_valid()) m->set_shader_parameter("road_tex", t);
					// RoadType::loadTexture: the _nrm companion when it exists (Render_Obj_Exists), else the fallback texel. Only "not found" is the
					// optional case; a companion that exists but cannot be decoded is an error.
					const std::string nrmName = RoadShading::normalTextureName(g.second.front()->texture);
					Array nrmErrors;
					Ref<Texture2D> nrmTex = loadTexture(rfs, nrmName, nrmErrors);
					if (nrmTex.is_valid())
					{
						++withNormalMap;
						normalMapNames.push_back(toGodot(nrmName));
					}
					else
					{
						++withFallbackNormal;
						nrmTex = fallbackNormal;
						for (int64_t i = 0; i < nrmErrors.size(); ++i)
						{
							if (nrmErrors[i] != Variant(toGodot("texture not found: " + nrmName)))
							{
								errors.push_back(nrmErrors[i]);
							}
						}
					}
					m->set_shader_parameter("road_normal_tex", nrmTex);
					// the terrain's light environment (applyMapLighting), also with the terrain mesh disabled
					applyMapLighting(m);
					for (int64_t i = 0; i < texErrors.size(); ++i)
					{
						errors.push_back(texErrors[i]);
					}
					mesh->surface_set_material(0, m);
				}
				MeshInstance3D *mi = memnew(MeshInstance3D);
				mi->set_name(toGodot("Road_" + g.first));
				mi->set_mesh(mesh);
				mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				roadRoot->add_child(mi);
			}
		}
		rd["shading"] = "terrain.fxo Default, RenderingMode 2: PSDefault_Array element " + String::num_int64(RoadShading::pixelShaderElement(RoadShading::RENDERING_MODE, 0, false));
		rd["normal_maps"] = withNormalMap;
		rd["fallback_normals"] = withFallbackNormal;
		rd["normal_map_names"] = normalMapNames;
		if (!strips.empty())
		{
			rd["sample_strip"] = toG(strips[0].position[0], strips[0].position[1], strips[0].position[2]);
			rd["sample_texture"] = toGodot(strips[0].texture);
		}
		rd["pairs"] = rrep.pairs;
		rd["orphan_points"] = rrep.orphanPoints;
		rd["duplicate_segments"] = rrep.duplicateSegments;
		rd["unknown_road_types"] = rrep.unknownRoadTypes;
		rd["mitered_joins"] = rrep.miteredJoins;
		rd["tee_joins"] = rrep.teeJoins;
		rd["four_way_joins"] = rrep.fourWayJoins;
		rd["end_caps"] = rrep.endCaps;
		rd["special_junctions_pending"] = rrep.specialJunctionsPending;
		rd["rounded_joins"] = rrep.roundedJoins;
		rd["strips"] = (int64_t)rrep.strips;
		rd["vertices"] = (int64_t)rrep.vertices;
		rd["triangles"] = (int64_t)rrep.triangles;
		rd["lift_world_units"] = lift;
		m_report["roads"] = rd;
	}

	// ---- 6. object markers ----------------------------------------------------------------------------
	{
		Dictionary od;
		struct Marker
		{
			Transform3D xf;
			Color color;
		};
		std::vector<Marker> markers;
		int64_t waypointN = 0, startN = 0, scorchN = 0, genericN = 0, plainN = 0, roadPts = 0;
		ObjectNameIndex names;
		bool haveNames = false;
		if (resolveObjects)
		{
			std::string nerr;
			haveNames = ObjectNameScan::build(*arch, names, &nerr);
			if (!haveNames)
			{
				errors.push_back(toGodot("object name scan: " + nerr));
			}
		}
		std::set<std::string> unresolved;
		const std::string mapIni = mapDir + "\\map.ini";
		for (const MapObject &o : map.chunks.objects)
		{
			if (o.getFlag(FLAG_ROAD_FLAGS))
			{
				++roadPts;
				continue; // road points are drawn by the road strips
			}
			if (haveNames && !o.m_objectName.empty() && names.resolveObject(o, mapIni) == ObjectNameIndex::Unresolved)
			{
				unresolved.insert(o.m_objectName);
			}
			Vector3 size(5, 9, 5);
			Color color;
			if (o.isWaypoint())
			{
				const std::string wn = o.getWaypointName();
				if (wn.rfind("Player_", 0) == 0 && wn.size() > 6 && wn.compare(wn.size() - 6, 6, "_Start") == 0)
				{
					color = Color(0.1f, 1.0f, 0.2f);
					size = Vector3(10, 60, 10);
					++startN;
				}
				else
				{
					color = Color(0.1f, 0.8f, 1.0f);
					size = Vector3(4, 16, 4);
					++waypointN;
				}
			}
			else if (o.isScorch())
			{
				color = Color(0.05f, 0.05f, 0.05f);
				size = Vector3(12, 0.4f, 12);
				++scorchN;
			}
			else if (o.m_objectName.rfind("*GenericAIObjects", 0) == 0)
			{
				color = Color(0.9f, 0.2f, 0.9f);
				size = Vector3(6, 22, 6);
				++genericN;
			}
			else
			{
				// a stable colour per template name
				std::uint32_t h = 2166136261u;
				for (char c : o.m_objectName)
				{
					h = (h ^ (std::uint8_t)c) * 16777619u;
				}
				color = Color::from_hsv((float)(h % 360) / 360.0f, 0.55f, 0.95f);
				++plainN;
			}
			const float gz = terrain.getGroundHeight(o.m_location.x, o.m_location.y) + o.m_location.z;
			Transform3D xf(Basis(Vector3(0, 1, 0), o.m_angle) * Basis::from_scale(size), toG(o.m_location.x, o.m_location.y, gz + size.y * 0.5f));
			markers.push_back({ xf, color });
		}
		if (wantMarkers && !markers.empty())
		{
			Ref<MultiMesh> mm;
			mm.instantiate();
			mm->set_transform_format(MultiMesh::TRANSFORM_3D);
			mm->set_use_colors(true);
			Ref<BoxMesh> box;
			box.instantiate();
			box->set_size(Vector3(1, 1, 1));
			Ref<StandardMaterial3D> mat;
			mat.instantiate();
			mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			box->set_material(mat);
			mm->set_mesh(box);
			mm->set_instance_count((int32_t)markers.size());
			for (size_t i = 0; i < markers.size(); ++i)
			{
				mm->set_instance_transform((int32_t)i, markers[i].xf);
				mm->set_instance_color((int32_t)i, markers[i].color);
			}
			MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
			mmi->set_name("ObjectMarkers");
			mmi->set_multimesh(mm);
			mmi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			root->add_child(mmi);
		}
		od["markers"] = wantMarkers ? (int64_t)markers.size() : (int64_t)0;
		od["player_starts"] = startN;
		od["waypoints"] = waypointN;
		od["scorch_decals"] = scorchN;
		od["generic_ai"] = genericN;
		od["other"] = plainN;
		od["road_points_not_marked"] = roadPts;
		od["resolved_names"] = haveNames;
		Array un;
		for (const std::string &s : unresolved)
		{
			un.push_back(toGodot(s));
		}
		od["unresolved_template_names"] = un;
		od["note"] = "markers only: object templates need the INI object model (another lane)";
		m_report["objects"] = od;
	}

	for (const MapStop *s : MapStops::terrainRender())
	{
		stops.push_back(toGodot(s->id));
	}
	for (const MapStop *s : MapStops::terrainLogic())
	{
		stops.push_back(toGodot(s->id)); // S-060: the ground heights used above are not on the numeric facade
	}
	timings["total"] = nowMs() - t0;
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_warning("MapTerrainBuilder: ", errors[i]);
	}
	return root;
}

} // namespace godot
