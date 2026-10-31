## Lane LENS-1: the pro camera's depth of field and exposure over the finished frame (NOT RETAIL: an OpenBFME addition;
## it draws on top of what the game draws and changes nothing it reads). docs/PRO_CAMERA.md has the method.
##
## A compositor effect running its compute shader (SOURCE, below) after the transparent pass: each pixel's blur circle
## from its depth by the thin lens (pro_lens.gd); the largest blur in each TILE x TILE tile, then the largest of the
## tiles it can reach (`max_blur` of the width at most, 6 % by default); the blur itself, Dennis Gustafsson's single
## pass ("Bokeh depth of field in a single pass", 2018) over a disc as large as that reach, every sample whose own
## circle reaches the pixel joining a running average with equal weight, so a blurred foreground spreads over a sharp
## background while a sample behind the pixel counts with at most twice the pixel's circle, so a sharp foreground keeps
## its edge; then the exposure, 2^ev. The disc's pattern turns per pixel, which trades a fixed pattern's repeated copies
## for fine grain. A depth- and blur-guided 5 x 5 resolve smooths that grain in defocused regions, leaving focused pixels
## alone. Colour and depth gathers use nearest sampling; the resolve smooths their grain. It works in linear light: on a
## camera OpenBFME draws, attach() keeps it after the gamma composite's decode (GammaCompositeEffect, lane RENDER-3);
## the map's colour grade (LookupTablePostEffect, which keeps itself last) comes after it. Without an aperture, an exposure or log output it costs nothing.
##
## `log_output` (off by default) writes the frame as ACEScct instead, a log encoding to colour grade later: the linear
## light turned from Rec. 709 primaries into ACES AP1 (the Bradford-adapted matrix the ACES project publishes), then the
## ACEScct curve (Academy S-2016-001), and handed over so that Godot's sRGB encode of the output turns it back into the
## ACEScct values, which an editor's ACEScct input transform reads. What comes after it is made neutral meanwhile: the
## map's colour grade (LookupTablePostEffect) switched off, as it would bake a look in, and the environment the camera
## draws with given a linear tone map at exposure 1, no glow and no adjustments (OpenBFME's "enhanced" look has AgX and
## glow); attach() puts them back when log_output is off, and detach() and freeing the effect always do.
##
## It also reads the distance along the view at a pixel of the viewport (ask_distance, then distance_of(request) once
## `answered` reaches the request; below 0 where nothing is drawn, the sky, or outside the picture), for focusing on
## whatever is drawn there; requests may overlap, each is answered with its own pixel. Set the lens from the main
## thread; the passes run on the render thread. pro_camera.gd sets it from its lens; it can be used on its own too.
##
## It needs the Forward+ renderer: a RenderingDevice and an RGBA16F 3D colour buffer, which the shader writes. Without
## them it draws nothing and says why once (`problem`, and a warning).
extends CompositorEffect

const Lens := preload("res://scripts/pro_lens.gd")
const TILE := 16              # SOURCE's
const PROBES := 16            # distance requests answered a frame at most (the probe buffer's slots); more wait
const ANSWERS_KEPT := 256     # distance requests whose answers distance_of keeps, the latest

var taps := 128               # the blur's samples per pixel (more: smoother, slower; 256 for a still)
var max_blur := Lens.MAX_BLUR # the largest blur circle drawn, a share of the image's width (larger: slower)
var focal := Lens.focal_from_fov(50.0)
var aperture := 0.0           # an f-number; 0: everything sharp
var focus := 1500.0           # world units along the view
var ev := 0.0                 # exposure, stops
var image_mm := Lens.SENSOR.x # the image's width in mm on the sensor's scale (pro_camera.gd image_mm)
var units_per_metre := Lens.UNITS_PER_METRE
var log_output := false       # ACEScct, to grade later (off: the picture as OpenBFME draws it)
var answered := 0             # the distance request answered last; every request up to it is answered
var problem := ""             # why the effect draws nothing, once it knows ("": it draws)

