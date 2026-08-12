extends SceneTree

## Banner carrier T-pose + black-shadow proof (lane kimi-bug-world-fx).
##
## GondorInfantryBanner authors idle GUBanner_IDLB (gondorinfantrybanner.ini:69-77)
## and OkToChangeModelColor. The horde presenter spawned the GLB but never
## played a clip (bind pose = T-pose) and the active men pack declares
## houseColor without shipping data/house-color.json (masks live on the
## supplemental bfme2-men-vslice pack).

const Watchdog := preload("res://tests/runner_watchdog.gd")
const SCALE := 0.02649232738129
const BANNER_ID := "bfme2.object.gondor-infantry-banner"
const EXPECTED_CHECKS := 6

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "BANNER_CARRIER_PRESENTATION", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var asset_factory: GDScript = load("res://src/view/asset_factory.gd") as GDScript
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	if asset_factory == null or battalion_script == null:
		_check("runtime scripts load", false)
		_finish()
		return
	_check("runtime scripts load", true)

	var visual: Node3D = asset_factory.make_bundle_object_visual(BANNER_ID, 0, SCALE)
	if visual == null:
		_check("banner visual instantiates", false, "null")
		_finish()
		return
	root.add_child(visual)
	_check("banner visual instantiates", bool(visual.get_meta("authored", false)))
	var clips: Array = visual.get_meta("animation_clips", []) as Array
	var has_idle := false
	for clip_value in clips:
		if String(clip_value).to_lower().contains("idlb") or String(clip_value).to_lower().contains("idla"):
			has_idle = true
	_check("banner GLB carries idle clips", has_idle, "clips=%s" % [clips])
	_check(
		"house-color masks bind (borrowed pack if the owner pack omitted the file)",
		int(visual.get_meta("house_color_surfaces", 0)) > 0,
		"house=%s status=%s" % [visual.get_meta("house_color_surfaces", 0), visual.get_meta("team_color_status", "")]
	)
	visual.queue_free()

	var battalion = battalion_script.new()
	root.add_child(battalion)
	battalion.configure(7, 0, "bfme2.object.gondor-fighter", {}, 1, SCALE, [Vector3.ZERO])
	battalion.sync_banner_carrier(true, BANNER_ID, Vector2(70.0, 0.0))
	var banner: Node3D = battalion.banner_carrier_visual
	_check("banner visual is authored", banner != null and bool(banner.get_meta("authored", false)))
	var playing := false
	if banner != null:
		var stack: Array[Node] = [banner]
		while not stack.is_empty():
			var node: Node = stack.pop_back()
			if node is AnimationPlayer and (node as AnimationPlayer).is_playing():
				var clip := String((node as AnimationPlayer).current_animation).to_lower()
				if clip.contains("idlb") or clip.contains("idla") or clip.contains("run"):
					playing = true
			for child in node.get_children():
				stack.append(child)
	_check("banner AnimationPlayer plays idle (not T-pose bind pose)", playing)
	battalion.queue_free()
	_finish()


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("BANNER_CARRIER_PRESENTATION FAIL liveness: ran %d expected %d" % [ran, EXPECTED_CHECKS])
	print("BANNER_CARRIER_PRESENTATION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])
