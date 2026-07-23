extends SceneTree
## Legal-safe gate for the exact-key, fail-closed Palantir APT binder.

var passed := 0
var failed := 0
var runtime_script
var wnd_script
var fixture_root := ""


func _initialize() -> void:
	create_timer(20.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	runtime_script = load("res://src/retail_slice/retail_hud_apt_runtime.gd")
	wnd_script = load("res://src/retail_slice/retail_hud_wnd_runtime.gd")
	_check("runtime_script_compiles", runtime_script != null)
	_check("wnd_runtime_script_compiles", wnd_script != null)
	if runtime_script == null or wnd_script == null:
		_finish()
		return
	fixture_root = ProjectSettings.globalize_path("user://openbfme-hud-apt-runtime-fixture")
	_cleanup_fixture()
	_check("fixture_directory_created", DirAccess.make_dir_recursive_absolute(fixture_root) == OK)
	var document := _document()
	_check("fixture_pack_written", _write_json(fixture_root.path_join("pack.json"), {
		"id": "hud-apt-runtime-fixture",
		"files": {"palantirScene": "data/ui/palantir/scene-contract.json"},
	}))
	_check("fixture_contract_written", _write_json(fixture_root.path_join("data/ui/palantir/scene-contract.json"), document))

	var strict_runtime = runtime_script.new()
	root.add_child(strict_runtime)
	var strict_result := bool(strict_runtime.configure_from_pack(fixture_root))
	_check(
		"unsupported_semantics_fail_closed_by_default",
		not strict_result
		and String(strict_runtime.error).contains("static subset"),
		"result=%s declared=%s error=%s" % [strict_result, strict_runtime.contract_declared, strict_runtime.error]
	)
	_check("strict_failure_never_claims_presentation_or_parity", not bool(strict_runtime.presentation_ready) and not bool(strict_runtime.parity_ready))

	var inspection_runtime = runtime_script.new()
	inspection_runtime.size = Vector2(1024.0, 768.0)
	root.add_child(inspection_runtime)
	var inspection_result := bool(inspection_runtime.configure_from_pack(fixture_root, true))
	_check("exact_palantir_scene_key_binds", inspection_result, "declared=%s error=%s" % [inspection_runtime.contract_declared, inspection_runtime.error])
	_check("declarative_triangle_is_executable", bool(inspection_runtime.contract_ready) and bool(inspection_runtime.presentation_ready) and int(inspection_runtime.draw_count) == 1)
	_check("inspection_opt_in_is_never_parity", bool(inspection_runtime.static_subset_opt_in) and not bool(inspection_runtime.parity_ready) and int(inspection_runtime.blocker_count) == 9)
	var owned_wnd = inspection_runtime.wnd_companion_runtime()
	_check("wnd_companion_is_owned_atomically", bool(inspection_runtime.wnd_companion_ready) and int(inspection_runtime.wnd_typed_callback_count) == 15 and owned_wnd != null and bool(owned_wnd.companion_configured))
	var control_bar_result := owned_wnd.control_bar_input({"windowIndex": 0, "controlId": "ControlBar.wnd:ControlBarParent", "controlType": "USER"}, 0, 0, 0) as Dictionary
	_check("owned_wnd_typed_callback_executes", bool(control_bar_result.get("ok", false)) and int(control_bar_result.get("handled", -1)) == 0)
	_check("exact_external_movie_slots_bind_atomically", bool(inspection_runtime.external_movie_slots_ready) and int(inspection_runtime.external_movie_slot_count) == 4 and inspection_runtime.external_movie_load_order == ["InGameSpellBook", "InGameHelpBox", "InGameHeroSelect", "InGamePlanningMode"])
	_check("external_movie_slots_are_direct_palantir_children", inspection_runtime.get_node_or_null("SpellBookUI") != null and inspection_runtime.get_node_or_null("helpBox") != null and inspection_runtime.get_node_or_null("HeroSelectUI") != null and inspection_runtime.get_node_or_null("planningModeUI") != null)
	var hero_slot := inspection_runtime.external_movie_slot_state("HeroSelectUI") as Dictionary
	_check("hero_select_show_state_is_not_guessed", not bool(hero_slot.get("visible", true)) and int(hero_slot.get("currentFrame", -1)) == 8 and String(hero_slot.get("lifecycleState", "")) == "attached-awaiting-capture" and not bool((inspection_runtime.get_node("HeroSelectUI") as Node2D).visible))
	var help_slot := inspection_runtime.external_movie_slot_state("helpBox") as Dictionary
	_check("help_slot_keeps_authored_placement", int(help_slot.get("depth", -1)) == 176 and (inspection_runtime.get_node("helpBox") as Node2D).position == Vector2(585.0, 607.0))
	_check("native_external_reset_order_is_exact", inspection_runtime.native_external_reset_order == ["HeroSelectUI", "helpBox", "planningModeUI"])
	_check("exact_timeline_inventory_is_bound", int(inspection_runtime.timeline_count) == 1 and int(inspection_runtime.timeline_frame_count) == 2 and int(inspection_runtime.timeline_instance_count) == 1)
	_check("typed_action_inventory_is_bound", int(inspection_runtime.action_script_count) == 2 and int(inspection_runtime.supported_action_script_count) == 1)
	var timeline_state := {"playing": true}
	_check("exact_stop_script_executes", bool(inspection_runtime.execute_action_script("palantir:100", timeline_state)) and not bool(timeline_state.playing))
	_check("vm_lane_executed_supported_stop_script", int(inspection_runtime.vm_executed_program_count) == 1 and int(inspection_runtime.vm_fallback_program_count) == 0)
	_check("blocked_host_script_cannot_execute", not bool(inspection_runtime.execute_action_script("palantir:12", timeline_state)))
	_check("vm_lane_never_touches_blocked_scripts", int(inspection_runtime.vm_executed_program_count) == 1 and int(inspection_runtime.vm_fallback_program_count) == 0)
	var minlod_document := document.duplicate(true)
	(minlod_document.actionScripts as Array).append_array(_minlod_fixture_programs())
	(minlod_document.summary as Dictionary)["actionScriptCount"] = 5
	(minlod_document.summary as Dictionary)["supportedActionScriptCount"] = 4
	(minlod_document.summary as Dictionary)["unsupportedActionScriptCount"] = 1
	var minlod_runtime = runtime_script.new()
	root.add_child(minlod_runtime)
	_check("typed_minlod_fixture_binds", bool(minlod_runtime.configure_document(minlod_document, fixture_root, true)), String(minlod_runtime.error))
	var globe_state := {"_name": "GlobeSwirlRender", "playing": true}
	_check("minlod_input_is_required", not bool(minlod_runtime.execute_action_script("palantir:152912", globe_state)))
	_check("minlod_input_must_be_boolean", not bool(minlod_runtime.execute_action_script("palantir:152912", globe_state, {"MinLOD": 1})))
	_check("minlod_false_branch_is_exact_noop", bool(minlod_runtime.execute_action_script("palantir:152912", globe_state, {"MinLOD": false})) and bool(globe_state.playing))
	_check("minlod_true_globe_branch_stops", bool(minlod_runtime.execute_action_script("palantir:152912", globe_state, {"MinLOD": true})) and not bool(globe_state.playing))
	var other_globe_state := {"_name": "BigGlobeSwirlRender", "playing": true}
	_check("minlod_true_nonmatching_name_is_noop", bool(minlod_runtime.execute_action_script("palantir:152912", other_globe_state, {"MinLOD": true})) and bool(other_globe_state.playing))
	var effect_state := {"targets": {"effect1": {"_visible": true}, "effect4": {"_visible": true}}}
	_check("minlod_false_named_targets_are_unchanged", bool(minlod_runtime.execute_action_script("palantir:333872", effect_state, {"MinLOD": false})) and bool(effect_state.targets.effect1._visible) and bool(effect_state.targets.effect4._visible))
	_check("minlod_true_named_targets_hide", bool(minlod_runtime.execute_action_script("palantir:333872", effect_state, {"MinLOD": true})) and not bool(effect_state.targets.effect1._visible) and not bool(effect_state.targets.effect4._visible))
	var incomplete_effect_state := {"targets": {"effect2": {"_visible": true}}}
	_check("minlod_named_target_shape_fails_closed", not bool(minlod_runtime.execute_action_script("palantir:334840", incomplete_effect_state, {"MinLOD": true})) and bool(incomplete_effect_state.targets.effect2._visible))
	var clip_timeline_state := {"playing": true}
	_check("exact_initialize_clip_action_executes", bool(inspection_runtime.execute_clip_action("palantir:300:fixture/1", 1, clip_timeline_state)) and not bool(clip_timeline_state.playing))
	_check("vm_lane_executed_clip_initialize_program", int(inspection_runtime.vm_executed_program_count) == 2 and int(inspection_runtime.vm_fallback_program_count) == 0)
	_check("blocked_unload_clip_action_cannot_execute", not bool(inspection_runtime.execute_clip_action("palantir:300:fixture/1", 0x040000, clip_timeline_state)))
	_check("inspection_mode_is_explicitly_diagnosed", _has_diagnostic(inspection_runtime.diagnostics, "apt-static-subset-explicitly-enabled"))
	_check("proven_good_double_is_bound", String(inspection_runtime.initial_frame_variant) == "good-double" and String(inspection_runtime.get_meta("initial_frame_variant", "")) == "good-double")
	_check("side_command_bar_remains_hidden", String(inspection_runtime.side_command_bar_initial_state) == "hidden-offscreen" and String(inspection_runtime.get_meta("side_command_bar_initial_state", "")) == "hidden-offscreen")
	_check("men_fords_side_fade_contract_binds", bool(inspection_runtime.side_command_fade_runtime_ready) and bool(inspection_runtime.get_meta("side_command_fade_runtime_ready", false)))
	_check("men_fords_side_fade_target_math_is_exact", [31, 32, 37, 41, 42].map(func(frame): return inspection_runtime.side_command_fade_target(frame)) == [12, 22, 17, 13, 12])
	var all_roster_eligible := true
	var roster_id := 100
	for selector in runtime_script.MEN_FORDS_SIDE_FADE_ROSTER:
		var roster_row := runtime_script.MEN_FORDS_SIDE_FADE_ROSTER[selector] as Dictionary
		var context := {"selected_ids": [], "selected_structure_id": 0, "entities": {}, "structures": {}, "winner": -1, "local_team": 0}
		if String(roster_row.kind) == "battalion":
			context.selected_ids = [roster_id]
			context.entities = {roster_id: {"team": 0, "health": 100, "unit_type": selector}}
		else:
			context.selected_structure_id = roster_id
			context.structures = {roster_id: {"team": 0, "health": 100, "structure_kind": selector, "production": []}}
		var roster_result := inspection_runtime.evaluate_men_fords_selection(context) as Dictionary
		all_roster_eligible = all_roster_eligible and bool(roster_result.get("valid", false)) and bool(roster_result.get("eligible", false)) and int(roster_result.get("eligibleCommandCount", -1)) == int(roster_row.eligibleCount)
		roster_id += 1
	_check("all_nine_men_fords_roster_selections_are_typed", all_roster_eligible)
	var mixed_context := {
		"selected_ids": [1, 2], "selected_structure_id": 0, "winner": -1, "local_team": 0,
		"entities": {
			1: {"team": 0, "health": 100, "unit_type": "bfme2.object.gondor-fighter-horde"},
			2: {"team": 0, "health": 100, "unit_type": "bfme2.object.gondor-archer"},
		},
		"structures": {},
	}
	var mixed_result := inspection_runtime.evaluate_men_fords_selection(mixed_context) as Dictionary
	_check("mixed_men_battalions_use_shared_commands", bool(mixed_result.valid) and bool(mixed_result.eligible) and int(mixed_result.eligibleCommandCount) == 3)
	var invalid_exclusive := mixed_context.duplicate(true)
	invalid_exclusive.selected_structure_id = 7
	invalid_exclusive.structures = {7: {"team": 0, "health": 100, "structure_kind": "farm", "production": []}}
	_check("mixed_selection_kinds_fail_closed", not bool((inspection_runtime.evaluate_men_fords_selection(invalid_exclusive) as Dictionary).valid))
	var enemy_context := mixed_context.duplicate(true)
	(enemy_context.entities[1] as Dictionary).team = 1
	_check("enemy_selection_does_not_fade", not bool((inspection_runtime.evaluate_men_fords_selection(enemy_context) as Dictionary).eligible))
	var post_match_context := mixed_context.duplicate(true)
	post_match_context.winner = 0
	_check("post_match_selection_does_not_fade", not bool((inspection_runtime.evaluate_men_fords_selection(post_match_context) as Dictionary).eligible))
	var no_selection_context := {"selected_ids": [], "selected_structure_id": 0, "entities": {}, "structures": {}, "winner": -1, "local_team": 0}
	_check("no_selection_keeps_loaded_hidden_state", bool(inspection_runtime.sync_men_fords_selection(no_selection_context)) and int(inspection_runtime.side_command_fade_state().nativeState) == 1 and not bool(inspection_runtime.side_command_fade_state().visible))
	_check("eligible_selection_dispatches_exact_fade_start", bool(inspection_runtime.sync_men_fords_selection(mixed_context)) and int(inspection_runtime.side_command_fade_state().nativeState) == 2 and int(inspection_runtime.side_command_fade_state().currentFrameOneBased) == 12 and bool(inspection_runtime.side_command_fade_state().playing))
	for _frame in range(20):
		inspection_runtime.advance_side_command_fade_frame()
	var settled_fade := inspection_runtime.side_command_fade_state() as Dictionary
	_check("fade_completion_and_settled_stop_are_exact", int(settled_fade.nativeState) == 3 and bool(settled_fade.completionDispatched) and int(settled_fade.currentFrameOneBased) == 31 and not bool(settled_fade.playing) and String(settled_fade.label) == "settled-stop")
	_check("fade_dispatch_order_is_exact", (settled_fade.dispatchLog as Array).map(func(row): return String((row as Dictionary).kind)) == ["FadeIn", "OnAptInGameSideCommandBarFadeInComplete", "Stop"])
	_check("typed_text_button_inventories_bind", int(inspection_runtime.font_count) == 1 and int(inspection_runtime.embedded_font_glyph_count) == 1 and int(inspection_runtime.text_count) == 1 and int(inspection_runtime.text_instance_count) == 1 and int(inspection_runtime.button_count) == 1 and int(inspection_runtime.button_instance_count) == 1 and int(inspection_runtime.button_action_count) == 0)
	_check("live_text_exact_normal_format", bool(inspection_runtime.set_live_text_values(1200, 1.0, 60, 200)) and String(inspection_runtime.live_text_value("$PalantirResources")) == "1200" and String(inspection_runtime.live_text_value("$PalantirResourceMultiplier")) == " " and String(inspection_runtime.live_text_value("$PalantirCommandPoints")) == "60/200")
	_check("live_text_exact_branch_format", bool(inspection_runtime.set_live_text_values(-1, 1.5, 60, -1)) and String(inspection_runtime.live_text_value("$PalantirResources")) == " " and String(inspection_runtime.live_text_value("$PalantirResourceMultiplier")) == "x1.5" and String(inspection_runtime.live_text_value("$PalantirCommandPoints")) == "60")
	var invalid_live_runtime = runtime_script.new()
	root.add_child(invalid_live_runtime)
	_check("nonfinite_live_multiplier_fails_closed", not bool(invalid_live_runtime.set_live_text_values(0, NAN, 0, 0)) and String(invalid_live_runtime.error).contains("non-finite"), String(invalid_live_runtime.error))

	var planner_schema := document.duplicate(true)
	planner_schema["schema"] = "openbfme.retail-hud-apt-plan"
	var planner_runtime = runtime_script.new()
	root.add_child(planner_runtime)
	_check("planner_only_contract_cannot_render", not bool(planner_runtime.configure_document(planner_schema, fixture_root, true)) and String(planner_runtime.error).contains("schema"), String(planner_runtime.error))

	var weakened := document.duplicate(true)
	(weakened.renderPolicy as Dictionary)["syntheticFallbackAllowed"] = true
	var weakened_runtime = runtime_script.new()
	root.add_child(weakened_runtime)
	_check("synthetic_fallback_policy_is_rejected", not bool(weakened_runtime.configure_document(weakened, fixture_root, true)) and String(weakened_runtime.error).contains("weakened"), String(weakened_runtime.error))

	var malformed_wnd := document.duplicate(true)
	((malformed_wnd.wndCompanion as Dictionary).source as Dictionary)["sha256"] = "0".repeat(64)
	var malformed_wnd_runtime = runtime_script.new()
	root.add_child(malformed_wnd_runtime)
	_check("malformed_wnd_companion_fails_atomically", not bool(malformed_wnd_runtime.configure_document(malformed_wnd, fixture_root, true)) and malformed_wnd_runtime.wnd_companion_runtime() == null and not bool(malformed_wnd_runtime.wnd_companion_ready) and String(malformed_wnd_runtime.error).contains("WND companion"), String(malformed_wnd_runtime.error))

	var malformed_external_slot := document.duplicate(true)
	((((malformed_external_slot.externalMovieAttachments as Array)[2] as Dictionary).placeholder as Dictionary))["depth"] = 175
	var malformed_external_slot_runtime = runtime_script.new()
	root.add_child(malformed_external_slot_runtime)
	_check("changed_external_slot_fails_closed", not bool(malformed_external_slot_runtime.configure_document(malformed_external_slot, fixture_root, true)) and String(malformed_external_slot_runtime.error).contains("authored transform"), String(malformed_external_slot_runtime.error))

	var guessed_hero_show := document.duplicate(true)
	((guessed_hero_show.externalMovieAttachments as Array)[2] as Dictionary)["defaultState"] = "shown"
	var guessed_hero_show_runtime = runtime_script.new()
	root.add_child(guessed_hero_show_runtime)
	_check("guessed_hero_show_fails_closed", not bool(guessed_hero_show_runtime.configure_document(guessed_hero_show, fixture_root, true)) and String(guessed_hero_show_runtime.error).contains("slot identity"), String(guessed_hero_show_runtime.error))

	var reset_runtime = runtime_script.new()
	root.add_child(reset_runtime)
	_check("external_slot_reset_fixture_binds", bool(reset_runtime.configure_document(document, fixture_root, true)) and int(reset_runtime.external_movie_slot_count) == 4)
	reset_runtime.reset_runtime()
	_check("external_slot_reset_is_atomic_and_callback_free", int(reset_runtime.external_movie_slot_count) == 0 and not bool(reset_runtime.external_movie_slots_ready) and reset_runtime.get_node_or_null("SpellBookUI") == null and reset_runtime.get_node_or_null("helpBox") == null and reset_runtime.get_node_or_null("HeroSelectUI") == null and reset_runtime.get_node_or_null("planningModeUI") == null)

	var guessed_variant := document.duplicate(true)
	((guessed_variant.frameSelection as Dictionary).palantir as Dictionary)["selectedVariant"] = "good-single"
	var guessed_variant_runtime = runtime_script.new()
	root.add_child(guessed_variant_runtime)
	_check("unproven_good_single_is_rejected", not bool(guessed_variant_runtime.configure_document(guessed_variant, fixture_root, true)) and String(guessed_variant_runtime.error).contains("proven retail _good"), String(guessed_variant_runtime.error))

	var changed_fade := document.duplicate(true)
	((changed_fade.sideCommandFadeRuntime as Dictionary).timeline as Dictionary)["fadeInBodySha256"] = "0".repeat(64)
	var changed_fade_runtime = runtime_script.new()
	root.add_child(changed_fade_runtime)
	_check("changed_side_fade_evidence_is_rejected", not bool(changed_fade_runtime.configure_document(changed_fade, fixture_root, true)) and String(changed_fade_runtime.error).contains("FadeIn body"), String(changed_fade_runtime.error))

	var broken_timeline := document.duplicate(true)
	(((broken_timeline.timelines as Array)[0] as Dictionary).frames as Array)[1]["frameIndex"] = 7
	var broken_timeline_runtime = runtime_script.new()
	root.add_child(broken_timeline_runtime)
	_check("nonsequential_timeline_fails_closed", not bool(broken_timeline_runtime.configure_document(broken_timeline, fixture_root, true)) and String(broken_timeline_runtime.error).contains("sequence"), String(broken_timeline_runtime.error))

	var malformed_action := document.duplicate(true)
	((malformed_action.actionScripts as Array)[0] as Dictionary)["terminalStackDepth"] = 1
	var malformed_action_runtime = runtime_script.new()
	root.add_child(malformed_action_runtime)
	_check("malformed_action_stack_fails_closed", not bool(malformed_action_runtime.configure_document(malformed_action, fixture_root, true)) and String(malformed_action_runtime.error).contains("unsupported semantics"), String(malformed_action_runtime.error))

	var malformed_minlod := minlod_document.duplicate(true)
	for program_value in malformed_minlod.actionScripts as Array:
		if String((program_value as Dictionary).get("scriptId", "")) == "palantir:333872":
			((((program_value as Dictionary).effects as Array)[0] as Dictionary).whenTrue as Array)[0]["target"] = "effect2"
	var malformed_minlod_runtime = runtime_script.new()
	root.add_child(malformed_minlod_runtime)
	_check("minlod_exact_target_identity_is_required", not bool(malformed_minlod_runtime.configure_document(malformed_minlod, fixture_root, true)) and String(malformed_minlod_runtime.error).contains("unsupported semantics"), String(malformed_minlod_runtime.error))

	var malformed_clip_order := document.duplicate(true)
	((((malformed_clip_order.clipActions as Array)[0] as Dictionary).events as Array)[1] as Dictionary)["eventIndex"] = 7
	var malformed_clip_order_runtime = runtime_script.new()
	root.add_child(malformed_clip_order_runtime)
	_check("malformed_clip_event_order_fails_closed", not bool(malformed_clip_order_runtime.configure_document(malformed_clip_order, fixture_root, true)) and String(malformed_clip_order_runtime.error).contains("mask, order"), String(malformed_clip_order_runtime.error))

	var missing_blocker := document.duplicate(true)
	missing_blocker["unsupportedSemantics"] = [
		(missing_blocker.unsupportedSemantics as Array)[0],
		(missing_blocker.unsupportedSemantics as Array)[1],
		(missing_blocker.unsupportedSemantics as Array)[-2],
		(missing_blocker.unsupportedSemantics as Array)[-1],
		(missing_blocker.unsupportedSemantics as Array)[-5],
		(missing_blocker.unsupportedSemantics as Array)[-4],
		(missing_blocker.unsupportedSemantics as Array)[-3],
	]
	(missing_blocker.summary as Dictionary)["blockerCount"] = 7
	var missing_blocker_runtime = runtime_script.new()
	root.add_child(missing_blocker_runtime)
	_check("bounded_selection_blockers_are_required", not bool(missing_blocker_runtime.configure_document(missing_blocker, fixture_root, true)) and String(missing_blocker_runtime.error).contains("selection blockers"), String(missing_blocker_runtime.error))

	var malformed_font := document.duplicate(true)
	((malformed_font.fonts as Array)[0] as Dictionary)["glyphCount"] = 2
	var malformed_font_runtime = runtime_script.new()
	root.add_child(malformed_font_runtime)
	_check("malformed_font_glyph_inventory_fails_closed", not bool(malformed_font_runtime.configure_document(malformed_font, fixture_root, true)) and String(malformed_font_runtime.error).contains("glyph inventory"), String(malformed_font_runtime.error))

	var missing_text_font := document.duplicate(true)
	((missing_text_font.texts as Array)[0] as Dictionary)["fontId"] = "fixture:missing"
	var missing_text_font_runtime = runtime_script.new()
	root.add_child(missing_text_font_runtime)
	_check("missing_exact_text_font_fails_closed", not bool(missing_text_font_runtime.configure_document(missing_text_font, fixture_root, true)) and String(missing_text_font_runtime.error).contains("layout changed"), String(missing_text_font_runtime.error))

	var malformed_button_state := document.duplicate(true)
	((((malformed_button_state.buttons as Array)[0] as Dictionary).records as Array)[0] as Dictionary)["states"] = ["up"]
	var malformed_button_state_runtime = runtime_script.new()
	root.add_child(malformed_button_state_runtime)
	_check("missing_exact_button_hit_state_fails_closed", not bool(malformed_button_state_runtime.configure_document(malformed_button_state, fixture_root, true)) and String(malformed_button_state_runtime.error).contains("hit state"), String(malformed_button_state_runtime.error))

	var vm_lane_document := document.duplicate(true)
	(vm_lane_document.actionScripts as Array).append_array(_vm_lane_fixture_programs())
	(vm_lane_document.summary as Dictionary)["actionScriptCount"] = 10
	(vm_lane_document.summary as Dictionary)["supportedActionScriptCount"] = 9
	(vm_lane_document.summary as Dictionary)["unsupportedActionScriptCount"] = 1
	var vm_lane_runtime = runtime_script.new()
	root.add_child(vm_lane_runtime)
	_check("vm_lane_fixture_binds", bool(vm_lane_runtime.configure_document(vm_lane_document, fixture_root, true)), String(vm_lane_runtime.error))
	var vm_goto_state := {"frame": 0}
	_check("vm_lane_goto_frame_executes_via_vm", bool(vm_lane_runtime.execute_action_script("palantir:140", vm_goto_state)) and int(vm_goto_state.frame) == 5 and int(vm_lane_runtime.vm_executed_program_count) == 1)
	var vm_label_state := {"label": ""}
	_check("vm_lane_goto_label_executes_via_vm", bool(vm_lane_runtime.execute_action_script("palantir:141", vm_label_state)) and String(vm_label_state.label) == "_show" and int(vm_lane_runtime.vm_executed_program_count) == 2)
	var vm_fallback_state := {"playing": false}
	_check("vm_lane_unsynthesizable_falls_back_to_legacy", bool(vm_lane_runtime.execute_action_script("palantir:142", vm_fallback_state)) and bool(vm_fallback_state.playing) and int(vm_lane_runtime.vm_fallback_program_count) == 1 and _has_diagnostic(vm_lane_runtime.diagnostics, "apt-vm-lane-fallback"))
	_check("vm_lane_fallback_diagnosed_once", bool(vm_lane_runtime.execute_action_script("palantir:142", vm_fallback_state)) and int(vm_lane_runtime.vm_fallback_program_count) == 2 and _count_diagnostic(vm_lane_runtime.diagnostics, "apt-vm-lane-fallback") == 1)
	# Tier-4: VM-lane property writes drive real display nodes, PlaySound
	# routes to the injectable audio-intent surface, and binding gaps fail
	# closed into diagnostics.
	var fx_item := Node2D.new()
	fx_item.name = "FxItemDisplay"
	root.add_child(fx_item)
	_check("vm_display_binding_registers", bool(vm_lane_runtime.bind_vm_display_item("fxItem", fx_item)) and not bool(vm_lane_runtime.bind_vm_display_item("", fx_item)))
	var vm_property_state := {}
	_check("vm_lane_property_write_moves_real_node", bool(vm_lane_runtime.execute_action_script("palantir:143", vm_property_state)) and absf(fx_item.position.x - 77.0) < 0.0001 and int(vm_lane_runtime.vm_executed_program_count) == 3)
	_check("vm_lane_alpha_write_fades_real_node", bool(vm_lane_runtime.execute_action_script("palantir:144", vm_property_state)) and absf(fx_item.modulate.a - 0.4) < 0.0001)
	_check("vm_lane_visible_write_hides_real_node", bool(vm_lane_runtime.execute_action_script("palantir:145", vm_property_state)) and not fx_item.visible)
	_check("vm_lane_playsound_intent_recorded", bool(vm_lane_runtime.execute_action_script("palantir:146", vm_property_state)) and (vm_lane_runtime.vm_audio_intents as Array).size() == 1 and (vm_lane_runtime.vm_audio_intents as Array)[0] == {"eventId": "Gui_PalantirResourceBarFlash", "dispatch": "FSCommand:PlaySound", "sourceScriptId": "palantir:146"})
	var captured_intents: Array = []
	vm_lane_runtime.set_vm_audio_intent_callback(func(intent: Dictionary) -> void: captured_intents.append(intent))
	_check("vm_lane_playsound_intent_routes_to_callback", bool(vm_lane_runtime.execute_action_script("palantir:146", vm_property_state)) and captured_intents.size() == 1 and String((captured_intents[0] as Dictionary).get("eventId", "")) == "Gui_PalantirResourceBarFlash" and (vm_lane_runtime.vm_audio_intents as Array).size() == 1)
	var fx_position_before_gap := fx_item.position.x
	_check("vm_lane_unbound_property_records_binding_gap", bool(vm_lane_runtime.execute_action_script("palantir:147", vm_property_state)) and _count_diagnostic(vm_lane_runtime.diagnostics, "apt-vm-display-binding-gap") == 1 and absf(fx_item.position.x - fx_position_before_gap) < 0.0001)
	_check("vm_lane_binding_gap_diagnosed_once", bool(vm_lane_runtime.execute_action_script("palantir:147", vm_property_state)) and _count_diagnostic(vm_lane_runtime.diagnostics, "apt-vm-display-binding-gap") == 1)
	vm_lane_runtime.reset_runtime()
	_check("vm_lane_counters_reset_atomically", int(vm_lane_runtime.vm_executed_program_count) == 0 and int(vm_lane_runtime.vm_fallback_program_count) == 0 and (vm_lane_runtime.vm_audio_intents as Array).is_empty())
	_run_private_contract_if_requested()
	_finish()


func _run_private_contract_if_requested() -> void:
	var contract_path := ""
	var pack_root := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--retail-hud-apt-contract="):
			contract_path = argument.trim_prefix("--retail-hud-apt-contract=")
		elif argument.begins_with("--retail-hud-apt-pack-root="):
			pack_root = argument.trim_prefix("--retail-hud-apt-pack-root=")
	if contract_path == "" and pack_root == "":
		return
	_check("private_contract_arguments_complete", contract_path != "" and pack_root != "")
	if contract_path == "" or pack_root == "":
		return
	var file := FileAccess.open(contract_path, FileAccess.READ)
	_check("private_contract_readable", file != null)
	if file == null:
		return
	var value: Variant = JSON.parse_string(file.get_as_text())
	_check("private_contract_json_valid", typeof(value) == TYPE_DICTIONARY)
	if typeof(value) != TYPE_DICTIONARY:
		return
	# The private atlases live outside res:// and are not imported in this
	# headless code-only gate. Keep the complete private timeline/blocker data,
	# but use the legal-safe triangle solely to exercise contract validation.
	var validation_document := (value as Dictionary).duplicate(true)
	validation_document["draws"] = _document()["draws"]
	(validation_document.summary as Dictionary)["drawCount"] = 1
	(validation_document.summary as Dictionary)["displayItemCount"] = 4
	var missing_private_font_runtime = runtime_script.new()
	root.add_child(missing_private_font_runtime)
	_check("private_missing_exact_font_fails_closed", not bool(missing_private_font_runtime.configure_document(validation_document, fixture_root, true)) and String(missing_private_font_runtime.error).contains("font is missing"), String(missing_private_font_runtime.error))
	var malformed_live_target := validation_document.duplicate(true)
	(((malformed_live_target.textInstances as Array)[0] as Dictionary).runtimeSource as Dictionary)["initialValue"] = "$MissingPalantirValue"
	var malformed_live_target_runtime = runtime_script.new()
	root.add_child(malformed_live_target_runtime)
	_check("private_missing_live_target_fails_closed", not bool(malformed_live_target_runtime.configure_document(malformed_live_target, pack_root, true)) and String(malformed_live_target_runtime.error).contains("operands changed"), String(malformed_live_target_runtime.error))
	var private_runtime = runtime_script.new()
	root.add_child(private_runtime)
	var private_bound := bool(private_runtime.configure_document(validation_document, pack_root, true))
	_check("private_exact_timeline_contract_binds", private_bound, "%s side=%s meta=%s actions=%s supported=%s blockers=%s" % [private_runtime.error, private_runtime.side_command_topology_ready, private_runtime.get_meta("side_command_topology_ready", false), private_runtime.action_script_count, private_runtime.supported_action_script_count, private_runtime.blocker_count])
	_check("private_exact_timeline_counts_match", int(private_runtime.timeline_count) == 22 and int(private_runtime.timeline_frame_count) == 640 and int(private_runtime.timeline_instance_count) == 51)
	_check("private_timeline_contract_remains_fail_closed", not bool(private_runtime.parity_ready) and int(private_runtime.blocker_count) == 19)
	_check("private_men_fords_side_fade_runtime_binds", bool(private_runtime.side_command_fade_runtime_ready) and not _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "side-command-bar-fade-runtime-not-bound"))
	var private_wnd = private_runtime.wnd_companion_runtime()
	_check("private_exact_wnd_companion_binds", bool(private_runtime.wnd_companion_ready) and int(private_runtime.wnd_typed_callback_count) == 15 and private_wnd != null and bool(private_wnd.companion_configured))
	_check("private_text_button_counts_match", int(private_runtime.font_count) == 1 and int(private_runtime.embedded_font_glyph_count) == 0 and int(private_runtime.text_count) == 3 and int(private_runtime.text_instance_count) == 3 and int(private_runtime.button_count) == 1 and int(private_runtime.button_instance_count) == 3 and int(private_runtime.button_action_count) == 0)
	_check("private_action_subset_counts_match", int(private_runtime.action_script_count) == 74 and int(private_runtime.supported_action_script_count) == 66)
	_check("private_side_command_topology_binds", bool(private_runtime.side_command_topology_ready) and bool(private_runtime.get_meta("side_command_topology_ready", false)))
	var side_state := private_runtime.make_side_command_state() as Dictionary
	_check("private_side_command_state_has_exact_local_buttons", (side_state.get("buttons", {}) as Dictionary).size() == 12 and (side_state.get("buttons", {}) as Dictionary).has("Button0") and (side_state.get("buttons", {}) as Dictionary).has("Button11") and not (side_state.get("buttons", {}) as Dictionary).has("Button12"))
	_check("private_side_command_ingame_input_is_required", not bool(private_runtime.execute_action_script("ingamesidecommandbar:7296", side_state)))
	_check("private_side_command_false_branch_is_exact_noop", bool(private_runtime.execute_action_script("ingamesidecommandbar:7296", side_state, {"InGame": false})) and (side_state.get("dispatchLog", []) as Array).is_empty())
	_check("private_side_command_show_executes", bool(private_runtime.execute_action_script("ingamesidecommandbar:7296", side_state, {"InGame": true})))
	var side_buttons := side_state.get("buttons", {}) as Dictionary
	_check("private_side_command_truth_table_applies", String((((side_buttons.Button1 as Dictionary).children as Dictionary).Frame as Dictionary).label) == "_top" and String((((side_buttons.Button2 as Dictionary).children as Dictionary).Frame as Dictionary).label) == "_middle" and String((((side_buttons.Button10 as Dictionary).children as Dictionary).Frame as Dictionary).label) == "_middle" and String((((side_buttons.Button11 as Dictionary).children as Dictionary).Frame as Dictionary).label) == "_bottom")
	var side_log := side_state.get("dispatchLog", []) as Array
	var show_calls: Array[String] = []
	for event_value in side_log:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "show-call":
			show_calls.append(String(event.get("target", "")))
	_check("private_side_command_targets_are_ascending", show_calls == ["Button1", "Button2", "Button3", "Button4", "Button5", "Button6", "Button7", "Button8", "Button9", "Button10", "Button11", "Button12", "Button13", "Button14", "Button15"])
	_check("private_side_command_absent_targets_are_noop", side_log.slice(-4).map(func(row): return (row as Dictionary).get("present", true)) == [false, false, false, false])
	var first_button_one_place := -1
	var first_button_one_update := -1
	for index in side_log.size():
		var event := side_log[index] as Dictionary
		if String(event.get("target", "")) == "Button1" and String(event.get("kind", "")) == "place" and first_button_one_place < 0:
			first_button_one_place = index
		if String(event.get("target", "")) == "Button1" and String(event.get("kind", "")) == "update-frame" and first_button_one_update < 0:
			first_button_one_update = index
	_check("private_side_command_placement_precedes_queued_action", first_button_one_place >= 0 and first_button_one_update > first_button_one_place)
	var neighbor_state := private_runtime.make_side_command_state() as Dictionary
	for name in ["Button4", "Button6"]:
		var neighbor_buttons := neighbor_state.buttons as Dictionary
		var neighbor_button := neighbor_buttons[name] as Dictionary
		(neighbor_button.children as Dictionary)["Frame"] = {"label": "", "playing": true}
		neighbor_buttons[name] = neighbor_button
		neighbor_state.buttons = neighbor_buttons
	_check("private_side_command_neighbor_update_executes", bool(private_runtime.execute_action_script("ingamesidecommandbar:6272", {"name": "Button5"}, {"buttonSet": neighbor_state})))
	_check("private_side_command_neighbor_order_is_next_then_prior", (neighbor_state.dispatchLog as Array).map(func(row): return String((row as Dictionary).get("target", ""))) == ["Button6", "Button4"])
	var malformed_side_topology := validation_document.duplicate(true)
	((malformed_side_topology.sideCommandTopology as Dictionary).source as Dictionary)["sha256"] = "0".repeat(64)
	var malformed_side_runtime = runtime_script.new()
	root.add_child(malformed_side_runtime)
	_check("private_side_command_source_identity_fails_closed", not bool(malformed_side_runtime.configure_document(malformed_side_topology, pack_root, true)) and String(malformed_side_runtime.error).contains("side-command source identity"), String(malformed_side_runtime.error))
	_check("private_palantir_command_topology_binds", bool(private_runtime.palantir_command_topology_ready) and bool(private_runtime.get_meta("palantir_command_topology_ready", false)))
	var command_state := private_runtime.make_palantir_command_state() as Dictionary
	_check("private_palantir_lifecycle_registration_executes", bool(private_runtime.execute_action_script("palantir:169224", command_state)))
	_check("private_palantir_lifecycle_registration_is_declaration_only", (command_state.lifecycleFunctions as Dictionary).size() == 6 and (command_state.registrationLog as Array).size() == 6 and (command_state.dispatchLog as Array).is_empty())
	_check("private_palantir_lifecycle_order_is_exact", (command_state.registrationLog as Array).map(func(row): return String((row as Dictionary).get("name", ""))) == ["OnMovieClipFrameLoaded", "OnMovieClipFrameUnloaded", "OnCommandButtonSubMenuLoaded", "OnCommandButtonSubMenuUnloaded", "OnCommandButtonToggleFlashLoaded", "OnCommandButtonToggleFlashUnloaded"])
	_check("private_palantir_show_frame_registers_methods", bool(private_runtime.enter_palantir_command_show_frame(command_state)))
	var command_children := command_state.children as Dictionary
	var exact_method_inventory := true
	for index in range(6):
		var button := command_children[str(index)] as Dictionary
		exact_method_inventory = exact_method_inventory and (button.methods as Dictionary).keys() == ["SetAutoAbilityOverlayState", "SetFlashEffectState", "SetGlassState"]
	_check("private_palantir_numeric_frames_have_exact_methods", exact_method_inventory)
	var command_registration_log := command_state.registrationLog as Array
	var final_placement_index := -1
	var first_method_registration_index := -1
	for index in command_registration_log.size():
		var event := command_registration_log[index] as Dictionary
		if String(event.get("kind", "")) == "place" and int(event.get("depth", -1)) == 45:
			final_placement_index = index
		if String(event.get("kind", "")) == "register-button-method" and first_method_registration_index < 0:
			first_method_registration_index = index
	_check("private_palantir_placements_precede_queued_registration", final_placement_index >= 0 and first_method_registration_index > final_placement_index)
	_check("private_palantir_overlay_method_dispatches", bool(private_runtime.execute_palantir_command_button_method(command_state, "2", "SetAutoAbilityOverlayState", "_disabled")) and String((((command_state.root as Dictionary).AutoAbilityOverlays as Dictionary).children as Dictionary)["2"].label) == "_disabled")
	_check("private_palantir_flash_method_dispatches", bool(private_runtime.execute_palantir_command_button_method(command_state, "2", "SetFlashEffectState", "_flash")) and String((((command_children.FlashEffects as Dictionary).children as Dictionary)["2"] as Dictionary).label) == "_flash")
	_check("private_palantir_glass_method_dispatches", bool(private_runtime.execute_palantir_command_button_method(command_state, "2", "SetGlassState", "_up")) and String((command_children.glass2 as Dictionary).label) == "_up")
	_check("private_palantir_root_effect_trace_gate_remains_blocked", not bool(private_runtime.execute_action_script("palantir:167296", command_state)))
	var malformed_command_topology := validation_document.duplicate(true)
	((malformed_command_topology.palantirCommandTopology as Dictionary).source as Dictionary)["sha256"] = "0".repeat(64)
	var malformed_command_runtime = runtime_script.new()
	root.add_child(malformed_command_runtime)
	_check("private_palantir_command_source_identity_fails_closed", not bool(malformed_command_runtime.configure_document(malformed_command_topology, pack_root, true)) and String(malformed_command_runtime.error).contains("command source identity"), String(malformed_command_runtime.error))
	_check("private_resource_flash_contract_binds", bool(private_runtime.resource_flash_ready) and bool(private_runtime.get_meta("resource_flash_ready", false)))
	var flash_state := {"timelineId": "palantir:309", "frame": 0, "label": "_stop", "playing": false, "audioEventIntents": []}
	_check("private_play_command_point_effect_rewinds_exact_instance", bool(private_runtime.play_command_point_effect(flash_state)) and int(flash_state.frame) == 8 and String(flash_state.label) == "_go" and bool(flash_state.playing))
	_check("private_resource_flash_exposes_exact_audio_intent", (flash_state.audioEventIntents as Array).size() == 1 and (flash_state.audioEventIntents as Array)[0] == {"eventId": "Gui_PalantirResourceBarFlash", "dispatch": "FSCommand:PlaySound", "sourceScriptId": "palantir:332504"})
	_check("private_resource_flash_retrigger_rewinds_and_reissues", bool(private_runtime.play_command_point_effect(flash_state)) and int(flash_state.frame) == 8 and (flash_state.audioEventIntents as Array).size() == 2)
	var malformed_flash_state := {"timelineId": "palantir:309", "frame": 0, "playing": false, "audioEventIntents": "invented-mixer"}
	_check("private_resource_flash_rejects_invented_mixer_state", not bool(private_runtime.play_command_point_effect(malformed_flash_state)) and int(malformed_flash_state.frame) == 0)
	var changed_flash_event := validation_document.duplicate(true)
	((changed_flash_event.resourceFlash as Dictionary).audioEventIntent as Dictionary)["eventId"] = "InventedFlash"
	var changed_flash_event_runtime = runtime_script.new()
	root.add_child(changed_flash_event_runtime)
	_check("private_resource_flash_event_identity_fails_closed", not bool(changed_flash_event_runtime.configure_document(changed_flash_event, pack_root, true)) and String(changed_flash_event_runtime.error).contains("resource-flash exact contract"), String(changed_flash_event_runtime.error))
	var private_globe_state := {"_name": "GlobeSwirlRender", "playing": true}
	_check("private_minlod_missing_input_fails_closed", not bool(private_runtime.execute_action_script("palantir:152912", private_globe_state)))
	_check("private_minlod_false_globe_branch_is_noop", bool(private_runtime.execute_action_script("palantir:152912", private_globe_state, {"MinLOD": false})) and bool(private_globe_state.playing))
	_check("private_minlod_true_globe_branch_stops", bool(private_runtime.execute_action_script("palantir:152912", private_globe_state, {"MinLOD": true})) and not bool(private_globe_state.playing))
	var private_effect_state := {"targets": {"effect2": {"_visible": true}, "effect3": {"_visible": true}}}
	_check("private_minlod_false_visibility_branch_is_noop", bool(private_runtime.execute_action_script("palantir:334840", private_effect_state, {"MinLOD": false})) and bool(private_effect_state.targets.effect2._visible) and bool(private_effect_state.targets.effect3._visible))
	_check("private_minlod_true_visibility_branch_hides", bool(private_runtime.execute_action_script("palantir:334840", private_effect_state, {"MinLOD": true})) and not bool(private_effect_state.targets.effect2._visible) and not bool(private_effect_state.targets.effect3._visible))
	_check("private_clip_action_counts_match", int(private_runtime.clip_action_program_count) == 6 and int(private_runtime.supported_clip_action_program_count) == 5 and int(private_runtime.clip_action_count) == 28 and int(private_runtime.clip_action_event_count) == 28 and int(private_runtime.executable_clip_action_event_count) == 27)
	var typed_binding_ids := {}
	for binding_value in (value as Dictionary).get("clipActions", []) as Array:
		var binding := binding_value as Dictionary
		for event_value in binding.get("events", []) as Array:
			var event := event_value as Dictionary
			var program_id := String(event.get("programId", ""))
			if program_id in ["ingamesidecommandbar:clip-event:13680", "libingameui:clip-event:56252", "palantir:clip-event:375628", "palantir:clip-event:375640", "palantir:clip-event:375652"]:
				typed_binding_ids[program_id] = String(binding.get("clipActionId", ""))
	_check("private_typed_initialize_programs_have_targets", typed_binding_ids.size() == 5)
	var method_state := {}
	_check("private_local_method_initialize_executes", bool(private_runtime.execute_clip_action(String(typed_binding_ids.get("ingamesidecommandbar:clip-event:13680", "")), 1, method_state)))
	_check("private_local_method_state_is_bounded", typeof(method_state.get("localMethods", {})) == TYPE_DICTIONARY and (method_state.get("localMethods", {}) as Dictionary).keys() == ["SetFlashEffectState"] and String(((method_state.get("localMethods", {}) as Dictionary).get("SetFlashEffectState", {}) as Dictionary).get("methodName", "")) == "gotoAndPlay")
	var visibility_state := {"_visible": true}
	_check("private_visibility_initialize_executes", bool(private_runtime.execute_clip_action(String(typed_binding_ids.get("libingameui:clip-event:56252", "")), 1, visibility_state)) and not bool(visibility_state.get("_visible", true)))
	var live_binding_state := {}
	_check("private_live_resource_initialize_executes", bool(private_runtime.execute_clip_action(String(typed_binding_ids.get("palantir:clip-event:375628", "")), 1, live_binding_state)) and String((live_binding_state.get("liveTextBinding", {}) as Dictionary).get("aptVariable", "")) == "$PalantirResources")
	var weakened_typed := validation_document.duplicate(true)
	for program_value in weakened_typed.clipActionPrograms as Array:
		if String((program_value as Dictionary).get("programId", "")) == "libingameui:clip-event:56252":
			(program_value as Dictionary)["sha256"] = "0".repeat(64)
	var weakened_typed_runtime = runtime_script.new()
	root.add_child(weakened_typed_runtime)
	_check("private_typed_initialize_byte_identity_is_required", not bool(weakened_typed_runtime.configure_document(weakened_typed, pack_root, true)) and String(weakened_typed_runtime.error).contains("unsupported semantics"), String(weakened_typed_runtime.error))
	var malformed_typed_effect := validation_document.duplicate(true)
	for program_value in malformed_typed_effect.clipActionPrograms as Array:
		if String((program_value as Dictionary).get("programId", "")) == "libingameui:clip-event:56252":
			((((program_value as Dictionary).get("effects", []) as Array)[0]) as Dictionary)["propertyIndex"] = 6
	var malformed_typed_effect_runtime = runtime_script.new()
	root.add_child(malformed_typed_effect_runtime)
	_check("private_typed_initialize_effect_shape_is_required", not bool(malformed_typed_effect_runtime.configure_document(malformed_typed_effect, pack_root, true)) and String(malformed_typed_effect_runtime.error).contains("unsupported semantics"), String(malformed_typed_effect_runtime.error))
	_check("private_generic_clip_blocker_removed", not _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "clip-actions-not-executed"))
	_check("private_additional_frames_are_converted", not _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "additional-timeline-frames-not-converted") and _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "timeline-playback-not-bound"))
	_check("private_generic_text_button_blockers_removed", not _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "text-character-not-converted") and not _has_blocker((value as Dictionary).get("unsupportedSemantics", []), "button-character-not-converted"))
	_check("private_exact_text_capture_blocker_is_singular", _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "text-rendered-parity-capture-not-passed") == 1 and _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "text-external-font-runtime-loading-not-bound") == 0 and _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "text-runtime-string-source-not-bound") == 0)
	_check("private_external_attachment_blockers_collapsed", _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "external-movie-target-attachment-not-bound") == 0 and _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "external-movie-lifecycle-capture-not-passed") == 1)
	_check("private_resource_flash_dynamic_gates_are_narrow", _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "resource-flash-native-trigger-capture-not-passed") == 1 and _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "resource-flash-mixer-overlap-capture-not-passed") == 1 and _count_blocker((value as Dictionary).get("unsupportedSemantics", []), "action-script-unsupported-opcodes") == 8)
	_check("private_external_slots_bind", bool(private_runtime.external_movie_slots_ready) and int(private_runtime.external_movie_slot_count) == 4 and not bool(private_runtime.external_movie_slot_state("HeroSelectUI").get("visible", true)))


