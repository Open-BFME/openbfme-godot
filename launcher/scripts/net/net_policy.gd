## Lane LAUNCH-1: where the launcher may connect (docs/RELEASE.md, "The launcher": privacy and network).
##
## Release builds: HTTPS only, to api.github.com (the releases list), github.com (the release download links) and *.githubusercontent.com
## (where github.com redirects release assets), with Godot's TLS certificate and host name verification (TLSOptions.client()). Every
## redirect is checked again before it is followed. A URL with user info, an explicit port, a fragment, a non-ASCII or IP-literal host is refused.
## Lane ONLINECOUNT-1: requests to the online lobby, on the lobby's own host (HTTPS, no port; set_lobby). Exactly two paths, both GET, with no query
## and no extra path: /v1/stats (the number of players online that the Play card shows) and /v1/games (the public game list). Nothing else of the
## lobby is allowed; nothing is sent but the request line and the standard headers.
## Test builds (tests_allowed()) may add ONE plain-HTTP loopback origin (127.0.0.1 or localhost with a port), the local test server.
## Lane AIO-1: while the player's opt-in download of the game files runs (scripts/core/aio_install.gd, off by default), extra_hosts also
## holds the All In One BFME Launcher service's two hosts (HTTPS, no explicit port, like GitHub's); it is empty otherwise.
extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")

const GITHUB_HOSTS := ["api.github.com", "github.com"]
const GITHUB_ASSET_SUFFIX := ".githubusercontent.com"

var test_origin := ""  # "http://127.0.0.1:<port>" in a test build, or ""
const LOBBY_PATHS := ["/v1/stats", "/v1/games"]  # exact paths: no query, no trailing slash, no extra segment
var lobby_host := ""  # the lobby's host (exact), for LOBBY_PATHS only; "" = none
var extra_hosts: Array = []  # exact host names allowed besides GitHub's (lane AIO-1, only during an AIO download)


## {ok, error, scheme, host, port, path (with query), origin}
static func parse_url(url: String) -> Dictionary:
	var bad := func(why: String) -> Dictionary: return {"ok": false, "error": L10n.t("launcher.net_policy.refused_url", {"url": url.left(200), "why": why})}
	var scheme := ""
	if url.begins_with("https://"):
		scheme = "https"
	elif url.begins_with("http://"):
		scheme = "http"
	else:
		return bad.call(L10n.t("launcher.net_policy.why_not_http"))
	var rest := url.substr(scheme.length() + 3)
	var slash := rest.find("/")
	var authority := rest if slash < 0 else rest.substr(0, slash)
	var path := "/" if slash < 0 else rest.substr(slash)
	if authority.is_empty():
		return bad.call(L10n.t("launcher.net_policy.why_no_host"))
	if "@" in authority:
		return bad.call(L10n.t("launcher.net_policy.why_user_info"))
	if "#" in path:
		return bad.call(L10n.t("launcher.net_policy.why_fragment"))
	for c in url:
		if c.unicode_at(0) <= 0x20 or c.unicode_at(0) >= 0x7f or c == "\\":
			return bad.call(L10n.t("launcher.net_policy.why_characters"))
	var host := authority
	var port := 443 if scheme == "https" else 80
	var colon := authority.rfind(":")
	if colon >= 0:
		host = authority.substr(0, colon)
		var p := authority.substr(colon + 1)
		if not p.is_valid_int() or p.to_int() < 1 or p.to_int() > 65535:
			return bad.call(L10n.t("launcher.net_policy.why_bad_port"))
		port = p.to_int()
	host = host.to_lower()
	if host.is_empty() or host.begins_with("[") or host.begins_with(".") or host.ends_with("."):
		return bad.call(L10n.t("launcher.net_policy.why_bad_host"))
	return {"ok": true, "error": "", "scheme": scheme, "host": host, "port": port, "path": path,
		"origin": "%s://%s" % [scheme, authority.to_lower()], "explicit_port": colon >= 0}


## "" when the launcher may request `url`, else why not
func check(url: String) -> String:
	var u := parse_url(url)
	if not u.ok:
		return u.error
	if test_origin != "" and u.origin == test_origin:
		return ""
	if u.scheme != "https":
		return L10n.t("launcher.net_policy.not_https", {"url": url.left(200)})
	if u.explicit_port:
		return L10n.t("launcher.net_policy.explicit_port", {"url": url.left(200)})
	var host: String = u.host
	if host in GITHUB_HOSTS or host in extra_hosts:
		return ""
	if lobby_host != "" and host == lobby_host and u.path in LOBBY_PATHS:
		return ""
	if host.ends_with(GITHUB_ASSET_SUFFIX) and host.length() > GITHUB_ASSET_SUFFIX.length():
		var label := host.substr(0, host.length() - GITHUB_ASSET_SUFFIX.length())
		var rx := RegEx.create_from_string("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$")
		if rx.search(label) != null:
			return ""
	if extra_hosts.is_empty():
		return L10n.t("launcher.net_policy.not_release_host", {"url": url.left(200), "host": host})
	return L10n.t("launcher.net_policy.not_release_or_aio_host", {"url": url.left(200), "host": host})


## "" when `origin` may be the test origin (a test build only): plain HTTP to the loopback address with a port
static func check_test_origin(origin: String) -> String:
	var rx := RegEx.create_from_string("^http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}$")
	return "" if rx.search(origin) != null else L10n.t("launcher.net_policy.test_origin_form", {"origin": origin})


## A new policy with the same rules (test origin, extra hosts, lobby). Each lobby request sets its lobby on its own copy, so the shared
## updater.policy that running download / count / game-list threads read is never changed under them (PR #93 follow-up).
func copy() -> RefCounted:
	var p: RefCounted = get_script().new()
	p.test_origin = test_origin
	p.lobby_host = lobby_host
	p.extra_hosts = extra_hosts.duplicate()
	return p


## "" when `url` may be the online lobby (https, a host name, no port, path, query or user info), else why not. Sets lobby_host. A test build's lobby is its test
## origin (the one plain-HTTP loopback origin), which check() allows anyway.
func set_lobby(url: String) -> String:
	lobby_host = ""
	var u := parse_url(url.trim_suffix("/"))
	if not u.ok:
		return u.error
	if test_origin != "" and u.origin == test_origin and u.path == "/":
		return ""
	if u.scheme != "https" or u.explicit_port or u.path != "/":
		return L10n.t("launcher.net_policy.lobby_form", {"url": url.left(200)})
	lobby_host = u.host
	return ""
