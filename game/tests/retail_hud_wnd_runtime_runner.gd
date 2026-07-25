extends SceneTree

var passed := 0
var failed := 0
var runtime_script
var commits: Array = []
var forwards: Array = []


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_HUD_WND_RUNTIME_RUNNER")
	call_deferred("_run")


func _run() -> void:
	runtime_script = load("res://src/retail_slice/retail_hud_wnd_runtime.gd")
	_check("runtime_compiles", runtime_script != null)
	if runtime_script == null:
		_finish()
		return
	var runtime = runtime_script.new()
	var document := _document()
	_check("exact_legal_fixture_configures", runtime.configure_document(document), runtime.last_error)
	var draw_document := _draw_document()
	_check("exact_draw_fixture_configures", runtime.configure_draw_document(draw_document), runtime.last_error)
	var message_document := _message_document()
	_check("exact_message_fixture_configures", runtime.configure_message_document(message_document), runtime.last_error)
	var companion_document := _companion_document()
	_check("exact_production_companion_configures", runtime.configure_companion_document(companion_document), runtime.last_error)
	_check("companion_enables_only_typed_local_runtime", runtime.companion_configured and runtime.configured and runtime.draw_configured and runtime.message_configured)
	_check("six_callbacks_remain_unimplemented", runtime.unimplemented_callbacks().size() == 6)
	_check("central_and_radar_callbacks_implemented", "ControlBarSystem" not in runtime.unimplemented_callbacks() and "LeftHUDInput" not in runtime.unimplemented_callbacks())
	_check("unsealed_default_draw_remains_blocked", "W3DGameWinDefaultDraw" in runtime.unimplemented_callbacks())
	_check("implemented_draw_inventory_is_exact", runtime.implemented_draw_callbacks().size() == 10 and "W3DNoDraw" in runtime.implemented_draw_callbacks() and "W3DPowerDraw" in runtime.implemented_draw_callbacks())
	_check("remaining_draw_inventory_is_exact", runtime.unimplemented_draw_callbacks() == ["W3DGameWinDefaultDraw"])
	_check("remaining_non_draw_inventory_is_exact", runtime.unimplemented_non_draw_callbacks().size() == 5 and "GameWinDefaultSystem" in runtime.unimplemented_non_draw_callbacks() and "ControlBarSystem" not in runtime.unimplemented_non_draw_callbacks())
	_check("men_v_men_required_inventory_is_closed", runtime.men_v_men_required_callbacks().size() == 5 and runtime.men_v_men_required_unimplemented_callbacks().is_empty())
	_check("outside_slice_inventory_is_explicit", runtime.outside_slice_callbacks() == {"BeaconWindowInput": "event-dormant", "ControlBarObserverSystem": "outside declared player-v-player slice"})
	_check("unresolved_builtins_are_not_claimed", runtime.unresolved_builtin_callbacks() == ["GameWinDefaultInput", "GameWinDefaultSystem", "GameWinDefaultTooltip", "W3DGameWinDefaultDraw"])
	_check("message_dynamic_gates_are_named", (runtime.message_dynamic_gates() as Dictionary).size() == 3 and (runtime.message_dynamic_gates().LeftHUDInput as Array).size() == 3 and (runtime.message_dynamic_gates().ControlBarSystem as Array).size() == 3)

	var parent := _control("input", 0, "ControlBar.wnd:ControlBarParent")
	var input_result: Dictionary = runtime.control_bar_input(parent, 0x4006, 7, 8)
	_check("control_bar_input_exact_unhandled", input_result.ok and input_result.handled == 0 and input_result.effects.is_empty())

	var block := _control("input", 7, "ControlBar.wnd:CenterBackground")
	_check("block_message_15_unhandled", runtime.game_win_block_input(block, 0x15, 0, 0, {}).handled == 0)
	_check("block_message_18_unhandled", runtime.game_win_block_input(block, 0x18, 0, 0, {}).handled == 0)
	_check("other_block_message_consumed_without_effect", runtime.game_win_block_input(block, 0x44, 0, 0, {}).handled == 1 and commits.is_empty())
	var block_result: Dictionary = runtime.game_win_block_input(block, 0x06, 0, 0, {"commit_effects": Callable(self, "_commit")})
	_check("message_6_commits_once", block_result.ok and block_result.handled == 1 and commits.size() == 1)
	_check("message_6_exact_effect_count", block_result.effects.size() == 5 and block_result.effects[0].target == "0x00852354")
	var before_failed_commit := commits.size()
	var failed_commit: Dictionary = runtime.game_win_block_input(block, 0x06, 0, 0, {"commit_effects": Callable(self, "_reject_commit")})
	_check("failed_commit_is_atomic", not failed_commit.ok and failed_commit.effects.is_empty() and commits.size() == before_failed_commit)

	var pass_control := _control("system", 13, "ControlBar.wnd:CommandWindow")
	var services := {"resolve_parent": Callable(self, "_resolve_parent"), "forward_message": Callable(self, "_forward")}
	for message in runtime.FORWARDED_MESSAGES:
		var result: Dictionary = runtime.pass_selected_buttons_to_parent_system(pass_control, int(message), 11, 12, services)
		_check("forward_%x" % int(message), result.ok and result.handled == 7)
	_check("exact_forwarded_count", forwards.size() == runtime.FORWARDED_MESSAGES.size())
	var ignored: Dictionary = runtime.pass_selected_buttons_to_parent_system(pass_control, 0x400A, 1, 2, services)
	_check("non_allowlisted_message_not_forwarded", ignored.ok and ignored.handled == 0 and forwards.size() == runtime.FORWARDED_MESSAGES.size())
	var missing_parent: Dictionary = runtime.pass_selected_buttons_to_parent_system(pass_control, 0x4006, 1, 2, {"resolve_parent": Callable(self, "_resolve_none"), "forward_message": Callable(self, "_forward")})
	_check("missing_parent_unhandled", missing_parent.ok and missing_parent.handled == 0)

	var left_control := _control("input", 80, "ControlBar.wnd:LeftHUD1Input")
	var left_mode_gate: Dictionary = runtime.left_hud_input(left_control, 0x99, 0, 0, {"modeByte10": false, "modeByte11": false, "predicate006aa08e": false})
	_check("left_hud_exact_mode_gate_consumes", left_mode_gate.ok and left_mode_gate.handled == 1 and left_mode_gate.effects.is_empty())
	var left_trivial: Dictionary = runtime.left_hud_input(left_control, 0x0008, 0, 0, {"modeByte10": true, "modeByte11": false, "predicate006aa08e": true})
	_check("left_hud_exact_trivial_message_consumes", left_trivial.handled == 1 and left_trivial.effects.is_empty())
	var left_project: Dictionary = runtime.left_hud_input(left_control, 0x0005, 12, 34, {"modeByte10": true, "modeByte11": false, "predicate006aa08e": true, "selectedObjectPresent": true, "objectField14": 0x18})
	_check("left_hud_projection_order_is_exact", left_project.ok and left_project.handled == 1 and left_project.effects.size() == 5 and left_project.effects[0].target == "0x00713bc6" and left_project.effects[1].target == "0x00713b3c" and left_project.effects[2].target == "0x006d81ec" and left_project.effects[3].target == "0x006d7744" and left_project.effects[4].candidateCommandIds == [0x42F, 0x430])
	var left_camera: Dictionary = runtime.left_hud_input(left_control, 0x0012, 0, 0, {"modeByte10": true, "modeByte11": true, "predicate006aa08e": true})
	_check("left_hud_camera_alias_stays_opaque", left_camera.handled == 1 and left_camera.effects[-1].candidateTargets == ["0x00dfdca0:vslot-0x4c", "selection-services"] and left_camera.dynamicGates.size() == 3)
	var left_reject: Dictionary = runtime.left_hud_input(left_control, 0x99, 0, 0, {"modeByte10": true, "modeByte11": true, "predicate006aa08e": true})
	_check("left_hud_unrecognized_message_rejects", left_reject.ok and left_reject.handled == 0)
	var left_bad_alias: Dictionary = runtime.left_hud_input(left_control, 0x000D, 0, 0, {"modeByte10": true, "modeByte11": true, "predicate006aa08e": true, "selectedObjectPresent": true, "objectField14": 7})
	_check("left_hud_invented_object_alias_fails_closed", not left_bad_alias.ok and left_bad_alias.effects.is_empty())

	var system_control := _control("system", 0, "ControlBar.wnd:ControlBarParent")
	var system_gate: Dictionary = runtime.control_bar_system(system_control, 0x4006, 0, 1, {"gameStateGateActive": true})
	_check("control_bar_system_exact_game_gate_rejects", system_gate.ok and system_gate.handled == 0 and system_gate.effects.is_empty())
	var system_init: Dictionary = runtime.control_bar_system(system_control, 0x0001, 0, 0, {"gameStateGateActive": false})
	_check("control_bar_system_initial_cache_is_exact", system_init.handled == 1 and system_init.effects[0].resolver == "0x00df36a4" and system_init.effects[0].destination == "0x00e02f00")
	var system_dispatch: Dictionary = runtime.control_bar_system(system_control, 0x4007, 0, 77, {"gameStateGateActive": false})
	_check("control_bar_system_selected_dispatch_is_exact", system_dispatch.handled == 1 and system_dispatch.effects[0].target == "0x0071abd4" and system_dispatch.effects[0].data2 == 77)
	var system_400b: Dictionary = runtime.control_bar_system(system_control, 0x400B, 0, 88, {"gameStateGateActive": false})
	_check("control_bar_system_400b_order_is_retained", system_400b.handled == 1 and system_400b.effects[0].destinations.size() == 8 and system_400b.effects[1].message400bOrdering == "cached-control-rejection-before-selected-button-fallback")
	var system_match: Dictionary = runtime.control_bar_system(system_control, 0x4031, 0, 99, {"gameStateGateActive": false, "matchedCachedControl": true})
	var system_miss: Dictionary = runtime.control_bar_system(system_control, 0x4031, 0, 99, {"gameStateGateActive": false, "matchedCachedControl": false})
	_check("control_bar_system_4031_match_return_is_typed", system_match.handled == 1 and system_match.effects.size() == 3 and system_miss.handled == 0 and system_miss.effects.size() == 2)
	var system_missing_match: Dictionary = runtime.control_bar_system(system_control, 0x4031, 0, 0, {"gameStateGateActive": false})
	_check("control_bar_system_missing_dynamic_match_fails_closed", not system_missing_match.ok)

	var no_draw := _control("draw", 1, "ControlBar.wnd:Munkee")
	var draw_result: Dictionary = runtime.w3d_no_draw(no_draw, {"sentinel": true})
	_check("w3d_no_draw_exact_noop", draw_result.ok and draw_result.commands.is_empty() and draw_result.effects.is_empty())
	var malformed_control: Dictionary = runtime.w3d_no_draw(_control("draw", 1, "ControlBar.wnd:Changed"), {})
	_check("malformed_control_fails_closed", not malformed_control.ok)

	var background: Dictionary = runtime.w3d_command_bar_background_draw(_control("draw", 3, "ControlBar.wnd:BackgroundMarker"), {}, {"ownerPresent": true, "markerWindowPresent": true, "rect": [8, 595, 13, 600], "instanceDrawState": "opaque-retail-state"})
	_check("background_exact_order_emitted", background.ok and background.commands.size() == 5 and background.commands[0].asset == "ControlBar.wnd:BackgroundMarker" and background.commands[3].target == "0x0071aeae" and background.commands[4].target == "0x0071fac5" and not background.renderingCommitted)
	var foreground: Dictionary = runtime.w3d_command_bar_foreground_draw(_control("draw", 77, "ControlBar.wnd:ForegroundMarker"), {}, {"ownerPresent": true, "markerWindowPresent": true, "rect": [0, 595, 5, 600], "instanceDrawState": "opaque-retail-state"})
	_check("foreground_uses_distinct_calls", foreground.ok and foreground.commands[3].target == "0x0071ae93" and foreground.commands[4].target == "0x0071fa9a")
	var experience: Dictionary = runtime.w3d_command_bar_gen_exp_draw(_control("draw", 53, "ControlBar.wnd:GeneralsExp"), {}, {"predicatePassed": true, "rect": [769, 503, 782, 590], "instanceDrawState": "opaque-retail-state", "progress": 140.0})
	_check("experience_assets_and_clamp_are_exact", experience.ok and experience.commands[1].asset == "GenExpBarTop1" and experience.commands[2].asset == "GenExpBarBottom1" and experience.commands[3].asset == "GenExpBar1" and float(experience.commands[5].value) == 100.0)
	var grid: Dictionary = runtime.w3d_command_bar_grid_draw(_control("draw", 66, "ControlBar.wnd:WinUnitSelected"), {}, {"predicateNegative": false, "rect": [621, 424, 792, 592], "gridState": "opaque-retail-state", "cells": [[0, 0, 4, 4], [4, 0, 8, 4]]})
	_check("grid_emits_ordered_bounded_cells", grid.ok and grid.commands.size() == 5 and grid.commands[3].cellIndex == 0 and grid.commands[4].cellIndex == 1 and grid.commands[3].target == "0x0044d664")
	var top: Dictionary = runtime.w3d_command_bar_top_draw(_control("draw", 86, "ControlBar.wnd:OnTopDraw"), {}, {"buttonGeneralPresent": true, "predicatePassed": true})
	_check("top_exact_tail_dispatch", top.ok and top.commands[-1].target == "0x006a7dd0" and top.commands[-1].asset == "ControlBar.wnd:ButtonGeneral")
	var button_control := _control("draw", 67, "ControlBar.wnd:CameoWindow")
	var button_delegate: Dictionary = runtime.w3d_gadget_push_button_image_draw(button_control, {}, {"predicateDelegate": true})
	var button_image: Dictionary = runtime.w3d_gadget_push_button_image_draw(button_control, {}, {"predicateDelegate": false, "imagePresent": true, "flag20": true, "rect": [0, 0, 45, 36]})
	var button_fallback: Dictionary = runtime.w3d_gadget_push_button_image_draw(button_control, {}, {"predicateDelegate": false, "imagePresent": true, "flag20": false})
	_check("push_button_three_way_branch_is_exact", button_delegate.commands[-1].target == "0x004a501c" and button_image.commands[-1].target == "0x004a56ed" and button_fallback.commands[-1].target == "0x004a4b7c")
	var left: Dictionary = runtime.w3d_left_hud_draw(_control("draw", 2, "ControlBar.wnd:LeftHUD"), {}, {"radarObjectPresent": false, "rect": [29, 452, 163, 575], "modeByte10": 0, "modeByte11": 1})
	_check("left_hud_preserves_service_boundary", left.ok and left.commands[0].service == "0x00dfedf0" and left.commands[0].vslot == "0x164" and left.commands[-1].vslot == "0x1c" and left.commands[-1].receiver == "opaque-mode-selected-service")
	var power: Dictionary = runtime.w3d_power_draw(_control("draw", 81, "ControlBar.wnd:PowerWindow"), {}, {"rect": [261, 473, 544, 480], "powerState": "opaque-retail-state"})
	_check("power_literal_order_is_exact", power.ok and power.commands[0].asset == "PowerPointY" and power.commands[1].asset == "PowerPointG" and power.commands[2].asset == "PowerBarSlider" and power.commands[-1].vslot == "0xb0")
	var right_delegate: Dictionary = runtime.w3d_right_hud_draw(_control("draw", 55, "ControlBar.wnd:RightHUD"), {}, {"predicateNegative": true})
	var right_no_draw: Dictionary = runtime.w3d_right_hud_draw(_control("draw", 55, "ControlBar.wnd:RightHUD"), {}, {"predicateNegative": false})
	_check("right_hud_conditional_delegate_is_exact", right_delegate.commands[-1].target == "0x0049dc32" and right_no_draw.commands.size() == 1)
	var malformed_grid: Dictionary = runtime.w3d_command_bar_grid_draw(_control("draw", 66, "ControlBar.wnd:WinUnitSelected"), {}, {"predicateNegative": false, "rect": [0, 0, 1, 1], "gridState": "opaque", "cells": [[0, 0, 1, 1], [0, 0, 1, 1], [0, 0, 1, 1], [0, 0, 1, 1], [0, 0, 1, 1]]})
	_check("grid_over_four_cells_fails_closed", not malformed_grid.ok and malformed_grid.commands.is_empty())
	var malformed_power: Dictionary = runtime.w3d_power_draw(_control("draw", 81, "ControlBar.wnd:PowerWindow"), {}, {"rect": [0, 0, NAN, 1], "powerState": "opaque"})
	_check("nonfinite_draw_geometry_fails_closed", not malformed_power.ok and malformed_power.commands.is_empty())

	var malformed := document.duplicate(true)
	((malformed.callbacks as Array)[0] as Dictionary)["name"] = "ChangedCallback"
	var malformed_runtime = runtime_script.new()
	_check("malformed_callback_identity_rejected", not malformed_runtime.configure_document(malformed))
	var malformed_binding := document.duplicate(true)
	((((malformed_binding.callbacks as Array)[1] as Dictionary).controls as Array)[0] as Dictionary)["controlId"] = "ControlBar.wnd:Changed"
	var malformed_binding_runtime = runtime_script.new()
	_check("malformed_control_binding_rejected", not malformed_binding_runtime.configure_document(malformed_binding))
	var malformed_draw := draw_document.duplicate(true)
	((malformed_draw.callbacks as Array)[0] as Dictionary)["sha256"] = "0".repeat(64)
	var malformed_draw_runtime = runtime_script.new()
	_check("malformed_draw_handler_rejected", not malformed_draw_runtime.configure_draw_document(malformed_draw))
	var malformed_message := message_document.duplicate(true)
	((malformed_message.handlers as Array)[0] as Dictionary)["entryVa"] = "0x00000000"
	var malformed_message_runtime = runtime_script.new()
	_check("malformed_message_handler_rejected", not malformed_message_runtime.configure_message_document(malformed_message))
	var malformed_companion := companion_document.duplicate(true)
	(malformed_companion.liveBinding as Dictionary)["renderServicesBound"] = true
	var malformed_companion_runtime = runtime_script.new()
	_check("malformed_companion_live_binding_rejected", not malformed_companion_runtime.configure_companion_document(malformed_companion) and not malformed_companion_runtime.companion_configured)
	_run_private_contract_if_requested()
	_finish()


