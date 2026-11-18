// OpenBFME. GPL-3.0. See GodotFXPlayer.h.

#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotFXPlayer.h"
#include "GodotDevice/GodotShaderBudget.h"

#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameClient/ParticleTexture.h"
#include "Libraries/WWVegas/WW3D2/fxshadergen.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>
#include <type_traits>

namespace godot
{

namespace
{

String toGodot(const std::string &s) { return String::utf8(s.c_str(), (int64_t)s.size()); }
std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

double usSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y)
Vector3 toGodotSpace(const Coord3D &c) { return Vector3(c.x, c.z, -c.y); }
Coord3D toSage(const Vector3 &v) { return Coord3D{ v.x, -v.z, v.y }; }

using namespace FXParticleSystem;

Ref<ArrayMesh> makeQuadMesh()
{
	// corners 0..3 of the point group quad: positions (-0.5, 0.5), (-0.5, -0.5), (0.5, -0.5), (0.5, 0.5); UV (0,0) (0,1) (1,1) (1,0); UV2.x = corner index
	PackedVector3Array v;
	PackedVector2Array uv, uv2;
	const float pos[4][2] = { { -0.5f, 0.5f }, { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f } };
	const float tc[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
	for (int k = 0; k < 4; ++k)
	{
		v.push_back(Vector3(pos[k][0], pos[k][1], 0.0f));
		uv.push_back(Vector2(tc[k][0], tc[k][1]));
		uv2.push_back(Vector2((float)k, 0.0f));
	}
	PackedInt32Array idx;
	const int ids[6] = { 0, 1, 2, 2, 3, 0 }; // RW indices (0,1,2)(2,3,0)
	for (int i : ids)
	{
		idx.push_back(i);
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = v;
	arrays[Mesh::ARRAY_TEX_UV] = uv;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2;
	arrays[Mesh::ARRAY_INDEX] = idx;
	Ref<ArrayMesh> m;
	m.instantiate();
	m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	m->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
	return m;
}

} // namespace

FXPlayer::FXPlayer()
{
	// lane FX-3 round 2: the client setting (Project Settings openbfme/rendering/soft_particles; on unless set to false)
	ProjectSettings *ps = ProjectSettings::get_singleton();
	m_soft = !(ps && ps->has_setting("openbfme/rendering/soft_particles")) || (bool)ps->get_setting("openbfme/rendering/soft_particles");
}
FXPlayer::~FXPlayer() {}

void FXPlayer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &FXPlayer::setup);
	ClassDB::bind_method(D_METHOD("preload_textures", "budget_ms"), &FXPlayer::preload_textures);
	ClassDB::bind_method(D_METHOD("list_particle_systems"), &FXPlayer::list_particle_systems);
	ClassDB::bind_method(D_METHOD("list_fx_lists"), &FXPlayer::list_fx_lists);
	ClassDB::bind_method(D_METHOD("play_particle_system", "name", "position"), &FXPlayer::play_particle_system);
	ClassDB::bind_method(D_METHOD("play_fx_list", "name", "position", "as_object"), &FXPlayer::play_fx_list);
	ClassDB::bind_method(D_METHOD("define_ini", "name", "text"), &FXPlayer::define_ini);
	ClassDB::bind_method(D_METHOD("play_w3d_emitter", "name", "position"), &FXPlayer::play_w3d_emitter);
	ClassDB::bind_method(D_METHOD("clear"), &FXPlayer::clear);
	ClassDB::bind_method(D_METHOD("seed", "seed"), &FXPlayer::seed);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &FXPlayer::advance);
	ClassDB::bind_method(D_METHOD("step_once"), &FXPlayer::step_once);
	ClassDB::bind_method(D_METHOD("rebuild"), &FXPlayer::rebuild);
	ClassDB::bind_method(D_METHOD("get_scorch_count"), &FXPlayer::scorchCount); // lane FEAT-BLDFX-1
	ClassDB::bind_method(D_METHOD("get_decal_count"), &FXPlayer::decalCount);   // lane FEAT-BLDFX-1
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &FXPlayer::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &FXPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("set_max_particles", "count"), &FXPlayer::set_max_particles);
	ClassDB::bind_method(D_METHOD("set_particle_lod", "max_count", "min_priority", "min_skip_priority"), &FXPlayer::set_particle_lod); // lane OPTS-2
	ClassDB::bind_method(D_METHOD("get_stats"), &FXPlayer::get_stats);
	ClassDB::bind_method(D_METHOD("get_unverified"), &FXPlayer::get_unverified);
	ClassDB::bind_method(D_METHOD("get_events"), &FXPlayer::get_events);
	ClassDB::bind_method(D_METHOD("get_frame"), &FXPlayer::get_frame);
	// lane FX-3 round 2: soft particles (default: the project setting openbfme/rendering/soft_particles, on when absent); false = retail's hard depth cut
	ClassDB::bind_method(D_METHOD("set_soft_particles", "enabled"), &FXPlayer::set_soft_particles);
	ClassDB::bind_method(D_METHOD("get_soft_particles"), &FXPlayer::get_soft_particles);
	ClassDB::bind_method(D_METHOD("set_batch_summary", "enabled"), &FXPlayer::set_batch_summary); // lane RENDER-4
	ClassDB::bind_method(D_METHOD("set_extra_pose_frustum", "planes"), &FXPlayer::set_extra_pose_frustum); // lane GPUPERF-3 (caster_pip.gd calls it)
	ClassDB::bind_method(D_METHOD("set_frustum_culling", "enabled"), &FXPlayer::set_frustum_culling);
	ClassDB::bind_method(D_METHOD("get_frustum_culling"), &FXPlayer::get_frustum_culling);
	ClassDB::bind_method(D_METHOD("get_batch_summary"), &FXPlayer::get_batch_summary);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "soft_particles"), "set_soft_particles", "get_soft_particles");
}

void FXPlayer::set_soft_particles(bool enabled)
{
	if (m_soft == enabled)
	{
		return;
	}
	m_soft = enabled;
	m_buildDirty = true;
	for (Batch &b : m_batches)
	{
		b.key.clear(); // every slot rebuilds its node and material with the other shader at the next upload
	}
}

void FXPlayer::_notification(int what)
{
	// lane RENDER-3 (S-831): the particle shaders output gamma-space values; the camera that shows them composites the transparent pass in gamma space
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		GammaComposite::ensure(this);
	}
}

Dictionary FXPlayer::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	Array errors;
	m_fs = fs;
	if (fs.is_null() || !fs->is_mounted() || !fs->archive())
	{
		errors.push_back("the retail file system is not mounted");
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	m_preloaded.clear(); // lane PERF-1 r2: a new setup queues the new data's textures
	m_preloadQueue.clear();
	m_preloadNext = 0;
	m_preloadQueued = false;
	if (m_decalRoot)
	{
		// lane FEAT-BLDFX-1: the previous data's decal nodes and their state go with the previous playback
		for (auto &kv : m_decalNodes)
		{
			kv.second->queue_free();
		}
		m_decalNodes.clear();
		if (m_scorchNode)
		{
			m_scorchNode->queue_free();
		}
		m_scorchVersion = 0;
		m_scorchNode = nullptr;
		m_scorchMesh.unref();
		m_hadDecals = false;
	}
	m_playback = std::make_unique<FXPlayback>(*fs->archive(), RandomAlgorithm::RotWK_GameDat_LCG);
	for (const std::string &e : m_playback->loadRetailData())
	{
		errors.push_back(toGodot(e));
	}
	m_renderRandom = std::make_unique<W3DClientRandom>(RandomAlgorithm::RotWK_GameDat_LCG);
	m_renderRandom->seed(1);
	m_builder = std::make_unique<ParticleDrawBuilder>(*m_renderRandom);
	m_quad = makeQuadMesh();
	// lane GPUPERF-3 review: a second setup replaces the playback (and the builder) but the uploaded batches belong to the old one; the render nodes and
	// the cache flags stay, so invalidate them: the next advance rebuilds from the new playback instead of keeping the old effects
	m_built = false;
	m_buildDirty = true;
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	result["particle_systems"] = (int64_t)m_playback->particleTemplates().templateCount();
	result["fx_lists"] = (int64_t)m_playback->fxLists().listCount();
	return result;
}

