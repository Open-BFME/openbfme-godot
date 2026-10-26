extends SceneTree

const Lobby := preload("res://scripts/online_lobby.gd")
const INVITE := "1111111111111111"
const GAME := "2222222222222222"
const PROFILE := "0123456789abcdef0123456789abcdef"


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var url := ""
	var mode := "success"
	var simulation_mode := "normal"
	for arg in args:
		if arg.begins_with("--lobby="): url = arg.substr(8)
		elif arg.begins_with("--mode="): mode = arg.substr(7)
		elif arg.begins_with("--simulation-mode="): simulation_mode = arg.substr(18)
	if mode == "local":
		var scenarios := [
			{"invite_id": "bad", "token": "session", "profile": PROFILE},
			{"invite_id": INVITE, "profile": PROFILE},
			{"invite_id": INVITE, "token": "session"},
			{"invite_id": INVITE, "token": "session", "profile": PROFILE, "join_id": GAME},
			{"invite_id": INVITE, "token": "session", "profile": PROFILE, "host_setup": {"name": "host"}},
			{"invite_id": INVITE, "token": "session", "profile": PROFILE, "mode": "invalid"},
		]
		for values in scenarios:
			var client: Node = Lobby.new()
			client.lobby_url = url
			for key in values: client.set(key, values[key])
			var errors: Array = []
			client.failed.connect(func(message): errors.append(message))
			root.add_child(client)
			client.begin()
			assert(errors.size() == 1, "invalid invite launch did not fail locally: %s" % values)
			client.queue_free()
		print("INVITE local validation PASS")
		quit(0)
		return
	var client: Node = Lobby.new()
	client.lobby_url = url
	client.token = "fake-session-token"
	client.profile = PROFILE
	client.mode = simulation_mode
	client.platform = "linux"
	client.invite_id = INVITE
	var outcome: Array = []
	client.failed.connect(func(message):
		outcome.append("failed:" + message)
		if mode == "success": quit(1)
		else: quit(0))
	client.started.connect(func(_start):
		outcome.append("started")
		quit(0 if mode == "success" else 1))
	root.add_child(client)
	client.begin()
	var ticks := 0
	while outcome.is_empty() and ticks < 400:
		await process_frame
		await create_timer(0.025).timeout
		ticks += 1
	if outcome.is_empty():
		print("INVITE timeout")
		quit(1)