var _mutex := Mutex.new()     # the distance requests and their answers, shared by the main and the render thread
var _asked := 0
var _pending: Array = []      # [number, pixel] of each request not answered yet, oldest first
var _answers := {}            # request number -> distance, the latest ANSWERS_KEPT
var _rd: RenderingDevice
var _shader: RID
var _pipeline: RID
var _sampler: RID
var _params: RID
var _probe: RID
var _copy: RID
var _blur: RID
var _tiles: RID
var _reach: RID
var _size := Vector2i.ZERO
var _graded: Array = []       # the colour grades log_output switched off, to switch on again
var _neutral := {}            # the environment log_output made neutral, and its settings before


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	access_resolved_color = true
	access_resolved_depth = true
	RenderingServer.call_on_render_thread(_setup)


func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE:
		return
	# freed without detach(): the map's grade and the environment back as they were. Written out, as _restore(): an
	# object being deleted takes no calls of its own methods, while its variables still read
	for grade in _graded:
		if is_instance_valid(grade):
			grade.enabled = true
	if not _neutral.is_empty():
		for key in ["tonemap_mode", "tonemap_exposure", "glow_enabled", "adjustment_enabled"]:
			_neutral.environment.set(key, _neutral[key])
	if _rd != null:
		for rid in [_copy, _blur, _tiles, _reach, _params, _probe, _sampler, _pipeline, _shader]:
			if rid.is_valid():
				_rd.free_rid(rid)


## `why` the effect draws nothing, kept in `problem` and reported once (an error when the fault is the effect's own).
func _report(why: String, error := false) -> void:
	if problem == why:
		return
	problem = why
	if error:
		push_error("LENS " + why)
	else:
		push_warning("LENS " + why)


## Read the distance along the view at `pixel` (of the viewport) with the next frame; returns the request's number, for
## distance_of() once `answered` reaches it. Requests may overlap: each is answered with its own pixel, in the order
## asked, PROBES a frame. Without a RenderingDevice nothing is drawn, so it is answered at once, below 0.
func ask_distance(pixel: Vector2i) -> int:
	_mutex.lock()
	_asked += 1
	var number := _asked
	if RenderingServer.get_rendering_device() == null:
		_answer(number, -1.0)
	else:
		_pending.append([number, pixel])
	_mutex.unlock()
	return number


## The answer to distance request `number`: along the view, world units, below 0 where nothing is drawn there (the sky,
## or outside the picture); NAN while it is not answered, and once ANSWERS_KEPT later answers have replaced it.
func distance_of(number: int) -> float:
	_mutex.lock()
	var d: float = _answers.get(number, NAN)
	_mutex.unlock()
	return d


## Request `number` answered with `d`, the oldest answer beyond ANSWERS_KEPT forgotten (requests are answered in order).
## With _mutex held.
func _answer(number: int, d: float) -> void:
	_answers[number] = d
	_answers.erase(number - ANSWERS_KEPT)
	answered = maxi(answered, number)


## The oldest waiting requests answered with `distances`, one each, in order; on the render thread, the only one that
## takes requests off _pending (ask_distance only adds to it).
func _answered(distances: Array) -> void:
	_mutex.lock()
	for d in distances:
		_answer(_pending.pop_front()[0], d)
	_mutex.unlock()


## `effect` on `camera`'s compositor where it sees linear light: right after OpenBFME's gamma composite (its last
## GammaCompositeEffect, the decode) when the camera has one, else last. Moves it only when it is elsewhere, so calling
## it every frame is cheap; OpenBFME installs its pair on the current camera every frame, so call it every frame you use
## it.
static func attach(camera: Camera3D, effect) -> void:     # effect: this script's
	if camera.compositor == null:
		camera.compositor = Compositor.new()
	# a copy: the compositor learns of a change by assignment
	var effects := camera.compositor.compositor_effects.duplicate()
	effect._grade(effects, camera)
	var at := effects.find(effect)
	effects.erase(effect)
	var decode := _last_gamma_composite(effects)
	var want := decode + 1 if decode >= 0 else (at if at >= 0 else effects.size())
	if want == at:
		return                # already there: the compositor is left as it is
	effects.insert(want, effect)
	camera.compositor.compositor_effects = effects


