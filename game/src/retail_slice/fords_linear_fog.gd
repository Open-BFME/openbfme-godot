@tool
class_name FordsLinearFog
extends CompositorEffect
## Exact camera-depth linear fog surface for BFME2 Fords of Isen II.
##
## The caller must supply the already-validated uniform map scale. This class
## deliberately does not infer SAGE-to-Godot distance conversion.

const SHADER_PATH := "res://src/retail_slice/fords_linear_fog.glsl"
const SOURCE_COLOR := Color(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0, 1.0)
const SOURCE_START := 350.0
const SOURCE_END := 2000.0
const WORKGROUP_SIZE := 8

var _rd: RenderingDevice
var _shader: RID
var _pipeline: RID
var _nearest_sampler: RID
var _parameter_mutex := Mutex.new()
var _configured := false
var _local_units_per_source_unit := 0.0
var _fog_start_local := 0.0
var _fog_end_local := 0.0
var _initialization_error := "render-thread initialization has not completed"
var _dispatch_count := 0
var _last_render_size := Vector2i.ZERO


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	access_resolved_depth = true
	_rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(_initialize_compute)


func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE or _rd == null:
		return
	if _shader.is_valid():
		_rd.free_rid(_shader)
	if _nearest_sampler.is_valid():
		_rd.free_rid(_nearest_sampler)


func configure_exact(
		source_color: Color,
		source_start: float,
		source_end: float,
		local_units_per_source_unit: float
) -> String:
	if not _colors_equal(source_color, SOURCE_COLOR):
		return "source fog color must be exact RGB 220,226,235"
	if source_start != SOURCE_START:
		return "source fog start must be exactly 350"
	if source_end != SOURCE_END:
		return "source fog end must be exactly 2000"
	if not is_finite(local_units_per_source_unit) or local_units_per_source_unit <= 0.0:
		return "an explicit positive finite uniform map scale is required"

	_parameter_mutex.lock()
	_local_units_per_source_unit = local_units_per_source_unit
	_fog_start_local = source_start * local_units_per_source_unit
	_fog_end_local = source_end * local_units_per_source_unit
	_configured = true
	_parameter_mutex.unlock()
	return ""


func configure_fords(local_units_per_source_unit: float) -> String:
	return configure_exact(SOURCE_COLOR, SOURCE_START, SOURCE_END, local_units_per_source_unit)


func is_configured() -> bool:
	_parameter_mutex.lock()
	var result := _configured
	_parameter_mutex.unlock()
	return result


func create_compositor() -> Compositor:
	if not is_configured():
		return null
	var compositor := Compositor.new()
	compositor.compositor_effects = [self]
	return compositor


func runtime_contract() -> Dictionary:
	_parameter_mutex.lock()
	var contract := {
		"source_color": SOURCE_COLOR,
		"source_start": SOURCE_START,
		"source_end": SOURCE_END,
		"local_units_per_source_unit": _local_units_per_source_unit,
		"fog_start_local": _fog_start_local,
		"fog_end_local": _fog_end_local,
		"configured": _configured,
		"depth_semantic": "negative camera-view z reconstructed by inverse projection",
		"curve": "clamp((camera_depth-start)/(end-start),0,1)",
		"callback": "post-transparent",
		"transparent_depth_status": "unresolved: transparent fragments do not author the sampled opaque depth buffer",
		"sky_depth_status": "reverse-z clear depth is left unchanged so the environment backdrop remains visible",
	}
	_parameter_mutex.unlock()
	return contract


func initialization_error() -> String:
	return _initialization_error


func execution_metrics() -> Dictionary:
	_parameter_mutex.lock()
	var metrics := {
		"dispatch_count": _dispatch_count,
		"last_render_size": _last_render_size,
	}
	_parameter_mutex.unlock()
	return metrics


static func fog_factor_for_camera_depth(
		camera_depth: float,
		fog_start: float,
		fog_end: float
) -> float:
	if not is_finite(camera_depth) or not is_finite(fog_start) or not is_finite(fog_end):
		return NAN
	if fog_end <= fog_start:
		return NAN
	return clampf((camera_depth - fog_start) / (fog_end - fog_start), 0.0, 1.0)


