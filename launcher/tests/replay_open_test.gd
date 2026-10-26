## Pure headless checks. Test data is generated, not a retail recording. No OS association is changed.
extends SceneTree

const Replay := preload("res://scripts/core/replay_open.gd")
const Association := preload("res://scripts/core/replay_association.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")
const SHA := "0123456789abcdef0123456789abcdef01234567"

class RecordingUpdater extends "res://scripts/core/updater.gd":
	var started := []
	var builds := []

	func replay_builds() -> Array:
		return builds

	func play(version: String, args: PackedStringArray = PackedStringArray(), _before_start := Callable()) -> Dictionary:
		started.append({"version": version, "args": args})
		return {"ok": true, "error": ""}

var checks := 0
var failures := 0


func check(ok: bool, what: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		printerr("REPLAY FAIL: ", what)


func _string(file: FileAccess, value: String) -> void:
	var bytes := value.to_utf8_buffer()
	file.store_32(bytes.size())
	file.store_buffer(bytes)


func fixture(path: String, version := "v0.4.13", commit := SHA, format := 10, date := "2026-10-22") -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer("OBFMEREP".to_ascii_buffer())
	file.store_32(format)
	_string(file, "openbfme-profile 1\nprofile enhanced\nengine synthetic\n")
	if format >= 9:
		_string(file, version)
		_string(file, commit)
	if format >= 10:
		_string(file, date)
	# Only routing metadata here; the native test generates the complete playback fixture.


func _put_string(buffer: StreamPeerBuffer, value: String) -> void:
	var bytes := value.to_utf8_buffer()
	buffer.put_u32(bytes.size())
	buffer.put_data(bytes)


func save_fixture(path: String, version := "v0.4.12-nightly.20261022", commit := SHA, format := 3, date := "2026-10-22") -> void:
	var head := StreamPeerBuffer.new()
	head.put_u32(format)
	for field in [version, "synthetic engine", commit, "openbfme-profile 1\nprofile enhanced\n", "enhanced"]:
		_put_string(head, field)
	for number in [2, 100, 0]: # game kind, frame, state hash
		head.put_u32(number)
	for field in ["Synthetic save", "maps/synthetic", "Synthetic map", "2030-01-02", "03:04:05"]:
		_put_string(head, field)
	head.put_u32(0)
	_put_string(head, "")
	head.put_u32(0)
	head.put_u32(0)
	if format >= 2:
		head.put_u32(0)
		head.put_u32(0)
	if format >= 3:
		_put_string(head, date)
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer("OBFMESAV".to_ascii_buffer())
	file.store_32(1)
	for tag in ["HEAD", "LOG ", "SNAP"]:
		var bytes := head.data_array if tag == "HEAD" else PackedByteArray()
		var hash := HashingContext.new()
		hash.start(HashingContext.HASH_SHA256)
		if not bytes.is_empty():
			hash.update(bytes)
		file.store_buffer(tag.to_ascii_buffer())
		file.store_32(bytes.size())
		file.store_buffer(hash.finish())
		file.store_buffer(bytes)


func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	for args in [["--replay", "space λ file.replay"], ["--replay=space λ file.replay"], ["space λ file.replay"], ["--offline", "space λ file.replay"]]:
		var r := Replay.arguments(PackedStringArray(args))
		check(r.ok and r.path == "space λ file.replay", "argument round trip: " + str(args))
	for args in [["--load", "space λ file.obfmesave"], ["--load=space λ file.obfmesave"], ["space λ file.obfmesave"], ["--offline", "space λ file.obfmesave"]]:
		var r := Replay.arguments(PackedStringArray(args))
		check(r.ok and r.path == "space λ file.obfmesave", "save argument round trip: " + str(args))
	for args in [["--replay"], ["--replay="], ["--replay", "--play"], ["--replay", "x.replay", "--install=v1.2.3"], ["x.replay", "y.replay"], ["--replay=x.replay", "--replay=y.replay"], ["--replay", "x\n.replay"]]:
		var r := Replay.arguments(PackedStringArray(args))
		check(r.mode and not r.ok and r.error.contains("exactly one replay"), "refuse ambiguous arguments " + str(args))
	check(not Replay.arguments(["--play"]).mode, "ordinary Play unchanged")
	for args in [["--load"], ["--load="], ["--load", "--play"], ["--load=x.obfmesave", "--replay=y.replay"], ["x.obfmesave", "y.obfmesave"]]:
		var r := Replay.arguments(PackedStringArray(args))
		check(r.mode and not r.ok, "refuse ambiguous save arguments " + str(args))
	check(Replay.is_recording("DRAG.OBFMESAVE") and Replay.is_recording("DRAG.REPLAY") and not Replay.is_recording("x.txt"), "drop filter accepts only supported extensions")
	check(Replay.arguments(["--trust-key=test", "--replay", "x.replay"], ["--trust-key="]).ok, "test options permitted only when caller allows")
	check(not Replay.arguments(["--trust-key=test", "--replay", "x.replay"]).ok, "release rejects extra options")
	var path := OS.get_user_data_dir().path_join("synthetic λ replay.replay")
	fixture(path)
	var header := Replay.read_header(path)
	check(header.ok and header.version == "v0.4.13" and header.commit == SHA and header.date == "2026-10-22", "bounded dated header read")
	var builds := [{"version": "v0.4.14", "commit": SHA, "installed_version": "v0.4.14"},
		{"version": "v0.4.13", "commit": SHA, "installed_version": "v0.4.13"}]
	check(Replay.select_build(header, builds).version == "v0.4.13", "matching build, not newest")
	check(Replay.select_build({"ok": true, "version": "v0.4.13 Mac preview", "commit": SHA}, [{"version": "v0.4.13 Mac preview", "commit": SHA, "installed_version": "v0.4.13"}]).version == "v0.4.13", "full build label routes to installation tag")
	var wrong := Replay.select_build(header, [{"version": "v0.4.13", "commit": "a".repeat(40)}])
	check(not wrong.ok and wrong.error.contains("0.4.13 (2026-10-22)") and wrong.error.contains("launcher") and not wrong.error.contains(SHA), "same version different commit refuses by name, not hash")
	check(not Replay.select_build(header, []).ok, "no installed build")
	check(not Replay.select_build({"ok": true, "version": "v0.4.13", "commit": "none"}, builds).ok, "unknown provenance never guessed")
	var updater := RecordingUpdater.new(BuildInfo.load_info())
	updater.builds = builds
	check(updater.open_replay(path).ok, "synthetic metadata reaches the real launch handoff")
	check(updater.started == [{"version": "v0.4.13", "args": PackedStringArray(["--", "--replay", path])}], "path is a distinct argument, exact build starts")
	updater.builds = []
	check(not updater.open_replay(path).ok and updater.started.size() == 1, "refusal starts no process")
	fixture(path, "v0.4.13", SHA, 9)
	check(Replay.read_header(path).ok and Replay.read_header(path).date == "", "format 9 routes with unavailable date, never a guessed date")
	for format in [5, 6, 7, 8]:
		fixture(path, "", "", format)
		var r := Replay.read_header(path)
		check(not r.ok and r.error.contains("legacy") and r.error.contains("launcher") and r.error.contains("no version was guessed") and not r.error.contains("synthetic"), "legacy build provenance explained without identity/hash dump")
	fixture(path, "v0.4.13", SHA, 99)
	check(Replay.read_header(path).error.contains("replay version 99"), "future format refusal")
	fixture(path, "", SHA)
	check(not Replay.read_header(path).ok, "empty build version refused")
	fixture(path, "v0.4.13", "f".repeat(39))
	check(not Replay.read_header(path).ok, "malformed commit refused")
	fixture(path, "v0.4.13\n", SHA)
	check(not Replay.read_header(path).ok, "control in version refused")
	fixture(path, "a".repeat(129), SHA)
	check(not Replay.read_header(path).ok, "bounded version")
	fixture(path)
	var bytes := FileAccess.get_file_as_bytes(path)
	for cut in [0, 8, 12, 16, bytes.size() - 1]:
		var f := FileAccess.open(path, FileAccess.WRITE)
		f.store_buffer(bytes.slice(0, cut))
		f.close()
		check(not Replay.read_header(path).ok, "truncated header " + str(cut))
	var sparse := FileAccess.open(path, FileAccess.WRITE)
	sparse.seek(Replay.MAX_BYTES)
	sparse.store_8(0)
	sparse.close()
	check(Replay.read_header(path).error.contains("268435456"), "disk size cap before allocation")
	DirAccess.remove_absolute(path)
	check(Replay.read_header(path).error.contains("cannot open"), "missing file refusal")
	var save_path := OS.get_user_data_dir().path_join("synthetic λ save.obfmesave")
	save_fixture(save_path)
	var save_header := Replay.read_header(save_path)
	check(save_header.ok and save_header.date == "2026-10-22" and save_header.argument == "--load", "save build date is not save date, shared header result")
	updater.started.clear()
	updater.builds = [{"version": "v0.4.13", "commit": SHA}, {"version": save_header.version, "commit": SHA}]
	check(updater.open_recording(save_path).ok, "old save installed routes through shared updater")
	check(updater.started == [{"version": save_header.version, "args": PackedStringArray(["--", "--load", save_path])}], "old save starts its exact build, not newest; path remains one argument")
	updater.builds = [{"version": "v0.4.13", "commit": SHA}]
	var absent := updater.open_recording(save_path)
	check(not absent.ok and absent.error.contains("Made with Nightly 0.4.12 (2026-10-22).") and absent.error.contains("Install that version from the launcher") and not absent.error.contains(SHA), "uninstalled old save identifies version and date, offers launcher action")
	check(updater.started.size() == 1, "missing save build starts no process")
	updater.builds = [{"version": save_header.version, "commit": "a".repeat(40)}]
	check(not updater.open_recording(save_path).ok and updater.started.size() == 1, "same save tag with wrong commit never routed")
	save_fixture(save_path, "v0.4.13")
	updater.builds = builds
	check(updater.open_recording(save_path).ok and updater.started.back().version == "v0.4.13", "current build save loads through existing --load handoff")
	for format in [1, 2]:
		save_fixture(save_path, "v0.4.13", SHA, format)
		var legacy := Replay.read_header(save_path)
		check(legacy.ok and legacy.date == "" and Replay.select_build(legacy, builds).ok, "legacy save headers stay routable " + str(format))
		check(Replay.select_build(legacy, []).error.contains("build date unavailable"), "legacy save has no invented build date " + str(format))
	save_fixture(save_path, "v0.4.13", SHA, 3, "2026-99-01")
	check(not Replay.read_header(save_path).ok, "invalid save build date refused")
	save_fixture(save_path, "v0.4.13", SHA, 99)
	check(Replay.read_header(save_path).error.contains("save header version 99"), "future save header refused")
	save_fixture(save_path)
	var saved := FileAccess.get_file_as_bytes(save_path)
	for cut in [0, 8, 12, 52, 60]:
		var file := FileAccess.open(save_path, FileAccess.WRITE)
		file.store_buffer(saved.slice(0, cut))
		file.close()
		check(not Replay.read_header(save_path).ok, "truncated save header " + str(cut))
	var corrupt := FileAccess.open(save_path, FileAccess.WRITE)
	saved[60] ^= 1
	corrupt.store_buffer(saved)
	corrupt.close()
	check(Replay.read_header(save_path).error.contains("SHA-256"), "save HEAD digest checked before routing")
	DirAccess.remove_absolute(save_path)
	var native_save := OS.get_environment("OPENBFME_SAVE_FIXTURE")
	if native_save != "":
		var native_header := Replay.read_header(native_save)
		check(native_header.ok and native_header.version == "v0.4.12-nightly.20261022" and native_header.date == "2026-10-22", "native synthetic save codec agrees with launcher codec")
		updater.builds = [{"version": native_header.get("version", ""), "commit": native_header.get("commit", "")}]
		check(updater.open_recording(native_save).ok and updater.started.back().args == PackedStringArray(["--", "--load", native_save]), "native fixture reaches shared save launch handoff")
	var commands := Association.windows_commands("C:\\Games with spaces\\OpenBFME Launcher.exe")
	for version in ["/forged/build/label", "Z:\\recordings\\forged", "v1.2.3\n", "v1.2.3\t", "v1.2.3 ", "v01.2.3", "v1.2.3-beta.1", "v1.2.3+meta",
			"v1.2.3-alpha.01", "v1.2.3-nightly.-1", "v1.2.3-nightly.1000000000", "v1000000000.2.3", "v1.2.3-4-g0123456789", "Made with <junk>"]:
		fixture(path, version, SHA)
		var bad_replay := updater.open_recording(path)
		check(not bad_replay.ok and bad_replay.error.contains("an unknown build") and not bad_replay.error.contains(version), "untrusted replay build never echoed: " + version.c_escape())
		save_fixture(save_path, version, SHA)
		var bad_save := updater.open_recording(save_path)
		check(not bad_save.ok and bad_save.error.contains("an unknown build") and not bad_save.error.contains(version), "untrusted save build never echoed: " + version.c_escape())
	for date in ["/not/date", "2026-00-01", "2026-01-00", "2026-13-01", "2026-01-32", "2026-1-01", "2026-xx-01", "2026-01-01\n"]:
		var bad := Replay.select_build({"ok": true, "version": "v1.2.3", "date": date, "commit": SHA}, [])
		check(not bad.ok and bad.error.contains("an unknown build") and not bad.error.contains(date), "untrusted date never echoed")
	for version in ["v0.0.0", "v1.2.3", "v0.4.0-alpha.4", "v0.3.0-preview.4", "v0.4.12-nightly.20261022", "v1.2.3-nightly.7"]:
		var known := Replay.select_build({"ok": true, "version": version, "date": "2026-10-22", "commit": SHA}, [])
		check(not known.ok and not known.error.contains("an unknown build") and known.error.contains("2026-10-22"), "supported version/date/channel preserved")
	DirAccess.remove_absolute(path)
	DirAccess.remove_absolute(save_path)
	check(commands.size() == 3 and commands[1].has("\"C:\\Games with spaces\\OpenBFME Launcher.exe\" -- --replay \"%1\""), "Windows quoted per-user command")
	check(commands.all(func(c): return c[1].begins_with("HKCU\\Software\\Classes\\")), "Windows per-user only")
	var entry := Association.linux_entry("/opt/Open BFME/100%/Launcher")
	check(entry.contains('Exec="/opt/Open BFME/100%%/Launcher" -- --replay %f'), "Linux literal percent, spaces and one file argument")
	check(entry.contains("MimeType=application/x-openbfme-replay;"), "Linux MIME type")
	check(Association.XML.contains('pattern="*.replay"') and Association.XML.contains('value="OBFMEREP"'), "MIME extension and magic")
	var mimeapps := "[Default Applications]\napplication/x-openbfme-replay=openbfme-replay.desktop;other.desktop;\ntext/plain=editor.desktop;\n[Added Associations]\napplication/x-openbfme-replay=openbfme-replay.desktop;\n"
	check(Association.without_handler(mimeapps) == "[Default Applications]\napplication/x-openbfme-replay=other.desktop;\ntext/plain=editor.desktop;\n[Added Associations]\napplication/x-openbfme-replay=\n", "opt-out removes only this handler, preserves other applications")
	var save_commands := Association.windows_commands("C:\\Games with spaces\\OpenBFME Launcher.exe", "save")
	check(save_commands[1].has('"C:\\Games with spaces\\OpenBFME Launcher.exe" -- --load "%1"'), "Windows save command quoting")
	check(save_commands[2][1] == "HKCU\\Software\\Classes\\.obfmesave\\OpenWithProgids", "Windows save extension and per-user registration")
	var save_entry := Association.linux_entry("/opt/Open BFME/100%/Launcher", "save")
	check(save_entry.contains('Exec="/opt/Open BFME/100%%/Launcher" -- --load %f') and save_entry.contains("MimeType=application/x-openbfme-save;"), "Linux save command quoting and MIME")
	check(Association.mime_xml("save").contains('pattern="*.obfmesave"') and Association.mime_xml("save").contains('value="OBFMESAV"'), "save MIME magic and extension")
	check(Association.without_handler("application/x-openbfme-save=openbfme-save.desktop;other.desktop;\n", "save") == "application/x-openbfme-save=other.desktop;\n", "save opt-out preserves other handlers")
	print("REPLAY LAUNCHER: %d checks, %d failures" % [checks, failures])
	quit(0 if failures == 0 else 1)