## The index of the last of OpenBFME's GammaCompositeEffects (its decode back to linear light), or -1.
static func _last_gamma_composite(effects: Array) -> int:
	var last := -1
	for i in effects.size():
		if effects[i] != null and effects[i].get_class() == "GammaCompositeEffect":
			last = i
	return last


## While log_output is on, what would change the picture after it neutral: the map's colour grades among `effects` off,
## and the environment `camera` sees with a linear tone map at exposure 1, no glow and no adjustments (OpenBFME's
## "enhanced" look has AgX and glow); all as they were again when it is off.
func _grade(effects: Array, camera: Camera3D = null) -> void:
	if not log_output:
		_restore()
		return
	for e in effects:
		if e != null and e.get_class() == "LookupTablePostEffect" and e.enabled:
			e.enabled = false
			_graded.append(e)
	var environment := _environment(camera)
	if environment == null or _neutral.get("environment") == environment:
		return
	_restore_environment()
	_neutral = {"environment": environment, "tonemap_mode": environment.tonemap_mode,
		"tonemap_exposure": environment.tonemap_exposure, "glow_enabled": environment.glow_enabled,
		"adjustment_enabled": environment.adjustment_enabled}
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	environment.tonemap_exposure = 1.0
	environment.glow_enabled = false
	environment.adjustment_enabled = false


## The environment `camera` draws with: its own, else its world's, else the world's fallback; null without a camera.
static func _environment(camera: Camera3D) -> Environment:
	if camera == null:
		return null
	if camera.environment != null:
		return camera.environment
	var world := camera.get_world_3d()
	if world == null:
		return null
	return world.environment if world.environment != null else world.fallback_environment


## The colour grades and the environment log output changed, as they were.
func _restore() -> void:
	for grade in _graded:
		if is_instance_valid(grade):
			grade.enabled = true
	_graded.clear()
	_restore_environment()


func _restore_environment() -> void:
	if _neutral.is_empty():
		return
	var environment: Environment = _neutral.environment
	for key in ["tonemap_mode", "tonemap_exposure", "glow_enabled", "adjustment_enabled"]:
		environment.set(key, _neutral[key])
	_neutral = {}


## `effect` off `camera`'s compositor, and what log output changed as it was.
static func detach(camera: Camera3D, effect) -> void:     # effect: this script's
	effect._restore()
	if camera.compositor == null:
		return
	# a copy: the compositor learns of a change by assignment
	var effects := camera.compositor.compositor_effects.duplicate()
	if effects.has(effect):
		effects.erase(effect)
		camera.compositor.compositor_effects = effects


func _setup() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		_report("no RenderingDevice: the effect needs the Forward+ renderer (headless and Compatibility have none) and "
			+ "draws nothing")
		return
	var source := RDShaderSource.new()
	source.language = RenderingDevice.SHADER_LANGUAGE_GLSL
	source.source_compute = SOURCE
	var spirv := _rd.shader_compile_spirv_from_source(source)
	if not spirv.compile_error_compute.is_empty():
		_report("its shader did not compile: " + spirv.compile_error_compute, true)
		return
	_shader = _rd.shader_create_from_spirv(spirv)
	_pipeline = _rd.compute_pipeline_create(_shader)
	var state := RDSamplerState.new()
	state.min_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
	state.mag_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
	state.repeat_u = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	state.repeat_v = RenderingDevice.SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE
	_sampler = _rd.sampler_create(state)
	_params = _rd.storage_buffer_create(128)
	_probe = _rd.storage_buffer_create(PROBES * 4)


