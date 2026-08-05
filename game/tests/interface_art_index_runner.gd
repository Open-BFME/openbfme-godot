extends SceneTree
## Proves the Godot side CONSUMES the importer's interface-art index.
##
## The importer lane (`importer/openbfme_importer/interface_art_lane.py`) crops
## every referenced retail MappedImage out of its retail atlas and writes
##
##     assets/ui/interface-art/<atlas-digest>/<slug>-<id-digest>.png
##     data/interface-art/index.json   {"images": {<MappedImage id>: <path>}}
##
## ContentDB loads that index into `retail_ui_images`, which is what
## `resolve_retail_ui_image_path` - and therefore the whole HUD icon path -
## already reads. This runner pins that wiring end to end on a synthetic pack:
## an id in the index resolves to a real file on disk and decodes to a texture,
## an id the index does not carry stays empty (nothing is invented), and a named
## retail-source gap is reported as such instead of looking like a converter bug.
##
## It also reports how many interface-art rows the CURRENTLY MOUNTED packs
## contribute, so a run against packs cooked before the lane existed is visibly
## zero rather than quietly indistinguishable from a working one (rulebook P8).
##
## LIVENESS: 9 checks are expected to run. Raise this when you add checks;
## never lower it (rulebook T3).
const EXPECTED_CHECKS := 9

var passed: int = 0
var failed: int = 0
var _pack_root: String = ""


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	var mod_loader = root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return

	_report_mounted_packs(content_db)
	_build_pack()
	_check_synthetic_index(content_db)
	_finish()


func _report_mounted_packs(content_db) -> void:
	var rows: int = 0
	for row_value in content_db.retail_ui_images.values():
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("_origin", "")) == "interface-art":
			rows += 1
	print("[INTERFACE_ART] mounted packs contribute %d interface-art rows (%d retail UI images total)" % [
		rows, content_db.retail_ui_images.size()
	])
	for pack_root_value in content_db.pack_roots:
		print("[INTERFACE_ART] pack: %s" % String(pack_root_value))


func _build_pack() -> void:
	var scratch := "user://interface-art-%d" % (Time.get_ticks_usec() & 0xFFFFFF)
	_pack_root = ProjectSettings.globalize_path(scratch.path_join("pack")).replace("\\", "/")
	var image_dir := _pack_root.path_join("assets/ui/interface-art/aaaaaaaaaaaa")
	DirAccess.make_dir_recursive_absolute(image_dir)
	DirAccess.make_dir_recursive_absolute(_pack_root.path_join("data/interface-art"))

	var image := Image.create(16, 16, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.25, 0.5, 0.75, 1.0))
	image.save_png(image_dir.path_join("bgbarracks-cad77488.png"))

	var index := {
		"schema": "openbfme.interface-art-index",
		"schemaVersion": 1,
		"scope": "men",
		"images": {
			"BGBarracks": "assets/ui/interface-art/aaaaaaaaaaaa/bgbarracks-cad77488.png",
		},
		"gaps": [
			{"id": "UPGondor_Banner", "reason": "no-mapped-image"},
		],
	}
	var file := FileAccess.open(_pack_root.path_join("data/interface-art/index.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(index, "  "))
	file.close()

	var pack_meta := {"id": "interface-art-test", "schema": "openbfme.content-pack", "schemaVersion": 0, "files": {}}
	var meta_file := FileAccess.open(_pack_root.path_join("pack.json"), FileAccess.WRITE)
	meta_file.store_string(JSON.stringify(pack_meta, "  "))
	meta_file.close()


func _check_synthetic_index(content_db) -> void:
	if not content_db.pack_roots.has(_pack_root):
		content_db.pack_roots.append(_pack_root)
	content_db._asset_exists_cache.clear()
	content_db._frozen_resolutions.clear()

	content_db._load_interface_art_index(_pack_root, {"files": {}})
	# A later-mounted pack wins by id, exactly like every other pack document,
	# so this row now points at the synthetic index rather than at whatever the
	# already-mounted retail packs published for the same id.
	var row: Dictionary = content_db.get_retail_ui_image("BGBarracks")
	_check("index_row_wins_by_load_order", String(row.get("_source", "")).begins_with(_pack_root))

	_check("row_present_case_insensitively", not row.is_empty())
	_check("row_marked_as_interface_art", String(row.get("_origin", "")) == "interface-art")

	var resolved: String = String(content_db.resolve_retail_ui_image_path("bgbarracks"))
	_check("path_resolves_to_a_png", resolved.get_extension().to_lower() == "png")
	_check("path_exists_on_disk", resolved != "" and FileAccess.file_exists(resolved))

	var loaded := Image.new()
	var decoded := loaded.load(resolved) == OK if resolved != "" else false
	_check("png_decodes_to_a_texture", decoded and ImageTexture.create_from_image(loaded) != null)

	# Nothing invented: an id the pack does not carry must stay empty.
	_check("unknown_id_stays_empty", String(content_db.resolve_retail_ui_image_path("NoSuchImageId")) == "")

	# A named retail-source gap is distinguishable from a converter failure.
	_check("named_gap_is_reported", String(content_db.interface_art_gap_reason("upgondor_banner")) == "no-mapped-image")


func _check(label: String, ok: bool) -> void:
	if ok:
		passed += 1
		print("[INTERFACE_ART] PASS %s" % label)
	else:
		failed += 1
		printerr("[INTERFACE_ART] FAIL %s" % label)


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("[INTERFACE_ART] FAIL check_universe ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("[INTERFACE_ART] passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
