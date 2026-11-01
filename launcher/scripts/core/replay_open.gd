## Shared bounded routing metadata. The game validates the complete file and mounted profile before loading.
extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")

const MAX_BYTES := 256 * 1024 * 1024
const MAX_SAVE_BYTES := 2 * MAX_BYTES + 1024 * 1024 + 132


static func refused(reason: String) -> Dictionary:
	return {"ok": false, "error": L10n.t("launcher.replay_open.refused", {"reason": reason})}


static func is_recording(path: String) -> bool:
	return path.to_lower().ends_with(".replay") or path.to_lower().ends_with(".obfmesave")


static func arguments(args: PackedStringArray, test_prefixes: Array = []) -> Dictionary:
	var rest: Array[String] = []
	for a in args:
		if a == "--offline" or test_prefixes.any(func(p): return a.begins_with(p)):
			continue
		rest.append(a)
	var mode := rest.any(func(a): return a in ["--replay", "--load"] or a.begins_with("--replay=") or a.begins_with("--load=") or is_recording(a))
	if not mode:
		return {"mode": false, "ok": true, "path": ""}
	var path := ""
	if rest.size() == 2 and rest[0] in ["--replay", "--load"]:
		path = rest[1]
	elif rest.size() == 1:
		path = rest[0].substr(9) if rest[0].begins_with("--replay=") else rest[0].substr(7) if rest[0].begins_with("--load=") else rest[0] if is_recording(rest[0]) else ""
	if path.is_empty() or path.begins_with("--") or path.contains("\n") or path.contains("\r"):
		return {"mode": true, "ok": false, "error": L10n.t("launcher.replay_open.bad_arguments")}
	return {"mode": true, "ok": true, "path": path}


static func _string(file: FileAccess, maximum: int, end: int = -1) -> Dictionary:
	if end < 0:
		end = file.get_length()
	if end - file.get_position() < 4:
		return refused(L10n.t("launcher.replay_open.truncated_field"))
	var n := file.get_32()
	if n > maximum or n > end - file.get_position():
		return refused(L10n.t("launcher.replay_open.overlong_field"))
	var bytes := file.get_buffer(n)
	if bytes.size() != n or bytes.has(0):
		return refused(L10n.t("launcher.replay_open.invalid_text"))
	var value := bytes.get_string_from_utf8()
	if value.to_utf8_buffer() != bytes:
		return refused(L10n.t("launcher.replay_open.not_utf8"))
	return {"ok": true, "value": value}