func _render_callback(_type: int, render_data: RenderData) -> void:
	# the oldest waiting distance requests, PROBES at most (ask_distance may add more meanwhile; they wait)
	_mutex.lock()
	var probes := _pending.slice(0, PROBES)
	_mutex.unlock()
	var probing := not probes.is_empty()
	if _rd == null or not _pipeline.is_valid():
		if probing and problem != "":
			_answered(probes.map(func(_request): return -1.0))     # it draws nothing: nothing drawn there
		return
	# the lens as it is now, for every pass (the main thread may change it meanwhile)
	var lens := {"focal": focal, "aperture": aperture, "focus": focus, "ev": ev, "width": image_mm, "taps": taps,
		"at": Vector2i.ZERO, "slot": 0, "mm": 1000.0 / units_per_metre, "log": log_output, "cap": max_blur}
	var changes: bool = lens.aperture > 0.0 or absf(lens.ev) > 1e-6 or lens.log
	if not (changes or probing):
		return
	var buffers := render_data.get_render_scene_buffers() as RenderSceneBuffersRD
	if buffers == null:
		return
	var size := buffers.get_internal_size()
	if size.x <= 0 or size.y <= 0:
		return
	var color := buffers.get_color_layer(0)
	var depth := buffers.get_depth_layer(0)
	if _rd.texture_get_format(color).format != RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT:
		_report("the 3D colour buffer is not RGBA16F (the Forward+ renderer's), which the shader writes: it draws nothing")
		if probing:
			_answered(probes.map(func(_request): return -1.0))
		return
	_targets(size, _rd.texture_get_format(color).format)
	var inverse := render_data.get_render_scene_data().get_view_projection(0).inverse()
	var uniforms := UniformSetCacheRD.get_cache(_shader, 0, [
		_uniform(RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, 0, [_params]),
		_uniform(RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, [_sampler, depth]),
		_uniform(RenderingDevice.UNIFORM_TYPE_IMAGE, 3, [color]),
		_uniform(RenderingDevice.UNIFORM_TYPE_IMAGE, 4, [_tiles]),
		_uniform(RenderingDevice.UNIFORM_TYPE_IMAGE, 5, [_reach]),
		_uniform(RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, 6, [_probe]),
		_uniform(RenderingDevice.UNIFORM_TYPE_IMAGE, 7, [_copy]),
		_uniform(RenderingDevice.UNIFORM_TYPE_IMAGE, 8, [_blur])])
	var tiles := Vector2i(ceili(float(size.x) / TILE), ceili(float(size.y) / TILE))
	if probing:
		# each request's pixel, read into its own slot of the probe buffer; the buffer read once for all of them
		var target := buffers.get_target_size()
		var inside := []
		for slot in probes.size():
			# the viewport's pixel in the frame the 3D is drawn at, which 3D scaling makes smaller
			var at := Vector2i((Vector2(probes[slot][1]) * Vector2(size) / Vector2(target.max(Vector2i.ONE))).floor())
			inside.append(at.x >= 0 and at.y >= 0 and at.x < size.x and at.y < size.y)
			if inside[slot]:
				lens.at = at
				lens.slot = slot
				_pass(uniforms, inverse, size, lens, 3, Vector2i.ONE)
		var slots := PackedFloat32Array()
		if inside.has(true):
			slots = _rd.buffer_get_data(_probe, 0, probes.size() * 4).to_float32_array()
		var distances := []
		for slot in probes.size():
			distances.append(slots[slot] if inside[slot] else -1.0)    # outside the picture: nothing drawn there
		_answered(distances)
	if not changes:
		return
	var pixels := Vector2i(ceili(size.x / 8.0), ceili(size.y / 8.0))
	_pass(uniforms, inverse, size, lens, 4, pixels)         # the frame copied, to read while the blur writes it
	if lens.aperture > 0.0:
		_pass(uniforms, inverse, size, lens, 0, Vector2i(ceili(tiles.x / 8.0), ceili(tiles.y / 8.0)))
		_pass(uniforms, inverse, size, lens, 1, Vector2i(ceili(tiles.x / 8.0), ceili(tiles.y / 8.0)))
		_pass(uniforms, inverse, size, lens, 2, pixels)     # gather into a separate image, never read while writing
	_pass(uniforms, inverse, size, lens, 5, pixels)         # guided resolve, then exposure and log encoding


