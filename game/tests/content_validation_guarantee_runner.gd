extends SceneTree
## Pins WHAT ContentDB.reload PROVES, not how fast it proves it.
##
## WHY THIS EXISTS. ContentDB.reload was made roughly twice as fast by changing
## HOW the filesystem is questioned about a pack: one directory enumeration in
## place of one stat per file, one containment context per pack root in place of
## one per call, and a resolution table warmed on the main thread and then only
## read by the validation fan-out. Every one of those is a cache, and a cache
## that can be wrong is worse than a slow boot.
##
## So this runner does not time anything. It fault-injects a pack that MUST be
## refused - a deleted asset, a corrupted digest, unparseable JSON, a path that
## escapes the pack through a junction - and asserts the refusal still happens
## AND still names the document. It then proves the caches invalidate: change an
## input, and the same pack that was admitted a moment ago is refused.
##
## LIVENESS: 38 checks are expected to run. Raise this when you add checks;
## never lower it. A silent drop in coverage is the failure mode this guards.
const EXPECTED_CHECKS := 38

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var passed: int = 0
var failed: int = 0
var _pack_root: String = ""
var _outside_root: String = ""
var _escape_link: String = ""
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "CONTENT_VALIDATION_GUARANTEE")
	call_deferred("_run")


func _run() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var content_db = root.get_node_or_null("ContentDB")
	_check("autoloads_available", mod_loader != null and content_db != null)
	if mod_loader == null or content_db == null:
		_finish()
		return

	var scratch := "user://content-guarantee-%d" % (Time.get_ticks_usec() & 0xFFFFFF)
	_pack_root = ProjectSettings.globalize_path(scratch.path_join("pack")).replace("\\", "/")
	_outside_root = ProjectSettings.globalize_path(scratch.path_join("outside")).replace("\\", "/")
	_build_pack(content_db)

	_check_directory_index(mod_loader)
	_check_admission(mod_loader, content_db)
	_check_asset_deletion_is_refused(mod_loader, content_db)
	_check_corruptions_are_refused(mod_loader, content_db)
	_check_link_escape_is_refused(mod_loader, content_db)
	_check_late_created_directory_resolves(mod_loader, content_db)
	_check_reload_clears_the_frozen_table(content_db)

	_remove_link(_escape_link)
	_remove_tree(_pack_root)
	_remove_tree(_outside_root)
	# Put the process back on the real content set so nothing downstream inherits
	# the fixture pack roots this runner appended.
	content_db.reload()
	_finish()


# ---------------------------------------------------------------- the fixture


func _build_pack(content_db) -> void:
	for relative in ["data", "assets/models", "assets/ui", "assets/audio"]:
		DirAccess.make_dir_recursive_absolute(_pack_root.path_join(relative))
	DirAccess.make_dir_recursive_absolute(_outside_root)
	_write_bytes(_outside_root.path_join("outside.png"), PackedByteArray([1, 2, 3]))
	_write_bytes(_pack_root.path_join("assets/models/fixture.glb"), PackedByteArray([7, 8, 9]))
	_write_bytes(_pack_root.path_join("assets/ui/fixture.png"), PackedByteArray([1, 2, 3]))
	_write_bytes(_pack_root.path_join("assets/audio/fixture.wav"), _silent_wav())
	_write_json(_pack_root.path_join("data/good.json"), _unit_document("GuaranteeGood"))
	if not content_db.pack_roots.has(_pack_root):
		content_db.pack_roots.append(_pack_root)


func _load(mod_loader, content_db, declared: Dictionary) -> Array:
	## Load one delta into a clean generation and return the skip reasons it
	## produced. Both cache generations are flushed first, which is exactly what
	## reload() does, so this exercises the same invalidation the game does.
	mod_loader.clear_path_caches()
	content_db._asset_exists_cache.clear()
	content_db._frozen_resolutions.clear()
	content_db.skipped_playable_unit_documents.clear()
	content_db._load_playable_unit_runtimes(_pack_root, declared)
	return content_db.get_skipped_playable_unit_documents()


func _refused(mod_loader, content_db, key: String, document: Variant, reason: String, label: String) -> void:
	## Write a deliberately broken document under `key`, load it, and require
	## that it was refused AND that the refusal names the document.
	var relative := "data/%s.json" % key
	if typeof(document) == TYPE_STRING:
		_write_text(_pack_root.path_join(relative), String(document))
	else:
		_write_json(_pack_root.path_join(relative), document)
	var declared := {"playableUnit.%s" % key: relative}
	var skipped: Array = _load(mod_loader, content_db, declared)
	var expected := "playableUnit.%s:%s" % [key, reason]
	_check(label, skipped.has(expected), "expected %s, got %s" % [expected, str(skipped)])
	_check(
		"%s_not_in_registry" % label,
		not content_db.get_playable_unit_runtimes().has(_object_id_of(document)),
		"a refused document must not reach the registry"
	)


