extends SceneTree
## Gate for the fail-closed retail SCREEN APT binder.
##
## Mirrors tests/retail_shell_apt_runtime_runner.gd, but the screen lane is
## addressed by MOVIE NAME rather than by a pinned pack key, because the
## importer cooks 62 of them (queue Q117). What this gate is really protecting
## is that a screen can never be presented in a state nobody chose: the
## contract must say which authored frame it was cooked at and by which rule,
## and a rule this runtime has not been told about is a rejection.

var passed := 0
var failed := 0
var runtime_script
var fixture_root := ""

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

const ATLAS_RELATIVE := "assets/ui/screens/spellstore/apt-spellstore-1-6f967901b486.png"
const CONTRACT_RELATIVE := "data/ui/screens/spellstore/scene-contract.json"


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_SCREEN_APT_RUNTIME_RUNNER")
	create_timer(20.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	runtime_script = load("res://src/ui/retail_screen_apt_runtime.gd")
	_check("runtime_script_compiles", runtime_script != null)
	if runtime_script == null:
		_finish()
		return

	fixture_root = ProjectSettings.globalize_path("user://openbfme-screen-apt-runtime-fixture")
	_remove_tree(fixture_root)
	_check("fixture_directory_created", DirAccess.make_dir_recursive_absolute(fixture_root) == OK)
	_check("fixture_atlas_written", _write_atlas(fixture_root.path_join(ATLAS_RELATIVE)))
	_write_json(fixture_root.path_join("pack.json"), {"id": "screen-apt-runtime-fixture", "files": {}})
	_check(
		"fixture_contract_written",
		_write_json(fixture_root.path_join(CONTRACT_RELATIVE), _document())
	)

	# 1. A pack that does not ship the screen is the fallback path, not a failure.
	var absent = runtime_script.new()
	root.add_child(absent)
	var absent_result := bool(absent.configure_from_pack(fixture_root, "ScoreScreen"))
	_check(
		"pack_without_the_screen_is_fallback_not_failure",
		absent_result
		and not bool(absent.contract_declared)
		and not bool(absent.presentation_ready),
		"result=%s declared=%s" % [absent_result, absent.contract_declared]
	)

	# 2. Named gaps fail closed unless the caller explicitly opts in. EVERY
	#    cooked screen carries blockers, because the importer names its gaps.
	var strict = runtime_script.new()
	root.add_child(strict)
	var strict_result := bool(strict.configure_from_pack(fixture_root, "SpellStore"))
	_check(
		"unsupported_semantics_fail_closed_by_default",
		not strict_result and bool(strict.contract_declared),
		"result=%s diag=%s" % [strict_result, strict.diagnostics]
	)
	_check(
		"strict_failure_never_claims_presentation_or_parity",
		not bool(strict.presentation_ready) and not bool(strict.parity_ready)
	)

	# 3. The explicit static subset binds the exact retail draws.
	var runtime = runtime_script.new()
	runtime.size = Vector2(1024.0, 768.0)
	root.add_child(runtime)
	var result := bool(runtime.configure_from_pack(fixture_root, "SpellStore", true))
	_check("cooked_screen_binds_by_movie_name", result, "diag=%s" % [runtime.diagnostics])
	_check(
		"retail_triangles_are_executable",
		bool(runtime.contract_ready)
		and bool(runtime.presentation_ready)
		and int(runtime.draw_count) == 2,
		"ready=%s draws=%s" % [runtime.contract_ready, runtime.draw_count]
	)
	_check("atlas_bound_from_pack", int(runtime.atlas_count) == 1)
	_check(
		"opt_in_is_never_parity",
		bool(runtime.static_subset_opt_in)
		and not bool(runtime.parity_ready)
		and int(runtime.blocker_count) == 2
	)
	# The whole point of the screen lane: WHICH authored state is on screen.
	_check(
		"the_bound_frame_state_is_reported",
		String(runtime.frame_label) == "_open"
		and int(runtime.frame_index) == 0
		and String(runtime.frame_rule) == "authored-open-label",
		"label=%s frame=%s rule=%s" % [runtime.frame_label, runtime.frame_index, runtime.frame_rule]
	)
	var metadata := runtime.runtime_metadata() as Dictionary
	_check(
		"runtime_metadata_reports_identity",
		String(metadata.get("movie", "")) == "SpellStore"
		and String(metadata.get("sourceAggregateSha256", "")).length() == 64
		and int(metadata.get("drawCount", -1)) == 2
		and not bool(metadata.get("parityReady", true))
	)
	var regions := runtime.button_regions() as Array
	_check("button_hit_regions_are_exposed", regions.size() == 1, "regions=%s" % [regions.size()])
	if regions.size() == 1:
		var rect := (regions[0] as Dictionary)["rect"] as Rect2
		_check(
			"button_hit_region_is_scaled_into_control_space",
			is_equal_approx(rect.position.x, 10.0) and is_equal_approx(rect.size.x, 100.0),
			"rect=%s" % [rect]
		)

	# 4. Identity, state and bounds violations fail closed.
	_check(
		"wrong_schema_fails_closed",
		not _configures(_mutated({"schema": "openbfme.retail-shell-apt-runtime"}))
	)
	_check(
		"a_contract_for_another_movie_fails_closed",
		not _configures(_mutated({"movie": "ScoreScreen"}))
	)
	_check(
		"bad_source_digest_fails_closed",
		not _configures(_mutated({"sourceAggregateSha256": "not-a-digest"}))
	)
	# A frame rule this runtime has not been told about would silently present
	# an unknown authored state - the one failure this lane must never have.
	var unknown_rule := (_document()["frameSelection"] as Dictionary).duplicate(true)
	unknown_rule["rule"] = "largest-cumulative-display-list"
	_check(
		"an_unknown_frame_rule_fails_closed",
		not _configures(_mutated({"frameSelection": unknown_rule}))
	)
	var negative_frame := (_document()["frameSelection"] as Dictionary).duplicate(true)
	negative_frame["frame"] = -1
	_check(
		"a_negative_frame_index_fails_closed",
		not _configures(_mutated({"frameSelection": negative_frame}))
	)
	var escaped_atlas := (_document()["atlases"] as Array).duplicate(true)
	(escaped_atlas[0] as Dictionary)["cookedPng"] = "../../etc/passwd.png"
	_check("unsafe_atlas_path_fails_closed", not _configures(_mutated({"atlases": escaped_atlas})))
	var missing_atlas_draws := (_document()["draws"] as Array).duplicate(true)
	(missing_atlas_draws[0] as Dictionary)["atlas"] = "assets/ui/screens/spellstore/absent.png"
	_check(
		"unbound_atlas_reference_fails_closed",
		not _configures(_mutated({"draws": missing_atlas_draws}))
	)
	var bad_draws := (_document()["draws"] as Array).duplicate(true)
	(bad_draws[1] as Dictionary)["points"] = [[0.0, 0.0], [1.0, 1.0]]
	_check("non_triangle_draw_fails_closed", not _configures(_mutated({"draws": bad_draws})))
	_check("empty_draw_list_never_presents", not _configures(_mutated({"draws": []})))
	_check(
		"a_blocker_row_without_a_code_fails_closed",
		not _configures(_mutated({"blockers": [{"movie": "SpellStore"}]}))
	)

	_finish()


func _configures(document: Dictionary) -> bool:
	var mutated_root := ProjectSettings.globalize_path("user://openbfme-screen-apt-runtime-mutated")
	_remove_tree(mutated_root)
	DirAccess.make_dir_recursive_absolute(mutated_root)
	_write_atlas(mutated_root.path_join(ATLAS_RELATIVE))
	_write_json(mutated_root.path_join("pack.json"), {"id": "screen-apt-runtime-mutated", "files": {}})
	_write_json(mutated_root.path_join(CONTRACT_RELATIVE), document)
	var runtime = runtime_script.new()
	runtime.size = Vector2(1024.0, 768.0)
	root.add_child(runtime)
	var ok := (
		bool(runtime.configure_from_pack(mutated_root, "SpellStore", true))
		and bool(runtime.presentation_ready)
	)
	runtime.queue_free()
	return ok


func _mutated(overrides: Dictionary) -> Dictionary:
	var document := _document()
	for key in overrides:
		document[key] = overrides[key]
	return document


func _document() -> Dictionary:
	return {
		"schema": "openbfme.retail-screen-scene",
		"schemaVersion": 0,
		"movie": "SpellStore",
		"closure": ["SpellStore"],
		"unplannableImports": [],
		"frameSelection": {
			"frame": 0,
			"label": "_open",
			"rule": "authored-open-label",
			"priority": ["_open", "_show", "_init", "_fadeIn"],
			"availableLabels": {"_close": 17, "_open": 0},
		},
		"stage": {
			"width": 1024,
			"height": 768,
			"frameCount": 32,
			"millisecondsPerFrame": 33,
		},
		"sourceAggregateSha256": "87a81965753ce14da688ebe283104fec7c8c567d4a5e7d4ff3dfedf820afcdf6",
		"atlases": [
			{
				"virtualPath": "art/Textures/apt_SpellStore_1.tga",
				"cookedPng": ATLAS_RELATIVE,
				"width": 1024,
				"height": 1024,
			},
		],
		"draws": [
			{
				"kind": "textured-triangle",
				"movie": "SpellStore",
				"displayOrder": 0,
				"atlas": ATLAS_RELATIVE,
				"points": [[752.0, 737.0], [752.0, 685.0], [886.0, 685.0]],
				"uvs": [[0.0, 0.25], [0.0, 0.125], [0.125, 0.125]],
				"color": [1.0, 1.0, 1.0, 1.0],
			},
			{
				"kind": "solid-triangle",
				"movie": "SpellStore",
				"displayOrder": 1,
				"points": [[900.0, 739.0], [900.0, 686.0], [1008.0, 686.0]],
				"color": [0.8, 0.0, 0.0, 1.0],
			},
		],
		"buttonInstances": [
			{
				"buttonId": "spellstore:41",
				"path": "layer:0:SpellStore/12",
				"hitVertices": [[0.0, 0.0], [100.0, 0.0], [100.0, 40.0], [0.0, 40.0]],
				"hitTransform": {"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [10.0, 20.0]},
			},
		],
		"blockers": [
			{"code": "action-script-unsupported-opcodes", "movie": "SpellStore"},
			{"code": "text-font-payload-not-bundled", "movie": "SpellStore"},
		],
		"blockerCounts": {
			"action-script-unsupported-opcodes": 1,
			"text-font-payload-not-bundled": 1,
		},
		"totals": {"draws": 2, "blockers": 2},
	}


func _write_atlas(path: String) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var image := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	image.fill(Color(1.0, 1.0, 1.0, 1.0))
	return image.save_png(path) == OK


func _write_json(path: String, document: Dictionary) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(document))
	file.close()
	return true


func _remove_tree(path: String) -> void:
	if path == "" or not DirAccess.dir_exists_absolute(path):
		return
	var dir := DirAccess.open(path)
	if dir == null:
		return
	dir.list_dir_begin()
	var name := dir.get_next()
	while name != "":
		var child := path.path_join(name)
		if dir.current_is_dir():
			_remove_tree(child)
		else:
			DirAccess.remove_absolute(child)
		name = dir.get_next()
	dir.list_dir_end()
	DirAccess.remove_absolute(path)


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s %s" % [label, detail])


func _watchdog_timeout() -> void:
	print("FAIL watchdog_timeout")
	failed += 1
	_finish()


func _finish() -> void:
	print("SCREEN_APT_RUNTIME_SUMMARY passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
