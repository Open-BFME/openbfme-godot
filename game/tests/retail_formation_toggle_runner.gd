extends SceneTree

## Q41 — the formation toggle is AUTHORED, not universal.
##
## RETAIL ORACLE (rotwk 2.01 effective view, read 2026-08-18):
##  * data/ini/commandbutton.ini declares 18 live HORDE_TOGGLE_FORMATION
##    buttons, e.g. :664 `Command_TowerGuardPorcupineFormation` with
##    `Options = TOGGLE_IMAGE_ON_FORMATION OK_FOR_MULTI_SELECT`,
##    `ButtonImage = UCCommon_PorcupineFormation UCCommon_PorcupineFormationOff`
##    and `UnitSpecificSound = TowerGuardVoiceWallFormation
##    TowerGuardVoiceLineFormation`; :196
##    `Command_ToggleFormationGondorFighter` uses its own
##    `UCSoldier_ShieldWall UCSoldier_ShieldWallOff` pair. Only 13 command sets
##    reference such a button at all — a Gondor archer horde has none.
##  * the effect is data/ini/attributemodifier.ini:756-764
##    `ModifierList GondorTowerShieldGuardHordePorcupine / Category = FORMATION
##    / Modifier = CRUSHED_DECELERATE 1000% / Duration = 0`. The SPEED, ARMOR,
##    DAMAGE_ADD and CRUSHABLE_LEVEL rows in that block are commented OUT in
##    retail and must stay off.
##  * CRUSHED_DECELERATE is documented at attributemodifier.ini:49 as
##    "Multiplicitive. The percentage that things crushing you slow", and the
##    crusher side is the locomotor's CrushDecelerationPercent, annotated by
##    retail itself at object/cinematic/cinematicobjects.ini:2264 as
##    `CrushDecelerationPercent = 20 ; Lose 80 percent of max velocity when
##    crushing.`

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var HudScript: Script

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