# This method and the render callback run on Godot's rendering thread.
func _initialize_compute() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		_initialization_error = "RenderingDevice unavailable; Forward+ compositor cannot execute"
		return
	var shader_file := load(SHADER_PATH) as RDShaderFile
	if shader_file == null:
		_initialization_error = "linear fog shader resource could not be loaded"
		return
	var shader_versions := shader_file.get_version_list()
	if shader_versions.size() != 1:
		_initialization_error = "linear fog shader has no single compiled version: %s" % shader_file.base_error
		return
	var spirv := shader_file.get_spirv(shader_versions[0])
	if spirv == null or spirv.compile_error_compute != "":
		_initialization_error = "linear fog compute shader did not compile"
		return
	_shader = _rd.shader_create_from_spirv(spirv)
	if not _shader.is_valid():
		_initialization_error = "linear fog compute shader RID is invalid"
		return
	_pipeline = _rd.compute_pipeline_create(_shader)
	if not _pipeline.is_valid():
		_initialization_error = "linear fog compute pipeline RID is invalid"
		return
	_initialization_error = ""


func _render_callback(
		callback_type: EffectCallbackType,
		render_data: RenderData
) -> void:
	if (
		_rd == null
		or callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
		or not _pipeline.is_valid()
	):
		return

	_parameter_mutex.lock()
	var configured := _configured
	var fog_start := _fog_start_local
	var fog_end := _fog_end_local
	_parameter_mutex.unlock()
	if not configured:
		return

	var buffers := render_data.get_render_scene_buffers()
	var scene_data := render_data.get_render_scene_data()
	if buffers == null or scene_data == null:
		return
	var size: Vector2i = buffers.get_internal_size()
	if size.x <= 0 or size.y <= 0:
		return

	if not _nearest_sampler.is_valid():
		var sampler_state := RDSamplerState.new()
		sampler_state.min_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
		sampler_state.mag_filter = RenderingDevice.SAMPLER_FILTER_NEAREST
		_nearest_sampler = _rd.sampler_create(sampler_state)
	if not _nearest_sampler.is_valid():
		return

	@warning_ignore("integer_division")
	var x_groups: int = (size.x - 1) / WORKGROUP_SIZE + 1
	@warning_ignore("integer_division")
	var y_groups: int = (size.y - 1) / WORKGROUP_SIZE + 1
	for view in buffers.get_view_count():
		var inverse_projection: Projection = scene_data.get_view_projection(view).inverse()
		var push_constant := _projection_push_data(inverse_projection)
		push_constant.append_array(PackedFloat32Array([
			float(size.x), float(size.y), fog_start, fog_end,
			SOURCE_COLOR.r, SOURCE_COLOR.g, SOURCE_COLOR.b, SOURCE_COLOR.a,
		]))
		var color_uniform := RDUniform.new()
		color_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		color_uniform.binding = 0
		color_uniform.add_id(buffers.get_color_layer(view))
		var depth_uniform := RDUniform.new()
		depth_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
		depth_uniform.binding = 1
		depth_uniform.add_id(_nearest_sampler)
		depth_uniform.add_id(buffers.get_depth_layer(view))
		var uniform_set := UniformSetCacheRD.get_cache(
			_shader,
			0,
			[color_uniform, depth_uniform]
		)
		var compute_list := _rd.compute_list_begin()
		_rd.compute_list_bind_compute_pipeline(compute_list, _pipeline)
		_rd.compute_list_bind_uniform_set(compute_list, uniform_set, 0)
		_rd.compute_list_set_push_constant(
			compute_list,
			push_constant.to_byte_array(),
			push_constant.size() * 4
		)
		_rd.compute_list_dispatch(compute_list, x_groups, y_groups, 1)
		_rd.compute_list_end()
		_parameter_mutex.lock()
		_dispatch_count += 1
		_last_render_size = size
		_parameter_mutex.unlock()


static func _projection_push_data(projection: Projection) -> PackedFloat32Array:
	# Projection exposes GLSL-compatible column vectors. Keep their order; no
	# coordinate convention is inferred or remapped here.
	return PackedFloat32Array([
		projection.x.x, projection.x.y, projection.x.z, projection.x.w,
		projection.y.x, projection.y.y, projection.y.z, projection.y.w,
		projection.z.x, projection.z.y, projection.z.z, projection.z.w,
		projection.w.x, projection.w.y, projection.w.z, projection.w.w,
	])


static func _colors_equal(left: Color, right: Color) -> bool:
	return left == right
