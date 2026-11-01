## Run on Linux with SIGXFSZ ignored and RLIMIT_FSIZE=4096 (regular files still open, but these writes are short).
## Pass -- --write-limited only under that limit; without it this test skips.
extends SceneTree

const GameOptions = preload("res://scripts/core/game_options.gd")
const DesktopEntry = preload("res://scripts/core/desktop_entry.gd")


func _init() -> void:
	if OS.get_name() != "Linux" or not OS.get_cmdline_user_args().has("--write-limited"):
		print("WRITE FAILURE SKIP: requires Linux RLIMIT_FSIZE=4096 and --write-limited")
		quit(77)
		return
	var path: String = OS.get_user_data_dir().path_join("short-write-options.ini")
	var why: String = GameOptions.set_value("test", "x".repeat(65536), path)
	var result: Dictionary = DesktopEntry.install("/opt/OpenBFME/Launcher", OS.get_user_data_dir().path_join("short-write-desktop"))
	var option_failed: bool = why.contains(path) and why.contains(error_string(ERR_FILE_CANT_WRITE))
	var desktop_error: String = result.error
	var desktop_failed: bool = not result.ok and desktop_error.contains("openbfme.png") and desktop_error.contains(error_string(ERR_FILE_CANT_WRITE))
	print("WRITE FAILURE %s: 2 checks (settings=%s, icon=%s)" % ["PASS" if option_failed and desktop_failed else "FAIL", option_failed, desktop_failed])
	quit(0 if option_failed and desktop_failed else 1)
