## Lane UBERJOIN-1: the GameWorld's performance mode through its real lifecycle (headless, against the retail files):
##   * set_simulation_profile really switches (Normal -> Uber and back: it once answered ok and stayed Normal);
##   * a lobby's switch for one game (switch_simulation_profile_for_game) keeps the player's setting and restore_simulation_profile undoes it, also after the
##     option changed meanwhile; "the game is over" (restore) and "the room left / nothing to play" (leave_lan_room_and_restore_profile) both end it;
##   * an online START of an Uber player is prepared as an Uber game (it once failed with the named profile mismatch), a Normal one as Normal;
##   * a new world (a restarted game after a crash) starts at the player's setting: nothing was written anywhere.
##
##   godot --headless --path godot --script res://tests/uberjoin1_test.gd
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("UBERJOIN1 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _world(fs: RefCounted) -> Node3D:
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok, "the object world loads")
	return world


## a player's START as the lobby builds it (OnlineStart.h): slot 0 of two players; the ticket is a version 1 ticket for slot 0 (the lobby's HMAC is the relay's to check)
func _start_json(profile_digest: String) -> String:
	var ticket := PackedByteArray()
	ticket.resize(32)
	ticket[0] = 1
	ticket[10] = 0
	ticket[11] = 0x03 # the slot mask: slots 0 and 1
	var b64 := Marshalls.raw_to_base64(ticket).replace("+", "-").replace("/", "_").replace("=", "")
	return JSON.stringify({"v": 1, "kind": "openbfme-start", "game": "0123456789abcdef", "slot": 0, "relay": "127.0.0.1:7350", "ticket": b64,
		"runAhead": 2, "crcInterval": 100, "profile": profile_digest,
		"setup": {"map": "maps/map mp evendim/map mp evendim.map", "seed": 5, "startingCash": 1500, "superweaponRestriction": 0, "rules": null,
			"legendaryHeroes": 1,
			"slots": [{"state": "player", "name": "A", "faction": 3, "color": 0, "startPos": 0, "team": -1},
				{"state": "player", "name": "B", "faction": 4, "color": 1, "startPos": 1, "team": -1}]}})


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	if not ClassDB.class_exists("GameWorld"):
		print("FAIL: the openbfme extension is not loaded")
		return 1
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount: ", mount.errors)
		return 1
	var world := _world(fs)
	# ---- the setter really switches ----
	_check(world.simulation_profile() == "faithful" and not world.profile_switched(), "a new world runs the player's setting: faithful, nothing switched")
	var normal_digest: String = world.net_profile_digest()
	var r: Dictionary = world.set_simulation_profile("uber")
	_check(r.ok and world.simulation_profile() == "uber", "set_simulation_profile(uber) from faithful: ok AND uber (%s)" % str(r))
	_check(not world.profile_switched(), "the setting is not a switch")
	var uber_digest: String = world.net_profile_digest()
	_check(uber_digest != normal_digest, "the identity digest follows the profile")
	r = world.set_simulation_profile("faithful")
	_check(r.ok and world.simulation_profile() == "faithful" and world.net_profile_digest() == normal_digest, "and back to faithful")
	r = world.set_simulation_profile("fast")
	_check(not r.ok and str(r.get("error", "")).contains("faithful or uber") and world.simulation_profile() == "faithful", "an unknown name is refused by name: %s" % str(r))
	# ---- a lobby's switch for one game ----
	r = world.switch_simulation_profile_for_game("uber")
	_check(r.ok and world.simulation_profile() == "uber" and world.profile_switched() and world.net_profile_digest() == uber_digest, "a lobby's switch to uber: uber, switched")
	r = world.set_simulation_profile("faithful") # the option is still Normal: nothing changes for the game
	_check(r.ok and world.simulation_profile() == "uber" and world.profile_switched(), "the option set while switched: the game's mode stands")
	world.restore_simulation_profile()
	_check(world.simulation_profile() == "faithful" and not world.profile_switched(), "restore: the player's setting (faithful) is back")
	world.set_simulation_profile("uber") # the option now says Uber
	world.switch_simulation_profile_for_game("faithful")
	_check(world.simulation_profile() == "faithful" and world.profile_switched(), "an Uber player's switch to normal for a game")
	world.leave_lan_room_and_restore_profile() # no LAN room: the same as the restore
	_check(world.simulation_profile() == "uber" and not world.profile_switched(), "the room left / nothing to play: back to the player's uber")
	r = world.switch_simulation_profile_for_game("sideways")
	_check(not r.ok and world.simulation_profile() == "uber", "an unknown switch is refused by name: %s" % str(r.get("error", "")))
	# ---- the LAN lobby follows: a switch changes what it announces and sends at a join, the restore puts it back ----
	world.set_simulation_profile("faithful")
	var lan: Dictionary = world.lan_open({"name": "LanTest", "broadcast": false, "port_base": 47100 + (Time.get_ticks_msec() % 300) * 2})
	_check(lan.ok, "a LAN lobby opens (%s)" % str(lan.get("error", "")))
	if lan.ok:
		var own_identity: String = world.lan_status().identity
		_check(own_identity == world.net_profile_digest(), "the lobby announces the player's identity")
		world.switch_simulation_profile_for_game("uber")
		_check(world.lan_status().identity == world.net_profile_digest() and world.lan_status().identity != own_identity, "a switch: the lobby announces the Uber identity")
		world.restore_simulation_profile()
		_check(world.lan_status().identity == own_identity, "the restore: the player's identity again")
		world.lan_close()
	# ---- an online START is prepared under the profile of its identity ----
	for mode in ["faithful", "uber"]:
		world.set_simulation_profile(mode)
		r = world.net_online(_start_json(world.net_profile_digest()))
		_check(r.ok, "%s: net_online accepts the START (%s)" % [mode, str(r)])
		var poll: Dictionary = world.net_poll()
		_check(poll.state == "started" and poll.has("message"), "%s: the START is a started game (%s)" % [mode, str(poll.get("state"))])
		if poll.get("message") == null:
			continue
		_check(int(poll.message.get("simulation_profile", -1)) == (1 if mode == "uber" else 0), "%s: the game's simulation_profile is the identity's" % mode)
		var prep: Dictionary = world.prepare_new_game(poll.message)
		_check(prep.ok, "%s: prepare_new_game %s" % [mode, str(prep.get("errors", []))])
	# a START of the other mode's identity is a refusal by name, not a game of the wrong profile
	world.set_simulation_profile("uber")
	r = world.net_online(_start_json(normal_digest))
	_check(not r.ok and str(r.get("error", "")).contains("different OpenBFME version or different game files"), "a normal START for an uber player is refused by name: %s" % str(r.get("error", "")).left(120))
	# ---- a restarted game (a crash) is at the player's setting: the switch was never written ----
	world.set_simulation_profile("faithful")
	world.switch_simulation_profile_for_game("uber")
	world.queue_free()
	await process_frame
	var again := _world(fs)
	_check(again.simulation_profile() == "faithful" and not again.profile_switched(), "a new world after a switched game: the setting, not the switch")
	again.queue_free()
	return 0 if _failures == 0 else 1
