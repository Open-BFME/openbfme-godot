// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotW3DInstancer.h"

#include "Common/JobSystem.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WW3D2/w3dsort.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cmath>
#include <cstring>

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

double msSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const char *kindName(RenderSubObject::Kind k)
{
	switch (k)
	{
	case RenderSubObject::SUB_MESH: return "mesh";
	case RenderSubObject::SUB_BOX: return "box";
	case RenderSubObject::SUB_EMITTER: return "emitter";
	default: return "unresolved";
	}
}

} // namespace

W3DInstancer::W3DInstancer()
{
	Epoch = std::chrono::steady_clock::now();
	unsigned hw = std::thread::hardware_concurrency();
	Workers = hw >= 8 ? 4 : (hw >= 4 ? 2 : 1);
}

W3DInstancer::~W3DInstancer() = default;

void W3DInstancer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &W3DInstancer::setup);
	ClassDB::bind_method(D_METHOD("add_model", "model_name"), &W3DInstancer::add_model);
	ClassDB::bind_method(D_METHOD("get_model_report", "model"), &W3DInstancer::get_model_report);
	ClassDB::bind_method(D_METHOD("add_instance", "model", "transform", "clip", "start_time", "speed"), &W3DInstancer::add_instance, DEFVAL(String()), DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("set_instance_transform", "instance", "transform"), &W3DInstancer::set_instance_transform);
	ClassDB::bind_method(D_METHOD("remove_instance", "instance"), &W3DInstancer::remove_instance);
	ClassDB::bind_method(D_METHOD("set_instance_clip", "instance", "clip", "start_time", "speed"), &W3DInstancer::set_instance_clip, DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("set_instance_pose", "instance", "clip0", "frame0", "clip1", "frame1", "percentage"), &W3DInstancer::set_instance_pose, DEFVAL(String()), DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_instance_count"), &W3DInstancer::get_instance_count);
	ClassDB::bind_method(D_METHOD("get_clip_info", "model", "clip"), &W3DInstancer::get_clip_info);
	ClassDB::bind_method(D_METHOD("set_global_time", "seconds"), &W3DInstancer::set_global_time);
	ClassDB::bind_method(D_METHOD("get_global_time"), &W3DInstancer::get_global_time);
	ClassDB::bind_method(D_METHOD("set_playing", "playing"), &W3DInstancer::set_playing);
	ClassDB::bind_method(D_METHOD("is_playing"), &W3DInstancer::is_playing);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &W3DInstancer::set_time_scale);
	ClassDB::bind_method(D_METHOD("set_worker_threads", "count"), &W3DInstancer::set_worker_threads);
	ClassDB::bind_method(D_METHOD("get_worker_threads"), &W3DInstancer::get_worker_threads);
	ClassDB::bind_method(D_METHOD("update_now"), &W3DInstancer::update_now);
	ClassDB::bind_method(D_METHOD("update_mappers"), &W3DInstancer::update_mappers);
	ClassDB::bind_method(D_METHOD("get_mapper_state"), &W3DInstancer::get_mapper_state);
	ClassDB::bind_method(D_METHOD("get_bone_position", "instance", "bone"), &W3DInstancer::get_bone_position);
	ClassDB::bind_method(D_METHOD("get_stats"), &W3DInstancer::get_stats);
	ClassDB::bind_method(D_METHOD("get_errors"), &W3DInstancer::get_errors);
	ClassDB::bind_method(D_METHOD("get_warnings"), &W3DInstancer::get_warnings);
	ClassDB::bind_method(D_METHOD("set_sort_camera", "transform"), &W3DInstancer::set_sort_camera);
	ClassDB::bind_method(D_METHOD("get_draw_order", "model", "draw"), &W3DInstancer::get_draw_order);
	ClassDB::bind_method(D_METHOD("set_instance_hidden_subobjects", "instance", "names"), &W3DInstancer::set_instance_hidden_subobjects);
	ClassDB::bind_method(D_METHOD("set_auto_update", "enabled"), &W3DInstancer::set_auto_update);
	ClassDB::bind_method(D_METHOD("set_house_colors_enabled", "enabled"), &W3DInstancer::set_house_colors_enabled);
	ClassDB::bind_method(D_METHOD("get_house_colors_enabled"), &W3DInstancer::get_house_colors_enabled);
	ClassDB::bind_method(D_METHOD("set_instance_house_color", "instance", "color"), &W3DInstancer::set_instance_house_color);
	ClassDB::bind_method(D_METHOD("set_instance_infantry_light", "instance", "infantry"), &W3DInstancer::set_instance_infantry_light);
	ClassDB::bind_method(D_METHOD("set_instance_opacity", "instance", "opacity"), &W3DInstancer::set_instance_opacity);
	ClassDB::bind_method(D_METHOD("get_auto_update"), &W3DInstancer::get_auto_update);
	ClassDB::bind_method(D_METHOD("probe_shaders", "origin"), &W3DInstancer::probe_shaders);
}

void W3DInstancer::_ready()
{
	set_process(AutoUpdate);
}

void W3DInstancer::set_house_colors_enabled(bool enabled)
{
	HouseColorEnabled = enabled;
	if (Materials)
	{
		Materials->Set_House_Colors(enabled && HouseColorsLoaded ? &HouseColors : nullptr);
	}
}

void W3DInstancer::set_instance_house_color(int64_t instance, const Color &color)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	Instance &inst = Instances[(size_t)instance];
	inst.HcPacked = color.a < 0.5f ? 0.0f : W3D_Pack_House_Color((int)std::lround(color.r * 255.0f), (int)std::lround(color.g * 255.0f), (int)std::lround(color.b * 255.0f));
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_infantry_light(int64_t instance, bool infantry)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	Instances[(size_t)instance].InfantryLight = infantry;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_opacity(int64_t instance, double opacity)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	const float o = opacity < 0.0 ? 0.0f : (opacity > 1.0 ? 1.0f : (float)opacity);
	if (Instances[(size_t)instance].Opacity == o) return;
	Instances[(size_t)instance].Opacity = o;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_auto_update(bool enabled)
{
	AutoUpdate = enabled;
	set_process(enabled && is_inside_tree());
}

// ---- worker pool -----------------------------------------------------------------------------------------------------

// lane PERF-2: the slices run on the process's client job pool (JobSystem::client()) instead of a pool of threads per instancer (two instancers and the
// client pool were three sets of threads competing for the cores). `Workers` still bounds the parallelism: the work is cut into at most Workers slices,
// so at most Workers threads run them at once; each slice writes only its own instances / draw items.
void W3DInstancer::set_worker_threads(int count)
{
	count = std::max(1, std::min(count, 32));
	Workers = count;
}

void W3DInstancer::run_parallel(size_t count, const std::function<void(size_t, size_t)> &fn)
{
	if (Workers <= 1 || count < 64)
	{
		fn(0, count);
		return;
	}
	const size_t slice = (count + (size_t)Workers - 1) / (size_t)Workers;
	JobSystem::client().parallelFor(count, slice, [&fn](size_t, size_t begin, size_t end) { fn(begin, end); });
}

// ---- setup / models --------------------------------------------------------------------------------------------------

