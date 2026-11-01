// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotGlowEffect.h"
#include "GodotDevice/GodotW3DInstancer.h"

#include <cstdio>

#include "Common/JobSystem.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WW3D2/w3dsort.h"

#include <cmath>

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
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
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace godot
{

namespace
{

// lane PERF-HD: every MultiMesh's custom AABB, a box beyond any map. Without one Godot recomputes the AABB over every instance at each set_buffer,
// and an animated model's buffers are rewritten every frame; one MultiMesh holds a model's instances across the whole battlefield, so its own
// box culled nothing anyway
const AABB kMultiMeshBounds(Vector3(-200000.0f, -50000.0f, -200000.0f), Vector3(400000.0f, 100000.0f, 400000.0f));

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

// RenderingDevice texture calls are only valid on the render thread. The node thread copies texel bytes into a
// PackedByteArray and appends work here; call_on_render_thread runs that work in order. The callable is a static
// function bound to this queue (not to W3DInstancer), so NOTIFICATION_PREDELETE can drop the node first.
struct PaletteRdQueue
{
	struct Op
	{
		enum Kind
		{
			Create,
			Upload,
			Release
		} kind = Upload;
		int height = 0;
		int row0 = 0;
		int rows = 0;
		bool full = false;
		PackedByteArray bytes;
		// lane PERF-HD: a partial upload of several runs: `bytes` holds them one after another (padded to the stage's rows), each run (palette row,
		// rows) copied to its place; empty: the one run row0 .. row0 + rows
		std::vector<std::pair<int, int>> runs;
		PackedByteArray fallback; // whole palette, used only if a partial copy is rejected
		Ref<Texture2DRD> sampler;
	};

	std::mutex mu;
	std::vector<Op> ops;
	bool scheduled = false;
	bool retired = false;
	RID palette;
	RID stage;
	int stage_rows = 0;
	Ref<Texture2DRD> sampler;
	std::atomic<bool> force_full{ false };
	std::atomic<bool> create_failed{ false };
	// lane PERF-CLIENT: the staging byte arrays the render thread is done with, handed out again by take(): an upload of several megabytes a frame
	// allocated (and the system zeroed) a new array every frame
	std::vector<PackedByteArray> spare;

	void enqueue(Op op, bool retire);
	PackedByteArray take(int64_t bytes);
	void give(PackedByteArray &bytes);
};

PackedByteArray PaletteRdQueue::take(int64_t bytes)
{
	PackedByteArray out;
	{
		std::lock_guard<std::mutex> lock(mu);
		size_t best = spare.size();
		for (size_t i = 0; i < spare.size(); ++i)
		{
			if (spare[i].size() == bytes)
			{
				best = i;
				break;
			}
			if (best == spare.size() || spare[i].size() > spare[best].size())
			{
				best = i;
			}
		}
		if (best < spare.size())
		{
			out = std::move(spare[best]);
			spare.erase(spare.begin() + (std::ptrdiff_t)best);
		}
	}
	if (out.size() != bytes)
	{
		out.resize(bytes);
	}
	return out;
}

void PaletteRdQueue::give(PackedByteArray &bytes)
{
	if (bytes.is_empty())
	{
		return;
	}
	std::lock_guard<std::mutex> lock(mu);
	if (spare.size() < 4)
	{
		spare.push_back(std::move(bytes));
	}
	bytes = PackedByteArray();
}

static void palette_rd_apply(int64_t raw)
{
	auto *queue = reinterpret_cast<PaletteRdQueue *>(static_cast<std::intptr_t>(raw));
	if (!queue)
	{
		return;
	}
	std::vector<PaletteRdQueue::Op> batch;
	bool dropping = false;
	Ref<Texture2DRD> held;
	{
		std::lock_guard<std::mutex> lock(queue->mu);
		batch.swap(queue->ops);
		queue->scheduled = false;
		dropping = queue->retired;
		held = queue->sampler;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	auto free_rid = [&](RID &rid) {
		if (rd && rid.is_valid())
		{
			rd->free_rid(rid);
		}
		rid = RID();
	};
	for (PaletteRdQueue::Op &op : batch)
	{
		if (op.kind == PaletteRdQueue::Op::Create)
		{
			if (!rd || op.height <= 0)
			{
				queue->create_failed.store(true, std::memory_order_release);
				continue;
			}
			Ref<RDTextureFormat> fmt;
			fmt.instantiate();
			fmt->set_width(W3D_PALETTE_WIDTH);
			fmt->set_height(op.height);
			fmt->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
			fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
				RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
			Ref<RDTextureView> view;
			view.instantiate();
			const RID created = rd->texture_create(fmt, view);
			const RID previous = queue->palette;
			if (!created.is_valid())
			{
				queue->create_failed.store(true, std::memory_order_release);
				continue;
			}
			queue->palette = created;
			queue->create_failed.store(false, std::memory_order_release);
			// Skip the sampler when the node is already going away. A later Release in this batch (or the next) frees the RID.
			if (!dropping && op.sampler.is_valid())
			{
				op.sampler->set_texture_rd_rid(created);
			}
			if (previous.is_valid())
			{
				rd->free_rid(previous);
			}
			free_rid(queue->stage);
			queue->stage_rows = 0;
		}
		else if (op.kind == PaletteRdQueue::Op::Upload)
		{
			if (!rd || !queue->palette.is_valid() || op.bytes.is_empty())
			{
				continue;
			}
			if (op.full)
			{
				rd->texture_update(queue->palette, 0, op.bytes);
				continue;
			}
			// lane PERF-HD: a runs upload's bytes are padded to its stage's rows (one of a few fixed sizes), so the stage is seldom made again
			const int stageRows = op.runs.empty() ? op.rows : (int)(op.bytes.size() / ((int64_t)W3D_PALETTE_WIDTH * 16));
			if (queue->stage_rows != stageRows || !queue->stage.is_valid())
			{
				free_rid(queue->stage);
				Ref<RDTextureFormat> fmt;
				fmt.instantiate();
				fmt->set_width(W3D_PALETTE_WIDTH);
				fmt->set_height(stageRows);
				fmt->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
				fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
				Ref<RDTextureView> view;
				view.instantiate();
				queue->stage = rd->texture_create(fmt, view);
				queue->stage_rows = queue->stage.is_valid() ? stageRows : 0;
			}
			bool copied = false;
			if (queue->stage.is_valid())
			{
				rd->texture_update(queue->stage, 0, op.bytes);
				if (op.runs.empty())
				{
					const Error err = rd->texture_copy(queue->stage, queue->palette, Vector3(0, 0, 0), Vector3(0, (real_t)op.row0, 0),
						Vector3(W3D_PALETTE_WIDTH, (real_t)op.rows, 1), 0, 0, 0, 0);
					copied = err == OK;
				}
				else
				{
					copied = true;
					int packed = 0;
					for (const std::pair<int, int> &run : op.runs)
					{
						const Error err = rd->texture_copy(queue->stage, queue->palette, Vector3(0, (real_t)packed, 0), Vector3(0, (real_t)run.first, 0),
							Vector3(W3D_PALETTE_WIDTH, (real_t)run.second, 1), 0, 0, 0, 0);
						copied = copied && err == OK;
						packed += run.second;
					}
				}
			}
			if (!copied)
			{
				// fallback is empty unless the caller already copied the whole image. Do not read PaletteImage here.
				if (!op.fallback.is_empty())
				{
					rd->texture_update(queue->palette, 0, op.fallback);
				}
				queue->force_full.store(true, std::memory_order_release);
			}
		}
		else if (op.kind == PaletteRdQueue::Op::Release)
		{
			if (held.is_valid())
			{
				held->set_texture_rd_rid(RID());
				held.unref();
			}
			free_rid(queue->palette);
			free_rid(queue->stage);
			queue->stage_rows = 0;
			std::lock_guard<std::mutex> lock(queue->mu);
			queue->sampler.unref();
		}
	}
	if (!dropping)
	{
		for (PaletteRdQueue::Op &op : batch)
		{
			if (op.kind == PaletteRdQueue::Op::Upload)
			{
				queue->give(op.bytes); // the device copied them (texture_update stages its own copy)
			}
		}
	}
	bool destroy = false;
	{
		std::lock_guard<std::mutex> lock(queue->mu);
		if (queue->retired && queue->ops.empty() && !queue->scheduled)
		{
			destroy = true;
		}
	}
	if (destroy)
	{
		delete queue;
	}
}

void PaletteRdQueue::enqueue(Op op, bool retire)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	bool schedule = false;
	{
		std::lock_guard<std::mutex> lock(mu);
		if (retire)
		{
			retired = true;
		}
		if (op.kind == Op::Create && op.sampler.is_valid())
		{
			sampler = op.sampler;
		}
		ops.push_back(std::move(op));
		// call_on_render_thread can run the callable before it returns (the render thread is this thread). Do not hold mu across it:
		// the callable locks mu, and a retired queue deletes itself before the call returns.
		if (!scheduled && rs)
		{
			scheduled = true;
			schedule = true;
		}
	}
	if (schedule)
	{
		rs->call_on_render_thread(callable_mp_static(palette_rd_apply).bind((int64_t)(std::intptr_t)this));
	}
}

W3DInstancer::W3DInstancer()
{
	Epoch = std::chrono::steady_clock::now();
	unsigned hw = std::thread::hardware_concurrency();
	Workers = hw >= 8 ? 4 : (hw >= 4 ? 2 : 1);
}

W3DInstancer::~W3DInstancer()
{
	release_palette_rd();
}

void W3DInstancer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &W3DInstancer::setup);
	ClassDB::bind_method(D_METHOD("add_model", "model_name"), &W3DInstancer::add_model);
	ClassDB::bind_method(D_METHOD("get_model_report", "model"), &W3DInstancer::get_model_report);
	ClassDB::bind_method(D_METHOD("add_instance", "model", "transform", "clip", "start_time", "speed"), &W3DInstancer::add_instance, DEFVAL(String()), DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("set_instance_cast_shadow", "instance", "enabled"), &W3DInstancer::set_instance_cast_shadow);
	ClassDB::bind_method(D_METHOD("set_shadow_receiving", "enabled"), &W3DInstancer::set_shadow_receiving);
	ClassDB::bind_static_method("W3DInstancer", D_METHOD("set_shadow_maps_enabled", "enabled"), &W3DInstancer::set_shadow_maps_enabled);
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
	ClassDB::bind_method(D_METHOD("set_pose_culling", "enabled"), &W3DInstancer::set_pose_culling); // lane PERF-3
	ClassDB::bind_method(D_METHOD("set_extra_pose_frustum", "planes"), &W3DInstancer::set_extra_pose_frustum);
	ClassDB::bind_method(D_METHOD("get_pose_culling"), &W3DInstancer::get_pose_culling);
	ClassDB::bind_method(D_METHOD("get_instance_palette", "instance"), &W3DInstancer::get_instance_palette);
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
	ClassDB::bind_method(D_METHOD("add_model_colored", "model", "kind", "colors"), &W3DInstancer::add_model_colored); // lane CAH-2
	ClassDB::bind_method(D_METHOD("set_instance_infantry_light", "instance", "infantry"), &W3DInstancer::set_instance_infantry_light);
	ClassDB::bind_method(D_METHOD("set_model_glow", "model", "emissive_only"), &W3DInstancer::set_model_glow);
	ClassDB::bind_method(D_METHOD("set_instance_opacity", "instance", "opacity"), &W3DInstancer::set_instance_opacity);
	ClassDB::bind_method(D_METHOD("set_instance_occlusion", "instance", "occluder", "slot"), &W3DInstancer::set_instance_occlusion); // lane OCCLUDE-1
	ClassDB::bind_method(D_METHOD("get_occlusion_pass_counts"), &W3DInstancer::get_occlusion_pass_counts);
	ClassDB::bind_method(D_METHOD("get_auto_update"), &W3DInstancer::get_auto_update);
	ClassDB::bind_method(D_METHOD("probe_shaders", "origin"), &W3DInstancer::probe_shaders);
}

