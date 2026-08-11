extends SceneTree
## Focused executable/source gate for the exact Fords linear fog surface.

const FOG_SCRIPT_PATH := "res://src/retail_slice/fords_linear_fog.gd"
const FOG_SHADER_PATH := "res://src/retail_slice/fords_linear_fog.glsl"

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_LINEAR_FOG_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var fog_script = load(FOG_SCRIPT_PATH)
	var shader_file := load(FOG_SHADER_PATH) as RDShaderFile
	_check("linear_fog_dependencies_load", fog_script != null and shader_file != null)
	if fog_script == null or shader_file == null:
		_finish()
		return

	var source := FileAccess.get_file_as_string(FOG_SCRIPT_PATH)
	var shader_source := FileAccess.get_file_as_string(FOG_SHADER_PATH)
	var fog = fog_script.new()
	_check("effect_refuses_unconfigured_compositor", not fog.is_configured() and fog.create_compositor() == null)
	_check("effect_rejects_non_finite_color", fog.configure_exact(Color(NAN, 0.0, 0.0, 1.0), 350.0, 2000.0, 0.5) != "")
	_check("effect_rejects_invalid_start", fog.configure_exact(Color.WHITE, -1.0, 2000.0, 0.5) != "")
	_check("effect_rejects_invalid_end", fog.configure_exact(Color.WHITE, 350.0, 350.0, 0.5) != "")
	_check("effect_rejects_missing_or_invalid_scale", fog.configure_exact(Color.WHITE, 350.0, 2000.0, 0.0) != "" and fog.configure_exact(Color.WHITE, 350.0, 2000.0, NAN) != "")
	var compiled_color := Color(220.0 / 255.0, 226.0 / 255.0, 235.0 / 255.0, 1.0)
	_check("effect_accepts_compiled_parameters_and_uniform_scale", fog.configure_exact(compiled_color, 350.0, 2000.0, 0.5) == "" and fog.is_configured())

	var contract: Dictionary = fog.runtime_contract()
	_check("configured_source_parameters_are_retained", _color_near(contract.get("source_color", Color.TRANSPARENT), compiled_color) and is_equal_approx(float(contract.get("source_start", 0.0)), 350.0) and is_equal_approx(float(contract.get("source_end", 0.0)), 2000.0))
	_check("source_to_local_scale_is_exact", is_equal_approx(float(contract.get("fog_start_local", 0.0)), 175.0) and is_equal_approx(float(contract.get("fog_end_local", 0.0)), 1000.0))
	_check("curve_is_camera_depth_linear", is_equal_approx(fog.fog_factor_for_camera_depth(174.0, 175.0, 1000.0), 0.0) and is_equal_approx(fog.fog_factor_for_camera_depth(175.0, 175.0, 1000.0), 0.0) and is_equal_approx(fog.fog_factor_for_camera_depth(587.5, 175.0, 1000.0), 0.5) and is_equal_approx(fog.fog_factor_for_camera_depth(1000.0, 175.0, 1000.0), 1.0) and is_equal_approx(fog.fog_factor_for_camera_depth(1001.0, 175.0, 1000.0), 1.0))
	_check("invalid_curve_inputs_fail_visible", is_nan(fog.fog_factor_for_camera_depth(NAN, 175.0, 1000.0)) and is_nan(fog.fog_factor_for_camera_depth(500.0, 1000.0, 175.0)))
	_check("configured_compositor_contains_only_exact_effect", fog.create_compositor() != null and fog.create_compositor().compositor_effects.size() == 1 and fog.create_compositor().compositor_effects[0] == fog)
	_check("resolved_depth_is_requested", fog.access_resolved_depth and fog.effect_callback_type == CompositorEffect.EFFECT_CALLBACK_TYPE_POST_TRANSPARENT)

	var shader_versions := shader_file.get_version_list()
	var spirv: RDShaderSPIRV = shader_file.get_spirv(shader_versions[0]) if shader_versions.size() == 1 else null
	_check("compute_shader_compiles", spirv != null and spirv.compile_error_compute == "", spirv.compile_error_compute if spirv != null else "no SPIR-V")
	_check("shader_reconstructs_camera_depth", shader_source.contains("params.inv_projection * vec4(ndc_xy, raw_depth, 1.0)") and shader_source.contains("camera_depth = -(view_position.z / view_position.w)"))
	_check("shader_uses_exact_linear_curve", shader_source.contains("(camera_depth - params.fog_start) / (params.fog_end - params.fog_start)"))
	_check("shader_has_no_exponential_approximation", not shader_source.contains("exp(") and not shader_source.contains("exp2(") and not source.contains("fog_density") and not source.contains("fog_height"))
	_check("no_coordinate_scale_is_guessed", source.contains("an explicit positive finite uniform map scale is required") and not source.contains("0.026492327"))
	_check("transparent_gap_and_clear_depth_semantics_are_explicit", String(contract.get("transparent_depth_status", "")).contains("unresolved") and String(contract.get("sky_depth_status", "")).contains("reverse-z clear depth"))
	var projection_probe := Projection(
		Vector4(1.0, 2.0, 3.0, 4.0),
		Vector4(5.0, 6.0, 7.0, 8.0),
		Vector4(9.0, 10.0, 11.0, 12.0),
		Vector4(13.0, 14.0, 15.0, 16.0)
	)
	_check("projection_push_columns_are_unmodified", fog._projection_push_data(projection_probe) == PackedFloat32Array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0]))

	var render_root := Node3D.new()
	root.add_child(render_root)
	var camera := Camera3D.new()
	camera.position = Vector3(0.0, 0.0, 3.0)
	camera.current = true
	camera.compositor = fog.create_compositor()
	render_root.add_child(camera)
	var probe_mesh := MeshInstance3D.new()
	probe_mesh.mesh = BoxMesh.new()
	render_root.add_child(probe_mesh)
	for frame in 5:
		await process_frame
	var execution: Dictionary = fog.execution_metrics()
	var initialization_error: String = String(fog.initialization_error())
	var headless_device_blocker: bool = initialization_error == "RenderingDevice unavailable; Forward+ compositor cannot execute"
	_check("pipeline_or_exact_headless_blocker", initialization_error == "" or headless_device_blocker, initialization_error)
	_check("dispatch_state_matches_device_availability", (initialization_error == "" and int(execution.get("dispatch_count", 0)) > 0 and Vector2i(execution.get("last_render_size", Vector2i.ZERO)).x > 0) or (headless_device_blocker and int(execution.get("dispatch_count", -1)) == 0))

	print("RETAIL_LINEAR_FOG_METRICS source_start=%.1f source_end=%.1f local_start=%.1f local_end=%.1f dispatches=%d headless_device_blocker=%s" % [float(contract.get("source_start", 0.0)), float(contract.get("source_end", 0.0)), float(contract.get("fog_start_local", 0.0)), float(contract.get("fog_end_local", 0.0)), int(execution.get("dispatch_count", 0)), headless_device_blocker])
	render_root.queue_free()
	await process_frame
	fog = null
	_finish()


func _color_near(value: Variant, expected: Color) -> bool:
	if not value is Color:
		return false
	var actual := value as Color
	return is_equal_approx(actual.r, expected.r) and is_equal_approx(actual.g, expected.g) and is_equal_approx(actual.b, expected.b) and is_equal_approx(actual.a, expected.a)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_LINEAR_FOG PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_LINEAR_FOG FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_LINEAR_FOG_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
