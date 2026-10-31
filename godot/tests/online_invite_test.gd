extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

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
			_check(errors.size() == 1, "invalid invite launch did not fail locally: %s" % values)
			client.queue_free()
		_finish("INVITE local validation PASS")
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
