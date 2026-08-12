extends SceneTree

## House-color borrow must stay object-scoped (lane kimi-bug-world-fx r2).
##
## A mounted Men pack shipping data/house-color.json is the right borrow for
## Gondor banners whose own pack omitted the file. It is not a global
## private-retail switch: Mordor/Isengard packs do not declare files.houseColor
## and do not ship matching masks, so they keep the invented team tint until
## their own masks exist.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const SCALE := 0.02649232738129
const EXPECTED_CHECKS := 11

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "HOUSE_COLOR_BORROW_SCOPE", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var asset_factory: GDScript = load("res://src/view/asset_factory.gd") as GDScript
	if asset_factory == null:
		_check("runtime scripts load", false, "asset_factory missing")
		_finish()
		return
	_check("runtime scripts load", true)

	var gondor := _probe(asset_factory, "bfme2.object.gondor-infantry-banner")
	_check("gondor banner visual instantiates", bool(gondor.get("authored", false)), str(gondor))
	_check(
		"gondor banner binds borrowed HouseColor masks",
		int(gondor.get("house", 0)) > 0,
		str(gondor)
	)
	_check(
		"gondor banner does not take invented team tint",
		int(gondor.get("tint", -1)) == 0,
		str(gondor)
	)
	_check(
		"gondor banner status is retail-house-color-masked",
		String(gondor.get("status", "")).contains("retail-house-color-masked"),
		str(gondor)
	)

	var mordor := _probe(asset_factory, "bfme2.object.mordor-fighter")
	_check("mordor fighter visual instantiates", bool(mordor.get("authored", false)), str(mordor))
	_check(
		"mordor fighter is not recolored by Men HouseColor masks",
		int(mordor.get("house", -1)) == 0,
		str(mordor)
	)
	_check(
		"mordor fighter keeps invented team tint",
		int(mordor.get("tint", 0)) > 0,
		str(mordor)
	)
	_check(
		"mordor fighter status is fallback-team-tint",
		String(mordor.get("status", "")) == "fallback-team-tint",
		str(mordor)
	)

	var isengard := _probe(asset_factory, "bfme2.object.isengard-fighter")
	_check(
		"isengard fighter keeps invented team tint",
		bool(isengard.get("authored", false))
			and int(isengard.get("house", -1)) == 0
			and int(isengard.get("tint", 0)) > 0
			and String(isengard.get("status", "")) == "fallback-team-tint",
		str(isengard)
	)

	var mordor_banner := _probe(asset_factory, "bfme2.object.mordor-banner-orc")
	_check(
		"mordor banner orc keeps invented team tint",
		bool(mordor_banner.get("authored", false))
			and int(mordor_banner.get("house", -1)) == 0
			and int(mordor_banner.get("tint", 0)) > 0
			and String(mordor_banner.get("status", "")) == "fallback-team-tint",
		str(mordor_banner)
	)
	_finish()


func _probe(asset_factory: GDScript, object_id: String) -> Dictionary:
	var visual: Node3D = asset_factory.make_bundle_object_visual(object_id, 0, SCALE)
	if visual == null:
		return {"id": object_id, "authored": false, "house": -1, "tint": -1, "status": "null"}
	root.add_child(visual)
	var row := {
		"id": object_id,
		"authored": bool(visual.get_meta("authored", false)),
		"house": int(visual.get_meta("house_color_surfaces", 0)),
		"tint": int(visual.get_meta("team_tinted_surfaces", 0)),
		"status": String(visual.get_meta("team_color_status", "")),
	}
	visual.queue_free()
	return row


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HOUSE_COLOR_BORROW_SCOPE FAIL liveness: ran %d expected %d" % [ran, EXPECTED_CHECKS])
	print("HOUSE_COLOR_BORROW_SCOPE_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])
