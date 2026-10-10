## Lane LAUNCH-1: where the launcher may connect (docs/RELEASE.md, "The launcher": privacy and network).
##
## Release builds: HTTPS only, to api.github.com (the releases list), github.com (the release download links) and *.githubusercontent.com
## (where github.com redirects release assets), with Godot's TLS certificate and host name verification (TLSOptions.client()). Every
## redirect is checked again before it is followed. A URL with user info, an explicit port, a fragment, a non-ASCII or IP-literal host is refused.
## Test builds (tests_allowed()) may add ONE plain-HTTP loopback origin (127.0.0.1 or localhost with a port), the local test server.
## Lane AIO-1: while the player's opt-in download of the game files runs (scripts/core/aio_install.gd, off by default), extra_hosts also
## holds the All In One BFME Launcher service's two hosts (HTTPS, no explicit port, like GitHub's); it is empty otherwise.
extends RefCounted

const GITHUB_HOSTS := ["api.github.com", "github.com"]
const GITHUB_ASSET_SUFFIX := ".githubusercontent.com"

var test_origin := ""  # "http://127.0.0.1:<port>" in a test build, or ""
var extra_hosts: Array = []  # exact host names allowed besides GitHub's (lane AIO-1, only during an AIO download)


## {ok, error, scheme, host, port, path (with query), origin}
static func parse_url(url: String) -> Dictionary:
	var bad := func(why: String) -> Dictionary: return {"ok": false, "error": "refused URL '%s': %s" % [url.left(200), why]}
	var scheme := ""
	if url.begins_with("https://"):
		scheme = "https"
	elif url.begins_with("http://"):
		scheme = "http"
	else:
		return bad.call("not http(s)")
	var rest := url.substr(scheme.length() + 3)
	var slash := rest.find("/")
	var authority := rest if slash < 0 else rest.substr(0, slash)
	var path := "/" if slash < 0 else rest.substr(slash)
	if authority.is_empty():
		return bad.call("no host")
	if "@" in authority:
		return bad.call("user info in the URL")
	if "#" in path:
		return bad.call("a fragment")
	for c in url:
		if c.unicode_at(0) <= 0x20 or c.unicode_at(0) >= 0x7f or c == "\\":
			return bad.call("characters outside printable ASCII")
	var host := authority
	var port := 443 if scheme == "https" else 80
	var colon := authority.rfind(":")
	if colon >= 0:
		host = authority.substr(0, colon)
		var p := authority.substr(colon + 1)
		if not p.is_valid_int() or p.to_int() < 1 or p.to_int() > 65535:
			return bad.call("a bad port")
		port = p.to_int()
	host = host.to_lower()
	if host.is_empty() or host.begins_with("[") or host.begins_with(".") or host.ends_with("."):
		return bad.call("a bad host")
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
		return "refused URL '%s': not HTTPS" % url.left(200)
	if u.explicit_port:
		return "refused URL '%s': an explicit port" % url.left(200)
	var host: String = u.host
	if host in GITHUB_HOSTS or host in extra_hosts:
		return ""
	if host.ends_with(GITHUB_ASSET_SUFFIX) and host.length() > GITHUB_ASSET_SUFFIX.length():
		var label := host.substr(0, host.length() - GITHUB_ASSET_SUFFIX.length())
		var rx := RegEx.create_from_string("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$")
		if rx.search(label) != null:
			return ""
	return "refused URL '%s': %s is not a %s" % [url.left(200), host, "GitHub release host" if extra_hosts.is_empty() else "GitHub release or All In One BFME Launcher host"]


## "" when `origin` may be the test origin (a test build only): plain HTTP to the loopback address with a port
static func check_test_origin(origin: String) -> String:
	var rx := RegEx.create_from_string("^http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}$")
	return "" if rx.search(origin) != null else "the test API origin must be http://127.0.0.1:<port> (got '%s')" % origin
