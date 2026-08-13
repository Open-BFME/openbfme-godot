extends SceneTree
## Horse mesh bind-or-name-the-gap, and palantir CommandSet vs construct dump.

const AdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const EXPECTED_CHECKS := 7
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "HORSE_COMMANDSET")
	call_deferred("_run")


func _run() -> void:
	_test_selection_commands_not_construct_dump()
	_test_mounted_mesh_names_missing_artefact()
	_test_mounted_glb_is_bound()
	_test_live_theoden_names_horse_gap()
	_test_hud_live_fighter_commandset()
	_test_battalion_mount_names_theoden_skn()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HORSE_COMMANDSET PASS %s" % label)
	else:
		failed += 1
		printerr("HORSE_COMMANDSET FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_selection_commands_not_construct_dump() -> void:
	var construct_only := {
		"registration": {
			"ui": {
				"commands": [
					{"commandId": "Command_ConstructGondorFighterHorde"},
				],
			},
		},
	}
	_check(
		"construct_dump_is_not_selection_command_set",
		AdapterScript.selection_command_ids(construct_only).is_empty()
	)
	var authored := {
		"registration": {
			"ui": {
				"commands": [{"commandId": "Command_ConstructGondorFighterHorde"}],
				"selectionCommands": [
					{"commandId": "Command_ToggleStance", "commandSetId": "GondorFighterHordeCommandSet"},
					{"commandId": "Command_AttackMove", "commandSetId": "GondorFighterHordeCommandSet"},
					{"commandId": "Command_Stop", "commandSetId": "GondorFighterHordeCommandSet"},
				],
			},
		},
	}
	var ids: PackedStringArray = AdapterScript.selection_command_ids(authored)
	_check(
		"selection_commands_are_unit_commandset",
		ids.find("Command_Stop") >= 0
			and ids.find("Command_AttackMove") >= 0
			and ids.find("Command_ConstructGondorFighterHorde") < 0,
		",".join(ids)
	)


func _test_mounted_mesh_names_missing_artefact() -> void:
	var gap_doc := {
		"registration": {
			"visual": {
				"authoredVisualLeaves": [
					{
						"conditions": ["MOUNTED"],
						"identifier": "RUHHs_Theo_SKN",
						"kind": "model",
						"output": "sources/units/rohantheoden/mounted.w3d",
					},
				],
			},
		},
	}
	var mesh: Dictionary = AdapterScript.authored_mounted_mesh(gap_doc)
	_check(
		"missing_horse_mesh_is_named",
		String(mesh.get("gap", "")).begins_with("mounted-model-unconverted:RUHHs_Theo_SKN"),
		str(mesh)
	)


func _test_mounted_glb_is_bound() -> void:
	var ok_doc := {
		"registration": {
			"visual": {
				"authoredVisualLeaves": [
					{
						"conditions": ["MOUNTED"],
						"identifier": "RUHHs_Theo_SKN",
						"kind": "model",
						"output": "assets/models/units/rohantheoden/mounted.glb",
					},
				],
			},
		},
	}
	var mesh: Dictionary = AdapterScript.authored_mounted_mesh(ok_doc)
	_check(
		"authored_horse_glb_is_bound",
		String(mesh.get("gap", "")) == "" and String(mesh.get("path", "")).ends_with(".glb"),
		str(mesh)
	)


func _test_live_theoden_names_horse_gap() -> void:
	var db := root.get_node_or_null("ContentDB")
	if db == null or not db.has_method("get_playable_unit_runtime"):
		_check("live_theoden_names_horse_gap", false, "ContentDB missing")
		return
	var doc: Variant = db.call("get_playable_unit_runtime", "RohanTheoden")
	if typeof(doc) != TYPE_DICTIONARY or (doc as Dictionary).is_empty():
		doc = db.call("get_playable_unit_runtime", "rohantheoden")
	if typeof(doc) != TYPE_DICTIONARY or (doc as Dictionary).is_empty():
		_check("live_theoden_names_horse_gap", false, "RohanTheoden document missing")
		return
	var mesh: Dictionary = AdapterScript.authored_mounted_mesh(doc as Dictionary)
	var gap := String(mesh.get("gap", ""))
	_check(
		"live_theoden_names_horse_gap",
		gap != "" and not gap.contains("container-payload"),
		str(mesh)
	)


func _test_hud_live_fighter_commandset() -> void:
	var db := root.get_node_or_null("ContentDB")
	if db == null:
		_check("hud_live_fighter_commandset", false, "ContentDB missing")
		return
	var hud_script: GDScript = load("res://src/retail_slice/retail_hud.gd") as GDScript
	if hud_script == null:
		_check("hud_live_fighter_commandset", false, "RetailHud script failed to load")
		return
	var hud: Node = hud_script.new()
	root.add_child(hud)
	hud.set("_bound_content_db", db)
	var entities := {
		1: {
			"unit_type": "bfme2.object.gondor-fighter-horde",
			"object_id": "bfme2.object.gondor-fighter",
		},
	}
	var selected_ids: Array[int] = []
	selected_ids.append(1)
	hud.call("set_unit_selection_state", selected_ids, entities)
	var ids: PackedStringArray = hud.get("last_selection_command_ids")
	var joined := ",".join(ids)
	_check(
		"hud_live_fighter_commandset",
		ids.find("Command_Stop") >= 0
			and ids.find("Command_AttackMove") >= 0
			and ids.find("Command_ConstructGondorFighterHorde") < 0
			and not joined.contains("ConstructGondorFighterHorde"),
		joined
	)
	hud.queue_free()


func _test_battalion_mount_names_theoden_skn() -> void:
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	if battalion_script == null:
		_check("battalion_mount_names_theoden_skn", false, "RetailBattalion script failed to load")
		return
	var battalion: Node = battalion_script.new()
	battalion.set("object_id", "bfme2.object.rohan-theoden")
	battalion.call("sync_mount_presentation", true)
	var gap := String(battalion.get("mount_visual_gap"))
	_check(
		"battalion_mount_names_theoden_skn",
		gap.contains("RUHHs_Theo_SKN") and not gap.contains("container-payload"),
		gap
	)
	battalion.free()


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("HORSE_COMMANDSET FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("HORSE_COMMANDSET_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
