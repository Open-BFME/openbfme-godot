extends SceneTree
## Focused four-unit retail audio routing audit. This runner never writes or
## copies retail payloads and exercises the enabled Godot playback path.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_FOUR_UNIT_AUDIO_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish([])
		return
	content_db.reload()
	var soldier_definition: Dictionary = content_db.get_bundle_object(AudioScript.SOLDIER_OBJECT_ID)
	var selected_pack_root := String(soldier_definition.get("_pack_root", ""))
	var external_root := OS.get_environment("OPENBFME_CONTENT")
	_check("selected_private_pack_root_available", selected_pack_root != "" and external_root != "" and mod_loader.path_is_within(external_root, selected_pack_root), selected_pack_root)
	if selected_pack_root == "":
		_finish([])
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	var compatibility_ready: bool = audio.configure(selected_pack_root, true)
	var production_default_route: Dictionary = audio.route_audio_event("ArrowDrawBow", 1)
	var production_default_observability_ok: bool = (
		not audio.observability_enabled
		and bool(production_default_route.get("ok", false))
		and String(audio.last_route_result.get("event_id", "")) == "ArrowDrawBow"
		and audio.intent_log.is_empty()
		and audio.routing_log.is_empty()
	)
	audio.observability_enabled = true
	_check("legacy_soldier_music_compatibility_and_production_observability_ready", compatibility_ready and production_default_observability_ok)
	_check("retail_playback_is_enabled", audio.playback_enabled)
	_check("non_spatial_players_use_real_godot_players", audio.music_player is AudioStreamPlayer and audio.voice_player is AudioStreamPlayer and audio.sfx_player is AudioStreamPlayer)
	_check("strict_four_unit_roster_audio_ready", audio.has_complete_roster_audio_closure())
	_check("legacy_select_leaf_count_preserved", audio.count_voice_kind("select") == 10, str(audio.count_voice_kind("select")))
	_check("soldier_attack_event_has_exact_six_retail_leaves", audio.count_voice_kind("attack") == 6, str(audio.count_voice_kind("attack")))

	for state in ["explore", "battle", "victory", "defeat"]:
		_check("music_%s_loaded" % state, audio.music_streams.has(state))
	var music_events: Array[Dictionary] = [
		{"sequence": 1, "kind": "music.explore", "entity_id": 0, "target_id": 0},
		{"sequence": 2, "kind": "music.battle", "entity_id": 0, "target_id": 0},
		{"sequence": 3, "kind": "music.victory", "entity_id": 0, "target_id": 0},
		{"sequence": 4, "kind": "music.defeat", "entity_id": 0, "target_id": 0},
	]
	audio.sync_events(music_events)
	_check("music_state_machine_intact", audio.current_music_state == "defeat", audio.current_music_state)
	_check("enabled_music_player_uses_declared_defeat_stream", audio.music_player.stream == audio.music_streams.get("defeat") and audio.music_player.playing)

	for object_id in AudioScript.ROSTER_OBJECT_IDS:
		var expected_by_kind: Dictionary = AudioScript.ROSTER_VOICE_EVENT_IDS[object_id]
		for kind in AudioScript.REQUIRED_VOICE_KINDS:
			var routed: Dictionary = audio.route_roster_voice(object_id, kind, 1)
			var expected_event := String(Array(expected_by_kind[kind])[0])
			var label := "%s_%s" % [_short_id(object_id), kind]
			_check(
				"%s_routes_exact_event" % label,
				bool(routed.get("ok", false))
				and String(routed.get("event_id", "")) == expected_event
				and String(routed.get("object_id", "")) == object_id
				and String(routed.get("kind", "")) == kind,
				str(routed)
			)
			_check("%s_uses_content_db_v1" % label, String(routed.get("source", "")) == "content-db-v1", str(routed))
			_check("%s_is_contained_header_valid_private_wav" % label, _is_valid_private_wav_route(routed, mod_loader, external_root), str(routed))
			var bound_by_kind: Dictionary = audio.roster_voice_routes.get(object_id, {})
			var route_definition: Dictionary = bound_by_kind.get(kind, {})
			var total_weight := _route_total_weight(route_definition)
			var repeated: Dictionary = audio.route_roster_voice(object_id, kind, 1)
			var wrapped: Dictionary = audio.route_roster_voice(object_id, kind, total_weight + 1)
			_check(
				"%s_variation_is_deterministic" % label,
				total_weight > 0
				and _route_signature(routed) == _route_signature(repeated)
				and _route_signature(routed) == _route_signature(wrapped),
				"weight=%d first=%s repeated=%s wrapped=%s" % [total_weight, _route_signature(routed), _route_signature(repeated), _route_signature(wrapped)]
			)

	var diagnostics: Array[String] = audio.readiness_diagnostics()
	_check("strict_readiness_has_zero_diagnostics", diagnostics.is_empty(), str(diagnostics))
	var ambient_diagnostics: Array[String] = audio.fords_ambient_readiness_diagnostics()
	_check("fords_ambient_map_contract_declares_exact_50_placements", audio.ambient_contract_declared, str(ambient_diagnostics))
	if audio.ambient_emitters.is_empty():
		_check("missing_ambient_pack_closure_fails_visibly", not ambient_diagnostics.is_empty() and _diagnostics_have_prefix(ambient_diagnostics, "missing-event:"), str(ambient_diagnostics))
	else:
		_check("available_ambient_streams_create_spatial_players", _valid_spatial_ambient_players(audio.ambient_emitters, mod_loader, external_root), str(ambient_diagnostics))
	var unsupported_semantics: Array[String] = audio._ambient_parameter_gaps("FocusedAmbient", {
		"parameters": [
			{"field": "Control", "value": "loop"},
			{"field": "Priority", "value": "lowest"},
			{"field": "Limit", "value": "2"},
			{"field": "PitchShift", "value": "-5 5"},
			{"field": "MinRange", "value": "300"},
			{"field": "MaxRange", "value": "800"},
			{"field": "Type", "value": "world everyone"},
			{"field": "SubmixSlider", "value": "Ambient"},
		]
	})
	_check("unsupported_range_loop_submix_priority_pitch_semantics_fail_visibly", _diagnostics_have_prefix(unsupported_semantics, "unsupported-attenuation-curve:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-loop-scheduler:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-submixslider:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-priority:") and _diagnostics_have_prefix(unsupported_semantics, "unsupported-pitchshift:"), str(unsupported_semantics))

	var soldier_select_1: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 1)
	var soldier_select_2: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 2)
	var soldier_select_11: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "select", 11)
	_check("soldier_select_variation_advances", String(soldier_select_1.get("path", "")) != String(soldier_select_2.get("path", "")))
	_check("soldier_select_variation_wraps_deterministically", String(soldier_select_1.get("path", "")) == String(soldier_select_11.get("path", "")))
	var soldier_attack_1: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "attack", 1)
	var soldier_attack_7: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "attack", 7)
	_check("soldier_attack_variation_wraps_deterministically", String(soldier_attack_1.get("path", "")) == String(soldier_attack_7.get("path", "")))

	for event_id in AudioScript.REQUIRED_SFX_EVENT_IDS:
		var routed_sfx: Dictionary = audio.route_audio_event(event_id, 1)
		_check("sfx_%s_routes_to_private_leaf" % event_id.to_snake_case(), bool(routed_sfx.get("ok", false)) and mod_loader.path_is_within(external_root, String(routed_sfx.get("path", ""))), str(routed_sfx))
	var bow_1: Dictionary = audio.route_audio_event("ArrowDrawBow", 1)
	var bow_2: Dictionary = audio.route_audio_event("ArrowDrawBow", 2)
	var bow_11: Dictionary = audio.route_audio_event("ArrowDrawBow", 11)
	_check("sfx_variation_advances", String(bow_1.get("path", "")) != String(bow_2.get("path", "")))
	_check("sfx_variation_wraps_deterministically", String(bow_1.get("path", "")) == String(bow_11.get("path", "")))

	var missing: Dictionary = audio.route_audio_event("DefinitelyMissingRetailEvent", 9)
	_check("missing_event_rejected_without_fallback", not bool(missing.get("ok", true)) and String(missing.get("reason", "")) == "missing_event")
	var unknown_roster: Dictionary = audio.route_roster_voice("bfme2.object.not-a-men-unit", "select", 1)
	_check("unknown_roster_object_fails_closed", not bool(unknown_roster.get("ok", true)) and String(unknown_roster.get("reason", "")) == "unknown_roster_object")
	var unknown_kind: Dictionary = audio.route_roster_voice(AudioScript.SOLDIER_OBJECT_ID, "retreat", 1)
	_check("unknown_voice_kind_fails_closed", not bool(unknown_kind.get("ok", true)) and String(unknown_kind.get("reason", "")) == "unknown_voice_kind")
	var valid_path := String(bow_1.get("path", ""))
	audio.audio_event_routes["corrupt.test"] = {
		"event_id": "Corrupt.Test",
		"source": "focused-runner",
		"leaves": [{"sample_id": "broken", "path": valid_path, "stream": null, "weight": 1}],
	}
	var corrupt: Dictionary = audio.route_audio_event("Corrupt.Test", 1)
	_check("corrupt_event_rejected_before_playback", not bool(corrupt.get("ok", true)) and String(corrupt.get("reason", "")) == "corrupt_event")
	audio.audio_event_routes.erase("corrupt.test")

	var intent_events: Array[Dictionary] = music_events.duplicate(true)
	intent_events.append_array([
		{"sequence": 5, "kind": "voice.select", "entity_id": 2, "target_id": 0, "object_id": AudioScript.ARCHER_OBJECT_ID},
		{"sequence": 6, "kind": "order.move", "entity_id": 102, "target_id": 0, "object_id": AudioScript.TOWER_GUARD_OBJECT_ID},
		{"sequence": 7, "kind": "voice.attack", "entity_id": 103, "target_id": 101, "object_id": AudioScript.KNIGHT_OBJECT_ID},
		{"sequence": 8, "kind": "combat.swing", "entity_id": 2, "target_id": 101, "object_id": AudioScript.ARCHER_OBJECT_ID},
		{"sequence": 9, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 2001},
		{"sequence": 10, "kind": "battalion.defeated", "entity_id": 1, "target_id": 103, "object_id": AudioScript.KNIGHT_OBJECT_ID},
		{"sequence": 11, "kind": "structure.destroyed", "entity_id": 1, "target_id": 2001},
		{"sequence": 12, "kind": "production.complete", "entity_id": 1003, "target_id": 10, "object_id": AudioScript.KNIGHT_OBJECT_ID},
		{"sequence": 13, "kind": "voice.select", "entity_id": 10, "target_id": 0},
		{"sequence": 14, "kind": "battalion.defeated", "entity_id": 101, "target_id": 10, "object_id": AudioScript.KNIGHT_OBJECT_ID},
	])
	audio._next_event_index = 4
	var horse_impacts_before_intents := _routing_log_count(audio.routing_log, "ImpactHorse", true)
	audio.sync_events(intent_events)
	var routed_horse_impacts := _routing_log_count(audio.routing_log, "ImpactHorse", true) - horse_impacts_before_intents
	_check("archer_swing_routes_bow_sfx", _routing_log_has(audio.routing_log, "ArrowDrawBow", true))
	_check("building_hit_routes_stone_sfx", _routing_log_has(audio.routing_log, "BuildingLightDamageStone", true))
	_check("knight_defeat_routes_horse_impact", routed_horse_impacts == 2 and not audio._entity_object_ids.has(103), "routed_impacts=%d pinned_103=%s" % [routed_horse_impacts, str(audio._entity_object_ids.has(103))])
	_check("structure_destroy_routes_heavy_stone_sfx", _routing_log_has(audio.routing_log, "BuildingHeavyDamageStone", true))
	_check("structure_destroy_routes_sink_sfx", _routing_log_has(audio.routing_log, "BuildingSink", true))
	_check("dynamic_production_tracks_exact_object_id", _routing_log_has(audio.routing_log, "GondorKnightVoiceSelectMS", true) and not audio._entity_object_ids.has(10))
	_check("enabled_voice_and_sfx_players_received_real_pack_streams", audio.voice_player.playing and audio.voice_player.stream is AudioStreamWAV and audio.sfx_player.playing and audio.sfx_player.stream is AudioStreamWAV)

	# S1: every entity resolves its own object from the event payload, never a
	# static roster pin. The men starting porter (absent from the retired table)
	# and the starting hero (pinned to the soldier before) route their own sets.
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 15, "kind": "voice.select", "entity_id": 3, "target_id": 0, "object_id": "bfme2.object.men-porter"},
		{"sequence": 16, "kind": "voice.select", "entity_id": 1, "target_id": 0, "object_id": "bfme2.object.gondor-aragorn-mp"},
		{"sequence": 17, "kind": "unit.summoned", "entity_id": 11, "target_id": 0, "object_id": "bfme2.object.gondor-trebuchet"},
		{"sequence": 18, "kind": "voice.move", "entity_id": 11, "target_id": 0},
	])
	_check("starting_porter_routes_own_select_set", _routing_log_has(audio.routing_log, "MenBuilderVoiceSelectMS", true))
	_check("starting_hero_routes_own_select_set", _routing_log_has(audio.routing_log, "AragornVoiceSelectMS", true))
	_check("summoned_unit_registers_and_moves_own_set", _routing_log_has(audio.routing_log, "TrebuchetVoiceMove", true))

	# S2: structure selects, UI clicks, and the construction loop build their
	# ContentDB routes lazily instead of rejecting missing_event.
	var farm_select: Dictionary = audio.play_structure_select("farm")
	_check("farm_select_routes_gondor_farm_select", bool(farm_select.get("ok", false)) and String(farm_select.get("event_id", "")) == "GondorFarmSelect", str(farm_select))
	_check("farm_select_plays_authored_leaf", String(farm_select.get("path", "")).to_lower().ends_with("gbgofar_selecta.wav"), str(farm_select.get("path", "")))
	var palantir_click: Dictionary = audio.play_ui_event("Gui_PalantirButtonClick")
	_check("palantir_click_routes", bool(palantir_click.get("ok", false)) and String(palantir_click.get("path", "")).to_lower().ends_with("uclick_jewel.wav"), str(palantir_click))
	var choose_power_click: Dictionary = audio.play_ui_event("Gui_PalantirChoosePowerClick")
	_check("choose_power_click_routes", bool(choose_power_click.get("ok", false)), str(choose_power_click))
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 19, "kind": "construction.started", "entity_id": 3, "target_id": 3000, "object_id": "bfme2.object.men-porter"},
		{"sequence": 20, "kind": "power.purchased", "entity_id": 0, "target_id": 0, "power_id": "SpellBookHeal"},
	])
	_check("power_purchase_plays_choose_power_click", _routing_log_has(audio.routing_log, "Gui_PalantirChoosePowerClick", true))
	_check("construction_start_routes_builder_voice_and_loop", _routing_log_has(audio.routing_log, "MenBuilderVoiceBuild", true) and _routing_log_has(audio.routing_log, "BuildingConstructionLoop", true))

	# S4: attack acks split by target class; siege fires its authored launch
	# sound; cavalry hits land the horse impact.
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 20, "kind": "voice.attack", "entity_id": 103, "target_id": 2001, "object_id": AudioScript.KNIGHT_OBJECT_ID, "target_kind": "structure"},
		{"sequence": 21, "kind": "voice.attack", "entity_id": 103, "target_id": 101, "object_id": AudioScript.KNIGHT_OBJECT_ID, "target_kind": "battalion"},
		{"sequence": 22, "kind": "combat.swing", "entity_id": 11, "target_id": 2001, "object_id": "bfme2.object.gondor-trebuchet"},
		{"sequence": 23, "kind": "combat.hit", "entity_id": 1, "target_id": 103, "target_object_id": AudioScript.KNIGHT_OBJECT_ID},
	])
	_check("knight_attack_structure_ack_is_target_classed", _routing_log_has(audio.routing_log, "GondorKnightVoiceAttackBuilding", true))
	_check("knight_attack_unit_ack_stays_generic", _routing_log_has(audio.routing_log, "GondorKnightVoiceAttack", true))
	_check("trebuchet_swing_routes_launch_voice", _routing_log_has(audio.routing_log, "TrebuchetLaunchVoice", true))
	_check("cavalry_hit_routes_horse_impact", _routing_log_count(audio.routing_log, "ImpactHorse", true) >= 3)

	# #24: alt-form heroes route their mounted set when the event carries the
	# form; the base form stays the default.
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 24, "kind": "voice.select", "entity_id": 12, "target_id": 0, "object_id": "bfme2.object.rohan-theoden"},
		{"sequence": 25, "kind": "voice.select", "entity_id": 12, "target_id": 0, "object_id": "bfme2.object.rohan-theoden", "form": "mounted"},
	])
	_check("theoden_base_form_routes_unmounted_select", _routing_log_has(audio.routing_log, "TheodenVoiceSelectMS", true))
	_check("theoden_mounted_form_routes_mounted_select", _routing_log_has(audio.routing_log, "TheodenVoiceSelectMountedMS", true))

	# #23: every fallen horde member lands its own class bodyfall; a machine
	# never borrows the human leaf (its authored die is its death voice).
	audio._next_event_index = 0
	var bodyfall_soldier_before := _routing_log_count(audio.routing_log, "BodyFallSoldier", true)
	var trebuchet_die_before := _routing_log_count(audio.routing_log, "TrebuchetDie", true)
	audio.sync_events([
		{"sequence": 26, "kind": "battalion.member_defeated", "entity_id": 101, "target_id": 2, "object_id": AudioScript.ARCHER_OBJECT_ID, "member_index": 3},
		{"sequence": 27, "kind": "battalion.member_defeated", "entity_id": 101, "target_id": 2, "object_id": AudioScript.ARCHER_OBJECT_ID, "member_index": 7},
		{"sequence": 28, "kind": "battalion.member_defeated", "entity_id": 1, "target_id": 11, "object_id": "bfme2.object.gondor-trebuchet", "member_index": 0},
		{"sequence": 29, "kind": "battalion.defeated", "entity_id": 1, "target_id": 11, "object_id": "bfme2.object.gondor-trebuchet"},
	])
	var bodyfall_soldier_delta := _routing_log_count(audio.routing_log, "BodyFallSoldier", true) - bodyfall_soldier_before
	_check("archer_members_land_per_member_bodyfalls", bodyfall_soldier_delta == 2, str(bodyfall_soldier_delta))
	_check("trebuchet_never_plays_human_bodyfall", bodyfall_soldier_delta == 2, str(bodyfall_soldier_delta))
	_check("trebuchet_defeat_routes_authored_die", _routing_log_count(audio.routing_log, "TrebuchetDie", true) - trebuchet_die_before == 1, str(_routing_log_count(audio.routing_log, "TrebuchetDie", true) - trebuchet_die_before))

	# S3: structure lifecycle audio comes from the structure doc's converted
	# evidence: damage sounds fire on ENTERING the doc's damaged bands (never
	# per hit), collapse accompanies destruction.
	var audio2 = AudioScript.new()
	root.add_child(audio2)
	audio2.observability_enabled = true
	audio2.configure(selected_pack_root, true, {}, {
		"select": {"farm": "GondorFarmSelect"},
		"damaged": {"farm": "BuildingLightDamageWood"},
		"really_damaged": {"farm": "BuildingHeavyDamageWood"},
		"damaged_fraction": {"farm": 0.6665},
		"really_damaged_fraction": {"farm": 0.3335},
	})
	audio2.sync_events([
		{"sequence": 1, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 3000, "structure_kind": "farm", "health": 1500, "maximum_health": 2000},
		{"sequence": 2, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 3000, "structure_kind": "farm", "health": 1200, "maximum_health": 2000},
		{"sequence": 3, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 3000, "structure_kind": "farm", "health": 1100, "maximum_health": 2000},
		{"sequence": 4, "kind": "combat.hit_structure", "entity_id": 1, "target_id": 3000, "structure_kind": "farm", "health": 600, "maximum_health": 2000},
		{"sequence": 5, "kind": "structure.destroyed", "entity_id": 1, "target_id": 3000, "structure_kind": "farm"},
	])
	_check("farm_damaged_band_plays_doc_wood_exactly_once", _routing_log_count(audio2.routing_log, "BuildingLightDamageWood", true) == 1, str(_routing_log_count(audio2.routing_log, "BuildingLightDamageWood", true)))
	_check("farm_really_damaged_band_plays_doc_heavy_wood", _routing_log_count(audio2.routing_log, "BuildingHeavyDamageWood", true) >= 1, str(_routing_log_count(audio2.routing_log, "BuildingHeavyDamageWood", true)))
	_check("farm_destroy_routes_sink_sfx", _routing_log_has(audio2.routing_log, "BuildingSink", true))
	var farm_doc_select: Dictionary = audio2.play_structure_select("farm")
	_check("farm_select_prefers_doc_contract", bool(farm_doc_select.get("ok", false)) and String(farm_doc_select.get("event_id", "")) == "GondorFarmSelect", str(farm_doc_select))

	# EVA: with no cooked eva side map and no Camp* samples mounted, the
	# announcer fails closed to silence (never a substitute sound).
	audio2._next_event_index = 0
	audio2.sync_events([
		{"sequence": 6, "kind": "eva.base_under_attack", "entity_id": 0, "target_id": 3000, "structure_kind": "farm"},
		{"sequence": 7, "kind": "eva.enemy_defeated", "entity_id": 0, "target_id": 0},
	])
	_check("eva_without_cooked_content_fails_closed", _routing_log_count(audio2.routing_log, "CampSoldierUnderAttackResource", true) == 0 and _routing_log_count(audio2.routing_log, "CampSoldierDieEnemy", true) == 0)

	audio2.dispose()
	audio2.free()

	# S5 + EVA (non-men): a recooked faction pack ships a faction-specific v1
	# audio registry and its Camp* announcer sets; the eva side map itself is
	# global retail data the runtime resolves from ANY mounted pack (the
	# men-eva overlay ships the identical map). When the mounted faction pack
	# carries only a host-registry copy (republish pending), faction Camp*
	# routing must fail closed — recorded rejection, never a substitute — and
	# the gap is reported as a readiness diagnostic instead of being hidden.
	var elves_pack_root := ""
	for pack_root_value in content_db.pack_roots:
		if String(pack_root_value).contains("bfme2-elves-vslice"):
			elves_pack_root = String(pack_root_value)
	_check("elves_pack_mounted_for_audio", elves_pack_root != "", elves_pack_root)
	var elves_registry_present := false
	var elves_eva_map: Dictionary = {}
	if elves_pack_root != "":
		var elves_audio_path := String(mod_loader.call("resolve_pack_path", elves_pack_root, "data/audio_events.json"))
		var elves_audio_doc: Variant = _read_json_quiet(elves_audio_path)
		elves_registry_present = (
			typeof(elves_audio_doc) == TYPE_DICTIONARY
			and String((elves_audio_doc as Dictionary).get("schema", "")) == "openbfme.audio-events"
			and int((elves_audio_doc as Dictionary).get("schemaVersion", -1)) == 1
		)
	for pack_root_value in content_db.pack_roots:
		if not elves_eva_map.is_empty():
			break
		var eva_doc: Variant = _read_json_quiet(String(mod_loader.call("resolve_pack_path", String(pack_root_value), "data/eva_events.json")))
		if typeof(eva_doc) == TYPE_DICTIONARY and String((eva_doc as Dictionary).get("schema", "")) == "openbfme.eva-events":
			elves_eva_map = (eva_doc as Dictionary).get("events", {}) as Dictionary
	_check("elves_pack_ships_v1_audio_registry", elves_registry_present)
	_check("eva_side_map_resolves_from_mounted_packs", not elves_eva_map.is_empty() and String((elves_eva_map.get("UnderAttackResource", {}) as Dictionary).get("Elves", "")) == "CampElfUnderAttackResource")
	var elves_faction_audio_mounted: bool = not content_db.get_retail_audio_event("ElfBarracksSelect").is_empty() and not content_db.get_retail_audio_event("CampElfUnderAttackCamp").is_empty()
	if elves_registry_present and not elves_eva_map.is_empty():
		var elves_scoped: Dictionary = {}
		for unit_id in content_db.get_playable_unit_runtimes().keys():
			var document: Dictionary = content_db.get_playable_unit_runtime(String(unit_id))
			if String(document.get("_pack_root", "")) == elves_pack_root:
				elves_scoped[String(unit_id)] = document
		var audio3 = AudioScript.new()
		root.add_child(audio3)
		audio3.observability_enabled = true
		audio3.configure(selected_pack_root, true, elves_scoped, {
			"select": {"barracks": "ElfBarracksSelect"},
			"eva_damaged": {"barracks": "StructureUnderAttack"},
			"eva_events": elves_eva_map,
		}, "Elves")
		audio3.sync_events([
			{"sequence": 1, "kind": "voice.select", "entity_id": 1, "target_id": 0, "object_id": "bfme2.object.elven-arwen"},
			{"sequence": 2, "kind": "construction.started", "entity_id": 3, "target_id": 3000, "object_id": "bfme2.object.elven-porter"},
			{"sequence": 3, "kind": "eva.base_under_attack", "entity_id": 0, "target_id": 3000, "structure_kind": "barracks"},
			{"sequence": 4, "kind": "eva.enemy_defeated", "entity_id": 0, "target_id": 0},
			{"sequence": 5, "kind": "construction.completed", "entity_id": 3, "target_id": 3000, "structure_kind": "barracks", "team": 0},
		])
		_check("elves_starting_arwen_routes_own_select", _routing_log_has(audio3.routing_log, "ArwenVoiceSelectMS", true))
		_check("elves_porter_routes_own_build_voice", _routing_log_has(audio3.routing_log, "ElfBuilderVoiceBuild", true))
		var elven_barracks_select: Dictionary = audio3.play_structure_select("barracks")
		if elves_faction_audio_mounted:
			_check("elves_structure_select_routes_elven_event", bool(elven_barracks_select.get("ok", false)) and String(elven_barracks_select.get("event_id", "")) == "ElfBarracksSelect", str(elven_barracks_select))
			_check("elves_base_under_attack_plays_camp_elf", _routing_log_has(audio3.routing_log, "CampElfUnderAttackCamp", true))
			_check("elves_enemy_defeated_plays_camp_elf", _routing_log_has(audio3.routing_log, "CampElfDieEnemy", true))
			_check("elves_construction_complete_plays_elf_sting", _routing_log_has(audio3.routing_log, "ElfBuilderVoiceCompleteGeneric", true))
		else:
			_check("elves_structure_select_fails_closed_without_faction_content", not bool(elven_barracks_select.get("ok", true)) and String(elven_barracks_select.get("reason", "")) != "", str(elven_barracks_select))
			_check("elves_camp_events_fail_closed_without_faction_content", _routing_log_count(audio3.routing_log, "CampElfUnderAttackCamp", true) == 0 and _routing_log_count(audio3.routing_log, "CampElfDieEnemy", true) == 0)
			# Flat registries merge across packs: an event another pack's nested
			# closure legitimately carries may still resolve (never a substitute —
			# only the exact side-map-named event may play).
			_check("elves_construction_sting_never_substitutes", _routing_log_has(audio3.routing_log, "ElfBuilderVoiceCompleteGeneric", true) or _routing_log_count(audio3.routing_log, "ElfBuilderVoiceCompleteGeneric", false) >= 1)
			if not diagnostics.has("faction-audio-republish-pending:elves"):
				diagnostics.append("faction-audio-republish-pending:elves")
		audio3.dispose()
		audio3.free()

	# EVA (men): the men-eva overlay ships the CampSoldier* sets; a men-side
	# contract routes base-under-attack, defeat, and construction-complete
	# announcements for the active men match.
	var men_eva_map: Dictionary = {}
	for pack_root_value in content_db.pack_roots:
		var root := String(pack_root_value)
		if not root.contains("bfme2-men-eva-overlay"):
			continue
		var eva_doc: Variant = _read_json_quiet(String(mod_loader.call("resolve_pack_path", root, "data/eva_events.json")))
		if typeof(eva_doc) == TYPE_DICTIONARY and String((eva_doc as Dictionary).get("schema", "")) == "openbfme.eva-events":
			men_eva_map = (eva_doc as Dictionary).get("events", {}) as Dictionary
	_check("men_eva_overlay_ships_side_map", not men_eva_map.is_empty())
	if not men_eva_map.is_empty():
		var audio4 = AudioScript.new()
		root.add_child(audio4)
		audio4.observability_enabled = true
		audio4.configure(selected_pack_root, true, {}, {
			"eva_damaged": {"farm": "UnderAttackResource"},
			"eva_events": men_eva_map,
		}, "Men")
		audio4.sync_events([
			{"sequence": 1, "kind": "eva.base_under_attack", "entity_id": 0, "target_id": 3000, "structure_kind": "farm"},
			{"sequence": 2, "kind": "eva.enemy_defeated", "entity_id": 0, "target_id": 0},
			{"sequence": 3, "kind": "eva.ally_defeated", "entity_id": 0, "target_id": 0},
			{"sequence": 4, "kind": "construction.completed", "entity_id": 3, "target_id": 3000, "structure_kind": "farm", "team": 0},
		])
		_check("men_base_under_attack_plays_camp_soldier", _routing_log_has(audio4.routing_log, "CampSoldierUnderAttackResource", true))
		_check("men_enemy_defeated_plays_camp_soldier", _routing_log_has(audio4.routing_log, "CampSoldierDieEnemy", true))
		_check("men_ally_defeated_plays_camp_soldier", _routing_log_has(audio4.routing_log, "CampSoldierAllyDefeated", true))
		_check("men_construction_complete_plays_sting", _routing_log_has(audio4.routing_log, "MenBuilderVoiceCompleteGeneric", true))
		audio4.dispose()
		audio4.free()

	audio.dispose()
	audio.free()
	# Give the audio server a bounded teardown window after this function returns:
	# routed-result dictionaries release their streams immediately, while active
	# playback objects are retired by the audio thread before leak accounting.
	create_timer(0.5, true, false, true).timeout.connect(_finish.bind(diagnostics))