PackedStringArray FXPlayer::list_particle_systems() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &n : m_playback->particleTemplates().templateNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

PackedStringArray FXPlayer::list_fx_lists() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &n : m_playback->fxLists().listNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

int64_t FXPlayer::play_particle_system(const String &name, const Vector3 &position)
{
	if (!m_playback)
	{
		return 0;
	}
	m_buildDirty = true;
	m_playGen.fetch_add(1, std::memory_order_relaxed);
	return (int64_t)m_playback->playParticleSystem(toNative(name), toSage(position));
}

bool FXPlayer::play_fx_list(const String &name, const Vector3 &position, bool as_object)
{
	m_buildDirty = true;
	m_playGen.fetch_add(1, std::memory_order_relaxed);
	return m_playback && m_playback->playFXList(toNative(name), toSage(position), as_object);
}

Array FXPlayer::define_ini(const String &name, const String &text)
{
	Array errors;
	if (!m_playback)
	{
		errors.push_back("FXPlayer.setup has not run");
		return errors;
	}
	m_buildDirty = true;
	m_playGen.fetch_add(1, std::memory_order_relaxed);
	for (const std::string &e : m_playback->loadIniText(toNative(name), toNative(text)))
	{
		errors.push_back(toGodot(e));
	}
	return errors;
}

Dictionary FXPlayer::play_w3d_emitter(const String &name, const Vector3 &position)
{
	Dictionary d;
	if (!m_playback)
	{
		d["ok"] = false;
		d["error"] = "FXPlayer.setup has not run";
		return d;
	}
	std::string error;
	m_buildDirty = true;
	m_playGen.fetch_add(1, std::memory_order_relaxed);
	ParticleEmitterInstance *e = m_playback->playW3DEmitter(toNative(name), toSage(position), &error);
	d["ok"] = e != nullptr;
	d["error"] = toGodot(error);
	return d;
}

void FXPlayer::clear()
{
	if (m_playback)
	{
		m_playback->emitters().clear();
		m_playback->particles().reset();
		m_playback->clearEvents();
		m_playback->clearClientEffects(); // lane FEAT-BLDFX-1: scorches, decals and light pulses belong to the game that is going
	}
	updateClientEffects();
	updateLightPulseUniforms(); // the shader globals outlive the game: switch the cleared pulses off now, not at the next advance
	rebuild();
}

void FXPlayer::seed(int64_t seed)
{
	if (m_playback)
	{
		m_playback->seedClientRandom((std::uint32_t)seed);
	}
}

void FXPlayer::set_max_particles(int64_t count)
{
	if (m_playback)
	{
		m_playback->particles().settings().maxParticleCount = (std::uint32_t)count;
	}
}

void FXPlayer::set_particle_lod(int64_t max_count, int64_t min_priority, int64_t min_skip_priority)
{
	if (m_playback)
	{
		ParticleSettings &set = m_playback->particles().settings();
		set.maxParticleCount = (std::uint32_t)(max_count < 0 ? 0 : max_count);
		set.minDynamicParticlePriority = (int)min_priority;
		set.minDynamicParticleSkipPriority = (int)min_skip_priority;
	}
}

void FXPlayer::step_once()
{
	if (!m_playback)
	{
		return;
	}
	const auto t0 = std::chrono::steady_clock::now();
	m_playback->step();
	// lane GPUPERF-3 review: a step taken by hand (a paused caller, a test) changes the particles and emitters outside advance()'s accumulator loop, so
	// the next advance must rebuild even if it takes no step of its own; otherwise the picture stays at the last built frame
	m_buildDirty = true;
	m_stepUs = usSince(t0);
	updateClientEffects();
}

const ScorchBuffer &FXPlayer::scorches() const
{
	static const ScorchBuffer empty;
	return m_playback ? m_playback->scorches() : empty;
}

int64_t FXPlayer::decalCount() const
{
	return m_playback ? (int64_t)m_playback->decals().size() : 0;
}