void W3DInstancer::_ready()
{
	set_process(AutoUpdate);
}

void W3DInstancer::set_alpha_override(bool enabled)
{
	AlphaOverride = enabled;
	if (Materials)
	{
		Materials->Set_Alpha_Override(enabled);
	}
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

void W3DInstancer::set_model_glow(int64_t model, bool emissive_only)
{
	if (model < 0 || (size_t)model >= Models.size())
	{
		warn("set_model_glow: unknown model " + std::to_string(model));
		return;
	}
	for (DrawItem &d : Models[(size_t)model]->Draws)
	{
		bool glow = true;
		if (emissive_only)
		{
			// MeshClass::Render: the mesh's first vertex material, emissive above 0.01 in any channel
			glow = false;
			for (const MeshDrawSurface &surface : d.Mesh->Data.Surfaces)
			{
				if (surface.HasVertexMaterial)
				{
					const W3dRGBStruct &e = surface.VertexMaterial.Emissive;
					glow = e.R / 255.0f > 0.01f || e.G / 255.0f > 0.01f || e.B / 255.0f > 0.01f;
					break;
				}
			}
		}
		const uint32_t layers = d.Node->get_layer_mask();
		d.Node->set_layer_mask(glow ? (layers | GlowPass::kGlowLayer) : (layers & ~GlowPass::kGlowLayer));
	}
}

void W3DInstancer::set_instance_infantry_light(int64_t instance, bool infantry)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	Instances[(size_t)instance].InfantryLight = infantry;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_sway(int64_t instance, int type)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || type < 0 || type > 10) return;
	Instances[(size_t)instance].Sway = type;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_fogged(int64_t instance, bool fogged)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || Instances[(size_t)instance].Fogged == fogged) return;
	Instances[(size_t)instance].Fogged = fogged;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_scene_fog(int64_t instance, bool fog)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || Instances[(size_t)instance].NoSceneFog == !fog) return;
	Instances[(size_t)instance].NoSceneFog = !fog;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_occlusion(int64_t instance, bool occluder, int slot)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	if (slot < 0 || slot >= W3D_OCCLUSION_SLOTS)
	{
		warn("set_instance_occlusion: slot " + std::to_string(slot) + " is out of 0 .. " + std::to_string(W3D_OCCLUSION_SLOTS - 1) + ": not an occludee");
		slot = 0;
	}
	Instance &inst = Instances[(size_t)instance];
	if (inst.Occluder == occluder && inst.OcclusionSlot == slot) return;
	inst.Occluder = occluder;
	inst.OcclusionSlot = slot;
	if (occluder || slot != 0)
	{
		for (DrawItem &d : Models[(size_t)inst.Model]->Draws)
		{
			ensure_occlusion_passes(*d.Mesh, occluder, slot != 0);
		}
	}
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::set_instance_cast_shadow(int64_t instance, bool enabled)
{
	if (instance < 0 || instance >= (int64_t)Instances.size()) return;
	Instance &inst = Instances[(size_t)instance];
	if (inst.Removed || inst.CastShadow == enabled) return;
	inst.CastShadow = enabled;
	PoseDirty = true;
	mark_model(instance);
}

