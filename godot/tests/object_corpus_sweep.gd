## Builds the object layer of every retail map through MapObjectBuilder and the W3D instancer (all 181 .map files), headless, and
## prints every distinct error and the totals. A developer sweep, not part of the smoke test (about a minute).
##
##   godot --headless --path godot --script res://tests/object_corpus_sweep.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (exit 77 without them). Exit 1 when any map fails to build or reports an error that is not in
## the baseline file (object_sweep_baseline.txt: asset names the instancer cannot resolve, a lane W3D-2 / retail data item).
extends SceneTree

const BASELINE := "res://tests/object_sweep_baseline.txt"


func _initialize() -> void:
	quit(_run())


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", mount.errors)
		return 1
	var survey_path := ProjectSettings.globalize_path("res://").path_join("../engine/tests/data/map-survey.json")
	var survey: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(survey_path))
	var builder: RefCounted = ClassDB.instantiate("MapObjectBuilder")
	var setup: Dictionary = builder.setup(fs)
	if not setup.ok:
		print("FAIL: object world: ", setup.errors)
		return 1
	var errors := {}
	var failed := 0
	var baseline := {}
	for line in FileAccess.get_file_as_string(BASELINE).split("\n"):
		if not line.is_empty() and not line.begins_with("#"):
			baseline[line] = true
	var totals := {"objects": 0, "drawables": 0, "models": 0, "static": 0, "animated": 0, "tinted": 0}
	var t0 := Time.get_ticks_msec()
	var built := 0
	var names: Array = survey.maps.keys()
	names.sort()
	for key in names:
		var path: String = key
		var dir := path.get_base_dir().get_file()
		if not path.begins_with("maps/"):
			continue # the 51 libraries and 2 bases are not under maps\\<name>\\ (the builder takes a map directory)
		var root: Node3D = builder.build_objects(fs, dir, {})
		var rep: Dictionary = builder.get_report()
		if root == null:
			failed += 1
			print("FAIL ", dir, ": ", rep.errors)
			continue
		built += 1
		totals.objects += rep.objects.total
		totals.drawables += rep.objects.drawables
		totals.models += rep.objects.drawn_models
		totals.static += rep.instancing.static_instances
		totals.animated += rep.instancing.animated_instances
		totals.tinted += rep.instancing.house_colored_instances
		for e in rep.errors:
			var k := String(e)
			if not errors.has(k):
				errors[k] = []
			errors[k].append(dir)
		root.free()
	print("SWEEP %d maps under maps\\ in %.1f s: %d objects, %d drawables, %d models (%d static + %d animated instances, %d tinted)" % [built, (Time.get_ticks_msec() - t0) / 1000.0, totals.objects, totals.drawables, totals.models, totals.static, totals.animated, totals.tinted])
	var seen := {}
	var unexplained := 0
	for k in errors:
		var msg := String(k)
		var key := ""
		# three kinds of asset the instancer cannot resolve, kept apart because they mean different things:
		#  * "absent prototype X": a sub object / aggregate of an HLOD names a render object no W3D file registers (retail's Create_Render_Obj
		#    returns NULL and the part is skipped);
		#  * "missing texture X": no file at the path retail's name builder gives (art\compiledtextures\<xx>\X.dds / .tga);
		#  * "terrain-folder texture X": the name exists only as art\terrain\X, a folder only the terrain loader searches (RW 0x709F77);
		#    retail's texture lookup (RW 0x477D1C) does not search it, so retail does not resolve it either.
		var rx := RegEx.create_from_string("texture not found: (\\S+)")
		var m := rx.search(msg)
		if m:
			key = ("terrain-folder texture " if msg.contains("exists only as art\\terrain\\") else "missing texture ") + m.get_string(1)
		else:
			rx = RegEx.create_from_string("sub object (\\S+) unresolved")
			m = rx.search(msg)
			if m:
				key = "absent prototype " + m.get_string(1)
		if key.is_empty() or not baseline.has(key):
			unexplained += 1
			print("  NEW ERROR x%d maps (%s): %s" % [errors[k].size(), String(errors[k][0]), msg.substr(0, 220)])
		else:
			seen[key] = true
	var vanished := 0
	for key in baseline:
		if not seen.has(key):
			vanished += 1
			print("  BASELINE ENTRY NOT SEEN: ", key)
	var by_kind := {"absent prototype": 0, "missing texture": 0, "terrain-folder texture": 0}
	for key in seen:
		for kind in by_kind:
			if String(key).begins_with(kind + " "):
				by_kind[kind] += 1
	print("SWEEP asset errors by kind: ", by_kind)
	var ok: bool = failed == 0 and unexplained == 0 and vanished == 0 and by_kind["absent prototype"] == 10 and by_kind["missing texture"] == 13 and by_kind["terrain-folder texture"] == 2
	print("SWEEP %s (%d maps failed to build, %d new errors, %d baseline entries not seen, %d baseline entries seen)" % ["PASS" if ok else "FAIL", failed, unexplained, vanished, seen.size()])
	return 0 if ok else 1
