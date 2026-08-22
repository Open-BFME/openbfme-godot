extends SceneTree
## THE SETUP NOBODY TOUCHES. The owner made a hero in MY HEROES, pressed SOLO
## PLAY, pressed the map, pressed PLAY - and the fortress offered no created
## hero at all (owner report 2026-08-22, queue Q69).
##
## WHY NO EXISTING GATE SAW IT. `menu_instant_runner` drives the same screen and
## the same launch write, but it calls `picker.select(picked_index)` first
## (menu_instant_runner.gd:287) - it makes the pick the player never made.
## `cah_match_runner` and `fortress_command_surface_runner` hand the roster a
## profile directly and never go through the setup screen at all. So the ONE
## path the player actually walks - real screen, real launch write, Hero column
## left exactly as the screen drew it - had no coverage, and it was the broken
## one:
##
##   `_populate_row_hero` selected item 0 ("-") on every rebuild, so an untouched
##   row reported no hero; `apply_skirmish_selection` wrote that as
##   `retail_picked_created_hero_documents = []`; and an EMPTY pick is
##   authoritative in `_add_created_heroes` (retail_vertical_slice.gd:2058-2062).
##   Nothing fell back, nothing complained, the hero page simply had no hero on
##   it.
##
## This runner drives the real menu, never touches the Hero column, and asserts
## the created hero reaches the fortress roster and can be bought through the
## deterministic command queue - then asserts the player's own decisions still
## win: "-" is selectable, sticks, and fields nothing.
##
## Heroes are saved and deleted in a SCRATCH store (tests/cah_profile_sandbox.gd)
## and the run proves it never went near the player's own.
##
## Env: OPENBFME_CONTENT - the content root to mount (as every goal runner).

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const ProfileSandboxScript := preload("res://tests/cah_profile_sandbox.gd")

const FACTION := "men"
const MAP_SCALE := 0.1
## Hero of the West / subclass 0 authors Men among its UsableFactions, so both
## fixtures are eligible for the default Men row the screen opens on.
const CLASS_INDEX := 0
const SUB_CLASS_INDEX := 0
const MAX_WAIT_FRAMES := 1200
## Filesystem modified-time resolution is one second, and "most recently saved"
## is decided on it. The fixtures are written a full tick apart so the newest is
## the newest by the same rule the menu uses, not by luck.
const SAVE_GAP_MS := 1100
## LIVENESS. A GDScript runtime error aborts its function without propagating, so
## a broken run would print zero failures. Raise deliberately; never lower.
const EXPECTED_CHECKS := 25

