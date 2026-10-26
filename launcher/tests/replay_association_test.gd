extends SceneTree

const Association := preload("res://scripts/core/replay_association.gd")
var checks := 0
var failures := 0


func expect(value: bool, label: String) -> void:
	checks += 1
	if not value:
		failures += 1
		printerr("FAIL: " + label)


func _initialize() -> void:
	var sandbox := OS.get_environment("OPENBFME_TEST_ASSOCIATION_SANDBOX")
	if OS.get_name() != "Linux" or sandbox == "" or OS.get_environment("XDG_DATA_HOME") != sandbox.path_join("data") or OS.get_environment("XDG_CONFIG_HOME") != sandbox.path_join("config"):
		printerr("An isolated Linux XDG sandbox is required; never run against a real user's defaults")
		quit(1)
		return
	var exe := sandbox.path_join("path with spaces/OpenBFMELauncher")
	expect(Association.set_enabled(exe, false).ok, "opt out before the first installation")
	var apps := sandbox.path_join("data/applications")
	DirAccess.make_dir_recursive_absolute(apps)
	var other := FileAccess.open(apps.path_join("other.desktop"), FileAccess.WRITE)
	other.store_string("[Desktop Entry]\nType=Application\nName=Other\nExec=/bin/true\nMimeType=application/x-openbfme-replay;\n")
	other.close()
	expect(Association._run("xdg-mime", ["default", "other.desktop", Association.MIME]).ok, "install an existing default")
	var installed := Association.set_enabled(exe, true)
	expect(installed.ok, "register: " + installed.get("error", ""))
	expect(installed.get("previous", "") == "other.desktop", "remember previous default")
	expect(Association._run("xdg-mime", ["query", "default", Association.MIME]).get("text", "").strip_edges() == Association.DESKTOP, "XDG selects OpenBFME")
	expect(FileAccess.get_file_as_string(apps.path_join(Association.DESKTOP)) == Association.linux_entry(exe), "desktop entry quotes the launcher path")
	var file := sandbox.path_join("match.replay")
	var replay := FileAccess.open(file, FileAccess.WRITE)
	replay.store_buffer("OBFMEREPsynthetic".to_ascii_buffer())
	replay.close()
	expect(Association._run("xdg-mime", ["query", "filetype", file]).get("text", "").strip_edges() == Association.MIME, "the installed MIME database recognizes a replay")
	expect(Association.set_enabled(exe, true, "other.desktop").get("previous", "") == "other.desktop", "idempotent registration preserves previous default")
	expect(Association.set_enabled(exe, false, "other.desktop").ok, "unregister")
	expect(Association._run("xdg-mime", ["query", "default", Association.MIME]).get("text", "").strip_edges() == "other.desktop", "restore previous default")
	expect(not FileAccess.file_exists(apps.path_join(Association.DESKTOP)), "remove only our desktop entry")
	expect(not FileAccess.file_exists(sandbox.path_join("data/mime/packages/openbfme-replay.xml")), "remove our MIME definition")
	expect(FileAccess.file_exists(apps.path_join("other.desktop")), "keep the other app")
	# The same registration/removal path handles saves without touching replay defaults.
	var save_type := Association._type("save")
	expect(Association.set_enabled(exe, false, "", "save").ok, "save opt out before installation")
	expect(Association._run("xdg-mime", ["default", "other.desktop", save_type.mime]).ok, "existing save default")
	var save_installed := Association.set_enabled(exe, true, "", "save")
	expect(save_installed.ok, "register save: " + save_installed.get("error", ""))
	expect(save_installed.get("previous", "") == "other.desktop", "remember save default")
	expect(Association._run("xdg-mime", ["query", "default", save_type.mime]).get("text", "").strip_edges() == save_type.desktop, "XDG selects save handler")
	expect(FileAccess.get_file_as_string(apps.path_join(save_type.desktop)) == Association.linux_entry(exe, "save"), "save desktop routes --load")
	var save_path := sandbox.path_join("match.obfmesave")
	var save_file := FileAccess.open(save_path, FileAccess.WRITE)
	save_file.store_buffer("OBFMESAVsynthetic".to_ascii_buffer())
	save_file.close()
	expect(Association._run("xdg-mime", ["query", "filetype", save_path]).get("text", "").strip_edges() == save_type.mime, "installed MIME recognizes save")
	expect(Association.set_enabled(exe, true, "other.desktop", "save").get("previous", "") == "other.desktop", "save idempotent registration")
	expect(Association.set_enabled(exe, false, "other.desktop", "save").ok, "unregister save")
	expect(Association._run("xdg-mime", ["query", "default", save_type.mime]).get("text", "").strip_edges() == "other.desktop", "restore save default")
	expect(not FileAccess.file_exists(apps.path_join(save_type.desktop)), "remove save desktop")
	expect(not FileAccess.file_exists(sandbox.path_join("data/mime/packages/openbfme-save.xml")), "remove save MIME definition")
	expect(Association._run("xdg-mime", ["query", "default", Association.MIME]).get("text", "").strip_edges() == "other.desktop", "save opt-out preserves replay default")
	print("REPLAY LINUX ASSOCIATION: %d checks, %d failures" % [checks, failures])
	quit(0 if failures == 0 else 1)