void W3DInstancer::ensure_occlusion_passes(MeshGpu &gpu, bool mark, bool color)
{
	if ((!mark || gpu.HasMark) && (!color || gpu.HasColor)) return;
	gpu.HasMark = gpu.HasMark || mark;
	gpu.HasColor = gpu.HasColor || color;
	gpu.MarkPasses.resize(gpu.Materials.size());
	gpu.ColorPasses.resize(gpu.Materials.size());
	for (size_t i = 0; i < gpu.Materials.size() && i < gpu.Keys.size(); ++i)
	{
		const W3DShaderKey &base = gpu.Keys[i];
		// retail renders the whole object into the stencil; here the opaque surfaces (the blended ones draw in the transparent pass, sorted with
		// the passes, and their alpha mostly leaves no shape)
		if (base.Blend != W3D_GODOT_BLEND_OPAQUE || base.AlphaOverride || !base.DepthWrite || base.DepthTestDisabled) continue;
		auto make = [&](int pass, int priority) {
			W3DShaderKey k = base;
			k.OcclusionPass = pass;
			Ref<ShaderMaterial> m = gpu.Materials[i]->duplicate(); // the surface's textures and colours: the passes keep its alpha test
			m->set_shader(Materials->Get_Shader(k));
			m->set_render_priority(priority);
			m->set_next_pass(Ref<Material>());
			AllMaterials.push_back(m); // the palette binding follows (rebuild_layout)
			m->set_shader_parameter("w3d_palette", palette_sampler());
			return m;
		};
		if (gpu.HasMark && gpu.MarkPasses[i].is_null()) gpu.MarkPasses[i] = make(W3D_OCCLUSION_PASS_MARK, W3D_OCCLUSION_PRIORITY_MARK);
		if (gpu.HasColor && gpu.ColorPasses[i].is_null()) gpu.ColorPasses[i] = make(W3D_OCCLUSION_PASS_COLOR, W3D_OCCLUSION_PRIORITY_COLOR);
		// the chain: surface -> mark -> colour (either may be absent)
		const Ref<ShaderMaterial> &mk = gpu.MarkPasses[i];
		const Ref<ShaderMaterial> &co = gpu.ColorPasses[i];
		if (mk.is_valid())
		{
			mk->set_next_pass(co);
			gpu.Materials[i]->set_next_pass(mk);
		}
		else
		{
			gpu.Materials[i]->set_next_pass(co);
		}
	}
}

bool W3DInstancer::instance_outside(int64_t instance, const std::vector<Plane> &frustum)
{
	if (frustum.empty() || instance < 0 || instance >= (int64_t)Instances.size() || Instances[(size_t)instance].Removed) return false;
	const Instance &inst = Instances[(size_t)instance];
	const Transform3D nodeXf = is_inside_tree() ? get_global_transform() : get_transform();
	const Vector3 centre = nodeXf.xform(inst.Xf.origin);
	const Vector3 sc = inst.Xf.basis.get_scale_abs();
	const Vector3 ns = nodeXf.basis.get_scale_abs();
	const real_t r = cull_radius(*Models[(size_t)inst.Model]) * std::max(sc.x, std::max(sc.y, sc.z)) * std::max(ns.x, std::max(ns.y, ns.z));
	for (const Plane &pl : frustum)
	{
		if (pl.distance_to(centre) > r) return true;
	}
	return false;
}

Dictionary W3DInstancer::get_occlusion_pass_counts() const
{
	int marks = 0, colors = 0;
	std::set<const MeshGpu *> seen;
	for (const std::unique_ptr<Model> &mp : Models)
	{
		for (const DrawItem &d : mp->Draws)
		{
			if (!d.Mesh || !seen.insert(d.Mesh.get()).second) continue;
			for (const Ref<ShaderMaterial> &m : d.Mesh->MarkPasses) marks += m.is_valid() ? 1 : 0;
			for (const Ref<ShaderMaterial> &m : d.Mesh->ColorPasses) colors += m.is_valid() ? 1 : 0;
		}
	}
	Dictionary r;
	r["marks"] = marks;
	r["colors"] = colors;
	return r;
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
// client pool were three sets of threads competing for the cores). Each slice writes only its own instances / draw items. Lane PERF-3: `Workers` sets the
// slice size (four slices per worker, at least 32 items each) instead of the number of slices, so the pool's threads share the work out as they come free.
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
	// lane PERF-3: four chunks per thread (at least 32 items each): the threads that finish early take the remaining chunks, so one slow slice (blended
	// poses, many pivots) or a late worker no longer holds the render thread
	const size_t slice = std::max<size_t>(32, (count + (size_t)Workers * 4 - 1) / ((size_t)Workers * 4));
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
		Source = fs->renderW3DSource(); // lane SDK-0: the asset packs in front (render side only)
		Assets.reset(new WW3DAssetManager(*Source));
		Materials.reset(new W3DMaterialFactory(*fs->archive_fs()));
		Materials->Set_Asset_Packs(fs->assetPacks().empty() ? nullptr : &fs->assetPacks());
		Materials->Set_Alpha_Override(AlphaOverride); // lane BUILD-5 r3
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
	const std::string key = AsciiStringUtil::lowered(mesh.Get_Name()) + BuildSuffix; // lane CAH-2: a colour set's meshes have their own materials
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
			// Godot's BINORMAL = cross(NORMAL, TANGENT.xyz) * w is made the file's bitangent: retail's vertex buffer holds it raw and
			// normalmapped.fxo blob 22 negates it (the generated shader's fx = -BINORMAL); S-023
			tangents.set(v * 4 + 0, d.Tangent[v * 3]);
			tangents.set(v * 4 + 1, d.Tangent[v * 3 + 1]);
			tangents.set(v * 4 + 2, d.Tangent[v * 3 + 2]);
			tangents.set(v * 4 + 3, Godot_Tangent_Sign(&d.Normal[v * 3], &d.Tangent[v * 3], &d.Bitangent[v * 3]));
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
		gpu->Keys.push_back(mat.Key);
		for (const W3DMapperBinding &b : mat.Mappers) gpu->Mappers.push_back(b);
		for (const std::string &n : mat.Notes) gpu->Notes.push_back(n);
		for (const std::string &e : mat.Errors) gpu->Errors.push_back(e);
		if (surf.Kind == MATERIAL_FX) gpu->UsesFx = true;
		if (mat.Key.HouseColor && !houseStopped)
		{
			houseStopped = true;
			W3DStopHit h;
			h.Id = "S-119";
			h.Message = W3D_Stop_Message("S-119", mesh.Get_Name() + (mat.Key.HouseColorBaked
			 ? ": house texels recoloured before filtering, combined after the light as retail's DefaultW3D.fx / fixed-function rules do; the owner's colour source is inferred (S-119)"
			 : ": instance colour recolours the filtered house texel as RW 0x531C77 kind 1 (retail recolours before filtering), combined after the light; the owner's colour source is inferred (S-119)"));
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

int64_t W3DInstancer::add_model_colored(const String &model_name, int64_t kind, const PackedInt64Array &colors)
{
	// lane CAH-2: RW 0x54BDE0 / 0x535BCF: the render object "#<model>#<options>" whose house colour textures are recoloured per texel (W3DMaterialFactory::
	// Recolored_House_Texture); its meshes are built again under that name so their materials take the baked textures
	HouseColorParams params;
	params.kind = (int)kind;
	for (int i = 0; i < 3; ++i)
	{
		params.colors[i] = i < colors.size() ? (std::uint32_t)colors[i] : 0u;
	}
	char opt[96];
	std::snprintf(opt, sizeof(opt), "#%d&%d&%d&%d", params.kind, (int)params.colors[0], (int)params.colors[1], (int)params.colors[2]);
	if (Materials)
	{
		Materials->Set_House_Params(params);
	}
	BuildSuffix = opt;
	const int64_t id = add_model(model_name);
	BuildSuffix.clear();
	if (Materials)
	{
		Materials->Set_House_Params(HouseColorParams());
	}
	return id;
}

std::string W3DInstancer::Texture_Swap_Suffix(const std::vector<std::pair<std::string, std::string>> &swaps)
{
	if (swaps.empty())
	{
		return std::string();
	}
	std::string suffix = "#tex";
	for (const std::pair<std::string, std::string> &swap : swaps)
	{
		suffix += "&" + AsciiStringUtil::lowered(swap.first) + "=" + AsciiStringUtil::lowered(swap.second);
	}
	return suffix;
}

int64_t W3DInstancer::add_model(const String &model_name)
{
	if (PendingSwaps.empty())
	{
		return build_model(model_name);
	}
	// stop S-5015: the swapped model's meshes are cached under their own name (after a colour set's suffix: add_model_colored sets that first)
	std::vector<std::pair<std::string, std::string>> swaps;
	swaps.swap(PendingSwaps);
	const std::string savedSuffix = BuildSuffix;
	BuildSuffix += Texture_Swap_Suffix(swaps);
	if (Materials)
	{
		Materials->Set_Texture_Swaps(swaps);
	}
	const int64_t id = build_model(model_name);
	if (Materials)
	{
		Materials->Set_Texture_Swaps({});
	}
	BuildSuffix = savedSuffix;
	return id;
}

int64_t W3DInstancer::build_model(const String &model_name)
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
			item.MM->set_custom_aabb(kMultiMeshBounds); // lane PERF-HD: see kMultiMeshBounds
			item.MM->set_use_custom_data(true);
			item.MM->set_mesh(gpu->Mesh);
			item.MM->set_instance_count(0);
			item.Node = memnew(MultiMeshInstance3D);
			item.Node->set_name(toGodot(name + "_" + sub.Name));
			item.Node->set_multimesh(item.MM);
			item.Node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			item.Node->set_instance_shader_parameter("w3d_receive_shadows", ShadowReceiving);
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
	inst.PaletteAnimated = !clip.is_empty();
	inst.DrawPoseState.resize(Models[(size_t)model]->Draws.size());
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
			(inst.PaletteAnimated ? PaletteAnimFree : PaletteFree)[inst.PaletteSize].push_back(inst.PaletteOffset);
		}

		inst.PaletteSize = 0;
		inst.PaletteAnimated = false;
		inst.Pose = HTreePose();
		decltype(inst.DrawPoseState)().swap(inst.DrawPoseState);
	}
	PoseDirty = true;
	return true;
}

