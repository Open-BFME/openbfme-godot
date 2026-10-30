## Lane SDK-1: the asset packs the game draws with (HD models and textures; engine GameClient/AssetPacks.h). A pack is a folder in the game's
## user data folder, <game user dir>/asset-packs/<pack>/; the game loads every one in name order except those named in asset-packs/disabled.txt
## (godot/scripts/release/release.gd asset_pack_folders). This lists them for the Mods screen, with the same rules the game applies at the
## start (so a pack the game would refuse is shown with the reason), and turns them on and off. The game stays the judge: it also checks
## each model against the one it replaces, which needs the mounted game files.
extends RefCounted

const GameOptions := preload("res://scripts/core/game_options.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const TEXTURES := ["dds", "tga", "png", "jpg"]
const TEXT_NAMES := ["openbfme-pack.json", "license", "licence", "readme"]
const DISABLED := "disabled.txt"


static func root() -> String:
	return GameOptions.game_user_dir().path_join("asset-packs")


## every pack folder in name order: {name, enabled, models, textures, bytes, problems: PackedStringArray}
static func list(dir := "") -> Array:
	var r := dir if dir != "" else root()
	var out := []
	if not DirAccess.dir_exists_absolute(r):
		return out
	var names := DirAccess.get_directories_at(r)
	names.sort()
	var off := disabled(r)
	var checked := report(r)
	for n in names:
		var p := {"name": n, "enabled": not off.has(n.to_lower()), "models": 0, "textures": 0, "bytes": 0, "problems": PackedStringArray(),
			"checked": checked.has(n), "misfits": []}
		if checked.has(n):
			p.misfits = checked[n].get("misfits", [])
		var seen := {}
		_scan(r.path_join(n), "", p, seen)
		out.append(p)
	return out


## lane SDK-1b (b): the game's check of the packs at its last start (<dir>/report.json, scripts/release/release.gd): pack name -> {misfits: [{file,
## why}]}; empty when the game has not checked them yet (or the file is not the game's report)
static func report(dir := "") -> Dictionary:
	var path := (dir if dir != "" else root()).path_join("report.json")
	var out := {}
	if not FileAccess.file_exists(path):
		return out
	var data = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (data is Dictionary) or data.get("format", "") != "openbfme.asset-pack-report 1" or not (data.get("packs") is Array):
		return out
	for p in data.packs:
		if p is Dictionary and p.get("name") is String:
			out[p.name] = p
	return out


static func _scan(folder: String, rel: String, p: Dictionary, seen: Dictionary) -> void:
	var d := DirAccess.open(folder)
	if d == null:
		p.problems.append(L10n.t("launcher.asset_packs.unreadable", {"path": rel}) if rel != "" else L10n.t("launcher.asset_packs.unreadable_folder"))
		return
	d.include_hidden = true
	var files := d.get_files()
	files.sort()
	for f in files:
		var r := rel.path_join(f) if rel != "" else f
		if d.is_link(f):
			p.problems.append(L10n.t("launcher.asset_packs.link", {"path": r}))
			continue
		var ext := f.get_extension().to_lower()
		var key := ""
		if ext in TEXTURES:
			key = "t:" + f.get_basename().to_lower()
			p.textures += 1
		elif ext == "w3d":
			key = "m:" + f.to_lower()
			p.models += 1
		elif ext == "txt" or ext == "md" or f.to_lower() in TEXT_NAMES:
			continue
		else:
			p.problems.append(L10n.t("launcher.asset_packs.not_asset", {"path": r}))
			continue
		if seen.has(key):
			p.problems.append(L10n.t("launcher.asset_packs.twice", {"first": seen[key], "second": r}))
		seen[key] = r
		var fa := FileAccess.open(folder.path_join(f), FileAccess.READ)
		if fa != null:
			p.bytes += fa.get_length()
	var subs := d.get_directories()
	subs.sort()
	for s in subs:
		var r := rel.path_join(s) if rel != "" else s
		if d.is_link(s):
			p.problems.append(L10n.t("launcher.asset_packs.link", {"path": r}))
			continue
		_scan(folder.path_join(s), r, p, seen)


## the pack names (lower case) in <dir>/disabled.txt; the game reads the same file (release.gd disabled_asset_packs)
static func disabled(dir := "") -> Dictionary:
	var off := {}
	var path := (dir if dir != "" else root()).path_join(DISABLED)
	if not FileAccess.file_exists(path):
		return off
	for line in FileAccess.get_file_as_string(path).split("\n"):
		var n := line.strip_edges()
		if not n.is_empty() and not n.begins_with("#"):
			off[n.to_lower()] = true
	return off


## turns the pack `name` on or off; "" when done, else why not
static func set_enabled(name: String, on: bool, dir := "") -> String:
	var r := dir if dir != "" else root()
	var found := false
	for p in list(r):
		if p.name.to_lower() == name.to_lower():
			found = true
	if not found:
		return L10n.t("launcher.asset_packs.missing", {"name": name, "folder": r})
	var off := disabled(r)
	if on:
		off.erase(name.to_lower())
	else:
		off[name.to_lower()] = true
	var keys := off.keys()
	keys.sort()
	var f := FileAccess.open(r.path_join(DISABLED), FileAccess.WRITE)
	if f == null:
		return L10n.t("launcher.asset_packs.unwritable", {"path": r.path_join(DISABLED)})
	f.store_string("# asset packs the game leaves out (one folder name per line; the launcher's Mods page writes this file)\n")
	for k in keys:
		f.store_string(String(k) + "\n")
	return ""


## the folder, made if missing (for Open the folder); "" when it cannot be made
static func ensure_root() -> String:
	var r := root()
	if DirAccess.make_dir_recursive_absolute(r) != OK:
		return ""
	return r