func _document() -> Dictionary:
	var callbacks := []
	var names: Array = runtime_script.BINDINGS.keys()
	names.sort()
	for name_value in names:
		var name := String(name_value)
		var controls := []
		for key_value in runtime_script.BINDINGS[name]:
			var parts := String(key_value).split("|", true, 3)
			controls.append({"kind": parts[0], "windowIndex": int(parts[1]), "controlId": parts[2], "controlType": parts[3]})
		callbacks.append({"name": name, "controls": controls, "genericDispatchAllowed": false})
	return {"schema": runtime_script.CONTRACT_SCHEMA, "summary": {"callbackIdentityCount": 21, "exactRetailHandlerCount": 17, "runtimeBuiltInHandlerCount": 4, "genericDispatchAllowed": false}, "callbacks": callbacks}


func _companion_document() -> Dictionary:
	return {
		"schema": runtime_script.COMPANION_SCHEMA,
		"schemaVersion": 0,
		"source": {
			"virtualPath": "window/controlbar.wnd", "sha256": runtime_script.SOURCE_SHA256,
			"windowCount": 87, "callbackCount": 21,
			"activationAuthority": "active-companion-not-candidate-dead",
		},
		"oracleAggregates": runtime_script.ORACLE_AGGREGATES.duplicate(),
		"callbackBindings": runtime_script.BINDINGS.duplicate(true),
		"runtimeInventory": {
			"implementedCallbacks": runtime_script.IMPLEMENTED.duplicate(), "implementedCallbackCount": 15,
			"requiredMessageCallbacks": runtime_script.MEN_V_MEN_REQUIRED_CALLBACKS.duplicate(), "requiredMessageCallbackCount": 5,
			"requiredMessageUnimplemented": [], "outsideSlice": runtime_script.OUTSIDE_SLICE_CALLBACKS.duplicate(),
			"unresolvedBuiltins": runtime_script.UNRESOLVED_BUILTIN_CALLBACKS.duplicate(),
		},
		"dynamicGates": {"drawAndService": runtime_script.DRAW_SERVICE_GATES.duplicate(), "messageAliases": runtime_script.MESSAGE_ALIAS_GATES.duplicate()},
		"liveBinding": {"callbackDispatchBound": false, "renderServicesBound": false, "genericDispatchAllowed": false, "fallbackVisualsAllowed": false},
	}