func _read_json_quiet(path: String) -> Variant:
	if path == "" or not FileAccess.file_exists(path):
		return null
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var parser := JSON.new()
	if parser.parse(file.get_as_text()) != OK:
		return null
	return parser.data


func _short_id(object_id: String) -> String:
	return object_id.trim_prefix("bfme2.object.").replace("-", "_")


func _is_valid_private_wav_route(route: Dictionary, mod_loader: Node, external_root: String) -> bool:
	var path := String(route.get("path", ""))
	var stream: Variant = route.get("stream")
	if (
		not bool(route.get("ok", false))
		or external_root == ""
		or not mod_loader.call("path_is_within", external_root, path)
		or not path.to_lower().ends_with(".wav")
		or not FileAccess.file_exists(path)
		or not (stream is AudioStreamWAV)
	):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() < 44:
		return false
	var riff := file.get_buffer(4).get_string_from_ascii()
	file.seek(8)
	var wave := file.get_buffer(4).get_string_from_ascii()
	return riff == "RIFF" and wave == "WAVE" and (stream as AudioStreamWAV).get_data().size() > 0


func _route_total_weight(route: Dictionary) -> int:
	var total := 0
	for leaf_value in Array(route.get("leaves", [])):
		if typeof(leaf_value) == TYPE_DICTIONARY:
			total += maxi(0, int((leaf_value as Dictionary).get("weight", 0)))
	return total


