extends SceneTree
## Focused external content-pack boundary test. Not part of the shared stage gates.

var passed: int = 0
var failed: int = 0
var _cache_root: String = ""
var _selection_path: String = ""
var _old_cache_setting: Variant
var _old_selection_setting: Variant
var _had_cache_setting: bool = false
var _had_selection_setting: bool = false
var _escape_link: String = ""
var _outside_root: String = ""
var _external_content_root: String = ""
var _startup_had_environment := false
var _startup_environment := ""


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "EXTERNAL_PACK_RUNNER")
	call_deferred("_run")


## Environment-selected report mode. When OPENBFME_PARITY_PROFILE_REPORT names a
## path, this runner does NOT run the boundary tests: it reports what a fresh
## process actually mounted for the selection under OPENBFME_CONTENT, so an
## external judge can compare that against the parity contract. The report is
## observation only — it makes no assertion and writes no verdict, because the
## thing under test is the selection this very process just loaded (RULE P8).
const PARITY_PROFILE_REPORT_ENV := "OPENBFME_PARITY_PROFILE_REPORT"
const PARITY_DURABLE_SELECTION_ENV := "OPENBFME_PARITY_DURABLE_SELECTION"
const PARITY_REPORT_SCHEMA := "openbfme.rotwk-parity-profile-report"


