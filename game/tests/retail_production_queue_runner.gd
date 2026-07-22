extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var HudScript: Script

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"starting_resources": 1000,
		"command_point_cap": 1000,
		"member_health": 100,
		"unit_rules": _unit_rules(15, 40),
		"soldier_cost": 200,
		"soldier_build_ticks": 10,
		"soldier_command_points": 10,
		"tower_guard_cost": 300,
		"tower_guard_build_ticks": 20,
		"tower_guard_command_points": 20,
	})
	sim.ai_enabled = false
	var barracks: int = sim.producer_id(SimScript.PLAYER_TEAM, "barracks")
	var first: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, barracks, SimScript.SOLDIER_HORDE_ID)
	var second: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, barracks, SimScript.TOWER_GUARD_OBJECT_ID)
	_check("two_jobs_queue", bool(first.get("ok", false)) and bool(second.get("ok", false)))
	var initial := sim.production_queue_state(barracks)
	_check(
		"queue_has_deterministic_timing",
		initial.size() == 2
			and int(initial[0].get("start_tick", -1)) == 0
			and int(initial[0].get("complete_tick", -1)) == 10
			and int(initial[1].get("start_tick", -1)) == 10
			and int(initial[1].get("complete_tick", -1)) == 30,
		str(initial)
	)
	sim.advance(4)
	var progressing := sim.production_queue_state(barracks)
	_check(
		"active_job_reports_live_progress",
		progressing.size() == 2
			and bool(progressing[0].get("active", false))
			and is_equal_approx(float(progressing[0].get("progress", -1.0)), 0.4)
			and not bool(progressing[1].get("active", true)),
		str(progressing)
	)
	var cancelled: Dictionary = sim.cancel_queued_unit(SimScript.PLAYER_TEAM, barracks, 0)
	var reflowed := sim.production_queue_state(barracks)
	_check(
		"cancel_refunds_and_reflows",
		bool(cancelled.get("ok", false))
			and int(cancelled.get("refund", -1)) == 200
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == 700
			and reflowed.size() == 1
			and String(reflowed[0].get("unit_type", "")) == SimScript.TOWER_GUARD_OBJECT_ID
			and int(reflowed[0].get("start_tick", -1)) == 4
			and int(reflowed[0].get("complete_tick", -1)) == 24,
		str(reflowed)
	)
	_check("cancel_event_is_authoritative", _has_event(sim.events, "production.cancelled", barracks))
	sim.advance(19)
	_check("reflowed_job_does_not_complete_early", not sim.entities.has(10))
	sim.advance(1)
	var completed: Dictionary = sim.entity(10)
	var producer_position := Vector2(sim.structure(barracks).get("position", Vector2.ZERO))
	var producer_rally := Vector2(sim.structure(barracks).get("rally", producer_position))
	var door_point := Vector2(completed.get("position", Vector2.ZERO))
	var create_point := Vector2(completed.get("production_exit_destination", Vector2.ZERO))
	_check(
		"reflowed_job_completes_on_exact_tick",
		sim.entities.has(10)
			and String(completed.get("unit_type", "")) == SimScript.TOWER_GUARD_OBJECT_ID
			and sim.production_queue_state(barracks).is_empty()
	)
	_check(
		"completed_horde_appears_at_doorway_before_rally",
		String(completed.get("state", "")) == "run"
			and Vector2(completed.get("production_rally", Vector2.ZERO)).is_equal_approx(producer_rally)
			and is_equal_approx(door_point.distance_to(producer_position), SimScript.PRODUCTION_DOOR_INSET_RADIUS)
			and is_equal_approx(create_point.distance_to(producer_position), SimScript.PRODUCTION_EXIT_RADIUS)
			and is_zero_approx(float(completed.get("production_exit_progress", -1.0))),
		str(completed)
	)
	sim.advance(9)
	completed = sim.entity(10)
	_check(
		"doorway_exit_reveals_members_while_root_moves",
		is_equal_approx(float(completed.get("production_exit_progress", -1.0)), 0.5)
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(door_point) > 0.0
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(create_point) > 0.0,
		str(completed)
	)
	sim.advance(8)
	_check(
		"doorway_exit_approaches_final_emergence_tick",
		is_equal_approx(float(sim.entity(10).get("production_exit_progress", -1.0)), 17.0 / 18.0)
			and Vector2(sim.entity(10).get("position", Vector2.ZERO)).distance_to(create_point) < Vector2(sim.entity(10).get("position", Vector2.ZERO)).distance_to(door_point)
	)
	sim.advance(1)
	completed = sim.entity(10)
	_check(
		"completed_horde_then_advances_to_rally",
		is_equal_approx(float(completed.get("production_exit_progress", -1.0)), 1.0)
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(door_point) > 0.0,
		str(completed)
	)
	_check("completion_event_records_exit_route", _has_completed_exit_event(sim.events, barracks, 10))

	# Load the HUD only after autoload initialization. AssetFactory references the
	# ContentDB autoload and cannot safely form that dependency from the main
	# script's compile-time preload graph.
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	_check("hud_script_available", HudScript != null)
	if HudScript == null:
		print("RETAIL_PRODUCTION_QUEUE_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)
		return
	var hud = HudScript.new()
	root.add_child(hud)
	hud.build()
	var queue_fixture: Array[Dictionary] = [{
		"index": 0,
		"unit_type": SimScript.SOLDIER_HORDE_ID,
		"progress": 0.42,
		"active": true,
	}]
	hud.set_production_state([SimScript.SOLDIER_HORDE_ID], true, 1, queue_fixture)
	_check(
		"hud_exposes_active_job",
		hud.production_queue_label.visible
			and hud.production_queue_label.text.contains("42%")
			and hud.production_progress.visible
			and is_equal_approx(hud.production_progress.value, 42.0)
			and hud.cancel_production_button.visible
			and not hud.cancel_production_button.disabled
	)
	var cancel_requests: Array[int] = []
	hud.cancel_production_requested.connect(func(queue_index: int) -> void: cancel_requests.append(queue_index))
	hud.cancel_production_button.pressed.emit()
	_check("hud_cancel_targets_active_job", cancel_requests == [0], str(cancel_requests))
	# Fortress hero recruits queue through the same production surface, but their
	# icons live on the hero roster buttons — the queue chip must fall back to
	# the hero set and still sweep the CCW dial + countdown (retail training
	# timer on every producer, heroes included). The chip chrome is parity
	# chrome; build it directly here (no pack bind in this runner).
	hud._ensure_production_queue_chips()
	var hero_image := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	hero_image.fill(Color(0.4, 0.6, 0.9))
	var hero_icon := ImageTexture.create_from_image(hero_image)
	var hero_roster_button := Button.new()
	hero_roster_button.icon = hero_icon
	# Parent the fixture button under the HUD so hud.free() releases it; an
	# orphan Button leaks itself plus its default theme resources at exit.
	hud.add_child(hero_roster_button)
	hud.hero_buttons["gondorboromir"] = hero_roster_button
	var hero_queue_fixture: Array[Dictionary] = [{
		"index": 0,
		"unit_type": "gondorboromir",
		"progress": 0.5,
		"active": true,
		"remaining_seconds": 12.0,
	}]
	hud.set_production_state(["gondorboromir"], true, 1, hero_queue_fixture)
	var hero_chip: Button = hud.production_queue_buttons[0]
	_check(
		"hero_recruit_queue_chip_binds_roster_icon",
		hero_chip.visible and hero_chip.icon == hero_icon,
		"visible=%s icon_match=%s" % [str(hero_chip.visible), str(hero_chip.icon == hero_icon)]
	)
	var hero_dial := hero_chip.get_node_or_null("TrainingDial") as TextureProgressBar
	var hero_countdown := hero_chip.get_node_or_null("TrainingCountdown") as Label
	_check(
		"hero_recruit_queue_chip_sweeps_ccw_dial_and_countdown",
		hero_dial != null and hero_dial.visible
			and hero_dial.fill_mode == TextureProgressBar.FILL_COUNTER_CLOCKWISE
			and is_equal_approx(float(hero_dial.value), 0.5)
			and hero_countdown != null and hero_countdown.visible
			and hero_countdown.text == "12s",
		"dial=%s countdown=%s" % [str(hero_dial), hero_countdown.text if hero_countdown != null else "null"]
	)
	hud.set_production_state([SimScript.SOLDIER_HORDE_ID], true, 0, [])
	_check(
		"hud_reports_ready_queue",
		hud.production_queue_label.visible
			and hud.production_queue_label.text == "Production queue ready"
			and not hud.production_progress.visible
			and not hud.cancel_production_button.visible
	)
	# Battalion OBJECT_UPGRADE purchase surface: compiled rows render as socket
	# buttons with honest doc-derived text when the pack has no icon/strings
	# (never a raw id), the NeededUpgrade tech gate locks+greys, and a queued
	# purchase sweeps the CCW dial with its live countdown.
	var upgrade_commands: Array[Dictionary] = [{
		"upgrade_id": "Upgrade_GondorBasicTraining",
		"command_id": "Command_PurchaseUpgradeGondorBasicTraining",
		"cost": 300,
		"duration_ticks": 45,
		"slot": 6,
		"label_id": "",
		"tooltip_id": "",
		"image_id": "",
		"research_owned": false,
		"required_upgrade": "Upgrade_GondorBasicTrainingTech",
		"applied": false,
		"queued": false,
	}]
	hud.set_battalion_upgrade_state(upgrade_commands, [])
	var purchase_button: Button = hud._battalion_upgrade_buttons.get("Upgrade_GondorBasicTraining")
	_check(
		"battalion_upgrade_button_renders_honest_text_not_raw_id",
		purchase_button != null
			and purchase_button.visible
			and not purchase_button.text.contains("CONTROLBAR:")
			and not purchase_button.text.contains("Upgrade_"),
		"text=%s" % (purchase_button.text if purchase_button != null else "null")
	)
	_check(
		"battalion_upgrade_button_locks_until_tech_owned",
		purchase_button != null
			and purchase_button.disabled
			and purchase_button.self_modulate.is_equal_approx(Color(0.45, 0.45, 0.5))
			and purchase_button.tooltip_text.contains("Cost: 300")
			and purchase_button.tooltip_text.contains("Requires "),
		"tooltip=%s" % (purchase_button.tooltip_text if purchase_button != null else "null")
	)
	# Tech owned → purchasable; queued → CCW dial + countdown, disabled.
	upgrade_commands[0]["research_owned"] = true
	var upgrade_queue_rows: Array[Dictionary] = [{
		"upgrade_id": "Upgrade_GondorBasicTraining",
		"duration_ticks": 45,
		"elapsed_ticks": 9,
		"progress": 0.2,
		"remaining_seconds": 36.0,
	}]
	hud.set_battalion_upgrade_state(upgrade_commands, upgrade_queue_rows)
	# Iconless honest-text rows carry progress as the live countdown only (the
	# dial sweeps the icon texture — none exists here, fail-closed no invented
	# art); pack-bound rows get the full CCW sweep (verified right after).
	var purchase_dial := purchase_button.get_node_or_null("TrainingDial") as TextureProgressBar
	var purchase_countdown := purchase_button.get_node_or_null("TrainingCountdown") as Label
	_check(
		"battalion_upgrade_queued_counts_down_on_iconless_button",
		purchase_dial != null and not purchase_dial.visible
			and purchase_countdown != null and purchase_countdown.visible
			and purchase_countdown.text == "36s"
			and purchase_button.disabled,
		"dial_visible=%s countdown=%s" % [
			str(purchase_dial.visible) if purchase_dial != null else "null",
			purchase_countdown.text if purchase_countdown != null else "null",
		]
	)
	# With a bound icon the same row sweeps the CCW dial (pack buttons ship
	# buttonImageId — BGArcheryRange_FireArrows / BRArmory_* etc.).
	var purchase_icon_image := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	purchase_icon_image.fill(Color(0.8, 0.5, 0.2))
	purchase_button.icon = ImageTexture.create_from_image(purchase_icon_image)
	hud.set_battalion_upgrade_state(upgrade_commands, upgrade_queue_rows)
	_check(
		"battalion_upgrade_queued_sweeps_dial_when_icon_bound",
		purchase_dial != null and purchase_dial.visible
			and purchase_dial.fill_mode == TextureProgressBar.FILL_COUNTER_CLOCKWISE
			and is_equal_approx(float(purchase_dial.value), 0.8)
			and purchase_countdown != null and purchase_countdown.visible
			and purchase_countdown.text == "36s"
			and purchase_button.disabled,
		"dial=%s countdown=%s" % [
			str(purchase_dial.value) if purchase_dial != null else "null",
			purchase_countdown.text if purchase_countdown != null else "null",
		]
	)
	# Applied rows leave the surface entirely (retail removes purchased buttons).
	upgrade_commands[0]["applied"] = true
	hud.set_battalion_upgrade_state(upgrade_commands, [])
	_check(
		"battalion_upgrade_applied_leaves_surface",
		not purchase_button.visible,
		"visible=%s" % str(purchase_button.visible)
	)
	var purchase_requests: Array[String] = []
	hud.battalion_upgrade_requested.connect(func(upgrade_id: String) -> void: purchase_requests.append(upgrade_id))
	upgrade_commands[0]["applied"] = false
	hud.set_battalion_upgrade_state(upgrade_commands, [])
	purchase_button.pressed.emit()
	_check(
		"battalion_upgrade_button_emits_purchase_request",
		purchase_requests == ["Upgrade_GondorBasicTraining"],
		str(purchase_requests)
	)
	hud.free()

	print("RETAIL_PRODUCTION_QUEUE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _unit_rules(member_count: int, member_damage: int) -> Dictionary:
	return {
		SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, member_count, member_damage, 1.15, 17.5),
		SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, member_count, member_damage, 30.0, 37.0),
		SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, member_count, member_damage, 3.5, 17.5),
		SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, member_count, member_damage, 1.15, 27.5),
	}


func _unit_rule(horde_id: String, member_count: int, member_damage: int, attack_range: float, vision_range: float) -> Dictionary:
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": attack_range,
		"attack_range_source": attack_range * 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": vision_range,
		"vision_range_source": vision_range * 10.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": member_damage,
		"member_count": member_count,
		"formation_positions": positions,
		"provenance": {},
	}


func _has_event(events: Array, kind: String, entity_id: int) -> bool:
	for event_value in events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			return true
	return false


func _has_completed_exit_event(events: Array, producer: int, battalion: int) -> bool:
	for event_value in events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		var event: Dictionary = event_value
		if (
			String(event.get("kind", "")) == "production.exit_complete"
			and int(event.get("entity_id", 0)) == producer
			and int(event.get("target_id", 0)) == battalion
		):
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_PRODUCTION_QUEUE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_PRODUCTION_QUEUE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
