## OpenBFME. GPL-3.0.
## Lane ONLINE-4 (Sol r1): the game's own lobby code never logs a credential and never reads a hostile answer beyond its bound. Run by
## tools/net/test_online_hostile.py with -- --lobby=<a server: /echo/... refuses every request with a message that echoes a ticket and a session
## token, /huge/... answers every request with 1 MiB> --ticket=T --token=S. Checks: game.gd's shell request line of an OnlineGameStart masks the
## START's ticket; ONLINE-2's online_lobby.gd fails on the echoing refusal without the ticket or token in its message, and on the 1 MiB answer
## without reading it; the online screens' transport (online_http.gd) hands the shell an error, not the body. One "HOSTILE ..." line per check;
## exit 0 when all hold (the harness also checks that the ticket and token appear nowhere in the output).
extends SceneTree


## the online screens' side of online_http.gd (AptMenuPlayer.online_take_requests / online_deliver): one GET, its answer kept
class ShellStub:
	extends RefCounted
	var sent := false
	var delivered: Array = []

	func online_take_requests() -> Array:
		if sent:
			return []
		sent = true
		return [{"id": 1, "method": "GET", "path": "/v1/motd", "body": "", "token": ""}]

	func online_deliver(id: int, status: int, body: String, error: String) -> void:
		delivered.append({"id": id, "status": status, "body_bytes": body.length(), "error": error})


var _ok := true


func _init() -> void:
	_run.call_deferred() # once the tree runs: an HTTPRequest works only inside it


func _check(what: String, cond: bool, detail: String) -> void:
	print("HOSTILE %s %s: %s" % ["ok  " if cond else "FAIL", what, detail])
	_ok = _ok and cond


func _run() -> void:
	var lobby := ""
	var ticket := ""
	var token := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--lobby="):
			lobby = a.substr(8)
		elif a.begins_with("--ticket="):
			ticket = a.substr(9)
		elif a.begins_with("--token="):
			token = a.substr(8)
	# game.gd's log line of the staging room's START (Sol r1: it printed the whole START, its relay ticket included)
	var game: GDScript = load("res://scripts/game.gd")
	var start := JSON.stringify({"v": 1, "kind": "openbfme-start", "game": "0123456789abcdef", "slot": 1, "relay": "127.0.0.1:7350", "ticket": ticket})
	var line: String = game.shell_request_line("OnlineGameStart", start)
	_check("shell request line", not line.contains(ticket) and line.contains("<redacted>") and line.contains("0123456789abcdef"), line)
	# ONLINE-2's client: a refusal that echoes credentials
	var outcome := await _lobby_outcome(lobby + "/echo")
	_check("echoed refusal", outcome.begins_with("failed: ") and not outcome.contains(ticket) and not outcome.contains(token), outcome)
	# ONLINE-2's client: an answer beyond the bound
	outcome = await _lobby_outcome(lobby + "/huge")
	_check("1 MiB answer (online_lobby.gd)", outcome.begins_with("failed: ") and outcome.contains("more than 262144 bytes"), outcome)
	# the online screens' transport
	var stub := ShellStub.new()
	var http: Node = load("res://scripts/online_http.gd").new()
	http.shell = stub
	http.lobby_url = lobby + "/huge"
	root.add_child(http)
	var waited := 0
	while stub.delivered.is_empty() and waited < 300:
		await create_timer(0.05).timeout
		waited += 1
	var d: Dictionary = stub.delivered[0] if not stub.delivered.is_empty() else {}
	_check("1 MiB answer (online_http.gd)", d.get("body_bytes", -1) == 0 and str(d.get("error", "")).contains("more than 262144 bytes"), str(d))
	quit(0 if _ok else 1)


func _lobby_outcome(url: String) -> String:
	var client: Node = load("res://scripts/online_lobby.gd").new()
	client.lobby_url = url
	client.player_name = "Rohan"
	client.profile = "0".repeat(32)
	var outcome := []
	client.failed.connect(func(m): outcome.append("failed: " + m))
	client.started.connect(func(_s): outcome.append("started"))
	root.add_child(client)
	client.begin()
	var waited := 0
	while outcome.is_empty() and waited < 300:
		await create_timer(0.05).timeout
		waited += 1
	client.queue_free()
	return outcome[0] if not outcome.is_empty() else "no outcome"
