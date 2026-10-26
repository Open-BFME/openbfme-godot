## Lane MODS-3: the one-click mods' rules (scripts/core/mod_catalog.gd) on a made-up catalogue in the service's JSON shape
## (tests/fixtures/mods/catalogue.json: invented packages, no real mod data), and the active mod's game arguments (scripts/core/
## mod_install.gd active_args) on a made-up mods folder. No network.
##
##   godot --headless --path launcher --script res://tests/mod_catalog_test.gd
##
## Run by tools/release/test_mods.py. Prints "MODS fail: ..." lines and "MODS OK <n> checks"; exit 0 only when every check passed.
extends SceneTree

const ModCatalog := preload("res://scripts/core/mod_catalog.gd")
const ModInstall := preload("res://scripts/core/mod_install.gd")

const FIXTURE := "res://tests/fixtures/mods/catalogue.json"

var _checks := 0
var _failed := 0
var _fx: Dictionary
var _fetched := {}


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		print("MODS fail: ", what)


func _eq(got: Variant, want: Variant, what: String) -> void:
	_check(got == want, "%s: got %s, expected %s" % [what, JSON.stringify(got), JSON.stringify(want)])


func _init() -> void:
	_fx = JSON.parse_string(FileAccess.get_file_as_string(FIXTURE))
	_check(typeof(_fx) == TYPE_DICTIONARY, "the fixture %s reads" % FIXTURE)
	if typeof(_fx) != TYPE_DICTIONARY:
		_end()
		return
	for g in _fx.entries:
		var c := ModCatalog.check_entry(_fx.entries[g], g, _fx.files_origin)
		if g == "test-broken":
			_check(not c.ok and "not a plain relative path" in c.error, "the package with ..\\..\\escape.big is refused: " + c.error)
		else:
			_check(c.ok, "the made-up package %s is accepted (%s)" % [g, c.error])
			if c.ok:
				_fetched[g] = c.entry
	_query()
	_entry_checks()
	_layouts()
	_grouping()
	_args()
	_end()


func _end() -> void:
	if _failed == 0:
		print("MODS OK %d checks" % _checks)
	else:
		print("MODS FAIL %d of %d checks" % [_failed, _checks])
	quit(0 if _failed == 0 else 1)


func _query() -> void:
	var page: Array = _fx.query["0"][0]
	var q := ModCatalog.parse_query(JSON.stringify(page).to_utf8_buffer())
	_check(q.ok, "a page of the list parses")
	_eq(q.items.map(func(i): return i.guid), ["test-patch-99", "test-realms"], "the base game (original-RotWK) is not a catalogue item")
	_eq([q.items[1].name, q.items[1].version, q.items[1].author, q.items[1].type, q.items[1].game], ["Example Realms", "1.2.0", "Realm Builders", 0, 2], "an item's fields")
	_eq(ModCatalog.parse_query(PackedByteArray()).ok, false, "an empty answer is refused")
	_eq(ModCatalog.parse_query("{}".to_utf8_buffer()).ok, false, "an object instead of a list is refused")
	_eq(ModCatalog.parse_query("[{\"Name\": \"x\", \"Type\": 0, \"Game\": 2}]".to_utf8_buffer()).ok, false, "an item without a Guid is refused")
	_eq(ModCatalog.parse_query("[{\"Guid\": \"../x\", \"Name\": \"x\", \"Type\": 0, \"Game\": 2}]".to_utf8_buffer()).ok, false, "a package id with ../ is refused")
	_eq(ModCatalog.parse_query("[{\"Guid\": \"a\", \"Name\": \"x\", \"Type\": 0.5, \"Game\": 2}]".to_utf8_buffer()).ok, false, "a fractional Type is refused")
	for good in ["0ff0ab7a-3baf-4f63-b52c-6e0b5e104481", "official-2", "test.v2_b"]:
		_check(ModCatalog.safe_guid(good), "a package id is accepted: " + good)
	for bad in ["", ".", "..", ".hidden", "a/b", "a\\b", "C:x", "a b", "x".repeat(81), "naïve"]:
		_check(not ModCatalog.safe_guid(bad), "a package id is refused: '%s'" % bad)


