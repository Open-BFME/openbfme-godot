## godot --headless --path launcher --script res://tests/bfme1_assets_test.gd   (tools/release/test_aio.py test_bfme1_assets_script)
## Optional BFME1_TEST_MANIFEST: also validate a captured service manifest.
## Community PR #36: the optional BFME1 package (Pins.BFME1_PACKAGES, restored on the owner's decision of 2026-10-18) with the real pins:
## its own manifest passes the AIO checks and a changed one does not; it is never part of the base download; the download is refused
## unless BFME1 is the chosen game; its handoff is found and passed to the game. Prints "BFME1 ASSETS OK" or "BFME1 ASSETS fail" lines.
extends SceneTree
const Aio = preload("res://scripts/core/aio_install.gd")
const Pins = preload("res://scripts/core/aio_pins.gd")
const Paths = preload("res://scripts/ui/paths.gd")
const Updater = preload("res://scripts/core/updater.gd")
const PlayScreen = preload("res://scripts/ui/play_screen.gd")
const BuildInfo = preload("res://scripts/core/build_info.gd")

var _failures := 0


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		print("BFME1 ASSETS fail: " + what)


func _manifest(pkg: Dictionary) -> Dictionary:
	var m := {"Guid": pkg.guid, "Game": float(pkg.game), "Type": 0.0, "Owner": pkg.owner, "Files": []}
	for f in pkg.files:
		m.Files.append({"Name": str(f[0]).replace("/", "\\"), "Md5": f[1], "Size": float(f[2]), "Language": "ALL", "Url": Aio.FILES + "/" + pkg.owner + "/" + f[1]})
	return m


func _refused(aio, pkg: Dictionary, m: Dictionary, why: String) -> void:
	var r: Dictionary = aio.check_manifest(pkg, JSON.stringify(m).to_utf8_buffer())
	_check(not r.ok and r.error.contains(why), "refused (%s): %s" % [why, str(r)])


func _initialize() -> void:
	var u = Updater.new(BuildInfo.load_info())
	var aio = Aio.new(u)
	var test_dir := ProjectSettings.globalize_path("user://bfme1-assets-test")
	DirAccess.make_dir_recursive_absolute(test_dir)
	OS.set_environment("OPENBFME_GAME_USER_DIR", test_dir)
	u.data_dir = test_dir

	# the pins: one optional package, never in the base download
	_check(Pins.PACKAGES.size() == 2 and Pins.PACKAGES.all(func(p): return p.guid != "original-BFME1" and p.key != "bfme1"), "the base download has no BFME1")
	_check(Pins.BFME1_PACKAGES.size() == 1, "one optional BFME1 package")
	var pkg: Dictionary = Pins.BFME1_PACKAGES[0]
	_check(pkg.key == "bfme1" and pkg.guid == "original-BFME1" and pkg.game == 0 and pkg.folder == "BFME1" and pkg.files.size() == 346, "the BFME1 package row")
	_check(aio.packages == Pins.PACKAGES and aio.choice == Aio.GAMES, "a new install offers the base download")

	# its own manifest passes; a changed one is refused before any download
	var manifest := JSON.stringify(_manifest(pkg)).to_utf8_buffer()
	if not OS.get_environment("BFME1_TEST_MANIFEST").is_empty():
		manifest = FileAccess.get_file_as_bytes(OS.get_environment("BFME1_TEST_MANIFEST"))
	var checked: Dictionary = aio.check_manifest(pkg, manifest)
	_check(checked.ok and checked.files.size() == 346, "the package's manifest passes: %s" % str(checked).left(300))
	var m := _manifest(pkg)
	for f in m.Files:
		if f.Name == "lotrbfme.exe":
			f.Md5 = "00000000000000000000000000000000"
			f.Url = Aio.FILES + "/" + pkg.owner + "/" + f.Md5
	_refused(aio, pkg, m, "lists lotrbfme.exe with MD5 00000000000000000000000000000000")
	m = _manifest(pkg)
	for f in m.Files:
		if f.Name == "dsound.dll":
			f.Size = float(f.Size + 1)
	_refused(aio, pkg, m, "lists dsound.dll with")
	m = _manifest(pkg)
	m.Files = m.Files.filter(func(f): return f.Name != "worldbuilder.exe")
	_refused(aio, pkg, m, "no longer lists worldbuilder.exe")
	m = _manifest(pkg)
	m.Files.append({"Name": "patch.exe", "Md5": m.Files[0].Md5, "Size": 0.0, "Language": "ALL", "Url": m.Files[0].Url})
	_refused(aio, pkg, m, "lists patch.exe, which OpenBFME's list")
	m = _manifest(pkg)
	m.Game = 1.0
	_refused(aio, pkg, m, "is no longer the base game package")

	# opt-in: refused unless BFME1 is the chosen game (nothing is asked of the service: the refusal comes first)
	aio.set_enabled(true)
	_check(aio.choose("bfme3") != "" and aio.choice == Aio.GAMES, "an unknown download is refused")
	_check(aio.choose(Aio.BFME1) == "" and aio.chosen() == Pins.BFME1_PACKAGES, "BFME1 can be chosen")
	_check(aio.consent_text().contains("BFME Arena (bfmeladder.com), a third-party service. OpenBFME does not host or distribute it."), "the consent names BFME Arena")
	u.set_state("launcher", "game", "bfme2")
	var r: Dictionary = aio.install(test_dir.path_join("games"), true)
	_check(not r.ok and r.kind == Aio.REFUSED and r.error.contains("offered only when Battle for Middle-earth I is the chosen game"), "refused for BFME II: %s" % str(r))
	_check(not DirAccess.dir_exists_absolute(test_dir.path_join("games")), "nothing was created")

	# the handoff: its own file (never downloaded-installs.cfg), found by the launcher and passed to the game
	var folder := test_dir.path_join("BFME1")
	DirAccess.make_dir_recursive_absolute(folder)
	OS.set_environment("BFME1_INSTALL", "")
	DirAccess.remove_absolute(test_dir.path_join(Aio.MARKER))
	_check(aio.write_bfme1_marker(folder) == "", "the BFME1 handoff is written")
	_check(not FileAccess.file_exists(test_dir.path_join(Aio.MARKER)), "the RotWK / BFME2 handoff is untouched")
	_check(Paths.bfme1_folder() == folder, "the downloaded folder is found (%s)" % Paths.bfme1_folder())
	u.set_state("launcher", "game", "bfme1")
	var launch = u.game_arguments()
	_check(launch.ok and launch.args == PackedStringArray(["--", "--game=bfme1", "--bfme1-install=" + folder]), "Play passes it: %s" % str(launch))
	_check(u.game_arguments(PackedStringArray(["--", "--online-join=test"])).args == PackedStringArray(["--", "--online-join=test", "--game=bfme2"]), "online joins from BFME1 use RotWK")
	u.set_state("launcher", "game", "bfme2")
	_check(u.game_arguments().args == PackedStringArray(["--", "--game=bfme2"]), "RotWK starts with its explicit game argument")
	var screen = PlayScreen.new()
	_check(screen.game_picker.item_count == 3 and screen.game_picker.get_item_text(1) == "Battle for Middle-earth I 1.03", "the game picker")
	screen.free()
	print("BFME1 ASSETS OK: pins, manifest checks, opt-in, handoff, launch arguments" if _failures == 0 else "BFME1 ASSETS FAILED: %d" % _failures)
	quit(0 if _failures == 0 else 1)