func _draw_document() -> Dictionary:
	var callbacks := []
	var names: Array = runtime_script.DRAW_SPECS.keys()
	names.sort()
	for name_value in names:
		var name := String(name_value)
		var expected := runtime_script.DRAW_SPECS[name] as Dictionary
		var controls := []
		for key_value in runtime_script.BINDINGS[name]:
			var parts := String(key_value).split("|", true, 3)
			controls.append({"windowIndex": int(parts[1]), "controlId": parts[2], "controlType": parts[3]})
		var unresolved := []
		for _index in int(expected.unresolvedCount):
			unresolved.append("oracle-sealed-dynamic-gate")
		var callback := {
			"name": name,
			"entryVa": String(expected.entryVa),
			"endVa": String(expected.endVa),
			"byteLength": int(expected.byteLength),
			"sha256": String(expected.sha256),
			"interface": "draw_%s(control, instance_data, typed_state) -> list[DrawCommand]" % name.trim_prefix("W3D").trim_suffix("Draw").to_lower(),
			"controls": controls,
			"assets": (expected.assets as Array).duplicate(),
			"inputs": ["oracle-sealed-typed-inputs"],
			"order": (expected.order as Array).duplicate(),
			"state": "oracle-sealed-state-boundary",
			"unresolved": unresolved,
			"genericDispatchAllowed": false,
		}
		if name == "W3DNoDraw":
			callback["inputs"] = []
			callback["proof"] = "single byte 0xc3 RET; no reads, writes, calls, draw commands, or return guarantee"
		callbacks.append(callback)
	return {
		"schema": runtime_script.DRAW_CONTRACT_SCHEMA,
		"aggregateSha256": "748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484",
		"summary": {
			"drawCallbackCount": 10, "exactFullBodyCount": 10, "provenNoOpCount": 1,
			"callbacksImplemented": false, "inventedVisualsAllowed": false, "genericDispatchAllowed": false,
		},
		"callbacks": callbacks,
	}