func _entry_checks() -> void:
	var origin: String = _fx.files_origin
	var good: Dictionary = _fx.entries["test-realms"]
	var cases := [
		[func(m): m.Guid = "test-other", "answered with the package 'test-other'"],
		[func(m): m.Files[0].Url = "https://example.org/x/" + m.Files[0].Md5, "not in the service's file store"],
		[func(m): m.Files[0].Url = origin + "/u/" + "0".repeat(32), "not in the service's file store"],
		[func(m): m.Files[0].Url = origin + "/../" + m.Files[0].Md5, "not in the service's file store"],
		[func(m): m.Files[0].Md5 = "ZZ", "malformed MD5"],
		[func(m): m.Files[0].Size = 12.5, "with the size 12.5"],
		[func(m): m.Files[0].Size = -1, "with the size -1"],
		[func(m): m.Files[0].Name = "C:\\Windows\\evil.big", "not a plain relative path"],
		[func(m): m.Files[0].Name = "/etc/evil.big", "not a plain relative path"],
		[func(m): m.Files[0].Name = "lang\\..\\..\\evil.big", "not a plain relative path"],
		[func(m): m.Dependencies = ["../../x"], "not a package id OpenBFME accepts"],
		[func(m): m.Dependencies = [7], "not a package id OpenBFME accepts"],
		[func(m): m.Dependencies = "test-realms", "dependency list OpenBFME does not recognise"],
		[func(m): m.erase("Files"), "has no file list"],
		[func(m): m.Type = 1.5, "has no name, game or type"],
	]
	for c in cases:
		var m: Dictionary = good.duplicate(true)
		c[0].call(m)
		var r := ModCatalog.check_entry(m, "test-realms", origin)
		_check(not r.ok and c[1] in r.error, "a changed package is refused with '%s' (got '%s')" % [c[1], r.error])
	var asked := ModCatalog.check_entry(good, "test-realms:1.2.0", origin)
	_check(asked.ok, "an entry asked for as guid:version is the guid's")


## {files: ["part/rel <- layer"], edits, left_out} of one resolve
func _layout(guid: String, enh: Array) -> Dictionary:
	var r := ModCatalog.resolve(guid, enh, _fetched)
	if not r.ok:
		return {"ok": false, "error": r.error, "missing": r.missing}
	return {"ok": true, "status": r.status, "files": r.plan.files.map(func(f): return "%s/%s <- %s" % [f.part, f.rel, f.layer]),
		"edits": Array(r.plan.engine_edits), "left_out": Array(r.plan.left_out), "layers": (r.first + r.rest).map(func(e): return e.guid),
		"summary": ModCatalog.summary(r.entry, r.first, r.rest, r.plan, r.enhancements), "plan": r.plan}