var _profiles := ProfileSandboxScript.new()
var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _initialize() -> void:
	_profiles.open("cah-untouched-setup")
	_runner_watchdog.start(self, "CAH_UNTOUCHED_SETUP", 900_000)
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var content_db := root.get_node_or_null("ContentDB")
	if content_db == null:
		printerr("CAH_UNTOUCHED_SETUP FAIL no ContentDB autoload")
		_finish()
		return
	await process_frame
	await process_frame

	var system_value: Variant = content_db.get("cah_system_runtime")
	var system: Dictionary = (
		system_value as Dictionary if typeof(system_value) == TYPE_DICTIONARY else {}
	)
	_check("mounted_pack_carries_a_cah_table", CahHeroes.system_is_valid(system))
	if not CahHeroes.system_is_valid(system):
		_finish()
		return

	# TWO heroes, saved a clock tick apart. The older one is the control: the
	# untouched row must offer the NEWEST, not merely "some hero", or the rule
	# would be untestable the day a player owns more than one.
	var older := CahHeroes.new_profile(system, "Older Hero", CLASS_INDEX, SUB_CLASS_INDEX)
	_check("older_hero_saves", CahHeroes.save_profile(older) == "")
	OS.delay_msec(SAVE_GAP_MS)
	var newest := CahHeroes.new_profile(system, "Newest Hero", CLASS_INDEX, SUB_CLASS_INDEX)
	_check("newest_hero_saves", CahHeroes.save_profile(newest) == "")
	var newest_object := "CreateAHero__%s" % String(newest.get("heroId", ""))
	var older_object := "CreateAHero__%s" % String(older.get("heroId", ""))

	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var menu := packed.instantiate()
	root.add_child(menu)
	var frames := 0
	while not bool(menu.get("_skirmish_options_ready")) and frames < MAX_WAIT_FRAMES:
		frames += 1
		await process_frame
	_check("skirmish_setup_becomes_ready", bool(menu.get("_skirmish_options_ready")),
		"frames=%d" % frames)
	if not bool(menu.get("_skirmish_options_ready")):
		_finish()
		return
	# The screen builds its own rows. NOTHING below presses the Hero column - that
	# omission is the whole test. And nothing below refreshes it for the screen
	# either: the owner saw the column stay "-" until a faction change, which is
	# exactly what a runner that calls _refresh_hero_rows() itself cannot see.
	var picker: OptionButton = menu.solo_flyout.hero_dropdowns[0]
	_check("screen_populates_hero_column_without_help", picker.item_count == 3,
		"item_count=%d before any refresh" % picker.item_count)
	_check("untouched_hero_column_still_offers_none",
		picker.item_count > 0 and String(picker.get_item_metadata(0)) == "",
		"item 0 must remain the '-' choice")
	_check("untouched_hero_column_lists_both_saved_heroes", picker.item_count == 3,
		"item_count=%d" % picker.item_count)
	_check("untouched_hero_column_offers_the_newest_hero",
		String(menu._selected_row_hero_id(0)) == String(newest.get("heroId", "")),
		"offered '%s', newest is '%s'" % [
			String(menu._selected_row_hero_id(0)), String(newest.get("heroId", ""))
		])

	# A hero made in MY HEROES after the rows were built must be offered the
	# moment the player returns to the Solo page - not after a faction change.
	OS.delay_msec(SAVE_GAP_MS)
	var late := CahHeroes.new_profile(system, "Late Hero", CLASS_INDEX, SUB_CLASS_INDEX)
	_check("late_hero_saves", CahHeroes.save_profile(late) == "")
	menu._show_page(menu.PAGE_SOLO)
	_check("returning_to_solo_lists_the_late_hero", picker.item_count == 4,
		"item_count=%d after a hero was saved and the Solo page re-entered" % picker.item_count)
	_check("returning_to_solo_offers_the_late_hero_as_newest",
		String(menu._selected_row_hero_id(0)) == String(late.get("heroId", "")),
		"offered '%s'" % String(menu._selected_row_hero_id(0)))
	_check("late_hero_removed", CahHeroes.delete_profile(String(late.get("heroId", ""))))
	menu._show_page(menu.PAGE_SOLO)
	_check("deleted_hero_leaves_the_list", picker.item_count == 3, "item_count=%d" % picker.item_count)

	# --- THE OWNER'S PATH: press PLAY without ever opening the Hero column ----
	_check("untouched_launch_is_recorded", menu.apply_skirmish_selection())
	var fielded := _fielded_created_heroes(menu, system)
	_check("untouched_setup_fields_the_newest_created_hero", fielded.has(newest_object),
		"fielded=%s expected=%s" % [str(fielded), newest_object])
	_check("untouched_setup_fields_only_one_created_hero",
		not fielded.has(older_object) and fielded.size() == 1, "fielded=%s" % str(fielded))

	# --- THE HERO IS BUYABLE, through the queue a real click goes through -----
	_check_the_fortress_sells_it(menu, system, newest_object)

	# --- THE PLAYER'S OWN DECISIONS STILL WIN --------------------------------
	# "-" means bring none. It has to survive both the launch write and the next
	# rebuild of the row, or the default would be the screen overruling them.
	picker.select(0)
	picker.item_selected.emit(0)
	_check("choosing_none_is_recorded", menu.apply_skirmish_selection())
	_check("choosing_none_fields_no_created_hero",
		_fielded_created_heroes(menu, system).is_empty(),
		"fielded=%s" % str(_fielded_created_heroes(menu, system)))
	menu._refresh_hero_rows()
	_check("choosing_none_survives_a_row_rebuild",
		String(menu._selected_row_hero_id(0)) == "",
		"row re-offered '%s' after the player chose none" % String(menu._selected_row_hero_id(0)))

	menu.queue_free()
	_finish()