func _object_id_of(document: Variant) -> String:
	if typeof(document) != TYPE_DICTIONARY:
		return "GuaranteeUnparseable"
	return String((document as Dictionary).get("objectId", ""))


# ------------------------------------------------------------------- checks


func _check_directory_index(mod_loader) -> void:
	## The enumeration must answer exactly what the filesystem answers: present
	## for a file that is there, absent for one that is not, and UNKNOWN - never
	## "absent" - for a directory it cannot read.
	var assets := _pack_root.path_join("assets/ui")
	_check("index_reports_present_file", mod_loader.directory_contains(assets, "fixture.png") == 1)
	_check("index_reports_absent_file", mod_loader.directory_contains(assets, "not-there.png") == 0)
	_check(
		"index_reports_unknown_directory",
		mod_loader.directory_contains(_pack_root.path_join("assets/nowhere"), "fixture.png") == -1,
		"a directory that cannot be enumerated must be UNKNOWN, not absent"
	)
	# Windows resolves FileAccess.file_exists case-insensitively. The listing has
	# to agree, or a pack declaring "Fixture.PNG" would newly be refused.
	if OS.get_name() == "Windows":
		_check("index_matches_case_insensitively", mod_loader.directory_contains(assets, "FIXTURE.PNG") == 1)
	else:
		_check("index_matches_case_sensitively", mod_loader.directory_contains(assets, "FIXTURE.PNG") == 0)
	_check("index_rejects_empty_arguments", mod_loader.directory_contains("", "x") == -1 and mod_loader.directory_contains(assets, "") == -1)
	# A directory entry is not a file entry: the listing must not report a
	# subdirectory as a resolvable asset.
	_check("index_does_not_report_subdirectories", mod_loader.directory_contains(_pack_root.path_join("assets"), "ui") == 0)


func _check_admission(mod_loader, content_db) -> void:
	var skipped: Array = _load(mod_loader, content_db, {"playableUnit.good": "data/good.json"})
	_check("intact_pack_is_admitted", skipped.is_empty(), str(skipped))
	_check("intact_pack_reaches_registry", content_db.get_playable_unit_runtimes().has("GuaranteeGood"))
	_check(
		"admission_froze_resolutions",
		not content_db._frozen_resolutions.is_empty(),
		"the fast path under test must actually have been exercised"
	)


func _check_asset_deletion_is_refused(mod_loader, content_db) -> void:
	## THE CACHE-INVALIDATION PROOF. The same document that was just admitted is
	## reloaded after one of its declared assets is deleted from disk. Nothing
	## about the document changed - only an input the caches describe.
	var audio := _pack_root.path_join("assets/audio/fixture.wav")
	DirAccess.remove_absolute(audio)
	_check("deleted_asset_is_gone", not FileAccess.file_exists(audio))
	var skipped: Array = _load(mod_loader, content_db, {"playableUnit.good": "data/good.json"})
	_check(
		"deleted_audio_binding_refuses_the_document",
		skipped.has("playableUnit.good:invalid-runtime"),
		"a missing declared asset must still fail closed, got %s" % str(skipped)
	)
	_check(
		"deleted_asset_no_longer_resolves",
		content_db.resolve_asset("assets/audio/fixture.wav", _pack_root) == "",
		"a frozen resolution must not survive the deletion of its file"
	)
	# The listing must have been re-enumerated, not reused.
	_check(
		"directory_index_saw_the_deletion",
		mod_loader.directory_contains(_pack_root.path_join("assets/audio"), "fixture.wav") == 0
	)
	_write_bytes(audio, _silent_wav())
	var restored: Array = _load(mod_loader, content_db, {"playableUnit.good": "data/good.json"})
	_check(
		"restoring_the_asset_readmits_the_document",
		restored.is_empty(),
		"invalidation must work in both directions, got %s" % str(restored)
	)