## Three supported generic programs for the tier-3 VM lane: a synthesizable
## goto-frame, a synthesizable goto-label, and a deliberately
## non-synthesizable program (branch opcode) that must fall back to the
## legacy declarative path.
func _vm_lane_fixture_programs() -> Array:
	var rows: Array = []
	var specifications := [
		{
			"scriptId": "palantir:140",
			"instructions": [
				{"offset": 1400, "nextOffset": 1408, "opcode": 0x81, "name": "goto-frame", "operand": 5, "body": []},
				{"offset": 1408, "nextOffset": 1409, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [{"kind": "goto", "targetType": "frame", "target": 5}],
		},
		{
			"scriptId": "palantir:141",
			"instructions": [
				{"offset": 1500, "nextOffset": 1508, "opcode": 0x8C, "name": "goto-label", "operand": "_show", "body": []},
				{"offset": 1508, "nextOffset": 1509, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [{"kind": "goto", "targetType": "label", "target": "_show"}],
		},
		{
			"scriptId": "palantir:142",
			"instructions": [
				{"offset": 1600, "nextOffset": 1608, "opcode": 0x99, "name": "branch-always", "operand": 0, "body": []},
				{"offset": 1608, "nextOffset": 1609, "opcode": 6, "name": "play", "body": []},
				{"offset": 1609, "nextOffset": 1610, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [{"kind": "play"}],
		},
		{
			"scriptId": "palantir:143",
			"instructions": [
				{"offset": 1700, "nextOffset": 1708, "opcode": 0xA1, "name": "push-string", "operand": "fxItem", "body": []},
				{"offset": 1708, "nextOffset": 1710, "opcode": 0xB5, "name": "push-byte", "operand": 0, "body": []},
				{"offset": 1710, "nextOffset": 1712, "opcode": 0xB5, "name": "push-byte", "operand": 77, "body": []},
				{"offset": 1712, "nextOffset": 1713, "opcode": 0x23, "name": "set-property", "body": []},
				{"offset": 1713, "nextOffset": 1714, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [],
		},
		{
			"scriptId": "palantir:144",
			"instructions": [
				{"offset": 1800, "nextOffset": 1808, "opcode": 0xA1, "name": "push-string", "operand": "fxItem", "body": []},
				{"offset": 1808, "nextOffset": 1810, "opcode": 0xB5, "name": "push-byte", "operand": 6, "body": []},
				{"offset": 1810, "nextOffset": 1812, "opcode": 0xB5, "name": "push-byte", "operand": 40, "body": []},
				{"offset": 1812, "nextOffset": 1813, "opcode": 0x23, "name": "set-property", "body": []},
				{"offset": 1813, "nextOffset": 1814, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [],
		},
		{
			"scriptId": "palantir:145",
			"instructions": [
				{"offset": 1900, "nextOffset": 1908, "opcode": 0xA1, "name": "push-string", "operand": "fxItem", "body": []},
				{"offset": 1908, "nextOffset": 1910, "opcode": 0xB5, "name": "push-byte", "operand": 7, "body": []},
				{"offset": 1910, "nextOffset": 1911, "opcode": 0x74, "name": "push-false", "body": []},
				{"offset": 1911, "nextOffset": 1912, "opcode": 0x23, "name": "set-property", "body": []},
				{"offset": 1912, "nextOffset": 1913, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [],
		},
		{
			"scriptId": "palantir:146",
			"instructions": [
				{"offset": 2000, "nextOffset": 2008, "opcode": 0xA1, "name": "push-string", "operand": "FSCommand:PlaySound", "body": []},
				{"offset": 2008, "nextOffset": 2016, "opcode": 0xA1, "name": "push-string", "operand": "Gui_PalantirResourceBarFlash", "body": []},
				{"offset": 2016, "nextOffset": 2017, "opcode": 0x9A, "name": "get-url2", "body": []},
				{"offset": 2017, "nextOffset": 2018, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [],
		},
		{
			"scriptId": "palantir:147",
			"instructions": [
				{"offset": 2100, "nextOffset": 2108, "opcode": 0xA1, "name": "push-string", "operand": "", "body": []},
				{"offset": 2108, "nextOffset": 2110, "opcode": 0xB5, "name": "push-byte", "operand": 0, "body": []},
				{"offset": 2110, "nextOffset": 2112, "opcode": 0xB5, "name": "push-byte", "operand": 9, "body": []},
				{"offset": 2112, "nextOffset": 2113, "opcode": 0x23, "name": "set-property", "body": []},
				{"offset": 2113, "nextOffset": 2114, "opcode": 0, "name": "end", "body": []},
			],
			"effects": [],
		},
	]
	var source_offset := 140
	for specification_value in specifications:
		var specification := specification_value as Dictionary
		var instructions := specification.instructions as Array
		rows.append({
			"scriptId": String(specification.scriptId),
			"movie": "Palantir",
			"actionKind": "action-script",
			"sourceOffset": source_offset,
			"instructionOffset": int((instructions[0] as Dictionary).offset),
			"byteLength": 9,
			"sha256": "a1".repeat(32),
			"instructions": instructions,
			"supported": true,
			"effects": specification.effects,
			"maximumStackDepth": 0,
			"terminalStackDepth": 0,
			"unsupportedInstructions": [],
		})
		source_offset += 1
	return rows


func _minlod_fixture_programs() -> Array:
	var rows: Array = []
	var specifications := [
		{
			"scriptId": "palantir:152912", "sourceOffset": 152912,
			"instructionOffset": 366952, "byteLength": 46,
			"sha256": "069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4",
			"maximumStackDepth": 2,
			"whenTrue": [{
				"kind": "stop-timeline-if-property-equals", "target": "this",
				"propertyIndex": 13, "propertyName": "_name", "equals": "GlobeSwirlRender",
			}],
		},
		{
			"scriptId": "palantir:333872", "sourceOffset": 333872,
			"instructionOffset": 370784, "byteLength": 37,
			"sha256": "93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba",
			"maximumStackDepth": 3,
			"whenTrue": [
				{"kind": "set-named-clip-property", "target": "effect1", "propertyName": "_visible", "value": false},
				{"kind": "set-named-clip-property", "target": "effect4", "propertyName": "_visible", "value": false},
			],
		},
		{
			"scriptId": "palantir:334840", "sourceOffset": 334840,
			"instructionOffset": 370840, "byteLength": 37,
			"sha256": "0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db",
			"maximumStackDepth": 3,
			"whenTrue": [
				{"kind": "set-named-clip-property", "target": "effect2", "propertyName": "_visible", "value": false},
				{"kind": "set-named-clip-property", "target": "effect3", "propertyName": "_visible", "value": false},
			],
		},
	]
	for specification_value in specifications:
		var specification := specification_value as Dictionary
		var instruction_offset := int(specification.instructionOffset)
		var byte_length := int(specification.byteLength)
		var script_id := String(specification.scriptId)
		rows.append({
			"scriptId": script_id,
			"movie": "Palantir",
			"actionKind": "action-script",
			"sourceOffset": int(specification.sourceOffset),
			"instructionOffset": instruction_offset,
			"byteLength": byte_length,
			"sha256": String(specification.sha256),
			"instructions": [{
				"offset": instruction_offset,
				"nextOffset": instruction_offset + byte_length,
				"opcode": 0,
				"name": "end",
				"body": [],
			}],
			"supported": true,
			"effects": [{
				"kind": "conditional-min-lod",
				"condition": {"kind": "required-boolean-input", "name": "MinLOD", "equals": true},
				"whenTrue": specification.whenTrue,
				"whenFalse": [],
				"sourceEvidence": {
					"programId": script_id,
					"instructionOffset": instruction_offset,
					"instructionEndOffset": instruction_offset + byte_length,
					"byteLength": byte_length,
					"sha256": String(specification.sha256),
				},
			}],
			"maximumStackDepth": int(specification.maximumStackDepth),
			"terminalStackDepth": 0,
			"unsupportedInstructions": [],
		})
	return rows


func _external_movie_fixture_attachments() -> Array:
	var rows: Array = []
	var constants := runtime_script.get_script_constant_map() as Dictionary
	for spec_value in constants.get("EXTERNAL_MOVIE_SLOT_SPECS", []) as Array:
		var spec := spec_value as Dictionary
		rows.append({
			"loadOrder": int(spec.loadOrder),
			"loadInstructionOffset": int(spec.loadInstructionOffset),
			"swf": String(spec.swf), "movie": String(spec.movie),
			"target": String(spec.target), "targetPath": String(spec.targetPath),
			"attachmentKind": "replace-authored-empty-child-clip",
			"godotInterface": String(spec.godotInterface),
			"placeholder": {
				"sourceOffset": int(spec.sourceOffset), "recordSha256": String(spec.recordSha256),
				"characterId": 41, "depth": int(spec.depth),
				"matrix": (spec.matrix as Array).duplicate(),
				"translation": (spec.translation as Array).duplicate(),
				"tint": [1.0, 1.0, 1.0, 1.0], "additive": [0.0, 0.0, 0.0, 0.0],
			},
			"sourceRoot": {
				"characterKind": "movie", "entryFrame": 0, "frameCount": int(spec.frameCount),
				"labels": (spec.labels as Dictionary).duplicate(),
				"initialStopFrame": int(spec.initialStopFrame),
				"programOffset": int(spec.programOffset), "programSha256": String(spec.programSha256),
			},
			"defaultState": String(spec.defaultState), "normalMenVsMen": String(spec.normalMenVsMen),
			"lifecycle": {
				"loadedCallback": String(spec.loadedCallback), "unloadedCallback": String(spec.unloadedCallback),
				"argument": String(spec.argument), "dispatchBound": false,
			},
			"genericVmRequired": false, "independentRootAllowed": false,
		})
	return rows


func _wnd_fixture_companion() -> Dictionary:
	return {
		"schema": "openbfme.retail-hud-wnd-companion",
		"schemaVersion": 0,
		"source": {
			"virtualPath": "window/controlbar.wnd",
			"sha256": wnd_script.SOURCE_SHA256,
			"windowCount": 87,
			"callbackCount": 21,
			"activationAuthority": "active-companion-not-candidate-dead",
		},
		"oracleAggregates": wnd_script.ORACLE_AGGREGATES.duplicate(),
		"callbackBindings": wnd_script.BINDINGS.duplicate(true),
		"runtimeInventory": {
			"implementedCallbacks": wnd_script.IMPLEMENTED.duplicate(),
			"implementedCallbackCount": 15,
			"requiredMessageCallbacks": wnd_script.MEN_V_MEN_REQUIRED_CALLBACKS.duplicate(),
			"requiredMessageCallbackCount": 5,
			"requiredMessageUnimplemented": [],
			"outsideSlice": wnd_script.OUTSIDE_SLICE_CALLBACKS.duplicate(),
			"unresolvedBuiltins": wnd_script.UNRESOLVED_BUILTIN_CALLBACKS.duplicate(),
		},
		"dynamicGates": {
			"drawAndService": wnd_script.DRAW_SERVICE_GATES.duplicate(),
			"messageAliases": wnd_script.MESSAGE_ALIAS_GATES.duplicate(),
		},
		"liveBinding": {
			"callbackDispatchBound": false,
			"renderServicesBound": false,
			"genericDispatchAllowed": false,
			"fallbackVisualsAllowed": false,
		},
	}


func _side_command_fade_fixture() -> Dictionary:
	var roster: Array = []
	for selector in runtime_script.MEN_FORDS_SIDE_FADE_ROSTER:
		var expected := runtime_script.MEN_FORDS_SIDE_FADE_ROSTER[selector] as Dictionary
		var eligible: Array[String] = []
		for index in int(expected.eligibleCount):
			eligible.append("Eligible_%s_%d" % [selector, index])
		var multi: Array = runtime_script.MEN_FORDS_MULTI_SELECT_COMMANDS.duplicate() if String(expected.kind) == "battalion" else []
		roster.append({
			"selectionKind": String(expected.kind),
			"selectorField": String(expected.field),
			"selectorValue": String(selector),
			"retailObject": "FixtureObject_%s" % selector,
			"commandSet": String(expected.commandSet),
			"objectSource": "fixture.ini",
			"commandRowCount": int(expected.eligibleCount),
			"eligibleCommandCount": int(expected.eligibleCount),
			"inPalantirYesCommands": eligible,
			"multiSelectCommands": multi,
		})
	return {
		"schema": "openbfme.retail-hud-men-fords-side-fade",
		"schemaVersion": 0,
		"movie": "InGameSideCommandBar",
		"source": {
			"virtualPath": "InGameSideCommandBar.apt", "byteLength": 14082,
			"sha256": runtime_script.MEN_FORDS_SIDE_FADE_SOURCE_SHA256,
			"retailIniSha256": runtime_script.MEN_FORDS_RETAIL_INI_SHA256.duplicate(),
		},
		"typedInput": {
			"type": "MenFordsSelectionCommandContext", "selectedIdsField": "selected_ids",
			"selectedStructureIdField": "selected_structure_id", "entitiesField": "entities",
			"structuresField": "structures", "winnerField": "winner", "localTeamField": "local_team",
			"localTeam": 0, "inProgressWinner": -1,
			"selectionKindsMutuallyExclusive": true, "selectedIdsSortedUnique": true,
		},
		"eligibility": {
			"roster": roster,
			"singleSelectionPredicate": "local team and health > 0 and winner == -1 and eligibleCommandCount > 0",
			"multiBattalionCommands": runtime_script.MEN_FORDS_MULTI_SELECT_COMMANDS.duplicate(),
			"multiBattalionEligibleCommandCount": 3,
			"noSelectionEligible": false, "enemyDeadOrPostMatchEligible": false,
		},
		"timeline": {
			"frameCount": 42, "millisecondsPerFrame": 33,
			"labelsZeroBased": {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31},
			"initialFrameZeroBased": 0, "fadeInStartOneBased": 12, "fadeInEndOneBased": 22,
			"fadeOutStartOneBased": 32, "fadeOutEndOneBased": 42,
			"targetRule": "outside [32,42) -> 12; inside [32,42) -> 12 + 42 - currentframe",
			"targetExamples": {"31": 12, "32": 22, "37": 17, "41": 13, "42": 12},
			"fadeInBodyRange": [8836, 9009], "fadeInBodySha256": "e360a3640690bda116ca9437e11bb4ece5f5afbe5f2f46f463facc5540a8939a",
			"completionFrameOneBased": 22, "completionProgramRange": [9404, 10086],
			"completionProgramSha256": "47b0231d9b4f7952f3dba37fd2ba6f3f07914edb3a546ba63a0f873e51ef1a9c",
			"completionCallback": "OnAptInGameSideCommandBarFadeInComplete", "completionStateTransition": [2, 3],
			"settledStopFrameOneBased": 31, "settledStopProgramRange": [10088, 10090],
			"settledStopProgramSha256": "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd",
		},
		"nativeStateMachine": {
			"loadedState": 1, "fadingInState": 2, "settledVisibleState": 3,
			"dispatchOnlyOutsideStates": [2, 3], "eligibleCommandCountPredicate": "greater-than-zero",
			"fadeOutBound": false,
		},
		"remainingTraceGates": [{
			"id": "side-command-native-row-alias-trace", "blocksTypedGodotImplementation": false,
			"blocksExactNativeAliasParityClaim": true,
		}],
		"genericActionScriptVmUsed": false, "genericTimelinePlaybackRequired": false,
	}


func _document() -> Dictionary:
	return {
		"schema": "openbfme.retail-hud-apt-runtime",
		"schemaVersion": 0,
		"sceneId": "bfme2.ui.palantir",
		"aggregateSha256": "a".repeat(64),
		"authoredResolution": [1024, 768],
		"renderPolicy": {
			"actionScriptExecuted": false,
			"boundedActionScriptSubsetExecuted": true,
			"boundedClipActionSubsetExecuted": true,
			"boundedInitialSetupApplied": true,
			"defaultRuntimeMode": "fail-closed",
			"staticSubsetRequiresExplicitOptIn": true,
			"syntheticFallbackAllowed": false,
			"exactTimelineDisplayLists": true,
			"timelinePlaybackBound": false,
			"exactExternalFontLoadingBound": true,
			"exactLiveTextBindingsBound": true,
			"exactUnifiedDisplayOrder": true,
			"exactExternalMovieChildSlotsBound": true,
			"exactMenFordsSideCommandFadeRuntimeBound": true,
			"exactWndCompanionBound": true,
			"wndLiveDispatchBound": false,
			"wndRenderServicesBound": false,
			"externalMovieLifecycleCapturePassed": false,
			"renderedTextParityCapturePassed": false,
		},
		"wndCompanion": _wnd_fixture_companion(),
		"frameSelection": {
			"policy": "bounded-retail-initial-setup-plus-men-fords-side-fade",
			"actionScriptVmUsed": false,
			"unknownStatePolicy": "fail-closed",
			"palantir": {
				"initialSetupState": "_good",
				"selectedVariant": "good-double",
				"rootCharacterId": 105,
				"selectedFrameIndex": 19,
				"localImportCharacterId": 102,
				"importMovie": "PalantirExport",
				"importSymbol": "PalantirFrame_GoodDouble",
				"exportCharacterId": 19,
				"initialSetupBody": {
					"byteLength": 317,
					"byteRange": [364716, 365033],
					"sha256": "55735eb6de14ebf8e03267e14bb52feea4ff51b6dca64d3bba731a450a8e74d6",
				},
			},
			"inGameSideCommandBar": {
				"initialState": "hidden-offscreen",
				"initialFrameIndex": 0,
				"hiddenLabelFrameIndex": 1,
				"settledFrameIndex": 10,
				"fadeInLabelFrameIndex": 11,
				"fadeInApplied": false,
				"selectionDrivenFadeInBound": true,
				"fadeRuntimeContract": "sideCommandFadeRuntime",
				"buttonSetTranslation": [1048.300048828125, 361.29998779296875],
			},
		},
		"sideCommandFadeRuntime": _side_command_fade_fixture(),
		"externalMovieLoads": [
			{"loadOrder": 0, "movie": "InGameSpellBook", "runtimeAttachment": "exact-palantir-child-slot-bound", "sourceLoadReachable": true, "sourceClosurePresent": true},
			{"loadOrder": 1, "movie": "InGameSideCommandBar", "runtimeAttachment": "already-bound-root-layer", "sourceLoadReachable": true, "sourceClosurePresent": true},
			{"loadOrder": 2, "movie": "InGameHelpBox", "runtimeAttachment": "exact-palantir-child-slot-bound", "sourceLoadReachable": true, "sourceClosurePresent": true},
			{"loadOrder": 3, "movie": "InGameHeroSelect", "runtimeAttachment": "exact-palantir-child-slot-bound", "sourceLoadReachable": true, "sourceClosurePresent": true},
			{"loadOrder": 4, "movie": "InGamePlanningMode", "runtimeAttachment": "exact-palantir-child-slot-bound", "sourceLoadReachable": true, "sourceClosurePresent": true},
		],
		"externalMovieAttachments": _external_movie_fixture_attachments(),
		"externalMovieLifecycle": {
			"initialSetupLoadOrder": ["InGameSpellBook", "InGameSideCommandBar", "InGameHelpBox", "InGameHeroSelect", "InGamePlanningMode"],
			"blockedTargetLoadOrder": ["InGameSpellBook", "InGameHelpBox", "InGameHeroSelect", "InGamePlanningMode"],
			"nativeRetainedSlots": {"HeroSelectUI": "+0xc4", "helpBox": "+0xc8", "planningModeUI": "+0xcc"},
			"nativeResetClearOrder": ["HeroSelectUI", "helpBox", "planningModeUI"],
			"nativeResetSha256": "caa92439a63eac781e297a16ace1e3f48e79abe8750f6b3b1e8a5637d6a61587",
			"spellBookResetPath": "separate-fscommand-relative-order-unresolved",
			"runtimeLoadPolicy": "atomic-authored-issue-order-without-callback-dispatch",
			"runtimeResetPolicy": "atomic-clear-without-synthetic-unload-dispatch",
		},
		"sourceDiagnostics": {"flaggedNullClipActionPointers": [{
			"movie": "InGameHeroSelect", "sourceVirtualPath": "InGameHeroSelect.apt",
			"sourceOffset": 166756, "flags": 182, "clipActionsOffset": 0,
			"recordSha256": "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135",
		}]},
		"timelines": [{
			"timelineId": "palantir:9",
			"movie": "Palantir",
			"characterId": 9,
			"frameCount": 2,
			"displayListComplete": true,
			"frames": [
				{
					"frameIndex": 0,
					"actionScripts": [{"scriptId": "palantir:100", "kind": "action-script", "sourceOffset": 100, "instructionsOffset": 1000}],
					"displayList": [{
						"depth": 1,
						"characterId": 10,
						"matrix": [1.0, 0.0, 0.0, 1.0],
						"translation": [0.0, 0.0],
						"tint": [1.0, 1.0, 1.0, 1.0],
						"additive": [0.0, 0.0, 0.0, 0.0],
						"ratio": 0.0,
						"name": "",
						"clipDepth": 0,
						"sourceOffsets": [100],
					}],
					"operations": [{"kind": "place-object", "sourceOffset": 100, "flags": 2, "depth": 1}],
				},
				{
					"frameIndex": 1,
					"displayList": [],
					"operations": [{"kind": "remove-object", "sourceOffset": 200, "depth": 1}],
				},
			],
		}],
		"actionScripts": [
			{
				"scriptId": "palantir:100",
				"movie": "Palantir",
				"actionKind": "action-script",
				"sourceOffset": 100,
				"instructionOffset": 1000,
				"byteLength": 2,
				"sha256": "b".repeat(64),
				"instructions": [
					{"offset": 1000, "nextOffset": 1001, "opcode": 7, "name": "stop", "body": []},
					{"offset": 1001, "nextOffset": 1002, "opcode": 0, "name": "end", "body": []},
				],
				"supported": true,
				"effects": [{"kind": "stop"}],
				"maximumStackDepth": 0,
				"terminalStackDepth": 0,
				"unsupportedInstructions": [],
			},
			{
				"scriptId": "palantir:12",
				"movie": "Palantir",
				"actionKind": "action-script",
				"sourceOffset": 12,
				"instructionOffset": 120,
				"byteLength": 3,
				"sha256": "c".repeat(64),
				"instructions": [
					{"offset": 120, "nextOffset": 122, "opcode": 178, "name": "call-named-method-pop", "operand": 1, "body": []},
					{"offset": 122, "nextOffset": 123, "opcode": 0, "name": "end", "body": []},
				],
				"supported": false,
				"effects": [],
				"maximumStackDepth": 0,
				"terminalStackDepth": 0,
				"unsupportedInstructions": [{"offset": 120, "opcode": 178, "name": "call-named-method-pop"}],
			},
		],
		"clipActionPrograms": [
			{
				"programId": "palantir:clip-event:500",
				"movie": "Palantir",
				"actionKind": "clip-action-event",
				"sourceOffset": 500,
				"instructionOffset": 5000,
				"instructionEndOffset": 5002,
				"byteLength": 2,
				"sha256": "d".repeat(64),
				"instructions": [
					{"offset": 5000, "nextOffset": 5001, "opcode": 7, "name": "stop", "body": []},
					{"offset": 5001, "nextOffset": 5002, "opcode": 0, "name": "end", "body": []},
				],
				"supported": true,
				"effects": [{"kind": "stop"}],
				"maximumStackDepth": 0,
				"terminalStackDepth": 0,
				"unsupportedInstructions": [],
			},
			{
				"programId": "palantir:clip-event:512",
				"movie": "Palantir",
				"actionKind": "clip-action-event",
				"sourceOffset": 512,
				"instructionOffset": 5100,
				"instructionEndOffset": 5103,
				"byteLength": 3,
				"sha256": "e".repeat(64),
				"instructions": [
					{"offset": 5100, "nextOffset": 5102, "opcode": 178, "name": "call-named-method-pop", "operand": 1, "body": []},
					{"offset": 5102, "nextOffset": 5103, "opcode": 0, "name": "end", "body": []},
				],
				"supported": false,
				"effects": [],
				"maximumStackDepth": 0,
				"terminalStackDepth": 0,
				"unsupportedInstructions": [{"offset": 5100, "opcode": 178, "name": "call-named-method-pop"}],
			},
		],
		"clipActions": [{
			"clipActionId": "palantir:300:fixture/1",
			"movie": "Palantir",
			"sourceOffset": 300,
			"clipActionsOffset": 3000,
			"headerEndOffset": 3008,
			"eventTableOffset": 3200,
			"eventCount": 2,
			"headerSha256": "f".repeat(64),
			"targetPath": "fixture/1",
			"targetSourceCharacterId": 9,
			"targetMovie": "Palantir",
			"targetCharacterId": 9,
			"targetKind": "sprite",
			"targetClipId": "palantir:9",
			"targetTimelineId": "palantir:9",
			"blockerCode": "clip-action-lifecycle-dispatch-not-bound",
			"events": [
				{
					"eventIndex": 0, "eventOffset": 500, "eventEndOffset": 512, "eventMask": 1,
					"eventNames": ["initialize"], "keyCode": 0, "nextEventOffset": 0,
					"instructionsOffset": 5000, "recordSha256": "1".repeat(64),
					"programId": "palantir:clip-event:500",
					"dispatchOrder": "after-target-create-and-name-before-display-list-insert",
					"executable": true,
				},
				{
					"eventIndex": 1, "eventOffset": 512, "eventEndOffset": 524, "eventMask": 0x040000,
					"eventNames": ["unload"], "keyCode": 0, "nextEventOffset": 0,
					"instructionsOffset": 5100, "recordSha256": "2".repeat(64),
					"programId": "palantir:clip-event:512",
					"dispatchOrder": "runtime-event-dispatch-not-bound",
					"executable": false,
				},
			],
		}],
		"timelineInstances": [{
			"timelineId": "palantir:9",
			"path": "fixture/1",
			"matrix": [1.0, 0.0, 0.0, 1.0],
			"translation": [0.0, 0.0],
			"tint": [1.0, 1.0, 1.0, 1.0],
			"additive": [0.0, 0.0, 0.0, 0.0],
		}],
		"fonts": [{
			"fontId": "fixture:63", "movie": "Fixture", "characterId": 63,
			"sourceOffset": 32, "definitionByteLength": 20, "definitionSha256": "3".repeat(64),
			"name": "Fixture Embedded", "glyphCount": 1, "glyphCharacterIds": [77],
			"fontPayloadContained": true,
		}],
		"texts": [{
			"textId": "fixture:130", "movie": "Fixture", "characterId": 130,
			"sourceOffset": 64, "definitionByteLength": 60, "definitionSha256": "4".repeat(64),
			"bounds": [0.0, 0.0, 32.0, 16.0], "fontId": "fixture:63",
			"alignmentCode": 0, "color": [0.0, 0.8, 1.0, 1.0], "fontHeight": 14.0,
			"readOnly": true, "multiline": false, "wordWrap": false,
			"placeholder": "Fixture", "variableName": "", "contentPolicy": "static-placeholder",
		}],
		"textInstances": [{
			"textId": "fixture:130", "path": "fixture/text",
			"displayOrder": 1,
			"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [0.0, 0.0],
			"tint": [1.0, 1.0, 1.0, 1.0], "additive": [0.0, 0.0, 0.0, 0.0],
			"transformedBounds": [[0.0, 0.0], [32.0, 0.0], [32.0, 16.0], [0.0, 16.0]],
			"transformedColor": [0.0, 0.8, 1.0, 1.0],
			"runtimeSource": {"kind": "static-placeholder"},
		}],
		"buttons": [{
			"buttonId": "fixture:129", "movie": "Fixture", "characterId": 129,
			"sourceOffset": 128, "definitionByteLength": 60, "definitionSha256": "5".repeat(64),
			"isMenu": false, "bounds": [-50.0, -50.0, 50.0, 50.0],
			"vertices": [[-50.0, 50.0], [-50.0, -50.0], [50.0, -50.0], [50.0, 50.0]],
			"triangles": [[0, 1, 2], [2, 3, 0]],
			"records": [{
				"recordIndex": 0, "sourceOffset": 256, "stateMask": 8, "states": ["hit"],
				"characterId": 128, "depth": 1, "matrix": [1.0, 0.0, 0.0, 1.0],
				"translation": [0.0, 0.0], "color": [1.0, 1.0, 1.0, 1.0],
				"unknown": [0.0, 0.0, 0.0, 0.0],
			}],
			"actions": [], "visualStatePolicy": "source-records-only", "eventPolicy": "source-actions-only",
		}],
		"buttonInstances": [{
			"buttonId": "fixture:129", "path": "fixture/button",
			"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [0.0, 0.0],
			"tint": [1.0, 1.0, 1.0, 1.0], "additive": [0.0, 0.0, 0.0, 0.0],
			"hitTransform": {
				"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [0.0, 0.0],
				"tint": [1.0, 1.0, 1.0, 1.0], "additive": [0.0, 0.0, 0.0, 0.0],
			},
			"hitVertices": [[-50.0, 50.0], [-50.0, -50.0], [50.0, -50.0], [50.0, 50.0]],
			"hitTriangles": [[0, 1, 2], [2, 3, 0]], "eventBindings": [],
		}],
		"draws": [{
			"kind": "solid-triangle",
			"displayOrder": 0,
			"movie": "Palantir",
			"geometryId": 1,
			"points": [[0.0, 0.0], [64.0, 0.0], [0.0, 64.0]],
			"color": [1.0, 0.5, 0.25, 1.0],
			"path": "fixture",
		}],
		"unsupportedSemantics": [
			{"code": "action-script-unsupported-opcodes", "movie": "Palantir", "sourceOffset": 12},
			{
				"code": "clip-action-lifecycle-dispatch-not-bound",
				"movie": "Palantir",
				"clipActionId": "palantir:300:fixture/1",
				"sourceOffset": 300,
				"clipActionsOffset": 3000,
				"targetPath": "fixture/1",
				"events": [{"eventIndex": 1, "eventOffset": 512, "eventMask": 0x040000}],
			},
			{
				"code": "timeline-playback-not-bound",
				"movie": "APT closure",
				"timelineIds": ["palantir:9"],
				"selectionPolicy": "static-selected-frames-only",
			},
			{
				"code": "palantir-nondefault-frame-selection-not-bound",
				"movie": "Palantir",
				"appliedState": "_good",
				"unboundStates": ["_evil", "_evilSingle", "_goodSingle"],
			},
			{
				"code": "text-rendered-parity-capture-not-passed",
				"movie": "Palantir",
				"fontId": "palantir:63",
				"textCharacterIds": [130, 132, 134],
				"gateCount": 7,
				"gates": [
					"font-size-device-mapping",
					"baseline-and-glyph-origin",
					"antialiasing-and-cff-hinting",
					"final-color-and-alpha-blend",
					"ancestor-clipping",
					"final-composite-order",
					"runtime-font-winner",
				],
				"resolution": [1024, 768],
				"retailVsGodotCaptureRequired": true,
				"fallbackAllowed": false,
				"parityReady": false,
			},
			{
				"code": "external-movie-lifecycle-capture-not-passed", "movie": "Palantir",
				"gateCount": 4,
				"gates": [
					{"id": "apt-load-completion-order", "trace": "record callbacks"},
					{"id": "hero-select-initial-visibility", "trace": "record visibility"},
					{"id": "palantir-target-removal-order", "trace": "record removals"},
					{"id": "help-box-alt-anchor-runtime-value", "trace": "record anchors"},
				],
				"targets": ["SpellBookUI", "helpBox", "HeroSelectUI", "planningModeUI"],
				"loadOrder": [0, 2, 3, 4], "heroInitialVisibilityGuessed": false,
				"asyncCompletionOrderGuessed": false, "unloadOrderGuessed": false, "parityReady": false,
			},
			{
				"code": "wnd-unresolved-runtime-builtins-not-bound", "movie": "controlbar.wnd",
				"callbacks": wnd_script.UNRESOLVED_BUILTIN_CALLBACKS.duplicate(),
				"callbackCount": 4, "parityReady": false,
			},
			{
				"code": "wnd-dynamic-draw-service-capture-not-passed", "movie": "controlbar.wnd",
				"gates": wnd_script.DRAW_SERVICE_GATES.duplicate(),
				"gateCount": 7, "parityReady": false,
			},
			{
				"code": "wnd-live-dispatch-render-services-not-bound", "movie": "controlbar.wnd",
				"callbackDispatchBound": false, "renderServicesBound": false,
				"messageAliasGates": wnd_script.MESSAGE_ALIAS_GATES.duplicate(),
				"messageAliasGateCount": 7, "genericDispatchAllowed": false,
				"fallbackVisualsAllowed": false, "parityReady": false,
			},
		],
		"summary": {
			"drawCount": 1,
			"blockerCount": 9,
			"timelineCount": 1,
			"timelineFrameCount": 2,
			"timelineInstanceCount": 1,
			"actionScriptCount": 2,
			"supportedActionScriptCount": 1,
			"unsupportedActionScriptCount": 1,
			"typedMenFordsSideCommandFadeRuntimeCount": 1,
			"clipActionProgramCount": 2,
			"supportedClipActionProgramCount": 1,
			"clipActionCount": 1,
			"clipActionEventCount": 2,
			"executableClipActionEventCount": 1,
			"fontCount": 1,
			"embeddedFontGlyphCount": 1,
			"textCount": 1,
			"textInstanceCount": 1,
			"displayItemCount": 2,
			"buttonCount": 1,
			"buttonInstanceCount": 1,
			"buttonActionCount": 0,
			"externalMovieLoadCount": 5,
			"externalMovieAttachmentBlockerCount": 0,
			"externalMovieAttachmentCount": 4,
			"externalMovieLifecycleCaptureBlockerCount": 1,
			"wndCompanionBound": true,
			"wndTypedCallbackCount": 15,
			"wndRequiredMessageCallbackCount": 5,
			"wndRequiredMessageUnimplementedCount": 0,
			"wndUnresolvedBuiltinCount": 4,
			"staticSubsetReady": true,
			"parityReady": false,
		},
	}


func _write_json(path: String, value: Dictionary) -> bool:
	if DirAccess.make_dir_recursive_absolute(path.get_base_dir()) != OK:
		return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value, "  ", true) + "\n")
	return true


func _count_diagnostic(values: Array, code: String) -> int:
	var count := 0
	for value in values:
		if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("code", "")) == code:
			count += 1
	return count


func _has_diagnostic(values: Array, code: String) -> bool:
	for value in values:
		if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("code", "")) == code:
			return true
	return false


func _has_blocker(value: Variant, code: String) -> bool:
	if typeof(value) != TYPE_ARRAY:
		return false
	for item in value as Array:
		if typeof(item) == TYPE_DICTIONARY and String((item as Dictionary).get("code", "")) == code:
			return true
	return false


func _count_blocker(value: Variant, code: String) -> int:
	if typeof(value) != TYPE_ARRAY:
		return 0
	var count := 0
	for item in value as Array:
		if typeof(item) == TYPE_DICTIONARY and String((item as Dictionary).get("code", "")) == code:
			count += 1
	return count


func _check(label: String, condition: bool, detail := "") -> void:
	if condition:
		passed += 1
		print("PASS ", label)
	else:
		failed += 1
		push_error("FAIL %s %s" % [label, detail])


func _cleanup_fixture() -> void:
	if DirAccess.dir_exists_absolute(fixture_root):
		OS.move_to_trash(fixture_root)


func _watchdog_timeout() -> void:
	push_error("HUD APT runtime gate timed out")
	quit(1)


func _finish() -> void:
	_cleanup_fixture()
	print("HUD_APT_RUNTIME_SUMMARY passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