func _check_the_fortress_sells_it(menu: Node, system: Dictionary, object_id: String) -> void:
	## Trainable, not merely present. The purchase goes through `submit_command`
	## + `advance`, which is the path a click takes in a real match.
	var slice = _classified_slice(menu, system)
	if slice == null:
		_check("fortress_offers_the_untouched_setups_hero", false, "no slice")
		return
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var content_db := root.get_node_or_null("ContentDB")
	var manifest: Dictionary = manifest_script.from_registries(
		FACTION,
		(slice.fieldable_unit_runtimes as Dictionary).duplicate(true),
		content_db.call("get_playable_structure_runtimes")
			if content_db != null and content_db.has_method("get_playable_structure_runtimes")
			else {}
	)
	var builder_rules: Dictionary = {}
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_rule: Dictionary = slice._faction_builder_unit_rule(String(builder_value))
		if not builder_rule.is_empty():
			builder_rules[String(builder_value)] = builder_rule
	slice.free()
	if not producible.has(object_id):
		_check("fortress_offers_the_untouched_setups_hero", false,
			"the hero is not in the producible roster")
		return
	var adapter = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var unit_type := String(adapter.runtime_unit_id(producible[object_id] as Dictionary))
	var sim = load("res://src/retail_slice/retail_slice_sim.gd").new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"spawn_initial_battalions": false,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_rules,
		"starting_resources": 100000000,
		"command_point_cap": 100000000,
		"source_map_transform_scale": MAP_SCALE,
	})
	if String(sim.configuration_error) != "":
		_check("fortress_offers_the_untouched_setups_hero", false,
			"the match refused to configure: %s" % String(sim.configuration_error))
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	var fortress := int(sim.fortress_id(0))
	_check("the_players_fortress_is_seeded", fortress != 0)
	if fortress == 0:
		return
	var offered: Array = Array((sim.structure(fortress) as Dictionary).get("production", []))
	_check("fortress_offers_the_untouched_setups_hero", offered.has(unit_type),
		"unit_type=%s" % unit_type)
	var accepted: bool = sim.submit_command({
		"tick": sim.tick_index + 1,
		"team": 0,
		"seq": 1,
		"type": "queue_unit",
		"args": {"producer": fortress, "unit_type": unit_type},
	})
	_check("the_hero_purchase_is_accepted_by_the_command_queue", accepted)
	sim.advance(2)
	_check("the_purchase_reaches_the_fortress_queue",
		(sim.production_queue_state(fortress) as Array).size() == 1)


func _classified_slice(menu: Node, system: Dictionary):
	## The created heroes THIS recorded launch would put on the fortress, read
	## back through the slice's own classification rather than re-derived here.
	var slice = load("res://src/retail_slice/retail_vertical_slice.gd").new()
	var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
	map_data.local_transform_scale = MAP_SCALE
	slice.source_map_data = map_data
	slice._classify_faction_units(FACTION, {}, {}, {}, menu._game_state, system)
	return slice


func _fielded_created_heroes(menu: Node, system: Dictionary) -> Array:
	var slice = _classified_slice(menu, system)
	var out: Array = []
	for object_id in (slice.producible_unit_runtimes as Dictionary).keys():
		if String(object_id).begins_with("CreateAHero__"):
			out.append(String(object_id))
	slice.free()
	out.sort()
	return out


func _check(name: String, ok: bool, detail: String = "") -> bool:
	_runner_watchdog.note(name)
	if ok:
		_passed += 1
		print("CAH_UNTOUCHED_SETUP PASS %s" % name)
	else:
		_failed += 1
		printerr("CAH_UNTOUCHED_SETUP FAIL %s %s" % [name, detail])
	return ok


func _finish() -> void:
	for profile in CahHeroes.load_profiles():
		CahHeroes.delete_profile(String(profile.get("heroId", "")))
	_check("run_left_the_players_own_heroes_untouched", _profiles.real_store_untouched(),
		_profiles.real_store_description())
	if _passed + _failed != EXPECTED_CHECKS:
		_failed += 1
		printerr("CAH_UNTOUCHED_SETUP FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [_passed + _failed, EXPECTED_CHECKS])
	_profiles.close()
	_runner_watchdog.stop()
	print("CAH_UNTOUCHED_SETUP_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