func _pass(uniforms: RID, inverse: Projection, size: Vector2i, lens: Dictionary, index: int, groups: Vector2i) -> void:
	var values := PackedFloat32Array()
	for column in [inverse.x, inverse.y, inverse.z, inverse.w]:
		values.append_array([column.x, column.y, column.z, column.w])
	var open: bool = lens.aperture > 0.0
	values.append_array([size.x, size.y, lens.at.x, lens.at.y, lens.mm, lens.focus * lens.mm, lens.focal,
		lens.focal / lens.aperture if open else lens.focal, size.x / lens.width, maxf(2.0, lens.cap * size.x),
		pow(2.0, lens.ev), lens.taps, 1.0 if open else 0.0, index, 1.0 if lens.log else 0.0, lens.slot])
	_rd.buffer_update(_params, 0, values.size() * 4, values.to_byte_array())
	var list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(list, _pipeline)
	_rd.compute_list_bind_uniform_set(list, uniforms, 0)
	_rd.compute_list_dispatch(list, groups.x, groups.y, 1)
	_rd.compute_list_end()


## The frame's copy, blur gather and tile images, made again when the size or the colour format changes.
func _targets(size: Vector2i, format: int) -> void:
	if size == _size and _copy.is_valid() and _rd.texture_get_format(_copy).format == format:
		return
	for rid in [_copy, _blur, _tiles, _reach]:
		if rid.is_valid():
			_rd.free_rid(rid)
	_size = size
	_copy = _texture(size, format, RenderingDevice.TEXTURE_USAGE_STORAGE_BIT)
	_blur = _texture(size, format, RenderingDevice.TEXTURE_USAGE_STORAGE_BIT)
	var tiles := Vector2i(ceili(float(size.x) / TILE), ceili(float(size.y) / TILE))
	_tiles = _texture(tiles, RenderingDevice.DATA_FORMAT_R32_SFLOAT, RenderingDevice.TEXTURE_USAGE_STORAGE_BIT)
	_reach = _texture(tiles, RenderingDevice.DATA_FORMAT_R32_SFLOAT, RenderingDevice.TEXTURE_USAGE_STORAGE_BIT)


func _texture(size: Vector2i, format: int, usage: int) -> RID:
	var f := RDTextureFormat.new()
	f.width = size.x
	f.height = size.y
	f.format = format
	f.usage_bits = usage
	return _rd.texture_create(f, RDTextureView.new())


static func _uniform(type: int, binding: int, ids: Array) -> RDUniform:
	var u := RDUniform.new()
	u.uniform_type = type
	u.binding = binding
	for id in ids:
		u.add_id(id)
	return u


