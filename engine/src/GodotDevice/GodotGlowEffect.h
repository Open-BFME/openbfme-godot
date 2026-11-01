// OpenBFME. GPL-3.0.
//
// Retail's glow pass on the 3D view (lane GLOW-1, stop S-5231): the GlowEffect block's blur of the glow-flagged objects, added over the view, and the
// effects-LOD gate that decides between the glow, the map's colour grade and neither (GameEngineDevice/W3DDevice/GameClient/W3DViewDrawPostPass.h has the
// target facts, ScreenHilightFilter.h the filter's constants and arithmetic).
//
// GlowPostEffect is a CompositorEffect at POST_TRANSPARENT (after GammaComposite's decode and in front of the colour grade): compute passes that
//   1. read the glow scene (a SubViewport that draws only the glow layer, see GlowPostEffectHost) and point sample it at the centres of a square
//      GlowTextureWidth grid (retail renders the scene into that square target; S-5231),
//   2. blur it horizontally and then vertically with the kernel of ScreenHilightFilter (groups of four taps, the first group stored, every later one
//      blended ONE / INVSRCCOLOR, 8 bit stores, LINEAR / CLAMP sampling), exactly as Glow.pso / Glow.vso do,
//   3. screen blend the result over the displayed (gamma) colour of the view: out = glow + frame * (1 - glow), like the render states of RW 0x4FA860.
// GlowPostEffectHost (a child of the game world) owns the glow SubViewport and camera, keeps the effect on the camera that shows the map, and every frame
// applies the gating order: a low effects LOD draws neither pass, an active colour grade is drawn and the glow is not, otherwise the glow runs. The
// glow-flagged objects are the draws W3DInstancer::set_model_glow put on kGlowLayer.

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/GlowEffect.h"
#include "GameEngineDevice/W3DDevice/GameClient/ScreenHilightFilter.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace godot
{

namespace GlowPass
{
// the visual layer the glow-flagged draws are on (bit 18; bit 0 is the default layer, bit 19 the terrain's)
constexpr uint32_t kGlowLayer = 1u << 18;
} // namespace GlowPass

class GlowPostEffect : public CompositorEffect
{
	GDCLASS(GlowPostEffect, CompositorEffect)

public:
	GlowPostEffect();
	~GlowPostEffect() override;
	// The active GlowEffect (kernel, weights and offsets are made here, once). Returns the problems of a block the pass cannot draw ("" none).
	std::string configure(const GlowEffect &effect);
	void setSceneViewport(const RID &viewport);
	void setActive(bool active) { m_active.store(active); }
	// Test hook (godot/tests/glow_test.gd): while set, the kernel buffers are not allocated, as when the device refuses; the pass must report it
	void simulateBufferFailure(bool on) { m_simulateBufferFailure.store(on); }
	bool active() const { return m_active.load(); }
	int64_t passes() const { return m_passes.load(); }
	std::string problem() const;
	int kernelSamples() const;
	int textureWidth() const { return m_width.load(); }
	void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

protected:
	static void _bind_methods() {}

private:
	struct Pass
	{
		std::vector<float> data; // per sample: weight, offset u, offset v, 0 (the 4 samples of a group are consecutive)
		int groups = 0;
	};
	bool ensureGpu(class RenderingDevice *rd, int width);
	void freeGpu(class RenderingDevice *rd);
	void fail(const std::string &why);

	mutable std::mutex m_mutex; // the kernel is replaced on the main thread and read on the render thread
	Pass m_passH, m_passV;
	uint32_t m_version = 0;
	std::atomic<int> m_width{ 256 };
	std::atomic<bool> m_active{ false };
	RID m_sceneViewport;
	std::string m_problem;
	std::atomic<int64_t> m_passes{ 0 };
	std::atomic<bool> m_failed{ false };
	std::atomic<bool> m_simulateBufferFailure{ false };
	// render thread
	RID m_shaderResample, m_shaderBlur, m_shaderComposite;
	RID m_pipelineResample, m_pipelineBlur, m_pipelineComposite;
	RID m_samplerNearest, m_samplerLinear;
	RID m_texScene, m_texA, m_texB; // the square targets: the resampled scene, the horizontal pass, the vertical pass (the glow texture)
	RID m_bufferH, m_bufferV;
	int m_gpuWidth = 0;
	uint32_t m_gpuVersion = 0;
	int m_gpuGroupsH = 0, m_gpuGroupsV = 0;
};

class MapPostEffectsHost;

class GlowPostEffectHost : public Node3D
{
	GDCLASS(GlowPostEffectHost, Node3D)

public:
	GlowPostEffectHost();
	// The active GlowEffect of the world and the map's colour grade host (null: the map has none). Call once, before the host enters the tree.
	void configure(const GlowEffect &effect, MapPostEffectsHost *grade);
	// The same from a Dictionary { enabled, diameter, intensity, texture_width, radius_scale1, amplitude1, radius_scale2, amplitude2, terrain_glow, multipass }
	// and the map root's PostEffects host (or null): the test hook (godot/tests/glow_test.gd)
	void configure_dictionary(const Dictionary &d, Node *grade);
	// Test hook: the glow pass's kernel buffer allocation fails while on (the failure must be reported in the stats' problem)
	void simulate_gpu_allocation_failure(bool on);
	// StaticLODState::effectsLOD (0 VeryLow .. 4 UltraHigh); GameWorld.set_effects_lod
	void set_effects_lod(int64_t lod);
	int64_t get_effects_lod() const { return m_effectsLod; }
	void _notification(int what);
	// { choice, glow_enabled, color_grade, effects_lod, installed, active, passes, kernel_samples, texture_width, scene_size, problem, unverified }
	Dictionary get_glow_stats() const;
	// The gating decision of the last frame: "none", "color_grade", "glow" or "special_filter" (tests)
	String get_choice() const { return String(m_choice.c_str()); }
	// Runs one frame of the host without a render (tests): the gate, the grade's suppression, the glow viewport.
	void update_now();

protected:
	static void _bind_methods();

private:
	void ensureViewport();
	void ensureOnCamera(Camera3D *camera, bool glowOn);
	void syncCamera(Camera3D *main);
	void removeFromCameras();
	GlowEffect m_effect;
	std::string m_configProblem;  // the stats' problem: a pass that cannot be drawn (fatal) or a block field that is not ported (a note)
	bool m_configFatal = false;
	Ref<GlowPostEffect> m_postEffect;
	SubViewport *m_viewport = nullptr;
	Camera3D *m_camera = nullptr;
	ObjectID m_grade;
	std::vector<ObjectID> m_cameras;
	int64_t m_effectsLod = 4; // the port's detected level (GameLODManager::kDetectedLevel)
	std::string m_choice = "none";
	bool m_viewportOn = false;
};

} // namespace godot