void W3DInstancer::mark_model(int64_t instance)
{
	Models[(size_t)Instances[(size_t)instance].Model]->BuffersDirty = true;
}

void W3DInstancer::note_palette_rows(int offset, int texels)
{
	if (texels <= 0 || PaletteHeight <= 0)
	{
		return;
	}
	const int r0 = offset / W3D_PALETTE_WIDTH;
	const int r1 = (offset + texels - 1) / W3D_PALETTE_WIDTH;
	// lane PERF-HD: the rows themselves, for an upload of the dirty runs only (culled poses leave most of the span between them unchanged)
	if (PaletteDirtyMap.size() != (size_t)PaletteHeight)
	{
		PaletteDirtyMap.assign((size_t)PaletteHeight, 0);
	}
	for (int r = std::max(0, r0); r <= std::min(PaletteHeight - 1, r1); ++r)
	{
		PaletteDirtyMap[(size_t)r] = 1;
	}
	if (PaletteDirtyRow1 < PaletteDirtyRow0)
	{
		PaletteDirtyRow0 = r0;
		PaletteDirtyRow1 = r1;
	}
	else
	{
		PaletteDirtyRow0 = std::min(PaletteDirtyRow0, r0);
		PaletteDirtyRow1 = std::max(PaletteDirtyRow1, r1);
	}
	PaletteChanged = true;
}

void W3DInstancer::allocate_palette(Instance &inst)
{
	const int size = inst.Pose.Num_Pivots() * W3D_PALETTE_TEXELS_PER_PIVOT;
	inst.PaletteSize = size;
	std::map<int, std::vector<int>> &freeList = inst.PaletteAnimated ? PaletteAnimFree : PaletteFree;
	auto it = freeList.find(size);
	if (it != freeList.end() && !it->second.empty())
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

void W3DInstancer::adopt_animated_palette(Instance &inst)
{
	if (inst.PaletteAnimated || inst.Removed)
	{
		return;
	}
	inst.PaletteAnimated = true;
	if (inst.PaletteSize == 0)
	{
		return;
	}
	const int size = inst.PaletteSize;
	const int old = inst.PaletteOffset;
	PaletteFree[size].push_back(old);
	int neu = -1;
	auto it = PaletteAnimFree.find(size);
	if (it != PaletteAnimFree.end() && !it->second.empty())
	{
		neu = it->second.back();
		it->second.pop_back();
	}
	else
	{
		neu = PaletteUsed;
		PaletteUsed += size;
		if (PaletteUsed > PaletteHeight * W3D_PALETTE_WIDTH)
		{
			grow_palette(PaletteUsed);
		}
	}
	inst.PaletteOffset = neu;
	if (PaletteImage.is_valid() && old != neu)
	{
		std::uint8_t *bytes = PaletteImage->ptrw();
		std::memmove(bytes + (size_t)neu * 16, bytes + (size_t)old * 16, (size_t)size * 16);
	}
	note_palette_rows(neu, size);
	if (inst.Model >= 0 && (size_t)inst.Model < Models.size())
	{
		Models[(size_t)inst.Model]->BuffersDirty = true;
	}
}

void W3DInstancer::grow_palette(int texels)
{
	const int capacity = std::max(texels, (PaletteHeight * W3D_PALETTE_WIDTH * 3) / 2);
	const int height = std::max(1, (capacity + W3D_PALETTE_WIDTH - 1) / W3D_PALETTE_WIDTH);
	// lane PERF-3: the new image takes the old one's texels and zeros after them (the palette lives in the image's buffer)
	const Ref<Image> old = PaletteImage;
	const size_t oldBytes = old.is_valid() ? (size_t)PaletteHeight * W3D_PALETTE_WIDTH * 16 : 0;
	PaletteHeight = height;
	PaletteImage = Image::create_empty(W3D_PALETTE_WIDTH, PaletteHeight, false, Image::FORMAT_RGBAF);
	{
		std::uint8_t *dst = PaletteImage->ptrw();
		const size_t bytes = (size_t)PaletteHeight * W3D_PALETTE_WIDTH * 16;
		if (oldBytes > 0)
		{
			std::memcpy(dst, old->ptr(), oldBytes);
		}
		std::memset(dst + oldBytes, 0, bytes - oldBytes);
	}
	bind_palette_gpu();
	PaletteGrown = true;
	PaletteFullUpload = true;
	PaletteChanged = true;
	LayoutDirty = true; // the materials take the new texture in rebuild_layout
}

void W3DInstancer::release_palette_rd()
{
	// The node may be freed before the render thread runs. RIDs live in PaletteQueue, and the queued callable does not touch this.
	PaletteOnRd = false;
	PaletteRdQueue *queue = PaletteQueue;
	PaletteQueue = nullptr;
	if (queue)
	{
		PaletteRdQueue::Op op;
		op.kind = PaletteRdQueue::Op::Release;
		queue->enqueue(std::move(op), true);
	}
	PaletteRD.unref();
}

Ref<Texture2D> W3DInstancer::palette_sampler() const
{
	if (PaletteRD.is_valid())
	{
		return PaletteRD;
	}
	return PaletteTexture;
}

void W3DInstancer::bind_palette_gpu()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	if (rd && PaletteImage.is_valid() && PaletteHeight > 0)
	{
		if (PaletteRD.is_null())
		{
			PaletteRD.instantiate();
		}
		if (!PaletteQueue)
		{
			PaletteQueue = new PaletteRdQueue();
		}
		PaletteRdQueue::Op op;
		op.kind = PaletteRdQueue::Op::Create;
		op.height = PaletteHeight;
		op.sampler = PaletteRD;
		PaletteQueue->enqueue(std::move(op), false);
		PaletteOnRd = true;
		PaletteTexture.unref();
		PaletteFullUpload = true;
		return;
	}
	// Compatibility has no RenderingDevice. ImageTexture::update stays on this thread.
	release_palette_rd();
	PaletteRD.unref();
	PaletteTexture = ImageTexture::create_from_image(PaletteImage);
	PaletteFullUpload = true;
}