Dictionary W3DInstancer::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	PackedStringArray errors;
	if (fs.is_null() || !fs->is_mounted() || fs->archive_fs() == nullptr)
	{
		errors.push_back("retail file system is not mounted");
	}
	else
	{
		FsRef = fs;
		Source.reset(new ArchiveW3DFileSource(*fs->archive_fs()));
		Assets.reset(new WW3DAssetManager(*Source));
		Materials.reset(new W3DMaterialFactory(*fs->archive_fs()));
		// housecolor.ini: the table only names which textures have a house texture; applying it is stop S-022 (reported per surface).
		// A missing or malformed file is an error, not an empty table.
		std::vector<std::uint8_t> hcBytes;
		std::string hcError;
		HouseColors = HouseColorTable();
		HouseColorsLoaded = false;
		if (!Source->Read("data\\ini\\housecolor.ini", hcBytes, &hcError))
		{
			errors.push_back(toGodot("housecolor.ini: " + hcError));
		}
		else if (!HouseColors.Parse(std::string(hcBytes.begin(), hcBytes.end()), &hcError))
		{
			errors.push_back(toGodot("housecolor.ini: " + hcError));
		}
		else
		{
			HouseColorsLoaded = true;
		}
	}
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	return result;
}

std::shared_ptr<W3DInstancer::MeshGpu> W3DInstancer::build_mesh(const MeshModelClass &mesh, std::vector<std::string> &errors)
{
	const std::string key = AsciiStringUtil::lowered(mesh.Get_Name());
	auto cached = MeshCache.find(key);
	if (cached != MeshCache.end())
	{
		return cached->second;
	}
	std::shared_ptr<MeshGpu> gpu = std::make_shared<MeshGpu>();
	std::string error;
	if (!Build_Mesh_Render_Data(mesh, gpu->Data, &error))
	{
		errors.push_back(error);
		MeshCache[key] = nullptr;
		return nullptr;
	}
	const MeshRenderData &d = gpu->Data;
	const int64_t nv = (int64_t)d.NumVertices;
	gpu->Mesh.instantiate();
	W3D_Collect_Mesh_Stops(d, gpu->Stops);

	PackedVector3Array positions, normals;
	positions.resize(nv);
	normals.resize(nv);
	for (int64_t v = 0; v < nv; ++v)
	{
		positions.set(v, Vector3(d.Position[v * 3], d.Position[v * 3 + 1], d.Position[v * 3 + 2]));
		normals.set(v, Vector3(d.Normal[v * 3], d.Normal[v * 3 + 1], d.Normal[v * 3 + 2]));
	}
	PackedFloat32Array tangents;
	if (!d.Tangent.empty())
	{
		tangents.resize(nv * 4);
		for (int64_t v = 0; v < nv; ++v)
		{
			Vector3 n(d.Normal[v * 3], d.Normal[v * 3 + 1], d.Normal[v * 3 + 2]);
			Vector3 t(d.Tangent[v * 3], d.Tangent[v * 3 + 1], d.Tangent[v * 3 + 2]);
			Vector3 b(d.Bitangent[v * 3], d.Bitangent[v * 3 + 1], d.Bitangent[v * 3 + 2]);
			// V is flipped on load (u, 1 - v), so +v' runs along -bitangent; Godot's binormal = cross(normal, tangent) * w
			float w = n.cross(t).dot(-b) < 0.0f ? -1.0f : 1.0f;
			tangents.set(v * 4 + 0, t.x);
			tangents.set(v * 4 + 1, t.y);
			tangents.set(v * 4 + 2, t.z);
			tangents.set(v * 4 + 3, w);
		}
	}
	PackedFloat32Array custom0, custom1, custom2;
	if (d.Skin)
	{
		custom0.resize(nv * 4);
		for (int64_t v = 0; v < nv; ++v)
		{
			custom0.set(v * 4 + 0, (float)d.Bone0[v]);
			custom0.set(v * 4 + 1, (float)d.Bone1[v]);
			custom0.set(v * 4 + 2, d.Weight0[v]);
			custom0.set(v * 4 + 3, d.Weight1[v]);
		}
		if (d.DualBone)
		{
			custom1.resize(nv * 4);
			custom2.resize(nv * 4);
			for (int64_t v = 0; v < nv; ++v)
			{
				for (int k = 0; k < 3; ++k)
				{
					custom1.set(v * 4 + k, d.Position1[v * 3 + k]);
					custom2.set(v * 4 + k, d.Normal1[v * 3 + k]);
				}
				custom1.set(v * 4 + 3, 0.0f);
				custom2.set(v * 4 + 3, 0.0f);
			}
		}
	}

	bool houseStopped = false;
	for (const MeshDrawSurface &surf : d.Surfaces)
	{
		W3D_Collect_Surface_Stops(d, surf, mesh, HouseColorsLoaded && !HouseColorEnabled ? &HouseColors : nullptr, gpu->Stops); // S-119 replaces S-022 when enabled
		if (surf.Kind == MATERIAL_CLASSIC && surf.State.NeverDraws)
		{
			gpu->Notes.push_back("surface with depth function NEVER skipped");
			continue;
		}
		const bool blends = surf.Kind == MATERIAL_CLASSIC && surf.State.Blend != W3D_GODOT_BLEND_OPAQUE && surf.State.Blend != W3D_GODOT_BLEND_NONE;
		if (blends && surf.Sorted)
		{
			gpu->HasSorted = true;
			gpu->SortPriorities.insert(std::clamp(surf.Pass - std::min(surf.SortLevel, 31) * 4, -128, 127)); // the render_priority the material gets
		}
		if (surf.Kind == MATERIAL_FX || (surf.Kind == MATERIAL_CLASSIC && surf.State.Blend == W3D_GODOT_BLEND_OPAQUE)) gpu->HasOpaque = true;
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = positions;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		const int uv0 = surf.StageUVStream[0];
		const int uv1 = surf.StageUVStream[1];
		const int shared = uv0 >= 0 ? uv0 : uv1;
		if (shared >= 0)
		{
			PackedVector2Array uv;
			uv.resize(nv);
			const std::vector<float> &src = d.PassUV[(size_t)shared].UV;
			for (int64_t v = 0; v < nv; ++v) uv.set(v, Vector2(src[v * 2], src[v * 2 + 1]));
			arrays[Mesh::ARRAY_TEX_UV] = uv;
		}
		bool hasUv2 = false;
		if (uv1 >= 0 && uv0 >= 0 && uv1 != uv0)
		{
			PackedVector2Array uv;
			uv.resize(nv);
			const std::vector<float> &src = d.PassUV[(size_t)uv1].UV;
			for (int64_t v = 0; v < nv; ++v) uv.set(v, Vector2(src[v * 2], src[v * 2 + 1]));
			arrays[Mesh::ARRAY_TEX_UV2] = uv;
			hasUv2 = true;
		}
		if (surf.HasDCG)
		{
			PackedColorArray colors;
			colors.resize(nv);
			const std::vector<std::uint8_t> &src = d.PassDCG[(size_t)surf.Pass];
			for (int64_t v = 0; v < nv; ++v) colors.set(v, Color(src[v * 4] / 255.0f, src[v * 4 + 1] / 255.0f, src[v * 4 + 2] / 255.0f, src[v * 4 + 3] / 255.0f));
			arrays[Mesh::ARRAY_COLOR] = colors;
		}
		if (!tangents.is_empty() && d.Camera == MESH_CAMERA_NONE) arrays[Mesh::ARRAY_TANGENT] = tangents;
		uint64_t flags = 0;
		if (d.Skin)
		{
			arrays[Mesh::ARRAY_CUSTOM0] = custom0;
			flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
			if (d.DualBone)
			{
				arrays[Mesh::ARRAY_CUSTOM1] = custom1;
				arrays[Mesh::ARRAY_CUSTOM2] = custom2;
				flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
				flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT;
			}
		}
		PackedInt32Array indices;
		indices.resize((int64_t)surf.Indices.size());
		for (size_t t = 0; t + 2 < surf.Indices.size() + 0; t += 3)
		{
			// W3D front faces are counter-clockwise; Godot's are clockwise: swap the last two
			indices.set((int64_t)t, (int32_t)surf.Indices[t]);
			indices.set((int64_t)t + 1, (int32_t)surf.Indices[t + 2]);
			indices.set((int64_t)t + 2, (int32_t)surf.Indices[t + 1]);
		}
		arrays[Mesh::ARRAY_INDEX] = indices;
		gpu->Mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), flags);
		const int surfaceIndex = gpu->Mesh->get_surface_count() - 1;

		W3DMaterialResult mat = Materials->Create(d, surf, mesh, hasUv2);
		gpu->Mesh->surface_set_material(surfaceIndex, mat.Material);
		gpu->Materials.push_back(mat.Material);
		for (const W3DMapperBinding &b : mat.Mappers) gpu->Mappers.push_back(b);
		for (const std::string &n : mat.Notes) gpu->Notes.push_back(n);
		for (const std::string &e : mat.Errors) gpu->Errors.push_back(e);
		if (surf.Kind == MATERIAL_FX) gpu->UsesFx = true;
		if (mat.Key.HouseColor && !houseStopped)
		{
			houseStopped = true;
			W3DStopHit h;
			h.Id = "S-119";
			h.Message = W3D_Stop_Message("S-119", mesh.Get_Name() + ": team colour applied by the hypothesis base = mix(base, base * team, house texture alpha); the real combine is unrecovered (S-022)");
			gpu->Stops.push_back(h);
		}
	}
	// vertices are in bone space for rigid meshes and skins: the pose decides where they land, so no useful bounds here
	gpu->Mesh->set_custom_aabb(AABB(Vector3(-400, -400, -400), Vector3(800, 800, 800)));
	MeshCache[key] = gpu;
	for (const Ref<ShaderMaterial> &m : gpu->Materials) AllMaterials.push_back(m);
	for (const W3DMapperBinding &b : gpu->Mappers) Mappers.push_back(b);
	LayoutDirty = true;
	return gpu;
}