func _run() -> void:
	var parity_report_path := OS.get_environment(PARITY_PROFILE_REPORT_ENV).strip_edges()
	if parity_report_path != "":
		_emit_parity_profile_report(parity_report_path)
		return
	_startup_had_environment = OS.has_environment("OPENBFME_CONTENT")
	_startup_environment = OS.get_environment("OPENBFME_CONTENT")
	var mod_loader = root.get_node_or_null("ModLoader")
	var content_db = root.get_node_or_null("ContentDB")
	var game_audio = root.get_node_or_null("GameAudio")
	_check("autoloads_available", mod_loader != null and content_db != null and game_audio != null)
	if mod_loader == null or content_db == null or game_audio == null:
		_finish()
		return

	_cache_root = "user://external-pack-test-%d" % Time.get_ticks_usec()
	_selection_path = _cache_root.path_join("selection.json")
	var pack_relative := "bfme2-men-vslice/sha256-test"
	var pack_root := _cache_root.path_join(pack_relative)
	_build_pack(pack_root)
	_outside_root = "user://external-pack-outside-%d" % Time.get_ticks_usec()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_outside_root))
	var outside_file := _outside_root.path_join("outside.txt")
	var outside_handle := FileAccess.open(outside_file, FileAccess.WRITE)
	if outside_handle != null:
		outside_handle.store_string("outside pack boundary\n")
		outside_handle.close()
	_escape_link = pack_root.path_join("escape-link")
	var link_created := _create_directory_link(_outside_root, _escape_link)

	_check("valid_pack_root", mod_loader.is_valid_pack_root(pack_root))
	_check("safe_nested_relative_path", mod_loader.is_safe_relative_path(pack_relative))
	_check("reject_parent_traversal", not mod_loader.is_safe_relative_path("../escape"))
	_check("reject_absolute_path", not mod_loader.is_safe_relative_path("C:/escape"))
	_check("reject_uri_path", not mod_loader.is_safe_relative_path("res://escape"))
	_check("contained_resolution", mod_loader.resolve_pack_path(pack_root, "assets/models/units/gondor-fighter.glb") == pack_root.path_join("assets/models/units/gondor-fighter.glb"))
	_check("escaped_resolution_empty", mod_loader.resolve_pack_path(pack_root, "../selection.json") == "")
	_check("reparse_fixture_created", link_created, _escape_link)
	_check("reparse_escape_rejected", link_created and mod_loader.resolve_pack_path(pack_root, "escape-link/outside.txt") == "")

	var select_error: String = mod_loader.select_user_pack(pack_relative, _cache_root, _selection_path)
	_check("selection_write", select_error == "", select_error)
	_check("selection_round_trip", mod_loader.selected_user_pack_root(_cache_root, _selection_path) == pack_root)

	_had_cache_setting = ProjectSettings.has_setting(mod_loader.PACK_CACHE_SETTING)
	_had_selection_setting = ProjectSettings.has_setting(mod_loader.PACK_SELECTION_SETTING)
	_old_cache_setting = ProjectSettings.get_setting(mod_loader.PACK_CACHE_SETTING) if _had_cache_setting else null
	_old_selection_setting = ProjectSettings.get_setting(mod_loader.PACK_SELECTION_SETTING) if _had_selection_setting else null
	ProjectSettings.set_setting(mod_loader.PACK_CACHE_SETTING, _cache_root)
	ProjectSettings.set_setting(mod_loader.PACK_SELECTION_SETTING, _selection_path)
	OS.unset_environment("OPENBFME_CONTENT")
	content_db.reload()

	_check("selected_pack_mounted", content_db.pack_roots.has(pack_root))
	_check("v0_object_indexed", content_db.bundle_objects.has("bfme2.object.gondor-soldier"))
	_check("animation_capability_indexed", content_db.animation_capabilities.has("bfme2.animation.gondor-soldier"))
	_check("v0_map_indexed", content_db.bundle_maps.has("bfme2.map.fords-of-isen-ii"))
	_check("map_catalog_second_map_indexed", content_db.bundle_maps.has("bfme2.map.rivendell"))
	var rivendell: Dictionary = content_db.get_bundle_map("bfme2.map.rivendell")
	_check("map_catalog_metadata_merged", int(rivendell.get("playerCount", -1)) == 3 and String(rivendell.get("routingGraphStatus", "")) == "empty-no-authored-navmesh")
	_check("map_catalog_document_authoritative", String(rivendell.get("displayName", "")) == "External Test Rivendell")
	_check("map_catalog_source_tracks_map_document", String(rivendell.get("_source", "")) == pack_root.path_join("maps/rivendell/map.json"))
	_check("map_catalog_tracks_pack_root", String(rivendell.get("_pack_root", "")) == pack_root)
	_check("retail_ui_manifest_indexed", not content_db.get_retail_ui_image("bgbarracks_soldiers").is_empty())
	_check("retail_string_indexed", content_db.get_retail_string("controlbar:constructgondorfighterhorde") == "Synthetic Gondor Soldiers")
	_check("retail_string_case_insensitive", content_db.get_retail_string("CoNtRoLbAr:ToOlTiPbUiLdGoNdOrFiGhTeRhOrDe") == "Synthetic Gondor Soldier tooltip")
	_check("retail_string_fallback", content_db.get_retail_string("missing:string", "fallback") == "fallback")
	_check("retail_audio_event_indexed", not content_db.get_retail_audio_event("gondorsoldiervoiceselect").is_empty())
	_check("retail_audio_sample_indexed", content_db.retail_audio_samples.has("synthetic_select"))
	var definition: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-soldier")
	_check("definition_tracks_pack_root", String(definition.get("_pack_root", "")) == pack_root)
	var expected_model := pack_root.path_join("assets/models/units/gondor-fighter.glb")
	_check("pack_relative_glb_resolves", content_db.resolve_mesh_path(definition) == expected_model, content_db.resolve_mesh_path(definition))
	_check("asset_traversal_rejected", content_db.resolve_asset("../selection.json", pack_root) == "")
	_check("v0_asset_does_not_cross_pack_fallback", content_db.resolve_asset("assets/models/units/aragorn.glb", pack_root) == "")
	var outside_absolute := ProjectSettings.globalize_path(_selection_path)
	_check("absolute_asset_reference_rejected", content_db.resolve_asset(outside_absolute, pack_root) == "")

	var external_audio := pack_root.path_join("assets/audio/music/explore.wav")
	var stream: AudioStream = game_audio._load_audio_stream(external_audio)
	_check("external_wav_loads_without_import_cache", stream != null)
	stream = null

	var asset_factory = load("res://src/view/asset_factory.gd")
	var expected_icon := pack_root.path_join("assets/ui/upgondor-soldier.png")
	_check("pack_relative_ui_texture_resolves", content_db.resolve_icon_path(definition) == expected_icon)
	_check("retail_ui_texture_resolves", content_db.resolve_retail_ui_image_path("BGBarracks_Soldiers") == expected_icon)
	_check("retail_ui_texture_case_insensitive", content_db.resolve_retail_ui_image_path("bGbArRaCkS_sOlDiErS") == expected_icon)
	_check("missing_retail_ui_rejected", content_db.resolve_retail_ui_image_path("MissingImage") == "")
	_check("retail_audio_sample_resolves", content_db.resolve_retail_audio_sample_path("synthetic_select") == external_audio)
	_check("missing_retail_audio_rejected", content_db.resolve_retail_audio_sample_path("missing_sample") == "")
	var texture: Texture2D = asset_factory.load_texture_asset(expected_icon)
	_check("external_texture_loads_without_import_cache", texture != null and texture.get_width() > 0 and texture.get_height() > 0)
	texture = null

	var public_hud = load("res://src/retail_slice/retail_hud.gd").new()
	root.add_child(public_hud)
	public_hud.build()
	var public_fallback_text := String(public_hud.train_button.text)
	var public_bind_error: String = public_hud.bind_retail_train_command(content_db, "", false)
	_check("retail_hud_public_fallback_retained", public_bind_error == "" and public_hud.train_button.icon == null and public_hud.train_button.text == public_fallback_text)
	public_hud.free()

	var retail_hud = load("res://src/retail_slice/retail_hud.gd").new()
	root.add_child(retail_hud)
	retail_hud.build()
	var retail_bind_error: String = retail_hud.bind_retail_train_command(content_db, pack_root, true)
	_check("retail_hud_binding_succeeds", retail_bind_error == "", retail_bind_error)
	_check("retail_hud_icon_texture_bound", retail_hud.train_button.icon != null and retail_hud.train_button.icon.get_width() == 1024 and retail_hud.train_button.icon.get_height() == 1024)
	_check("retail_hud_icon_path_bound", String(retail_hud.train_button.get_meta("retail_icon_path", "")) == expected_icon)
	_check("retail_hud_icon_source_size_preserved", Vector2i(retail_hud.train_button.get_meta("retail_icon_source_size", Vector2i.ZERO)) == Vector2i(1024, 1024))
	_check("retail_hud_icon_aspect_preserved", retail_hud.train_button.expand_icon and is_equal_approx(float(retail_hud.train_button.get_meta("retail_icon_aspect_ratio", 0.0)), 1.0))
	_check("retail_hud_localized_label", retail_hud.train_button.text == "Synthetic Gondor Soldiers")
	_check("retail_hud_localized_tooltip", retail_hud.train_button.tooltip_text == "Synthetic Gondor Soldier tooltip")
	retail_hud.set_train_state(true, "This fallback must not replace retail text")
	_check("retail_hud_localized_label_survives_state_sync", retail_hud.train_button.text == "Synthetic Gondor Soldiers")
	retail_hud.free()

	var tooltip_key := "controlbar:tooltipbuildgondorfighterhorde"
	var saved_tooltip: String = String(content_db.retail_strings.get(tooltip_key, ""))
	content_db.retail_strings.erase(tooltip_key)
	var missing_string_hud = load("res://src/retail_slice/retail_hud.gd").new()
	root.add_child(missing_string_hud)
	missing_string_hud.build()
	var missing_string_error: String = missing_string_hud.bind_retail_train_command(content_db, pack_root, true)
	_check("retail_hud_missing_string_fails_closed", not missing_string_hud.retail_train_command_bound and missing_string_hud.train_button.icon == null and missing_string_error.contains("CONTROLBAR:ToolTipBuildGondorFighterHorde"), missing_string_error)
	missing_string_hud.free()
	content_db.retail_strings[tooltip_key] = saved_tooltip

	var image_key := "bgbarracks_soldiers"
	var saved_image: Dictionary = (content_db.retail_ui_images.get(image_key, {}) as Dictionary).duplicate(true)
	var broken_image := saved_image.duplicate(true)
	broken_image["path"] = "assets/ui/broken.png"
	content_db.retail_ui_images[image_key] = broken_image
	var broken_image_hud = load("res://src/retail_slice/retail_hud.gd").new()
	root.add_child(broken_image_hud)
	broken_image_hud.build()
	var broken_image_error: String = broken_image_hud.bind_retail_train_command(content_db, pack_root, true)
	_check("retail_hud_decode_failure_fails_closed", not broken_image_hud.retail_train_command_bound and broken_image_hud.train_button.icon == null and broken_image_error.contains("could not be decoded as PNG"), broken_image_error)
	broken_image_hud.free()
	content_db.retail_ui_images[image_key] = saved_image

	var visual: Node3D = asset_factory.make_bundle_object_visual("bfme2.object.gondor-soldier", 0)
	_check("external_glb_instantiates", visual != null and String(visual.get_meta("mesh_path", "")) == expected_model and bool(visual.get_meta("authored", false)))
	_check("rig_metadata_exposed", visual != null and visual.has_meta("has_skeleton") and visual.has_meta("animation_clips"))
	if visual != null:
		visual.free()
	for cached in asset_factory._mesh_cache.values():
		if cached is Node and is_instance_valid(cached):
			(cached as Node).free()
	asset_factory._mesh_cache.clear()

	_run_map_catalog_rejection_tests(content_db, pack_root)
	_test_external_selection_with_supplement(mod_loader)
	_test_external_selection_supplemental_packs(mod_loader)
	_test_external_invalid_selection_fails_closed(mod_loader)

	_restore_settings()
	if _startup_had_environment:
		OS.set_environment("OPENBFME_CONTENT", _startup_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")
	content_db.reload()
	game_audio.music_player.stop()
	game_audio.music_player.stream = null
	game_audio.sfx_player.stop()
	game_audio.sfx_player.stream = null
	game_audio._streams.clear()
	_remove_link(_escape_link)
	_remove_tree(_cache_root)
	_remove_tree(_outside_root)
	_remove_tree(_external_content_root)
	await process_frame
	_finish()


func _emit_parity_profile_report(report_path: String) -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var content_db = root.get_node_or_null("ContentDB")
	if mod_loader == null or content_db == null:
		printerr("PARITY_PROFILE_REPORT FAIL autoloads unavailable (ModLoader/ContentDB)")
		quit(2)
		return
	var selection_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	print("PARITY_PROFILE_REPORT stage=reload content=%s strict=%s" % [
		selection_root, OS.get_environment("OPENBFME_STRICT_PARITY_PROFILE")
	])
	content_db.reload()
	var mounted: Array = []
	for value in content_db.pack_roots:
		mounted.append(String(value))
	var runtime: Dictionary = mod_loader.runtime_pack_report(mounted)
	print("PARITY_PROFILE_REPORT stage=mounted packs=%d source=%s" % [
		mounted.size(), String(runtime.get("activeContentSource", ""))
	])
	var selection_path := String(runtime.get("activeSelectionPath", ""))
	if (selection_path == "" or not selection_path.ends_with("selection.json")) and selection_root != "":
		selection_path = selection_root.replace("\\", "/").path_join("selection.json")
	var selection_block := _parity_selection_block(selection_path)
	var durable_block := _parity_durable_block(
		OS.get_environment(PARITY_DURABLE_SELECTION_ENV).strip_edges(), selection_path
	)
	var census := _parity_map_census(content_db, runtime)
	print("PARITY_PROFILE_REPORT stage=census total=%d mp=%d wor=%d" % [
		int(census["total"]), int(census["mp"]), int(census["wor"])
	])
	var report := {
		"schema": PARITY_REPORT_SCHEMA,
		"schemaVersion": 1,
		"generatedUtc": Time.get_datetime_string_from_system(true, true),
		"godotVersion": String(Engine.get_version_info().get("string", "")),
		"selection": selection_block,
		"runtimeMounted": runtime,
		"durableSelection": durable_block,
		"mapCensus": census,
	}
	var handle := FileAccess.open(report_path, FileAccess.WRITE)
	if handle == null:
		printerr("PARITY_PROFILE_REPORT FAIL cannot write report to %s (error %d)" % [
			report_path, FileAccess.get_open_error()
		])
		quit(3)
		return
	handle.store_string(JSON.stringify(report, "  ") + "\n")
	handle.close()
	print("PARITY_PROFILE_REPORT_WRITTEN path=%s packs=%d entries=%d maps=%d strictErrors=%d" % [
		report_path,
		mounted.size(),
		(selection_block["orderedEntries"] as Array).size(),
		int(census["total"]),
		(runtime["strictParityErrors"] as Array).size(),
	])
	quit(0)


func _parity_selection_block(selection_path: String) -> Dictionary:
	var block := {
		"path": selection_path,
		"readable": false,
		"sha256": "",
		"orderedEntries": [],
		"strictParityProfileDeclared": false,
	}
	if selection_path == "" or not FileAccess.file_exists(selection_path):
		return block
	block["sha256"] = FileAccess.get_sha256(selection_path)
	var file := FileAccess.open(selection_path, FileAccess.READ)
	if file == null:
		return block
	var text := file.get_as_text()
	file.close()
	var json := JSON.new()
	if json.parse(text) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return block
	var document: Dictionary = json.data
	block["readable"] = true
	block["strictParityProfileDeclared"] = bool(document.get("strictParityProfile", false))
	var entries: Array = []
	var active := String(document.get("activePack", ""))
	if active != "":
		entries.append(active)
	var supplements: Variant = document.get("supplementalPacks", [])
	if typeof(supplements) == TYPE_ARRAY:
		for value in supplements as Array:
			entries.append(String(value))
	block["orderedEntries"] = entries
	return block


func _parity_durable_block(durable_path: String, workspace_path: String) -> Dictionary:
	var block := {
		"path": durable_path,
		"present": false,
		"sha256": "",
		"workspaceSha256": "",
		"byteIdenticalToWorkspace": false,
		"compared": false,
	}
	if durable_path == "":
		# Declared absent rather than silently assumed identical: the validator
		# reports this as missing coverage, never as a pass (RULE P7).
		return block
	if workspace_path != "" and FileAccess.file_exists(workspace_path):
		block["workspaceSha256"] = FileAccess.get_sha256(workspace_path)
	if not FileAccess.file_exists(durable_path):
		block["compared"] = true
		return block
	block["present"] = true
	block["sha256"] = FileAccess.get_sha256(durable_path)
	# The durable document's own entries, in its own order. Byte identity and
	# identity-set equality are DIFFERENT claims: a report that only carries the
	# boolean lets one flipped flag hide a whole divergent pack set, so the judge
	# gets the entries and re-derives the answer itself.
	var durable_document := _parity_selection_block(durable_path)
	block["readable"] = bool(durable_document["readable"])
	block["orderedEntries"] = durable_document["orderedEntries"]
	if String(block["workspaceSha256"]) == "":
		return block
	block["compared"] = true
	block["byteIdenticalToWorkspace"] = (
		FileAccess.get_file_as_bytes(durable_path) == FileAccess.get_file_as_bytes(workspace_path)
	)
	return block


func _parity_map_census(content_db: Node, runtime: Dictionary) -> Dictionary:
	## The lobby-shaped map universe: retail's 22 `map mp` skirmish rows plus the
	## 50 `map wor` living-world battle rows the strategic layer resolves
	## (RULE S5). Counted from the catalog the runtime actually indexed, per
	## category, so an extra BFME2 map pack shows up as drift instead of hiding
	## inside a total.
	##
	## Every row is also attributed to the MOUNTED PACK its map document came
	## from, using the document's own `_pack_root`. That is what lets the judge
	## require one content-addressed bundle to own all 72 rows without matching
	## on a pack name: a pack called "…-maps-private" that owns zero rows while
	## seven faction packs supply the census is invisible in the aggregate and
	## obvious here. A row whose pack root matches no mounted pack is counted as
	## unattributed, never silently dropped (RULE P7).
	var rows: Array = content_db.list_catalog_maps(content_db.PLAYABLE_MAP_CATEGORIES)
	var mp := 0
	var wor := 0
	var uncategorized := 0
	var by_root: Dictionary = {}
	var identities: Array = runtime.get("orderedPacks", []) as Array
	var root_to_identity: Dictionary = {}
	for value in identities:
		var identity: Dictionary = value
		root_to_identity[_parity_comparison_path(String(identity["packRoot"]))] = identity
	var unattributed := 0
	for value in rows:
		var row: Dictionary = value
		var category := String(row.get("category", ""))
		if category == "skirmish":
			mp += 1
		elif category == "wotr-battle":
			wor += 1
		else:
			uncategorized += 1
		var document: Dictionary = content_db.get_bundle_map(String(row.get("id", "")))
		var pack_root := _parity_comparison_path(String(document.get("_pack_root", "")))
		if pack_root == "" or not root_to_identity.has(pack_root):
			unattributed += 1
			continue
		if not by_root.has(pack_root):
			by_root[pack_root] = {"total": 0, "mp": 0, "wor": 0}
		var bucket: Dictionary = by_root[pack_root]
		bucket["total"] = int(bucket["total"]) + 1
		if category == "skirmish":
			bucket["mp"] = int(bucket["mp"]) + 1
		elif category == "wotr-battle":
			bucket["wor"] = int(bucket["wor"]) + 1
	# EVERY mounted pack gets a row, including the ones that own nothing: a pack
	# with zero rows is the finding, so it may not be omitted.
	var by_pack: Array = []
	for value in identities:
		var identity: Dictionary = value
		var key := _parity_comparison_path(String(identity["packRoot"]))
		var bucket: Dictionary = by_root.get(key, {"total": 0, "mp": 0, "wor": 0})
		by_pack.append({
			"packId": String(identity["packId"]),
			"bundleDigest": String(identity["bundleDigest"]),
			"bundleDirectory": String(identity["bundleDirectory"]),
			"contentAddressed": bool(identity["contentAddressed"]),
			"packRoot": String(identity["packRoot"]),
			"total": int(bucket["total"]),
			"mp": int(bucket["mp"]),
			"wor": int(bucket["wor"]),
		})
	return {
		"total": rows.size(),
		"mp": mp,
		"wor": wor,
		"uncategorized": uncategorized,
		"unattributed": unattributed,
		"byPack": by_pack,
		"source": "ContentDB.list_catalog_maps(PLAYABLE_MAP_CATEGORIES) attributed by map document _pack_root",
	}


func _parity_comparison_path(path: String) -> String:
	if path == "":
		return ""
	return path.replace("\\", "/").trim_suffix("/").to_lower()


func _build_pack(pack_root: String) -> void:
	for relative in ["data", "maps/fords-of-isen-ii", "maps/rivendell", "assets/models/units", "assets/audio/music", "assets/ui"]:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(pack_root.path_join(relative)))
	_write_json(pack_root.path_join("pack.json"), {
		"schema": "openbfme.content-pack",
		"schemaVersion": 0,
		"id": "bfme2-men-vslice-test",
		"version": "1.06-test",
		"priority": 900,
		"dataPolicy": {"externalPathsAllowed": false},
		"files": {
			"objects": "data/objects.json",
			"animationCapabilities": "data/animation_capabilities.json",
			"entryMap": "maps/fords-of-isen-ii/map.json",
			"mapCatalog": "data/maps.json",
			"uiManifest": "data/ui_manifest.json",
			"strings": "data/strings.json",
			"audioEvents": "data/audio_events.json",
		},
	})
	_write_json(pack_root.path_join("data/objects.json"), {
		"schema": "openbfme.objects",
		"schemaVersion": 0,
		"objects": [{
			"id": "bfme2.object.gondor-soldier",
			"kind": "member",
			"displayName": "External Test Soldier",
			"animationCapabilityId": "bfme2.animation.gondor-soldier",
			"presentation": {
				"model": "assets/models/units/gondor-fighter.glb",
				"icon": "assets/ui/upgondor-soldier.png",
			},
		}],
	})
	_write_json(pack_root.path_join("data/animation_capabilities.json"), {
		"schema": "openbfme.animation-capabilities",
		"schemaVersion": 0,
		"capabilities": [{
			"id": "bfme2.animation.gondor-soldier",
			"presentationMode": "skeletal-glb",
			"states": {"idle": {"required": true, "clip": "idle"}},
		}],
	})
	_write_json(pack_root.path_join("data/ui_manifest.json"), {
		"schema": "openbfme.ui-manifest",
		"schemaVersion": 0,
		"images": [{
			"id": "BGBarracks_Soldiers",
			"path": "assets/ui/upgondor-soldier.png",
			"width": 1024,
			"height": 1024,
		}],
	})
	_write_json(pack_root.path_join("data/strings.json"), {
		"schema": "openbfme.localized-strings",
		"schemaVersion": 0,
		"locale": "en",
		"strings": {
			"CONTROLBAR:ConstructGondorFighterHorde": "Synthetic Gondor Soldiers",
			"CONTROLBAR:ToolTipBuildGondorFighterHorde": "Synthetic Gondor Soldier tooltip",
		},
	})
	_write_json(pack_root.path_join("data/audio_events.json"), {
		"schema": "openbfme.audio-events",
		"schemaVersion": 1,
		"rootIds": ["GondorSoldierVoiceSelect"],
		"events": {
			"GondorSoldierVoiceSelect": {"sounds": [{"id": "synthetic_select"}]},
		},
		"multisounds": {},
		"samples": {"synthetic_select": "assets/audio/music/explore.wav"},
	})
	_write_json(pack_root.path_join("data/maps.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [
			{
				"id": "bfme2.map.fords-of-isen-ii",
				"map": "maps/fords-of-isen-ii/map.json",
				"playerCount": 2,
				"routingGraphStatus": "empty-no-authored-navmesh",
			},
			{
				"id": "bfme2.map.rivendell",
				"map": "maps/rivendell/map.json",
				"playerCount": 3,
				"routingGraphStatus": "empty-no-authored-navmesh",
			},
		],
	})
	_write_json(pack_root.path_join("maps/fords-of-isen-ii/map.json"), {
		"schema": "openbfme.map",
		"schemaVersion": 0,
		"id": "bfme2.map.fords-of-isen-ii",
		"displayName": "External Test Fords",
	})
	_write_json(pack_root.path_join("maps/rivendell/map.json"), {
		"schema": "openbfme.map",
		"schemaVersion": 0,
		"id": "bfme2.map.rivendell",
		"displayName": "External Test Rivendell",
	})
	var source_model := ProjectSettings.globalize_path("res://data/base/assets/models/units/soldier.glb")
	var target_model := ProjectSettings.globalize_path(pack_root.path_join("assets/models/units/gondor-fighter.glb"))
	DirAccess.copy_absolute(source_model, target_model)
	var source_audio := ProjectSettings.globalize_path("res://data/base/assets/audio/music/explore.wav")
	var target_audio := ProjectSettings.globalize_path(pack_root.path_join("assets/audio/music/explore.wav"))
	DirAccess.copy_absolute(source_audio, target_audio)
	var source_icon := ProjectSettings.globalize_path("res://data/base/assets/icons/soldier.png")
	var target_icon := ProjectSettings.globalize_path(pack_root.path_join("assets/ui/upgondor-soldier.png"))
	DirAccess.copy_absolute(source_icon, target_icon)
	var broken_icon := FileAccess.open(pack_root.path_join("assets/ui/broken.png"), FileAccess.WRITE)
	if broken_icon != null:
		broken_icon.store_string("not a png\n")
		broken_icon.close()


