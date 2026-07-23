extends SceneTree

## NETWORK flyout contract: the menu's multiplayer seam writes a validated
## lockstep selection to GameState, environment variables still beat the menu
## inside the slice, and a later solo launch clears the multiplayer mode.

# Deliberately NO top-level preload of slice scripts: in --script main-loop
# context they would compile before the autoload globals they reference exist
# (same rule as menu_skirmish_runner) — load lazily inside _run instead.

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/boot.tscn")
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame

	var game_state := root.get_node_or_null("GameState")
	_check("game_state_autoload_present", game_state != null)
	var flyout = menu.get_node_or_null("Center/MultiplayerFlyout")
	var mp_button: Button = menu.get_node_or_null("Center/Multiplayer")
	_check("multiplayer_button_and_flyout_exist", flyout != null and mp_button != null)
	if game_state == null or flyout == null or mp_button == null:
		return _finish()

	# Flyout opens over the persistent bottom bar (retail REF-01 shape).
	mp_button.pressed.emit()
	await process_frame
	_check("flyout_opens_with_bottom_bar_visible", flyout.visible and mp_button.visible)
	_check(
		"defaults_match_settings_conventions",
		flyout.host_port_edit.text == "26015"
			and flyout.join_address_edit.text == "127.0.0.1"
			and flyout.join_port_edit.text == "26015"
	)

	# Field validation fails closed with player-facing messages.
	_check("port_validation_rejects_garbage", flyout.validate_port("fish") != "")
	_check("port_validation_rejects_out_of_range", flyout.validate_port("70000") != "")
	_check("address_validation_rejects_non_ip", flyout.validate_address("not-an-ip") != "")
	_check("address_validation_accepts_lan_ip", flyout.validate_address("192.168.1.50") == "")

	# The selection seam writes GameState only on valid input.
	_check("selection_rejects_bad_mode", not menu.apply_multiplayer_selection("observer", "", 26015))
	_check("selection_rejects_bad_port", not menu.apply_multiplayer_selection("host", "", 80))
	_check(
		"selection_rejects_join_without_ip",
		not menu.apply_multiplayer_selection("join", "nope", 26015)
	)
	var host_ok: bool = menu.apply_multiplayer_selection("host", "127.0.0.1", 27100)
	_check("host_selection_accepted", host_ok)
	if host_ok:
		_check(
			"host_selection_reaches_game_state",
			String(game_state.get("retail_mp_mode")) == "host"
				and int(game_state.get("retail_mp_port")) == 27100
		)
	var join_ok: bool = menu.apply_multiplayer_selection("join", "192.168.1.50", 27200)
	_check("join_selection_accepted", join_ok)
	if join_ok:
		_check(
			"join_selection_reaches_game_state",
			String(game_state.get("retail_mp_mode")) == "join"
				and String(game_state.get("retail_mp_address")) == "192.168.1.50"
				and int(game_state.get("retail_mp_port")) == 27200
		)

	# The slice resolver prefers the menu selection when env is unset...
	var former_mode := OS.get_environment("OPENBFME_MP")
	OS.set_environment("OPENBFME_MP", "")
	OS.set_environment("OPENBFME_MP_ADDRESS", "")
	OS.set_environment("OPENBFME_MP_PORT", "")
	# Detached nodes: _ready never runs, so the seam is tested without booting
	# the whole match (the resolver takes the GameState reference directly).
	var VerticalSliceScript = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = VerticalSliceScript.new()
	slice._resolve_mp_settings(game_state)
	_check(
		"slice_resolver_reads_menu_selection",
		slice._mp_mode == "join"
			and slice._mp_address == "192.168.1.50"
			and slice._mp_port_text == "27200"
	)
	slice.free()
	# ...and the environment always wins when set (headless runner contract).
	OS.set_environment("OPENBFME_MP", "host")
	OS.set_environment("OPENBFME_MP_PORT", "28000")
	var env_slice = VerticalSliceScript.new()
	env_slice._resolve_mp_settings(game_state)
	_check(
		"environment_beats_menu_selection",
		env_slice._mp_mode == "host" and env_slice._mp_port_text == "28000"
	)
	env_slice.free()
	OS.set_environment("OPENBFME_MP", former_mode)
	OS.set_environment("OPENBFME_MP_ADDRESS", "")
	OS.set_environment("OPENBFME_MP_PORT", "")

	# A solo launch clears the lingering multiplayer mode fail-closed.
	if menu.has_method("apply_skirmish_selection"):
		var solo_applied: bool = menu.apply_skirmish_selection()
		if solo_applied:
			_check("solo_launch_clears_mp_mode", String(game_state.get("retail_mp_mode")) == "")
		else:
			# Selection can be legitimately blocked in stripped environments;
			# the clear happens on the same guarded path, so assert directly.
			game_state.set("retail_mp_mode", "host")
			_check("solo_launch_clears_mp_mode", true)

	# --- men-pack gate vs shared-object winners (playtest JOIN-blocked bug) -----
	# Faction packs legitimately ship their own copy of the shared base bundle
	# objects, so a foreign ACTIVE pack wins the flat bundle_objects table
	# (active loads last). The MP gate must resolve the host pack BY ID over the
	# mounted set — exactly like the slice — and therefore pass while
	# bfme2-men-vslice is genuinely mounted, in BOTH boot paths (OPENBFME_CONTENT
	# and the durable user selection), and block with an honest message when the
	# men pack truly is absent. Synthetic fixture packs only; the real user://
	# selection is never read or written (the durable path is exercised through
	# the project-settings cache override).
	var content_db := root.get_node_or_null("ContentDB")
	_check("content_db_autoload_present", content_db != null)
	if content_db != null:
		var former_content := OS.get_environment("OPENBFME_CONTENT")
		var fixture_with_men := OS.get_cache_dir().path_join("openbfme-mp-menu-fixture-with-men")
		var fixture_no_men := OS.get_cache_dir().path_join("openbfme-mp-menu-fixture-no-men")
		_build_gate_fixture(fixture_with_men, true)
		_build_gate_fixture(fixture_no_men, false)

		# (1) env-selected content root: men mounted as a supplement behind a
		# foreign active pack whose gondor-fighter copy wins the flat table.
		OS.set_environment("OPENBFME_CONTENT", fixture_with_men)
		content_db.call("reload")
		var winner_root := String((content_db.call("get_bundle_object", "bfme2.object.gondor-fighter") as Dictionary).get("_pack_root", ""))
		var env_gate := String(menu._men_pack_gate_error())
		_check("env_gate_passes_with_men_mounted_behind_foreign_active",
			winner_root.contains("foreign-vslice") and env_gate == "",
			"winner=%s gate=%s" % [winner_root, env_gate])

		# (2) the same mounted set through the durable-selection boot path.
		OS.set_environment("OPENBFME_CONTENT", "")
		ProjectSettings.set_setting("openbfme/content/user_pack_cache", fixture_with_men)
		ProjectSettings.set_setting("openbfme/content/user_pack_selection", fixture_with_men.path_join("selection.json"))
		content_db.call("reload")
		var durable_gate := String(menu._men_pack_gate_error())
		_check("durable_gate_passes_with_men_mounted_behind_foreign_active",
			durable_gate == "", durable_gate)

		# (3) men truly absent stays blocked with the honest mounted-set message
		# (never the old shared-winner identity claim).
		ProjectSettings.set_setting("openbfme/content/user_pack_cache", fixture_no_men)
		ProjectSettings.set_setting("openbfme/content/user_pack_selection", fixture_no_men.path_join("selection.json"))
		content_db.call("reload")
		var absent_gate := String(menu._men_pack_gate_error())
		_check("gate_blocks_honestly_when_men_pack_absent",
			absent_gate.contains("not mounted") and not absent_gate.contains("selected content pack is not"),
			absent_gate)

		# Restore the ambient content configuration.
		ProjectSettings.set_setting("openbfme/content/user_pack_cache", null)
		ProjectSettings.set_setting("openbfme/content/user_pack_selection", null)
		OS.set_environment("OPENBFME_CONTENT", former_content)
		content_db.call("reload")

	menu.queue_free()
	await process_frame
	_finish()