func _layouts() -> void:
	# a data mod alone: English and ALL files only, its launcher and mod.txt left out, no engine edits
	var realms := _layout("test-realms", [])
	_eq(realms.status, ModCatalog.READY, "Example Realms is ready")
	_eq(realms.files, ["install/__realms_data.big <- Example Realms", "install/__realms_ini.big <- Example Realms", "install/lang/English.big <- Example Realms",
		"install/lang/englishaudio.big <- Example Realms", "install/worldbuilder_noeula.txt <- Example Realms"], "Example Realms' layout")
	_eq(realms.left_out, ["Example Realms: Realms_Launcher.exe (a tool)", "Example Realms: mod.txt"], "Example Realms' files left out")
	_eq(realms.edits, [], "Example Realms has no engine edits")
	var sizes := {}
	for f in realms.plan.files:
		sizes[f.rel] = f.size
	_eq([sizes["lang/English.big"], sizes["worldbuilder_noeula.txt"]], [0, 0], "Size 0 stays 0 (unknown: the download asks the server)")
	# L1 wins: the enhancements' files replace the package's, in the order they were picked
	var with := _layout("test-realms", ["test-realms-nointro", "test-realms-font"])
	_eq(with.files, ["install/_____short_intro.big <- Realms No Intro", "install/__realms_ini.big <- Realms No Intro",
		"install/lang/English.big <- Realms Big Font", "install/__realms_data.big <- Example Realms", "install/lang/englishaudio.big <- Example Realms",
		"install/worldbuilder_noeula.txt <- Example Realms"], "Example Realms with two enhancements")
	_eq(with.layers, ["test-realms-nointro", "test-realms-font", "test-realms"], "the layers L1, L2")
	_eq(with.summary.enhancements, ["test-realms-nointro", "test-realms-font"], "the record names the enabled enhancements")
	var s: Dictionary = with.summary
	_eq([s.schema, s.guid, s.name, s.version, s.type, s.parts, s.files], [ModCatalog.SCHEMA, "test-realms", "Example Realms", "1.2.0", 0, ["install"], 6], "the record's fields")
	_eq(s.bytes, 1100 + 1300 + 1700 + 5000 + 2200, "the record's bytes (files listed without a size count 0, as the tool's)")
	_eq(s.layers.map(func(l): return l.guid), ["test-realms-nointro", "test-realms-font", "test-realms"], "the record's layers")
	# an enhancement that does not extend the package is refused (WorkshopKit's incompatible-enhancement check)
	var zoom := _layout("test-realms", ["test-zoom"])
	_check(not zoom.ok and zoom.error == "Zoom For The Original does not extend Example Realms", "an enhancement of another package is refused: " + str(zoom.get("error")))
	var notenh := _layout("test-realms", ["test-tales"])
	_check(not notenh.ok and "is not an enhancement" in notenh.error, "a mod is not an enhancement: " + str(notenh.get("error")))
	# a patch-based mod: L3 under it, the engine edits of both layers listed, a dependency for another game left out
	var siege := _layout("test-siege", [])
	_eq(siege.status, ModCatalog.NEEDS, "Sample Siege needs engine features")
	_eq(siege.layers, ["test-siege", "test-patch-99", "test-bfme2-patch"], "Sample Siege's layers (guid:version is the guid)")
	_eq(siege.edits, ["Sample Siege: d3d9.dll", "Made-Up Patch 9.9: lotrbfme2ep1.exe", "Made-Up Patch 9.9: game.dat"], "Sample Siege's engine edits, by layer")
	_eq(siege.files, ["install/__siege.big <- Sample Siege", "install/data/ini/weapon.ini <- Sample Siege", "install/lang/englishpatch99.big <- Sample Siege",
		"install/__mup_patch.big <- Made-Up Patch 9.9"], "Sample Siege's layout (its englishpatch99.big wins over the patch's)")
	_eq(siege.left_out, ["Made-Up Patch 9.9: Patcher.exe (a tool)", "Made-Up Patch 9.9: readme.pdf",
		"Made-Up BFME2 Patch: INI.big (for another game: not mounted from a mod yet)"], "Sample Siege's files left out")
	var timer := _layout("test-siege", ["test-siege-timer"])
	_eq(timer.edits, ["Siege Timer: timer.asi", "Sample Siege: d3d9.dll", "Made-Up Patch 9.9: lotrbfme2ep1.exe", "Made-Up Patch 9.9: game.dat"], "an enhancement's engine edit comes first")
	# a -mod mod: its own folder; the same destination twice (other case, other slash) is taken once; a maps dependency is left out
	var tales := _layout("test-tales", [])
	_eq(tales.status, ModCatalog.READY, "Tiny Tales is ready")
	_eq(tales.files, ["mod/!!tales.big <- Tiny Tales", "mod/data/ini/object/tale.ini <- Tiny Tales"], "Tiny Tales' layout")
	_eq(tales.summary.parts, ["mod"], "Tiny Tales has only a -mod part")
	_eq(tales.left_out, ["Tale Maps: tale\\tale.ini (a map: maps from the service are not supported yet)"], "Tiny Tales' map left out")
	# an enhancement on its own (it extends the original game): it is the package
	var alone := _layout("test-zoom", [])
	_eq([alone.status, alone.files], [ModCatalog.READY, ["install/__zoom.big <- Zoom For The Original"]], "an enhancement of the original game alone")
	# what is not fetched yet is asked for, never guessed
	var part := _fetched.duplicate()
	part.erase("test-patch-99")
	var miss := ModCatalog.resolve("test-siege", [], part)
	_check(not miss.ok and Array(miss.missing) == ["test-patch-99"], "a dependency not fetched yet is reported missing: " + str(miss.get("missing")))
	_check(not ModCatalog.resolve("test-bfme2-patch", [], _fetched).ok, "a package for another game is not installed")
	_check(not ModCatalog.resolve("test-maps", [], _fetched).ok, "a maps package is not installed")
	# every destination of every layout stays below its folder
	for g in _fetched:
		var r := ModCatalog.resolve(g, [], _fetched)
		if r.ok:
			for f in r.plan.files:
				_check(ModCatalog.inside("/data/mods/" + g, "/data/mods/%s/%s/%s" % [g, f.part, f.rel]), "%s: %s stays inside" % [g, f.rel])