func _check_corruptions_are_refused(mod_loader, content_db) -> void:
	var missing_image := _unit_document("GuaranteeMissingImage")
	missing_image["registration"]["imageBindings"]["BIFixture"] = "assets/ui/absent.png"
	_refused(mod_loader, content_db, "missingimage", missing_image, "invalid-runtime", "missing_image_binding_refused")

	var missing_model := _unit_document("GuaranteeMissingModel")
	missing_model["registration"]["visual"]["components"][0]["output"] = "assets/models/absent.glb"
	_refused(mod_loader, content_db, "missingmodel", missing_model, "invalid-runtime", "missing_default_model_refused")

	var bad_digest := _unit_document("GuaranteeBadDigest")
	bad_digest["descriptorSha256"] = "not-a-sha256"
	_refused(mod_loader, content_db, "baddigest", bad_digest, "invalid-runtime", "corrupt_digest_refused")

	var bad_schema := _unit_document("GuaranteeBadSchema")
	bad_schema["schema"] = "openbfme.something-else"
	_refused(mod_loader, content_db, "badschema", bad_schema, "invalid-runtime", "wrong_schema_refused")

	var escaping := _unit_document("GuaranteeEscapingBinding")
	escaping["registration"]["imageBindings"]["BIFixture"] = "../outside/outside.png"
	_refused(mod_loader, content_db, "escaping", escaping, "invalid-runtime", "traversing_binding_refused")

	_refused(mod_loader, content_db, "unparseable", "{ this is not json", "invalid-runtime", "unparseable_document_refused")

	# An unsafe DECLARATION path is refused before the document is even read.
	var skipped: Array = _load(mod_loader, content_db, {"playableUnit.unsafe": "../outside/outside.png"})
	_check(
		"unsafe_declaration_path_refused",
		skipped.has("playableUnit.unsafe:unsafe-path"),
		str(skipped)
	)


func _check_link_escape_is_refused(mod_loader, content_db) -> void:
	## Fault injection at the containment boundary. A junction inside the pack
	## pointing outside it is the case the per-call link walk exists to catch,
	## and the case a directory index could quietly launder if the listing were
	## ever trusted over the link probe.
	_escape_link = _pack_root.path_join("assets/escape")
	var created := _create_directory_link(_outside_root, _escape_link)
	_check("link_fixture_created", created, _escape_link)
	if not created:
		return
	mod_loader.clear_path_caches()
	content_db._asset_exists_cache.clear()
	content_db._frozen_resolutions.clear()
	_check(
		"link_escape_path_rejected",
		mod_loader.resolve_pack_path(_pack_root, "assets/escape/outside.png") == "",
		"a path through a junction must not resolve"
	)
	_check(
		"link_escape_asset_rejected",
		content_db.resolve_asset("assets/escape/outside.png", _pack_root) == "",
		"the asset resolver must not admit a file reached through a junction"
	)
	# The file behind the junction genuinely exists, so this is not passing by
	# accident: it is the link, not the absence, that refuses it.
	_check("link_target_really_exists", FileAccess.file_exists(_outside_root.path_join("outside.png")))
	var linked := _unit_document("GuaranteeLinkEscape")
	linked["registration"]["imageBindings"]["BIFixture"] = "assets/escape/outside.png"
	_refused(mod_loader, content_db, "linkescape", linked, "invalid-runtime", "link_escaping_binding_refused")
	_remove_link(_escape_link)
	_escape_link = ""


func _check_late_created_directory_resolves(mod_loader, content_db) -> void:
	## REGRESSION PIN. Caching "this directory could not be opened" made every
	## asset later written into it permanently unresolvable for the process. A
	## directory's CONTENTS are stable for a generation; its EXISTENCE is not.
	var late := _pack_root.path_join("assets/late")
	var late_asset := "assets/late/late.png"
	mod_loader.clear_path_caches()
	content_db._asset_exists_cache.clear()
	_check("late_directory_absent_first", content_db.resolve_asset(late_asset, _pack_root) == "")
	DirAccess.make_dir_recursive_absolute(late)
	_write_bytes(late.path_join("late.png"), PackedByteArray([1, 2, 3]))
	_check(
		"late_created_directory_resolves_without_a_flush",
		content_db.resolve_asset(late_asset, _pack_root) != "",
		"a negative directory probe must never be cached"
	)


func _check_reload_clears_the_frozen_table(content_db) -> void:
	## The frozen resolution table has exactly the lifetime of a load generation.
	## reload() is the only thing that starts a generation, so it must empty it.
	content_db._frozen_resolutions["sentinel\nsentinel"] = "/definitely/not/a/real/path"
	content_db.reload()
	_check(
		"reload_clears_frozen_resolutions",
		not content_db._frozen_resolutions.has("sentinel\nsentinel"),
		"a generation change must discard every frozen resolution"
	)
	_check("reload_clears_asset_exists_cache", not content_db._asset_exists_cache.has("sentinel"))