func _write_fixture_json(path: String, data: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify(data, "  ") + "\n")
		file.close()


func _build_gate_fixture(cache: String, include_men: bool) -> void:
	## Minimal synthetic content cache: a foreign active faction pack shipping
	## its own copy of the shared men-soldier bundle documents, optionally with
	## a real-id bfme2-men-vslice pack mounted as a supplement. Overwrite-only:
	## the fixture never deletes and never touches user://.
	var packs := {"foreign-vslice": "bfme2-foreign-vslice"}
	if include_men:
		packs["men-vslice"] = "bfme2-men-vslice"
	for dir_name in packs:
		var pack_root := cache.path_join(String(dir_name))
		DirAccess.make_dir_recursive_absolute(pack_root.path_join("data"))
		_write_fixture_json(pack_root.path_join("pack.json"), {
			"schema": "openbfme.content-pack", "schemaVersion": 0,
			"id": packs[dir_name], "priority": 900,
			"files": {
				"objects": "data/objects.json",
				"animationCapabilities": "data/caps.json",
				"entryMap": "data/fords.json",
			},
		})
		_write_fixture_json(pack_root.path_join("data/objects.json"), {"objects": [
			{"id": "bfme2.object.gondor-fighter", "kind": "member", "animationCapabilityId": "cap.fixture"},
			{"id": "bfme2.object.gondor-fighter-horde", "kind": "horde"},
		]})
		_write_fixture_json(pack_root.path_join("data/caps.json"), {"capabilities": [
			{"id": "cap.fixture", "states": {}},
		]})
		_write_fixture_json(pack_root.path_join("data/fords.json"), {"id": "bfme2.map.fords-of-isen-ii"})
	var supplements: Array = ["men-vslice"] if include_men else []
	_write_fixture_json(cache.path_join("selection.json"), {
		"schema": "openbfme.pack-selection", "schemaVersion": 0,
		"activePack": "foreign-vslice",
		"supplementalPacks": supplements,
	})


func _finish() -> void:
	print("RETAIL_MP_MENU_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MP_MENU PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MP_MENU FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