void W3DInstancer::upload_palette()
{
	PaletteRowsUploaded = 0;
	PaletteUploadFull = false;
	// Check a failed texture create before any no-change return. A static frame has no dirty rows, and
	// update_now rebuilds materials before this call, so LayoutDirty is what makes them drop PaletteRD.
	if (PaletteOnRd && PaletteQueue && PaletteQueue->create_failed.load(std::memory_order_acquire))
	{
		release_palette_rd();
		PaletteRD.unref();
		if (PaletteImage.is_valid())
		{
			PaletteTexture = ImageTexture::create_from_image(PaletteImage);
		}
		LayoutDirty = true;
	}
	// A rejected partial copy is completed here, on the caller thread, even when this frame wrote no new rows.
	const bool copyRejected = PaletteOnRd && PaletteQueue && PaletteQueue->force_full.load(std::memory_order_acquire);
	if ((!PaletteChanged && !PaletteFullUpload && !copyRejected) || PaletteImage.is_null() || PaletteHeight <= 0)
	{
		PaletteChanged = false;
		return;
	}
	const bool full = PaletteFullUpload || copyRejected || PaletteDirtyRow1 < PaletteDirtyRow0;
	const int row0 = full ? 0 : std::max(0, PaletteDirtyRow0);
	const int row1 = full ? PaletteHeight - 1 : std::min(PaletteHeight - 1, PaletteDirtyRow1);
	const int rows = row1 >= row0 ? row1 - row0 + 1 : 0;
	PaletteRowsUploaded = rows;
	PaletteUploadFull = full || rows == PaletteHeight;
	auto clear = [&]() {
		PaletteChanged = false;
		PaletteFullUpload = false;
		PaletteDirtyRow0 = 0;
		PaletteDirtyRow1 = -1;
		std::fill(PaletteDirtyMap.begin(), PaletteDirtyMap.end(), (std::uint8_t)0);
	};
	if (rows <= 0)
	{
		clear();
		return;
	}
	if (PaletteOnRd && PaletteQueue)
	{
		const bool uploadFull = full || (row0 == 0 && rows == PaletteHeight);
		PaletteRdQueue::Op op;
		op.kind = PaletteRdQueue::Op::Upload;
		op.full = uploadFull;
		op.row0 = row0;
		op.rows = rows;
		// lane PERF-HD: a partial upload sends the dirty runs (gaps of up to kRunGap clean rows bridged) packed into a stage of a few fixed sizes
		constexpr int kRunGap = 4;
		constexpr int kStageRowStep = 64;
		const size_t rowBytes = (size_t)W3D_PALETTE_WIDTH * 16;
		if (!uploadFull && PaletteDirtyMap.size() == (size_t)PaletteHeight)
		{
			int total = 0;
			for (int r = row0; r <= row1;)
			{
				if (!PaletteDirtyMap[(size_t)r])
				{
					++r;
					continue;
				}
				int last = r, gap = 0;
				for (int k = r + 1; k <= row1; ++k)
				{
					if (PaletteDirtyMap[(size_t)k])
					{
						last = k;
						gap = 0;
					}
					else if (++gap > kRunGap)
					{
						break;
					}
				}
				op.runs.emplace_back(r, last - r + 1);
				total += last - r + 1;
				r = last + 1;
			}
			if (!op.runs.empty())
			{
				// the stage is sized in steps of a quarter of the next power of two (at least kStageRowStep), so it is seldom made again
				int pow2 = kStageRowStep;
				while (pow2 < total)
				{
					pow2 *= 2;
				}
				const int step = std::max(kStageRowStep, pow2 / 4);
				const int stageRows = std::min(((total + step - 1) / step) * step, std::max(PaletteHeight, total));
				op.bytes = PaletteQueue->take((int64_t)stageRows * (int64_t)rowBytes);
				std::uint8_t *dst = op.bytes.ptrw();
				for (const std::pair<int, int> &run : op.runs)
				{
					std::memcpy(dst, PaletteImage->ptr() + (size_t)run.first * rowBytes, (size_t)run.second * rowBytes);
					dst += (size_t)run.second * rowBytes;
				}
				op.row0 = op.runs.front().first;
				op.rows = total;
				PaletteRowsUploaded = total;
			}
			else
			{
				op.runs.clear();
			}
		}
		if (op.runs.empty())
		{
			const int64_t nbytes = uploadFull ? (int64_t)PaletteHeight * W3D_PALETTE_WIDTH * 16 : (int64_t)rows * W3D_PALETTE_WIDTH * 16;
			const std::uint8_t *src = PaletteImage->ptr() + (uploadFull ? 0 : (size_t)row0 * W3D_PALETTE_WIDTH * 16);
			op.bytes = PaletteQueue->take(nbytes);
			std::memcpy(op.bytes.ptrw(), src, (size_t)nbytes);
		}
		// A partial copy that can still succeed copies only the dirty rows. The whole image is copied
		// above when uploadFull is set (the copy will not be attempted, or an earlier copy failed).
		if (uploadFull)
		{
			PaletteQueue->force_full.store(false, std::memory_order_release);
			PaletteUploadFull = true;
			PaletteRowsUploaded = PaletteHeight;
		}
		PaletteQueue->enqueue(std::move(op), false);
		if (!uploadFull && PaletteQueue->force_full.load(std::memory_order_acquire))
		{
			// This thread ran the copy and it failed. Copy the whole palette now, still off the render thread.
			PaletteRdQueue::Op fullOp;
			fullOp.kind = PaletteRdQueue::Op::Upload;
			fullOp.full = true;
			fullOp.row0 = 0;
			fullOp.rows = PaletteHeight;
			const int64_t all = (int64_t)PaletteHeight * W3D_PALETTE_WIDTH * 16;
			fullOp.bytes = PaletteQueue->take(all);
			std::memcpy(fullOp.bytes.ptrw(), PaletteImage->ptr(), (size_t)all);
			PaletteQueue->force_full.store(false, std::memory_order_release);
			PaletteQueue->enqueue(std::move(fullOp), false);
			PaletteUploadFull = true;
			PaletteRowsUploaded = PaletteHeight;
		}
		clear();
		return;
	}
	if (PaletteTexture.is_valid())
	{
		// Compatibility has no partial texture update. The image still holds every pose; this copies all of it.
		PaletteTexture->update(PaletteImage);
		PaletteUploadFull = true;
		PaletteRowsUploaded = PaletteHeight;
	}
	clear();
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
	adopt_animated_palette(inst);
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
	return set_instance_pose_keyed(instance, clip0, nullptr, frame0, clip1, nullptr, frame1, percentage);
}

bool W3DInstancer::set_instance_pose_keyed(int64_t instance, const std::string &clip0, const void *key0, double frame0, const std::string &clip1, const void *key1,
	double frame1, double percentage)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || !Assets) return false;
	Instance &inst = Instances[(size_t)instance];
	Model &m = *Models[(size_t)inst.Model];
	const HAnimClass *a0 = nullptr;
	const HAnimClass *a1 = nullptr;
	// lane PERF-3: the instance's last names first (a string compare instead of the model's hash lookup); only resolved clips are kept, so a failing name
	// still reports on every call
	auto resolve = [&](int k, const std::string &clip, const void *key) -> const HAnimClass * {
		if (inst.MemoAnim[k] && key != nullptr && key == inst.MemoKey[k])
		{
			return inst.MemoAnim[k]; // the key stands for the name the memo holds
		}
		if (inst.MemoAnim[k] && clip == inst.MemoClip[k])
		{
			inst.MemoKey[k] = key;
			return inst.MemoAnim[k];
		}
		const HAnimClass *a = resolve_clip_cached(m, clip);
		if (a)
		{
			inst.MemoClip[k] = clip;
			inst.MemoAnim[k] = a;
			inst.MemoKey[k] = key;
		}
		return a;
	};
	if (!clip0.empty())
	{
		a0 = resolve(0, clip0, key0);
		if (!a0) return false;
	}
	if (!clip1.empty())
	{
		a1 = resolve(1, clip1, key1);
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
	adopt_animated_palette(inst);
	return true;
}