# -------------------------------------------------------------------- fixture


func _unit_document(object_id: String) -> Dictionary:
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "infantry",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["fixture-model", "fixture-ui", "fixture-audio"],
		"registration": {
			"production": [{
				"producerObjectId": "GuaranteeProducer",
				"commandSetId": "GuaranteeProducerCommandSet",
				"commandId": "Command_Construct%s" % object_id,
				"surface": "command-socket",
				"slot": 1,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
			"gameplay": {},
			"simulation": {
				"displayName": "%s Display" % object_id,
				"buildCost": 700,
				"buildTimeSeconds": 45.0,
				"commandPoints": 35,
				"memberCount": 1,
				"memberHealth": 2500,
				"speed": 50.0,
				"visionRange": 400.0,
				"combat": {
					"attackRange": 30.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 200,
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
			},
			"capabilities": [{"id": "move"}],
			"visual": {
				"components": [{
					"default": true,
					"output": "assets/models/fixture.glb",
					"resourceId": "fixture-model",
					"sourceW3d": "art/w3d/fixture.w3d",
				}],
				"coreAnimations": {
					"idle": [{"identifier": "fixture_idle"}],
					"move": [{"identifier": "fixture_move"}],
				},
			},
			"ui": {
				"portraitImageIds": ["UPFixture"],
				"commands": [{
					"commandId": "Command_Construct%s" % object_id,
					"fields": {
						"ButtonImage": ["BIFixture"],
						"TextLabel": ["CONTROLBAR:%s" % object_id],
						"DescriptLabel": ["CONTROLBAR:ToolTip%s" % object_id],
					},
				}],
			},
			"imageBindings": {
				"BIFixture": "assets/ui/fixture.png",
				"UPFixture": "assets/ui/fixture.png",
			},
			"audioRoutes": {"container": {}, "primaryMember": {}},
			"audioBindings": {"VoiceSelect": "assets/audio/fixture.wav"},
			"audioResolution": {},
			"unsupportedCapabilities": [],
		},
	}


func _silent_wav() -> PackedByteArray:
	return PackedByteArray([
		82, 73, 70, 70, 38, 0, 0, 0, 87, 65, 86, 69,
		102, 109, 116, 32, 16, 0, 0, 0, 1, 0, 1, 0,
		64, 31, 0, 0, 128, 62, 0, 0, 2, 0, 16, 0,
		100, 97, 116, 97, 2, 0, 0, 0, 0, 0,
	])


# --------------------------------------------------------------- scaffolding


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CONTENT_GUARANTEE PASS %s" % label)
	else:
		failed += 1
		printerr("CONTENT_GUARANTEE FAIL %s %s" % [label, detail])


func _finish() -> void:
	var total := passed + failed
	if total != EXPECTED_CHECKS:
		failed += 1
		printerr("CONTENT_GUARANTEE FAIL liveness expected %d checks, ran %d" % [EXPECTED_CHECKS, total])
	print("CONTENT_GUARANTEE_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(1 if failed > 0 else 0)


func _write_json(path: String, value: Variant) -> void:
	_write_text(path, JSON.stringify(value, "  ") + "\n")


func _write_text(path: String, text: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(text)
	file.close()


func _write_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_buffer(bytes)
	file.close()


func _create_directory_link(source: String, link: String) -> bool:
	var output: Array = []
	if OS.get_name() == "Windows":
		var script := "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path '%s' -Target '%s' | Out-Null" % [
			link.replace("'", "''"), source.replace("'", "''")
		]
		var code := OS.execute(
			"powershell.exe",
			PackedStringArray(["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script]),
			output,
			true
		)
		if code != 0:
			printerr("CONTENT_GUARANTEE link fixture failed: %s" % str(output))
		return code == 0
	return OS.execute("ln", PackedStringArray(["-s", source, link]), output, true) == 0


func _remove_link(path: String) -> void:
	if path != "":
		DirAccess.remove_absolute(path)


func _remove_tree(path: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var name := directory.get_next()
	while name != "":
		var child := path.path_join(name)
		if directory.current_is_dir():
			_remove_tree(child)
		else:
			DirAccess.remove_absolute(child)
		name = directory.get_next()
	directory.list_dir_end()
	DirAccess.remove_absolute(path)