func _grouping() -> void:
	var items := []
	for t in ["0", "1", "2"]:
		for page in _fx.query[t]:
			items.append_array(ModCatalog.parse_query(JSON.stringify(page).to_utf8_buffer()).items)
	var problems := {"test-broken": "The service's package test-broken lists a file name that is not a plain relative path"}
	var v := ModCatalog.group(items, _fetched, problems)
	_eq(v.packages.map(func(r): return r.guid), ["test-patch-99", "test-realms", "test-siege", "test-tales", "test-broken"], "the packages, in the service's order")
	var by := {}
	for r in v.packages:
		by[r.guid] = r
	_eq(by["test-patch-99"].status, ModCatalog.NEEDS, "the patch needs engine features")
	_eq(Array(by["test-patch-99"].engine_edits), ["Made-Up Patch 9.9: lotrbfme2ep1.exe", "Made-Up Patch 9.9: game.dat"], "the patch's engine edits")
	_eq(by["test-realms"].status, ModCatalog.READY, "Example Realms is listed ready")
	_eq(by["test-realms"].enhancements.map(func(r): return r.guid), ["test-realms-nointro", "test-realms-font"], "Example Realms' enhancements (guid:version too)")
	_eq(by["test-siege"].enhancements.map(func(r): return r.guid), ["test-siege-timer"], "Sample Siege's enhancement")
	_eq(by["test-siege"].enhancements[0].status, ModCatalog.NEEDS, "the timer needs engine features")
	_eq(by["test-tales"].enhancements, [], "Tiny Tales has no enhancement")
	_check(by["test-broken"].error.begins_with("The service's package test-broken"), "the refused package keeps its reason on its row")
	_eq(v.loose.map(func(r): return r.guid), ["test-zoom"], "an enhancement of the original game is listed on its own")
	var nopatch := _fetched.duplicate()
	nopatch.erase("test-patch-99")
	var v2 := ModCatalog.group(items, nopatch, {"test-patch-99": "The service answered HTTP 403 for the file list of test-patch-99."})
	var siege2: Dictionary = v2.packages.filter(func(r): return r.guid == "test-siege")[0]
	_check(siege2.error.contains("HTTP 403"), "a package whose dependency was refused says why: " + siege2.error)