const PORCUPINE_TOGGLE := {
	"command_id": "Command_TowerGuardPorcupineFormation",
	"modifier": {
		"id": "GondorTowerShieldGuardHordePorcupine",
		"category": "FORMATION",
		"modifiers": [
			# 1000% authored -> the importer's percent/100 convention.
			{"kind": "CRUSHED_DECELERATE", "value": 10.0, "application": "multiplicative"},
		],
	},
}


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_FORMATION_TOGGLE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_sim_gate_and_modifier()
	_test_crush_deceleration()
	_test_hud_button_is_authored_only()
	print("RETAIL_FORMATION_TOGGLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim():
	var sim = SimScript.new()
	sim.setup({}, {})
	sim.ai_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _horde(sim, id: int, with_toggle: bool) -> Dictionary:
	var row := {
		"id": id,
		"team": SimScript.PLAYER_TEAM,
		"health": 100,
		"position": Vector2(float(id), 0.0),
		"unit_type": "fixture.horde",
		"category": "infantry",
		"state": "idle",
		"formation_mode": "Line",
		"formation_positions": [Vector3.ZERO],
		"formation_positions_base": [Vector3.ZERO],
		"member_count": 1,
		"member_damage": 1,
		"timed_modifiers": {},
		# Descriptor-backed marker: these rows are held to the authored data,
		# unlike the legacy synthetic fixtures the determinism pins script.
		"module_contracts": {},
	}
	if with_toggle:
		row["formation_toggle"] = PORCUPINE_TOGGLE.duplicate(true)
	sim.entities[id] = row
	return sim.entities[id]


func _test_sim_gate_and_modifier() -> void:
	var sim = _make_sim()
	var porcupine := _horde(sim, 1, true)
	var archer := _horde(sim, 2, false)
	var ids: Array[int] = [1]
	var accepted := int(sim.issue_toggle_formation(ids))
	_check(
		"authored_horde_toggles_into_formation",
		accepted == 1 and String(porcupine.get("formation_mode", "")) == "Block",
		"accepted=%d mode=%s" % [accepted, String(porcupine.get("formation_mode", ""))]
	)
	var table: Dictionary = porcupine.get("timed_modifiers", {}) as Dictionary
	var entry: Dictionary = table.get("formation", {}) as Dictionary
	_check(
		"formation_installs_the_authored_modifier_list",
		String(entry.get("modifier_id", "")) == "GondorTowerShieldGuardHordePorcupine"
			and String(entry.get("category", "")) == "FORMATION"
			and int(entry.get("expires_tick", 0)) == -1,
		str(entry)
	)
	var kinds: Array[String] = []
	for modifier_value in entry.get("modifiers", []) as Array:
		kinds.append(String((modifier_value as Dictionary).get("kind", "")))
	_check(
		"only_the_uncommented_crushed_decelerate_row_is_applied",
		kinds == ["CRUSHED_DECELERATE"],
		str(kinds)
	)
	sim.issue_toggle_formation(ids)
	_check(
		"leaving_formation_removes_the_modifier",
		String(porcupine.get("formation_mode", "")) == "Line"
			and not (porcupine.get("timed_modifiers", {}) as Dictionary).has("formation")
	)
	var archer_ids: Array[int] = [2]
	_check(
		"horde_without_the_authored_button_cannot_toggle",
		int(sim.issue_toggle_formation(archer_ids)) == 0
			and String(archer.get("formation_mode", "")) == "Line"
	)
	_check(
		"horde_without_the_authored_button_cannot_be_set_either",
		int(sim.issue_set_formation(archer_ids, "Block")) == 0
			and String(archer.get("formation_mode", "")) == "Line"
	)


func _test_crush_deceleration() -> void:
	## `CrushDecelerationPercent = 20` keeps 20% of speed (loses 80%); the
	## victim's `CRUSHED_DECELERATE 1000%` multiplies that 80% loss by ten, so
	## the charge is stopped dead. A victim NOT in formation loses only the
	## authored 80%.
	var plain = _make_sim()
	var loose := _horde(plain, 2, true)
	var crusher := _crusher(plain, 1)
	plain._apply_crush_deceleration(crusher, loose)
	_check(
		"crush_into_a_loose_horde_pays_only_the_authored_deceleration",
		is_equal_approx(float(crusher.get("current_speed", -1.0)), 2.0),
		str(crusher.get("current_speed", -1.0))
	)
	var braced_sim = _make_sim()
	var braced := _horde(braced_sim, 2, true)
	var braced_ids: Array[int] = [2]
	braced_sim.issue_set_formation(braced_ids, "Block")
	var braced_crusher := _crusher(braced_sim, 1)
	braced_sim._apply_crush_deceleration(braced_crusher, braced)
	_check(
		"crush_into_a_porcupine_horde_is_stopped_by_the_modifier",
		is_zero_approx(float(braced_crusher.get("current_speed", -1.0))),
		str(braced_crusher.get("current_speed", -1.0))
	)
	# Absent CrushDecelerationPercent stays absent: no invented term.
	var untouched := {"id": 3, "current_speed": 10.0}
	braced_sim._apply_crush_deceleration(untouched, braced)
	_check(
		"no_authored_deceleration_means_no_term_at_all",
		is_equal_approx(float(untouched["current_speed"]), 10.0)
	)


func _crusher(sim, id: int) -> Dictionary:
	sim.entities[id] = {
		"id": id,
		"team": 1,
		"health": 100,
		"position": Vector2.ZERO,
		"category": "cavalry",
		"speed": 10.0,
		"current_speed": 10.0,
		"crush_deceleration_percent": 20.0,
		"formation_positions": [Vector3.ZERO],
	}
	return sim.entities[id]


func _test_hud_button_is_authored_only() -> void:
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	_check("hud_script_available", HudScript != null)
	if HudScript == null:
		return
	var hud = HudScript.new()
	root.add_child(hud)
	hud.build()
	var tower_guard := {
		"presentation": {"ui": {"selectionCommands": [
			{"slot": 1, "commandId": "Command_Stop", "commandKinds": ["STOP"], "fields": {}},
			{
				"slot": 2,
				"commandId": "Command_TowerGuardPorcupineFormation",
				"commandKinds": ["HORDE_TOGGLE_FORMATION"],
				"fields": {
					"ButtonImage": ["UCCommon_PorcupineFormation UCCommon_PorcupineFormationOff"],
					"Options": ["TOGGLE_IMAGE_ON_FORMATION", "OK_FOR_MULTI_SELECT"],
					"TextLabel": ["CONTROLBAR:TogglePorcupineFormation", "CONTROLBAR:ToggleLineFormation"],
				},
			},
		]}}
	}
	var gondor_fighter := {
		"presentation": {"ui": {"selectionCommands": [{
			"slot": 1,
			"commandId": "Command_ToggleFormationGondorFighter",
			"commandKinds": ["HORDE_TOGGLE_FORMATION"],
			"fields": {
				"ButtonImage": ["UCSoldier_ShieldWall UCSoldier_ShieldWallOff"],
				"Options": ["TOGGLE_IMAGE_ON_FORMATION", "OK_FOR_MULTI_SELECT"],
			},
		}]}}
	}
	var archer := {
		"presentation": {"ui": {"selectionCommands": [
			{"slot": 1, "commandId": "Command_Stop", "commandKinds": ["STOP"], "fields": {}},
		]}}
	}
	_check(
		"tower_guard_command_set_carries_a_formation_button",
		String(hud.formation_toggle_command(tower_guard).get("commandId", ""))
			== "Command_TowerGuardPorcupineFormation"
	)
	_check(
		"gondor_fighter_carries_its_own_shieldwall_button",
		String(hud.formation_toggle_command(gondor_fighter).get("commandId", ""))
			== "Command_ToggleFormationGondorFighter"
	)
	_check(
		"archer_command_set_carries_none",
		hud.formation_toggle_command(archer).is_empty()
	)
	var button: Button = hud.unit_action_buttons.get("formation")
	_check("formation_socket_exists", button != null)
	if button == null:
		hud.free()
		return
	hud._selection_formation_command = hud.formation_toggle_command(tower_guard)
	hud.set_active_formation("Line")
	var off_image := String(button.get_meta("formation_image_id", ""))
	hud.set_active_formation("Block")
	var on_image := String(button.get_meta("formation_image_id", ""))
	_check(
		"toggle_image_on_formation_swaps_the_two_authored_ids",
		off_image == "UCCommon_PorcupineFormationOff"
			and on_image == "UCCommon_PorcupineFormation",
		"off=%s on=%s" % [off_image, on_image]
	)
	hud._selection_formation_command = hud.formation_toggle_command(gondor_fighter)
	hud.set_active_formation("Block")
	_check(
		"each_command_set_uses_its_own_icons",
		String(button.get_meta("formation_image_id", "")) == "UCSoldier_ShieldWall",
		String(button.get_meta("formation_image_id", ""))
	)
	hud.free()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_FORMATION_TOGGLE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_FORMATION_TOGGLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
