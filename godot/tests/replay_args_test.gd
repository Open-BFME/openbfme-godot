extends SceneTree

const Args := preload("res://scripts/replay_args.gd")
var checks := 0
var failures := 0


func check(ok: bool, what: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		printerr("REPLAY GAME ARGS FAIL: ", what)


func _initialize() -> void:
	for args in [["--replay", "space λ file.replay"], ["--replay=space λ file.replay"], ["--replay", "space λ file.replay", "--replay-check", "--replay-speed=2"]]:
		var r := Args.parse(PackedStringArray(args))
		check(r.ok and r.path == "space λ file.replay", str(args))
	for args in [["--replay"], ["--replay="], ["--replay", "--auto"], ["--replay=x", "--replay", "y"], ["--replay=x", "--auto"], ["--replay=x", "--online-host"], ["--replay=x", "--campaign=foo"]]:
		var r := Args.parse(PackedStringArray(args))
		check(not r.ok and not r.error.is_empty(), str(args))
	check(Args.parse(["--advance=10"]).path.is_empty(), "ordinary startup unaffected")
	for args in [["--load", "space λ save.obfmesave"], ["--load=space λ save.obfmesave"]]:
		var r := Args.parse(PackedStringArray(args))
		check(r.ok and r.path.is_empty() and r.load_path == "space λ save.obfmesave", str(args))
	for args in [["--load"], ["--load="], ["--load", "--auto"], ["--load=x", "--load", "y"], ["--load=x", "--load=y"],
			["--load=x", "--replay=y"], ["--replay=x", "--load=y"], ["--load=x", "--auto"], ["--load=x", "--campaign=foo"],
			["--load=x", "--online-host"], ["--load=x", "--lan-host"], ["--load=x", "--net-host=1234"], ["--load=x", "--qa"], ["--load=x", "--replay-check"]]:
		var r := Args.parse(PackedStringArray(args))
		check(not r.ok and not r.error.is_empty(), str(args))
	check(Args.parse(["--replay=x"]).load_path.is_empty(), "replay never starts a save load")
	check(Args.parse([]).load_path.is_empty(), "ordinary startup never starts a save load")
	print("REPLAY GAME ARGS: %d checks, %d failures" % [checks, failures])
	quit(0 if failures == 0 else 1)
