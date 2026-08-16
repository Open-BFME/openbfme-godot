extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 37
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var document := _document("bfme2", 0.3, false)
	var rules := Adapter.ability_rules(document)
	_check("adapter_accepts_sealed_typed_contract", rules.size() == 2)
	var disguise := rules[0] as Dictionary if rules.size() == 2 else {}
	_check("adapter_preserves_exact_phase_times", int((disguise.get("effect", {}) as Dictionary).get("unpackTimeMs", -1)) == 1000 and int((disguise.get("effect", {}) as Dictionary).get("preparationTimeMs", -1)) == 1 and int((disguise.get("effect", {}) as Dictionary).get("persistentPrepTimeMs", -1)) == 250 and int((disguise.get("effect", {}) as Dictionary).get("packTimeMs", -1)) == 1000)
	_check("adapter_preserves_exact_templates", String((disguise.get("effect", {}) as Dictionary).get("ownerDisguiseTemplateId", "")) == "RohanEowynDisguised" and String((disguise.get("effect", {}) as Dictionary).get("hostileDisguiseTemplateId", "")) == "RohanRohirrimHorde")
	var tampered := document.duplicate(true)
	var tampered_ability := (((tampered["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)
	(tampered_ability["effect"] as Dictionary)["opacityTarget"] = 0.9
	_check("adapter_rejects_effect_prerequisite_drift", Adapter.ability_rules(tampered).is_empty())
	var missing := document.duplicate(true); (missing["registration"] as Dictionary).erase("specialDisguisePresentationPrerequisite")
	_check("adapter_rejects_missing_presentation_seal", Adapter.ability_rules(missing).is_empty())
	var bad_digest := document.duplicate(true)
	var bad_closure := (((bad_digest["registration"] as Dictionary)["specialDisguisePresentationPrerequisite"] as Dictionary)["closure"] as Dictionary)
	bad_closure["aggregateSha256"] = "not-a-digest"
	_check("adapter_rejects_malformed_presentation_digest", Adapter.ability_rules(bad_digest).is_empty())

	var sim := _sim(); sim._unit_ability_rules["RohanEowyn"] = rules
	var actor := sim.entities[1] as Dictionary
	actor["level"] = 2
	_check("authored_unlock_gate_blocks_early", String(sim.cast_ability(1, "Command_SpecialAbilityEowynDisguise", Vector2.ZERO, 0).get("reason", "")) == "level-required")
	actor["level"] = 3
	var cast := sim.cast_ability(1, "Command_SpecialAbilityEowynDisguise", Vector2.ZERO, 0)
	_check("cast_starts_unpack", bool(cast.get("ok", false)) and String(cast.get("phase", "")) == "unpacking")
	_check("force_mount_happens_before_presentation", bool(actor.get("mounted", false)) and String(actor.get("form", "")) == "mounted" and bool(_event_value(sim, "ability.special_disguise_presentation", "force_mounted")))
	_check("authoritative_identity_never_replaced", String(actor.get("unit_type", "")) == "RohanEowyn")
	_check("unpack_timer_exact", int((actor.get("special_disguise_channel", {}) as Dictionary).get("phase_end_tick", -1)) == 10)
	sim.tick_index = 5; sim._step_special_disguise(actor)
	_check("opacity_interpolates_linearly", is_equal_approx(sim.special_disguise_opacity(actor), 0.65))
	_check("not_disguised_during_unpack", not bool((actor.get("object_status", {}) as Dictionary).get("DISGUISED", false)))
	sim.tick_index = 10; sim._step_special_disguise(actor)
	_check("enters_one_tick_preparation", String((actor.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "preparation" and int((actor.get("special_disguise_channel", {}) as Dictionary).get("phase_end_tick", -1)) == 11)
	_check("opacity_reaches_authored_target", is_equal_approx(sim.special_disguise_opacity(actor), 0.3))
	sim.tick_index = 11; sim._step_special_disguise(actor)
	_check("single_trigger_enters_persistent_hold", String((actor.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "persistent-hold" and bool((actor.get("object_status", {}) as Dictionary).get("DISGUISED", false)))
	_check("trigger_presentation_requests_disguise_leaf", String(_event_value(sim, "ability.special_disguise_presentation", "template_id")) == "RohanEowynDisguised")
	var trigger_events := _event_count(sim, "ability.special_disguise_triggered")
	sim.tick_index = 100; sim._step_special_disguise(actor)
	_check("persistent_prep_does_not_retrigger", _event_count(sim, "ability.special_disguise_triggered") == trigger_events)
	_check("hold_keeps_target_opacity", is_equal_approx(sim.special_disguise_opacity(actor), 0.3))

	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("hold_snapshot_restores", restored.restore(snap))
	var restored_actor := restored.entities[1] as Dictionary
	_check("hold_hash_round_trips", restored.state_hash() == hash and is_equal_approx(restored.special_disguise_opacity(restored_actor), 0.3))
	var cancel: Dictionary = sim.cancel_special_disguise(1, "explicit", false)
	_check("explicit_cancel_starts_pack", bool(cancel.get("ok", false)) and String((actor.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "packing")
	_check("explicit_cancel_clears_disguised_immediately", not bool((actor.get("object_status", {}) as Dictionary).get("DISGUISED", false)))
	_check("explicit_cancel_emits_exit_fx", String(_event_value(sim, "ability.special_disguise_cancelled", "exit_fx_id")) == "FX_DisguiseExit")
	sim.tick_index = 105; sim._step_special_disguise(actor)
	_check("pack_opacity_interpolates", is_equal_approx(sim.special_disguise_opacity(actor), 0.65))
	sim.tick_index = 110; sim._step_special_disguise(actor)
	_check("pack_finishes_exactly", not actor.has("special_disguise_channel") and is_equal_approx(sim.special_disguise_opacity(actor), 1.0))
	_check("pack_requests_owner_presentation", String(_event_value(sim, "ability.special_disguise_presentation", "template_id")) == "RohanEowyn")

	var dismount := _sim(); dismount._unit_ability_rules["RohanEowyn"] = rules; var drow := dismount.entities[1] as Dictionary
	dismount.cast_ability(1, "Command_SpecialAbilityEowynDisguise", Vector2.ZERO, 0); dismount.tick_index = 10; dismount._step_special_disguise(drow); dismount.tick_index = 11; dismount._step_special_disguise(drow)
	var event_before := _event_count(dismount, "ability.special_disguise_cancelled")
	var unmount := dismount.cast_ability(1, "Command_ToggleMounted", Vector2.ZERO, 0)
	_check("dismount_cancels_disguise", bool(unmount.get("ok", false)) and not bool(drow.get("mounted", true)) and _event_count(dismount, "ability.special_disguise_cancelled") == event_before + 1)
	_check("dismount_suppresses_exit_fx", String(_event_value(dismount, "ability.special_disguise_cancelled", "exit_fx_id")) == "" and bool(_event_value(dismount, "ability.special_disguise_cancelled", "suppress_exit_fx")))
	_check("dismount_still_packs_opacity", String((drow.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "packing")
	var attack := _sim(); attack._unit_ability_rules["RohanEowyn"] = rules; var arow := attack.entities[1] as Dictionary
	attack.cast_ability(1, "Command_SpecialAbilityEowynDisguise", Vector2.ZERO, 0); attack.tick_index = 10; attack._step_special_disguise(arow); attack.tick_index = 11; attack._step_special_disguise(arow)
	var victim := arow.duplicate(true); victim["id"] = 2; victim["team"] = 1; victim["unit_type"] = "Victim"; attack.entities[2] = victim
	attack._apply_damage(1, 2, 1)
	_check("attack_cancels_disguise_with_pack", String((arow.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "packing" and not bool((arow.get("object_status", {}) as Dictionary).get("DISGUISED", false)))
	_check("attack_cancel_emits_exit_fx", String(_event_value(attack, "ability.special_disguise_cancelled", "reason")) == "attack" and String(_event_value(attack, "ability.special_disguise_cancelled", "exit_fx_id")) == "FX_DisguiseExit")

	var rotwk_doc := _document("rotwk", 0.3, false)
	_check("canonical_rotwk_201_shape_is_accepted", Adapter.ability_rules(rotwk_doc).size() == 2)
	_check("fanpatch_rider2_execution_fails_closed", Adapter.ability_rules(_document("rotwk", 0.9, true)).is_empty())

	var dead := _sim(); dead._unit_ability_rules["RohanEowyn"] = rules; dead.cast_ability(1, "Command_SpecialAbilityEowynDisguise", Vector2.ZERO, 0); var dead_row := dead.entities[1] as Dictionary; dead_row["health"] = 0; dead.tick_index = 10; dead._step_special_disguise(dead_row)
	_check("death_ordering_fails_closed", String(dead_row.get("special_disguise_deferred_boundary", "")) == "death-reset-ordering" and String((dead_row.get("special_disguise_channel", {}) as Dictionary).get("phase", "")) == "unpacking")
	_check("viewer_split_is_explicitly_deferred", (disguise.get("limitations", []) as Array).has("special-disguise-viewer-perspective-deferred"))
	_check("critical_and_user1_ordering_deferred", (disguise.get("limitations", []) as Array).has("special-disguise-critical-hit-ordering-deferred") and (disguise.get("limitations", []) as Array).has("special-disguise-user1-stealth-ordering-deferred"))

	if passed + failed != EXPECTED:
		failed += 1; push_error("SPECIAL_DISGUISE_RUNTIME_FAIL liveness")
	print("SPECIAL_DISGUISE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document(edition: String, opacity: float, rider2: bool) -> Dictionary:
	var effect := {
		"kind": "special-disguise", "specialPowerTemplateId": "SpecialAbilityDisguise",
		"targetMode": "SELF", "unpackTimeMs": 1000, "preparationTimeMs": 1,
		"persistentPrepTimeMs": 250, "packTimeMs": 1000,
		"opacityTarget": opacity, "ownerObjectId": "RohanEowyn",
		"ownerDisguiseTemplateId": "RohanEowynDisguised",
		"hostileDisguiseTemplateId": "RohanRohirrimHorde",
		"disguiseFxId": "FX_DisguiseExit", "forceMountedWhenDisguising": true,
		"deferredBoundaries": ["viewer-perspective", "death-reset-ordering", "critical-hit-ordering", "user1-stealth-ordering"],
	}
	if rider2:
		effect.merge({
			"triggerAttributeModifierId": "Rider2Tracker",
			"attributeModifierDurationMs": 2000,
			"triggerAttributeModifier": {"id": "Rider2Tracker"},
		})
	var fields := {
		"SpecialPowerTemplate": {"authored": "SpecialAbilityDisguise"},
		"UnpackTime": {"authored": "1000"}, "PreparationTime": {"authored": "1"},
		"PersistentPrepTime": {"authored": "250"}, "PackTime": {"authored": "1000"},
		"OpacityTarget": {"authored": str(opacity)},
		"DisguiseAsTemplate": {"authored": "RohanEowynDisguised"},
		"DisguisedAsTemplate_EnemyPerspective": {"authored": "RohanRohirrimHorde"},
		"DisguiseFX": {"authored": "FX_DisguiseExit"},
		"ForceMountedWhenDisguising": {"authored": "Yes"},
	}
	if rider2:
		fields.merge({"TriggerAttributeModifier": {"authored": "Rider2Tracker"}, "AttributeModifierDuration": {"authored": "2000"}})
	var closure := {
		"schema": "openbfme.special-disguise-presentation-prerequisite", "schemaVersion": 0,
		"edition": edition, "objectId": "RohanEowyn",
		"descriptorSha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
		"runtimeStatus": "sealed-deferred-no-runtime-activation", "presentationOnly": true,
		"authoritativeEntityRegistration": false,
		"moduleReceipt": {"kind": "SpecialDisguiseUpdate", "instanceTag": "ModuleTag_SpecialDisguiseUpdateUpdate", "fields": fields},
		"presentationIdentities": {"ownerObjectId": "RohanEowyn", "nonOwnerDisguiseTemplateId": "RohanEowynDisguised", "hostilePerspectiveTemplateId": "RohanRohirrimHorde"},
		"visualLeafBindings": [{"role": "owner-base-presentation"}, {"role": "owner-mounted-presentation"}, {"role": "owner-disguised-presentation"}, {"role": "non-owner-disguised-presentation"}],
		"aggregateSha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	}
	var limitations := ["special-disguise-viewer-perspective-deferred", "special-disguise-death-reset-ordering-deferred", "special-disguise-critical-hit-ordering-deferred", "special-disguise-user1-stealth-ordering-deferred"]
	var disguise_ability := {
		"id": "Command_SpecialAbilityEowynDisguise", "slot": 1,
		"specialPowerId": "SpecialAbilityDisguise", "targeting": "self", "cooldownMs": 0,
		"button": {"iconIds": ["HSEowynDisguise"], "labelIds": ["CONTROLBAR:EowynDisguise"], "tooltipIds": ["CONTROLBAR:ToolTipEowynDisguise"], "options": []},
		"effect": effect, "implementation": {"status": "implemented", "reason": "", "limitations": limitations},
		"levelGate": {"requiredLevel": 3},
	}
	var mount_ability := {
		"id": "Command_ToggleMounted", "slot": 2, "specialPowerId": "SpecialAbilityToggleMounted",
		"targeting": "self", "cooldownMs": 0,
		"button": {"iconIds": ["HSEowynMount"], "labelIds": ["CONTROLBAR:EowynMount"], "tooltipIds": ["CONTROLBAR:ToolTipEowynMount"], "options": []},
		"effect": {"kind": "mount-toggle", "mountedSpeed": 100, "mounted_speed_scaled": 10.0, "mountedWeaponModeKey": "", "mountedMemberHealth": 0, "dismountedMemberHealth": 0},
		"implementation": {"status": "implemented", "reason": "", "limitations": []}, "levelGate": {},
	}
	return {
		"objectId": "RohanEowyn", "descriptorSha256": closure["descriptorSha256"],
		"registration": {"stringBindings": {}, "specialDisguisePresentationPrerequisite": {"closure": closure}, "abilities": [disguise_ability, mount_ability]},
	}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rule := {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "hero", "speed": 1.0,
		"speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 10.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 10.0, "vision_range_source": 100.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0,
		"firing_duration_ticks": 0, "member_damage": 1, "member_health": 100,
		"member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {},
	}
	var unit_rules := {}; for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: unit_rules[oid] = unit_rule
	sim.setup({}, {"source_unit_scale":0.1,"unit_rules":unit_rules}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); sim.events.clear()
	sim.entities[1] = {"id":1,"team":0,"health":100,"unit_type":"RohanEowyn","level":3,"ability_states":{},"position":Vector2.ZERO,"destination":Vector2.ZERO,"route":[],"order_kind":"","state":"idle","target_id":0,"target_kind":"","attack_move":false,"kind_of":["HERO","INFANTRY"],"speed":1.0,"speed_source":10.0,"member_health":[100],"member_maximum_health":100,"member_count":1,"maximum_health":100}
	return sim


func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for event_value in sim.events: if String((event_value as Dictionary).get("kind", "")) == kind: count += 1
	return count


func _event_value(sim: RetailSliceSim, kind: String, key: String) -> Variant:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind: return event.get(key)
	return null


func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("SPECIAL_DISGUISE_RUNTIME_FAIL " + name)