func _message_document() -> Dictionary:
	var handlers := []
	var names: Array = runtime_script.MESSAGE_SPECS.keys()
	names.sort()
	for name_value in names:
		var name := String(name_value)
		var expected := runtime_script.MESSAGE_SPECS[name] as Dictionary
		var controls := []
		for key_value in runtime_script.BINDINGS[name]:
			var parts := String(key_value).split("|", true, 3)
			controls.append({"kind": parts[0], "windowIndex": int(parts[1]), "controlId": parts[2], "controlType": parts[3]})
		var states := []
		for index in int(expected.stateCount):
			states.append({"when": "oracle-state-%d" % index, "effects": [], "return": 0})
		var dependencies := []
		for index in int(expected.dependencyCount):
			dependencies.append("oracle-dependency-%d" % index)
		var unresolved := []
		for index in int(expected.unresolvedCount):
			unresolved.append("oracle-dynamic-gate-%d" % index)
		handlers.append({
			"name": name,
			"entryVa": String(expected.entryVa), "endVa": String(expected.endVa),
			"byteLength": int(expected.byteLength), "sha256": String(expected.sha256),
			"controls": controls, "interface": String(expected.interface),
			"returnContract": String(expected.returnContract), "states": states,
			"dependencies": dependencies, "unresolved": unresolved,
			"genericDispatchAllowed": false,
		})
	return {
		"schema": runtime_script.MESSAGE_CONTRACT_SCHEMA,
		"aggregateSha256": "238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c",
		"summary": {
			"prioritizedHandlerCount": 5, "fullyExactHandlerCount": 3,
			"boundedOpaqueBranchHandlerCount": 2, "implemented": false,
			"genericDispatchAllowed": false,
		},
		"handlers": handlers,
		"retainedNotImplemented": runtime_script.OUTSIDE_SLICE_CALLBACKS.duplicate(),
	}


