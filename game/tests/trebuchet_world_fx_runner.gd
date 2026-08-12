extends SceneTree

## Fire-stones visual + ground snap for GondorTrebuchet (lane kimi-bug-world-fx).
##
## Retail:
##   trebuchet.ini:525-531 WeaponSetUpgrade + SubObjectsUpgrade ShowSubObjects=FirePlane
##   weapon.ini:3256-3258 flaming projectile GondorTrebuchetRockProjectileFlaming
## The converter ships FIREPLANE in 00.glb visible, so the AABB floor snap
## treated a 40-source-unit billboard as the wheels.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const SCALE := 0.02649232738129
const TREBUCHET_ID := "bfme2.object.gondor-trebuchet"
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "TREBUCHET_WORLD_FX", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var asset_factory: GDScript = load("res://src/view/asset_factory.gd") as GDScript
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	if asset_factory == null or battalion_script == null:
		_check("runtime scripts load", false, "asset_factory or battalion missing")
		_finish()
		return
	_check("runtime scripts load", true)

	var visual: Node3D = asset_factory.make_bundle_object_visual(TREBUCHET_ID, 0, SCALE)
	if visual == null:
		_check("trebuchet visual instantiates", false, "null visual")
		_finish()
		return
	root.add_child(visual)
	_check("trebuchet visual instantiates", bool(visual.get_meta("authored", false)))
	_check(
		"FIREPLANE starts hidden (ShowSubObjects default)",
		_count_visible_named(visual, "FirePlane") == 0,
		"visible=%d" % _count_visible_named(visual, "FirePlane")
	)
	var aabb: AABB = asset_factory.model_aabb(visual)
	_check(
		"AABB floor sits on y=0 after FirePlane is hidden",
		is_finite(aabb.position.y) and absf(aabb.position.y) < 0.02,
		"aabb.position.y=%s size.y=%s" % [aabb.position.y, aabb.size.y]
	)
	visual.queue_free()

	var battalion = battalion_script.new()
	root.add_child(battalion)
	battalion.configure(4902, 0, TREBUCHET_ID, {}, 1, SCALE, [Vector3.ZERO])
	_check(
		"battalion hides FirePlane before the upgrade",
		battalion.fire_plane_visible_count() == 0 and not battalion.fire_upgrade_active,
		"visible=%d active=%s" % [battalion.fire_plane_visible_count(), battalion.fire_upgrade_active]
	)
	var base_projectile := String(battalion.projectile_object_id)
	battalion.sync_upgrade_visuals({"Upgrade_GondorFireStones": 1})
	_check(
		"Fire Stones reveals FirePlane",
		battalion.fire_upgrade_active and battalion.fire_plane_visible_count() > 0,
		"active=%s visible=%d" % [battalion.fire_upgrade_active, battalion.fire_plane_visible_count()]
	)
	_check(
		"Fire Stones binds the flaming projectile id",
		String(battalion.projectile_object_id).to_lower().contains("flaming"),
		"projectile=%s base=%s" % [battalion.projectile_object_id, base_projectile]
	)
	battalion.sync_upgrade_visuals({})
	_check(
		"removing the upgrade hides FirePlane again",
		not battalion.fire_upgrade_active and battalion.fire_plane_visible_count() == 0,
		"active=%s visible=%d" % [battalion.fire_upgrade_active, battalion.fire_plane_visible_count()]
	)
	battalion.queue_free()

	var slice_script: GDScript = load("res://src/retail_slice/retail_vertical_slice.gd") as GDScript
	if slice_script == null:
		_check("siege presentation height is terrain (no 0.35 pad)", false, "slice script missing")
		_check("infantry keep the 0.35 hover pad", false, "slice script missing")
	else:
		var slice = slice_script.new()
		var siege_y := float(slice._presentation_height_for_entity({"category": "siege"}, Vector2.ZERO))
		var infantry_y := float(slice._presentation_height_for_entity({"category": "infantry"}, Vector2.ZERO))
		_check(
			"siege presentation height is terrain (no 0.35 pad)",
			is_equal_approx(siege_y, 0.0),
			"siege_y=%s" % siege_y
		)
		_check(
			"infantry keep the 0.35 hover pad",
			is_equal_approx(infantry_y, 0.35),
			"infantry_y=%s" % infantry_y
		)
		slice.free()
	_finish()


func _count_visible_named(root: Node, token: String) -> int:
	var count := 0
	var want := token.to_lower().replace(" ", "")
	if root is Node3D:
		var folded := root.name.to_lower().replace(" ", "")
		if (folded == want or folded.begins_with(want)) and (root as Node3D).visible:
			count += 1
	for child in root.get_children():
		count += _count_visible_named(child, token)
	return count


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("TREBUCHET_WORLD_FX FAIL liveness: ran %d checks, expected %d" % [ran, EXPECTED_CHECKS])
	print("TREBUCHET_WORLD_FX_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])
