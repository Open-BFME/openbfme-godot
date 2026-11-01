## Headless local-log checks; no game files, credentials or network required.
extends SceneTree

const Log := preload("res://scripts/core/launcher_log.gd")
var checks := 0
var failed := 0


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("PRIVACY FAIL ", label)


func _initialize() -> void:
	Log._home = "/srv/players/Fixture Player"
	Log._user = "Fixture Player"
	for path in [Log._home + "/logs/crash.log", "/" + "home/Fixture Player/Games", "/" + "Users/Fixture Player/Library",
		"C:/" + "Users/Fixture Player/AppData", "d:\\users\\Fixture Player\\Games", "/media/Fixture Player/Games"]:
		var clean: String = Log.scrub("ERROR " + path + "\nusername=Fixture Player")
		check(not clean.contains("Fixture Player") and clean.contains("<user>"), "all profile spellings")
	for field in ["Authorization: Bearer synthetic value", "cookie: session=synthetic", "password=two words", "access_token=synthetic",
		"refresh-token: synthetic", "sessionKey=synthetic", "api_key='two words'", "secret=synthetic", "player_name=Fixture Player",
		"displayName=Fixture Player", "GAME ONLINE signed in as Fixture Player (guest)", "GAME ONLINE chat Fixture Player: file contents",
		"session key=synthetic", "authToken=synthetic", "player name=Fixture Player", "GAME ONLINE remembered name Fixture Player: saved",
		"GAME ONLINE WALK games: Fixture Player"]:
		var clean: String = Log.scrub(field)
		check(clean.contains(Log.OMITTED) and not clean.contains("synthetic") and not clean.contains("two words") and not clean.contains("Fixture Player"), "only private values omitted")
		check(Log.scrub(clean) == clean, "redaction is idempotent")
	check(Log.scrub("contact fixture@example.invalid at 192.0.2.10 or [2001:db8::10]") == "contact <address> at <address> or [<address>]", "addresses")
	check(Log.scrub("connect https://fixture.invalid/path?value=synthetic") == "connect https://fixture.invalid/path?value=synthetic", "ordinary URL preserved")
	check(Log.scrub("12:34:56 std::function") == "12:34:56 std::function", "timestamps and function names remain diagnostic")
	check(Log.scrub("ERROR: token: synthetic; retry failed") == "ERROR: token: " + Log.OMITTED + "; retry failed", "prefixed credential field redacted")
	check(Log.scrub("response eyJmaXh0dXJlIjoiMSJ9.eyJmaXh0dXJlIjoyfQ.c3ludGhldGlj failed") == "response " + Log.OMITTED + " failed", "recognizable JWT redacted without hiding hashes")
	for diagnostic in ["commit 0123456789abcdef0123456789abcdef01234567", "engine id abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcd",
		"engine/src/GameClient/AnimationSoundClientBehavior.cpp:322", "res://scripts/parser.gd:7 Unexpected token in expression",
		"Unexpected token: '}'", "version 1.2.3.4", "Godot 4.7.2.0", "v1.2.3.4", "version 999.999.999.999"]:
		check(Log.scrub(diagnostic) == diagnostic, "diagnostic identifiers and parser messages survive")
	check(Log.scrub("version 1.2.3.4 peer 192.0.2.10 port 3000") == "version 1.2.3.4 peer <address> port 3000", "version versus address")
	check(Log.scrub("peer=1.2.3.4") == "peer=<address>", "unlabelled address remains private")
	check(Log.scrub("peer 192.0.2.10.") == "peer <address>.", "address with sentence punctuation")
	check(Log.scrub("connect https://Fixture:synthetic@fixture.invalid/path") == "connect https://" + Log.OMITTED + "@fixture.invalid/path", "URL credentials scrubbed without losing host or path")
	check(Log.scrub("https://fixture.invalid/log?access_token=synthetic&version=1.2.3.4") == "https://fixture.invalid/log?access_token=" + Log.OMITTED + "&version=1.2.3.4", "credential query value only")
	check(Log.scrub("Load /srv/players/Fixture Player/log; token='synthetic'; retry failed") == "Load <user>/log; token='" + Log.OMITTED + "'; retry failed", "message context retained")
	check(Log.scrub("x".repeat(Log.MAX_FIELD_BYTES + 1)) == Log.OVERSIZED, "field cap")
	check(Log.scrub("x".repeat(Log.MAX_LOG_BYTES + 1)) == Log.OVERSIZED, "report cap")
	Log.open()
	var path := OS.get_user_data_dir().path_join("launcher.log")
	if Log._file != null:
		Log._home = "/srv/players/Fixture Player"
		Log._user = "Fixture Player"
		Log.line("mount /srv/players/Fixture Player/Games")
		Log.line("Authorization: Bearer synthetic value")
		var text := FileAccess.get_file_as_string(path)
		check(not text.contains("Fixture Player") and not text.contains("synthetic value"), "disk is scrubbed and flushed")
		var header := "OpenBFME version 1.2.3.4 commit 0123456789abcdef0123456789abcdef01234567"
		Log.line(header)
		for i in 400:
			Log.line("record %d %s" % [i, "x".repeat(8000)])
			if Log._file == null or Log._file.get_length() > Log.MAX_LOG_BYTES:
				check(false, "file bounded throughout multiple in-process rotations")
				quit(1)
				return
		Log.line("ERROR final crash")
		text = FileAccess.get_file_as_string(path)
		check(text.contains(header), "build header retained")
		check(text.contains("record 399 ") and text.ends_with("ERROR final crash\n"), "newest records retained after rotations")
		check(not text.contains("record 100 "), "old tail evicted")
		check(text.to_utf8_buffer().size() == Log._file.get_length() and Log._file.get_length() <= Log.MAX_LOG_BYTES, "disk cap includes metadata")
		Log.open()
		Log.line("after restart")
		text = FileAccess.get_file_as_string(path)
		check(text.contains(header) and text.contains("ERROR final crash") and text.ends_with("after restart\n"), "retained log reopens without loss")
		Log._file.seek(0)
		Log._file.store_string("legacy header\n" + "x".repeat(Log.MAX_LOG_BYTES))
		Log._file.flush()
		Log.line("after oversized legacy line")
		text = FileAccess.get_file_as_string(path)
		check(text.begins_with("legacy header\n") and text.ends_with("after oversized legacy line\n") and text.to_utf8_buffer().size() <= Log.MAX_LOG_BYTES, "legacy unbroken tail cannot loop or exceed the cap")
		Log._file.close()
		Log._file = null
	else:
		check(false, "log opened")
	Log.line("logger closed")
	check(Log._file == null, "closed sink does not recurse or reopen")
	print("PRIVACY ", "OK" if failed == 0 else "FAIL", " ", checks, " checks; ", failed, " failed")
	quit(0 if failed == 0 else 1)
