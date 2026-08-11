extends SceneTree
## Retail SpecialAbilityToggleMounted, COMMAND-BUTTON half.
##
## Retail authority: the commandset does NOT swap when a hero mounts. Retail
## instead tags the individual buttons and the palantir hides the ones that do
## not belong to the live form - commandbutton.ini:
##   Command_SpecialAbilityTheodenGloriousCharge   Options = MOUNTED_ONLY
##   Command_SpecialAbilityGlorfindelWindRider     Options = MOUNTED_ONLY
##   Command_ToggleFaramirWeapon                   Options = UNMOUNTED_ONLY ...
##   Command_SpecialAbilityWoundArrow              Options = NEED_TARGET_ENEMY_OBJECT UNMOUNTED_ONLY
## (12 MOUNTED_ONLY / UNMOUNTED_ONLY occurrences in the retail file.)
##
## The compiled `options` array already reaches the runtime rule; this runner is
## the gate on the HUD honouring it. The options are READ OUT OF THE PACK, never
## hand-written, so a republish that drops them fails this gate.
## Env: OPENBFME_CONTENT must point at the private content-pack root.

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
# Loaded at RUNTIME, not preloaded: the HUD script references the ModLoader
# autoload, which does not exist while this runner's script is being compiled.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "HERO_MOUNTED_BUTTON_OPTIONS_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	_check("autoloads_available", content_db != null)
	if content_db == null:
		_finish()
		return
	var runtimes: Dictionary = content_db.call("get_playable_unit_runtimes")

	# --- Evidence: which loaded heroes actually carry form-gated buttons. ---
	var subjects: Dictionary = {}
	var mounted_only: Dictionary = {}
	var unmounted_only: Dictionary = {}
	var mount_ability: Dictionary = {}
	for key_value in runtimes.keys():
		var key := String(key_value)
		var document: Dictionary = runtimes[key] as Dictionary
		var toggle := ""
		var gated_mounted: Array[String] = []
		var gated_unmounted: Array[String] = []
		for rule in Adapter.ability_rules(document):
			var options: Array = rule.get("options", []) as Array
			if String((rule.get("effect", {}) as Dictionary).get("kind", "")) == "mount-toggle":
				toggle = String(rule.get("ability_id", ""))
			if options.has("MOUNTED_ONLY"):
				gated_mounted.append(String(rule.get("ability_id", "")))
			if options.has("UNMOUNTED_ONLY"):
				gated_unmounted.append(String(rule.get("ability_id", "")))
		if toggle == "" or (gated_mounted.is_empty() and gated_unmounted.is_empty()):
			continue
		subjects[key] = document
		mount_ability[key] = toggle
		mounted_only[key] = gated_mounted
		unmounted_only[key] = gated_unmounted
	var subject_ids: Array = subjects.keys()
	subject_ids.sort()
	print("HERO_MOUNTED_BUTTON_OPTIONS EVIDENCE subjects=%s" % str(subject_ids))
	for key in subject_ids:
		print("HERO_MOUNTED_BUTTON_OPTIONS EVIDENCE %s mounted_only=%s unmounted_only=%s" % [key, str(mounted_only[key]), str(unmounted_only[key])])
	# Named, so a republish that silently drops the compiled options is a RED
	# here rather than a quietly weaker gate.
	_check("pack_carries_theoden_mounted_only_button", (mounted_only.get("RohanTheoden", []) as Array).has("Command_SpecialAbilityTheodenGloriousCharge"))
	_check("pack_carries_glorfindel_mounted_only_button", (mounted_only.get("ElvenGlorfindel", []) as Array).has("Command_SpecialAbilityGlorfindelWindRider"))
	_check("pack_carries_faramir_unmounted_only_buttons", (unmounted_only.get("GondorFaramir", []) as Array).has("Command_SpecialAbilityWoundArrow"))
	# THE FOURTH FORM-GATED HERO, WHO IS NOT A SUBJECT.
	#
	# The shipped packs carry exactly four form-gated heroes. Three of them are
	# exercised above and through the HUD/sim sections below. The fourth, Angmar's
	# Morgramir, authors `Command_SpecialAbilityDarkGlory Options = UNMOUNTED_ONLY`
	# but has NO mount button at all: angmarmorgramir.ini:716 gives him the
	# ToggleMounted behavior while AngmarMorgramirCommandSet (commandset.ini:
	# 3261-3273) never offers it, so the importer correctly emits no mount-toggle
	# ability and the subject filter above skips him. His behaviour is right in
	# every reachable retail state - he always reads unmounted, so Dark Glory
	# always shows - but without this row a republish that dropped his
	# UNMOUNTED_ONLY option would not redden anything. Asserted as PACK DATA only;
	# there is no HUD state to drive.
	_check(
		"pack_carries_morgramir_unmounted_only_option_though_he_has_no_mount_button",
		_object_option_is_compiled(
			content_db, "AngmarMorgramir", "Command_SpecialAbilityDarkGlory", "UNMOUNTED_ONLY"
		),
		"angmarmorgramir.json must still compile the authored UNMOUNTED_ONLY option"
	)
	if subjects.is_empty():
		_finish()
		return
	print("HERO_MOUNTED_BUTTON_OPTIONS SECTION evidence_read")

	# --- One HUD carrying every form-gated hero across the loaded factions. -
	var hud = load("res://src/retail_slice/retail_hud.gd").new()
	# Producer kinds default to the source producer object id, which is already
	# unique per faction fortress: no manifest needed for a presentation gate.
	var registration_error: String = hud.enable_playable_unit_content(subjects, {})
	_check("hud_registration_succeeds", registration_error == "", registration_error)
	if registration_error != "":
		hud.free()
		_finish()
		return
	hud.build()

	# PHASE 1 - every subject, driven by an authoritative entity row.
	# Cross-faction: a men-host sim cannot spawn ElvenGlorfindel (its producer
	# lives in the elves slice), and the filter under test is presentation-only,
	# so the row is handed in exactly as the sim hands it in. PHASE 2 below
	# proves the same behaviour end-to-end off the REAL sim rows.
	for key_value in subject_ids:
		var key := String(key_value)
		var unit_id := Adapter.runtime_unit_id(subjects[key] as Dictionary)
		var buttons: Dictionary = hud.hero_ability_buttons.get(unit_id, {}) as Dictionary
		if not _check("%s_has_ability_buttons" % key, not buttons.is_empty()):
			continue
		var ungated := _ungated_ids(buttons, mounted_only[key] as Array, unmounted_only[key] as Array)
		var selection: Array[int] = [1]

		var entities := {1: {"category": "hero", "unit_type": unit_id, "level": 10, "ability_states": {}, "mounted": false}}
		hud.set_unit_selection_state(selection, entities, 0)
		_check("%s_row_unmounted_hides_mounted_only" % key, _all_hidden(buttons, mounted_only[key] as Array), _visible_names(buttons, mounted_only[key] as Array))
		_check("%s_row_unmounted_shows_unmounted_only" % key, _all_visible(buttons, unmounted_only[key] as Array), _hidden_names(buttons, unmounted_only[key] as Array))
		# The ungated buttons must be UNTOUCHED by the form filter. Not "all
		# visible": a button authored beyond the retail command ring (capture at
		# slot 12) is hidden by the socket layout in both forms, and that is
		# pre-existing behaviour this gate must not silently change.
		var ungated_before := _visibility_map(buttons, ungated)

		((entities[1] as Dictionary))["mounted"] = true
		hud.set_unit_selection_state(selection, entities, 0)
		_check("%s_row_mounted_shows_mounted_only" % key, _all_visible(buttons, mounted_only[key] as Array), _hidden_names(buttons, mounted_only[key] as Array))
		_check("%s_row_mounted_hides_unmounted_only" % key, _all_hidden(buttons, unmounted_only[key] as Array), _visible_names(buttons, unmounted_only[key] as Array))
		_check("%s_row_ungated_buttons_survive_the_toggle" % key, _visibility_map(buttons, ungated) == ungated_before, "%s -> %s" % [str(ungated_before), str(_visibility_map(buttons, ungated))])

		# Deselecting still hides everything: the form filter must never
		# resurrect a button for an unselected hero.
		var empty_selection: Array[int] = []
		hud.set_unit_selection_state(empty_selection, entities, 0)
		var all_ids: Array[String] = []
		for id_value in buttons.keys():
			all_ids.append(String(id_value))
		_check("%s_row_deselected_hides_every_button" % key, _all_hidden(buttons, all_ids), _visible_names(buttons, all_ids))
		print("HERO_MOUNTED_BUTTON_OPTIONS SECTION %s_row_done" % key)

	# PHASE 2 - the host faction's own heroes, off the real sim and a real
	# `cast_ability` mount toggle. Nothing here is hand-set.
	var host_subjects: Dictionary = {}
	for key_value in subject_ids:
		var key := String(key_value)
		if _producer_object_id(subjects[key] as Dictionary) == "MenFortress":
			host_subjects[key] = subjects[key]
	var host_ids: Array = host_subjects.keys()
	host_ids.sort()
	_check("host_faction_contributes_both_option_kinds", host_ids.has("RohanTheoden") and host_ids.has("GondorFaramir"), str(host_ids))

	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": host_subjects,
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check("sim_registration_succeeds", String(sim.configuration_error) == "", String(sim.configuration_error))
	sim.ai_enabled = false

	var entity_id := 1
	for key_value in host_ids:
		var key := String(key_value)
		var unit_id := Adapter.runtime_unit_id(host_subjects[key] as Dictionary)
		sim._add_battalion(entity_id, 0, Vector2(float(entity_id) * 40.0, 0.0), key, unit_id, unit_id)
		var hero_row: Dictionary = sim.entity(entity_id)
		if not _check("%s_spawned" % key, not hero_row.is_empty()):
			entity_id += 1
			continue
		var buttons: Dictionary = hud.hero_ability_buttons.get(unit_id, {}) as Dictionary
		var selection: Array[int] = [entity_id]
		# Faramir's ToggleMounted carries an authored rank gate; the form filter
		# is not what is under test here, so the hero is ranked past every gate.
		hero_row["level"] = 10

		hud.set_unit_selection_state(selection, sim.entities, sim.tick_index)
		_check("%s_sim_unmounted_hides_mounted_only" % key, _all_hidden(buttons, mounted_only[key] as Array), _visible_names(buttons, mounted_only[key] as Array))
		_check("%s_sim_unmounted_shows_unmounted_only" % key, _all_visible(buttons, unmounted_only[key] as Array), _hidden_names(buttons, unmounted_only[key] as Array))

		var mounted: Dictionary = sim.cast_ability(entity_id, String(mount_ability[key]), Vector2.ZERO)
		_check("%s_sim_mount_toggle_casts" % key, bool(mounted.get("ok", false)) and bool(mounted.get("mounted", false)), String(mounted.get("reason", "")))
		hud.set_unit_selection_state(selection, sim.entities, sim.tick_index)
		_check("%s_sim_mounted_shows_mounted_only" % key, _all_visible(buttons, mounted_only[key] as Array), _hidden_names(buttons, mounted_only[key] as Array))
		_check("%s_sim_mounted_hides_unmounted_only" % key, _all_hidden(buttons, unmounted_only[key] as Array), _visible_names(buttons, unmounted_only[key] as Array))

		sim.advance(15)
		var dismounted: Dictionary = sim.cast_ability(entity_id, String(mount_ability[key]), Vector2.ZERO)
		_check("%s_sim_dismount_toggle_casts" % key, bool(dismounted.get("ok", false)) and not bool(dismounted.get("mounted", true)), String(dismounted.get("reason", "")))
		hud.set_unit_selection_state(selection, sim.entities, sim.tick_index)
		_check("%s_sim_dismounted_hides_mounted_only_again" % key, _all_hidden(buttons, mounted_only[key] as Array), _visible_names(buttons, mounted_only[key] as Array))
		_check("%s_sim_dismounted_shows_unmounted_only_again" % key, _all_visible(buttons, unmounted_only[key] as Array), _hidden_names(buttons, unmounted_only[key] as Array))
		print("HERO_MOUNTED_BUTTON_OPTIONS SECTION %s_sim_done" % key)
		entity_id += 1

	hud.free()
	_finish()


