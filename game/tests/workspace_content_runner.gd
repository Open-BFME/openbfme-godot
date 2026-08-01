extends SceneTree
## Workspace-first content resolution test. Proves an env-less launch prefers
## the repo workspace selection over the durable user cache, and that a broken
## workspace falls back to the durable cache with a loud recorded diagnostic.

var passed: int = 0
var failed: int = 0
var _had_cache_setting := false
var _had_selection_setting := false
var _old_cache_setting: Variant
var _old_selection_setting: Variant
var _startup_had_environment := false
var _startup_environment := ""


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	_check("mod_loader_autoload_available", mod_loader != null)
	if mod_loader == null:
		_finish()
		return

	_startup_had_environment = OS.has_environment("OPENBFME_CONTENT")
	_startup_environment = OS.get_environment("OPENBFME_CONTENT")

	# Section 0: report the machine's real default resolution before any fixture
	# overrides. When no env override is set and a usable repo workspace exists,
	# the workspace MUST be the active source — that is the stale-cache fix.
	OS.unset_environment("OPENBFME_CONTENT")
	var default_roots: Array[String] = mod_loader.list_pack_roots()
	var default_source := String(mod_loader.get("active_content_source"))
	print("WORKSPACE_CONTENT default source=%s roots=%d" % [default_source, default_roots.size()])
	for diagnostic in (mod_loader.get("diagnostics") as Array):
		print("WORKSPACE_CONTENT default diagnostic: %s" % String(diagnostic))
	var implicit_workspace := String(mod_loader.call("workspace_content_root"))
	if implicit_workspace != "" and String(mod_loader.call("selected_user_pack_root", implicit_workspace, implicit_workspace.path_join("selection.json"))) != "":
		_check("envless_launch_uses_workspace_selection", default_source == "workspace", default_source)
	else:
		print("WORKSPACE_CONTENT no usable implicit workspace on this machine; skipping envless assertion")

	# Fixture setup: an explicit workspace override plus a fixture durable cache,
	# so every assertion below is deterministic on any machine.
	_had_cache_setting = ProjectSettings.has_setting(mod_loader.PACK_CACHE_SETTING)
	_had_selection_setting = ProjectSettings.has_setting(mod_loader.PACK_SELECTION_SETTING)
	_old_cache_setting = ProjectSettings.get_setting(mod_loader.PACK_CACHE_SETTING) if _had_cache_setting else null
	_old_selection_setting = ProjectSettings.get_setting(mod_loader.PACK_SELECTION_SETTING) if _had_selection_setting else null

	var fixture_root := ProjectSettings.globalize_path("user://workspace-content-test-%d" % Time.get_ticks_usec())
	var workspace_root := fixture_root.path_join("workspace")
	var durable_root := fixture_root.path_join("durable")
	var workspace_pack := workspace_root.path_join("workspace-pack/sha256-fresh")
	var durable_pack := durable_root.path_join("durable-pack/sha256-stale")
	_write_minimal_pack(workspace_pack, "workspace-fresh-test")
	_write_minimal_pack(durable_pack, "durable-stale-test")
	_write_json(workspace_root.path_join("selection.json"), {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "workspace-pack/sha256-fresh",
	})
	_write_json(durable_root.path_join("selection.json"), {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "durable-pack/sha256-stale",
	})
	ProjectSettings.set_setting(mod_loader.PACK_CACHE_SETTING, durable_root)
	ProjectSettings.set_setting(mod_loader.PACK_SELECTION_SETTING, durable_root.path_join("selection.json"))
	ProjectSettings.set_setting(mod_loader.WORKSPACE_CONTENT_SETTING, workspace_root)

	# (a) A usable workspace selection wins over the durable cache.
	var roots: Array[String] = mod_loader.list_pack_roots()
	_check("workspace_selection_preferred", roots.has(workspace_pack), str(roots))
	_check("stale_durable_pack_suppressed", not roots.has(durable_pack), str(roots))
	_check("active_source_is_workspace", String(mod_loader.get("active_content_source")) == "workspace")

	# (b) A corrupt workspace selection falls back to the durable cache and the
	# fallback is diagnosed loudly — never a silent stale load.
	_write_text(workspace_root.path_join("selection.json"), "{ not json !!!")
	roots = mod_loader.list_pack_roots()
	var diagnostics := _joined_diagnostics(mod_loader)
	_check("broken_workspace_falls_back_to_durable", roots.has(durable_pack), str(roots))
	_check("broken_workspace_fallback_diagnosed",
		diagnostics.contains("falling back to the durable user pack cache"), diagnostics)
	_check("active_source_is_durable_after_fallback", String(mod_loader.get("active_content_source")) == "durable")

	# (c) A workspace directory without a selection document is also diagnosed.
	DirAccess.remove_absolute(workspace_root.path_join("selection.json"))
	roots = mod_loader.list_pack_roots()
	diagnostics = _joined_diagnostics(mod_loader)
	_check("selectionless_workspace_diagnosed", diagnostics.contains("has no selection.json"), diagnostics)
	_check("selectionless_workspace_still_falls_back", roots.has(durable_pack), str(roots))

	# (d) An explicit OPENBFME_CONTENT override still beats the workspace.
	_write_json(workspace_root.path_join("selection.json"), {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "workspace-pack/sha256-fresh",
	})
	var external_pack := fixture_root.path_join("external-pack")
	_write_minimal_pack(external_pack, "external-override-test")
	OS.set_environment("OPENBFME_CONTENT", external_pack)
	roots = mod_loader.list_pack_roots()
	_check("env_override_beats_workspace", roots.has(external_pack) and not roots.has(workspace_pack), str(roots))
	_check("active_source_is_external_under_env", String(mod_loader.get("active_content_source")) == "external")

	_restore(mod_loader)
	_finish()


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("WORKSPACE_CONTENT PASS %s" % name)
	else:
		failed += 1
		printerr("WORKSPACE_CONTENT FAIL %s %s" % [name, detail])


func _joined_diagnostics(mod_loader) -> String:
	var parts: Array[String] = []
	for diagnostic in (mod_loader.get("diagnostics") as Array):
		parts.append(String(diagnostic))
	return " | ".join(parts)


func _write_minimal_pack(pack_root: String, id: String) -> void:
	_write_json(pack_root.path_join("pack.json"), {
		"schema": "openbfme.content-pack",
		"schemaVersion": 0,
		"id": id,
		"priority": 900,
		"dataPolicy": {"externalPathsAllowed": false},
		"files": {},
	})


func _write_json(path: String, value: Variant) -> void:
	_write_text(path, JSON.stringify(value, "  ") + "\n")


func _write_text(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(text)
	file.close()


func _restore(mod_loader) -> void:
	ProjectSettings.set_setting(mod_loader.PACK_CACHE_SETTING, _old_cache_setting if _had_cache_setting else null)
	ProjectSettings.set_setting(mod_loader.PACK_SELECTION_SETTING, _old_selection_setting if _had_selection_setting else null)
	ProjectSettings.set_setting(mod_loader.WORKSPACE_CONTENT_SETTING, null)
	if _startup_had_environment:
		OS.set_environment("OPENBFME_CONTENT", _startup_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")


func _finish() -> void:
	print("WORKSPACE_CONTENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