int64_t W3DInstancer::add_model(const String &model_name)
{
	if (!Assets)
	{
		Errors.push_back("add_model before setup");
		return -1;
	}
	const std::string name = toNative(model_name);
	std::string error;
	const RenderObjPrototype *proto = Assets->Create_Render_Obj(name, &error);
	if (!proto)
	{
		Errors.push_back(error);
		UtilityFunctions::push_error("W3DInstancer: ", toGodot(error));
		return -1;
	}
	std::unique_ptr<Model> model(new Model());
	model->Name = name;
	model->Proto = proto;
	Dictionary report;
	Array subs;
	PackedStringArray notes, errors;
	size_t surfaces = 0, fxSurfaces = 0;
	std::vector<W3DStopHit> modelStops;
	for (const RenderSubObject &sub : proto->SubObjects)
	{
		Dictionary s;
		s["name"] = toGodot(sub.Name);
		s["kind"] = kindName(sub.Type);
		s["bone"] = sub.BoneIndex;
		s["aggregate"] = sub.Aggregate;
		if (sub.Type == RenderSubObject::SUB_UNRESOLVED)
		{
			s["reason"] = toGodot(sub.Reason);
			errors.push_back(toGodot("sub object " + sub.Name + " unresolved: " + sub.Reason));
		}
		else if (sub.Type == RenderSubObject::SUB_EMITTER)
		{
			notes.push_back(toGodot("emitter " + sub.Name + " not drawn (particle systems are not part of this lane)"));
		}
		else if (sub.Type == RenderSubObject::SUB_MESH)
		{
			if (sub.Mesh->Is_Hidden())
			{
				s["hidden"] = true;
				notes.push_back(toGodot("mesh " + sub.Name + " has the HIDDEN flag and is not drawn"));
				subs.push_back(s);
				continue;
			}
			std::vector<std::string> meshErrors;
			std::shared_ptr<MeshGpu> gpu = build_mesh(*sub.Mesh, meshErrors);
			if (!gpu)
			{
				for (const std::string &e : meshErrors) errors.push_back(toGodot(e));
				s["error"] = true;
				subs.push_back(s);
				continue;
			}
			s["vertices"] = (int64_t)gpu->Data.NumVertices;
			s["triangles"] = (int64_t)gpu->Data.NumTriangles;
			s["skin"] = gpu->Data.Skin;
			s["dual_bone"] = gpu->Data.DualBone;
			s["surfaces"] = (int64_t)gpu->Data.Surfaces.size();
			s["zero_weight_vertices"] = (int64_t)gpu->Data.ZeroWeightVertices;
			{
				PackedInt32Array priorities; // render priorities of the sorted blended surfaces (S-029 compares batches that share one)
				for (int pr : gpu->SortPriorities) priorities.push_back(pr);
				s["sort_priorities"] = priorities;
			}
			surfaces += gpu->Data.Surfaces.size();
			for (const MeshDrawSurface &surf : gpu->Data.Surfaces)
				if (surf.Kind == MATERIAL_FX) ++fxSurfaces;
			for (const std::string &n : gpu->Notes) notes.push_back(toGodot(sub.Name + ": " + n));
			for (const W3DStopHit &h : gpu->Stops) modelStops.push_back(h);
			for (const std::string &e : gpu->Errors) errors.push_back(toGodot(sub.Name + ": " + e));

			DrawItem item;
			item.Sub = &sub;
			item.SubIndex = (int)(&sub - proto->SubObjects.data());
			item.Mesh = gpu;
			item.MM.instantiate();
			item.MM->set_transform_format(MultiMesh::TRANSFORM_3D);
			item.MM->set_use_custom_data(true);
			item.MM->set_mesh(gpu->Mesh);
			item.MM->set_instance_count(0);
			item.Node = memnew(MultiMeshInstance3D);
			item.Node->set_name(toGodot(name + "_" + sub.Name));
			item.Node->set_multimesh(item.MM);
			item.Node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			add_child(item.Node);
			model->Draws.push_back(std::move(item));
		}
		subs.push_back(s);
	}
	report["name"] = toGodot(name);
	report["type"] = proto->Type == RenderObjPrototype::PROTO_HLOD ? "hlod" : "mesh";
	report["hierarchy"] = toGodot(proto->HierarchyName);
	report["hierarchy_missing"] = proto->HierarchyMissing;
	report["pivots"] = proto->Tree->Num_Pivots();
	report["lod_arrays"] = (int64_t)proto->LodCount;
	report["extra_lod_arrays"] = (int64_t)proto->ExtraLodArrays;
	report["sub_objects"] = subs;
	report["draw_items"] = (int64_t)model->Draws.size();
	report["surfaces"] = (int64_t)surfaces;
	report["fx_surfaces"] = (int64_t)fxSurfaces;
	// Every acceptance stop this model uses, by id: { count = covered vertices / surfaces, hits = messages, messages }. Also in the notes
	// and in get_warnings(); the ids are registered in docs/STOPS.md.
	Dictionary stops;
	{
		std::map<std::string, std::vector<const W3DStopHit *>> byId;
		for (const W3DStopHit &h : modelStops) byId[h.Id].push_back(&h);
		for (const auto &kv : byId)
		{
			Dictionary e;
			int64_t covered = 0;
			PackedStringArray messages;
			std::set<std::string> seenMessage;
			for (const W3DStopHit *h : kv.second)
			{
				covered += (int64_t)h->Count;
				if (seenMessage.insert(h->Message).second)
				{
					messages.push_back(toGodot(h->Message));
					notes.push_back(toGodot(h->Message));
					warn(h->Message);
				}
			}
			e["count"] = covered;
			e["hits"] = (int64_t)kv.second.size();
			e["messages"] = messages;
			stops[toGodot(kv.first)] = e;
		}
	}
	report["stops"] = stops;
	report["notes"] = notes;
	report["errors"] = errors;
	report["ok"] = errors.is_empty();
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		Errors.push_back(toNative(errors[i]));
		UtilityFunctions::push_error("W3DInstancer: ", name.c_str(), ": ", errors[i]);
	}
	model->Report = report;
	Models.push_back(std::move(model));
	LayoutDirty = true;
	return (int64_t)Models.size() - 1;
}

