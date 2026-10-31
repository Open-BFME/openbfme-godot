## OpenBFME. GPL-3.0.
## Lane ONLINECOUNT-1: "N players online" in the main menu and in the online menu, from the lobby's GET /v1/stats (services/online/lobby/src/lobby.mjs:
## counts only: players, open games, running games). What it asks, and when:
##   * only while a menu is on screen (set_mode): "menu" (the main menu) asks once when it comes up and then at most every MENU_SECONDS, with NO
##     sign-in token (the lobby learns nothing about the player); "online" (the online menu's pages) asks when it opens and then every ONLINE_SECONDS,
##     with the session token of the player who signed in there (the lobby counts a player it has seen in the last 150 s, so the poll is what
##     keeps them counted; a staging room's own polls do the same, so the staging room asks nothing). "" (a game on screen, a loading screen, anything else) asks nothing, and nothing is ever asked in the background;
##   * never twice within MIN_GAP_SECONDS (the lobby serves one cached answer for 30 s anyway); one request at a time;
##   * never when the player turned it off (disabled_reason: the launcher's setting, Options.ini, --no-online-count, OPENBFME_NO_ONLINE_COUNT=1), and
##     never in a development or scripted run unless a lobby was named (--online-lobby / OPENBFME_LOBBY), as the online screens are.
## An answer that is not exactly {"v": 1, "players": n, "games_open": n, "games_running": n, ...} with whole numbers in range, a 404 (a lobby that has
## no stats yet), a timeout or a refusal show NOTHING (text is ""): never an old number, never a guess, never a box. One `.request(` call, to lobby_url
## (tools/release/net_guard.py), no redirect followed.
extends Node

signal changed(text: String)
## lane ONLINEUX-1 r2 (Sol 5): the counts of the last answer changed (counts(): {} when none is current). The line's text has no running games, so a
## change of games_running alone never reaches `changed`: the online home page's ticker (Games In Progress) follows this one
signal counts_changed(counts: Dictionary)

const MENU_SECONDS := 300.0
const ONLINE_SECONDS := 90.0
const MIN_GAP_SECONDS := 20.0
const TIMEOUT_SECONDS := 8.0
const HttpTimed := preload("res://scripts/http_timed.gd") ## a time limit in running frames (a blocking load does not use it up)
const BODY_LIMIT := 4096
const MAX_COUNT := 10000000
const LAUNCHER_STATE := "OpenBFMELauncher/state.cfg" ## under OS.get_data_dir(): the OpenBFME Launcher's settings file (read only)
const OPTIONS_KEY := "OpenBFMEShowOnlineCount"
const OFF_ARG := "--no-online-count"
## acceptance stop S-4831, printed when the count is switched on (game.gd) and pinned by tests/online_stats_test.gd
const STOP_LINE := "[S-4831] the count is drawn as the project's own Godot text (as the version corner and the online test note), not as an APT text of RotWK's movies; a game started without the launcher has no switch in the menus (--no-online-count, OPENBFME_NO_ONLINE_COUNT=1 or Options.ini OpenBFMEShowOnlineCount; no row in OPTS-5's Options menu yet)"
const OFF_ENV := "OPENBFME_NO_ONLINE_COUNT"

var lobby_url := ""
var token_provider := Callable()  ## returns the session token of the online menu's player ("" when not signed in)
var mode := ""
var visible_check := Callable()   ## returns false while the window is minimized or in the background: nothing is asked then (null: always visible)
var text := ""                    ## what to show ("" = nothing)
var requests_sent := 0            ## tests
var menu_s := MENU_SECONDS        ## (tests shorten these; the game uses the constants)
var online_s := ONLINE_SECONDS
var min_gap_s := MIN_GAP_SECONDS
var timeout_s := TIMEOUT_SECONDS

var _http: HTTPRequest
var _timer: Timer
var _in_flight := false
var _sent_serial := 0
var _mode_serial := 0
var _last_sent_ms := -1000000
var _last := {}                   ## the last good answer, and when it came: shown again at once by the other menu, but never older than MENU_SECONDS
var _last_ms := 0


func _ready() -> void:
	_http = HTTPRequest.new()
	HttpTimed.set_budget(_http, timeout_s)
	_http.max_redirects = 0
	_http.body_size_limit = BODY_LIMIT
	add_child(_http)
	_http.request_completed.connect(_on_done)
	_timer = Timer.new()
	_timer.one_shot = true
	_timer.timeout.connect(_tick)
	add_child(_timer)


## "" when the count may be asked for; else why not (named in the log). `options`: the Options.ini values; `launcher_state`: the launcher's state.cfg
## text ("" when there is none); `scripted`: a development or scripted run without a named lobby
static func disabled_reason(args: PackedStringArray, env_off: String, options: Dictionary, launcher_state: String, scripted: bool) -> String:
	if args.has(OFF_ARG):
		return "turned off (%s)" % OFF_ARG
	if env_off == "1":
		return "turned off (%s=1)" % OFF_ENV
	if _is_off(str(options.get(OPTIONS_KEY, ""))):
		return "turned off (Options.ini %s)" % OPTIONS_KEY
	if not launcher_state.is_empty():
		var cfg := ConfigFile.new()
		if cfg.parse(launcher_state) == OK:
			# a missing key is the default (on: an older launcher); a value that is not true / false is a damaged file: off, and said so
			var v: Variant = cfg.get_value("launcher", "show_online_count") if cfg.has_section_key("launcher", "show_online_count") else true
			if typeof(v) != TYPE_BOOL:
				return "turned off: the launcher's show_online_count is not true or false"
			if not v:
				return "turned off in the launcher's settings"
	if scripted:
		return "a development or scripted run (no lobby named)"
	return ""