// lane FEAT-BLDFX-1: the terrain decals. Retail draws the scorches as one terrain-conforming grid per scorch in a 3 x 3 atlas cell
// (DONOR BFME2 0x678BB) and the dynamic decals as projected quads with the opacity envelope of DONOR BFME2 0x10C785. Both are client-only presentation;
// this renderer batches each texture into one ArrayMesh and never feeds anything back into the simulation.
void FXPlayer::updateClientEffects()
{
	if (!m_playback)
	{
		return;
	}
	if (!m_decalRoot)
	{
		m_decalRoot = memnew(Node3D);
		m_decalRoot->set_name("TerrainDecals");
		add_child(m_decalRoot);
	}
	// ---- scorches (TARGET RW 0x5E0045; DONOR BFME2 0x67EA3 / 0x678BB) ----
	const ScorchBuffer &scorches = m_playback->scorches();
	if (scorches.version() != m_scorchVersion)
	{
		m_scorchVersion = scorches.version();
		if (!m_scorchNode)
		{
			m_scorchMaterial.instantiate();
			Ref<Shader> shader;
			shader.instantiate();
			shader->set_code("shader_type spatial;\n"
							 "render_mode blend_mix, depth_draw_never, cull_disabled, unshaded, shadows_disabled;\n"
							 "uniform sampler2D decal_tex : filter_linear_mipmap, repeat_disable;\n"
							 "void fragment() {\n"
							 "\tvec4 t = texture(decal_tex, UV);\n"
							 "\tALBEDO = t.rgb * COLOR.rgb;\n"
							 "\tALPHA = t.a * COLOR.a;\n"
							 "}\n");
			m_scorchMaterial->set_shader(shader);
			m_scorchNode = memnew(MeshInstance3D);
			m_scorchNode->set_name("Scorches");
			m_scorchNode->set_material_override(m_scorchMaterial);
			m_scorchNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			m_decalRoot->add_child(m_scorchNode);
		}
		const int kCells = 4; // 4 x 4 cells per scorch: the retail grid's 10-unit cells are finer; 4 keeps 500 scorches at 12.5k vertices
		PackedVector3Array verts;
		PackedVector2Array uvs;
		PackedColorArray colors;
		PackedInt32Array indices;
		for (const ScorchFx &s : scorches.entries())
		{
			if (!(s.radius > 0.0f))
			{
				continue;
			}
			const int type = s.type < 0 || s.type >= 9 ? 0 : s.type; // DONOR BFME2 0x678BB: a type outside 0 .. 8 draws SCORCH_1
			const float u0 = (float)(type % 3) / 3.0f;
			const float v0 = (float)(type / 3) / 3.0f;
			const int32_t base = (int32_t)verts.size();
			for (int j = 0; j <= kCells; ++j)
			{
				for (int i = 0; i <= kCells; ++i)
				{
					const float fx = (float)i / (float)kCells;
					const float fy = (float)j / (float)kCells;
					const float x = s.position.x + (fx * 2.0f - 1.0f) * s.radius;
					const float y = s.position.y + (fy * 2.0f - 1.0f) * s.radius;
					const float z = m_playback->groundHeight(x, y) + 1.0f; // DONOR BFME2 0x678BB: the scorch sits one unit above the terrain
					verts.push_back(toGodotSpace(Coord3D{ x, y, z }));
					uvs.push_back(Vector2(u0 + fx / 3.0f, v0 + fy / 3.0f));
					colors.push_back(Color(1, 1, 1, 1));
				}
			}
			for (int j = 0; j < kCells; ++j)
			{
				for (int i = 0; i < kCells; ++i)
				{
					const int32_t a = base + j * (kCells + 1) + i;
					const int32_t b = a + 1;
					const int32_t c = a + (kCells + 1);
					const int32_t d = c + 1;
					indices.push_back(a); indices.push_back(b); indices.push_back(c);
					indices.push_back(b); indices.push_back(d); indices.push_back(c);
				}
			}
		}
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		if (!verts.is_empty())
		{
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = verts;
			arrays[Mesh::ARRAY_TEX_UV] = uvs;
			arrays[Mesh::ARRAY_COLOR] = colors;
			arrays[Mesh::ARRAY_INDEX] = indices;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		}
		// bound at every rebuild: an asset-pack switch clears the texture cache (clearTextureCache) and keeps this material
		m_scorchMaterial->set_shader_parameter("decal_tex", particleTexture("exscorch01.tga"));
		m_scorchMesh = mesh;
		m_scorchNode->set_mesh(m_scorchMesh);
	}
	// ---- dynamic decals (TARGET RW 0x5E05AB; DONOR BFME2 0x1E0E43 / 0x10C785) ----
	const std::vector<DecalFx> &decals = m_playback->decals();
	if (decals.empty() && !m_hadDecals)
	{
		return;
	}
	m_hadDecals = !decals.empty();
	// two materials: the DynamicDecal Shader field ADDITIVE (1) makes an additive decal (type 0x800), every other value an alpha decal (type 0x400):
	// DONOR BFME2 0x1E0E43 `m_type = m_shader == 1 ? 0x800 : 0x400` (SUBTRACT draws as ALPHA; no retail FXList sets the field)
	for (int shader = 0; shader < 2; ++shader)
	{
		if (m_decalShaders[shader].is_valid())
		{
			continue;
		}
		const char *mode = shader == 1 ? "blend_add" : "blend_mix";
		Ref<Shader> s;
		s.instantiate();
		const std::string code = std::string("shader_type spatial;\nrender_mode ") + mode
			+ ", depth_draw_never, cull_disabled, unshaded, shadows_disabled;\n"
			  "uniform sampler2D decal_tex : filter_linear_mipmap, repeat_disable;\n"
			  "void fragment() {\n"
			  "\tvec4 t = texture(decal_tex, UV);\n"
			  "\tALBEDO = t.rgb * COLOR.rgb;\n"
			  "\tALPHA = t.a * COLOR.a;\n"
			  "}\n";
		s->set_code(toGodot(code));
		m_decalShaders[shader] = s;
	}
	std::map<DecalKey, std::vector<const DecalFx *>> groups;
	for (const DecalFx &d : decals)
	{
		const int shader = d.desc.shader == 1 ? 1 : 0;
		groups[DecalKey{ d.desc.name, shader }].push_back(&d);
	}
	for (auto &kv : groups)
	{
		MeshInstance3D *node = nullptr;
		auto it = m_decalNodes.find(kv.first);
		if (it != m_decalNodes.end())
		{
			node = it->second;
		}
		else
		{
			node = memnew(MeshInstance3D);
			node->set_name(toGodot(kv.first.name));
			Ref<ShaderMaterial> mat;
			mat.instantiate();
			mat->set_shader(m_decalShaders[kv.first.shader]);
			mat->set_shader_parameter("decal_tex", particleTexture(kv.first.name));
			node->set_material_override(mat);
			node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			m_decalRoot->add_child(node);
			m_decalNodes[kv.first] = node;
		}
		PackedVector3Array verts;
		PackedVector2Array uvs;
		PackedColorArray colors;
		PackedInt32Array indices;
		for (const DecalFx *d : kv.second)
		{
			const float opacity = (float)d->opacityAt(m_playback->clientFrame()) / 255.0f;
			const float r = (float)((d->desc.color >> 16) & 0xff) / 255.0f;
			const float g = (float)((d->desc.color >> 8) & 0xff) / 255.0f;
			const float b = (float)(d->desc.color & 0xff) / 255.0f;
			// ShadowTypeInfo's sizeX / sizeY are the decal's full width / height (ZH W3DProjectedShadowManager::addDecal: m_decalSizeX = sizeX,
			// the quad's corners at objPos -+ size * 0.5)
			const float h = d->desc.size * 0.5f;
			const float c = std::cos(d->desc.yaw), s = std::sin(d->desc.yaw);
			const int32_t base = (int32_t)verts.size();
			const float corner[4][2] = { { -h, -h }, { h, -h }, { h, h }, { -h, h } };
			for (int k = 0; k < 4; ++k)
			{
				const float lx = corner[k][0], ly = corner[k][1];
				const float x = d->desc.position.x + lx * c - ly * s;
				const float y = d->desc.position.y + lx * s + ly * c;
				const float z = m_playback->groundHeight(x, y) + 1.0f;
				verts.push_back(toGodotSpace(Coord3D{ x, y, z }));
				colors.push_back(Color(r, g, b, opacity));
				uvs.push_back(k == 0 ? Vector2(0, 0) : k == 1 ? Vector2(1, 0) : k == 2 ? Vector2(1, 1) : Vector2(0, 1));
			}
			indices.push_back(base); indices.push_back(base + 1); indices.push_back(base + 2);
			indices.push_back(base); indices.push_back(base + 2); indices.push_back(base + 3);
		}
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		if (!verts.is_empty())
		{
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = verts;
			arrays[Mesh::ARRAY_TEX_UV] = uvs;
			arrays[Mesh::ARRAY_COLOR] = colors;
			arrays[Mesh::ARRAY_INDEX] = indices;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		}
		node->set_mesh(mesh);
		node->set_visible(!verts.is_empty());
	}
	// a texture / shader group that no longer has a decal
	for (auto it = m_decalNodes.begin(); it != m_decalNodes.end();)
	{
		if (groups.find(it->first) == groups.end())
		{
			it->second->queue_free();
			it = m_decalNodes.erase(it);
		}
		else
		{
			++it;
		}
	}

}

