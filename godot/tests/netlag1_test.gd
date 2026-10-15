## Headless test of the command delay through the HUD in a LAN game (lane NETLAG-1): a host and a joiner play 60 logic frames; at frame 20 the host
## presses its fortress's builder button through the HUD (game.gd --net-press: InGameHudNode.press_command_button, the ControlBar's press into the
## lockstep) and logs when the unit reaches the queue. Every local command of both peers must run one frame after the frame it was issued on
## (RotWK's frame + 1, RW 0x8D4301), the run-ahead must stay at the lobby's 1, and the game must end in lockstep.
##
##   godot --headless --path godot --script res://tests/netlag1_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("NETLAG1 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


# the JSON of the last "GAME NET status: " line of a log, {} without one
func _status(log: String) -> Dictionary:
	var at := log.rfind("GAME NET status: ")
	if at < 0:
		return {}
	var line := log.substr(at + 17, log.find("\n", at) - at - 17)
	var parsed: Variant = JSON.parse_string(line)
	return parsed if parsed is Dictionary else {}


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("NETLAG1 SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var godot_path := OS.get_executable_path()
	var project := ProjectSettings.globalize_path("res://")
	var dir := OS.get_user_data_dir()
	DirAccess.make_dir_recursive_absolute(dir)
	var port := 33000 + (Time.get_ticks_msec() % 2000)
	var common := ["--headless", "--path", project]
	var host_args: Array = common + ["--log-file", "%s/netlag1_host.log" % dir, "--", "--net-host=%d" % port, "--seed=9",
		"--net-slots=human:FactionMen:0:0;human:FactionMordor:1:1", "--net-crc=10", "--net-frames=60", "--net-press=MenPorter", "--no-record"]
	var join_args: Array = common + ["--log-file", "%s/netlag1_join.log" % dir, "--", "--net-join=127.0.0.1:%d" % port, "--net-name=Joiner",
		"--net-script", "--net-frames=60", "--no-record"]
	for who in ["host", "join"]:
		DirAccess.remove_absolute("%s/netlag1_%s.log" % [dir, who])
	var pids := { "host": OS.create_process(godot_path, PackedStringArray(host_args)) }
	OS.delay_msec(3000)
	pids["join"] = OS.create_process(godot_path, PackedStringArray(join_args))
	var codes := {}
	var deadline := Time.get_ticks_msec() + 600000
	for who in ["host", "join"]:
		var pid: int = pids[who]
		while pid > 0 and OS.is_process_running(pid) and Time.get_ticks_msec() < deadline:
			OS.delay_msec(200)
		codes[who] = str(OS.get_process_exit_code(pid)) if pid > 0 and not OS.is_process_running(pid) else ""
		if pid > 0 and OS.is_process_running(pid):
			OS.kill(pid)
	var finals := {}
	for who in ["host", "join"]:
		var log := FileAccess.get_file_as_string("%s/netlag1_%s.log" % [dir, who])
		var rc: String = codes[who]
		_check(rc == "0", "%s exits 0 (got '%s')" % [who, rc])
		var st := _status(log)
		_check(not st.is_empty(), "%s: the network status at frame 60" % who)
		_check(st.get("desyncs", [1]).is_empty(), "%s: no desync" % who)
		_check(st.get("errors", [1]).is_empty(), "%s: no network error (%s)" % [who, str(st.get("errors", "none"))])
		_check(int(st.get("frame", -1)) >= 60, "%s: reached frame 60 (at %s)" % [who, str(st.get("frame", "none"))])
		# the game's CRC exchange every 10 frames (--net-crc=10: with the default 100 nothing is compared in 60 frames); every one of them passed
		_check(int(st.get("crc_checks_passed", 0)) == 5, "%s: the CRC checks of frames 10, 20, 30, 40 and 50 all passed (got %s)" % [who, str(st.get("crc_checks_passed", "none"))])
		finals[who] = [int(st.get("hash_frame", -1)), int(st.get("hash", 0))]
		_check(int(st.get("run_ahead", -1)) == 1, "%s: the run-ahead stayed at the lobby's 1 (got %s)" % [who, str(st.get("run_ahead", "none"))])
		var delays: Array = st.get("command_delay_frames", [])
		var all_one := not delays.is_empty()
		for d in delays:
			all_one = all_one and int(d) == 1
		_check(all_one, "%s: every local command ran one frame after its issue frame (%d commands: %s)" % [who, delays.size(), str(delays)])
		print("    %s: issue -> batch %s; issue -> effect %s" % [who, st.get("input_latency_ms", "?"), st.get("command_effect_ms", "?")])
		if who == "host":
			var at := log.find("GAME NET PRESS ENTRY ")
			_check(log.contains("GAME NET PRESS MenPorter pressed on") and at >= 0, "host: the HUD's press reached the producer's queue")
			if at >= 0:
				var line := log.substr(at, log.find("\n", at) - at)
				print("    ", line)
				# the exact entry: one MenPorter appended to an empty queue; it first showed in the world two logic frames after the one the press was made on
				# (issued on frame P, stamped P + 1 = the run-ahead 1, run by batch P + 1 whose result is frame P + 2), not at P + 1
				var f := line.split(" ")
				var pressed := int(f[f.find("frame_pressed") + 1])
				var seen := int(f[f.find("frame_seen") + 1])
				_check(f[f.find("template") + 1] == "MenPorter" and int(f[f.find("quantity_total") + 1]) == 1 and int(f[f.find("quantity_produced") + 1]) == 0, "host: the queue entry is one MenPorter, none produced yet")
				_check(int(f[f.find("queue_before") + 1]) == 0 and int(f[f.find("queue_after") + 1]) == 1, "host: the queue went from empty to that one entry")
				_check(seen - pressed == 2, "host: the entry first showed on logic frame press + 2 (pressed %d, seen %d)" % [pressed, seen])
		if rc != "0":
			for line in log.split("\n"):
				if line.begins_with("GAME FAIL") or line.contains("handle_crash") or line.contains("GAME NET PRESS"):
					print("    ", line)
	# both peers' state hash of the same logic frame (a CRC of the whole game state, not the game's own exchange)
	_check(finals.has("host") and finals.has("join") and int(finals["host"][0]) > 0, "both peers reported a state hash")
	if finals.has("host") and finals.has("join"):
		_check(finals["host"] == finals["join"], "both peers' state hash of one logic frame is equal (host %s, joiner %s)" % [str(finals["host"]), str(finals["join"])])
	return 0 if _failures == 0 else 1
