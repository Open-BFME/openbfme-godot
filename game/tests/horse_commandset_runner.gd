extends SceneTree
## Horse mesh bind-or-name-the-gap, and palantir CommandSet vs construct dump.

const AdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const EXPECTED_CHECKS := 11
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
	_test_compiled_mounted_component_is_bound()
	_test_mount_toggle_swaps_visibility_instantly()
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
	# Q34: the mounted skin must be ACCOUNTED FOR either way -- bound to a
	# converted GLB once the pack is recooked with MOUNTED components, or named
	# in the gap while it is not. A silent container-payload answer is the bug.
	_check(
		"live_theoden_mounted_skin_is_bound_or_named",
		(gap != "" or String(mesh.get("path", "")).ends_with(".glb"))
			and not gap.contains("container-payload"),
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
	var state := String(battalion.get("mount_presentation_state"))
	# Before the recook the pack has no mounted component and the gap names
	# RUHHs_Theo_SKN; after it, the component binds and the presenter reports
	# either "mounted" or a NAMED instancing failure (this orphan battalion has
	# no members, so it cannot instance).
	_check(
		"battalion_mount_names_theoden_skn",
		(gap.contains("RUHHs_Theo_SKN") or state.contains("RUHHs_Theo") or state.contains(".glb"))
			and not gap.contains("container-payload"),
		"gap=%s state=%s" % [gap, state]
	)
	battalion.free()


func _compiled_mounted_document() -> Dictionary:
	## The shape the importer actually ships: converted ModelConditionState
	## models land in `registration.visual.components` (one row per authored
	## state, `output` = the cooked GLB). Retail anchor: eomer.ini:64-69
	## `ModelConditionState = MOUNTED` / `Model = RUEomrHrs_SKN`.
	return {
		"objectId": "RohanEomer",
		"registration": {
			"visual": {
				"authoredVisualLeaves": [
					{
						"conditions": [],
						"identifier": "ShadowI",
						"kind": "shadow",
						"output": "assets/visual/units/rohaneomer/shadow.png",
					},
				],
				"components": [
					{
						"authoredOccurrences": [
							{"conditions": [], "identifier": "RUEomer_SKN"},
						],
						"conditions": [],
						"default": true,
						"output": "assets/models/units/rohaneomer/00.glb",
						"ownerObjectId": "RohanEomer",
						"sourceW3d": "art/w3d/ru/rueomer_skn.w3d",
					},
					{
						"authoredOccurrences": [
							{"conditions": ["MOUNTED"], "identifier": "RUEomrHrs_SKN"},
						],
						"conditions": ["MOUNTED"],
						"default": false,
						"output": "assets/models/units/rohaneomer/01.glb",
						"ownerObjectId": "RohanEomer",
						"sourceW3d": "art/w3d/ru/rueomrhrs_skn.w3d",
					},
				],
			},
		},
	}


func _test_compiled_mounted_component_is_bound() -> void:
	# Q34: a pack that DID cook the mounted skin still reported
	# `mounted-model-missing` because the adapter only read
	# `authoredVisualLeaves`, which never carries models.
	var mesh: Dictionary = AdapterScript.authored_mounted_mesh(_compiled_mounted_document())
	_check(
		"compiled_mounted_component_is_bound",
		String(mesh.get("gap", "")) == ""
			and String(mesh.get("path", "")) == "assets/models/units/rohaneomer/01.glb"
			and String(mesh.get("id", "")) == "RUEomrHrs_SKN",
		str(mesh)
	)
	var foot_only := _compiled_mounted_document()
	var components: Array = ((foot_only["registration"] as Dictionary)["visual"] as Dictionary)["components"]
	components.remove_at(1)
	var absent: Dictionary = AdapterScript.authored_mounted_mesh(foot_only)
	_check(
		"unmounted_unit_still_names_the_gap",
		String(absent.get("gap", "")).begins_with("mounted-model-missing"),
		str(absent)
	)


func _test_mount_toggle_swaps_visibility_instantly() -> void:
	# Retail authors no crossfade on the MOUNTED state, so the swap must be
	# visible in the SAME frame the toggle is applied.
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	if battalion_script == null:
		_check("mount_toggle_swaps_visibility_instantly", false, "RetailBattalion script failed to load")
		_check("mount_toggle_restores_foot_form_instantly", false, "RetailBattalion script failed to load")
		return
	var battalion: Node = battalion_script.new()
	root.add_child(battalion)
	var foot_root := Node3D.new()
	var foot_mesh := Node3D.new()
	foot_mesh.name = "FootForm"
	foot_root.add_child(foot_mesh)
	battalion.add_child(foot_root)
	var mount_form := Node3D.new()
	mount_form.name = "MountedForm"
	mount_form.visible = false
	foot_root.add_child(mount_form)
	var member_visuals: Dictionary = battalion.get("member_visuals")
	member_visuals[0] = foot_root
	var mounted_visuals: Dictionary = battalion.get("mounted_member_visuals")
	mounted_visuals[0] = mount_form
	var foot_children: Dictionary = battalion.get("_foot_form_children")
	var captured: Array[Node3D] = []
	captured.append(foot_mesh)
	foot_children[0] = captured

	battalion.call("apply_mount_visibility", true)
	_check(
		"mount_toggle_swaps_visibility_instantly",
		mount_form.visible and not foot_mesh.visible
			and String(battalion.get("mount_presentation_state")) == "mounted",
		"mounted=%s foot=%s state=%s" % [
			mount_form.visible, foot_mesh.visible, battalion.get("mount_presentation_state")
		]
	)
	battalion.call("apply_mount_visibility", false)
	_check(
		"mount_toggle_restores_foot_form_instantly",
		foot_mesh.visible and not mount_form.visible
			and String(battalion.get("mount_presentation_state")) == "foot",
		"mounted=%s foot=%s state=%s" % [
			mount_form.visible, foot_mesh.visible, battalion.get("mount_presentation_state")
		]
	)
	battalion.queue_free()


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("HORSE_COMMANDSET FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("HORSE_COMMANDSET_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