void FXPlayer::updateLightPulseUniforms()
{
	if (!m_playback)
	{
		return;
	}
	W3D_Ensure_Light_Globals(); // the four pulse names must exist before the first set (the material factory usually registers them first)
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs)
	{
		return;
	}
	// the shader's lights are view-space (the object shaders light in view space); the camera is the one the next frame draws
	Transform3D view;
	bool haveView = false;
	Vector3 cameraWorldPos;
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			view = c->get_global_transform().affine_inverse();
			cameraWorldPos = c->get_global_position();
			haveView = true;
		}
	}
	const std::vector<Plane> planes = cullPlanes();

	struct CandidateLight
	{
		const LightPulseFx *light = nullptr;
		Vector3 viewPos;
		float contribution = 0.0f; // RW 0x005403ED (Diffuse.Length2)
	};
	std::vector<CandidateLight> candidates;
	for (const LightPulseFx &l : m_playback->lightPulses().lights())
	{
		if (!l.enabled)
		{
			continue;
		}
		const RGBColor c = l.colorNow();
		// RW 0x00540261 (LightEnvironmentClass::Add_Light): black light cull (< 0.05f on all RGB channels, 0x00BDD760)
		if (c.red < 0.05f && c.green < 0.05f && c.blue < 0.05f)
		{
			continue;
		}
		const float range = l.range();
		if (range <= 0.0001f)
		{
			continue;
		}
		const Vector3 world = toGodotSpace(l.position);

		// RW 0x0046FCF9 (RTS3DScene::renderOneObject): cull against view frustum (Spheres_Intersect)
		bool culled = false;
		for (const Plane &pl : planes)
		{
			if (pl.distance_to(world) < -range)
			{
				culled = true;
				break;
			}
		}
		if (culled)
		{
			continue;
		}

		// RW 0x0053F640: linear attenuation over range: clamp(1.0 - (dist - attenStart)/(attenEnd - attenStart))
		// RW 0x005403ED: Contribution = effectiveDiffuse.Length2()
		// INFERENCE (S-687): retail ranks per render object at the object's centre; the Godot uniforms are viewport-global, so the
		// distance here is measured from the camera
		float contribution = 0.0f;
		if (haveView)
		{
			const float dist = (world - cameraWorldPos).length();
			const float atten = std::clamp(1.0f - (dist - l.farAttenStart) / std::max(0.001f, range - l.farAttenStart), 0.0f, 1.0f);
			const float effR = c.red * atten;
			const float effG = c.green * atten;
			const float effB = c.blue * atten;
			contribution = effR * effR + effG * effG + effB * effB;
		}
		else
		{
			contribution = c.red * c.red + c.green * c.green + c.blue * c.blue;
		}

		const Vector3 pos = haveView ? view.xform(world) : world;
		candidates.push_back({ &l, pos, contribution });
	}

	// RW 0x0054044B: sorted by Contribution descending, top 4 chosen (MAX_LIGHTS = 4)
	if (candidates.size() > 4)
	{
		std::stable_sort(candidates.begin(), candidates.end(), [](const CandidateLight &a, const CandidateLight &b) {
			return a.contribution > b.contribution;
		});
	}

	size_t used = 0;
	for (const CandidateLight &cand : candidates)
	{
		if (used >= 4)
		{
			break;
		}
		const LightPulseFx &l = *cand.light;
		const RGBColor c = l.colorNow();
		const String base = String("w3d_pulse") + String::num_int64((int64_t)used);
		rs->global_shader_parameter_set(StringName(base), Vector4(cand.viewPos.x, cand.viewPos.y, cand.viewPos.z, l.range()));
		rs->global_shader_parameter_set(StringName(base + String("_col")), Vector4(c.red, c.green, c.blue, 0.0f));
		++used;
	}
	for (size_t i = used; i < 4; ++i)
	{
		const String base = String("w3d_pulse") + String::num_int64((int64_t)i);
		rs->global_shader_parameter_set(StringName(base), Vector4());
		rs->global_shader_parameter_set(StringName(base + String("_col")), Vector4());
	}
}

void FXPlayer::advance(double delta)
{
	if (!m_playback)
	{
		return;
	}
	bool stepped = false;
	if (!m_paused)
	{
		m_accumulator += delta;
		int steps = 0;
		const double dt = 1.0 / 30.0; // RW: one particle step per Display::draw at the 30 FPS limit
		while (m_accumulator >= dt && steps < 4)
		{
			step_once();
			m_accumulator -= dt;
			++steps;
		}
		if (steps == 4)
		{
			m_accumulator = 0.0; // never spiral
		}
		stepped = steps > 0;
	}
	// lane GPUPERF-3 (presentation only): the batches are a function of the particles (changed by a step, or by the live game's effect calls between
	// steps: the playback signature), the cameras (the billboards' sides, the sort, the culling) and the draw settings; a render frame with none of
	// them changed keeps the batches it has (retail draws once per particle step: its 30 Hz Display::draw), so the render stream is not drawn from
	// either (the GpuDraw alpha keys are sampled once per built frame, RW 0x5f66b7)
	if (!stepped && m_built && !m_buildDirty && !m_summaryEnabled)
	{
		Transform3D cam;
		if (Viewport *vp = get_viewport())
		{
			if (Camera3D *c = vp->get_camera_3d())
			{
				cam = c->get_global_transform();
			}
		}
		if (cam == m_builtCamera && (m_frustumCulling ? cullPlanes() : std::vector<Plane>()) == m_builtPlanes && playbackSignature() == m_builtSignature)
		{
			++m_rebuildsSkipped;
			return;
		}
	}
	updateLightPulseUniforms();
	rebuild();
}

bool FXPlayer::set_extra_pose_frustum(const TypedArray<Plane> &planes)
{
	if (planes.size() != 0 && planes.size() != 6)
	{
		return false;
	}
	m_extraFrustum.clear();
	for (int64_t i = 0; i < planes.size(); ++i)
	{
		m_extraFrustum.push_back(planes[i]);
	}
	return true;
}

std::vector<Plane> FXPlayer::cullPlanes() const
{
	std::vector<Plane> out;
	Viewport *vp = is_inside_tree() ? get_viewport() : nullptr;
	Camera3D *c = vp ? vp->get_camera_3d() : nullptr;
	if (!c)
	{
		return out; // no camera: nothing is culled
	}
	const TypedArray<Plane> main = c->get_frustum();
	if (main.size() != 6)
	{
		return out;
	}
	for (int64_t k = 0; k < main.size(); ++k)
	{
		out.push_back(main[k]);
	}
	for (const Plane &pl : m_extraFrustum)
	{
		out.push_back(pl);
	}
	return out;
}

uint64_t FXPlayer::playbackSignature() const
{
	// lane GPUPERF-3: what changes the batches between particle steps is a play call (the live game's effect calls, on the logic thread): a counter,
	// not a walk of the playback's containers, which the logic thread may be adding to while the render thread reads them
	return m_playGen.load(std::memory_order_relaxed);
}

void FXPlayer::setNodeAabb(Batch &b, GeometryInstance3D *node, const AABB &box)
{
	if (!node || (b.haveAabb && b.lastAabb == box))
	{
		return;
	}
	node->set_custom_aabb(box);
	b.lastAabb = box;
	b.haveAabb = true;
}

Ref<Texture2D> FXPlayer::loadTexture(const std::string &name)
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}
	auto pre = m_preloaded.find(name);
	if (pre != m_preloaded.end())
	{
		// lane PERF-1 r2: decoded ahead (preload_textures); its miss, if any, is reported now, at the first use, as a load here would have
		if (!pre->second.missing.empty())
		{
			m_missing.push_back(pre->second.missing);
		}
		Ref<Texture2D> out = pre->second.texture;
		m_preloaded.erase(pre);
		m_textures[name] = out;
		return out;
	}
	std::string missing;
	Ref<Texture2D> out = decodeTexture(name, missing);
	if (!missing.empty())
	{
		m_missing.push_back(missing);
	}
	m_textures[name] = out;
	return out;
}