Dictionary W3DInstancer::get_model_report(int64_t model) const
{
	if (model < 0 || model >= (int64_t)Models.size()) return Dictionary();
	return Models[(size_t)model]->Report;
}

// ---- clips / instances -----------------------------------------------------------------------------------------------

Dictionary W3DInstancer::get_clip_info(int64_t model, const String &clip)
{
	Dictionary r;
	r["ok"] = false;
	if (model < 0 || model >= (int64_t)Models.size() || !Assets) return r;
	const Model &m = *Models[(size_t)model];
	std::string name = toNative(clip), resolved, error;
	const bool dotted = name.find('.') != std::string::npos;
	if (dotted) resolved = name;
	else if (!Assets->Resolve_Animation(m.Proto->HierarchyName, name, false, 0, &resolved))
	{
		r["error"] = toGodot("clip " + name + " is not registered for hierarchy " + m.Proto->HierarchyName);
		return r;
	}
	const HAnimClass *anim = Assets->Get_HAnim(resolved, &error);
	if (!anim)
	{
		r["error"] = toGodot(error);
		return r;
	}
	if (!dotted)
	{
		const W3DStopHit stop = W3D_Bare_Clip_Stop(name, m.Proto->HierarchyName, resolved);
		warn(stop.Message);
		r["stop"] = toGodot(stop.Message);
	}
	r["ok"] = true;
	r["name"] = toGodot(anim->Get_Name());
	r["frames"] = anim->Get_Num_Frames();
	r["frame_rate"] = anim->Get_Frame_Rate();
	r["seconds"] = (double)anim->Get_Num_Frames() / (double)anim->Get_Frame_Rate();
	r["pivots"] = anim->Get_Num_Pivots();
	return r;
}

int64_t W3DInstancer::add_instance(int64_t model, const Transform3D &transform, const String &clip, double start_time, double speed)
{
	if (model < 0 || model >= (int64_t)Models.size())
	{
		Errors.push_back("add_instance: no such model");
		return -1;
	}
	Instance inst;
	inst.Model = (int)model;
	inst.Xf = transform;
	inst.Start = start_time;
	inst.Speed = speed;
	// lane SMOOTH-1: the pose and the palette block are made here; the layout pass only grows the MultiMesh capacities (it no longer relays out
	// every instance and recreates the palette texture on each add, a frame-time spike whenever an object was created or changed its model)
	inst.Pose.Resize(Models[(size_t)model]->Proto->Tree->Num_Pivots());
	allocate_palette(inst);
	Instances.push_back(std::move(inst));
	const int id = (int)Instances.size() - 1;
	Models[(size_t)model]->InstanceIds.push_back(id);
	Models[(size_t)model]->BuffersDirty = true;
	if ((int)Models[(size_t)model]->InstanceIds.size() > (Models[(size_t)model]->Draws.empty() ? 0 : Models[(size_t)model]->Draws.front().Allocated))
	{
		LayoutDirty = true;
	}
	if (!clip.is_empty() && !set_instance_clip(id, clip, start_time, speed))
	{
		return id; // the instance exists in bind pose; the error is recorded
	}
	return id;
}

void W3DInstancer::set_instance_transform(int64_t instance, const Transform3D &transform)
{
	if (instance >= 0 && instance < (int64_t)Instances.size())
	{
		Instances[(size_t)instance].Xf = transform;
		PoseDirty = true;
		mark_model(instance);
	}
}

bool W3DInstancer::remove_instance(int64_t instance)
{
	if (instance < 0 || instance >= (int64_t)Instances.size())
	{
		return false;
	}
	Instance &inst = Instances[(size_t)instance];
	if (!inst.Removed)
	{
		// lane SMOOTH-1: the instance leaves its model's list and its palette block is free for the next instance of the same size
		inst.Removed = true;
		Model &m = *Models[(size_t)inst.Model];
		auto it = std::find(m.InstanceIds.begin(), m.InstanceIds.end(), (int)instance);
		if (it != m.InstanceIds.end())
		{
			m.InstanceIds.erase(it);
		}
		m.BuffersDirty = true;
		if (inst.PaletteSize > 0)
		{
			PaletteFree[inst.PaletteSize].push_back(inst.PaletteOffset);
		}
		inst.PaletteSize = 0;
		inst.Pose = HTreePose();
	}
	PoseDirty = true;
	return true;
}

void W3DInstancer::mark_model(int64_t instance)
{
	Models[(size_t)Instances[(size_t)instance].Model]->BuffersDirty = true;
}

void W3DInstancer::allocate_palette(Instance &inst)
{
	const int size = inst.Pose.Num_Pivots() * W3D_PALETTE_TEXELS_PER_PIVOT;
	inst.PaletteSize = size;
	auto it = PaletteFree.find(size);
	if (it != PaletteFree.end() && !it->second.empty())
	{
		inst.PaletteOffset = it->second.back();
		it->second.pop_back();
		return;
	}
	inst.PaletteOffset = PaletteUsed;
	PaletteUsed += size;
	if (PaletteUsed > PaletteHeight * W3D_PALETTE_WIDTH)
	{
		grow_palette(PaletteUsed);
	}
}

void W3DInstancer::grow_palette(int texels)
{
	const int capacity = std::max(texels, (PaletteHeight * W3D_PALETTE_WIDTH * 3) / 2);
	const int height = std::max(1, (capacity + W3D_PALETTE_WIDTH - 1) / W3D_PALETTE_WIDTH);
	PaletteData.resize((size_t)height * W3D_PALETTE_WIDTH * 4, 0.0f);
	PaletteHeight = height;
	PaletteImage = Image::create_empty(W3D_PALETTE_WIDTH, PaletteHeight, false, Image::FORMAT_RGBAF);
	PaletteTexture = ImageTexture::create_from_image(PaletteImage);
	PaletteGrown = true;
	PaletteChanged = true;
	LayoutDirty = true; // the materials take the new texture in rebuild_layout
}

const HAnimClass *W3DInstancer::resolve_clip(const Model &m, const std::string &name)
{
	std::string resolved, error;
	if (name.find('.') != std::string::npos) resolved = name;
	else if (!Assets->Resolve_Animation(m.Proto->HierarchyName, name, false, 0, &resolved))
	{
		Errors.push_back("clip " + name + " is not registered for hierarchy " + m.Proto->HierarchyName);
		UtilityFunctions::push_error("W3DInstancer: ", toGodot(Errors.back()));
		return nullptr;
	}
	const HAnimClass *anim = Assets->Get_HAnim(resolved, &error);
	if (!anim)
	{
		Errors.push_back(error);
		UtilityFunctions::push_error("W3DInstancer: ", toGodot(error));
		return nullptr;
	}
	if (name.find('.') == std::string::npos)
	{
		warn(W3D_Bare_Clip_Stop(name, m.Proto->HierarchyName, resolved).Message);
	}
	return anim;
}

bool W3DInstancer::set_instance_clip(int64_t instance, const String &clip, double start_time, double speed)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || !Assets) return false;
	Instance &inst = Instances[(size_t)instance];
	inst.Start = start_time;
	inst.Speed = speed;
	inst.Explicit = false;
	inst.Anim1 = nullptr;
	inst.PoseStale = true;
	PoseDirty = true;
	if (clip.is_empty())
	{
		inst.Anim = nullptr;
		return true;
	}
	const HAnimClass *anim = resolve_clip_cached(*Models[(size_t)inst.Model], toNative(clip));
	if (!anim) return false;
	inst.Anim = anim;
	return true;
}

