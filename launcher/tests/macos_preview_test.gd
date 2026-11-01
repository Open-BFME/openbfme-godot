extends SceneTree

const BuildInfo := preload("res://scripts/core/build_info.gd")
const Updater := preload("res://scripts/core/updater.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")
const VERSION := "v0.4.6 Mac Unstable Preview (Apple Silicon)"

var checks := 0
var failed := 0


class PreviewUpdater:
	extends Updater
	var verified := {"ok": true, "error": "", "manifest": {"version": "v0.4.7"}}
	var assets_fetched := 0
	var started := {}

	func release_manifest(_release: Dictionary) -> Dictionary:
		return verified

	func fetch_asset(_release: Dictionary, _asset: Dictionary) -> Dictionary:
		assets_fetched += 1
		return {"ok": false, "error": "unexpected asset download"}

	func _play_bundled(bundle: Dictionary, _args: PackedStringArray, _before_start: Callable = Callable()) -> Dictionary:
		started = bundle
		return {"ok": true, "error": "", "pid": 1}


class PlatformInfo:
	extends BuildInfo
	var target := "linux-x64"

	func platform() -> String:
		return target


func _init() -> void:
	var info := BuildInfo.new()
	info.version = VERSION
	info.macos_preview = true
	_check((info.platform() == "macos-arm64-unstable-preview") == (OS.get_name() == "macOS"),
		"preview flag is effective only on macOS")
	for platform in ["linux-x64", "windows-x64"]:
		var regular := PlatformInfo.new()
		regular.macos_preview = true
		regular.target = platform
		var standard := Updater.new(regular)
		_check(not standard.macos_preview() and not standard.macos_updates_unsupported(), platform + " keeps standard update policy")
		_check(Updater.package_root("/package/launcher", platform) == "/package", platform + " keeps package root")
	if OS.get_name() != "macOS":
		print("MAC PREVIEW native bundle checks SKIP: macOS required")
		quit(77 if failed == 0 else 1)
		return
	var u := PreviewUpdater.new(info)
	var root := OS.get_user_data_dir().path_join("mac-preview-unit")
	Updater.remove_tree(root)
	DirAccess.make_dir_recursive_absolute(root)
	u.data_dir = root.path_join("state")
	DirAccess.make_dir_recursive_absolute(u.data_dir)
	u.set_launcher_dir(root)
	u.set_auto_update(false)
	_check(not u.auto_update(), "startup-check switch persists off")
	u.set_auto_update(true)
	_check(u.auto_update(), "startup-check switch persists on")
	_check(u.bundled().is_empty(), "missing bundled game is refused")
	_write(root.path_join("OpenBFME.app/Contents/MacOS/OpenBFME"), "fixture")
	_write(root.path_join("OpenBFME.app/Contents/Resources/OpenBFME.pck"), "fixture")
	_write(root.path_join("VERSION"), "OpenBFME " + VERSION + "\n")
	_check(u.check_bundled().ok, "explicit Mac preview needs no signed payload")
	_check(u.play_version() == VERSION, "only bundled version is offered")
	DirAccess.make_dir_recursive_absolute(u.data_dir.path_join("versions").path_join(VERSION))
	_check(u.installed().is_empty() and u.plays_bundled(VERSION), "stale cache cannot replace the bundled game")
	_check(u.play(VERSION).ok and u.started.path == root, "Play dispatches to the sibling game bundle")
	_check(not u.play("v99.0.0").ok, "Play refuses any other version")
	_check(Updater.package_root(root.path_join("OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"), info.platform()) == root,
		"launcher resolves its containing package")
	var notice := u.macos_preview_notice({})
	_check(notice.ok and notice.newer and notice.message == "A newer version may be on the releases page", "newer verified version is advisory")
	_check(notice.url == "https://github.com/Open-BFME/openbfme-godot/releases", "advisory links to releases")
	u.verified.manifest.version = "v0.4.6"
	_check(not u.macos_preview_notice({}).newer, "same base version is not an update despite preview label")
	u.verified = {"ok": false, "error": "invalid signature"}
	_check(not u.macos_preview_notice({}).ok, "unverified manifest cannot announce an update")
	_check(not u.install({}, false).ok and not u.install({}, true).ok, "automatic and explicit installation are refused")
	_check(not u.stage_self_update({}).ok, "self-update cannot download another platform")
	_check(not SelfUpdate.on_start(u, PackedStringArray(["--self-update"])).exit, "startup never swaps a Mac app")
	_check(u.assets_fetched == 0, "no package assets were requested")
	_write(root.path_join("VERSION"), "OpenBFME v99.0.0\n")
	_check(u.bundled().is_empty(), "mismatched sibling version is refused")
	Updater.remove_tree(root)
	print("MAC PREVIEW UNIT %s: %d checks, %d failures" % ["OK" if failed == 0 else "FAIL", checks, failed])
	quit(0 if failed == 0 else 1)


func _write(path: String, contents: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(contents)
	file.close()


func _check(ok: bool, message: String) -> void:
	checks += 1
	if not ok:
		failed += 1
		print("MAC PREVIEW FAIL: ", message)
