// OpenBFME. GPL-3.0.
//
// Gamma-space compositing of the transparent pass (lane RENDER-3, review r1 of RENDER-3: stop S-831).
//
// TARGET FACT (spec w3d-and-draw, RENDER-1's lighting notes, terrain_common.gdshaderinc): retail draws through D3D9 without sRGB conversion, so every
// texture modulation and every frame-buffer blend (SRCALPHA / INVSRCALPHA, ONE / ONE, DESTCOLOR / ZERO) works on the gamma-encoded values the display
// shows: white at alpha 0.25 over black displays 0.25. Godot's Forward+ renderer keeps the 3D colour buffer linear (RGBA16F) and sRGB-encodes it once at
// the end, so a blend in that buffer displays 0.5333 for the same draw (measured by the RENDER-3 review).
//
// The port: two CompositorEffects on the active Camera3D. PRE_TRANSPARENT re-encodes the colour buffer to the display's gamma encoding (the sRGB curve
// Godot's output applies), the transparent pass then blends gamma values, and POST_TRANSPARENT decodes the buffer back to linear before Godot's own
// tonemap / output encoding. The shaders drawn in the transparent pass (FX particles, streaks, blended W3D materials, roads, rivers, standing water)
// output gamma-space values (their owners, FXPlayer / W3DInstancer / GameWorld / the map root GammaCompositeHost, call GammaComposite::ensure every frame); the opaque pass is untouched. The finished composite is
// converted once. The colour and alpha data stay retail's.
//
// INFERENCE / remainder (reported as S-831 by GammaComposite::unverified): the round trip through the 16-bit float buffer (half precision, below 1/255
// after encoding), the sRGB curve as the display's gamma (Godot's output curve; retail shows the raw value on a display of its own gamma, which is the
// same pixel value), Godot's built-in transparent materials that are not ours (none in the game scenes), a colour buffer in another format (the
// effect then does nothing and reports it) and the missing per-draw saturation: D3D9's frame buffer clamps every blend result to [0, 1], the RGBA16F
// buffer keeps additive overbright through later alpha / multiply draws (review r2 reproductions in fx_render_test.gd).
// MSAA: the encode writes the multisample attachment the transparent pass draws into (encodeSamples). Default (review r3, cost): each pixel's resolved
// opaque colour, gamma-encoded, to all its samples (opaque edges keep Godot's linear sample average; reported); project setting
// openbfme/rendering/gamma_exact_msaa_edges: every sample from a copy of the attachment (exact, about 2 ms more at 1440p 4x). When neither can run, the
// cameras showing the pair are turned to MSAA off and the reason is reported.
// lane GPUPERF-1 (cost, presentation only): on a graded map the decode of a view is left to the grade effect when it directly follows the pair
// (GodotPostEffects.h: deferDecodeTo), whose pass reads the gamma-space buffer, grades and decodes once: one full-resolution pass less.

#pragma once

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <map>
#include <string>
#include <vector>

namespace godot
{

class GammaCompositeEffect : public CompositorEffect
{
	GDCLASS(GammaCompositeEffect, CompositorEffect)

public:
	GammaCompositeEffect();
	~GammaCompositeEffect() override;
	void setDecode(bool decode);
	bool isDecode() const { return m_decode; }
	void detachCleanup() { disconnectCleanup(); } // extension shutdown: stop the per-frame prune before the effect is released
	void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

protected:
	static void _bind_methods() {}

private:
	void encodeSamples(RenderingDevice *rd, RenderSceneBuffersRD *buffers, const Vector2i &size);

	bool m_decode = false;
	// the multisample encode (review r2 / r3): pipelines per framebuffer format and mode, one framebuffer (and, in the exact mode, one sample copy) per
	// multisample attachment; all owned, released when the attachment dies and in the destructor
	struct AttachmentEntry
	{
		RID attachment;
		RID framebuffer;
		RID copy;
	};
	RID rasterPipeline(RenderingDevice *rd, int64_t format, RenderingDevice::TextureSamples samples, bool perSample);
	void pruneAttachments(RenderingDevice *rd);
	void connectCleanup();
	void disconnectCleanup();
	void queueCleanup();            // frame_post_draw (main thread)
	void cleanupOnRenderThread();   // prunes the attachment entries, MSAA viewport or not
public:
	// lane GPUPERF-1: the compute pipeline (compiled on first use); false (and reported) when it cannot be made
	bool ensureCompute(RenderingDevice *rd);
	// lane GPUPERF-1: the in-place encode / decode of every view of `buffers` (the non-MSAA path of _render_callback)
	void convert(RenderingDevice *rd, RenderSceneBuffersRD *buffers, const Vector2i &size, bool decode);
private:
	bool m_cleanupConnected = false;
	static void releaseAttachment(RenderingDevice *rd, AttachmentEntry &e);
	RID m_pixelShader;
	RID m_sampleShader;
	RID m_sampler;
	std::map<int64_t, RID> m_pixelPipelines;
	std::map<int64_t, RID> m_samplePipelines;
	std::map<uint64_t, AttachmentEntry> m_attachments;
	bool m_msaaFailed = false;
	RID m_shader;
	RID m_pipeline;
	bool m_failed = false;
};

// A Node3D that installs the gamma-space pair on the camera showing it (READY and every internal process), for scene roots whose transparent materials
// hand over gamma-space values without a FXPlayer / W3DInstancer / GameWorld next to them (MapTerrainBuilder's map root: roads, rivers, standing water).
class GammaCompositeHost : public Node3D
{
	GDCLASS(GammaCompositeHost, Node3D)

public:
	void _notification(int what);
	// review r3: counters of the MSAA encode (encodes run, framebuffers / pipelines / sample copies created) for the leak regression
	Dictionary get_gamma_stats() const;

protected:
	static void _bind_methods();
};

namespace GammaComposite
{
// Installs the encode / decode pair on the camera `node`'s viewport currently uses (once per camera; a camera with a compositor of its own gets the two
// effects added at the ends of its list). Cheap when already installed; call it every frame from the nodes that draw gamma-space transparent shaders.
void ensure(Node *node);
// lane GPUPERF-1: fusion. The grade effect `grade` directly follows the decode on the compositor of `vp`'s camera this frame: the decode of that view
// is left to it (its pass reads the gamma-space buffer, grades and decodes once: one full-screen pass less). Called every frame by its owner.
void deferDecodeTo(Viewport *vp, const Ref<CompositorEffect> &grade);
// render thread: true when this frame's decode of render target `rt` was left to the grade, which must then decode (its fused pass or decodeNow)
bool decodeDeferred(const RID &rt);
// render thread: the decode of `buffers`, for a grade that took it over but cannot run its own pass this frame
void decodeNow(RenderSceneBuffersRD *buffers);
// True once ensure() installed the pair on some camera: the gamma-space transparent shaders are generated from then on.
bool installed();
// The S-831 remainder lines (empty before installation) and the problems the effects met at render time (an unsupported buffer format).
PackedStringArray unverified();
// Releases the shared effects (extension shutdown).
void shutdown();
} // namespace GammaComposite

} // namespace godot
