## godot --headless --path launcher --script res://tests/bfme1_assets_test.gd
## Optional BFME1_TEST_MANIFEST: also validate a captured service manifest.
extends SceneTree
const Aio = preload("res://scripts/core/aio_install.gd")
const Pins = preload("res://scripts/core/aio_pins.gd")
const Paths = preload("res://scripts/ui/paths.gd")
const Updater = preload("res://scripts/core/updater.gd")
const PlayScreen = preload("res://scripts/ui/play_screen.gd")
const LauncherMain = preload("res://scripts/main.gd")
const BuildInfo = preload("res://scripts/core/build_info.gd")
func _initialize() -> void:
	var u = Updater.new(BuildInfo.load_info())
	var aio = Aio.new(u)
	var test_dir := ProjectSettings.globalize_path("user://bfme1-assets-test")
	DirAccess.make_dir_recursive_absolute(test_dir)
	OS.set_environment("OPENBFME_GAME_USER_DIR", test_dir)
	u.data_dir = test_dir
	var pkg = Pins.PACKAGES[2]
	var generated := {"Guid": pkg.guid, "Game": pkg.game, "Type": 0, "Owner": pkg.owner, "Files": []}
	for f in pkg.files:
		generated.Files.append({"Name": f[0], "Md5": f[1], "Size": f[2], "Language": "ALL", "Url": Aio.FILES + "/" + pkg.owner + "/" + f[1]})
	var manifest: PackedByteArray = JSON.stringify(generated).to_utf8_buffer()
	if not OS.get_environment("BFME1_TEST_MANIFEST").is_empty():
		manifest = FileAccess.get_file_as_bytes(OS.get_environment("BFME1_TEST_MANIFEST"))
	assert(pkg.key == "bfme1" and pkg.game == 0 and pkg.files.size() == 346)
	var checked = aio.check_manifest(pkg, manifest)
	assert(checked.ok, str(checked))
	var broken = JSON.parse_string(manifest.get_string_from_utf8())
	for f in broken.Files:
		if f.Name == "Data.big": f.Md5 = "00000000000000000000000000000000"
	assert(not aio.check_manifest(pkg, JSON.stringify(broken).to_utf8_buffer()).ok)
	var folder := test_dir.path_join("BFME1")
	DirAccess.make_dir_recursive_absolute(folder)
	OS.set_environment("BFME1_INSTALL", folder)
	assert(aio.write_marker({"bfme1": folder}) == "")
	OS.set_environment("BFME1_INSTALL", "")
	assert(Paths.bfme1_folder() == folder)
	u.set_state("launcher", "game", "bfme1")
	var launch = u.game_arguments()
	assert(launch.ok and launch.args == PackedStringArray(["--", "--game=bfme1", "--bfme1-install=" + folder]))
	assert(u.game_arguments(PackedStringArray(["--", "--online-join=test"])).args == PackedStringArray(["--", "--online-join=test"]))
	u.set_state("launcher", "game", "bfme2")
	assert(u.game_arguments().args.is_empty())
	var screen = PlayScreen.new()
	assert(screen.game_picker.item_count == 2)
	assert(screen.game_picker.get_item_text(1) == "Battle for Middle-earth I")
	screen.free()
	print("BFME1 ASSETS OK: manifest validation, tamper rejection, saved path, launch arguments")
	quit()
