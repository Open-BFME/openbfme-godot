extends SceneTree
## Structure radial command-set gate (lane kimi-bug-structure-ui).
##
## THE BUG IT PINS. The owner (v0.2.2, Men): "The icons above buildings, when
## you click on them, don't have the radial dial wheel anymore." Screenshots
## showed a selected structure's palantir with black/empty sockets. The compiled
## command set every Men structure carries - its retail CommandSet rows - ends
## in `Command_Sell` (slot 6): commandset.ini:5771 SellableCommandSet is the
## farm/well/statue's WHOLE set, and every production structure's direct set
## (GondorBarracksCommandSet, GondorArcheryCommandSet, GondorForgeCommandSet,
## GondorWorkshopCommandSet, MenFortressCommandSet) carries it too. The radial
## surfaced trains/upgrades/research/expansions but never the sell row, so a
## farm - whose authored set IS that one row - showed a completely empty wheel.
##
## EXTERNAL ORACLE, re-derived here, never typed in: the mounted pack's compiled
## playable-structure documents (`registration.gameplay.trainedCommandSets`),
## cross-cited to the pure RotWK retail tree
## (.private/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * commandset.ini:5771-5773  SellableCommandSet  6 = Command_Sell
##   * commandset.ini GondorBarracksCommandSet slots 1-4 + 6 = Command_Sell
##   * commandbutton.ini:3554-3562  Command_Sell (SELL, BCSell, InPalantir Yes)
##   * gamedata.ini:8973  SellPercentage = 50%
##   * lotr.str:14228 CONTROLBAR:SellBuilding "Demolish Building"
##
## Sections
##   1. Fortress radial carries its compiled sell row plus porter/selectors,
##      and the upgrades page still matches the authored improvements + back.
##   2. BUILT barracks / archery range / blacksmith (forge) radial ids equal
##      each compiled direct command set.
##   3. A BUILT farm's radial is exactly its compiled set: Command_Sell alone.
##   4. Clicking the sell button sells through the lockstep command codec:
##      structure razed, 50% refund (gamedata.ini:8973), selection cleared.
##   5. Unit palantir unchanged: the porter keeps stop + stance, no radial.
##
## Run:
##   OPENBFME_CONTENT=<repo>/.private/content-packs godot --headless --path game \
##     --script res://tests/structure_radial_command_set_runner.gd

const BOOT_DEADLINE_MS := 300000
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0
var _slice = null
var _sections_completed: Array[String] = []
var _last_construct_detail := ""
## Liveness pin: an aborted coroutine must exit non-zero, so the expected check
## count is asserted at finish. Keep in sync with the _check call sites below.
const EXPECTED_CHECKS := 27


