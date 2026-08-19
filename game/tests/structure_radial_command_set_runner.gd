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
## (workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * commandset.ini:5771-5773  SellableCommandSet  6 = Command_Sell
##   * commandset.ini GondorBarracksCommandSet slots 1-4 + 6 = Command_Sell
##   * commandbutton.ini:3554-3562  Command_Sell (SELL, BCSell, InPalantir Yes)
##   * gamedata.ini:8973  SellPercentage = 50%
##   * lotr.str:14228 CONTROLBAR:SellBuilding "Demolish Building"
##
## Sections
##   1. Fortress radial carries its compiled sell row plus porter/selectors,
##      and the upgrades page still matches the authored improvements + back.
##   2. BUILT barracks / archery range / blacksmith (forge) radial SOCKETS
##      equal each compiled direct command set (authored holes preserved).
##   3. A BUILT farm's radial is Command_Sell in palantir socket 6, with the
##      five empty dishes left visible (commandset.ini:5772).
##   4. An UNDER-CONSTRUCTION farm still exposes SELL (farm.ini:34 has no
##      construction override); demolish refunds 50% of the already-paid cost.
##   5. The sell tooltip is CONTROLBAR:SellBuilding / ToolTipSellBuilding
##      ("Demolish Building" / "Demolish") and a Refund, never Cost: -<n>.
##   6. Clicking the sell button sells through the lockstep command codec:
##      structure razed, 50% refund (gamedata.ini:8973), selection cleared.
##   7. Unit palantir unchanged: the porter keeps stop + stance, no radial.
##
## Run:
##   OPENBFME_CONTENT=<repo>/workspace/content-packs godot --headless --path game \
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
## 2026-08-19 UI PARITY lane: +7 — fortress main page is exactly the authored
## MenFortressCommandSet (no expansion entries), no selection-arc node on the
## selected structure, no ring on unclicked plots, world-ring tooltips, and
## the authored-complete revivables page incl. the Create-a-Hero slot.
const EXPECTED_CHECKS := 39


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
	var farm_compiled_slots := _command_slots_of(farm_direct)
	_check(
		"compiled_farm_command_set_is_sell_only",
		farm_compiled_slots == {6: "Command_Sell"},
		"commandset.ini:5772 SellableCommandSet 6=Command_Sell -> %s" % _format_slots(farm_compiled_slots)
	)
	var barracks_doc: Dictionary = content_db.get_playable_structure_runtime("GondorBarracks")
	var barracks_direct := _direct_command_set(((barracks_doc.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("trainedCommandSets", []) as Array)
	var barracks_compiled_slots := _command_slots_of(barracks_direct)
	_check(
		"compiled_barracks_command_set_has_five_rows",
		barracks_direct.size() == 5 and String(barracks_compiled_slots.get(6, "")) == "Command_Sell" and not barracks_compiled_slots.has(5),
		"GondorBarracksCommandSet slots 1-4 + 6 (hole at 5) -> %s" % _format_slots(barracks_compiled_slots)
	)
	var archery_compiled_slots := _compiled_direct_slots_for_kind("archery_range")
	_check(
		"compiled_archery_command_set_has_five_rows",
		archery_compiled_slots.size() == 5 and String(archery_compiled_slots.get(6, "")) == "Command_Sell" and not archery_compiled_slots.has(5),
		"GondorArcheryCommandSet (2 trains + fire arrows + level2 + sell) -> %s" % _format_slots(archery_compiled_slots)
	)
	var forge_compiled_slots := _compiled_direct_slots_for_kind("forge")
	_check(
		"compiled_forge_command_set_has_five_rows",
		forge_compiled_slots.size() == 5 and String(forge_compiled_slots.get(6, "")) == "Command_Sell" and not forge_compiled_slots.has(5),
		"GondorForgeCommandSet (3 techs + level2 + sell) -> %s" % _format_slots(forge_compiled_slots)
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
	# UI PARITY lane (2026-08-19, owner playtest 2026-08-18): the authored
	# MenFortressCommandSet (commandset.ini:4055-4082) carries NO expansion
	# commands — the side-building/expansion options belong to the
	# MenFortressExpansionPad{Corner,Side}CommandSet objects (commandset.ini
	# :4091-4101) the player clicks. The fortress wheel must be exactly
	# porter + Command_Sell + the two page selectors.
	var fortress_kinds: Array[String] = []
	for entry_value in _slice.hud.radial_entries():
		fortress_kinds.append(String((entry_value as Dictionary).get("command_kind", "")))
	_check(
		"fortress_radial_has_no_expansion_entries",
		not fortress_kinds.has("expansion"),
		"kinds=%s ids=%s" % [str(fortress_kinds), str(fortress_ids)]
	)
	fortress_kinds.sort()
	_check(
		"fortress_main_page_is_exactly_the_authored_set",
		fortress_kinds == ["page", "page", "sell", "train"],
		"commandset.ini:4055 slots 1/2/5/6 -> %s" % str(fortress_kinds)
	)
	# Retail draws NO arc/ring on a selected structure (selection = the health
	# bar above it; REF-25/33/35 vs ours-fortress-radial-heroes-green-arc).
	var fortress_node: Node = _slice.structure_nodes.get(fortress)
	_check(
		"selected_structure_has_no_selection_arc_node",
		fortress_node != null and fortress_node.get_node_or_null("SelectionRing") == null,
		"node=%s" % str(fortress_node)
	)
	# The expansion PLOTS are flat pads flush with the ground (REF-33): no ring
	# on any plot while none is clicked.
	var rings_visible: Array[String] = []
	for fortress_id_value in _slice._expansion_pad_markers.keys():
		for marker in (_slice._expansion_pad_markers[fortress_id_value] as Array):
			if (marker as Node3D).visible:
				rings_visible.append(str((marker as Node3D).name))
	_check(
		"expansion_plots_show_no_ring_when_unclicked",
		rings_visible.is_empty(),
		str(rings_visible)
	)
	# Every command button shows retail's hover tooltip (REF-25): the WORLD
	# ring buttons mirror their palantir twin's tooltip metadata and register
	# the hover path.
	var world_tooltip_gaps: Array[String] = []
	for world_button in _slice.hud.world_radial_buttons():
		if String((world_button as Button).get_meta("tooltip_group", "")) == "" \
				or not (world_button as Button).has_meta("tooltip_registered"):
			world_tooltip_gaps.append(String((world_button as Button).name))
	_check(
		"world_radial_buttons_carry_retail_tooltips",
		world_tooltip_gaps.is_empty(),
		str(world_tooltip_gaps)
	)
	# Heroes page == the authored revivables (commandset.ini:4069-4076 slots
	# 15-24: ring hero, Create-a-Hero, seven generic revive slots): every hero
	# the fortress roster offers must reach the page — the owner's "not all
	# heroes + my custom hero are available". The Men roster is the seven
	# faction heroes plus whatever created heroes the local store fields (the
	# headless scratch store is empty BY DESIGN — cah_heroes.gd
	# PROFILE_DIR_HEADLESS_SUFFIX — so the CAH half of this proof lives where
	# a profile is seeded: fortress_command_surface_runner section 6, and the
	# windowed ui_parity_capture_runner, which shows both local CAH profiles).
	_slice.hud.set_radial_page(_slice.hud.RADIAL_PAGE_HEROES)
	_slice._sync_presentation()
	for i in 4:
		await process_frame
	var hero_page_ids: Array[String] = []
	for entry_value in _slice.hud.radial_entries():
		var hero_entry: Dictionary = entry_value
		if String(hero_entry.get("command_kind", "")) == "hero":
			hero_page_ids.append(String(hero_entry.get("id", "")))
	var offered_heroes: Array[String] = []
	for unit_id_value in Array((sim.structure(fortress) as Dictionary).get("production", [])):
		var unit_id := String(unit_id_value)
		if unit_id != "bfme2.object.men-porter":
			offered_heroes.append(unit_id)
	hero_page_ids.sort()
	offered_heroes.sort()
	_check(
		"fortress_heroes_page_is_authored_complete",
		hero_page_ids == offered_heroes and offered_heroes.size() >= 7,
		"page=%s production=%s" % [str(hero_page_ids), str(offered_heroes)]
	)
	var cah_offered := offered_heroes.filter(func(id: String) -> bool: return id.contains("create-ahero"))
	var cah_on_page := hero_page_ids.filter(func(id: String) -> bool: return id.contains("create-ahero"))
	_check(
		"fortress_heroes_page_includes_every_offered_create_a_hero",
		cah_on_page == cah_offered,
		"commandset.ini:4067 slot 16 Command_CreateAHeroReviveSlot; offered=%s on-page=%s (empty in the headless scratch store by design)" % [str(cah_offered), str(cah_on_page)]
	)
	_slice.hud.set_radial_page(_slice.hud.RADIAL_PAGE_MAIN)
	_slice._sync_presentation()
	for i in 4:
		await process_frame
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
			var barracks_wheel := _radial_command_slots()
			_check(
				"barracks_radial_ids_match_compiled_command_set",
				barracks_wheel == barracks_compiled_slots,
				"wheel=%s compiled=%s" % [_format_slots(barracks_wheel), _format_slots(barracks_compiled_slots)]
			)
			_check(
				"barracks_empty_socket_shown_for_authored_hole",
				_empty_socket_visible(4) and not _empty_socket_visible(5),
				"slot5_empty=%s slot6_empty=%s" % [str(_empty_socket_visible(4)), str(_empty_socket_visible(5))]
			)
		_section_completed("barracks")

		var archery := await _build_structure(porter, "archery_range", fortress_pos, [Vector2(-18, 0), Vector2(18, 0), Vector2(14, 14), Vector2(-14, -14)])
		_check("archery_built", archery != 0)
		if archery != 0:
			await _select_structure(archery)
			var archery_wheel := _radial_command_slots()
			_check(
				"archery_radial_ids_match_compiled_command_set",
				archery_wheel == archery_compiled_slots,
				"kind=%s wheel=%s compiled=%s" % [
					String(sim.structure(archery).get("structure_kind", "")),
					_format_slots(archery_wheel),
					_format_slots(archery_compiled_slots),
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
			var forge_wheel := _radial_command_slots()
			_check(
				"forge_radial_ids_match_compiled_command_set",
				forge_wheel == forge_compiled_slots,
				"wheel=%s compiled=%s" % [_format_slots(forge_wheel), _format_slots(forge_compiled_slots)]
			)
		_section_completed("forge")

		# --- Section 3a: under-construction farm still exposes SELL (farm.ini:34)
		var incomplete_farm := _begin_structure(porter, "farm", fortress_pos, [Vector2(16, 16), Vector2(-16, 16), Vector2(16, -16), Vector2(-20, 8)])
		var incomplete_row: Dictionary = sim.structure(incomplete_farm) if incomplete_farm != 0 else {}
		_check(
			"incomplete_farm_exposes_sell",
			incomplete_farm != 0
			and float(incomplete_row.get("construction_progress", 1.0)) < 1.0
			and not sim.structure_sell_command(incomplete_farm).is_empty(),
			"id=%d progress=%s sell=%s last=%s" % [
				incomplete_farm,
				str(incomplete_row.get("construction_progress", "<gone>")),
				str(sim.structure_sell_command(incomplete_farm)),
				_last_construct_detail,
			]
		)
		if incomplete_farm != 0:
			await _select_structure(incomplete_farm)
			var incomplete_wheel := _radial_command_slots()
			var incomplete_before: int = sim.resources_for_team(0)
			var incomplete_sell := _find_sell_button()
			if incomplete_sell != null:
				incomplete_sell.pressed.emit()
				for i in 4:
					await process_frame
			var incomplete_sold: Dictionary = sim.structure(incomplete_farm)
			_check(
				"incomplete_farm_sale_refunds_fifty_percent",
				(incomplete_sold.is_empty() or int(incomplete_sold.get("health", 1)) <= 0)
				and sim.resources_for_team(0) == incomplete_before + 150
				and incomplete_wheel == {6: "Command_Sell"},
				"wheel=%s health=%s before=%d after=%d" % [
					_format_slots(incomplete_wheel),
					str(incomplete_sold.get("health", "<gone>")),
					incomplete_before,
					sim.resources_for_team(0),
				]
			)

		# --- Sections 3+4: farm wheel is sell at socket 6, and the sale executes ------
		var farm := await _build_structure(porter, "farm", fortress_pos, [Vector2(0, 18), Vector2(-18, 0), Vector2(0, -18), Vector2(-14, -14)])
		_check("farm_built", farm != 0)
		if farm != 0:
			await _select_structure(farm)
			var farm_wheel := _radial_command_slots()
			_check(
				"farm_radial_is_exactly_command_sell",
				farm_wheel == {6: "Command_Sell"},
				"wheel=%s entries=%s" % [_format_slots(farm_wheel), str(_radial_ids())]
			)
			_check(
				"farm_empty_sockets_shown_for_unoccupied",
				_empty_socket_visible(0) and _empty_socket_visible(1) and _empty_socket_visible(2)
				and _empty_socket_visible(3) and _empty_socket_visible(4) and not _empty_socket_visible(5),
				"visible=%s" % str(_empty_socket_visibility())
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
				_slice.hud.show_retail_tooltip(sell_button)
				var tip_title: String = _slice.hud.retail_tooltip.title_text()
				var tip_desc: String = _slice.hud.retail_tooltip.description_text()
				var tip_cost: String = _slice.hud.retail_tooltip.cost_text()
				_check(
					"farm_sell_tooltip_is_demolish_with_refund",
					tip_title == "Demolish Building"
					and tip_desc == "Demolish"
					and tip_cost == "Refund: 150"
					and not tip_cost.begins_with("Cost:"),
					"title=%s desc=%s cost=%s meta_cost=%s" % [
						tip_title, tip_desc, tip_cost, str(sell_button.get_meta("tooltip_cost", "<missing>")),
					]
				)
				_slice.hud.retail_tooltip.hide_tooltip()
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


func _command_slots_of(slots: Array) -> Dictionary:
	## Authored palantir map: {slot_number: commandId}. Holes are omitted keys.
	var mapped := {}
	for slot_value in slots:
		var row: Dictionary = slot_value
		var slot := int(row.get("slot", 0))
		var command_id := String(row.get("commandId", ""))
		if slot >= 1 and command_id != "":
			mapped[slot] = command_id
	return mapped


func _compiled_direct_slots_for_kind(kind: String) -> Dictionary:
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
			return _command_slots_of(direct)
	return {}


func _format_slots(slots: Dictionary) -> String:
	var keys: Array = slots.keys()
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s=%s" % [str(key), str(slots[key])])
	return "{%s}" % ", ".join(parts)


func _porter_id() -> int:
	for id in _slice.simulation.entities:
		var row: Dictionary = _slice.simulation.entities[id]
		if int(row.get("team", -1)) == 0 and bool(row.get("is_builder", false)) and int(row.get("health", 0)) > 0:
			return int(id)
	return 0


func _begin_structure(porter: int, kind: String, anchor: Vector2, offsets: Array) -> int:
	## Issue construct and return the site immediately, still under construction.
	var sim = _slice.simulation
	_last_construct_detail = "no-attempt"
	for offset_value in offsets:
		var offset := offset_value as Vector2
		var result: Dictionary = sim._issue_construct_for_team(0, [porter] as Array[int], kind, anchor + offset)
		_last_construct_detail = "%s@%s -> %s" % [kind, str(offset), str(result)]
		if bool(result.get("ok", false)):
			return int(result.get("structure_id", 0))
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


func _radial_command_slots() -> Dictionary:
	## Socket-position-aware wheel map. Slot numbers come from the button's
	## actual palantir position (RETAIL_COMMAND_SLOT_SOURCE), not from an
	## entry.slot self-oracle — packing-by-index must fail this comparison.
	var mapped := {}
	var sim = _slice.simulation
	var production_rules: Dictionary = sim.unit_production_rules_for_team(0)
	var entries: Array = _slice.hud.radial_entries()
	var buttons: Array = _slice.hud._radial_buttons
	for i in entries.size():
		var entry: Dictionary = entries[i]
		var kind := String(entry.get("command_kind", ""))
		var id := String(entry.get("id", ""))
		var command_id := id
		match kind:
			"train", "hero":
				var rule: Dictionary = production_rules.get(id, {}) as Dictionary
				command_id = String(rule.get("command_id", id))
			"upgrade":
				command_id = String(entry.get("command_id", id))
			"page", "back":
				continue
		var socket := 0
		if i < buttons.size():
			socket = _palantir_socket_for_button(buttons[i] as Button)
		if socket >= 1:
			mapped[socket] = command_id
	return mapped


func _palantir_socket_for_button(button: Button) -> int:
	## 1-based authored palantir socket, or 0 if the button is not on a dish.
	if button == null or _slice.hud.command_panel == null:
		return 0
	var origin: Vector2 = _slice.hud.command_panel.position
	for index in _slice.hud.RETAIL_COMMAND_SLOT_SOURCE.size():
		var expected: Vector2 = origin + (_slice.hud.RETAIL_COMMAND_SLOT_SOURCE[index] as Vector2)
		if button.position.is_equal_approx(expected):
			return index + 1
	return 0


func _empty_socket_visible(slot_index: int) -> bool:
	if _slice.hud.command_grid == null:
		return false
	var socket := _slice.hud.command_grid.get_node_or_null("RetailEmptySocket%d" % slot_index) as CanvasItem
	return socket != null and socket.visible


func _empty_socket_visibility() -> Array:
	var out: Array = []
	for slot_index in _slice.hud.RETAIL_COMMAND_SLOT_SOURCE.size():
		out.append(_empty_socket_visible(slot_index))
	return out


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