int64_t FXPlayer::preload_textures(double budget_ms)
{
	if (!m_playback || m_fs.is_null() || !m_fs->archive())
	{
		return 0;
	}
	if (!m_preloadQueued)
	{
		// every ParticleName of the particle system templates, in definition order, once
		m_preloadQueued = true;
		const FXParticleSystemTemplateStore &store = m_playback->particles().templates();
		std::set<std::string> seen;
		for (const std::string &n : store.templateNames())
		{
			const ParticleSystemTemplate *t = store.findTemplate(n);
			const std::string &tex = t ? t->info().m_particleTypeName : std::string();
			if (!tex.empty() && seen.insert(tex).second)
			{
				m_preloadQueue.push_back(tex);
			}
		}
	}
	const auto t0 = std::chrono::steady_clock::now();
	while (m_preloadNext < m_preloadQueue.size() && (budget_ms <= 0.0 || usSince(t0) < budget_ms * 1000.0))
	{
		const std::string &name = m_preloadQueue[m_preloadNext++];
		if (m_textures.count(name) || m_preloaded.count(name))
		{
			continue;
		}
		Preloaded p;
		p.texture = decodeTexture(name, p.missing);
		m_preloaded.emplace(name, std::move(p));
	}
	return (int64_t)(m_preloadQueue.size() - m_preloadNext);
}

Ref<Texture2D> FXPlayer::decodeTexture(const std::string &name, std::string &missing)
{
	Ref<Texture2D> out;
	// lane SDK-1: an asset pack's replacement of the particle texture first (GameClient/AssetPacks.h); a broken one is a miss, reported
	std::string packError;
	Ref<Image> packImage = Load_Asset_Pack_Image(m_fs->assetPacks().empty() ? nullptr : &m_fs->assetPacks(), name, packError);
	if (packImage.is_valid())
	{
		if (!packImage->has_mipmaps() && !packImage->is_compressed())
		{
			packImage->generate_mipmaps();
		}
		return ImageTexture::create_from_image(packImage);
	}
	if (!packError.empty())
	{
		missing = name + " (" + packError + ")";
		Ref<Image> white = Image::create(1, 1, false, Image::FORMAT_RGBA8);
		white->fill(Color(1, 1, 1, 1));
		return ImageTexture::create_from_image(white);
	}
	ArchiveFileSystem &fs = *m_fs->archive();
	const ParticleTextureResolution res = ResolveParticleTexture(name, [&](const std::string &p) { return fs.doesFileExist(p); });
	if (!res.Found)
	{
		missing = name;
	}
	else
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!fs.readFile(res.Path, bytes, &error))
		{
			missing = name + " (" + error + ")";
		}
		else
		{
			PackedByteArray buffer;
			buffer.resize((int64_t)bytes.size());
			if (!bytes.empty())
			{
				std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
			}
			Ref<Image> image;
			image.instantiate();
			Error err = FAILED;
			switch (res.Kind)
			{
			case ParticleTextureResolution::DDS: err = image->load_dds_from_buffer(buffer); break;
			case ParticleTextureResolution::TGA: err = image->load_tga_from_buffer(buffer); break;
			case ParticleTextureResolution::JPG: err = image->load_jpg_from_buffer(buffer); break;
			default: break;
			}
			if (err != OK || image->is_empty())
			{
				missing = name + " (could not decode " + res.Path + ")";
			}
			else
			{
				if (!image->has_mipmaps() && !image->is_compressed())
				{
					image->generate_mipmaps();
				}
				out = ImageTexture::create_from_image(image);
			}
		}
	}
	if (out.is_null())
	{
		// retail draws its default texture when a name does not resolve (RW 0x532962): a 1x1 white; the miss is reported in get_stats
		Ref<Image> white = Image::create(1, 1, false, Image::FORMAT_RGBA8);
		white->fill(Color(1, 1, 1, 1));
		out = ImageTexture::create_from_image(white);
	}
	return out;
}

Ref<Shader> FXPlayer::shaderFor(const std::string &key, const std::string &code)
{
	auto it = m_shaders.find(key);
	if (it != m_shaders.end())
	{
		return it->second;
	}
	W3D_Ensure_Light_Globals(); // lane RENDER-4: the particle shaders name the fog globals (w3d_fog / w3d_fog_color): they must exist before they compile
	Ref<Shader> s;
	s.instantiate();
	SetShaderCode(s.ptr(), toGodot(code), String("FX ") + toGodot(key));
	m_shaders[key] = s;
	return s;
}

FXPlayer::Batch &FXPlayer::batchSlot(size_t index, const std::string &key, int kind)
{
	if (index >= m_batches.size())
	{
		m_batches.resize(index + 1);
	}
	Batch &b = m_batches[index];
	if (b.kind != kind || b.key != key)
	{
		// the slot changes identity: drop its nodes and rebuild them for the new key
		if (b.meshNode)
		{
			b.meshNode->queue_free();
			b.meshNode = nullptr;
		}
		if (b.multiNode)
		{
			b.multiNode->queue_free();
			b.multiNode = nullptr;
		}
		b.multi.unref();
		b.mesh.unref();
		b.material.unref();
		b.kind = kind;
		b.key = key;
		b.lastBuffer = PackedFloat32Array();
		b.haveGpuParams = false;
		b.lastTexture.unref();
		b.haveOrder = false;
	}
	return b;
}

// lane RENDER-4: the per-batch summary (see set_batch_summary); presentation diagnostics only
void FXPlayer::summarize(const std::vector<DrawBatch> &batches)
{
	m_summary.clear();
	for (const DrawBatch &b : batches)
	{
		Dictionary e;
		e["template"] = toGodot(b.templateName);
		e["kind"] = b.kind == DrawBatch::SPRITES ? "sprites" : (b.kind == DrawBatch::GPU ? "gpu" : "mesh");
		e["shader"] = (int64_t)b.shader;
		e["texture"] = toGodot(b.texture);
		e["deferred"] = b.deferred;
		e["ground_aligned"] = b.groundAligned;
		double cx = 0, cy = 0, cz = 0, size = 0, maxSize = 0, r = 0, g = 0, bl = 0, a = 0, maxA = 0;
		size_t n = 0;
		if (b.kind == DrawBatch::SPRITES)
		{
			for (const SpriteInstance &s : b.sprites)
			{
				cx += s.x; cy += s.y; cz += s.z;
				size += s.size; maxSize = std::max(maxSize, (double)s.size);
				r += s.r; g += s.g; bl += s.b; a += s.a; maxA = std::max(maxA, (double)s.a);
				++n;
			}
		}
		else if (b.kind == DrawBatch::GPU)
		{
			for (const GpuInstance &s : b.gpu)
			{
				cx += s.x; cy += s.y; cz += s.z;
				++n;
			}
		}
		else
		{
			for (const MeshVertex &v : b.vertices)
			{
				cx += v.x; cy += v.y; cz += v.z;
				r += v.r; g += v.g; bl += v.b; a += v.a; maxA = std::max(maxA, (double)v.a);
				++n;
			}
		}
		e["count"] = (int64_t)n;
		const double inv = n ? 1.0 / (double)n : 0.0;
		e["centre"] = Vector3((real_t)(cx * inv), (real_t)(cy * inv), (real_t)(cz * inv));
		e["mean_size"] = size * inv;
		e["max_size"] = maxSize;
		e["mean_color"] = Color((float)(r * inv), (float)(g * inv), (float)(bl * inv), (float)(a * inv));
		e["max_alpha"] = maxA;
		m_summary.push_back(e);
	}
}