const HAnimClass *W3DInstancer::resolve_clip_cached(Model &m, const std::string &name)
{
	// lane SMOOTH-1: an animated drawable is posed every render frame by clip name; resolving the name (the hierarchy lookup, the asset map, the
	// S-0xx bare clip report) once per model and name keeps that per-frame cost a hash lookup. Failures are not cached: they report every time.
	auto it = m.Clips.find(name);
	if (it != m.Clips.end())
	{
		return it->second;
	}
	const HAnimClass *anim = resolve_clip(m, name);
	if (anim)
	{
		m.Clips.emplace(name, anim);
	}
	return anim;
}

bool W3DInstancer::set_instance_pose_native(int64_t instance, const std::string &clip0, double frame0, const std::string &clip1, double frame1, double percentage)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || !Assets) return false;
	Instance &inst = Instances[(size_t)instance];
	Model &m = *Models[(size_t)inst.Model];
	const HAnimClass *a0 = nullptr;
	const HAnimClass *a1 = nullptr;
	if (!clip0.empty())
	{
		a0 = resolve_clip_cached(m, clip0);
		if (!a0) return false;
	}
	if (!clip1.empty())
	{
		a1 = resolve_clip_cached(m, clip1);
		if (!a1) return false;
	}
	const HAnimClass *anim1 = a0 ? a1 : nullptr;
	const float f0 = (float)frame0;
	const float f1 = (float)frame1;
	const float pct = (float)(percentage < 0.0 ? 0.0 : (percentage > 1.0 ? 1.0 : percentage));
	if (inst.Explicit && inst.Anim == a0 && inst.Anim1 == anim1 && inst.Frame0 == f0 && inst.Frame1 == f1 && inst.Percentage == pct)
	{
		return true; // the same pose: nothing to evaluate
	}
	inst.Explicit = true;
	inst.Anim = a0;
	inst.Anim1 = anim1;
	inst.Frame0 = f0;
	inst.Frame1 = f1;
	inst.Percentage = pct;
	inst.PoseStale = true;
	PoseDirty = true;
	return true;
}

bool W3DInstancer::set_instance_pose(int64_t instance, const String &clip0, double frame0, const String &clip1, double frame1, double percentage)
{
	return set_instance_pose_native(instance, clip0.is_empty() ? std::string() : toNative(clip0), frame0, clip1.is_empty() ? std::string() : toNative(clip1), frame1, percentage);
}

bool W3DInstancer::set_instance_hidden_subobjects(int64_t instance, const PackedStringArray &names)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return false;
	Instance &inst = Instances[(size_t)instance];
	const Model &m = *Models[(size_t)inst.Model];
	std::vector<std::uint8_t> hidden;
	if (!names.is_empty()) hidden.assign(m.Proto->SubObjects.size(), 0);
	for (int64_t i = 0; i < names.size(); ++i)
	{
		const std::string want = AsciiStringUtil::lowered(toNative(names[i]));
		bool found = false;
		for (size_t k = 0; k < m.Proto->SubObjects.size(); ++k)
		{
			if (AsciiStringUtil::lowered(m.Proto->SubObjects[k].Name) == want)
			{
				hidden[k] = 1;
				found = true;
			}
		}
		if (!found)
		{
			Errors.push_back("model " + m.Name + " has no sub object " + want);
			return false;
		}
	}
	inst.HiddenSubs.swap(hidden);
	PoseDirty = true;
	mark_model(instance);
	return true;
}

// ---- per frame -------------------------------------------------------------------------------------------------------

void W3DInstancer::rebuild_layout()
{
	// lane SMOOTH-1: the palette blocks are allocated per instance (allocate_palette); this pass makes sure a palette texture exists for the
	// materials of new models and grows each draw item's MultiMesh capacity by half when its live instances outgrow it (the visible instance
	// count draws only the written ones), so a creation does not reallocate every buffer
	if (PaletteTexture.is_null())
	{
		grow_palette(std::max(PaletteUsed, 1));
	}
	for (const Ref<ShaderMaterial> &m : AllMaterials)
	{
		m->set_shader_parameter("w3d_palette", PaletteTexture);
	}
	PaletteGrown = false;
	PaletteTexels = PaletteUsed;
	for (std::unique_ptr<Model> &mp : Models)
	{
		mp->HasSorted = false;
		const int n = (int)mp->InstanceIds.size();
		for (DrawItem &d : mp->Draws)
		{
			mp->HasSorted = mp->HasSorted || d.Mesh->HasSorted;
			if (n > d.Allocated)
			{
				const int capacity = std::max(n, std::max(4, (d.Allocated * 3) / 2));
				d.MM->set_instance_count(capacity);
				d.Buffer.resize((int64_t)capacity * 16);
				d.Allocated = capacity;
				mp->BuffersDirty = true;
			}
		}
	}
	LayoutDirty = false;
	PoseDirty = true;
}

void W3DInstancer::evaluate_pose(Instance &inst) const
{
	const Model &m = *Models[(size_t)inst.Model];
	const HTreeClass &tree = *m.Proto->Tree;
	if (inst.Anim == nullptr)
	{
		tree.Base_Pose(Matrix3D(), inst.Pose);
		inst.Inexact = false;
		return;
	}
	if (inst.Explicit)
	{
		if (inst.Anim1 != nullptr) tree.Blend_Pose(Matrix3D(), inst.Anim, inst.Frame0, inst.Anim1, inst.Frame1, inst.Percentage, inst.Pose);
		else tree.Anim_Pose(Matrix3D(), inst.Anim, inst.Frame0, inst.Pose);
		inst.Inexact = !inst.Pose.ExactOperationOrder;
		return;
	}
	const double fps = inst.Anim->Get_Frame_Rate();
	const int frames = inst.Anim->Get_Num_Frames();
	double frame = 0.0;
	if (frames > 1)
	{
		// BFME LOOP mode wraps by N - 1 (spec 4.5 table, Rva0076C080AdvanceAnimation.cpp:385-411)
		const double span = (double)(frames - 1);
		frame = std::fmod((GlobalTime * inst.Speed + inst.Start) * fps, span);
		if (frame < 0.0) frame += span;
	}
	tree.Anim_Pose(Matrix3D(), inst.Anim, (float)frame, inst.Pose);
	inst.Inexact = !inst.Pose.ExactOperationOrder;
}

void W3DInstancer::write_palette(const Instance &inst)
{
	const int pivots = inst.Pose.Num_Pivots();
	float *out = PaletteData.data() + (size_t)inst.PaletteOffset * 4;
	for (int p = 0; p < pivots; ++p)
	{
		const Matrix3D &m = inst.Pose.Transform[(size_t)p];
		std::memcpy(out + (size_t)p * 12, &m.Row[0][0], sizeof(float) * 12); // three rows of (x, y, z, translation)
	}
}