## The one place the count is switched on: a node (added to `parent`) that asks the lobby while a menu shows, or {"why"} when it is turned off,
## in which case NOTHING is made and no request can ever be sent
static func start(parent: Node, lobby: String, tokens: Callable, args: PackedStringArray, env_off: String, options: Dictionary,
		launcher_state: String, scripted: bool) -> Dictionary:
	var why := disabled_reason(args, env_off, options, launcher_state, scripted)
	if not why.is_empty():
		return {"why": why}
	var node := new()
	node.name = "OnlineCount"
	node.lobby_url = lobby
	node.token_provider = tokens
	parent.add_child(node)
	return {"why": "", "node": node}


static func _is_off(value: String) -> bool:
	return value.strip_edges().to_lower() in ["no", "off", "false", "0"]


static func read_launcher_state() -> String:
	var path := OS.get_data_dir().path_join(LAUNCHER_STATE)
	if not FileAccess.file_exists(path):
		return ""
	var f := FileAccess.open(path, FileAccess.READ)
	return "" if f == null else f.get_buffer(65536).get_string_from_utf8()


## {ok, players, games_open, games_running} from an answer; {ok: false, why} when it is not a stats answer of version 1
static func parse(code: int, body: String) -> Dictionary:
	if code != 200:
		return {"ok": false, "why": "the lobby answered %d" % code}
	var parser := JSON.new() # (JSON.parse_string prints an engine error for text that is not JSON)
	var j: Variant = parser.data if parser.parse(body) == OK else null
	if typeof(j) != TYPE_DICTIONARY or j.get("v") != 1.0:
		return {"ok": false, "why": "not a version 1 answer"}
	var out := {"ok": true}
	for k in ["players", "games_open", "games_running"]:
		var n: Variant = j.get(k)
		if typeof(n) != TYPE_FLOAT or n != floorf(n) or n < 0.0 or n > MAX_COUNT:
			return {"ok": false, "why": "%s is not a count" % k}
		out[k] = int(n)
	return out


## lane ONLINEUX-1: the counts of the last answer ({} when none is current): the online home page's ticker shows them
func counts() -> Dictionary:
	return _last


## the line to show: "3 players online" (the online menu adds ", 1 game open")
static func line(stats: Dictionary, with_games: bool) -> String:
	var n: int = stats.players
	var s := "%d %s online" % [n, "player" if n == 1 else "players"]
	if with_games:
		var g: int = stats.games_open
		s += ", %d %s open" % [g, "game" if g == 1 else "games"]
	return s


## the menu on screen: "menu", "online" or "" (nothing asked). Called every frame by the game; acts when the mode changes
func set_mode(m: String) -> void:
	if m == mode:
		return
	mode = m
	_mode_serial += 1
	_timer.stop()
	if m.is_empty():
		_set_text("")
		return
	if not _last.is_empty() and float(Time.get_ticks_msec() - _last_ms) / 1000.0 < menu_s:
		_set_text(line(_last, m == "online"))
	else:
		_set_text("")
	_tick()


func _tick() -> void:
	if mode.is_empty() or _in_flight:
		return
	# a line older than two intervals is not shown (the lobby has not answered since): never a stale number
	var interval := menu_s if mode == "menu" else online_s
	if not _last.is_empty() and float(Time.get_ticks_msec() - _last_ms) / 1000.0 >= 2.0 * interval:
		_set_last({})
		_set_text("")
	if visible_check.is_valid() and not bool(visible_check.call()):
		_timer.start(maxf(min_gap_s, 1.0)) # asked again when the window is back
		return
	var wait := min_gap_s - float(Time.get_ticks_msec() - _last_sent_ms) / 1000.0
	if wait > 0.0:
		_timer.start(wait)
		return
	var headers := PackedStringArray(["accept: application/json"])
	if mode == "online":
		var token := str(token_provider.call()) if token_provider.is_valid() else ""
		if not token.is_empty():
			headers.append("authorization: Bearer " + token)
	var err := _http.request(lobby_url.trim_suffix("/") + "/v1/stats", headers)
	_last_sent_ms = Time.get_ticks_msec()
	if err != OK:
		_set_text("")
		_timer.start(menu_s if mode == "menu" else online_s)
		return
	requests_sent += 1
	_in_flight = true
	_sent_serial = _mode_serial


func _on_done(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_in_flight = false
	var fresh := _sent_serial == _mode_serial
	var shown := ""
	if result == HTTPRequest.RESULT_SUCCESS and (code < 300 or code >= 400):
		var p := parse(code, body.get_string_from_utf8())
		if p.ok:
			_set_last(p)
			_last_ms = Time.get_ticks_msec()
			shown = line(p, mode == "online")
		else:
			print("GAME ONLINE count: not shown (%s)" % p.why)
	else:
		print("GAME ONLINE count: not shown (no answer: %d / %d)" % [result, code])
	if shown.is_empty():
		_set_last({})
	if mode.is_empty() or not fresh:
		# the player moved on while it was asked: nothing of it is shown, and the new menu asks for itself
		if not mode.is_empty():
			_tick()
		return
	_set_text(shown)
	_timer.start(menu_s if mode == "menu" else online_s)


func _set_last(d: Dictionary) -> void:
	var was := _last
	_last = d
	for k in ["players", "games_open", "games_running"]:
		if was.get(k, -1) != d.get(k, -1):
			counts_changed.emit(d)
			return


func _set_text(t: String) -> void:
	if t == text:
		return
	text = t
	changed.emit(t)
