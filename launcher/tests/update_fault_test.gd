extends SceneTree

const BuildInfo := preload("res://scripts/core/build_info.gd")
const Updater := preload("res://scripts/core/updater.gd")

var updater: RefCounted
var scenario := ""
var tag := ""
var fired := false


func _init() -> void:
	call_deferred("_run")


func _value(prefix: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(prefix):
			return arg.substr(prefix.length())
	return ""


func _fault(point: String) -> void:
	var versions: String = updater.data_dir.path_join("versions")
	var partial := versions.path_join(tag + ".partial")
	var at: String = {
		"unpack-permission": "install-unpacking",
		"archive-full": "install-unpacking",
		"manifest-permission": "install-extracted",
		"manifest-full": "install-extracted",
		"signature-full": "install-extracted",
		"aside-permission": "install-unpacked",
		"swap-permission": "install-aside",
		"swap-missing-stage": "install-aside",
	}.get(scenario, "")
	if point != at:
		return
	fired = true
	match scenario:
		"unpack-permission", "aside-permission", "swap-permission":
			assert(FileAccess.set_unix_permissions(versions, 365) == OK)
		"manifest-permission":
			assert(FileAccess.set_unix_permissions(partial, 365) == OK)
		"archive-full", "manifest-full", "signature-full":
			pass  # The subprocess fixture redirects this file's flush to the kernel ENOSPC device.
		"swap-missing-stage":
			Updater.remove_tree(partial)
	print("FAULT fired " + scenario + " at " + point)


func _run() -> void:
	var info := BuildInfo.new()
	info.repo = "Test/repo"
	info.release_keys = [_value("--key=").hex_decode()]  # lane SIGNKMS-1: the trusted key list
	updater = Updater.new(info)
	assert(updater.set_test_api_base(_value("--origin=")) == "")
	scenario = _value("--scenario=")
	tag = _value("--tag=")
	updater.recover()
	var releases: Dictionary = updater.fetch_releases()
	assert(releases.ok)
	var release: Dictionary = releases.releases.filter(func(r): return r.tag == tag)[0]
	Updater.test_hook = _fault
	var result: Dictionary = updater.install(release, _value("--pick=") == "yes")
	Updater.test_hook = Callable()
	var versions: String = updater.data_dir.path_join("versions")
	FileAccess.set_unix_permissions(versions, 493)
	var partial := versions.path_join(tag + ".partial")
	if DirAccess.dir_exists_absolute(partial):
		FileAccess.set_unix_permissions(partial, 493)
	updater.recover()
	print("FAULT RESULT " + JSON.stringify({"fired": fired, "ok": result.ok, "error": result.error}))
	quit(0 if fired and not result.ok else 1)