void FXPlayer::rebuild()
{
	if (!m_playback || !m_builder)
	{
		return;
	}
	DrawCamera cam;
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			const Transform3D t = c->get_global_transform();
			cam.position = toSage(t.origin);
			cam.right = toSage(t.basis.get_column(0));
			cam.up = toSage(t.basis.get_column(1));
			cam.forward = toSage(-t.basis.get_column(2));
		}
	}
	// lane GPUPERF-3: the systems no camera can see are not built (ParticleDrawBuilder::setVisibilityTest); the boxes are SAGE space, the planes Godot's
	const std::vector<Plane> planes = m_frustumCulling ? cullPlanes() : std::vector<Plane>();
	if (planes.size() >= 6)
	{
		m_builder->setVisibilityTest([planes](const float lo[3], const float hi[3]) {
			// SAGE (x, y, z) -> Godot (x, z, -y)
			const Vector3 gmin(lo[0], lo[2], -hi[1]), gmax(hi[0], hi[2], -lo[1]);
			const Vector3 c = (gmin + gmax) * 0.5f, e = (gmax - gmin) * 0.5f;
			auto inside = [&](size_t first, size_t last) {
				for (size_t k = first; k < last; ++k)
				{
					const Plane &pl = planes[k];
					const real_t r = std::abs(pl.normal.x) * e.x + std::abs(pl.normal.y) * e.y + std::abs(pl.normal.z) * e.z;
					if (pl.distance_to(c) > r)
					{
						return false;
					}
				}
				return true;
			};
			return inside(0, 6) || (planes.size() >= 12 && inside(6, 12));
		});
	}
	else
	{
		m_builder->setVisibilityTest(nullptr);
	}
	m_builtPlanes = planes;
	m_builtSignature = playbackSignature();
	m_builtCamera = Transform3D();
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			m_builtCamera = c->get_global_transform();
		}
	}
	m_built = true;
	m_buildDirty = false;
	const auto t0 = std::chrono::steady_clock::now();
	std::vector<DrawBatch> batches = m_builder->build(m_playback->particles(), cam);
	m_emitterStops = BuildEmitterBatches(m_playback->emitters(), cam, batches);
	m_buildUs = usSince(t0);
	if (m_summaryEnabled)
	{
		summarize(batches);
	}
	const auto t1 = std::chrono::steady_clock::now();
	upload(batches);
	m_uploadUs = usSince(t1);
}

// lane PERF-1: upload() compares GpuDrawParams bytewise; that is only the parameters' equality while the struct is 53 four-byte members without padding
static_assert(sizeof(FXParticleSystem::GpuDrawParams) == 53 * 4 && std::is_trivially_copyable<FXParticleSystem::GpuDrawParams>::value,
	"GpuDrawParams changed: review the bytewise comparison in FXPlayer::upload");

void FXPlayer::setBufferIfChanged(Batch &b, const PackedFloat32Array &buf)
{
	if (b.lastBuffer.size() == buf.size() && (buf.is_empty() || std::memcmp(b.lastBuffer.ptr(), buf.ptr(), (size_t)buf.size() * sizeof(float)) == 0))
	{
		return;
	}
	b.multi->set_buffer(buf);
	b.lastBuffer = buf;
}

void FXPlayer::applyOrder(Batch &b, const BatchOrder &order)
{
	if (b.haveOrder && b.lastOrder.priority == order.priority && std::memcmp(&b.lastOrder.sortOffset, &order.sortOffset, sizeof(float)) == 0)
	{
		return;
	}
	b.haveOrder = true;
	b.lastOrder = order;
	b.material->set_render_priority(order.priority);
	if (b.multiNode)
	{
		b.multiNode->set_sorting_offset(order.sortOffset);
	}
	if (b.meshNode)
	{
		b.meshNode->set_sorting_offset(order.sortOffset);
	}
}