func _run_map_catalog_rejection_tests(content_db: Node, pack_root: String) -> void:
	var initial_count: int = content_db.bundle_maps.size()
	var fixture_root := pack_root.path_join("maps/catalog-fixtures")
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root))
	_write_json(fixture_root.path_join("atomic-valid.json"), {
		"schema": "openbfme.map",
		"schemaVersion": 0,
		"id": "bfme2.map.atomic-valid",
	})
	_write_json(pack_root.path_join("data/catalog-unsafe.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [
			{"id": "bfme2.map.atomic-valid", "map": "maps/catalog-fixtures/atomic-valid.json"},
			{"id": "bfme2.map.unsafe", "map": "../selection.json"},
		],
	})
	var loaded: bool = content_db._load_map_catalog(pack_root, "data/catalog-unsafe.json")
	_check("map_catalog_unsafe_row_rejected_atomically", not loaded and not content_db.bundle_maps.has("bfme2.map.atomic-valid") and not content_db.bundle_maps.has("bfme2.map.unsafe") and content_db.bundle_maps.size() == initial_count)

	_write_json(pack_root.path_join("data/catalog-missing.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [{"id": "bfme2.map.missing", "map": "maps/catalog-fixtures/missing.json"}],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-missing.json")
	_check("map_catalog_missing_map_rejected", not loaded and not content_db.bundle_maps.has("bfme2.map.missing") and content_db.bundle_maps.size() == initial_count)

	_write_json(pack_root.path_join("data/catalog-bad-schema.json"), {
		"schema": "openbfme.wrong-catalog",
		"schemaVersion": 0,
		"maps": [],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-bad-schema.json")
	_check("map_catalog_schema_rejected", not loaded and content_db.bundle_maps.size() == initial_count)

	_write_json(pack_root.path_join("data/catalog-bad-version.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 1,
		"maps": [],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-bad-version.json")
	_check("map_catalog_version_rejected", not loaded and content_db.bundle_maps.size() == initial_count)

	_write_json(fixture_root.path_join("bad-schema.json"), {
		"schema": "openbfme.wrong-map",
		"schemaVersion": 0,
		"id": "bfme2.map.bad-schema",
	})
	_write_json(pack_root.path_join("data/catalog-map-bad-schema.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [{"id": "bfme2.map.bad-schema", "map": "maps/catalog-fixtures/bad-schema.json"}],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-map-bad-schema.json")
	_check("map_catalog_referenced_map_schema_rejected", not loaded and not content_db.bundle_maps.has("bfme2.map.bad-schema") and content_db.bundle_maps.size() == initial_count)

	_write_json(fixture_root.path_join("bad-version.json"), {
		"schema": "openbfme.map",
		"schemaVersion": 1,
		"id": "bfme2.map.bad-version",
	})
	_write_json(pack_root.path_join("data/catalog-map-bad-version.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [{"id": "bfme2.map.bad-version", "map": "maps/catalog-fixtures/bad-version.json"}],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-map-bad-version.json")
	_check("map_catalog_referenced_map_version_rejected", not loaded and not content_db.bundle_maps.has("bfme2.map.bad-version") and content_db.bundle_maps.size() == initial_count)

	_write_json(fixture_root.path_join("id-mismatch.json"), {
		"schema": "openbfme.map",
		"schemaVersion": 0,
		"id": "bfme2.map.different-id",
	})
	_write_json(pack_root.path_join("data/catalog-id-mismatch.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": [{"id": "bfme2.map.expected-id", "map": "maps/catalog-fixtures/id-mismatch.json"}],
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-id-mismatch.json")
	_check("map_catalog_id_mismatch_rejected", not loaded and not content_db.bundle_maps.has("bfme2.map.expected-id") and not content_db.bundle_maps.has("bfme2.map.different-id") and content_db.bundle_maps.size() == initial_count)

	var oversized_rows: Array = []
	for index in range(257):
		oversized_rows.append({"id": "bfme2.map.bound-%d" % index, "map": "maps/catalog-fixtures/atomic-valid.json"})
	_write_json(pack_root.path_join("data/catalog-oversized.json"), {
		"schema": "openbfme.map-catalog",
		"schemaVersion": 0,
		"maps": oversized_rows,
	})
	loaded = content_db._load_map_catalog(pack_root, "data/catalog-oversized.json")
	_check("map_catalog_row_bound_enforced", not loaded and content_db.bundle_maps.size() == initial_count)


func _test_external_selection_with_supplement(mod_loader: Node) -> void:
	_external_content_root = ProjectSettings.globalize_path("user://external-content-root-%d" % Time.get_ticks_usec())
	var selected_root := _external_content_root.path_join("selected-pack/sha256-test")
	var supplement_root := _external_content_root.path_join("five-map-supplement")
	DirAccess.make_dir_recursive_absolute(selected_root)
	DirAccess.make_dir_recursive_absolute(supplement_root)
	_write_minimal_pack(selected_root, "external-selected-test", 901)
	_write_minimal_pack(supplement_root, "external-supplement-test", 902)
	_write_json(_external_content_root.path_join("selection.json"), {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "selected-pack/sha256-test",
	})

	var had_environment := OS.has_environment("OPENBFME_CONTENT")
	var old_environment := OS.get_environment("OPENBFME_CONTENT")
	OS.set_environment("OPENBFME_CONTENT", _external_content_root)
	var roots: Array[String] = mod_loader.list_pack_roots()
	_check("external_selection_nested_pack_discovered", roots.has(selected_root))
	# Current discovery contract: a selection document mounts ONLY its named
	# active pack (plus any explicitly listed supplementalPacks) — sibling packs
	# sitting next to it are never auto-mounted, so stale leaves can never leak
	# into the selected closure.
	var sibling_auto_mounted := roots.has(supplement_root)
	OS.set_environment("OPENBFME_CONTENT", selected_root)
	var direct_roots: Array[String] = mod_loader.list_pack_roots()
	_check(
		"external_unlisted_sibling_never_auto_mounted",
		not sibling_auto_mounted and direct_roots.has(selected_root) and not direct_roots.has(supplement_root)
	)

	_write_minimal_pack(selected_root, "external-selected-test", 901, true)
	OS.set_environment("OPENBFME_CONTENT", _external_content_root)
	var completion_roots: Array[String] = mod_loader.list_pack_roots()
	_check(
		"external_completion_selection_suppresses_sibling_packs",
		completion_roots.has(selected_root) and not completion_roots.has(supplement_root)
	)
	if had_environment:
		OS.set_environment("OPENBFME_CONTENT", old_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")


func _test_external_selection_supplemental_packs(mod_loader: Node) -> void:
	_external_content_root = ProjectSettings.globalize_path("user://external-content-root-%d" % Time.get_ticks_usec())
	var selected_root := _external_content_root.path_join("selected-pack/sha256-test")
	var supplement_root := _external_content_root.path_join("faction-pack/sha256-test")
	var sibling_root := _external_content_root.path_join("stale-leaves")
	DirAccess.make_dir_recursive_absolute(selected_root)
	DirAccess.make_dir_recursive_absolute(supplement_root)
	DirAccess.make_dir_recursive_absolute(sibling_root)
	_write_minimal_pack(selected_root, "external-selected-test", 901, true)
	_write_minimal_pack(supplement_root, "external-faction-test", 900)
	_write_minimal_pack(sibling_root, "external-stale-test", 902)
	_write_json(_external_content_root.path_join("selection.json"), {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "selected-pack/sha256-test",
		"supplementalPacks": ["faction-pack/sha256-test", "../outside", "missing-pack/sha256-test"],
	})

	var had_environment := OS.has_environment("OPENBFME_CONTENT")
	var old_environment := OS.get_environment("OPENBFME_CONTENT")
	OS.set_environment("OPENBFME_CONTENT", _external_content_root)
	var roots: Array[String] = mod_loader.list_pack_roots()
	_check(
		"external_invalid_supplement_set_mounts_nothing",
		roots.is_empty()
	)
	_check(
		"external_supplement_refuses_unsafe_and_missing_entries",
		not roots.has(_external_content_root.path_join("../outside")) and not roots.has(_external_content_root.path_join("missing-pack/sha256-test"))
	)
	_check(
		"external_invalid_supplement_set_suppresses_unlisted_siblings",
		not roots.has(sibling_root)
	)
	if had_environment:
		OS.set_environment("OPENBFME_CONTENT", old_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")


func _test_external_invalid_selection_fails_closed(mod_loader: Node) -> void:
	var explicit_root := ProjectSettings.globalize_path("user://external-invalid-selection-%d" % Time.get_ticks_usec())
	var sibling_root := explicit_root.path_join("stale-sibling")
	DirAccess.make_dir_recursive_absolute(sibling_root)
	_write_minimal_pack(sibling_root, "external-stale-sibling", 999)
	var had_environment := OS.has_environment("OPENBFME_CONTENT")
	var old_environment := OS.get_environment("OPENBFME_CONTENT")
	OS.set_environment("OPENBFME_CONTENT", explicit_root)

	var missing_roots: Array[String] = mod_loader.list_pack_roots()
	_check("external_missing_selection_mounts_nothing", missing_roots.is_empty())
	_check("external_missing_selection_refuses_sibling", not missing_roots.has(sibling_root))

	var selection_path := explicit_root.path_join("selection.json")
	var corrupt_file := FileAccess.open(selection_path, FileAccess.WRITE)
	if corrupt_file != null:
		corrupt_file.store_string("{ truncated")
		corrupt_file.close()
	var corrupt_roots: Array[String] = mod_loader.list_pack_roots()
	_check("external_corrupt_selection_mounts_nothing", corrupt_roots.is_empty())

	var selected_root := explicit_root.path_join("selected-pack/sha256-test")
	DirAccess.make_dir_recursive_absolute(selected_root)
	_write_minimal_pack(selected_root, "external-selected-test", 901)
	_write_json(selection_path, {
		"schema": "openbfme.pack-selection",
		"schemaVersion": 0,
		"activePack": "selected-pack/sha256-test",
		"supplementalPacks": ["missing-pack/sha256-test"],
	})
	var incomplete_roots: Array[String] = mod_loader.list_pack_roots()
	_check("external_missing_supplement_mounts_nothing", incomplete_roots.is_empty())
	_check("external_missing_supplement_refuses_active", not incomplete_roots.has(selected_root))

	if had_environment:
		OS.set_environment("OPENBFME_CONTENT", old_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")
	_remove_tree(explicit_root)


func _write_minimal_pack(pack_root: String, id: String, priority: int, profile_build_complete := false) -> void:
	var document := {
		"schema": "openbfme.content-pack",
		"schemaVersion": 0,
		"id": id,
		"priority": priority,
		"dataPolicy": {"externalPathsAllowed": false},
		"files": {},
	}
	if profile_build_complete:
		document["profile_build_complete"] = true
	_write_json(pack_root.path_join("pack.json"), document)


func _write_json(path: String, value: Variant) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.close()


func _restore_settings() -> void:
	var mod_loader = root.get_node("ModLoader")
	ProjectSettings.set_setting(mod_loader.PACK_CACHE_SETTING, _old_cache_setting if _had_cache_setting else null)
	ProjectSettings.set_setting(mod_loader.PACK_SELECTION_SETTING, _old_selection_setting if _had_selection_setting else null)


func _create_directory_link(source: String, link: String) -> bool:
	var source_absolute := ProjectSettings.globalize_path(source)
	var link_absolute := ProjectSettings.globalize_path(link)
	var output: Array = []
	if OS.get_name() == "Windows":
		var escaped_link := link_absolute.replace("'", "''")
		var escaped_source := source_absolute.replace("'", "''")
		var script := "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path '%s' -Target '%s' | Out-Null" % [escaped_link, escaped_source]
		var code := OS.execute(
			"powershell.exe",
			PackedStringArray(["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script]),
			output,
			true
		)
		if code != 0:
			printerr("EXTERNAL_PACK link fixture failed: %s" % str(output))
		return code == 0
	return OS.execute("ln", PackedStringArray(["-s", source_absolute, link_absolute]), output, true) == 0


func _remove_link(path: String) -> void:
	if path == "":
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func _remove_tree(path: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var name := directory.get_next()
	while name != "":
		if name != "." and name != "..":
			var child := path.path_join(name)
			if directory.is_link(name):
				DirAccess.remove_absolute(ProjectSettings.globalize_path(child))
			elif directory.current_is_dir():
				_remove_tree(child)
			else:
				DirAccess.remove_absolute(ProjectSettings.globalize_path(child))
		name = directory.get_next()
	directory.list_dir_end()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("EXTERNAL_PACK PASS %s" % name)
	else:
		failed += 1
		printerr("EXTERNAL_PACK FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("EXTERNAL_PACK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
