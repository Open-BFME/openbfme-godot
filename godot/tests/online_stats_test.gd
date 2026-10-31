## OpenBFME. GPL-3.0.
## Lane ONLINECOUNT-1: scripts/online_stats.gd against a fake lobby (tools/net/test_online_stats.py plays it and checks the requests it saw).
## Run with -- --lobby=<fake lobby: /ok/v1/stats answers {"v":1,"players":3,"games_open":1,"games_running":2,"as_of":..}, /e404/ 404, /bad/ garbage,
## /float/ 1.5 players, /neg/ negative, /huge/ 1 MiB, /redirect/ 302 to /redirected/ (which must never be asked), /slow/ answers after 3 s, /down/ stops answering, /switch/ answers
## ok once and then 503>. One "STATS ok|FAIL" line per check; exit 0 when all hold.
extends SceneTree

const OnlineStats := preload("res://scripts/online_stats.gd")

var _ok := true
var _lobby := ""


func _init() -> void:
	_run.call_deferred()


func _check(what: String, cond: bool, detail := "") -> void:
	print("STATS %s %s%s" % ["ok  " if cond else "FAIL", what, ": " + detail if not cond or detail != "" else ""])
	_ok = _ok and cond


## timeout: 1.5 s only where a timeout is the point (/slow answers after 3 s). Every other answer gets 10 s: HTTPRequest's timeout also counts
## blocking frames, so on a loaded gate host the process's first request ran out at 1.5 s ("no answer: 13 / 0", srv1 om1node3-srv1).
func _make(path: String, tokens := "", timeout := 10.0) -> Node:
	var made: Dictionary = OnlineStats.start(root, _lobby + path, func() -> String: return tokens, PackedStringArray(), "", {}, "", false)
	var n: Node = made.node
	n.min_gap_s = 0.0
	n.timeout_s = timeout
	n._http.timeout = timeout
	return n


## the answer's text after the mode was set ("" = nothing shown); waits until the request was answered or failed
func _asked(n: Node, mode: String) -> String:
	var before: int = n.requests_sent
	n.set_mode(mode)
	var until := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < until and (n.requests_sent == before or n._in_flight):
		await process_frame
	await process_frame
	return n.text