func _producer_object_id(document: Dictionary) -> String:
	for route_value in Array((document.get("registration", {}) as Dictionary).get("production", [])):
		if typeof(route_value) == TYPE_DICTIONARY:
			return String((route_value as Dictionary).get("producerObjectId", ""))
	return ""


func _ungated_ids(buttons: Dictionary, mounted_only: Array, unmounted_only: Array) -> Array[String]:
	var output: Array[String] = []
	for id_value in buttons.keys():
		var ability_id := String(id_value)
		if not mounted_only.has(ability_id) and not unmounted_only.has(ability_id):
			output.append(ability_id)
	output.sort()
	return output


func _visibility_map(buttons: Dictionary, ability_ids: Array) -> Dictionary:
	var output: Dictionary = {}
	for ability_value in ability_ids:
		var button: Button = buttons.get(String(ability_value)) as Button
		output[String(ability_value)] = button != null and button.visible
	return output


func _all_visible(buttons: Dictionary, ability_ids: Array) -> bool:
	return _hidden_names(buttons, ability_ids) == ""


func _all_hidden(buttons: Dictionary, ability_ids: Array) -> bool:
	return _visible_names(buttons, ability_ids) == ""


func _visible_names(buttons: Dictionary, ability_ids: Array) -> String:
	var output: Array[String] = []
	for ability_value in ability_ids:
		var button: Button = buttons.get(String(ability_value)) as Button
		if button != null and button.visible:
			output.append(String(ability_value))
	return "visible:%s" % str(output) if not output.is_empty() else ""


