## Lane MACPLAY-1: the Mac launcher's pure parts, on any system (tools/release/test_macplay.py runs it):
##   godot --headless --path launcher --script res://tests/macplay_test.gd -- --payload=<file> --sig=<file> --key=<hex>
## the window size at Mac scale factors (main.gd fit_size; expected values worked out by hand in the comments), App Translocation, the
## package folder of an app bundle in folders with spaces, and the signed payload inventory of the macos-arm64 package. Prints
## "MACPLAY UNIT OK <n> checks" and exits 0 only when every check passed (and at least one ran).
extends SceneTree

const Main := preload("res://scripts/main.gd")
const Manifest := preload("res://scripts/core/manifest.gd")
const Updater := preload("res://scripts/core/updater.gd")

var _checks := 0
var _failed := 0


class MacInfo:
	extends RefCounted
	var version := "v1.3.0"
	var display_version := "v1.3.0"
	var repo := "Test/repo"
	var packaged := false
	var commit := ""
	var release_keys: Array = []   # lane SIGNKMS-1: the trusted key list
	var error := ""
	var macos_updates := true
	var test_build := true

	func platform() -> String:
		return "macos-arm64"

	func os_name() -> String:
		return OS.get_name()

	func tests_allowed() -> bool:
		return true

	func release_version() -> String:
		return version


func _init() -> void:
	# MacBook Air 13" at its default "looks like 1470 x 956": Godot's scale 2, 224 dpi, usable 2940 x 1824 px. 224 / 96 = 2.333:
	# 1280 x 720 -> 2986.7 x 1680, over 92 % of 2940 wide (2704.8): x 0.9056 -> 2704 x 1521 (at the Retina factor alone: 2560 x 1440)
	_eq(Main.fit_size("macOS", 2.0, 224, Vector2(2940, 1824)), Vector2i(2704, 1521), "MacBook Air: physical size like Windows at 100 %")
	_eq(Main.fit_size("macOS", 2.0, 0, Vector2(2940, 1824)), Vector2i(2560, 1440), "Mac screen without a DPI: the Retina factor")
	# a 1x external screen at 110 dpi: 1.146 -> 1466.7 x 825, fits 92 % of 1920 x 1055
	_eq(Main.fit_size("macOS", 1.0, 110, Vector2(1920, 1055)), Vector2i(1466, 825), "Mac 1x screen at 110 dpi")
	# a wrong 400 dpi on a Retina screen: at most 1.5 x the Retina factor = 3 -> 3840 x 2160 (fits 5120 x 2830)
	_eq(Main.fit_size("macOS", 2.0, 400, Vector2(5120, 2830)), Vector2i(3840, 2160), "Mac DPI factor capped at 1.5 x the scale")
	_eq(Main.fit_size("Windows", 1.0, 144, Vector2(3840, 2100)), Vector2i(1920, 1080), "Windows 150 % unchanged")
	_eq(Main.fit_size("Windows", 1.0, 96, Vector2(1920, 1040)), Vector2i.ZERO, "Windows 100 %: the project's size")
	_eq(Main.fit_size("Linux", 1.0, 96, Vector2(1280, 800)), Vector2i.ZERO, "Linux scale 1: the project's size")

	var exe := "/Volumes/Games b/My Games/Open BFME (1)/OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"
	_eq(Updater.package_root(exe, "macos-arm64"), "/Volumes/Games b/My Games/Open BFME (1)", "the folder of both apps, spaces kept")
	var moved := "/Volumes/T/AppTranslocation/1A2B3C4D-0000-0000-0000-000000000000/d/OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"
	_check(Updater.is_translocated(Updater.package_root(moved, "macos-arm64")), "an App Translocation copy is recognised")
	_check(not Updater.is_translocated(Updater.package_root(exe, "macos-arm64")), "a normal folder is not translocated")
	_check(not Updater.is_translocated("/Volumes/a/AppTranslocationNotes/OpenBFME"), "only the AppTranslocation folder itself counts")

	var u := Updater.new(MacInfo.new())
	u.set_launcher_dir(Updater.package_root(moved, "macos-arm64"))
	_check(u.translocated != "" and u.launcher_dir == "", "a translocated launcher has no package folder")
	_eq(u.no_game_reason(), Updater.TRANSLOCATED_TEXT, "Play says what to do about the translocation")
	_eq(Updater.TRANSLOCATED_TEXT, "macOS opened a temporary copy of the launcher, so it cannot find the game. " +
		"Quit, then use Finder to move the folder containing both apps out of Downloads and reopen the launcher. " +
		"If macOS blocks it, verify the download, then use System Settings > Privacy & Security > Open Anyway. " +
		"Do not override malware or damaged-app warnings.", "the translocation text gives safe Finder and first-open steps")
	u.set_launcher_dir("/Volumes/Games b/My Games/Open BFME (1)")
	_check(u.translocated == "" and u.launcher_dir == "/Volumes/Games b/My Games/Open BFME (1)", "a normal folder is the package folder")
	_check(u.no_game_reason().begins_with("No game is installed yet"), "no game: the reason is never empty")

	_payload()
	if _checks > 0 and _failed == 0:
		print("MACPLAY UNIT OK %d checks" % _checks)
		quit(0)
	else:
		print("MACPLAY UNIT FAIL %d of %d checks" % [_failed, _checks])
		quit(1)


func _payload() -> void:
	var a := {}
	for arg in OS.get_cmdline_user_args():
		for k in ["payload", "sig", "key"]:
			if arg.begins_with("--%s=" % k):
				a[k] = arg.substr(k.length() + 3)
	if a.size() != 3:
		_check(false, "the payload fixture arguments are given")
		return
	var bytes := FileAccess.get_file_as_bytes(a.payload)
	var sig := FileAccess.get_file_as_bytes(a.sig)
	var key: PackedByteArray = String(a.key).hex_decode()
	var v := Manifest.verify_payload(bytes, sig, [key], {"repo": "Test/repo", "platform": "macos-arm64"})
	_check(v.ok and v.payload.files.has("OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"),
		"the macos-arm64 payload inventory verifies (%s)" % v.error)
	_check(not Manifest.verify_payload(bytes, sig, [key], {"repo": "Test/repo", "platform": "linux-x64"}).ok,
		"a macos-arm64 inventory is not a Linux one")
	var other := key.duplicate()
	other[0] ^= 1
	_check(not Manifest.verify_payload(bytes, sig, [other], {"repo": "Test/repo", "platform": "macos-arm64"}).ok,
		"another key's inventory is refused")


func _eq(got, want, what: String) -> void:
	_check(got == want, "%s: got %s, expected %s" % [what, str(got), str(want)])


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		print("MACPLAY UNIT FAIL ", what)
