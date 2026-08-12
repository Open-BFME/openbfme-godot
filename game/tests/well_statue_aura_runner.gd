extends SceneTree

## Well/statue aura ring + well water FX (lane kimi-bug-world-fx).
##
## well.ini:228-235 PassiveAreaEffectBehavior EffectRadius = GONDOR_WELL_AOE_RADIUS
##   gamedata.ini:2337 = 200; experiencelevels.ini:10132-10139 SelectionDecal
##   decal_hero_good at GONDOR_WELL_AOE_RADIUS_DECAL 440.
## well.ini:121-126 second Draw module ParticleSysBone NONE WellHealFX.
## statue.ini:168-174 PassiveAreaEffectBehavior EffectRadius = GONDOR_STATUE_AOE_RADIUS 200.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const SCALE := 0.02649232738129
const EXPECTED_CHECKS := 14

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "WELL_STATUE_AURA", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd") as GDScript
	if structure_script == null:
		_check("structure script loads", false)
		_finish()
		return
	_check("structure script loads", true)

	var well = structure_script.new()
	root.add_child(well)
	well.configure(
		{
			"id": 801,
			"team": 0,
			"structure_kind": "well",
			"health": 1500,
			"maximum_health": 1500,
			"construction_progress": 1.0,
		},
		"bfme2.object.gondor-well",
		SCALE
	)
	_check(
		"well compiles PassiveAreaEffect radius 200",
		absf(float(well.aura_radius_source) - 200.0) < 0.001,
		"radius_source=%s" % well.aura_radius_source
	)
	_check(
		"well aura local radius is 200 * map scale",
		absf(float(well.aura_radius_local) - 200.0 * SCALE) < 0.0001,
		"local=%s" % well.aura_radius_local
	)
	well.set_selected(true)
	var ring: Node = well.find_child("AreaEffectRadius", true, false)
	_check(
		"selected well shows the aura ring",
		ring is MeshInstance3D and (ring as MeshInstance3D).visible,
		"ring=%s visible=%s" % [ring, ring.visible if ring is Node3D else "?"]
	)
	_check(
		"intact well presents WellHealFX water cue",
		well.water_fx_present and well.find_child("WellHealFX", true, false) != null,
		"water_fx_present=%s" % well.water_fx_present
	)
	well.queue_free()

	var statue = structure_script.new()
	root.add_child(statue)
	statue.configure(
		{
			"id": 802,
			"team": 0,
			"structure_kind": "statue",
			"health": 1500,
			"maximum_health": 1500,
			"construction_progress": 1.0,
		},
		"bfme2.object.gondor-statue",
		SCALE
	)
	_check(
		"statue compiles PassiveAreaEffect radius 200",
		absf(float(statue.aura_radius_source) - 200.0) < 0.001,
		"radius_source=%s" % statue.aura_radius_source
	)
	statue.set_selected(true)
	var statue_ring: Node = statue.find_child("AreaEffectRadius", true, false)
	_check(
		"selected statue shows the aura ring",
		statue_ring is MeshInstance3D and (statue_ring as MeshInstance3D).visible,
		"ring=%s" % statue_ring
	)
	statue.queue_free()

	# fortress.ini:897-904 MenFortressCitadel House of Healing is upgrade-gated.
	# The healer authors HealFX = FX_SpellHealUnitHealBuff, not WellHealFX.
	var citadel = structure_script.new()
	root.add_child(citadel)
	citadel.configure(
		{
			"id": 3006,
			"team": 0,
			"structure_kind": "fortress",
			"health": 7500,
			"maximum_health": 7500,
			"construction_progress": 1.0,
		},
		"bfme2.object.men-fortress-citadel",
		SCALE
	)
	_check(
		"citadel authors UpgradeRequired House of Healing",
		String(citadel.aura_upgrade_required) == "Upgrade_MenFortressHouseOfHealing",
		"upgrade_required=%s" % citadel.aura_upgrade_required
	)
	_check(
		"citadel without House of Healing has no aura radius",
		absf(float(citadel.aura_radius_source)) < 0.001,
		"radius_source=%s kind=%s" % [citadel.aura_radius_source, citadel.aura_effect_kind]
	)
	_check(
		"citadel without House of Healing has no WellHealFX",
		not citadel.water_fx_present and citadel.find_child("WellHealFX", true, false) == null,
		"water_fx_present=%s" % citadel.water_fx_present
	)
	citadel.set_selected(true)
	var citadel_ring: Node = citadel.find_child("AreaEffectRadius", true, false)
	_check(
		"selected citadel without upgrade shows no ring",
		citadel_ring == null or not (citadel_ring as Node3D).visible,
		"ring=%s visible=%s" % [citadel_ring, citadel_ring.visible if citadel_ring is Node3D else "?"]
	)
	citadel.sync_state(
		{
			"id": 3006,
			"team": 0,
			"structure_kind": "fortress",
			"health": 7500,
			"maximum_health": 7500,
			"construction_progress": 1.0,
			"completed_upgrades": ["Upgrade_MenFortressHouseOfHealing"],
		}
	)
	_check(
		"citadel after House of Healing presents heal radius 200",
		absf(float(citadel.aura_radius_source) - 200.0) < 0.001,
		"radius_source=%s" % citadel.aura_radius_source
	)
	citadel.set_selected(true)
	citadel_ring = citadel.find_child("AreaEffectRadius", true, false)
	_check(
		"selected citadel after upgrade shows the aura ring",
		citadel_ring is MeshInstance3D and (citadel_ring as MeshInstance3D).visible,
		"ring=%s" % citadel_ring
	)
	_check(
		"citadel after upgrade still has no WellHealFX",
		not citadel.water_fx_present and citadel.find_child("WellHealFX", true, false) == null,
		"water_fx_present=%s" % citadel.water_fx_present
	)
	citadel.queue_free()
	_finish()


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("WELL_STATUE_AURA FAIL liveness: ran %d expected %d" % [ran, EXPECTED_CHECKS])
	print("WELL_STATUE_AURA_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(name: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s | %s" % [name, detail])