bool W3DInstancer::set_instance_bone_spins_native(int64_t instance, const std::vector<std::string> &bones, const std::vector<float> &angles)
{
	if (instance < 0 || instance >= (int64_t)Instances.size() || bones.size() != angles.size()) return false;
	Instance &inst = Instances[(size_t)instance];
	const Model &m = *Models[(size_t)inst.Model];
	if (!m.Proto || !m.Proto->Tree) return false;
	std::vector<std::pair<int, float>> spins;
	for (size_t i = 0; i < bones.size(); ++i)
	{
		const int pivot = m.Proto->Tree->Get_Bone_Index(bones[i]);
		if (pivot > 0)
		{
			spins.emplace_back(pivot, angles[i]);
		}
	}
	if (spins == inst.BoneSpins) return true;
	inst.BoneSpins = std::move(spins);
	inst.PoseStale = true;
	PoseDirty = true;
	if (!inst.BoneSpins.empty())
	{
		adopt_animated_palette(inst);
	}
	return true;
}

void W3DInstancer::set_instance_pose_share(int64_t instance, float maxFrameDelta)
{
	if (instance < 0 || instance >= (int64_t)Instances.size())
	{
		return;
	}
	Instances[(size_t)instance].ShareDelta = maxFrameDelta;
}

namespace
{
// lane COMBAT-4: the pivot `p` of the evaluated pose turned by Rotate_Y(angle) in its own frame (Control_Bone after the animation): its world becomes W * R and
// every pivot under it follows (W' * W^-1 * child); the hierarchy lists parents before children
void spinPivot(const HTreeClass &tree, HTreePose &pose, int p, float angle)
{
	const Matrix3D w = pose.Transform[(size_t)p];
	Matrix3D r;
	const float c = std::cos(angle), s = std::sin(angle);
	r.Row[0][0] = c;
	r.Row[0][2] = s;
	r.Row[2][0] = -s;
	r.Row[2][2] = c;
	Matrix3D wNew;
	Matrix3D::Multiply(w, r, &wNew);
	// W^-1 of a rigid transform: the transposed rotation and -R^T t
	Matrix3D inv;
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			inv.Row[i][j] = w.Row[j][i];
		}
	}
	for (int i = 0; i < 3; ++i)
	{
		inv.Row[i][3] = -(inv.Row[i][0] * w.Row[0][3] + inv.Row[i][1] * w.Row[1][3] + inv.Row[i][2] * w.Row[2][3]);
	}
	Matrix3D delta;
	Matrix3D::Multiply(wNew, inv, &delta);
	pose.Transform[(size_t)p] = wNew;
	std::vector<std::uint8_t> under((size_t)pose.Num_Pivots(), 0);
	under[(size_t)p] = 1;
	for (int i = p + 1; i < pose.Num_Pivots(); ++i)
	{
		const int parent = tree.Get_Pivot(i).ParentIdx;
		if (parent >= 0 && under[(size_t)parent])
		{
			under[(size_t)i] = 1;
			Matrix3D moved;
			Matrix3D::Multiply(delta, pose.Transform[(size_t)i], &moved);
			pose.Transform[(size_t)i] = moved;
		}
	}
}
} // namespace

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
	if (PaletteImage.is_null() || (PaletteTexture.is_null() && PaletteRD.is_null()))
	{
		grow_palette(std::max(PaletteUsed, 1));
	}
	for (const Ref<ShaderMaterial> &m : AllMaterials)
	{
		m->set_shader_parameter("w3d_palette", palette_sampler());
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
	evaluate_animation_pose(inst);
	if (!inst.BoneSpins.empty())
	{
		const HTreeClass &tree = *Models[(size_t)inst.Model]->Proto->Tree;
		for (const std::pair<int, float> &sp : inst.BoneSpins)
		{
			if (sp.first < inst.Pose.Num_Pivots())
			{
				spinPivot(tree, inst.Pose, sp.first, sp.second);
			}
		}
	}
}

void W3DInstancer::evaluate_animation_pose(Instance &inst) const
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

float W3DInstancer::cull_radius(Model &m) const
{
	if (m.CullRadius >= 0.0f)
	{
		return m.CullRadius;
	}
	// the farthest pivot of the bind pose plus the largest mesh extent (skin vertices are in bone space, rigid ones hang on their pivot), with room for
	// animations that reach beyond the bind pose (a swing, a fall, a mount's stride): 1.5 times that plus 30 units (stop S-1730: our bound, not retail's)
	float pivots = 0.0f;
	if (m.Proto && m.Proto->Tree)
	{
		HTreePose base;
		m.Proto->Tree->Base_Pose(Matrix3D(), base);
		for (int i = 0; i < base.Num_Pivots(); ++i)
		{
			const ::Vector3 t = base.Transform[(size_t)i].Get_Translation();
			pivots = std::max(pivots, std::sqrt(t.X * t.X + t.Y * t.Y + t.Z * t.Z));
		}
	}
	float extent = 0.0f;
	for (const DrawItem &d : m.Draws)
	{
		const float *lo = d.Mesh->Data.BoundsMin, *hi = d.Mesh->Data.BoundsMax;
		const float x = std::max(std::fabs(lo[0]), std::fabs(hi[0])), y = std::max(std::fabs(lo[1]), std::fabs(hi[1])), z = std::max(std::fabs(lo[2]), std::fabs(hi[2]));
		extent = std::max(extent, std::sqrt(x * x + y * y + z * z));
	}
	m.CullRadius = W3D_Pose_Cull_Radius(pivots, extent); // S-1730
	return m.CullRadius;
}

PackedFloat32Array W3DInstancer::get_instance_palette(int64_t instance) const
{
	PackedFloat32Array out;
	if (instance < 0 || instance >= (int64_t)Instances.size() || PaletteImage.is_null())
	{
		return out;
	}
	const Instance &inst = Instances[(size_t)instance];
	if (inst.Removed || inst.PaletteSize == 0)
	{
		return out;
	}
	out.resize((int64_t)inst.PaletteSize * 4);
	const PackedByteArray data = PaletteImage->get_data();
	std::memcpy(out.ptrw(), data.ptr() + (size_t)inst.PaletteOffset * 16, (size_t)inst.PaletteSize * 16);
	return out;
}

void W3DInstancer::ensure_pose(int64_t instance) const
{
	const Instance &inst = Instances[(size_t)instance];
	if (PoseCulling && inst.PoseStale && !inst.Removed)
	{
		Instance &mut = const_cast<Instance &>(inst); // the pose is a cache of the instance's inputs: evaluating it does not change what the instance is
		evaluate_pose(mut);
		if (!mut.QueryPosePending)
		{
			Models[(size_t)inst.Model]->QueriedPoseIds.push_back((int)instance);
			mut.QueryPosePending = true;
		}
	}
}