func _hidden_names(buttons: Dictionary, ability_ids: Array) -> String:
	var output: Array[String] = []
	for ability_value in ability_ids:
		var button: Button = buttons.get(String(ability_value)) as Button
		if button == null or not button.visible:
			output.append(String(ability_value))
	return "hidden:%s" % str(output) if not output.is_empty() else ""



func _object_option_is_compiled(
	content_db, object_id: String, ability_id: String, option: String
) -> bool:
	## Pack-data only: does this object's compiled ability rule still carry the
	## authored option? Used for a form-gated hero the HUD sections cannot drive
	## because retail never gives him a mount button.
	var document: Dictionary = content_db.call("get_playable_unit_runtime", object_id)
	if document.is_empty():
		return false
	for rule in Adapter.ability_rules(document):
		if String(rule.get("ability_id", "")) != ability_id:
			continue
		return (rule.get("options", []) as Array).has(option)
	return false

func _check(name: String, ok: bool, detail: String = "") -> bool:
	_runner_watchdog.note(name)
	if ok:
		passed += 1
		print("HERO_MOUNTED_BUTTON_OPTIONS PASS %s" % name)
	else:
		failed += 1
		print("HERO_MOUNTED_BUTTON_OPTIONS FAIL %s | %s" % [name, detail])
	return ok


func _finish() -> void:
	print("HERO_MOUNTED_BUTTON_OPTIONS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
