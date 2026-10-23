extends SceneTree

const Manifest := preload("res://scripts/core/manifest.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")
const Updater := preload("res://scripts/core/updater.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")

var _checks := 0
var _failed := 0


class MacBuildInfo:
	extends RefCounted
	var version := "v1.0.0"
	var repo := "Test/repo"
	var packaged := true
	var commit := "0123456789abcdef0123456789abcdef01234567"
	var release_keys: Array = []   # lane SIGNKMS-1: the trusted key list
	var error := ""

	func platform() -> String:
		return "macos-universal"


func _init() -> void:
	var launcher := "/Applications/OpenBFME/OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher"
	_check(Updater.package_root(launcher, "macos-universal") == "/Applications/OpenBFME", "macOS launcher resolves the package root")
	_check(Updater.GAME_EXE["macos-universal"] == "OpenBFME.app/Contents/MacOS/OpenBFME",
		"macOS game executable resolves through its sibling app bundle")
	_check(Updater.LAUNCHER_EXE["macos-universal"] == "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher",
		"macOS launcher executable resolves within its app bundle")
	_check(Updater.package_root("/opt/openbfme/OpenBFMELauncher.x86_64", "linux-x64") == "/opt/openbfme",
		"Linux launcher package root stays unchanged")
	if OS.get_name() == "macOS":
		_check(BuildInfo.new().platform() == "macos-universal", "macOS is not identified as Linux")

	var updater := Updater.new(MacBuildInfo.new())
	_check(updater.macos_updates_unsupported(), "macOS automated updates are unsupported")
	_check(updater.launcher_dir == Updater.package_root(OS.get_executable_path(), "macos-universal"),
		"packaged macOS launcher initializes its containing package folder")
	var fetched := updater.fetch_releases()
	_check(not fetched.ok and fetched.error.contains("macOS automated updates are unsupported"), "macOS release fetch fails closed")
	var installed := updater.install({}, false)
	_check(not installed.ok and installed.error.contains("macOS automated updates are unsupported"), "macOS install fails closed")
	var staged := updater.stage_self_update({})
	_check(not staged.ok and staged.error.contains("macOS launcher self-update is unsupported"), "macOS self-update fails closed")
	var self_update := SelfUpdate.on_start(updater, PackedStringArray(["--self-update"]))
	_check(not self_update.exit, "macOS --self-update does not enter the executable swap")
	_manifest_payload()
	_game_discovery()
	if _failed == 0:
		print("MACOS UNIT OK %d checks" % _checks)
		quit(0)
	else:
		print("MACOS UNIT FAIL %d of %d checks" % [_failed, _checks])
		quit(1)


func _manifest_payload() -> void:
	var args := {}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--payload="):
			args.payload = arg.trim_prefix("--payload=")
		elif arg.begins_with("--sig="):
			args.sig = arg.trim_prefix("--sig=")
		elif arg.begins_with("--key="):
			args.key = arg.trim_prefix("--key=")
	if not args.has("payload") or not args.has("sig") or not args.has("key"):
		_check(false, "signed macOS payload fixture arguments are present")
		return
	var bytes := FileAccess.get_file_as_bytes(args.payload)
	var sig := FileAccess.get_file_as_bytes(args.sig)
	var key: PackedByteArray = args.key.hex_decode()
	_check(not "macos-universal" in Manifest.PLATFORMS and "macos-universal" in Manifest.PAYLOAD_PLATFORMS,
		"macOS is allowed for signed payloads but not release manifests")
	var verified := Manifest.verify_payload(bytes, sig, [key], {"repo": "Test/repo", "version": "v1.0.0", "platform": "macos-universal"})
	_check(verified.ok, "signed macOS bundle inventory verifies (%s)" % verified.error)
	if not verified.ok:
		return
	var root := OS.get_user_data_dir().path_join("macos_payload_unit")
	DirAccess.make_dir_recursive_absolute(root)
	for rel in verified.payload.files:
		var path: String = root.path_join(rel)
		DirAccess.make_dir_recursive_absolute(path.get_base_dir())
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(PackedByteArray([1, 2, 3, 4]))
		file.close()
	_check(Manifest.check_files(root, verified.payload.files) == "", "both nested app inventories match the signed payload")
	var changed := bytes.get_string_from_utf8().replace("macos-universal", "linux-x64").to_utf8_buffer()
	_check(not Manifest.verify_payload(changed, sig, [key], {"repo": "Test/repo"}).ok, "tampered macOS payload is rejected")
	var first: String = verified.payload.files.keys()[0]
	var file := FileAccess.open(root.path_join(first), FileAccess.WRITE)
	file.store_buffer(PackedByteArray([4, 3, 2, 1]))
	file.close()
	_check(Manifest.check_files(root, verified.payload.files).contains("signed SHA-256"), "tampered bundled app file is rejected")


func _game_discovery() -> void:
	var old_rotwk := OS.get_environment("ROTWK_INSTALL")
	var old_bfme2 := OS.get_environment("BFME2_INSTALL")
	OS.set_environment("ROTWK_INSTALL", "")
	OS.set_environment("BFME2_INSTALL", "")
	var data := OS.get_user_data_dir().path_join("macos_path_unit")
	DirAccess.make_dir_recursive_absolute(data)
	DirAccess.remove_absolute(data.path_join(Paths.CONFIG))
	DirAccess.remove_absolute(data.path_join(Paths.MARKER))
	var missing := Paths.game_folders(data)
	_check(not missing.found and missing.source == "", "game discovery reports missing folders")
	var config := FileAccess.open(data.path_join(Paths.CONFIG), FileAccess.WRITE)
	config.store_string("ROTWK_INSTALL=%s\nBFME2_INSTALL=%s\n" % [data.path_join("missing-rotwk"), data.path_join("missing-bfme2")])
	config.close()
	missing = Paths.game_folders(data)
	_check(not missing.found and missing.source == "config", "remembered folders must both exist")
	OS.set_environment("ROTWK_INSTALL", old_rotwk)
	OS.set_environment("BFME2_INSTALL", old_bfme2)


func _check(ok: bool, message: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		print("MACOS UNIT FAIL ", message)