void W3DInstancer::write_buffers()
{
	// Camera for depth sorting: the explicit one, else the viewport's current camera.
	bool haveCamera = false;
	::Vector3 camPos, camForward;
	Transform3D cam;
	{
		if (SortCameraSet)
		{
			cam = SortCamera;
			haveCamera = true;
		}
		else if (is_inside_tree() && get_viewport() != nullptr && get_viewport()->get_camera_3d() != nullptr)
		{
			cam = get_viewport()->get_camera_3d()->get_global_transform();
			haveCamera = true;
		}
		if (haveCamera)
		{
			const Vector3 f = -cam.basis.get_column(2);
			camPos = ::Vector3(cam.origin.x, cam.origin.y, cam.origin.z);
			camForward = ::Vector3(f.x, f.y, f.z);
		}
	}
	const Transform3D nodeXf = is_inside_tree() ? get_global_transform() : Transform3D();
	// lane SMOOTH-1: a model is rewritten only when one of its instances changed (moved, posed, added, removed, re-tinted, hid a sub object), or,
	// for a model with back-to-front sorted items, when the camera moved; the others keep last frame's buffers and counts
	const bool cameraMoved = !haveCamera || !HaveLastCamera || !(cam == LastCamera) || !(nodeXf == LastNodeXf); // the node's transform enters the depths too
	LastCamera = cam;
	LastNodeXf = nodeXf;
	HaveLastCamera = haveCamera;

	int dithered = 0, sortedItems = 0, visible = 0, writes = 0;
	bool missedCamera = false;
	bool sortRewritten = false;
	for (std::unique_ptr<Model> &mp : Models)
	{
		Model &m = *mp;
		if (!m.BuffersDirty && !(m.HasSorted && cameraMoved))
		{
			dithered += m.Dithered;
			sortedItems += m.SortedItems;
			for (const DrawItem &d : m.Draws) visible += (int)d.Order.size();
			continue;
		}
		++writes;
		m.BuffersDirty = false;
		m.Dithered = 0;
		m.SortedItems = 0;
		for (DrawItem &d : m.Draws)
		{
			const int bone = d.Sub->BoneIndex;
			const bool rigid = !d.Mesh->Data.Skin;
			std::vector<int> &ids = d.Order;
			ids.clear();
			for (int id : m.InstanceIds)
			{
				const Instance &inst = Instances[(size_t)id];
				if (inst.Removed) continue;
				if (!inst.HiddenSubs.empty() && inst.HiddenSubs[(size_t)d.SubIndex]) continue;
				if (!inst.Pose.IsVisible[(size_t)bone]) continue;
				if (inst.Pose.PivotFade[(size_t)bone] * inst.Opacity <= 0.0f) continue;
				ids.push_back(id);
			}
			// a batch that existed and is now gone (its last sorted instance removed or hidden) changes the S-029 count too (review r1)
			sortRewritten = sortRewritten || d.HasBatch;
			d.HasBatch = false;
			if (d.Mesh->HasSorted && !ids.empty())
			{
				if (haveCamera)
				{
					// Back to front by the camera depth of the pivot the mesh hangs on (w3dsort.h).
					std::vector<float> &depth = DepthScratch;
					depth.resize(ids.size());
					for (size_t k = 0; k < ids.size(); ++k)
					{
						const Instance &inst = Instances[(size_t)ids[k]];
						const ::Vector3 t = inst.Pose.Transform[(size_t)bone].Get_Translation();
						const Vector3 world = nodeXf.xform(inst.Xf.xform(Vector3(t.X, t.Z, -t.Y))); // W3D (x, y, z) -> Godot (x, z, -y)
						depth[k] = (world.x - camPos.X) * camForward.X + (world.y - camPos.Y) * camForward.Y + (world.z - camPos.Z) * camForward.Z;
					}
					d.Batch.Depth = depth;
					d.Batch.Priorities = d.Mesh->SortPriorities;
					d.HasBatch = true;
					sortRewritten = true;
					const std::vector<int> order = W3D_Back_To_Front(depth);
					IdScratch.resize(ids.size());
					for (size_t k = 0; k < order.size(); ++k) IdScratch[k] = ids[(size_t)order[k]];
					ids.swap(IdScratch);
					++m.SortedItems;
				}
				else
				{
					missedCamera = true;
				}
			}
			float *buf = d.Buffer.ptrw();
			int count = 0;
			for (int id : ids)
			{
				const Instance &inst = Instances[(size_t)id];
				const float fade = inst.Pose.PivotFade[(size_t)bone] * inst.Opacity; // PROJ-2: the drawable's opacity
				if (d.Mesh->HasOpaque && fade < 0.999f) ++m.Dithered;
				float *o = buf + (size_t)count * 16;
				const Basis &b = inst.Xf.basis;
				o[0] = b.rows[0][0]; o[1] = b.rows[0][1]; o[2] = b.rows[0][2]; o[3] = inst.Xf.origin.x;
				o[4] = b.rows[1][0]; o[5] = b.rows[1][1]; o[6] = b.rows[1][2]; o[7] = inst.Xf.origin.y;
				o[8] = b.rows[2][0]; o[9] = b.rows[2][1]; o[10] = b.rows[2][2]; o[11] = inst.Xf.origin.z;
				o[12] = inst.InfantryLight ? -(float)(inst.PaletteOffset + 1) : (float)inst.PaletteOffset;
				o[13] = fade;
				o[14] = inst.HcPacked;
				o[15] = rigid ? (float)bone : 0.0f;
				++count;
			}
			d.MM->set_buffer(d.Buffer);
			d.MM->set_visible_instance_count(count);
			visible += count;
		}
		dithered += m.Dithered;
		sortedItems += m.SortedItems;
	}
	DitheredFadeInstances = dithered;
	SortedDrawItems = sortedItems;
	VisibleInstances = visible;
	BufferWrites = writes;
	// Godot draws each MultiMesh as one instanced draw, so instances of two batches cannot be interleaved: where the depths of two blended
	// batches with a common priority interleave, no order is back to front. Reported, not hidden (stop S-029). Recounted when a sorted item
	// was rewritten (the depths of the others are those of their last write, still current: neither they nor the camera moved).
	if (sortRewritten)
	{
		SortBatchScratch.clear();
		for (const std::unique_ptr<Model> &mp : Models)
			for (const DrawItem &d : mp->Draws)
				if (d.HasBatch) SortBatchScratch.push_back(d.Batch);
		InterleavedBatchPairs = (int)W3D_Count_Interleaved_Batches(SortBatchScratch);
	}
	if (InterleavedBatchPairs > 0) warn(W3D_Batch_Order_Stop((size_t)InterleavedBatchPairs).Message);
	if (dithered > 0) warn(W3D_Fade_Dither_Stop((size_t)dithered).Message);
	if (missedCamera)
	{
		warn("blended draw items are drawn in insertion order: no sort camera (set_sort_camera) and no current Camera3D");
	}
}

void W3DInstancer::update_mappers()
{
	const auto t0 = std::chrono::steady_clock::now();
	const std::uint32_t syncMs = (std::uint32_t)(GlobalTime * 1000.0);
	if (syncMs == MapperSyncMs && MapperCount == Mappers.size())
	{
		MapperMs = msSince(t0); // SMOOTH-1: the clock did not move (paused, or two frames within a millisecond): the matrices are those already set
		return;
	}
	MapperSyncMs = syncMs;
	MapperCount = Mappers.size();
	for (W3DMapperBinding &b : Mappers)
	{
		W3DTexMatrix m;
		b.Mapper->Calculate_Texture_Matrix(m, syncMs);
		// SMOOTH-1: the uniform names as StringNames made once per stage (building two Strings per mapper per frame showed in the frame time)
		const size_t stage = (size_t)std::max(0, b.Stage);
		while (MapperNames.size() <= stage)
		{
			const String ss = String::num_int64((int64_t)MapperNames.size());
			MapperNames.push_back({ StringName(String("w3d_m") + ss + "r0"), StringName(String("w3d_m") + ss + "r1") });
		}
		b.Material->set_shader_parameter(MapperNames[stage].first, Vector4(m.M[0][0], m.M[0][1], m.M[0][2], m.M[0][3]));
		b.Material->set_shader_parameter(MapperNames[stage].second, Vector4(m.M[1][0], m.M[1][1], m.M[1][2], m.M[1][3]));
	}
	MapperMs = msSince(t0);
}

