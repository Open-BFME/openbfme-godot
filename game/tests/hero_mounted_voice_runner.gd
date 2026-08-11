extends SceneTree
## Retail SpecialAbilityToggleMounted, VOICE half.
##
## Retail authority: heroes that carry `ToggleMountedSpecialAbilityUpdate`
## author a SECOND voice set for the mounted form and the engine picks it while
## the MOUNTED ModelConditionFlag is live (theoden.ini authors both
## `TheodenVoiceSelectMS` and `TheodenVoiceSelectMountedMS` under VoiceSelect,
## `TheodenVoiceMove` and `TheodenVoiceMoveMounted` under VoiceMove).
##
## The audio module already classifies those alternates by form
## (`_voice_candidate_form`) and `route_roster_voice(..., form)` already prefers
## them - but the form only ever arrives on the event, out of
## `_voice_event_identity()` reading `row["form"]`. This runner is the gate on
## the sim actually setting that key across a mount toggle, and - just as
## important - on the key staying ABSENT otherwise, because every present key
## in an entity row is walked by `_canonicalize()` and would move the
## determinism pin.
##
## Reads the mount ability id and the mounted event ids OUT OF THE PACK, so a
## republish that drops either fails this gate instead of silently passing.
## Env: OPENBFME_CONTENT must point at the private content-pack root.

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "HERO_MOUNTED_VOICE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	_check("autoloads_available", content_db != null)
	if content_db == null:
		_finish()
		return
	var runtimes: Dictionary = content_db.call("get_playable_unit_runtimes")
	var theoden: Dictionary = runtimes.get("RohanTheoden", {}) as Dictionary
	_check("selected_pack_carries_theoden", not theoden.is_empty())
	if theoden.is_empty():
		_finish()
		return
	print("HERO_MOUNTED_VOICE SECTION pack_loaded")

	# --- Evidence read from the pack, never hand-written. -------------------
	var mount_ability := _mount_toggle_ability_id(theoden)
	_check("pack_binds_theoden_mount_toggle", mount_ability != "", mount_ability)
	var mounted_select := _form_event_id(theoden, "VoiceSelect")
	var mounted_move := _form_event_id(theoden, "VoiceMove")
	var foot_select := _plain_event_id(theoden, "VoiceSelect")
	var foot_move := _plain_event_id(theoden, "VoiceMove")
	_check("pack_binds_theoden_mounted_select_event", mounted_select != "", mounted_select)
	_check("pack_binds_theoden_mounted_move_event", mounted_move != "", mounted_move)
	_check("pack_binds_theoden_foot_select_event", foot_select != "" and foot_select != mounted_select, foot_select)
	_check("pack_binds_theoden_foot_move_event", foot_move != "" and foot_move != mounted_move, foot_move)
	if mount_ability == "" or mounted_select == "":
		_finish()
		return

	var unit_id := Adapter.runtime_unit_id(theoden)
	var object_id := Adapter.runtime_member_id(theoden)

	# --- Sim: the form key across one mount / dismount cycle. ---------------
	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"RohanTheoden": theoden},
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check("sim_registration_succeeds", String(sim.configuration_error) == "", String(sim.configuration_error))
	sim.ai_enabled = false
	sim._add_battalion(1, 0, Vector2.ZERO, "Theoden", unit_id, unit_id)
	var hero_row: Dictionary = sim.entity(1)
	_check("hero_spawned", not hero_row.is_empty())
	if hero_row.is_empty():
		_finish()
		return

	# PIN SAFETY. `form` must not exist on a hero that never toggled: a key
	# seeded unconditionally would be canonicalized into every snapshot and
	# would move the 3000-tick state pin.
	_check("spawned_hero_has_no_form_key", not hero_row.has("form"))
	_check("spawned_hero_is_unmounted", not bool(hero_row.get("mounted", false)))
	_check("foot_identity_carries_no_form", String(sim._voice_event_identity(1).get("form", "")) == "")
	sim.select_only(1)
	_check("foot_select_event_carries_no_form", String(_last_voice_select(sim).get("form", "")) == "")

	var mounted: Dictionary = sim.cast_ability(1, mount_ability, Vector2.ZERO)
	_check("mount_toggle_casts", bool(mounted.get("ok", false)) and bool(mounted.get("mounted", false)), String(mounted.get("reason", "")))
	_check("mounted_row_carries_mounted_form", String(hero_row.get("form", "")) == "mounted", String(hero_row.get("form", "<absent>")))
	_check("mounted_identity_carries_form", String(sim._voice_event_identity(1).get("form", "")) == "mounted")
	sim.select_only(1)
	_check("mounted_select_event_carries_form", String(_last_voice_select(sim).get("form", "")) == "mounted")
	print("HERO_MOUNTED_VOICE SECTION mount_applied")

	sim.advance(15)
	var dismounted: Dictionary = sim.cast_ability(1, mount_ability, Vector2.ZERO)
	_check("dismount_toggle_casts", bool(dismounted.get("ok", false)) and not bool(dismounted.get("mounted", true)), String(dismounted.get("reason", "")))
	# ABSENT, not empty: an empty string is still a key in the canonical walk.
	_check("dismounted_row_has_no_form_key", not hero_row.has("form"))
	sim.select_only(1)
	_check("dismounted_select_event_carries_no_form", String(_last_voice_select(sim).get("form", "")) == "")
	print("HERO_MOUNTED_VOICE SECTION dismount_applied")

	# --- Audio: the form actually resolves the mounted variant. -------------
	var pack_root := String(
		PackCapabilityScript.resolve_host_slice_pack(content_db.pack_meta).get("root", "")
	)
	_check("host_pack_root_resolved", pack_root != "")
	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(pack_root, false, {"RohanTheoden": theoden})
	var foot_route: Dictionary = audio.route_roster_voice(object_id, "select", 1)
	var mounted_route: Dictionary = audio.route_roster_voice(object_id, "select", 2, "mounted")
	var mounted_move_route: Dictionary = audio.route_roster_voice(object_id, "move", 3, "mounted")
	_check("foot_form_routes_foot_select", bool(foot_route.get("ok", false)) and String(foot_route.get("event_id", "")) == foot_select, String(foot_route.get("event_id", foot_route.get("reason", ""))))
	_check("mounted_form_routes_mounted_select", bool(mounted_route.get("ok", false)) and String(mounted_route.get("event_id", "")) == mounted_select, String(mounted_route.get("event_id", mounted_route.get("reason", ""))))
	_check("mounted_form_routes_mounted_move", bool(mounted_move_route.get("ok", false)) and String(mounted_move_route.get("event_id", "")) == mounted_move, String(mounted_move_route.get("event_id", mounted_move_route.get("reason", ""))))
	root.remove_child(audio)
	audio.free()
	print("HERO_MOUNTED_VOICE SECTION audio_routed")

	# --- Honest census of every mount-toggle hero in the loaded packs. ------
	var voiced: Array[String] = []
	var gapped: Array[String] = []
	for key in runtimes.keys():
		var document: Dictionary = runtimes[key] as Dictionary
		if _mount_toggle_ability_id(document) == "":
			continue
		if _form_event_id(document, "VoiceSelect") != "" or _form_event_id(document, "VoiceMove") != "":
			voiced.append(String(key))
		else:
			gapped.append(String(key))
	voiced.sort()
	gapped.sort()
	print("HERO_MOUNTED_VOICE CENSUS mount_toggle_heroes_with_mounted_voice=%s" % str(voiced))
	print("HERO_MOUNTED_VOICE CENSUS mount_toggle_heroes_without_mounted_voice=%s" % str(gapped))
	_check("census_covers_at_least_the_men_and_elven_mounted_heroes", voiced.size() >= 5, str(voiced.size()))
	print("HERO_MOUNTED_VOICE SECTION census_done")
	_finish()