static func read_header(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return refused(L10n.t("launcher.replay_open.cannot_open", {"path": path, "reason": error_string(FileAccess.get_open_error())}))
	if file.get_length() < 16:
		return refused(L10n.t("launcher.replay_open.truncated_file"))
	var magic := file.get_buffer(8).get_string_from_ascii()
	var limit := MAX_SAVE_BYTES if magic == "OBFMESAV" else MAX_BYTES
	if file.get_length() > limit:
		return refused(L10n.t("launcher.replay_open.too_big", {"limit": limit}))
	if magic == "OBFMESAV":
		return _save_header(file)
	if magic != "OBFMEREP":
		return refused(L10n.t("launcher.replay_open.not_recording"))
	var format := file.get_32()
	if not format in [5, 6, 7, 8, 9, 10]:
		return refused(L10n.t("launcher.replay_open.replay_version", {"version": format}))
	var profile := _string(file, 1024 * 1024)
	if not profile.ok:
		return profile
	if format < 9:
		return refused(L10n.t("launcher.replay_open.legacy_format", {"version": format}))
	var version := _string(file, 128)
	if not version.ok:
		return version
	var commit := _string(file, 40)
	if not commit.ok:
		return commit
	var date := {"ok": true, "value": ""}
	if format >= 10:
		date = _string(file, 10)
		if not date.ok:
			return date
	return _build(version.value, commit.value, date.value, "replay")


static func _build(version: String, commit: String, date: String, kind: String) -> Dictionary:
	if version.is_empty() or version.contains("\n") or version.contains("\r") or version.contains("\t") or not _valid_date(date):
		return _unknown_build()
	if commit != "none" and RegEx.create_from_string("^[0-9a-f]{40}$").search(commit) == null:
		return refused(L10n.t("launcher.replay_open.invalid_build", {"kind": kind}))
	return {"ok": true, "error": "", "version": version, "commit": commit, "date": date, "kind": kind, "argument": "--load" if kind == "save" else "--replay"}


static func _valid_date(date: String) -> bool:
	return date == "" or (date.length() == 10 and RegEx.create_from_string("^[0-9]{4}-[0-9]{2}-[0-9]{2}$").search(date) != null and date.substr(5, 2) >= "01" and date.substr(5, 2) <= "12" and date.substr(8, 2) >= "01" and date.substr(8, 2) <= "31")


static func _unknown_build() -> Dictionary:
	return refused(L10n.t("launcher.replay_open.unknown_build"))


static func _save_header(file: FileAccess) -> Dictionary:
	if file.get_32() != 1 or file.get_buffer(4).get_string_from_ascii() != "HEAD":
		return refused(L10n.t("launcher.replay_open.save_container"))
	var size := file.get_32()
	if size > 1024 * 1024 or size < 4 or file.get_length() - file.get_position() < 32 + size:
		return refused(L10n.t("launcher.replay_open.save_head"))
	var digest := file.get_buffer(32)
	var start := file.get_position()
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(file.get_buffer(size))
	if hash.finish() != digest:
		return refused(L10n.t("launcher.replay_open.save_damaged"))
	file.seek(start)
	var end := start + size
	var format := file.get_32()
	if not format in [1, 2, 3]:
		return refused(L10n.t("launcher.replay_open.save_version", {"version": format}))
	var fields := []
	for maximum in [128, 1024 * 1024, 40, 1024 * 1024, 1024 * 1024]:
		var field := _string(file, maximum, end)
		if not field.ok:
			return field
		fields.append(field.value)
	if end - file.get_position() < 12:
		return refused(L10n.t("launcher.replay_open.truncated_save"))
	file.seek(file.get_position() + 12) # game kind, frame, state hash
	for i in 5: # description, map path, map label, save date, save time (not the build date)
		var field := _string(file, 1024 * 1024, end)
		if not field.ok:
			return field
	if end - file.get_position() < 4:
		return refused(L10n.t("launcher.replay_open.truncated_save"))
	file.seek(file.get_position() + 4) # commands
	var campaign := _string(file, 1024 * 1024, end)
	if not campaign.ok:
		return campaign
	var tail := 8 + (8 if format >= 2 else 0) # mission, difficulty; save point and flags
	if end - file.get_position() < tail:
		return refused(L10n.t("launcher.replay_open.truncated_save"))
	file.seek(file.get_position() + tail)
	var date := {"ok": true, "value": ""}
	if format >= 3:
		date = _string(file, 10, end)
		if not date.ok:
			return date
	if file.get_position() != end:
		return refused(L10n.t("launcher.replay_open.save_extra"))
	return _build(fields[0], fields[2], date.value, "save")


static func select_build(header: Dictionary, installed: Array) -> Dictionary:
	if not header.ok:
		return header
	for build in installed:
		if header.version == build.version and header.commit != "none" and header.commit == build.get("commit", ""):
			return {"ok": true, "error": "", "version": build.get("installed_version", build.version), "argument": header.get("argument", "--replay")}
	if not preload("res://scripts/core/semver.gd").is_valid(header.version) or not _valid_date(header.get("date", "")):
		return _unknown_build()
	var date: String = header.get("date", "")
	var label: String = preload("res://scripts/core/semver.gd").display(header.version)
	var suffix := " " + L10n.t("launcher.replay_open.build_suffix", {"date": L10n.t("launcher.replay_open.build_date_unavailable") if date == "" else date})
	if not label.ends_with(suffix):
		label += suffix
	return refused(L10n.t("launcher.replay_open.not_installed", {"label": label, "kind": header.get("kind", "replay")}))