func _run() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--lobby="):
			_lobby = a.substr(8)
	# the answer, per mode: the main menu says the players; the online menu adds the open games
	var n := _make("/ok", "MENU-MUST-NOT-SEND")
	_check("main menu line", await _asked(n, "menu") == "3 players online", n.text)
	n.set_mode("")
	_check("no mode shows nothing", n.text == "")
	var o := _make("/ok", "SESSION-TOKEN-1")
	_check("online menu line", await _asked(o, "online") == "3 players online, 1 game open", o.text)
	# the line is made from whole counts, singular and plural
	_check("one player", OnlineStats.line({"players": 1, "games_open": 2}, true) == "1 player online, 2 games open")
	_check("no players", OnlineStats.line({"players": 0, "games_open": 0}, false) == "0 players online")
	# every answer that is not a stats answer shows nothing
	for bad in ["/e404", "/bad", "/float", "/neg", "/huge", "/redirect", "/slow"]:
		var b := _make(bad, "", 1.5 if bad == "/slow" else 10.0)
		var shown := await _asked(b, "menu")
		_check("%s shows nothing" % bad, shown == "" and b.requests_sent == 1, "shown '%s', requests %d" % [shown, b.requests_sent])
	# parse: the closed set of what a stats answer may be
	var good := '{"v":1,"players":2,"games_open":0,"games_running":1,"as_of":5}'
	_check("parse good", OnlineStats.parse(200, good).ok)
	for body in ['{"v":2,"players":2,"games_open":0,"games_running":1}', '{"players":2,"games_open":0,"games_running":1}', '{"v":1,"players":"2","games_open":0,"games_running":1}',
			'{"v":1,"players":2,"games_open":0}', '{"v":1,"players":-1,"games_open":0,"games_running":1}', '{"v":1,"players":2.5,"games_open":0,"games_running":1}',
			'{"v":1,"players":99999999999,"games_open":0,"games_running":1}', '[1,2]', 'null', '', '{"v":1,"players":null,"games_open":0,"games_running":1}']:
		_check("parse refuses %s" % body.left(50), not OnlineStats.parse(200, body).ok)
	_check("parse refuses a 404 with a good body", not OnlineStats.parse(404, good).ok)
	# a number that was shown is never kept past a failure
	var s := _make("/switch")
	_check("switch: first answer", await _asked(s, "menu") == "3 players online", s.text)
	s.set_mode("")
	_check("switch: second asks again and fails", await _asked(s, "menu") == "", s.text)
	# a mode change within the gap asks nothing new
	var g := _make("/ok")
	g.min_gap_s = 30.0
	await _asked(g, "menu")
	g.set_mode("")
	g.set_mode("menu")
	g.set_mode("online")
	await create_timer(0.5).timeout
	_check("the gap holds", g.requests_sent == 1, str(g.requests_sent))
	_check("the other menu shows the last answer in its own form", g.text == "3 players online, 1 game open", g.text)
	# r2 (Sol): a window in the background asks nothing, and a line the lobby has not renewed for two intervals disappears
	var vis := _make("/vis")
	vis.menu_s = 0.4
	vis.min_gap_s = 0.1
	var shown_visible := [true] # (a lambda copies a local: the array is shared)
	vis.visible_check = func() -> bool: return shown_visible[0]
	_check("visible: asked", await _asked(vis, "menu") == "3 players online", vis.text)
	shown_visible[0] = false
	var asked: int = vis.requests_sent
	await create_timer(1.6).timeout
	_check("in the background: nothing asked", vis.requests_sent == asked, "%d -> %d" % [asked, vis.requests_sent])
	_check("in the background: the old line expired", vis.text == "", vis.text)
	shown_visible[0] = true
	await create_timer(1.6).timeout
	_check("back in view: asked again, shown again", vis.requests_sent > asked and vis.text == "3 players online", "%d %s" % [vis.requests_sent, vis.text])
	vis.set_mode("")
	# lane ONLINEUX-1 r2 (Sol 5): an answer that changes only the running games keeps the line's text but reaches the ticker (counts_changed)
	var run := _make("/running", "T")
	run.online_s = 0.3
	var seen_counts := []
	run.counts_changed.connect(func(c: Dictionary) -> void: seen_counts.append(int(c.get("games_running", -1))))
	_check("running: the first answer", await _asked(run, "online") == "3 players online, 1 game open" and seen_counts == [2], "%s %s" % [run.text, str(seen_counts)])
	var texts := [0]
	run.changed.connect(func(_t: String) -> void: texts[0] += 1)
	var until := Time.get_ticks_msec() + 5000
	while Time.get_ticks_msec() < until and (run.requests_sent < 2 or run._in_flight):
		await process_frame
	await process_frame
	_check("running: the next poll changes the running games only: the ticker hears it, the line stays", run.requests_sent >= 2 and seen_counts == [2, 5]
		and texts[0] == 0 and run.text == "3 players online, 1 game open" and int(run.counts().get("games_running", -1)) == 5,
		"requests %d counts %s text changes %d '%s'" % [run.requests_sent, str(seen_counts), texts[0], run.text])
	run.set_mode("")
	# r2 (Sol): a launcher setting that is not true / false is a damaged file: off, said by name; a missing key or an older file is on
	for bad in ['"yes"', '"false"', '[1]', '{"a": 1}', '1', '0']:
		var why: String = OnlineStats.disabled_reason(PackedStringArray(), "", {}, "[launcher]\nshow_online_count=%s\n" % bad, false)
		_check("malformed %s is off" % bad, why.contains("not true or false") or why.contains("garbled"), why)
	for ok in ["[launcher]\nchannel=\"stable\"\n", "", "[other]\nshow_online_count=false\n", "[launcher]\nshow_online_count=true\n"]:
		_check("on: %s" % ok.c_escape().left(50), OnlineStats.disabled_reason(PackedStringArray(), "", {}, ok, false) == "")
	_check("explicit false is off", OnlineStats.disabled_reason(PackedStringArray(), "", {}, "[launcher]\nshow_online_count=false\n", false) != "")
	# off: nothing is made, so nothing is ever asked
	for off in [[PackedStringArray(["--no-online-count"]), "", {}, ""], [PackedStringArray(), "1", {}, ""], [PackedStringArray(), "", {"OpenBFMEShowOnlineCount": "No"}, ""],
			[PackedStringArray(), "", {}, "[launcher]\nshow_online_count=false\n"]]:
		var made: Dictionary = OnlineStats.start(root, _lobby + "/off", func() -> String: return "T", off[0], off[1], off[2], off[3], false)
		_check("off: %s" % str(off).left(60), made.why != "" and not made.has("node"), str(made))
	var scripted: Dictionary = OnlineStats.start(root, _lobby + "/off", Callable(), PackedStringArray(), "", {}, "", true)
	_check("a scripted run without a lobby is off", scripted.why != "" and not scripted.has("node"))
	_check("on by default", OnlineStats.disabled_reason(PackedStringArray(), "", {}, "[launcher]\nshow_online_count=true\n", false) == "")
	_check("an unreadable launcher file does not switch it off", OnlineStats.disabled_reason(PackedStringArray(), "", {}, "garbage[=\n[[launcher", false) == "")
	_check("the stop is named", OnlineStats.STOP_LINE.begins_with("[S-4831]") and OnlineStats.STOP_LINE.contains("OpenBFMEShowOnlineCount"))
	await create_timer(0.3).timeout
	quit(0 if _ok else 1)