func _initialize() -> void:
	_runner_watchdog.start(self, "STRUCTURE_RADIAL_COMMAND_SET_RUNNER")
	for env_name in ["OPENBFME_SLICE_MAP", "OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("slice_scene_loads", slice_scene != null):
		return _finish()
	_slice = slice_scene.instantiate()
	root.add_child(_slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(_slice.ready_ok) or String(_slice.failure_reason) != "":
			break
	if not _check("slice_boots", bool(_slice.ready_ok), String(_slice.failure_reason)):
		return _finish()
	var sim = _slice.simulation

	# ------------------------------------------------------------------
	# Section 0: the compiled oracle itself (pack rows, not transcriptions).
	# ------------------------------------------------------------------
	var content_db := root.get_node("ContentDB")
	var farm_doc: Dictionary = content_db.get_playable_structure_runtime("GondorFarm")
	var farm_sets: Array = ((farm_doc.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("trainedCommandSets", []) as Array
	var farm_direct := _direct_command_set(farm_sets)
	_check(
		"compiled_farm_command_set_is_sell_only",
		farm_direct.size() == 1 and String((farm_direct[0] as Dictionary).get("commandId", "")) == "Command_Sell",
		"commandset.ini:5771 SellableCommandSet -> %s" % str(farm_direct)
	)
	var barracks_doc: Dictionary = content_db.get_playable_structure_runtime("GondorBarracks")
	var barracks_direct := _direct_command_set(((barracks_doc.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("trainedCommandSets", []) as Array)
	var barracks_compiled_ids := _command_ids_of(barracks_direct)
	_check(
		"compiled_barracks_command_set_has_five_rows",
		barracks_direct.size() == 5,
		"GondorBarracksCommandSet (3 trains + level2 + sell) -> %s" % str(barracks_compiled_ids)
	)
	var archery_compiled_ids := _compiled_direct_ids_for_kind("archery_range")
	_check(
		"compiled_archery_command_set_has_five_rows",
		archery_compiled_ids.size() == 5 and archery_compiled_ids.has("Command_Sell"),
		"GondorArcheryCommandSet (2 trains + fire arrows + level2 + sell) -> %s" % str(_sorted(archery_compiled_ids))
	)
	var forge_compiled_ids := _compiled_direct_ids_for_kind("forge")
	_check(
		"compiled_forge_command_set_has_five_rows",
		forge_compiled_ids.size() == 5 and forge_compiled_ids.has("Command_Sell"),
		"GondorForgeCommandSet (3 techs + level2 + sell) -> %s" % str(_sorted(forge_compiled_ids))
	)
	_section_completed("oracle")

	# ------------------------------------------------------------------
	# Section 1: fortress wheel (map-seeded citadel) + upgrades page.
	# ------------------------------------------------------------------
	var fortress := int(sim.fortress_id(0))
	await _select_structure(fortress)
	var fortress_ids := _radial_ids()
	_check("fortress_radial_has_sell", fortress_ids.has("Command_Sell"), str(fortress_ids))
	_check("fortress_radial_has_porter", fortress_ids.has("bfme2.object.men-porter"), str(fortress_ids))
	_check(
		"fortress_radial_has_both_page_selectors",
		fortress_ids.has("upgrades") and fortress_ids.has("heroes"),
		str(fortress_ids)
	)
	_check(
		"fortress_dish_caption_is_level_1",
		_slice.hud.dish_level_caption() == "Level: 1",
		"got '%s'" % _slice.hud.dish_level_caption()
	)
	# Upgrades page: authored improvements (slots 8-13) + RadialBack.
	_slice.hud.set_radial_page(_slice.hud.RADIAL_PAGE_UPGRADES)
	_slice._sync_presentation()
	for i in 4:
		await process_frame
	var upgrade_entries: Array = _slice.hud.radial_entries()
	var upgrade_kinds := {}
	var upgrade_count := 0
	for entry_value in upgrade_entries:
		var entry: Dictionary = entry_value
		upgrade_kinds[String(entry.get("command_kind", ""))] = true
		if String(entry.get("command_kind", "")) == "upgrade":
			upgrade_count += 1
	_check(
		"fortress_upgrades_page_has_six_improvements_and_back",
		upgrade_count == 6 and upgrade_kinds.has("back"),
		"MenFortressCommandSet slots 8-13 + RadialBack -> %s" % str(upgrade_entries.map(func(e: Dictionary) -> String: return "%s:%s" % [String(e.get("command_kind", "")), String(e.get("id", ""))]))
	)
	_slice.hud.set_radial_page(_slice.hud.RADIAL_PAGE_MAIN)
	_section_completed("fortress")

	# ------------------------------------------------------------------
	# Sections 2-4: built structures through the real construction path.
	# ------------------------------------------------------------------
	var porter := _porter_id()
	_check("porter_found", porter != 0)
	if porter != 0:
		sim.team_resources[0] = 100000
		var fortress_pos := Vector2(sim.structure(fortress).get("position", Vector2.ZERO))

		# --- Section 2: barracks / archery / forge radials == compiled sets ----
		var barracks := await _build_structure(porter, "barracks", fortress_pos, [Vector2(18, 0), Vector2(-18, 0), Vector2(0, 18), Vector2(0, -18)])
		_check("barracks_built", barracks != 0)
		if barracks != 0:
			await _select_structure(barracks)
			var barracks_wheel := _radial_command_ids()
			_check(
				"barracks_radial_ids_match_compiled_command_set",
				barracks_wheel == barracks_compiled_ids,
				"wheel=%s compiled=%s" % [str(_sorted(barracks_wheel)), str(_sorted(barracks_compiled_ids))]
			)
		_section_completed("barracks")

		var archery := await _build_structure(porter, "archery_range", fortress_pos, [Vector2(-18, 0), Vector2(18, 0), Vector2(14, 14), Vector2(-14, -14)])
		_check("archery_built", archery != 0)
		if archery != 0:
			await _select_structure(archery)
			var archery_wheel := _radial_command_ids()
			_check(
				"archery_radial_ids_match_compiled_command_set",
				archery_wheel == archery_compiled_ids,
				"kind=%s wheel=%s compiled=%s" % [
					String(sim.structure(archery).get("structure_kind", "")),
					str(_sorted(archery_wheel)),
					str(_sorted(archery_compiled_ids)),
				]
			)
		_section_completed("archery")

		var forge := await _build_structure(porter, "forge", fortress_pos, [
			Vector2(0, -18), Vector2(14, -14), Vector2(-14, 14), Vector2(22, 8),
			Vector2(-22, 8), Vector2(8, 22), Vector2(-8, -22), Vector2(26, 0),
		])
		_check(
			"forge_built",
			forge != 0,
			"build_rules_has_forge=%s last=%s" % [
				str((sim.structure_build_rules_for_team(0) as Dictionary).has("forge")),
				_last_construct_detail,
			]
		)
		if forge != 0:
			await _select_structure(forge)
			var forge_wheel := _radial_command_ids()
			_check(
				"forge_radial_ids_match_compiled_command_set",
				forge_wheel == forge_compiled_ids,
				"wheel=%s compiled=%s" % [str(_sorted(forge_wheel)), str(_sorted(forge_compiled_ids))]
			)
		_section_completed("forge")

		# --- Sections 3+4: farm wheel is sell-only, and the sale executes ------
		var farm := await _build_structure(porter, "farm", fortress_pos, [Vector2(0, 18), Vector2(-18, 0), Vector2(0, -18), Vector2(-14, -14)])
		_check("farm_built", farm != 0)
		if farm != 0:
			await _select_structure(farm)
			var farm_wheel := _radial_command_ids()
			_check(
				"farm_radial_is_exactly_command_sell",
				farm_wheel == {"Command_Sell": true},
				"wheel=%s" % str(_radial_ids())
			)
			var farm_cost := int((sim.structure_build_rules_for_team(0).get("farm", {}) as Dictionary).get("cost", 0))
			_check("farm_build_cost_is_compiled", farm_cost == 300, "gamedata.ini GONDOR_FARM build cost -> %d" % farm_cost)
			var resources_before: int = sim.resources_for_team(0)
			var sell_button := _find_sell_button()
			var button_names: Array = []
			for button in _slice.hud._radial_buttons:
				button_names.append(String(button.name))
			_check("farm_sell_button_present", sell_button != null, "buttons=%s entries=%s" % [str(button_names), str(_radial_ids())])
			if sell_button != null:
				sell_button.pressed.emit()
				for i in 4:
					await process_frame
				var sold: Dictionary = sim.structure(farm)
				_check(
					"farm_sold_razes_the_structure",
					sold.is_empty() or int(sold.get("health", 1)) <= 0,
					"health=%s" % str(sold.get("health", "<gone>"))
				)
				# gamedata.ini:8973 SellPercentage = 50%.
				_check(
					"farm_sale_refunds_fifty_percent",
					sim.resources_for_team(0) == resources_before + int(farm_cost * 0.5),
					"before=%d after=%d expected+%d" % [resources_before, sim.resources_for_team(0), int(farm_cost * 0.5)]
				)
				_check(
					"farm_sale_clears_the_wheel",
					_slice.selected_structure_id == 0 or _slice.hud.radial_command_count() == 0,
					"selected_structure_id=%d" % _slice.selected_structure_id
				)
		_section_completed("farm")
		_section_completed("sell")

	# ------------------------------------------------------------------
	# Section 5: unit palantir unchanged (porter keeps stop + stance).
	# ------------------------------------------------------------------
	if porter != 0:
		_slice.selected_structure_id = 0
		sim.select_only(porter)
		_slice._sync_presentation()
		for i in 6:
			await process_frame
		var visible_actions: Array[String] = []
		for action_id in _slice.hud.unit_action_buttons.keys():
			if (_slice.hud.unit_action_buttons[action_id] as Button).visible:
				visible_actions.append(String(action_id))
		visible_actions.sort()
		_check(
			"porter_palantir_unchanged_stop_and_stance",
			visible_actions == ["stance", "stop"],
			str(visible_actions)
		)
		_check("porter_selection_hides_the_radial", not _slice.hud._radial_layer.visible)
	_section_completed("unit")

	_finish()


# --------------------------------------------------------------- helpers --


func _direct_command_set(sets: Array) -> Array:
	for set_value in sets:
		var row: Dictionary = set_value
		if String(row.get("kind", "")) == "direct":
			return row.get("slots", []) as Array
	return (sets[0] as Dictionary).get("slots", []) as Array if not sets.is_empty() else []


func _command_ids_of(slots: Array) -> Dictionary:
	var ids := {}
	for slot_value in slots:
		ids[String((slot_value as Dictionary).get("commandId", ""))] = true
	return ids


func _compiled_direct_ids_for_kind(kind: String) -> Dictionary:
	## Resolve the kind through the team's authored source-object aliases so a
	## fortress / citadel split (or a forge slug vs objectId) still hits the
	## document that actually carries trainedCommandSets.
	var aliases: Variant = _slice.simulation.structure_source_object_ids_for_team(0).get(kind, [])
	var object_ids: Array = []
	if typeof(aliases) == TYPE_ARRAY:
		object_ids = aliases
	elif typeof(aliases) in [TYPE_STRING, TYPE_STRING_NAME]:
		object_ids = [aliases]
	var content_db := root.get_node("ContentDB")
	for object_id_value in object_ids:
		var document: Dictionary = content_db.get_playable_structure_runtime(String(object_id_value))
		var sets: Array = ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("trainedCommandSets", []) as Array
		var direct := _direct_command_set(sets)
		if not direct.is_empty():
			return _command_ids_of(direct)
	return {}


func _sorted(ids: Dictionary) -> Array:
	var out := ids.keys()
	out.sort()
	return out


func _porter_id() -> int:
	for id in _slice.simulation.entities:
		var row: Dictionary = _slice.simulation.entities[id]
		if int(row.get("team", -1)) == 0 and bool(row.get("is_builder", false)) and int(row.get("health", 0)) > 0:
			return int(id)
	return 0


func _build_structure(porter: int, kind: String, anchor: Vector2, offsets: Array) -> int:
	var sim = _slice.simulation
	_last_construct_detail = "no-attempt"
	for offset_value in offsets:
		var offset := offset_value as Vector2
		var result: Dictionary = sim._issue_construct_for_team(0, [porter] as Array[int], kind, anchor + offset)
		_last_construct_detail = "%s@%s -> %s" % [kind, str(offset), str(result)]
		if not bool(result.get("ok", false)):
			continue
		var structure_id := int(result.get("structure_id", 0))
		var waited := 0
		while waited < 2400 and float(sim.structure(structure_id).get("construction_progress", 0.0)) < 1.0:
			_slice.step_for_test(150)
			waited += 150
		if float(sim.structure(structure_id).get("construction_progress", 0.0)) >= 1.0:
			return structure_id
		_last_construct_detail = "%s id=%d progress=%s after %d ticks" % [
			kind, structure_id, str(sim.structure(structure_id).get("construction_progress", -1)), waited,
		]
		return 0
	return 0


func _select_structure(structure_id: int) -> void:
	_slice.simulation.clear_selection()
	_slice.selected_structure_id = structure_id
	var row: Dictionary = _slice.simulation.structure(structure_id)
	_slice.camera_focus = Vector2(row.get("position", Vector2.ZERO))
	_slice._sync_presentation()
	for i in 8:
		await process_frame


func _radial_ids() -> Array:
	var ids: Array = []
	for entry_value in _slice.hud.radial_entries():
		ids.append(String((entry_value as Dictionary).get("id", "")))
	return ids


func _radial_command_ids() -> Dictionary:
	## The wheel's command vocabulary: trains map through the sim's compiled
	## production rule (its command_id), upgrades through the compiled contract,
	## sell/page/back by their own ids.
	var ids := {}
	var sim = _slice.simulation
	var production_rules: Dictionary = sim.unit_production_rules_for_team(0)
	for entry_value in _slice.hud.radial_entries():
		var entry: Dictionary = entry_value
		var kind := String(entry.get("command_kind", ""))
		var id := String(entry.get("id", ""))
		match kind:
			"train", "hero":
				var rule: Dictionary = production_rules.get(id, {}) as Dictionary
				ids[String(rule.get("command_id", id))] = true
			"upgrade":
				ids[String(entry.get("command_id", id))] = true
			"page", "back":
				continue
			_:
				ids[id] = true
	return ids


func _find_sell_button() -> Button:
	## Match the sell socket by entry data, not button.name. sync_radial_commands
	## queue_frees the previous wheel in the same frame it rebuilds, so a second
	## Command_Sell socket (farm after archery) is often renamed @Button@N.
	var buttons: Array = _slice.hud._radial_buttons
	var entries: Array = _slice.hud.radial_entries()
	for i in buttons.size():
		var entry: Dictionary = entries[i] if i < entries.size() else {}
		if String(entry.get("command_kind", "")) == "sell" or String(entry.get("id", "")) == "Command_Sell":
			return buttons[i] as Button
		if String((buttons[i] as Button).name).to_lower().contains("sell"):
			return buttons[i] as Button
	return null


func _section_completed(name: String) -> void:
	if not _sections_completed.has(name):
		_sections_completed.append(name)


func _check(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		passed += 1
		print("STRUCTURE_RADIAL PASS %s" % name)
	else:
		failed += 1
		printerr("STRUCTURE_RADIAL FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	return condition


func _finish() -> void:
	var checks_run := passed + failed
	if checks_run != EXPECTED_CHECKS:
		failed += 1
		printerr("STRUCTURE_RADIAL FAIL liveness: expected %d checks, ran %d (sections completed: %s)" % [
			EXPECTED_CHECKS, checks_run, str(_sections_completed)
		])
	print("STRUCTURE_RADIAL_COMMAND_SET_RESULT passed=%d failed=%d sections=%s" % [passed, failed, str(_sections_completed)])
	quit(0 if failed == 0 else 1)
