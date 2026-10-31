## Lane ONLINECOUNT-1: "N players online" on the Play card, from the online lobby's GET /v1/stats (services/online/lobby: counts only, no names).
## ONE request: no sign-in, no token, no cookie, nothing about the player or the machine (the User-Agent names the launcher and its version, as for GitHub).
## It is made only when the setting is on (Updater.show_online_count), at most once per MIN_GAP_SECONDS, never while offline, and an answer that is not
## exactly the lobby's version 1 counts, a 404 (a lobby without stats), a timeout or a refusal gives text "" = nothing shown (never an old number).
extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")

const Log := preload("res://scripts/core/launcher_log.gd")
const HttpFetch := preload("res://scripts/net/http_fetch.gd")

const MIN_GAP_SECONDS := 30.0
const REFRESH_SECONDS := 300.0   # while the Play screen is visible
const EXPIRY_SECONDS := 2.0 * REFRESH_SECONDS  # a number the lobby has not renewed for this long is not shown
const BODY_CAP := 4096
const MAX_COUNT := 10000000


## "N players online" from an answer, or "" when it is not a stats answer. {ok, text, why}
static func parse(code: int, body: PackedByteArray) -> Dictionary:
	if code != 200:
		return {"ok": false, "text": "", "why": L10n.t("launcher.online_count.lobby_answered", {"code": code})}
	var parser := JSON.new() # (JSON.parse_string prints an engine error for text that is not JSON)
	var j: Variant = parser.data if parser.parse(body.get_string_from_utf8()) == OK else null
	if typeof(j) != TYPE_DICTIONARY or j.get("v") != 1.0:
		return {"ok": false, "text": "", "why": L10n.t("launcher.online_count.not_version_1")}
	for k in ["players", "games_open", "games_running"]:
		var n: Variant = j.get(k)
		if typeof(n) != TYPE_FLOAT or n != floorf(n) or n < 0.0 or n > MAX_COUNT:
			return {"ok": false, "text": "", "why": L10n.t("launcher.online_count.not_a_count", {"field": k})}
	var players := int(j.players)
	return {"ok": true, "text": L10n.t("launcher.online_count.players_online", {"count": players}), "why": ""}


## the chip's text now: `text` while it is younger than EXPIRY_SECONDS, else "" (never an old number)
static func chip_text(text: String, age_seconds: float) -> String:
	return text if age_seconds >= 0.0 and age_seconds < EXPIRY_SECONDS else ""


## whether a Play screen coming into view should ask again: no number shown, or one older than REFRESH_SECONDS
static func stale(shown: String, age_seconds: float) -> bool:
	return shown == "" or age_seconds >= REFRESH_SECONDS or age_seconds < 0.0


## whether the window is in view enough to ask: not minimized and in the foreground (a headless run, which has no window, counts as in view)
static func in_view(headless: bool, minimized: bool, focused: bool) -> bool:
	return headless or (not minimized and focused)


## asks the lobby (blocking: run it on a thread). `http`: an HttpFetch of its own (never the update jobs'). {ok, text, why}
static func fetch(http: HttpFetch, lobby_url: String) -> Dictionary:
	var r := http.get_small(lobby_url.trim_suffix("/") + "/v1/stats", BODY_CAP, "application/json")
	if not r.ok:
		return {"ok": false, "text": "", "why": r.error}
	return parse(r.code, r.body)