Array W3DInstancer::get_mapper_state() const
{
	Array out;
	for (const W3DMapperBinding &b : Mappers)
	{
		Dictionary d;
		const String ss = String::num_int64(b.Stage);
		d["source"] = toGodot(b.Source);
		d["stage"] = b.Stage;
		d["r0"] = b.Material->get_shader_parameter(String("w3d_m") + ss + "r0");
		d["r1"] = b.Material->get_shader_parameter(String("w3d_m") + ss + "r1");
		out.push_back(d);
	}
	return out;
}

void W3DInstancer::update_now()
{
	if (!Assets) return;
	if (LayoutDirty) rebuild_layout();

	update_mappers();

	// lane SMOOTH-1: only the instances whose pose inputs changed are evaluated (a new pose request, a new clip, or the clock for clip-driven
	// instances); before, every instance was evaluated and every palette texel uploaded each render frame
	auto t0 = std::chrono::steady_clock::now();
	std::atomic<int> poseErrors(0);
	std::string firstError;
	std::mutex errorMutex;
	const bool timeMoved = GlobalTime != PosedTime;
	PosedTime = GlobalTime;
	StaleScratch.clear();
	for (size_t i = 0; i < Instances.size(); ++i)
	{
		const Instance &inst = Instances[i];
		if (!inst.Removed && (inst.PoseStale || (timeMoved && inst.Anim != nullptr && !inst.Explicit)))
		{
			StaleScratch.push_back(i);
		}
	}
	run_parallel(StaleScratch.size(), [&](size_t begin, size_t end) {
		for (size_t k = begin; k < end; ++k)
		{
			Instance &inst = Instances[StaleScratch[k]];
			try
			{
				evaluate_pose(inst);
			}
			catch (const std::exception &e)
			{
				if (poseErrors.fetch_add(1) == 0)
				{
					std::lock_guard<std::mutex> lock(errorMutex);
					firstError = e.what();
				}
			}
			write_palette(inst);
			inst.PoseStale = false;
		}
	});
	for (size_t i : StaleScratch)
	{
		Models[(size_t)Instances[i].Model]->BuffersDirty = true; // the pivot visibility and fade the buffers read may have changed
	}
	PoseEvaluations = (int)StaleScratch.size();
	if (!StaleScratch.empty())
	{
		PaletteChanged = true;
	}
	if (poseErrors.load() > 0)
	{
		Errors.push_back("pose evaluation failed for " + std::to_string(poseErrors.load()) + " instance(s): " + firstError);
		UtilityFunctions::push_error("W3DInstancer: ", toGodot(Errors.back()));
	}
	PoseMs = msSince(t0);
	int inexact = 0;
	for (const Instance &inst : Instances)
	{
		inexact += (!inst.Removed && inst.Inexact) ? 1 : 0;
	}
	InexactPoseInstances = inexact;
	if (inexact > 0)
	{
		InexactPoseTotal = inexact;
		warn(W3D_Pose_Order_Stop((size_t)inexact).Message);
	}
	else
	{
		InexactPoseTotal = 0;
	}

	t0 = std::chrono::steady_clock::now();
	write_buffers();
	if (PaletteChanged)
	{
		PackedByteArray bytes;
		bytes.resize((int64_t)PaletteData.size() * 4);
		std::memcpy(bytes.ptrw(), PaletteData.data(), PaletteData.size() * 4);
		PaletteImage->set_data(W3D_PALETTE_WIDTH, PaletteHeight, false, Image::FORMAT_RGBAF, bytes);
		PaletteTexture->update(PaletteImage);
		PaletteChanged = false;
	}
	UploadMs = msSince(t0);
	PoseDirty = false;
}

void W3DInstancer::_notification(int what)
{
	// lane RENDER-3 (S-831): this node's transparent shaders output gamma-space values; the camera that shows them composites the transparent pass in
	// gamma space (GodotGammaComposite.h)
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		GammaComposite::ensure(this);
	}
}

void W3DInstancer::_process(double delta)
{
	if (Playing) GlobalTime += delta * TimeScale;
	update_now();
}

Vector3 W3DInstancer::get_bone_position(int64_t instance, int bone) const
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return Vector3();
	const Instance &inst = Instances[(size_t)instance];
	if (bone < 0 || bone >= inst.Pose.Num_Pivots()) return Vector3();
	::Vector3 t = inst.Pose.Transform[(size_t)bone].Get_Translation();
	return Vector3(t.X, t.Y, t.Z);
}

int W3DInstancer::get_bone_index_native(int64_t model, const std::string &name) const
{
	if (model < 0 || model >= (int64_t)Models.size() || !Models[(size_t)model]->Proto || !Models[(size_t)model]->Proto->Tree)
	{
		return -1;
	}
	// RW 0xB54D33 walks the bone names with _strcmpi (Get_Bone_Index would answer the root for a missing name)
	const HTreeClass &tree = *Models[(size_t)model]->Proto->Tree;
	for (int i = 0; i < tree.Num_Pivots(); ++i)
	{
		const std::string &pivot = tree.Get_Pivot(i).Name;
		if (pivot.size() == name.size() && std::equal(pivot.begin(), pivot.end(), name.begin(), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }))
		{
			return i;
		}
	}
	return -1;
}

bool W3DInstancer::get_bone_transform_native(int64_t instance, int bone, ::Matrix3D &out) const
{
	if (instance < 0 || instance >= (int64_t)Instances.size())
	{
		return false;
	}
	const Instance &inst = Instances[(size_t)instance];
	if (bone < 0 || bone >= inst.Pose.Num_Pivots())
	{
		return false;
	}
	out = inst.Pose.Transform[(size_t)bone];
	return true;
}

Dictionary W3DInstancer::get_stats() const
{
	Dictionary s;
	s["instances"] = (int64_t)Instances.size();
	s["models"] = (int64_t)Models.size();
	int64_t draws = 0;
	for (const std::unique_ptr<Model> &m : Models) draws += (int64_t)m->Draws.size();
	s["draw_items"] = draws;
	s["palette_texels"] = PaletteTexels;
	s["palette_rows"] = PaletteHeight;
	s["shaders"] = Materials ? (int64_t)Materials->Shader_Count() : 0;
	s["textures"] = Materials ? (int64_t)Materials->Texture_Count() : 0;
	s["pose_ms"] = PoseMs;
	s["upload_ms"] = UploadMs;
	s["pose_evaluations"] = PoseEvaluations; // SMOOTH-1: instances posed in the last update (only the changed ones)
	s["buffer_writes"] = BufferWrites;       // SMOOTH-1: models whose MultiMesh buffers were rewritten in the last update
	s["mapper_ms"] = MapperMs;
	s["visible_draw_instances"] = VisibleInstances;
	s["worker_threads"] = Workers;
	s["errors"] = (int64_t)Errors.size();
	s["warnings"] = (int64_t)Warnings.size();
	s["inexact_pose_instances"] = InexactPoseTotal;     // S-028 in the last update
	s["dithered_fade_instances"] = DitheredFadeInstances; // S-029 in the last update
	s["sorted_draw_items"] = SortedDrawItems;
	s["interleaved_batch_pairs"] = InterleavedBatchPairs; // S-029 (second condition) in the last update
	return s;
}