void FXPlayer::upload(const std::vector<DrawBatch> &batches)
{
	m_sprites = m_gpu = m_vertices = 0;
	m_batchCount = batches.size();
	// Compositing order. The builder's batch order is the draw order (list order, then the deferred SortLevel 1 systems in reverse); alpha and additive
	// blends do not commute, so only CONSECUTIVE compatible sprite batches are merged into one MultiMesh (a smoke / fire / smoke sequence stays three
	// nodes). The deferred flag is part of the key and the deferred batches keep their particle order.
	struct Merged
	{
		std::string key;
		int kind;
		int shader;
		bool ground;
		bool volume;
		bool deferred;
		std::string texture;
		std::vector<const DrawBatch *> parts;
	};
	std::vector<Merged> merged;
	// lane GPUPERF-3: a culled batch (nothing of it in view) takes no slot, but it still separates its neighbours: two batches a drawn one stood
	// between are not merged, so the depth-sorted ones keep their order
	bool separated = false;
	m_culledBatches = 0;
	for (const DrawBatch &b : batches)
	{
		if (b.culled)
		{
			separated = true;
			++m_culledBatches;
			continue;
		}
		const bool joinable = !separated;
		separated = false;
		if (b.kind == DrawBatch::SPRITES)
		{
			const std::string key = "S|" + b.texture + "|" + std::to_string(b.shader) + "|" + (b.groundAligned ? "g" : "b") + (b.volume ? "v" : "") + "|" + (b.deferred ? "d" : "n");
			if (joinable && !merged.empty() && merged.back().key == key)
			{
				merged.back().parts.push_back(&b);
			}
			else
			{
				merged.push_back({ key, DrawBatch::SPRITES, b.shader, b.groundAligned, b.volume, b.deferred, b.texture, { &b } });
			}
		}
		else
		{
			merged.push_back({ "X|" + std::to_string(b.system) + "|" + std::to_string((int)b.kind) + "|" + std::to_string(b.shader), (int)b.kind, b.shader, false, false, b.deferred, b.texture, { &b } });
		}
	}
	m_sortedBatchSeen = false;
	Transform3D camXf;
	bool haveCam = false;
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			camXf = c->get_global_transform();
			haveCam = true;
		}
	}
	size_t slot = 0;
	for (const Merged &m : merged)
	{
		const BatchOrder order = OrderForBatch(slot, merged.size());
		Batch &slotRef = batchSlot(slot++, m.key, m.kind);
		Batch *bp = &slotRef;
		const Ref<Texture2D> tex = loadTexture(m.texture);
		if (m.kind == DrawBatch::SPRITES)
		{
			if (!bp->multiNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("sprite|" + std::to_string(m.shader) + "|" + (m.ground ? "g" : "b") + (m_soft ? "|soft" : ""), FXShaderGen::SpriteShader(m.shader, m.ground, m_soft)));
				bp->material->set_shader_parameter("tex", tex);
				bp->multi.instantiate();
				bp->multi->set_transform_format(MultiMesh::TRANSFORM_3D);
				bp->multi->set_use_colors(true);
				bp->multi->set_use_custom_data(true);
				bp->multi->set_mesh(m_quad);
				bp->multiNode = memnew(MultiMeshInstance3D);
				bp->multiNode->set_multimesh(bp->multi);
				bp->multiNode->set_material_override(bp->material);
				bp->multiNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(bp->multiNode);
				bp->haveAabb = false;
			}
			applyOrder(*bp, order);
			// gather: VOLUME_PARTICLE re-submits the set in 6 layers shifted toward the camera (RW 0x57efb0, depth 6, 0.1 / depth)
			std::vector<const SpriteInstance *> all;
			for (const DrawBatch *d : m.parts)
			{
				for (const SpriteInstance &s : d->sprites)
				{
					all.push_back(&s);
				}
			}
			const int layers = m.volume ? 6 : 1;
			const bool sortBack = SpriteBatchDepthSorted(m.shader, m.deferred);
			if (sortBack && haveCam)
			{
				m_sortedBatchSeen = true;
				const Vector3 cp = camXf.origin;
				std::stable_sort(all.begin(), all.end(), [&](const SpriteInstance *a, const SpriteInstance *b) {
					const Vector3 pa = toGodotSpace(Coord3D{ a->x, a->y, a->z }), pb = toGodotSpace(Coord3D{ b->x, b->y, b->z });
					return (pa - cp).length_squared() > (pb - cp).length_squared();
				});
			}
			const size_t count = all.size() * (size_t)layers;
			if ((size_t)bp->multi->get_instance_count() != count)
			{
				bp->multi->set_instance_count((int)count);
			}
			PackedFloat32Array buf;
			buf.resize((int64_t)(count * 20));
			float *w = buf.ptrw();
			size_t n = 0;
			for (int layer = 0; layer < layers; ++layer)
			{
				for (const SpriteInstance *s : all)
				{
					Vector3 p = toGodotSpace(Coord3D{ s->x, s->y, s->z });
					if (layer > 0 && haveCam)
					{
						const Vector3 toCam = (camXf.origin - p).normalized();
						p += toCam * ((float)layer * all.front()->size * (0.1f / 6.0f));
					}
					float *o = w + n * 20;
					o[0] = 1; o[1] = 0; o[2] = 0; o[3] = p.x;
					o[4] = 0; o[5] = 1; o[6] = 0; o[7] = p.y;
					o[8] = 0; o[9] = 0; o[10] = 1; o[11] = p.z;
					o[12] = s->r; o[13] = s->g; o[14] = s->b; o[15] = s->a;
					o[16] = s->size; o[17] = (float)s->angleByte; o[18] = 0; o[19] = 0;
					++n;
				}
			}
			setBufferIfChanged(*bp, buf);
			// lane GPUPERF-3: the batch's box (Godot culls it per camera): the centres written, grown by 3 x the largest size (the billboard corners
			// reach size / sqrt(2), the ground-aligned ones sqrt(2) x size)
			{
				AABB box;
				float big = 0.0f;
				for (size_t i = 0; i < n; ++i)
				{
					const float *o = w + i * 20;
					const AABB pt(Vector3(o[3], o[7], o[11]), Vector3());
					box = i == 0 ? pt : box.merge(pt);
					big = std::max(big, std::fabs(o[16]));
				}
				box = box.grow(3.0f * big + 1.0f);
				setNodeAabb(*bp, bp->multiNode, box);
			}
			m_sprites += count;
		}
		else if (m.kind == DrawBatch::GPU)
		{
			const DrawBatch &d = *m.parts[0];
			if (!bp->multiNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("gpu|" + std::to_string(m.shader) + (m_soft ? "|soft" : ""), FXShaderGen::GpuShader(d.gpuParams.shaderType, m_soft)));
				bp->multi.instantiate();
				bp->multi->set_transform_format(MultiMesh::TRANSFORM_3D);
				bp->multi->set_use_colors(true);
				bp->multi->set_use_custom_data(true);
				// the GPU quad: positions unused, UV2.x = corner index 0..3, UV = unused
				if (m_gpuQuad.is_null())
				{
					PackedVector3Array v;
					PackedVector2Array uv2;
					for (int k = 0; k < 4; ++k)
					{
						v.push_back(Vector3(0, 0, 0));
						uv2.push_back(Vector2((float)k, 0.0f));
					}
					PackedInt32Array idx;
					const int ids[6] = { 0, 1, 2, 0, 2, 3 }; // RW index buffer (b, b+1, b+2), (b, b+2, b+3)
					for (int i : ids)
					{
						idx.push_back(i);
					}
					Array arrays;
					arrays.resize(Mesh::ARRAY_MAX);
					arrays[Mesh::ARRAY_VERTEX] = v;
					arrays[Mesh::ARRAY_TEX_UV2] = uv2;
					arrays[Mesh::ARRAY_INDEX] = idx;
					m_gpuQuad.instantiate();
					m_gpuQuad->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
					m_gpuQuad->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
				}
				bp->multi->set_mesh(m_gpuQuad);
				bp->multiNode = memnew(MultiMeshInstance3D);
				bp->multiNode->set_multimesh(bp->multi);
				bp->multiNode->set_material_override(bp->material);
				bp->multiNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				bp->multiNode->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
				add_child(bp->multiNode);
			}
			applyOrder(*bp, order);
			const GpuDrawParams &gp = d.gpuParams;
			Material *mat = bp->material.ptr();
			// lane PERF-1: GpuDrawParams is all 4-byte floats and ints (no padding): equal bytes are the same parameters, which the material already has
			if (bp->lastTexture != tex)
			{
				bp->material->set_shader_parameter("tex", tex);
				bp->lastTexture = tex;
			}
			if (!bp->haveGpuParams || std::memcmp(&bp->lastGpuParams, &gp, sizeof(GpuDrawParams)) != 0)
			{
				bp->haveGpuParams = true;
				bp->lastGpuParams = gp;
				bp->material->set_shader_parameter("tex_layout", Vector2(gp.framesPerRow, gp.totalFrames));
				bp->material->set_shader_parameter("speed_mult", gp.speedMultiplier);
				Array ck;
				for (int i = 0; i < 4; ++i)
				{
					ck.push_back(Color(gp.colorKeys[i][0], gp.colorKeys[i][1], gp.colorKeys[i][2], gp.colorKeys[i][3]));
				}
				bp->material->set_shader_parameter("color_keys", ck);
				bp->material->set_shader_parameter("time_keys", Color(gp.timeKeys[0], gp.timeKeys[1], gp.timeKeys[2], gp.timeKeys[3]));
				bp->material->set_shader_parameter("color_scale", Vector2(gp.colorScale[0], gp.colorScale[1]));
				bp->material->set_shader_parameter("gravity", Vector3(gp.gravity[0], gp.gravity[1], gp.gravity[2]));
				bp->material->set_shader_parameter("drift", Vector3(gp.drift[0], gp.drift[1], gp.drift[2]));
				bp->material->set_shader_parameter("vel_damp", Vector2(gp.velocityDamping[0], gp.velocityDamping[1]));
				bp->material->set_shader_parameter("size_u", Vector2(gp.size[0], gp.size[1]));
				bp->material->set_shader_parameter("size_rate", Vector2(gp.sizeRate[0], gp.sizeRate[1]));
				bp->material->set_shader_parameter("size_rate_damp", Vector2(gp.sizeRateDamping[0], gp.sizeRateDamping[1]));
				bp->material->set_shader_parameter("xy_rot", Vector2(gp.xyRotation[0], gp.xyRotation[1]));
				bp->material->set_shader_parameter("xy_rot_rate", Vector2(gp.xyRotationRate[0], gp.xyRotationRate[1]));
				bp->material->set_shader_parameter("xy_rot_damp", Vector2(gp.xyRotationDamping[0], gp.xyRotationDamping[1]));
				bp->material->set_shader_parameter("z_rot", Vector2(gp.zRotation[0], gp.zRotation[1]));
				bp->material->set_shader_parameter("z_rot_rate", Vector2(gp.zRotationRate[0], gp.zRotationRate[1]));
				bp->material->set_shader_parameter("z_rot_damp", Vector2(gp.zRotationDamping[0], gp.zRotationDamping[1]));
				bp->material->set_shader_parameter("now", gp.now);
			}
			(void)mat;
			const size_t count = d.gpu.size();
			if ((size_t)bp->multi->get_instance_count() != count)
			{
				bp->multi->set_instance_count((int)count);
			}
			PackedFloat32Array buf;
			buf.resize((int64_t)(count * 20));
			float *w = buf.ptrw();
			for (size_t i = 0; i < count; ++i)
			{
				const GpuInstance &g = d.gpu[i];
				const Vector3 p = toGodotSpace(Coord3D{ g.x, g.y, g.z });
				// the shader works in SAGE space for the kinematics: the transform origin carries the SAGE position, converted there
				float *o = w + i * 20;
				o[0] = 1; o[1] = 0; o[2] = 0; o[3] = g.x;
				o[4] = 0; o[5] = 1; o[6] = 0; o[7] = g.y;
				o[8] = 0; o[9] = 0; o[10] = 1; o[11] = g.z;
				o[12] = g.life; o[13] = g.random; o[14] = 0; o[15] = 0;
				o[16] = g.vx; o[17] = g.vy; o[18] = g.vz; o[19] = g.birth;
				(void)p;
			}
			setBufferIfChanged(*bp, buf);
			m_gpu += count;
		}
		else
		{
			const DrawBatch &d = *m.parts[0];
			if (!bp->meshNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("mesh|" + std::to_string(m.shader) + (m_soft ? "|soft" : ""), FXShaderGen::MeshShader(m.shader, m_soft)));
				bp->meshNode = memnew(MeshInstance3D);
				bp->meshNode->set_material_override(bp->material);
				bp->meshNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(bp->meshNode);
			}
			if (bp->lastTexture != tex)
			{
				bp->material->set_shader_parameter("tex", tex);
				bp->lastTexture = tex;
			}
			applyOrder(*bp, order);
			// lane GPUPERF-3: the same geometry as the last build keeps its mesh (no new ArrayMesh, nothing uploaded)
			const bool same = bp->meshNode->get_mesh().is_valid() && d.vertices.size() == bp->lastVertices.size() && d.indices == bp->lastIndices &&
				(d.vertices.empty() || std::memcmp(d.vertices.data(), bp->lastVertices.data(), d.vertices.size() * sizeof(MeshVertex)) == 0);
			if (same)
			{
				m_vertices += d.vertices.size();
				continue;
			}
			bp->lastVertices = d.vertices;
			bp->lastIndices = d.indices;
			PackedVector3Array v;
			PackedVector2Array uv;
			PackedColorArray col;
			PackedInt32Array idx;
			v.resize((int64_t)d.vertices.size());
			uv.resize((int64_t)d.vertices.size());
			col.resize((int64_t)d.vertices.size());
			for (size_t i = 0; i < d.vertices.size(); ++i)
			{
				const MeshVertex &mv = d.vertices[i];
				v.set((int64_t)i, toGodotSpace(Coord3D{ mv.x, mv.y, mv.z }));
				uv.set((int64_t)i, Vector2(mv.u, mv.v));
				col.set((int64_t)i, Color(mv.r, mv.g, mv.b, mv.a));
			}
			idx.resize((int64_t)d.indices.size());
			for (size_t i = 0; i < d.indices.size(); ++i)
			{
				idx.set((int64_t)i, (int32_t)d.indices[i]);
			}
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = v;
			arrays[Mesh::ARRAY_TEX_UV] = uv;
			arrays[Mesh::ARRAY_COLOR] = col;
			arrays[Mesh::ARRAY_INDEX] = idx;
			Ref<ArrayMesh> am;
			am.instantiate();
			am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			// lane GPUPERF-3: the vertices' own box (they are world positions), so Godot culls the batch per camera
			AABB box;
			for (int64_t i = 0; i < v.size(); ++i)
			{
				const AABB pt(v[i], Vector3());
				box = i == 0 ? pt : box.merge(pt);
			}
			am->set_custom_aabb(box.grow(1.0f));
			bp->meshNode->set_mesh(am);
			m_vertices += d.vertices.size();
		}
	}
	// hide / free the unused slots
	for (size_t i = slot; i < m_batches.size(); ++i)
	{
		Batch &b = m_batches[i];
		if (b.multiNode)
		{
			b.multiNode->queue_free();
			b.multiNode = nullptr;
		}
		if (b.meshNode)
		{
			b.meshNode->queue_free();
			b.meshNode = nullptr;
		}
		b.key.clear();
		b.kind = -1;
	}
	if (slot < m_batches.size())
	{
		m_batches.resize(slot);
	}
}

