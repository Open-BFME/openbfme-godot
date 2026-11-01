## Lane ONLINE-2 (docs/ONLINE.md sections 4 and 6, phase O1): openbfme:// links. A link "openbfme://join/<game id>" (from a lobby's invite page
## https://<lobby>/j/<id>, a friend's chat) starts the version Play starts with --online-join=<id> --online-lobby=<the lobby>; the game signs in, joins
## and plays (godot/scripts/online_lobby.gd). Online play is a test feature.
## The operating system hands the link to the launcher once it is registered (--register-url-handler):
##   Windows: HKCU\Software\Classes\openbfme (no administrator rights, no UAC): URL Protocol, and shell\open\command =
##   "<launcher>" -- --open-url "%1"
##   Linux / Steam Deck: ~/.local/share/applications/openbfme-url.desktop with MimeType=x-scheme-handler/openbfme and
##   Exec=<launcher> -- --open-url %u, made the default with xdg-mime
## The "--" puts the link among Godot's user arguments (a bare argument could be taken for a scene path).
## ONLINE-2 r2 (Sol r1: Windows splices the link into the command line, so a link with quotes in it adds arguments of its own): a link is only ever
## taken in the isolated mode isolated_url() checks: the user arguments are exactly --open-url and one strict link (a development / test build also
## allows its test options before them), else nothing at all runs. A link anywhere else on the command line is refused the same way.
extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")

const SCHEME := "openbfme"
const DESKTOP_FILE := "openbfme-url.desktop"


## {ok, action, game} for exactly "openbfme://join/<16 lower-case hex>" with an optional trailing slash; {ok: false, error} for anything else (no
## spaces, quotes, other characters or parts): a link never does more than join
static func parse(link: String) -> Dictionary:
	var re := RegEx.create_from_string("^openbfme://join/([0-9a-f]{16})/?$")
	var m := re.search(link)
	if m == null or m.get_string() != link: # the whole text ("$" alone would let a trailing newline through)
		return {"ok": false, "error": L10n.t("launcher.url_handler.not_a_join_link")}
	return {"ok": true, "action": "join", "game": m.get_string(1)}


## ONLINE-2 r2: the isolated link mode. {mode: false} when the arguments hold no link and no --open-url; else {mode: true, ok, link, game, error}:
## ok only for the user arguments ["--open-url", <a link parse() takes>], with `test_options` (arguments starting with one of `test_prefixes`) allowed
## before them when `allow_test_options` (a development or test build)
static func isolated_url(args: PackedStringArray, allow_test_options: bool, test_prefixes: Array) -> Dictionary:
	var linked := false
	for a in args:
		linked = linked or a == "--open-url" or a.to_lower().contains(SCHEME + ":")
	if not linked:
		return {"mode": false}
	var rest: Array = []
	var leading := true
	for a in args:
		var test_option := allow_test_options and leading and test_prefixes.any(func(p): return a.begins_with(p))
		if not test_option:
			leading = false
			rest.append(a)
	if rest.size() != 2 or rest[0] != "--open-url":
		return {"mode": true, "ok": false, "error": L10n.t("launcher.url_handler.link_alone", {"count": rest.size()})}
	var p := parse(rest[1])
	if not p.ok:
		return {"mode": true, "ok": false, "error": p.error}
	return {"mode": true, "ok": true, "link": rest[1], "game": p.game, "error": ""}


## the game's arguments for a parsed join link (after "--": the game reads its own options from Godot's user arguments)
static func game_args(parsed: Dictionary, lobby: String) -> PackedStringArray:
	return PackedStringArray(["--", "--online-join=" + str(parsed.game), "--online-lobby=" + lobby])


static func linux_desktop_entry(exe: String) -> String:
	return "[Desktop Entry]\nType=Application\nName=OpenBFME Launcher (game links)\nExec=\"%s\" -- --open-url %%u\nMimeType=x-scheme-handler/%s;\nNoDisplay=true\nTerminal=false\n" % [exe, SCHEME]


## the reg.exe argument lists that register the handler for the current user
static func windows_reg_commands(exe: String) -> Array:
	var key := "HKCU\\Software\\Classes\\" + SCHEME
	return [
		PackedStringArray(["add", key, "/ve", "/d", "URL:OpenBFME game link", "/f"]),
		PackedStringArray(["add", key, "/v", "URL Protocol", "/d", "", "/f"]),
		PackedStringArray(["add", key + "\\shell\\open\\command", "/ve", "/d", "\"%s\" -- --open-url \"%%1\"" % exe, "/f"]),
	]


## registers this launcher (`exe`) as the handler; {ok, error, where}
static func register(exe: String) -> Dictionary:
	if OS.get_name() == "Windows":
		for cmd in windows_reg_commands(exe):
			var out := []
			if OS.execute("reg", cmd, out, true) != 0:
				return {"ok": false, "error": L10n.t("launcher.url_handler.reg_failed", {"command": " ".join(cmd), "output": "".join(out).strip_edges()})}
		return {"ok": true, "error": "", "where": "HKCU\\Software\\Classes\\" + SCHEME}
	if OS.get_name() in ["Linux", "FreeBSD", "NetBSD", "OpenBSD", "BSD"]:
		var home := OS.get_environment("XDG_DATA_HOME")
		if home == "":
			home = OS.get_environment("HOME").path_join(".local/share")
		var dir := home.path_join("applications")
		DirAccess.make_dir_recursive_absolute(dir)
		var path := dir.path_join(DESKTOP_FILE)
		var f := FileAccess.open(path, FileAccess.WRITE)
		if f == null:
			return {"ok": false, "error": L10n.t("launcher.url_handler.cannot_write", {"path": path, "reason": error_string(FileAccess.get_open_error())})}
		f.store_string(linux_desktop_entry(exe))
		f.close()
		var out := []
		if OS.execute("xdg-mime", ["default", DESKTOP_FILE, "x-scheme-handler/" + SCHEME], out, true) != 0:
			return {"ok": false, "error": L10n.t("launcher.url_handler.xdg_mime_failed", {"output": "".join(out).strip_edges()})}
		OS.execute("update-desktop-database", [dir], [], true) # optional: some desktops read the cache
		return {"ok": true, "error": "", "where": path}
	return {"ok": false, "error": L10n.t("launcher.url_handler.unsupported_os", {"os": OS.get_name()})}
