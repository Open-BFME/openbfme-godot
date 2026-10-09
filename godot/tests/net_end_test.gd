## Headless test of the end of a LAN game (lanes END-1 / END-2): a host and a joiner play 40 logic frames (scripted players), then the joiner surrenders
## through the quit menu (Esc, Forfeit, the confirmation: MSG_SELF_DESTRUCT on the lockstep); the joiner sees the defeat, the host the victory; both leave
## through the quit menu's Exit to the score screen (type 3, GameWorld.clear_game_data releases the session), and Continue opens the LAN lobby again, where
## a second LAN game starts and runs 20 frames without a desync.
##
##   godot --headless --path godot --script res://tests/net_end_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("NETEND %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("NETEND SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var godot_path := OS.get_executable_path()
	var project := ProjectSettings.globalize_path("res://")
	var dir := OS.get_user_data_dir()
	DirAccess.make_dir_recursive_absolute(dir)
	var port := 31000 + (Time.get_ticks_msec() % 2000)
	# two processes started directly (no shell: lane WIN-1 runs this under Windows too), each logging through Godot's own --log-file
	var common := ["--headless", "--path", project]
	var host_args: Array = common + ["--log-file", "%s/host.log" % dir, "--", "--net-host=%d" % port, "--seed=9",
		"--net-slots=human:FactionMen:0:0;human:FactionMordor:1:1", "--net-script", "--net-frames=40", "--net-end"]
	var join_args: Array = common + ["--log-file", "%s/join.log" % dir, "--", "--net-join=127.0.0.1:%d" % port, "--net-name=Joiner", "--net-script",
		"--net-frames=40", "--net-end"]
	for who in ["host", "join"]:
		DirAccess.remove_absolute("%s/%s.log" % [dir, who])
	var pids := { "host": OS.create_process(godot_path, PackedStringArray(host_args)) }
	OS.delay_msec(3000)
	pids["join"] = OS.create_process(godot_path, PackedStringArray(join_args))
	var codes := {}
	var deadline := Time.get_ticks_msec() + 900000
	for who in ["host", "join"]:
		var pid: int = pids[who]
		while pid > 0 and OS.is_process_running(pid) and Time.get_ticks_msec() < deadline:
			OS.delay_msec(200)
		codes[who] = str(OS.get_process_exit_code(pid)) if pid > 0 and not OS.is_process_running(pid) else ""
		if pid > 0 and OS.is_process_running(pid):
			OS.kill(pid)
	for who in ["host", "join"]:
		var log := FileAccess.get_file_as_string("%s/%s.log" % [dir, who])
		var rc: String = codes[who]
		_check(rc == "0", "%s exits 0 (got '%s')" % [who, rc])
		var surrendered: bool = who == "join"
		if surrendered:
			_check(log.contains("\"popup_type\":\"Forfeit\""), "join: the quit menu offers Forfeit in a LAN game (RW 0x9216EC) with Save and Load disabled (RW 0x921D5B)")
			_check(log.contains("GAME QUIT action: QuitMenuForfeit") and log.contains("GAME QUIT surrender: {\"ok\":true"), "join: Forfeit sent MSG_SELF_DESTRUCT(false) on the lockstep")
		_check(log.contains("GAME NET END end screen: shown true, victory %s" % ("false" if surrendered else "true")), "%s: the %s screen" % [who, "defeat" if surrendered else "victory"])
		_check(log.contains("GAME QUIT action: QuitMenuExit"), "%s: left through the quit menu's Exit" % who)
		_check(log.contains("GAME NET END score screen type 3") and log.contains("local result %d" % (1 if surrendered else 0)), "%s: the score screen is type 3 with the local result %s" % [who, "defeated" if surrendered else "victorious"])
		_check(log.contains("games started 2") and log.contains("net active true"), "%s: Continue opened the LAN lobby again and a second LAN game runs" % who)
		_check(log.contains("GAME NET END RESULT: ok"), "%s: the whole flow" % who)
		if rc != "0":
			for line in log.split("\n"):
				if line.begins_with("GAME FAIL") or line.contains("handle_crash") or line.contains("NET END"):
					print("    ", line)
	return 0 if _failures == 0 else 1
