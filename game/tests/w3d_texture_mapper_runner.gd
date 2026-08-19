extends SceneTree
## Retail W3D texture mappers animate at runtime (owner playtest 2026-08-19,
## queue Q47: "Men fortress flags do not sway"). Retail authors the fortress
## banner GBFortress.GBFFLAG as a GRID flip-book (stage0_mapping 7,
## "FPS=15.0; Log2Width=2; Last=16" over EXGFlagSeq.tga) and the torches
## (FLAMES, EXFireTorchSeq.tga) the same way; the shipped GLB already carries
## both in material extras. This runner pins:
##   1. the mapper parser against the exact retail argument strings;
##   2. the W3D GridClassMapper arithmetic (frame -> uv scale/offset);
##   3. loading the shipped Men fortress GLB through AssetFactory registers
##      GBFFLAG and FLAMES as animated and leaves GBFORTRESS alone;
##   4. the live Men slice ticks them (uv1_offset changes across frames).
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/w3d_texture_mapper_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const MappersScript := preload("res://src/view/w3d_texture_mappers.gd")
## Loaded after the tree is up: AssetFactory depends on the ContentDB
## autoload, which does not exist yet while a SceneTree script is parsed.
var AssetFactoryScript: GDScript = null
const BOOT_DEADLINE_MS := 240000
const RETAIL_FLAG_ARGS := "FPS=15.0; The frames per second, Log2Width=2; So 0=width 1, 1=width 2, 2=width 4. The default means animate using a texture divided up into quarters., Last=16; The last frame to use"
const RETAIL_FLAME_ARGS := "FPS=15, Log2Width=2"

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _init() -> void:
	_runner_watchdog.start(self, "W3D_TEXTURE_MAPPER_RUNNER", 480000)
	await process_frame
	AssetFactoryScript = load("res://src/view/asset_factory.gd")
	# ---- 1. parser on the exact retail strings ----
	var args := MappersScript.parse_args(RETAIL_FLAG_ARGS)
	_check("parses_retail_flag_args", is_equal_approx(float(args.get("fps", 0.0)), 15.0)
		and int(args.get("log2width", -1)) == 2 and int(args.get("last", -1)) == 16, str(args))
	var flag_mapper := MappersScript.parse_mapper({"stage0_mapping": 7, "vm_args_0": RETAIL_FLAG_ARGS})
	_check("flag_is_a_4x4_grid_at_15fps_16_frames", String(flag_mapper.get("kind", "")) == "grid"
		and int(flag_mapper.get("width", 0)) == 4 and is_equal_approx(float(flag_mapper.get("fps", 0.0)), 15.0)
		and int(flag_mapper.get("last", 0)) == 16, str(flag_mapper))
	var flame_mapper := MappersScript.parse_mapper({"stage0_mapping": 7, "vm_args_0": RETAIL_FLAME_ARGS})
	_check("flame_grid_defaults_last_to_all_cells", String(flame_mapper.get("kind", "")) == "grid"
		and int(flame_mapper.get("last", 0)) == 16, str(flame_mapper))
	_check("plain_uv_mapper_is_not_animated", MappersScript.parse_mapper({"stage0_mapping": 0, "vm_args_0": ""}).is_empty())
	_check("missing_extras_are_not_animated", MappersScript.parse_mapper({}).is_empty())
	var linear := MappersScript.parse_mapper({"stage0_mapping": 4, "vm_args_0": "UPerSec=0.0\r\nVPerSec=-0.5"})
	_check("linear_offset_mapper_parses_signed_rates", String(linear.get("kind", "")) == "linear"
		and is_equal_approx(float(linear.get("v_per_sec", 0.0)), -0.5), str(linear))
	# ---- 2. GridClassMapper arithmetic ----
	var t0 := MappersScript.uv_transform_for(flag_mapper, 0.0)
	_check("frame0_is_top_left_quarter_cell", (t0["scale"] as Vector3).is_equal_approx(Vector3(0.25, 0.25, 1.0))
		and (t0["offset"] as Vector3).is_equal_approx(Vector3.ZERO), str(t0))
	var t1 := MappersScript.uv_transform_for(flag_mapper, 1.0 / 15.0 + 0.001)
	_check("frame1_steps_one_cell_right", (t1["offset"] as Vector3).is_equal_approx(Vector3(0.25, 0.0, 0.0)), str(t1))
	var t4 := MappersScript.uv_transform_for(flag_mapper, 4.0 / 15.0 + 0.001)
	_check("frame4_wraps_to_second_row", (t4["offset"] as Vector3).is_equal_approx(Vector3(0.0, 0.25, 0.0)), str(t4))
	var t15 := MappersScript.uv_transform_for(flag_mapper, 15.0 / 15.0 + 0.001)
	_check("frame15_is_bottom_right", (t15["offset"] as Vector3).is_equal_approx(Vector3(0.75, 0.75, 0.0)), str(t15))
	var t16 := MappersScript.uv_transform_for(flag_mapper, 16.0 / 15.0 + 0.001)
	_check("frame16_loops_to_frame0", (t16["offset"] as Vector3).is_equal_approx(Vector3.ZERO), str(t16))
	# ---- 3. the shipped Men fortress GLB through AssetFactory ----
	MappersScript.clear()
	var content_root := OS.get_environment("OPENBFME_CONTENT")
	var glb := _find_men_fortress_glb(content_root)
	_check("men_fortress_glb_present", glb != "", "content=%s" % content_root)
	if glb != "":
		var model: Node3D = AssetFactoryScript._try_load_model(glb)
		_check("men_fortress_glb_loads", model != null)
		_check("fortress_flag_and_torches_registered_as_animated", MappersScript.animated_count() == 2,
			"animated=%d" % MappersScript.animated_count())
		var flag_material: BaseMaterial3D = null
		var body_material: BaseMaterial3D = null
		if model != null:
			var stack: Array = [model]
			while not stack.is_empty():
				var node = stack.pop_back()
				if node is MeshInstance3D:
					var mi := node as MeshInstance3D
					var m := mi.get_active_material(0)
					if String(mi.name) == "GBFFLAG":
						flag_material = m as BaseMaterial3D
					elif String(mi.name) == "GBFORTRESS":
						body_material = m as BaseMaterial3D
				for c in node.get_children():
					stack.append(c)
		_check("flag_material_carries_the_grid_mapper", flag_material != null
			and flag_material.has_meta(MappersScript.META_KEY)
			and String((flag_material.get_meta(MappersScript.META_KEY) as Dictionary).get("kind", "")) == "grid")
		_check("fortress_body_is_not_animated", body_material != null and not body_material.has_meta(MappersScript.META_KEY))
		if flag_material != null:
			MappersScript.advance(0.0)
			var off0 := flag_material.uv1_offset
			MappersScript.advance(1.0 / 15.0 + 0.001)
			var off1 := flag_material.uv1_offset
			_check("advancing_the_clock_moves_the_flag_uv", off0.is_equal_approx(Vector3.ZERO)
				and off1.is_equal_approx(Vector3(0.25, 0.0, 0.0)) and flag_material.uv1_scale.is_equal_approx(Vector3(0.25, 0.25, 1.0)),
				"off0=%s off1=%s scale=%s" % [str(off0), str(off1), str(flag_material.uv1_scale)])
		if model != null:
			model.free()
	# ---- 4. the live Men slice ticks the mappers ----
	MappersScript.clear()
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", "rotwk.map.adorn-river")
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if _check("scene_loads", scene != null):
		var slice = scene.instantiate()
		root.add_child(slice)
		var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
		while Time.get_ticks_msec() < deadline:
			await process_frame
			if bool(slice.ready_ok) or String(slice.failure_reason) != "":
				break
		if _check("men_slice_ready", bool(slice.ready_ok), String(slice.failure_reason)):
			_check("live_slice_registered_animated_mappers", MappersScript.animated_count() > 0,
				"animated=%d" % MappersScript.animated_count())
			var samples: Array[Vector3] = []
			var live_flag: BaseMaterial3D = null
			for material in MappersScript._animated:
				if material is BaseMaterial3D and String((material.get_meta(MappersScript.META_KEY) as Dictionary).get("kind", "")) == "grid":
					live_flag = material as BaseMaterial3D
					break
			if live_flag != null:
				for i in 24:
					await process_frame
					var offset := live_flag.uv1_offset
					if samples.is_empty() or not samples[samples.size() - 1].is_equal_approx(offset):
						samples.append(offset)
			_check("live_slice_flip_book_advances_across_frames", samples.size() >= 2, "distinct offsets=%d" % samples.size())
		root.remove_child(slice)
		slice.free()
		await process_frame
	_finish()


func _find_men_fortress_glb(content_root: String) -> String:
	if content_root == "":
		return ""
	var packs := DirAccess.open(content_root.path_join("rotwk-men-vslice"))
	if packs == null:
		return ""
	var newest := ""
	var newest_time := 0
	for digest in packs.get_directories():
		var candidate := content_root.path_join("rotwk-men-vslice").path_join(digest).path_join("assets/models/structures/menfortress/construction-intact-gbfortress.glb")
		if FileAccess.file_exists(candidate):
			var stamp := FileAccess.get_modified_time(candidate)
			if stamp > newest_time:
				newest_time = stamp
				newest = candidate
	return newest


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		_passed += 1
		print("W3D_TEXTURE_MAPPER PASS %s" % name)
	else:
		_failed += 1
		print("W3D_TEXTURE_MAPPER FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("W3D_TEXTURE_MAPPER_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