func _mount_toggle_ability_id(document: Dictionary) -> String:
	for rule in Adapter.ability_rules(document):
		if String((rule.get("effect", {}) as Dictionary).get("kind", "")) == "mount-toggle":
			return String(rule.get("ability_id", ""))
	return ""


func _voice_field_ids(document: Dictionary, field: String) -> Array[String]:
	var output: Array[String] = []
	var routes: Dictionary = (document.get("registration", {}) as Dictionary).get("audioRoutes", {}) as Dictionary
	for owner_value in routes.values():
		if typeof(owner_value) != TYPE_DICTIONARY:
			continue
		for row_value in Array((owner_value as Dictionary).get(field, [])):
			var event_id := String((row_value as Dictionary).get("id", ""))
			if event_id != "" and not output.has(event_id):
				output.append(event_id)
	return output


func _form_event_id(document: Dictionary, field: String) -> String:
	for event_id in _voice_field_ids(document, field):
		if event_id.to_lower().contains("mounted") or event_id.to_lower().contains("knight"):
			return event_id
	return ""


func _plain_event_id(document: Dictionary, field: String) -> String:
	for event_id in _voice_field_ids(document, field):
		if not (event_id.to_lower().contains("mounted") or event_id.to_lower().contains("knight")):
			return event_id
	return ""


func _last_voice_select(sim) -> Dictionary:
	var found: Dictionary = {}
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "voice.select":
			found = event
	return found


func _check(name: String, ok: bool, detail: String = "") -> void:
	_runner_watchdog.note(name)
	if ok:
		passed += 1
		print("HERO_MOUNTED_VOICE PASS %s" % name)
	else:
		failed += 1
		print("HERO_MOUNTED_VOICE FAIL %s | %s" % [name, detail])


func _finish() -> void:
	print("HERO_MOUNTED_VOICE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
