extends SceneTree
## Consumer gate for resolve-once HUD image validation. Host and supplemental
## images are trusted only when ContentDB resolves them through a mounted pack;
## images with no pack backing stay fail-closed rejected.
##
## The HUD script is loaded at runtime (not preloaded): a --script main loop
## compiles its top-level preloads before the autoload globals it references
## (ContentDB, ModLoader) are registered.

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_HUD_MULTIPACK_RUNNER")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var hud_script = load("res://src/retail_slice/retail_hud.gd")
	var content_db := root.get_node_or_null("ContentDB")
	_check("hud_script_and_content_db_load", hud_script != null and content_db != null)
	if hud_script == null or content_db == null:
		_finish()
		return
	var hud = hud_script.new()

	var men_root := String((content_db.call("get_bundle_object", "bfme2.object.gondor-fighter") as Dictionary).get("_pack_root", ""))
	var men_doc := {}
	var elves_doc := {}
	for object_id_value in (content_db.call("get_playable_unit_runtimes") as Dictionary).keys():
		var document := (content_db.call("get_playable_unit_runtimes") as Dictionary)[object_id_value] as Dictionary
		var pack_root := String(document.get("_pack_root", ""))
		var bindings := ((document.get("registration", {}) as Dictionary).get("imageBindings", {}) as Dictionary)
		if pack_root == "" or bindings.is_empty():
			continue
		if _same_path(pack_root, men_root) and men_doc.is_empty():
			men_doc = {"object_id": String(object_id_value), "image_id": String(bindings.keys()[0]), "root": pack_root}
		elif not _same_path(pack_root, men_root) and elves_doc.is_empty():
			elves_doc = {"object_id": String(object_id_value), "image_id": String(bindings.keys()[0]), "root": pack_root}
	_check("men_host_pack_resolves", men_root != "" and not men_doc.is_empty(), men_root)
	_check("supplemental_faction_pack_present", not elves_doc.is_empty() and not _same_path(String(elves_doc.get("root", "")), men_root), str(elves_doc.get("root", "missing")))
	if men_doc.is_empty() or elves_doc.is_empty() or men_root == "":
		_finish()
		return

	# Host-pack runtime-backed image keeps validating against the host root.
	var men_validation: Dictionary = hud._validate_retail_image(
		content_db, men_root, String(men_doc["image_id"]), Vector2i.ZERO, String(men_doc["object_id"])
	)
	_check("host_pack_image_still_accepted", String(men_validation.get("error", "")) == "", String(men_validation.get("error", "")))

	# The supplemental hit is accepted because ContentDB resolved it through a
	# mounted pack; no mutable HUD-local allowlist participates.
	var multi_validation: Dictionary = hud._validate_retail_image(
		content_db, men_root, String(elves_doc["image_id"]), Vector2i.ZERO, String(elves_doc["object_id"])
	)
	_check(
		"supplemental_image_accepted_by_mounted_resolver",
		String(multi_validation.get("error", "")) == "",
		String(multi_validation.get("error", ""))
	)

	# Sequential clears cannot alter resolver provenance.
	hud._clear_retail_command_bindings(false)
	var repeated_validation: Dictionary = hud._validate_retail_image(
		content_db, men_root, String(elves_doc["image_id"]), Vector2i.ZERO, String(elves_doc["object_id"])
	)
	_check(
		"sequential_clear_preserves_mounted_resolution",
		String(repeated_validation.get("error", "")) == "",
		String(repeated_validation.get("error", ""))
	)

	# Images with no pack backing stay fail-closed rejected.
	var unpackaged: Dictionary = hud._validate_retail_image(content_db, men_root, "DefinitelyNotARealImageId", Vector2i.ZERO)
	_check("unpackaged_image_rejected", String(unpackaged.get("error", "")).contains("is missing"), String(unpackaged.get("error", "")))
	var no_selection: Dictionary = hud._validate_retail_image(content_db, "", "AptStrategicUnitUpgradeArmor", Vector2i.ZERO)
	_check("mounted_shared_image_needs_no_second_selection_gate", String(no_selection.get("error", "")) == "", String(no_selection.get("error", "")))
	var missing_runtime: Dictionary = hud._validate_retail_image(content_db, men_root, "AnyImage", Vector2i.ZERO, "NoSuchRuntimeObject")
	_check("missing_runtime_rejected", String(missing_runtime.get("error", "")).contains("is missing"), String(missing_runtime.get("error", "")))

	hud.free()
	_finish()


func _same_path(left: String, right: String) -> bool:
	return left.replace("\\", "/").simplify_path().trim_suffix("/").to_lower() == right.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_HUD_MULTIPACK PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_HUD_MULTIPACK FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_HUD_MULTIPACK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