func _control(kind: String, index: int, control_id: String) -> Dictionary:
	return {"kind": kind, "windowIndex": index, "controlId": control_id, "controlType": "USER"}


func _commit(effects: Array) -> bool:
	commits.append(effects.duplicate(true))
	return true


func _reject_commit(_effects: Array) -> bool:
	return false


func _resolve_parent(_control_value: Dictionary) -> Dictionary:
	return {"id": "parent"}


func _resolve_none(_control_value: Dictionary) -> Variant:
	return null


func _forward(parent_value: Dictionary, message: int, data1: int, data2: int) -> int:
	forwards.append([parent_value, message, data1, data2])
	return 7


func _run_private_contract_if_requested() -> void:
	var path := ""
	var draw_path := ""
	var message_path := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--retail-hud-wnd-contract="):
			path = argument.trim_prefix("--retail-hud-wnd-contract=")
		elif argument.begins_with("--retail-hud-wnd-draw-contract="):
			draw_path = argument.trim_prefix("--retail-hud-wnd-draw-contract=")
		elif argument.begins_with("--retail-hud-wnd-message-contract="):
			message_path = argument.trim_prefix("--retail-hud-wnd-message-contract=")
	if path == "" and draw_path == "" and message_path == "":
		return
	var runtime = runtime_script.new()
	_check("private_callback_fixture_configures", runtime.configure_document(_document()), runtime.last_error)
	if path != "":
		var file := FileAccess.open(path, FileAccess.READ)
		_check("private_contract_readable", file != null)
		if file != null:
			var value: Variant = JSON.parse_string(file.get_as_text())
			_check("private_contract_json", typeof(value) == TYPE_DICTIONARY)
			if typeof(value) == TYPE_DICTIONARY:
				_check("private_exact_contract_configures", runtime.configure_document(value as Dictionary), runtime.last_error)
	if draw_path != "":
		var draw_file := FileAccess.open(draw_path, FileAccess.READ)
		_check("private_draw_contract_readable", draw_file != null)
		if draw_file != null:
			var draw_value: Variant = JSON.parse_string(draw_file.get_as_text())
			_check("private_draw_contract_json", typeof(draw_value) == TYPE_DICTIONARY)
			if typeof(draw_value) == TYPE_DICTIONARY:
				_check("private_exact_draw_contract_configures", runtime.configure_draw_document(draw_value as Dictionary), runtime.last_error)
				var private_power: Dictionary = runtime.w3d_power_draw(_control("draw", 81, "ControlBar.wnd:PowerWindow"), {}, {"rect": [261, 473, 544, 480], "powerState": "opaque-retail-state"})
				_check("private_power_draw_literals_emit", private_power.ok and private_power.commands[0].asset == "PowerPointY" and private_power.commands[1].asset == "PowerPointG" and private_power.commands[2].asset == "PowerBarSlider" and not private_power.renderingCommitted)
	if message_path != "":
		var message_file := FileAccess.open(message_path, FileAccess.READ)
		_check("private_message_contract_readable", message_file != null)
		if message_file != null:
			var message_value: Variant = JSON.parse_string(message_file.get_as_text())
			_check("private_message_contract_json", typeof(message_value) == TYPE_DICTIONARY)
			if typeof(message_value) == TYPE_DICTIONARY:
				_check("private_exact_message_contract_configures", runtime.configure_message_document(message_value as Dictionary), runtime.last_error)
				var private_left: Dictionary = runtime.left_hud_input(_control("input", 80, "ControlBar.wnd:LeftHUD1Input"), 0x0005, 12, 34, {"modeByte10": true, "modeByte11": false, "predicate006aa08e": true, "selectedObjectPresent": true, "objectField14": 0x18})
				_check("private_left_hud_typed_branch_emits", private_left.ok and private_left.handled == 1 and private_left.effects[2].target == "0x006d81ec" and private_left.dynamicGates.size() == 3)
				var private_system: Dictionary = runtime.control_bar_system(_control("system", 0, "ControlBar.wnd:ControlBarParent"), 0x400B, 0, 77, {"gameStateGateActive": false})
				_check("private_control_bar_typed_branch_emits", private_system.ok and private_system.handled == 1 and private_system.effects[0].destinations.size() == 8 and private_system.dynamicGates.size() == 3)


func _check(label: String, condition: bool, detail := "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("WND_RUNTIME_FAIL %s %s" % [label, detail])


func _finish() -> void:
	print("RETAIL_HUD_WND_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