## The compute shader, compiled when the effect starts. It lives here rather than in a .glsl file because OpenBFME's
## release exports only .gdshader files as shaders (tools/release/audit_package.py), as the gamma composite's lives in
## its C++.
const SOURCE := """#version 450
// Lane LENS-1: the pro camera's depth of field and exposure (NOT RETAIL: an OpenBFME addition), compiled by
// scripts/pro_lens_effect.gd, whose header has the method: each pixel's blur circle from its depth by the thin lens
// (scripts/pro_lens.gd); the largest blur in each TILE x TILE tile (pass 0), then the largest of the tiles it can reach
// (pass 1); the blur gather over a disc as large as that reach (pass 2), then the guided resolve, exposure and optional
// ACEScct encode (pass 5). Pass 3 reads the distance at one pixel into its slot, to focus on it; pass 4 copies the frame
// for pass 2 to read. The colour buffer is linear here (after OpenBFME's gamma composite decode); bright
// pixels keep their energy, so a light out of focus spreads into a bright disc.
//
// Sources: blur_pass's gather loop (the clamp behind the pixel, the smoothstep reach, the running average) is adapted
// from Dennis Gustafsson, "Bokeh depth of field in a single pass" (blog.tuxedolabs.com, 2018), which states no licence.
// The ACES numbers are published constants: the ACEScct curve of Academy S-2016-001, and the Rec. 709 to AP1 matrix the
// ACES project's transforms use (it follows from the two sets of primaries and the Bradford adaptation).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer Params {
	mat4 inv_projection;
	vec2 size;              // pixels
	vec2 probe;             // pass 3's pixel
	float mm_per_unit;
	float focus_mm;
	float focal_mm;
	float aperture_mm;      // the aperture's diameter: focal / f-number
	float px_per_mm;
	float max_blur;         // pixels, the largest circle's diameter
	float exposure;         // 2^ev
	float taps;
	float use_dof;
	float pass_index;
	float log_output;       // 1: ACEScct
	float probe_slot;       // pass 3's slot in Probe
} p;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;
layout(rgba16f, set = 0, binding = 3) uniform restrict image2D color_img;      // the frame
layout(r32f, set = 0, binding = 4) uniform restrict image2D tile_img;
layout(r32f, set = 0, binding = 5) uniform restrict image2D reach_img;
// one slot for each distance request answered this frame
layout(set = 0, binding = 6, std430) restrict writeonly buffer Probe {
	float distance[];
} probe_out;
// the frame's copy (pass 4), read by pass 2
layout(rgba16f, set = 0, binding = 7) uniform restrict image2D copy_img;
layout(rgba16f, set = 0, binding = 8) uniform restrict image2D blur_img;

const int TILE = 16;

// Nearest colour from the untouched frame; the guided resolve removes sampling grain.
vec4 frame_at(vec2 uv) {
	ivec2 at = clamp(ivec2(uv * p.size), ivec2(0), ivec2(p.size) - 1);
	return imageLoad(copy_img, at);
}

// The distance along the view axis (world units) of a depth buffer value.
float view_z(float depth) {
	vec4 q = p.inv_projection * vec4(0.0, 0.0, depth, 1.0);
	return -q.z / q.w;
}

// The blur circle's radius in pixels of a point `units` world units away (pro_lens.gd blur_pixels / 2, capped).
float radius(float units) {
	float d = max(units * p.mm_per_unit, 1e-3);
	float s = max(p.focus_mm, p.focal_mm * 1.001);
	return min(p.aperture_mm * p.focal_mm / (s - p.focal_mm) * abs(d - s) / d * p.px_per_mm, p.max_blur) * 0.5;
}

// Linear Rec. 709 (sRGB primaries, D65) to ACES AP1 (D60), the Bradford-adapted matrix of the ACES project's
// transforms; column-major.
const mat3 REC709_TO_AP1 = mat3(
	0.6130974024, 0.0701937225, 0.0206155929,
	0.3395231462, 0.9163538791, 0.1095697729,
	0.0473794514, 0.0134523985, 0.8698146342);

// The ACEScct curve (Academy S-2016-001): linear below 2^-7, log2 above.
vec3 acescct(vec3 x) {
	vec3 lin = 10.5402377416545 * x + 0.0729055341958355;
	vec3 lg = (log2(max(x, vec3(1e-10))) + 9.72) / 17.52;
	return mix(lin, lg, step(vec3(0.0078125), x));
}

// The value whose sRGB encode is v: what to hand over so that Godot's output encode gives v itself.
vec3 srgb_decode(vec3 v) {
	return mix(v / 12.92, pow((v + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), v));
}

void tile_pass(ivec2 t) {
	ivec2 base = t * TILE;
	ivec2 last = ivec2(p.size) - 1;
	float m = 0.0;
	for (int y = 0; y < TILE; y++) {
		for (int x = 0; x < TILE; x++) {
			m = max(m, radius(view_z(texelFetch(depth_tex, min(base + ivec2(x, y), last), 0).r)));
		}
	}
	imageStore(tile_img, t, vec4(m));
}

void spread_pass(ivec2 t) {
	int spread = int(ceil(p.max_blur / 2.0 / float(TILE)));
	ivec2 last = imageSize(tile_img) - 1;
	float m = 0.0;
	for (int y = -spread; y <= spread; y++) {
		for (int x = -spread; x <= spread; x++) {
			m = max(m, imageLoad(tile_img, clamp(t + ivec2(x, y), ivec2(0), last)).r);
		}
	}
	imageStore(reach_img, t, vec4(m));
}

void blur_pass(ivec2 pixel) {
	vec2 uv = (vec2(pixel) + 0.5) / p.size;
	vec4 own = frame_at(uv);
	vec3 c = own.rgb;
	float reach = p.use_dof > 0.5 ? imageLoad(reach_img, pixel / TILE).r : 0.0;
	if (reach >= 0.5) {
		float z0 = view_z(texture(depth_tex, uv).r);
		float s0 = radius(z0);
		float golden = 2.39996323;
		// interleaved gradient noise: the pattern's turn for this pixel
		float turn = 6.2831853 * fract(52.9829189 * fract(dot(vec2(pixel) + 0.5, vec2(0.06711056, 0.00583715))));
		float total = 1.0;
		int taps = int(p.taps);
		for (int i = 0; i < taps; i++) {
			float r = sqrt((float(i) + 0.5) / float(taps)) * reach;
			float a = float(i) * golden + turn;
			vec2 suv = uv + vec2(cos(a), sin(a)) * r / p.size;
			float z = view_z(texture(depth_tex, suv).r);
			float s = radius(z);
			if (z > z0) {
				s = min(s, s0 * 2.0);            // behind the pixel: it cannot spread over a sharper pixel
			}
			float m = smoothstep(r - 0.5, r + 0.5, s);   // does its circle reach this pixel?
			c += mix(c / total, frame_at(suv).rgb, m);
			total += 1.0;
		}
		c /= total;
	}
	imageStore(blur_img, pixel, vec4(c, own.a));
}

// A small binomial kernel resolves the per-pixel sampling grain. Reject different surfaces and blur radii;
// fade out at focus, so sharp detail and foreground silhouettes are not softened by a full-frame blur.
// All reads use the completed gather, not the colour buffer being written by neighbouring invocations.
void resolve_pass(ivec2 pixel) {
	vec4 own = p.use_dof > 0.5 ? imageLoad(blur_img, pixel) : imageLoad(copy_img, pixel);
	vec3 c = own.rgb;
	if (p.use_dof > 0.5) {
		float z0 = view_z(texelFetch(depth_tex, pixel, 0).r);
		float s0 = radius(z0);
		float amount = smoothstep(1.0, 3.0, s0);
		if (amount > 0.0) {
			const float kernel[5] = float[5](1.0, 4.0, 6.0, 4.0, 1.0);
			vec3 sum = vec3(0.0);
			float total = 0.0;
			for (int y = -2; y <= 2; y++) {
				for (int x = -2; x <= 2; x++) {
					ivec2 at = clamp(pixel + ivec2(x, y), ivec2(0), ivec2(p.size) - 1);
					float z = view_z(texelFetch(depth_tex, at, 0).r);
					float s = radius(z);
					float depth_weight = 1.0 - smoothstep(0.005, 0.02, abs(z - z0) / max(abs(z0), 1e-3));
					float blur_weight = 1.0 - smoothstep(0.5, 2.0, abs(s - s0));
					float w = kernel[x + 2] * kernel[y + 2] * depth_weight * blur_weight;
					sum += imageLoad(blur_img, at).rgb * w;
					total += w;
				}
			}
			c = mix(c, sum / total, amount);
		}
	}
	c *= p.exposure;
	if (p.log_output > 0.5) {
		c = srgb_decode(acescct(REC709_TO_AP1 * max(c, vec3(0.0))));
	}
	imageStore(color_img, pixel, vec4(c, own.a));
}

void main() {
	ivec2 id = ivec2(gl_GlobalInvocationID.xy);
	int pass_index = int(p.pass_index);
	if (pass_index == 0 || pass_index == 1) {
		if (any(greaterThanEqual(id, imageSize(tile_img)))) {
			return;
		}
		if (pass_index == 0) {
			tile_pass(id);
		} else {
			spread_pass(id);
		}
	} else if (pass_index == 2 || pass_index == 4 || pass_index == 5) {
		if (any(greaterThanEqual(id, ivec2(p.size)))) {
			return;
		}
		if (pass_index == 2) {
			blur_pass(id);
		} else if (pass_index == 5) {
			resolve_pass(id);
		} else {
			imageStore(copy_img, id, imageLoad(color_img, id));
		}
	} else if (id == ivec2(0)) {
		float depth = texelFetch(depth_tex, ivec2(p.probe), 0).r;
		// reversed z: 0 is the cleared far plane, nothing drawn
		probe_out.distance[int(p.probe_slot)] = depth > 0.0 ? view_z(depth) : -1.0;
	}
}
"""