func _route_signature(route: Dictionary) -> String:
	return "%s|%s|%d" % [
		String(route.get("sample_id", "")),
		String(route.get("path", "")),
		int(route.get("variation_index", -1)),
	]


func _routing_log_has(log: Array[Dictionary], event_id: String, expected_ok: bool) -> bool:
	for row in log:
		if String(row.get("event_id", "")) == event_id and bool(row.get("ok", false)) == expected_ok:
			return true
	return false


func _routing_log_count(log: Array[Dictionary], event_id: String, expected_ok: bool) -> int:
	var count := 0
	for row in log:
		if String(row.get("event_id", "")) == event_id and bool(row.get("ok", false)) == expected_ok:
			count += 1
	return count


func _diagnostics_have_prefix(diagnostics: Array[String], prefix: String) -> bool:
	for diagnostic in diagnostics:
		if diagnostic.begins_with(prefix):
			return true
	return false


func _valid_spatial_ambient_players(emitters: Array[Dictionary], mod_loader: Node, external_root: String) -> bool:
	if emitters.size() != AudioScript.FORDS_AMBIENT_PLACEMENT_COUNT:
		return false
	for emitter in emitters:
		var player: Variant = emitter.get("player")
		var path := String(emitter.get("path", ""))
		if not (player is AudioStreamPlayer3D) or not mod_loader.path_is_within(external_root, path) or not FileAccess.file_exists(path):
			return false
		if (player as AudioStreamPlayer3D).stream == null or (player as AudioStreamPlayer3D).position != emitter.get("position"):
			return false
	return true


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish(diagnostics: Array[String]) -> void:
	for diagnostic in diagnostics:
		print("AUDIO_READINESS_GAP %s" % diagnostic)
	print("RETAIL_FOUR_UNIT_AUDIO_RESULT passed=%d failed=%d missing=%d" % [passed, failed, diagnostics.size()])
	quit(0 if failed == 0 else 1)
