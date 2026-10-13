## OpenBFME. GPL-3.0.
## Lane ONLINE-2 r2 (Sol r1): the online lobby client (scripts/online_lobby.gd) never takes the session token to another origin. Run by
## tools/net/test_online_lobby_redirect.py with -- --lobby=<a server that answers every request with a redirect to another server>; the client
## holds a token (as with OPENBFME_LOBBY_TOKEN) and joins. Prints "REDIRECT failed: <message>" once the client gave up, or "REDIRECT started" (it
## must not), then quits; also checks the lobby URL rule. Exit 0 when the client failed on the redirect and every bad URL was refused.
extends SceneTree


func _init() -> void:
	_run.call_deferred() # once the tree runs: an HTTPRequest works only inside it


func _run() -> void:
	var lobby := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--lobby="):
			lobby = a.substr(8)
	var script: GDScript = load("res://scripts/online_lobby.gd")
	var ok := true
	for bad in ["http://user:pw@lobby.example", "https://lobby.example/?next=http://x", "https://lobby.example/#x", "ftp://lobby.example", "javascript:x",
			"https://lobby.example\n", "http://lobby.example:8787/ path"]:
		if script.lobby_url_problem(bad).is_empty():
			print("REDIRECT a bad lobby URL was accepted: ", bad.c_escape())
			ok = false
	for good in ["http://127.0.0.1:8787", "https://lobby.example.org", "https://lobby.example.org/openbfme/"]:
		if not script.lobby_url_problem(good).is_empty():
			print("REDIRECT a good lobby URL was refused: ", good)
			ok = false
	var client: Node = script.new()
	client.lobby_url = lobby
	client.token = "secret-session-token"
	client.join_id = "0123456789abcdef"
	client.profile = "0".repeat(32)
	var outcome := []
	client.failed.connect(func(m): outcome.append("failed: " + m))
	client.started.connect(func(_s): outcome.append("started"))
	root.add_child(client)
	client.begin()
	var waited := 0
	while outcome.is_empty() and waited < 300:
		await process_frame
		await create_timer(0.05).timeout
		waited += 1
	print("REDIRECT ", outcome[0] if not outcome.is_empty() else "no outcome")
	quit(0 if ok and not outcome.is_empty() and outcome[0].begins_with("failed: ") and outcome[0].contains("redirect") else 1)