func _args() -> void:
	_eq(ModCatalog.game_args("/m/g/install", true, "/m/g/mod", true), PackedStringArray(["--mod-install=/m/g/install", "--mod=/m/g/mod"]), "both parts")
	_eq(ModCatalog.game_args("/m/g/install", true, "/m/g/mod", false), PackedStringArray(["--mod-install=/m/g/install"]), "an install-folder mod")
	_eq(ModCatalog.game_args("/m/g/install", false, "/m/g/mod", true), PackedStringArray(["--mod=/m/g/mod"]), "a -mod mod")
	_check(ModCatalog.inside("/a/mods/g", "/a/mods/g/install/x.big"), "inside: below")
	_check(not ModCatalog.inside("/a/mods/g", "/a/mods/g/../h/x.big"), "inside: .. out of it")
	_check(not ModCatalog.inside("/a/mods/g", "/a/mods/gh/x.big"), "inside: a sibling with the same prefix")
	_check(not ModCatalog.inside("", "/x"), "inside: no base")
	# the active mod on a made-up mods folder (user data of this test run)
	var root := OS.get_user_data_dir().path_join("mod-catalog-test/mods")
	ModInstall._remove_tree(root)
	DirAccess.make_dir_recursive_absolute(root.path_join("test-realms/install"))
	var none := ModInstall.active_args(root)
	_check(none.ok and none.args.is_empty(), "no active.cfg: the original game, no arguments")
	var rec := {"schema": ModCatalog.SCHEMA, "guid": "test-realms", "name": "Example Realms", "version": "1.2.0", "type": 0, "parts": ["install"],
		"enhancements": ["test-realms-font"]}
	var f := FileAccess.open(root.path_join("test-realms").path_join(ModInstall.RECORD), FileAccess.WRITE)
	f.store_string(JSON.stringify(rec))
	f.close()
	var m := ModInstall.new(_FakeUpdater.new(root.get_base_dir()), null)
	_eq(m.set_active("test-other"), "test-other is not installed", "an uninstalled mod cannot be picked")
	_eq(m.set_active("test-realms", ["test-realms-font"]), "", "an installed mod is picked")
	var a := ModInstall.active_args(root)
	_eq(a.args, PackedStringArray(["--mod-install=" + root.path_join("test-realms/install")]), "the active mod's arguments (absolute)")
	_check(a.args[0].trim_prefix("--mod-install=").is_absolute_path(), "the install folder is an absolute path")
	_eq(m.active(), {"guid": "test-realms", "enhancements": ["test-realms-font"]}, "active.cfg keeps the pick")
	m.set_active("test-realms", [])
	_check(not ModInstall.active_args(root).ok and "not the installed ones" in ModInstall.active_args(root).error, "picked enhancements that are not installed stop the start")
	m.set_active("test-realms", ["test-realms-font"])
	DirAccess.remove_absolute(root.path_join("test-realms/install"))
	_check(not ModInstall.active_args(root).ok and "missing its files" in ModInstall.active_args(root).error, "a missing part stops the start")
	rec.parts = ["install", "mod"]
	DirAccess.make_dir_recursive_absolute(root.path_join("test-realms/install"))
	DirAccess.make_dir_recursive_absolute(root.path_join("test-realms/mod"))
	f = FileAccess.open(root.path_join("test-realms").path_join(ModInstall.RECORD), FileAccess.WRITE)
	f.store_string(JSON.stringify(rec))
	f.close()
	_eq(ModInstall.active_args(root).args, PackedStringArray(["--mod-install=" + root.path_join("test-realms/install"), "--mod=" + root.path_join("test-realms/mod")]), "both parts' arguments")
	_eq(m.uninstall("../escape"), "'../escape' is not a package id", "uninstall refuses a path")
	_eq(m.uninstall("test-realms"), "", "uninstall removes the mod")
	_check(not DirAccess.dir_exists_absolute(root.path_join("test-realms")), "its folder is gone")
	_eq(m.active().guid, "", "the original game is picked again")
	_check(ModInstall.active_args(root).ok and ModInstall.active_args(root).args.is_empty(), "no arguments after the uninstall")
	ModInstall._remove_tree(root.get_base_dir())


## the one member of Updater that ModInstall's disk side reads (its mods folder is <data_dir>/mods)
class _FakeUpdater:
	var data_dir := ""
	func _init(parent: String) -> void:
		data_dir = parent
