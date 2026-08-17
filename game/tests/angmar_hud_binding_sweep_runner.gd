extends SceneTree
## Angmar HUD image-binding gate: full-scene boot + whole-pack per-slot sweep.
##
## Section 1 — boot: retail_vertical_slice.tscn with OPENBFME_SLICE_FACTION=
## angmar must reach ready_ok (which includes the fail-closed retail HUD image
## gate that rejected the 64x64 BIWargSentry_Warg / KUSnowTrollIcon bindings in
## the 191/192 portrait slot).
##
## Section 2 — sweep: every playable-unit document mounted from the
## rotwk-angmar-vslice pack is validated against the HUD validator's per-slot
## size expectations (retail_hud.gd):
##   * command-socket ButtonImage: exactly 64x64
##   * hero-roster ButtonImage: decodable PNG, metadata == PNG header
##   * selected portrait (adapter _select_portrait_id): square 191/192
##   * every imageBindings entry: resolvable PNG whose IHDR matches the doc's
##     imageBindingMetadata (the validator's declared-vs-decoded rule)
##
## Run:
##   OPENBFME_CONTENT=<repo>/workspace/content-packs godot --headless --path game \
##     --script res://tests/angmar_hud_binding_sweep_runner.gd

const BOOT_DEADLINE_MS := 300000
const PACK_MARKER := "rotwk-angmar-vslice"

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "ANGMAR_HUD_BINDING_SWEEP_RUNNER")
	OS.set_environment("OPENBFME_SLICE_FACTION", "angmar")
	for env_name in ["OPENBFME_SLICE_MAP", "OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scenes_parse", adapter != null and slice_scene != null, "")
	if adapter == null or slice_scene == null:
		return _finish()

	# --- Section 1: full-scene Angmar boot through the HUD gate ---
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	var boot_ok := bool(slice.ready_ok) and String(slice.failure_reason) == ""
	print("RESULT angmar_boot ready_ok=%s failure_reason=%s" % [str(bool(slice.ready_ok)), String(slice.failure_reason)])
	_check("angmar_slice_boots_ready_ok", boot_ok, String(slice.failure_reason))

	# --- Section 2: whole-pack binding sweep ---
	var db = root.get_node_or_null("/root/ContentDB")
	_check("contentdb_present", db != null, "")
	if db == null:
		return _finish()
	var runtimes: Dictionary = db.get_playable_unit_runtimes()
	var swept := 0
	var hits: Array[String] = []
	for runtime_id_value in runtimes.keys():
		var runtime_id := String(runtime_id_value)
		var document: Dictionary = runtimes[runtime_id]
		if String(document.get("_pack_root", "")).findn(PACK_MARKER) == -1:
			continue
		swept += 1
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var metadata: Dictionary = registration.get("imageBindingMetadata", {}) as Dictionary
		var bindings: Dictionary = registration.get("imageBindings", {}) as Dictionary

		# Every shipped binding must decode and match its declared metadata.
		for image_id_value in bindings.keys():
			var image_id := String(image_id_value)
			var path := String(db.resolve_playable_unit_image_path(runtime_id, image_id))
			var header := _png_header_size(path)
			if header == Vector2i(-1, -1):
				hits.append("%s: binding '%s' does not resolve to a decodable PNG (%s)" % [runtime_id, image_id, path])
				continue
			var declared_row: Dictionary = metadata.get(image_id, {}) as Dictionary
			var declared := Vector2i(int(declared_row.get("width", -1)), int(declared_row.get("height", -1)))
			if declared != header:
				hits.append("%s: binding '%s' declares %s but PNG header is %s" % [runtime_id, image_id, str(declared), str(header)])

		# Per-slot expectations mirrored from retail_hud.gd.
		var specs: Array = adapter.hud_specs(document)
		for spec_value in specs:
			var spec := spec_value as Dictionary
			var surface := String(spec.get("surface", ""))
			var button_id := String(spec.get("image_id", ""))
			var button_row: Dictionary = metadata.get(button_id, {}) as Dictionary
			var button_size := Vector2i(int(button_row.get("width", -1)), int(button_row.get("height", -1)))
			if surface == "command-socket" and button_size != Vector2i(64, 64):
				hits.append("%s: command-socket ButtonImage '%s' must be exactly 64x64, got %s" % [runtime_id, button_id, str(button_size)])
			elif surface == "hero-roster" and (button_size.x <= 0 or button_size.y <= 0):
				hits.append("%s: hero-roster ButtonImage '%s' has no measurable binding" % [runtime_id, button_id])
			var portrait_id := String(spec.get("portrait_image_id", ""))
			var portrait_row: Dictionary = metadata.get(portrait_id, {}) as Dictionary
			var portrait_size := Vector2i(int(portrait_row.get("width", -1)), int(portrait_row.get("height", -1)))
			if portrait_size.x != portrait_size.y or portrait_size.x not in [191, 192]:
				hits.append("%s: portrait slot '%s' must be square 191/192, got %s" % [runtime_id, portrait_id, str(portrait_size)])

		# Honest-socket accounting: a doc with producer commands but an empty
		# portrait list silently vanishes from the HUD — surface it.
		var ui: Dictionary = registration.get("ui", {}) as Dictionary
		var portraits: Array = ui.get("portraitImageIds", []) as Array
		if specs.is_empty() and not (ui.get("commands", []) as Array).is_empty() and portraits.is_empty():
			hits.append("%s: has UI commands but an empty portrait socket (dropped from HUD)" % runtime_id)

	print("RESULT angmar_sweep docs=%d hits=%d" % [swept, hits.size()])
	for hit in hits:
		print("SWEEP-HIT %s" % hit)
	_check("angmar_pack_docs_present", swept > 0, "no angmar docs mounted")
	_check("angmar_sweep_clean", hits.is_empty(), "%d binding hits" % hits.size())

	root.remove_child(slice)
	slice.queue_free()
	await process_frame
	_finish()


func _png_header_size(path: String) -> Vector2i:
	if path == "" or not FileAccess.file_exists(path):
		return Vector2i(-1, -1)
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return Vector2i(-1, -1)
	var bytes := file.get_buffer(33)
	file.close()
	if bytes.size() < 24:
		return Vector2i(-1, -1)
	var signature := PackedByteArray([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
	for index in signature.size():
		if bytes[index] != signature[index]:
			return Vector2i(-1, -1)
	var width := (bytes[16] << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19]
	var height := (bytes[20] << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23]
	return Vector2i(width, height)


func _check(label: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, "" if detail == "" else " — " + detail])


func _finish() -> void:
	print("RESULT angmar_hud_binding_sweep passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