Dictionary FXPlayer::get_stats() const
{
	Dictionary d;
	if (m_playback)
	{
		d["frame"] = (int64_t)m_playback->clientFrame();
		d["systems"] = (int64_t)m_playback->particles().systemCount();
		d["particles"] = (int64_t)m_playback->particles().particleCount();
		d["emitters"] = (int64_t)m_playback->emitters().emitters().size();
		d["max_particles"] = (int64_t)m_playback->particles().settings().maxParticleCount; // lane OPTS-2: the detail level's limits
		d["min_particle_priority"] = (int64_t)m_playback->particles().settings().minDynamicParticlePriority;
	}
	d["batches"] = (int64_t)m_batchCount;
	d["textures_preload_queue"] = (int64_t)m_preloadQueue.size(); // lane PERF-1 r2: particle textures to preload, decoded ahead and not used yet
	d["textures_preloaded_unused"] = (int64_t)m_preloaded.size();
	d["sprites"] = (int64_t)m_sprites;
	d["gpu_instances"] = (int64_t)m_gpu;
	d["mesh_vertices"] = (int64_t)m_vertices;
	d["step_us"] = m_stepUs;
	d["build_us"] = m_buildUs;
	d["upload_us"] = m_uploadUs;
	PackedStringArray missing;
	for (const std::string &m : m_missing)
	{
		missing.push_back(toGodot(m));
	}
	d["missing_textures"] = missing;
	d["soft_particles"] = m_soft; // lane FX-3 round 2
	d["culled_batches"] = (int64_t)m_culledBatches;  // lane GPUPERF-3: systems of the last build not built (out of every camera's view)
	d["rebuilds_skipped"] = m_rebuildsSkipped;       // advances that kept the batches (no step, no change)
	return d;
}

PackedStringArray FXPlayer::get_unverified() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &s : m_playback->unverified())
		{
			out.push_back(toGodot(s));
		}
	}
	if (m_builder)
	{
		for (const std::string &s : m_builder->unverified())
		{
			out.push_back(toGodot(s));
		}
	}
	for (const std::string &s : m_emitterStops)
	{
		out.push_back(toGodot(s));
	}
	if (m_playback)
	{
		out.push_back(toGodot(std::string("S-198: ") + FogOmissionText()));
		// lane RENDER-3: the gamma-space compositing of the transparent pass and its inferred remainder (S-831)
		out.append_array(GammaComposite::unverified());
		if (m_sortedBatchSeen)
		{
			out.push_back(toGodot(std::string("S-198: ") + SortApproximationText()));
		}
	}
	return out;
}

Array FXPlayer::get_events() const
{
	Array out;
	if (m_playback)
	{
		for (const FXPlayback::Event &e : m_playback->events())
		{
			Dictionary d;
			d["kind"] = toGodot(e.kind);
			d["detail"] = toGodot(e.detail);
			d["frame"] = (int64_t)e.frame;
			out.push_back(d);
		}
	}
	return out;
}

void FXPlayer::clearTextureCache()
{
	m_buildDirty = true;
	m_textures.clear();
	m_preloaded.clear();
	m_preloadQueue.clear();
	m_preloadNext = 0;
	m_preloadQueued = false;
}

} // namespace godot
