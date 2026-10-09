// OpenBFME. GPL-3.0.
//
// The map's post effects on the 3D view (lane RENDER-4, stop S-1650): PostEffectsChunk "LookupTablePostEffect", the colour grade of
// postfx_lookuptable.fxo (W3DLookupTablePostEffect.h has the target facts and the reference arithmetic).
//
// LookupTablePostEffect is a CompositorEffect at POST_TRANSPARENT that runs after GammaComposite's decode: one compute pass reads the linear 3D colour
// buffer, encodes it to the displayed (gamma) value retail's frame buffer holds, clamps it as the D3D9 frame buffer does, samples the 32^3 lookup
// (LINEAR / CLAMP) at that value, lerps by BlendFactor and decodes back to linear. MapPostEffectsHost (a child of the map root MapTerrainBuilder builds)
// keeps the effect last in the compositor of the camera showing the map and takes it off when the map leaves the tree. The interface is drawn after
// the 3D view and is not graded.

#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/image_texture3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace godot
{

class LookupTablePostEffect : public CompositorEffect
{
	GDCLASS(LookupTablePostEffect, CompositorEffect)

public:
	LookupTablePostEffect();
	~LookupTablePostEffect() override;
	// The 32^3 RGBA8 volume of W3DLookupTablePostEffect::VolumeFromStrip and the chunk's BlendFactor
	void setLookup(const std::vector<std::uint8_t> &volume, float blend);
	float blend() const { return m_blend; }
	// Render-time failures (a shader that does not compile, an unsupported colour buffer): reported, never silent
	std::string problem() const;
	int64_t passes() const { return m_passes.load(); }
	void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

protected:
	static void _bind_methods() {}

private:
	Ref<ImageTexture3D> m_lookup;
	float m_blend = 0.0f;
	RID m_shader;
	RID m_pipeline;
	RID m_sampler;
	bool m_failed = false;
	std::string m_problem;
	std::atomic<int64_t> m_passes{ 0 };
};

class MapPostEffectsHost : public Node3D
{
	GDCLASS(MapPostEffectsHost, Node3D)

public:
	void setEffect(const Ref<LookupTablePostEffect> &effect) { m_effect = effect; }
	void _notification(int what);
	// { installed, passes, blend, problem }
	Dictionary get_post_effect_stats() const;

protected:
	static void _bind_methods();

private:
	void ensureOnCamera();
	void removeFromCameras();
	Ref<LookupTablePostEffect> m_effect;
	std::vector<ObjectID> m_cameras;
};

} // namespace godot
