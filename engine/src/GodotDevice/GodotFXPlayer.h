// OpenBFME. GPL-3.0.
//
// Device layer for the FX stack: plays retail FXLists and particle systems (FXPlayback, no Godot) and draws the simulator's state with
// batched MultiMeshes. The simulation stays in C++ at a fixed 30 Hz (RW steps particles once per Display::draw under the 30 FPS limit);
// rendering runs at any rate and never feeds back into the simulation.
//
// Batching: DefaultDraw sprites are one MultiMesh per (texture, blend, ground-aligned, volume) with a vertex shader that builds the
// billboard in view space (RW PointGroup::Render, notes fx-draw.md 4.1); GPU particle systems (GpuDraw, 101 retail systems) are one
// MultiMesh per system with a port of gpuparticle.fxo's vs_2_0 (notes fx-draw-gpu.md 2.1); Streak / Lightning / Quad / Butterfly systems
// are one dynamic mesh per system. Blend states follow RW ShaderClass::Apply (fx-draw.md 3). Everything is unlit and unshadowed.

#pragma once

#include "GameClient/FXPlayback.h"
#include "GameClient/ParticleDraw.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace godot
{

class RetailFileSystem;

class FXPlayer : public Node3D
{
	GDCLASS(FXPlayer, Node3D)

public:
	FXPlayer();
	~FXPlayer() override;

	// Loads the retail FX INI data from the mounted archives. { ok, errors, particle_systems, fx_lists }
	Dictionary setup(const Ref<RetailFileSystem> &fs);
	PackedStringArray list_particle_systems() const;
	PackedStringArray list_fx_lists() const;

	// Positions are Godot world space (x, up, -south): the simulation keeps SAGE space (x east, y north, z up) and maps (x, z, -y).
	int64_t play_particle_system(const String &name, const Vector3 &position);
	bool play_fx_list(const String &name, const Vector3 &position, bool as_object);
	// Test and mod hook: loads FXParticleSystem / FXList blocks from INI text (as a file named `name`) into the running stores; returns the errors.
	Array define_ini(const String &name, const String &text);
	// W3D model emitter (e_fire_sm, e_smoke_sm, ...): { ok, error }
	Dictionary play_w3d_emitter(const String &name, const Vector3 &position);
	void clear();
	// lane PERF-1 r2: decode the particle textures of every particle system template ahead of their first use (definition order), until `budget_ms`
	// has passed (<= 0: all); returns how many are left. A texture's first use takes the decoded one (its miss reported then): what is drawn and
	// reported does not change, only when the decoding happens (the load instead of the battle's first volley)
	int64_t preload_textures(double budget_ms);
	void seed(int64_t seed);

	// Advances the simulation by `delta` seconds of client time (fixed 30 Hz steps, at most max_steps per call) and refreshes the
	// batches. paused simulation still redraws.
	void advance(double delta);
	void step_once();
	void rebuild();
	void set_paused(bool paused) { m_paused = paused; }
	// lane FX-3 round 2: soft particles (fade by the depth distance to the opaque scene; presentation beyond retail). false: retail's exact look
	void set_soft_particles(bool enabled);
	bool get_soft_particles() const { return m_soft; }
	bool is_paused() const { return m_paused; }
	void set_max_particles(int64_t count);

	// { frame, systems, particles, batches, sprites, gpu_instances, mesh_vertices, step_us, build_us, upload_us, missing_textures }
	Dictionary get_stats() const;
	PackedStringArray get_unverified() const;
	// Engine calls the FXLists made that no lane implements yet: [{ kind, detail, frame }]
	Array get_events() const;
	int64_t get_frame() const { return m_playback ? (int64_t)m_playback->clientFrame() : 0; }
	// lane RENDER-4 (diagnostic, client only): with the summary on, every rebuild records one entry per draw batch of the builder: { template, kind
	// (sprites / gpu / mesh), shader, texture, deferred, count, centre (SAGE x, y, z), mean_size, max_size, mean_color (r, g, b, a of the vertex colours),
	// max_alpha }. Off by default (no cost); the FX reports and the ep03 smoke investigation read it.
	void set_batch_summary(bool enabled) { m_summaryEnabled = enabled; if (!enabled) m_summary.clear(); }
	Array get_batch_summary() const { return m_summary; }
	// lane FX-2 (C++ only): the playback the live game's LiveFX drives; null before setup() succeeded
	FXPlayback *playback() const { return m_playback.get(); }

protected:
	static void _bind_methods();
	void _notification(int what);

private:
	struct Batch
	{
		MeshInstance3D *meshNode = nullptr;
		MultiMeshInstance3D *multiNode = nullptr;
		Ref<MultiMesh> multi;
		Ref<ArrayMesh> mesh;
		Ref<ShaderMaterial> material;
		int kind = 0;
		std::string key;
		// lane PERF-1: what the slot last handed to the rendering server, so an unchanged frame (no particle step, the same camera) re-sends nothing: the
		// instance buffer, the GPU batch's parameters and texture, the mesh batch's texture, the draw order
		PackedFloat32Array lastBuffer;
		bool haveGpuParams = false;
		FXParticleSystem::GpuDrawParams lastGpuParams;
		Ref<Texture2D> lastTexture;
		bool haveOrder = false;
		FXParticleSystem::BatchOrder lastOrder{ 0, 0.0f };
	};
	Ref<Texture2D> loadTexture(const std::string &name);
	// lane PERF-1 r2: a particle texture read and decoded (the 1x1 white for a name that does not resolve, `missing` = the report line), not cached
	Ref<Texture2D> decodeTexture(const std::string &name, std::string &missing);
	struct Preloaded
	{
		Ref<Texture2D> texture;
		std::string missing; ///< reported when the texture is first used, as an unpreloaded load reports it
	};
	std::map<std::string, Preloaded> m_preloaded;
	std::vector<std::string> m_preloadQueue;
	size_t m_preloadNext = 0;
	bool m_preloadQueued = false;
	bool m_summaryEnabled = false; ///< lane RENDER-4
	Array m_summary;
	bool m_soft = true; ///< lane FX-3 round 2 (set from the project setting in the constructor)
	Ref<Shader> shaderFor(const std::string &key, const std::string &code);
	Batch &batchSlot(size_t index, const std::string &key, int kind);
	void upload(const std::vector<FXParticleSystem::DrawBatch> &batches);
	void summarize(const std::vector<FXParticleSystem::DrawBatch> &batches); // lane RENDER-4
	static void applyOrder(Batch &b, const FXParticleSystem::BatchOrder &order);
	// lane PERF-1: hands `buf` to the slot's MultiMesh unless it is bit for bit the buffer the slot already shows
	static void setBufferIfChanged(Batch &b, const PackedFloat32Array &buf);

	Ref<RetailFileSystem> m_fs;
	std::unique_ptr<FXPlayback> m_playback;
	std::unique_ptr<W3DClientRandom> m_renderRandom;
	std::unique_ptr<FXParticleSystem::ParticleDrawBuilder> m_builder;
	std::map<std::string, Ref<Texture2D>> m_textures;
	std::map<std::string, Ref<Shader>> m_shaders;
	std::vector<Batch> m_batches;
	std::vector<std::string> m_missing;
	std::vector<std::string> m_emitterStops;
	bool m_sortedBatchSeen = false;
	Ref<ArrayMesh> m_quad;
	Ref<ArrayMesh> m_gpuQuad;
	double m_accumulator = 0.0;
	bool m_paused = false;
	double m_stepUs = 0.0, m_buildUs = 0.0, m_uploadUs = 0.0;
	size_t m_sprites = 0, m_gpu = 0, m_vertices = 0, m_batchCount = 0;
};

} // namespace godot