void W3DInstancer::write_palette(const Instance &inst)
{
	const int pivots = inst.Pose.Num_Pivots();
	if (!PaletteWrite || inst.PaletteSize == 0)
	{
		return;
	}
	float *out = PaletteWrite + (size_t)inst.PaletteOffset * 4;
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
	// Instance record changes still rewrite the model. A pose alone only rewrites affected draw items: bone matrices live in the palette,
	// while visibility/fade enter these records. Sorted draws keep the conservative pose/camera invalidation for animated pivot depths.
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
		const bool consumeQueries = !m.QueriedPoseIds.empty() && (m.BuffersDirty || m.PoseEvaluated || (m.HasSorted && cameraMoved));
		if (consumeQueries && !m.BuffersDirty)
		{
			// A query can update a culled Pose outside StaleScratch. The baseline would consume it when ANY pose in this model
			// evaluates, or its sorted camera changes. Compare only these queued candidates, never all historical instances.
			for (int id : m.QueriedPoseIds)
			{
				const Instance &inst = Instances[(size_t)id];
				if (inst.Removed) continue;
				const HTreePose &pose = pose_source(inst).Pose; // lane PERF-HD
				for (size_t k = 0; k < m.Draws.size(); ++k)
				{
					DrawItem &d = m.Draws[k];
					if (d.PoseDirty || d.Mesh->HasSorted) continue;
					const size_t bone = (size_t)d.Sub->BoneIndex;
					if (inst.DrawPoseState[k] != std::make_pair(pose.IsVisible[bone], pose.PivotFade[bone]))
					{
						d.PoseDirty = true;
					}
				}
			}
		}
		bool wrote = false;
		m.Dithered = 0;
		m.SortedItems = 0;
		for (DrawItem &d : m.Draws)
		{
			if (!m.BuffersDirty && !d.PoseDirty && !(d.Mesh->HasSorted && cameraMoved))
			{
				m.Dithered += d.Dithered;
				m.SortedItems += d.HasBatch ? 1 : 0;
				visible += (int)d.Order.size();
				continue;
			}
			wrote = true;
			d.PoseDirty = false;
			d.Dithered = 0;
			const int bone = d.Sub->BoneIndex;
			const size_t drawIndex = (size_t)(&d - m.Draws.data());
			const bool rigid = !d.Mesh->Data.Skin;
			std::vector<int> &ids = d.Order;
			ids.clear();
			for (int id : m.InstanceIds)
			{
				Instance &inst = Instances[(size_t)id];
				if (inst.Removed) continue;
				const HTreePose &pose = pose_source(inst).Pose; // lane PERF-HD: a shared member is drawn with its group's pose
				inst.DrawnOffset = pose_source(inst).PaletteOffset;
				// Cache every consumed pose before membership filters, including queried culled/hidden/zero-fade instances.
				if (!d.Mesh->HasSorted)
				{
					inst.DrawPoseState[drawIndex] = std::make_pair(pose.IsVisible[(size_t)bone], pose.PivotFade[(size_t)bone]);
				}
				if (!inst.HiddenSubs.empty() && inst.HiddenSubs[(size_t)d.SubIndex]) continue;
				if (!pose.IsVisible[(size_t)bone]) continue;
				if (pose.PivotFade[(size_t)bone] * inst.Opacity <= 0.0f) continue;
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
						const ::Vector3 t = pose_source(inst).Pose.Transform[(size_t)bone].Get_Translation();
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
				const Instance &src = pose_source(inst); // lane PERF-HD
				const float fade = src.Pose.PivotFade[(size_t)bone] * inst.Opacity; // PROJ-2: the drawable's opacity
				if (d.Mesh->HasOpaque && fade < 0.999f) ++d.Dithered;
				float *o = buf + (size_t)count * 16;
				const Basis &b = inst.Xf.basis;
				o[0] = b.rows[0][0]; o[1] = b.rows[0][1]; o[2] = b.rows[0][2]; o[3] = inst.Xf.origin.x;
				o[4] = b.rows[1][0]; o[5] = b.rows[1][1]; o[6] = b.rows[1][2]; o[7] = inst.Xf.origin.y;
				o[8] = b.rows[2][0]; o[9] = b.rows[2][1]; o[10] = b.rows[2][2]; o[11] = inst.Xf.origin.z;
				o[12] = inst.InfantryLight ? -(float)(src.PaletteOffset + 1) : (float)src.PaletteOffset;
				o[13] = fade;
				o[14] = inst.HcPacked;
				o[15] = (float)((rigid ? (int)bone : 0) + 1024 * inst.Sway + (inst.Fogged ? 16384 : 0) // lane VISUAL-2: the sway type, the fogged light
					+ (inst.OcclusionSlot << W3D_OCCLUSION_SLOT_SHIFT) + (inst.Occluder ? W3D_OCCLUSION_OCCLUDER_BIT : 0) // lane OCCLUDE-1
				+ (inst.NoSceneFog ? W3D_NO_SCENE_FOG_BIT : 0)); // lane INGAMEBG-1
				++count;
			}
			d.MM->set_buffer(d.Buffer);
			d.MM->set_visible_instance_count(count);
			// Share the actual surface shaders: skinning, vegetation sway and alpha cutouts must match the visible mesh.
			// Separate batches are necessary because casting is a GeometryInstance property, while eligibility is per drawable.
			int shadowCount = 0;
			for (int id : ids)
				if (Instances[(size_t)id].CastShadow) ++shadowCount;
			if (shadowCount != 0 && d.ShadowMM.is_null())
			{
				d.ShadowMM.instantiate();
				d.ShadowMM->set_transform_format(MultiMesh::TRANSFORM_3D);
				d.ShadowMM->set_use_custom_data(true);
				d.ShadowMM->set_mesh(d.Mesh->Mesh);
				MultiMeshInstance3D *shadow = memnew(MultiMeshInstance3D);
				shadow->set_name(String(d.Node->get_name()) + "_shadow");
				shadow->set_multimesh(d.ShadowMM);
				shadow->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_SHADOWS_ONLY);
				add_child(shadow);
			}
			if (d.ShadowMM.is_valid())
			{
				if (d.ShadowMM->get_instance_count() != d.Allocated)
				{
					d.ShadowMM->set_instance_count(d.Allocated);
					d.ShadowBuffer.resize((int64_t)d.Allocated * 16);
				}
				float *shadowBuffer = d.ShadowBuffer.ptrw();
				int out = 0;
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (Instances[(size_t)ids[i]].CastShadow)
					{
						std::memcpy(shadowBuffer + (size_t)out * 16, d.Buffer.ptr() + i * 16, 16 * sizeof(float));
						++out;
					}
				}
				d.ShadowMM->set_buffer(d.ShadowBuffer);
				d.ShadowMM->set_visible_instance_count(shadowCount);
			}
			visible += count;
			m.Dithered += d.Dithered;
		}
		if (wrote) ++writes;
		if (consumeQueries)
		{
			for (int id : m.QueriedPoseIds) Instances[(size_t)id].QueryPosePending = false;
			m.QueriedPoseIds.clear();
		}
		m.PoseEvaluated = false;
		m.BuffersDirty = false;
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
	if (InterleavedBatchPairs > 0) warn_stop(W3D_Batch_Order_Stop((size_t)InterleavedBatchPairs));
	if (dithered > 0) warn_stop(W3D_Fade_Dither_Stop((size_t)dithered));
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