Dictionary W3DInstancer::probe_shaders(const Vector3 &origin)
{
	Dictionary result;
	result["ok"] = false;
	if (FsRef.is_null() || FsRef->archive_fs() == nullptr)
	{
		result["error"] = "setup first";
		return result;
	}
	ArchiveFileSystem &afs = *FsRef->archive_fs();
	FilenameList list;
	afs.getFileListInDirectory("", "", "*.w3d", list, true);

	struct Probe
	{
		W3DShaderKey Key;
		Ref<ShaderMaterial> Material;
		bool Dual = false;
	};
	std::map<std::string, Probe> distinct;
	W3DMaterialFactory factory(afs);
	size_t files = 0, meshes = 0, surfaces = 0, failures = 0;
	std::vector<std::uint8_t> bytes;
	for (const std::string &path : list)
	{
		std::string error;
		if (!afs.readFile(path, bytes, &error)) continue;
		W3DFileContents contents;
		if (!Load_W3D_File(bytes.data(), bytes.size(), contents, &error)) continue; // the 8 corrupt files
		++files;
		for (const MeshModelClass &mesh : contents.Meshes)
		{
			++meshes;
			MeshRenderData data;
			if (!Build_Mesh_Render_Data(mesh, data, &error))
			{
				++failures;
				continue;
			}
			for (const MeshDrawSurface &surf : data.Surfaces)
			{
				++surfaces;
				if (surf.Kind == MATERIAL_CLASSIC && surf.State.NeverDraws) continue;
				const int uv0 = surf.StageUVStream[0], uv1 = surf.StageUVStream[1];
				const bool hasUv2 = uv1 >= 0 && uv0 >= 0 && uv1 != uv0;
				// the key is decided without touching a texture; only the first surface of each permutation is kept
				W3DMaterialResult r = factory.Create(data, surf, mesh, hasUv2, false);
				const std::string id = r.Key.Id();
				if (distinct.find(id) == distinct.end())
				{
					Probe p;
					p.Key = r.Key;
					p.Material = r.Material;
					distinct[id] = p;
				}
			}
		}
	}

	// identity palette: one pivot, three texels
	Ref<Image> pimg = Image::create_empty(W3D_PALETTE_WIDTH, 1, false, Image::FORMAT_RGBAF);
	PackedByteArray pb;
	pb.resize((int64_t)W3D_PALETTE_WIDTH * 16);
	std::memset(pb.ptrw(), 0, (size_t)pb.size());
	float *pf = reinterpret_cast<float *>(pb.ptrw());
	pf[0] = 1; pf[5] = 1; pf[10] = 1; // rows (1,0,0,0) (0,1,0,0) (0,0,1,0)
	pimg->set_data(W3D_PALETTE_WIDTH, 1, false, Image::FORMAT_RGBAF, pb);
	Ref<ImageTexture> ptex = ImageTexture::create_from_image(pimg);

	int index = 0;
	for (auto &kv : distinct)
	{
		const W3DShaderKey &k = kv.second.Key;
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		PackedVector3Array v, n;
		v.push_back(Vector3(0, 0, 0)); v.push_back(Vector3(10, 0, 0)); v.push_back(Vector3(0, 0, 10));
		for (int i = 0; i < 3; ++i) n.push_back(Vector3(0, 1, 0));
		arrays[Mesh::ARRAY_VERTEX] = v;
		arrays[Mesh::ARRAY_NORMAL] = n;
		PackedVector2Array uv;
		uv.push_back(Vector2(0, 0)); uv.push_back(Vector2(1, 0)); uv.push_back(Vector2(0, 1));
		arrays[Mesh::ARRAY_TEX_UV] = uv;
		if (k.Uv2) arrays[Mesh::ARRAY_TEX_UV2] = uv;
		if (k.HasDCG)
		{
			PackedColorArray c;
			for (int i = 0; i < 3; ++i) c.push_back(Color(1, 1, 1, 1));
			arrays[Mesh::ARRAY_COLOR] = c;
		}
		if (k.Tangents)
		{
			PackedFloat32Array t;
			for (int i = 0; i < 3; ++i) { t.push_back(1); t.push_back(0); t.push_back(0); t.push_back(1); }
			arrays[Mesh::ARRAY_TANGENT] = t;
		}
		uint64_t flags = 0;
		if (k.Skin)
		{
			PackedFloat32Array c0, c1, c2;
			for (int i = 0; i < 3; ++i)
			{
				c0.push_back(0); c0.push_back(0); c0.push_back(1); c0.push_back(0);
				c1.push_back(0); c1.push_back(0); c1.push_back(0); c1.push_back(0);
				c2.push_back(0); c2.push_back(0); c2.push_back(1); c2.push_back(0);
			}
			arrays[Mesh::ARRAY_CUSTOM0] = c0;
			flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
			if (k.Dual)
			{
				arrays[Mesh::ARRAY_CUSTOM1] = c1;
				arrays[Mesh::ARRAY_CUSTOM2] = c2;
				flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
				flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT;
			}
		}
		PackedInt32Array idx;
		idx.push_back(0); idx.push_back(2); idx.push_back(1);
		arrays[Mesh::ARRAY_INDEX] = idx;
		Ref<ArrayMesh> am;
		am.instantiate();
		am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), flags);
		am->surface_set_material(0, kv.second.Material);
		am->set_custom_aabb(AABB(Vector3(-50, -50, -50), Vector3(100, 100, 100)));
		kv.second.Material->set_shader_parameter("w3d_palette", ptex);

		Ref<MultiMesh> mm;
		mm.instantiate();
		mm->set_transform_format(MultiMesh::TRANSFORM_3D);
		mm->set_use_custom_data(true);
		mm->set_mesh(am);
		mm->set_instance_count(1);
		PackedFloat32Array buf;
		buf.resize(16);
		float *b = buf.ptrw();
		std::memset(b, 0, 64);
		b[0] = 1; b[5] = 1; b[10] = 1;
		b[3] = origin.x + (float)(index % 30) * 40.0f;
		b[7] = origin.y;
		b[11] = origin.z + (float)(index / 30) * 40.0f;
		b[12] = 0.0f; // palette offset
		b[13] = 1.0f; // opacity
		b[15] = 0.0f; // rigid bone
		mm->set_buffer(buf);
		MultiMeshInstance3D *node = memnew(MultiMeshInstance3D);
		node->set_name(toGodot("probe_" + kv.first));
		node->set_multimesh(mm);
		node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		add_child(node);
		++index;
	}
	result["ok"] = true;
	result["files"] = (int64_t)files;
	result["meshes"] = (int64_t)meshes;
	result["surfaces"] = (int64_t)surfaces;
	result["permutations"] = (int64_t)distinct.size();
	result["build_failures"] = (int64_t)failures;
	return result;
}

PackedStringArray W3DInstancer::get_errors() const
{
	PackedStringArray a;
	for (const std::string &e : Errors) a.push_back(toGodot(e));
	if (Materials)
		for (const std::string &e : Materials->Texture_Errors()) a.push_back(toGodot(e));
	return a;
}

PackedStringArray W3DInstancer::get_warnings() const
{
	PackedStringArray out;
	for (const std::string &w : Warnings) out.push_back(toGodot(w));
	return out;
}

void W3DInstancer::warn(const std::string &message)
{
	if (WarnedMessages.insert(message).second)
	{
		Warnings.push_back(message);
		UtilityFunctions::print("W3DInstancer: ", toGodot(message));
	}
}

void W3DInstancer::set_sort_camera(const Transform3D &transform)
{
	SortCamera = transform;
	SortCameraSet = true;
	PoseDirty = true;
}

PackedInt32Array W3DInstancer::get_draw_order(int64_t model, int64_t draw) const
{
	PackedInt32Array out;
	if (model < 0 || model >= (int64_t)Models.size()) return out;
	const Model &m = *Models[(size_t)model];
	if (draw < 0 || draw >= (int64_t)m.Draws.size()) return out;
	for (int id : m.Draws[(size_t)draw].Order) out.push_back(id);
	return out;
}

} // namespace godot
