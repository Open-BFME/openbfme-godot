extends SceneTree
## Gate for the fail-closed retail main-menu shell APT binder.
##
## Mirrors tests/retail_hud_apt_runtime_runner.gd: a synthetic pack declares the
## `shellScene` key, and the runtime must bind the exact contract, refuse a
## contract with unsupported semantics unless the static subset is explicitly
## enabled, and never claim presentation or parity on a rejection.

var passed := 0
var failed := 0
var runtime_script
var fixture_root := ""


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_SHELL_APT_RUNTIME_RUNNER")
	create_timer(20.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	runtime_script = load("res://src/ui/retail_shell_apt_runtime.gd")
	_check("runtime_script_compiles", runtime_script != null)
	var menu_script = load("res://src/ui/main_menu.gd")
	_check("main_menu_still_compiles", menu_script != null)
	if runtime_script == null:
		_finish()
		return

	fixture_root = ProjectSettings.globalize_path("user://openbfme-shell-apt-runtime-fixture")
	_cleanup_fixture()
	_check("fixture_directory_created", DirAccess.make_dir_recursive_absolute(fixture_root) == OK)
	_check("fixture_atlas_written", _write_atlas(fixture_root.path_join("assets/ui/shell/atlases/apt-menuexport-2-268de9714542.png")))
	_check("fixture_pack_written", _write_json(fixture_root.path_join("pack.json"), {
		"id": "shell-apt-runtime-fixture",
		"files": {"shellScene": "data/ui/shell/scene-contract.json"},
	}))
	var document := _document()
	_check("fixture_contract_written", _write_json(fixture_root.path_join("data/ui/shell/scene-contract.json"), document))

	# 1. A pack without the key is the fallback path, not a failure.
	var bare_root := ProjectSettings.globalize_path("user://openbfme-shell-apt-runtime-bare")
	_remove_tree(bare_root)
	DirAccess.make_dir_recursive_absolute(bare_root)
	_write_json(bare_root.path_join("pack.json"), {"id": "bare", "files": {}})
	var bare_runtime = runtime_script.new()
	root.add_child(bare_runtime)
	var bare_result := bool(bare_runtime.configure_from_pack(bare_root))
	_check(
		"pack_without_shell_key_is_fallback_not_failure",
		bare_result and not bool(bare_runtime.contract_declared) and not bool(bare_runtime.presentation_ready),
		"result=%s declared=%s" % [bare_result, bare_runtime.contract_declared]
	)

	# 2. Unsupported semantics fail closed unless explicitly opted into.
	var strict_runtime = runtime_script.new()
	root.add_child(strict_runtime)
	var strict_result := bool(strict_runtime.configure_from_pack(fixture_root))
	_check(
		"unsupported_semantics_fail_closed_by_default",
		not strict_result and bool(strict_runtime.contract_declared),
		"result=%s declared=%s diag=%s" % [strict_result, strict_runtime.contract_declared, strict_runtime.diagnostics]
	)
	_check(
		"strict_failure_never_claims_presentation_or_parity",
		not bool(strict_runtime.presentation_ready) and not bool(strict_runtime.parity_ready)
	)

	# 3. The explicit static subset binds the exact retail draws.
	var runtime = runtime_script.new()
	runtime.size = Vector2(1024.0, 768.0)
	root.add_child(runtime)
	var result := bool(runtime.configure_from_pack(fixture_root, true))
	_check("exact_shell_scene_key_binds", result, "diag=%s" % [runtime.diagnostics])
	_check(
		"retail_triangles_are_executable",
		bool(runtime.contract_ready) and bool(runtime.presentation_ready) and int(runtime.draw_count) == 2,
		"ready=%s draws=%s" % [runtime.contract_ready, runtime.draw_count]
	)
	_check("atlas_bound_from_pack", int(runtime.atlas_count) == 1)
	_check(
		"opt_in_is_never_parity",
		bool(runtime.static_subset_opt_in) and not bool(runtime.parity_ready) and int(runtime.blocker_count) == 2
	)
	_check(
		"native_view3d_backdrop_is_reported_not_substituted",
		bool(runtime.requires_native_backdrop)
	)
	var metadata := runtime.runtime_metadata() as Dictionary
	_check(
		"runtime_metadata_reports_identity",
		String(metadata.get("aggregateSha256", "")).length() == 64
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

	# 4. Identity/bounds violations fail closed.
	_check("wrong_scene_id_fails_closed", not _configures(_mutated({"sceneId": "bfme2.ui.palantir"})))
	_check("wrong_schema_fails_closed", not _configures(_mutated({"schema": "openbfme.retail-hud-apt-runtime"})))
	_check("bad_digest_fails_closed", not _configures(_mutated({"aggregateSha256": "not-a-digest"})))
	_check("unsafe_atlas_path_fails_closed", not _configures(_mutated({"atlases": ["../../etc/passwd.png"]})))
	var loose_policy := (_document()["renderPolicy"] as Dictionary).duplicate(true)
	loose_policy["actionScriptExecuted"] = true
	_check("action_script_execution_claim_fails_closed", not _configures(_mutated({"renderPolicy": loose_policy})))
	var synthetic_policy := (_document()["renderPolicy"] as Dictionary).duplicate(true)
	synthetic_policy["syntheticFallbackAllowed"] = true
	_check("synthetic_fallback_claim_fails_closed", not _configures(_mutated({"renderPolicy": synthetic_policy})))
	var missing_atlas_draws := (_document()["draws"] as Array).duplicate(true)
	(missing_atlas_draws[0] as Dictionary)["atlas"] = "assets/ui/shell/atlases/absent-000000000000.png"
	_check("unbound_atlas_reference_fails_closed", not _configures(_mutated({"draws": missing_atlas_draws})))
	var bad_draws := (_document()["draws"] as Array).duplicate(true)
	(bad_draws[1] as Dictionary)["points"] = [[0.0, 0.0], [1.0, 1.0]]
	_check("non_triangle_draw_fails_closed", not _configures(_mutated({"draws": bad_draws})))
	_check("empty_draw_list_never_presents", not _configures(_mutated({"draws": []})))

	_finish()


func _configures(document: Dictionary) -> bool:
	var mutated_root := ProjectSettings.globalize_path("user://openbfme-shell-apt-runtime-mutated")
	_remove_tree(mutated_root)
	DirAccess.make_dir_recursive_absolute(mutated_root)
	_write_atlas(mutated_root.path_join("assets/ui/shell/atlases/apt-menuexport-2-268de9714542.png"))
	_write_json(mutated_root.path_join("pack.json"), {
		"id": "shell-apt-runtime-mutated",
		"files": {"shellScene": "data/ui/shell/scene-contract.json"},
	})
	_write_json(mutated_root.path_join("data/ui/shell/scene-contract.json"), document)
	var runtime = runtime_script.new()
	runtime.size = Vector2(1024.0, 768.0)
	root.add_child(runtime)
	var ok := bool(runtime.configure_from_pack(mutated_root, true)) and bool(runtime.presentation_ready)
	runtime.queue_free()
	return ok


func _mutated(overrides: Dictionary) -> Dictionary:
	var document := _document()
	for key in overrides:
		document[key] = overrides[key]
	return document


func _document() -> Dictionary:
	return {
		"schema": "openbfme.retail-shell-apt-runtime",
		"schemaVersion": 0,
		"sceneId": "bfme2.ui.shell.mainmenu",
		"aggregateSha256": "0ad5251e5b3cdfb9559303593d4d43daeff35a991f3f0c9b1ce8c94f0f9e6c8c",
		"authoredResolution": [1024, 768],
		"renderPolicy": {
			"actionScriptExecuted": false,
			"boundedActionScriptSubsetExecuted": false,
			"defaultRuntimeMode": "fail-closed",
			"staticSubsetRequiresExplicitOptIn": true,
			"syntheticFallbackAllowed": false,
			"exactTimelineDisplayLists": true,
			"timelinePlaybackBound": false,
			"nativeBackdropBound": false,
			"nativeGadgetsBound": false,
		},
		"atlases": ["assets/ui/shell/atlases/apt-menuexport-2-268de9714542.png"],
		"draws": [
			{
				"kind": "textured-triangle",
				"movie": "MenuExport",
				"displayOrder": 0,
				"atlas": "assets/ui/shell/atlases/apt-menuexport-2-268de9714542.png",
				"points": [[752.0, 737.0], [752.0, 685.0], [886.0, 685.0]],
				"uvs": [[0.0, 0.25], [0.0, 0.125], [0.125, 0.125]],
				"color": [1.0, 1.0, 1.0, 1.0],
			},
			{
				"kind": "solid-triangle",
				"movie": "MainMenu",
				"displayOrder": 1,
				"points": [[900.0, 739.0], [900.0, 686.0], [1008.0, 686.0]],
				"color": [0.8, 0.0, 0.0, 1.0],
			},
		],
		"buttonInstances": [
			{
				"buttonId": "mainmenu:103",
				"path": "layer:1:MainMenu/389",
				"hitVertices": [[0.0, 0.0], [100.0, 0.0], [100.0, 40.0], [0.0, 40.0]],
				"hitTransform": {"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [10.0, 20.0]},
			},
		],
		"unsupportedSemantics": [
			{"code": "timeline-playback-not-bound", "movie": "shell APT closure"},
			{"code": "shell-backdrop-requires-native-view3d", "movie": "Background"},
		],
		"summary": {"drawCount": 2, "parityReady": false},
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


func _cleanup_fixture() -> void:
	_remove_tree(fixture_root)


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
	print("SHELL_APT_RUNTIME_SUMMARY passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