bool W3DInstancer::set_extra_pose_frustum(const TypedArray<Plane> &planes)
{
	if (planes.size() != 0 && planes.size() != 6)
	{
		return false;
	}
	ExtraPoseFrustum.clear();
	for (int i = 0; i < planes.size(); ++i)
	{
		ExtraPoseFrustum.push_back(planes[i]);
	}
	return true;
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
	// lane PERF-3: pose culling (set_pose_culling): the viewport camera's world-space frustum
	Plane frustum[6];
	bool cull = false;
	Transform3D nodeXf;
	if (PoseCulling && is_inside_tree() && get_viewport() != nullptr && get_viewport()->get_camera_3d() != nullptr)
	{
		const TypedArray<Plane> planes = get_viewport()->get_camera_3d()->get_frustum();
		if (planes.size() == 6)
		{
			for (int k = 0; k < 6; ++k)
			{
				frustum[k] = planes[k];
			}
			nodeXf = get_global_transform();
			cull = true;
		}
	}
	PoseCulled = 0;
	StaleScratch.clear();
	for (size_t i = 0; i < Instances.size(); ++i)
	{
		Instance &inst = Instances[i];
		if (!inst.Removed && (inst.PoseStale || (timeMoved && inst.Anim != nullptr && !inst.Explicit) || inst.ShareLeader >= 0))
		{
			if (cull && !(inst.CastShadow && ShadowMapsEnabled)) // an off-screen caster still throws an on-screen shadow
			{
				// the bounding sphere of the model around the instance's origin, scaled by the larger of the node's and the instance's scale
				const Vector3 centre = nodeXf.xform(inst.Xf.origin);
				const Vector3 sc = inst.Xf.basis.get_scale_abs();
				const Vector3 ns = nodeXf.basis.get_scale_abs();
				real_t r = cull_radius(*Models[(size_t)inst.Model]) * std::max(sc.x, std::max(sc.y, sc.z)) * std::max(ns.x, std::max(ns.y, ns.z));
				bool outside = false;
				for (const Plane &pl : frustum)
				{
					if (pl.distance_to(centre) > r)
					{
						outside = true;
						break;
					}
				}
				if (outside && !ExtraPoseFrustum.empty())
				{
					outside = false;
					for (const Plane &pl : ExtraPoseFrustum)
					{
						if (pl.distance_to(centre) > r)
						{
							outside = true;
							break;
						}
					}
				}
				if (outside)
				{
					inst.PoseStale = true; // pending: posed by the first update that sees it (with the inputs of that frame)
					++PoseCulled;
					if (inst.ShareLeader >= 0)
					{
						inst.ShareLeader = -1; // lane PERF-HD: off screen it is drawn with its own (pending) block again
						Models[(size_t)inst.Model]->BuffersDirty = true;
					}
					continue;
				}
			}
			StaleScratch.push_back(i);
		}
	}
	// lane PERF-HD: retail's horde pose sharing (W3DHordeModelDrawManager::ProcessGroup, PartOfSameGroup: the same model, clip and mode, frames within the
	// draw's MaxAnimFrameDelta of the group's first). The candidates are sorted by (model, clip, frame), so a group is a run whose frames stay within the delta
	// of its first; its first is posed and written, the others are drawn with that block and keep their own pose pending (the bone queries evaluate it).
	// INFERENCE: retail groups the handles in its own list order; sorting by frame makes the groups as large as the delta allows
	PoseShared = 0;
	ShareScratch.clear();
	const bool share = PoseSharing && PoseCulling;
	for (size_t k = 0; k < StaleScratch.size(); ++k)
	{
		Instance &inst = Instances[StaleScratch[k]];
		const int before = inst.ShareLeader;
		inst.ShareLeader = -1;
		if (share && inst.ShareDelta >= 0.0f && inst.Explicit && inst.Anim != nullptr && inst.Anim1 == nullptr && inst.BoneSpins.empty() && inst.PaletteSize > 0)
		{
			ShareScratch.push_back(StaleScratch[k]);
		}
		if (before >= 0)
		{
			inst.PoseStale = true; // it was drawn with another's pose: its own is still pending
		}
	}
	if (ShareScratch.size() > 1)
	{
		std::sort(ShareScratch.begin(), ShareScratch.end(), [&](size_t a, size_t b) {
			const Instance &x = Instances[a], &y = Instances[b];
			if (x.Model != y.Model) return x.Model < y.Model;
			if (x.Anim != y.Anim) return std::less<const HAnimClass *>()(x.Anim, y.Anim);
			if (x.Frame0 != y.Frame0) return x.Frame0 < y.Frame0;
			return a < b;
		});
		for (size_t g = 0; g < ShareScratch.size();)
		{
			const Instance &first = Instances[ShareScratch[g]];
			size_t e = g + 1;
			while (e < ShareScratch.size())
			{
				Instance &x = Instances[ShareScratch[e]];
				if (x.Model != first.Model || x.Anim != first.Anim || x.Frame0 - first.Frame0 > std::min(first.ShareDelta, x.ShareDelta))
				{
					break;
				}
				x.ShareLeader = (int)ShareScratch[g];
				++e;
			}
			g = e;
		}
	}
	// the members leave the evaluation list; an instance whose records point at another block than they were written with rewrites its model's records
	MemberScratch.clear();
	{
		size_t kept = 0;
		for (size_t k = 0; k < StaleScratch.size(); ++k)
		{
			Instance &inst = Instances[StaleScratch[k]];
			if (pose_source(inst).PaletteOffset != inst.DrawnOffset)
			{
				Models[(size_t)inst.Model]->BuffersDirty = true;
			}
			if (inst.ShareLeader >= 0)
			{
				MemberScratch.push_back(StaleScratch[k]);
				++PoseShared;
				continue;
			}
			StaleScratch[kept++] = StaleScratch[k];
		}
		StaleScratch.resize(kept);
	}
	PaletteWrite = StaleScratch.empty() || PaletteImage.is_null() ? nullptr : reinterpret_cast<float *>(PaletteImage->ptrw()); // the image's buffer (not shared: no copy)
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
	for (size_t i : MemberScratch)
	{
		StaleScratch.push_back(i); // lane PERF-HD: the members' visibility and fades (their group's) are checked below too; their rows are not written
	}
	for (size_t i : StaleScratch)
	{
		const Instance &inst = Instances[i];
		const Instance &src = pose_source(inst);
		if (inst.ShareLeader < 0)
		{
			note_palette_rows(inst.PaletteOffset, inst.PaletteSize);
		}
		Model &m = *Models[(size_t)inst.Model];
		m.PoseEvaluated = true;
		if (m.BuffersDirty) continue; // the full rewrite will consume and cache all poses, including queried culled ones
		for (size_t k = 0; k < m.Draws.size(); ++k)
		{
			DrawItem &d = m.Draws[k];
			if (d.PoseDirty) continue; // one changed instance already guarantees this draw's full rewrite
			if (d.Mesh->HasSorted)
			{
				d.PoseDirty = true;
				continue;
			}
			const size_t bone = (size_t)d.Sub->BoneIndex;
			const auto state = std::make_pair(src.Pose.IsVisible[bone], src.Pose.PivotFade[bone]);
			if (inst.DrawPoseState[k] != state)
			{
				d.PoseDirty = true;
			}
		}
	}
	PoseEvaluations = (int)(StaleScratch.size() - MemberScratch.size());
	if (PoseCulled > 0)
	{
		warn(W3D_Pose_Cull_Stop().Message); // S-1730 (once: warn keeps each message once)
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
		warn_stop(W3D_Pose_Order_Stop((size_t)inexact));
	}
	else
	{
		InexactPoseTotal = 0;
	}

	t0 = std::chrono::steady_clock::now();
	write_buffers();
	// Changed rows only. A static prop stays where it was uploaded; a prop that starts animating has been moved into the animated pool
	// so its row is inside this span. A missed row would keep the previous pose on the GPU.
	upload_palette();
	UploadMs = msSince(t0);
	PoseDirty = false;
}

void W3DInstancer::_notification(int what)
{
	// lane RENDER-3 (S-831): this node's transparent shaders output gamma-space values; the camera that shows them composites the transparent pass in
	// gamma space (GodotGammaComposite.h)
	if (what == NOTIFICATION_PREDELETE)
	{
		release_palette_rd();
	}
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
	ensure_pose(instance);
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
	ensure_pose(instance);
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
	s["palette_rows_uploaded"] = PaletteRowsUploaded;
	s["palette_upload_full"] = PaletteUploadFull;
	s["shaders"] = Materials ? (int64_t)Materials->Shader_Count() : 0;
	s["textures"] = Materials ? (int64_t)Materials->Texture_Count() : 0;
	s["pose_ms"] = PoseMs;
	s["upload_ms"] = UploadMs;
	s["pose_evaluations"] = PoseEvaluations; // SMOOTH-1: instances posed in the last update (only the changed ones)
	s["pose_shared"] = PoseShared;           // lane PERF-HD: instances drawn with their horde group's pose in the last update
	s["pose_culled"] = PoseCulled;           // lane PERF-3: instances whose pose the last update left pending (outside the frustum, set_pose_culling)
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

// lane PERF-HD: a stop whose message carries this frame's count is reported once per instancer (its first count); the counts stay in get_stats. Keyed by
// the message, every new count printed a line and was kept: about 13 lines a second in a big battle (S-028), each a print on the main thread
void W3DInstancer::warn_stop(const W3DStopHit &hit)
{
	if (WarnedMessages.insert("#stop " + hit.Id).second)
	{
		warn(hit.Message);
	}
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
