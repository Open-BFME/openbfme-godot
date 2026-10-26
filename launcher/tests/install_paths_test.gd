## Synthetic paths only. The signed-package verifier is stubbed; the real play(), picker and OS.create_process paths run unchanged.
extends SceneTree

const Paths := preload("res://scripts/ui/paths.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const Updater := preload("res://scripts/core/updater.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")
const GamesScreen := preload("res://scripts/ui/games_screen.gd")

class PathUpdater extends Updater:
	func verify_installed(_version: String, _full := false) -> Dictionary:
		return {"ok": true}
	func plays_bundled(_version: String) -> bool:
		return false
	func lock_versions(_wait_ms := 30000) -> bool:
		return true
	func unlock_versions() -> void:
		pass
	func _live_runs() -> Dictionary:
		return {}
	func check_bundled() -> Dictionary:
		return {"ok": true}

var checks := 0
var failed := 0

func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		printerr("PATH FAIL ", label)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var cases: Array = JSON.parse_string(FileAccess.get_file_as_string(OS.get_environment("OPENBFME_PATH_CASES")))
	for item in cases:
		var folder: String = item.folder
		var game_dir: String = item.user_dir
		var chosen := GamesScreen.new()
		chosen.set_folder(folder)
		check(chosen.folder == folder, "folder picker preserves all characters")
		chosen.free()
		var cfg := Paths.read_cfg(game_dir.path_join("install-paths.cfg"))
		check(cfg.get("ROTWK_INSTALL") == folder, "config keeps the selected path")
		var remembered := Paths.game_folders(game_dir)
		check(remembered.found and remembered.rotwk == folder, "launcher finds the remembered folder")
		var updater := PathUpdater.new(BuildInfo.new())
		updater.data_dir = item.launcher_dir
		updater.set_selected("v1.2.3")
		var again := PathUpdater.new(BuildInfo.new())
		again.data_dir = item.launcher_dir
		again.state.load(item.launcher_dir.path_join("state.cfg"))
		check(again.selected() == "v1.2.3", "version selection persists in difficult user-data folder")
		var output: String = item.output
		OS.set_environment("OPENBFME_PATH_ARGV_OUTPUT", output)
		OS.set_environment("BFME1_INSTALL", folder)
		updater.set_state("launcher", "game", "bfme1")
		var input := PackedStringArray(["--", "--save-dir=" + folder, "--record=" + folder.path_join("my replay.rep"), "literal & (value)=one;two", "literal \"quotes\"", "trailing-backslash\\"])
		var expected := Array(input) + ["--game=bfme1", "--bfme1-install=" + folder]
		var arguments := updater.game_arguments(input)
		check(arguments.ok and Array(arguments.args) == expected, "arguments remain separate, never manually quoted")
		check(input.size() == 6, "argument building does not mutate the caller")
		var played := updater.play("v1.2.3", input)
		check(played.ok, "real version-folder process starts")
		await check_output(output, expected)
		DirAccess.remove_absolute(output)
		var bundled := updater._play_bundled({"version": "v1.2.3", "path": item.bundled_dir}, arguments.args)
		check(bundled.ok, "real bundled process starts")
		await check_output(output, expected)
		DirAccess.remove_absolute(output)
		check(Updater.package_root("/Applications/Game With Spaces/OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher", "macos-arm64") == "/Applications/Game With Spaces", "app bundle parent keeps spaces")
	for name in ["Jürgen", "Анна", "玩家"]:
		var home: String = "/srv/players/" + name
		check(Paths.scrub(home + "/Game /media/" + name + "/Game", home, name) == "~/Game /media/…/Game", "Unicode UI privacy")
		OS.set_environment("USER", name)
		OS.set_environment("USERNAME", name)
		Log._home = home
		Log._user = name
		check(Log.scrub(home + "/Game /media/" + name + "/Game") == "<user>/Game /media/<user>/Game", "Unicode log privacy outside home")
	print("PATH LAUNCHER checks=", checks, " failed=", failed, " cases=", cases.size())
	quit(0 if failed == 0 else 1)

func check_output(output: String, expected: Array) -> void:
	for _i in range(500):
		if FileAccess.file_exists(output):
			var got: Variant = JSON.parse_string(FileAccess.get_file_as_string(output))
			if got != null:
				check(got == expected, "spawned process receives exact UTF-8 argv")
				return
		await create_timer(0.01).timeout
	check(false, "spawned process did not report argv")
