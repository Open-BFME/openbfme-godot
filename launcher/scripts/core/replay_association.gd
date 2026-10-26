extends RefCounted

const DesktopEntry := preload("res://scripts/core/desktop_entry.gd")
const MIME := "application/x-openbfme-replay"
const DESKTOP := "openbfme-replay.desktop"
const PROGID := "OpenBFME.Replay"
const ROOT := "HKCU\\Software\\Classes\\"
const XML := "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\"><mime-type type=\"application/x-openbfme-replay\"><comment>OpenBFME replay</comment><glob pattern=\"*.replay\"/><magic priority=\"80\"><match type=\"string\" value=\"OBFMEREP\" offset=\"0\"/></magic></mime-type></mime-info>\n"


static func _type(kind: String) -> Dictionary:
	return {"mime": "application/x-openbfme-" + kind, "desktop": "openbfme-" + kind + ".desktop",
		"progid": "OpenBFME.Save" if kind == "save" else PROGID, "extension": ".obfmesave" if kind == "save" else ".replay",
		"argument": "--load" if kind == "save" else "--replay"}


static func mime_xml(kind := "replay") -> String:
	return XML if kind == "replay" else XML.replace("replay", "save").replace("*.save", "*.obfmesave").replace("OBFMEREP", "OBFMESAV")


static func windows_commands(exe: String, kind := "replay") -> Array:
	var t := _type(kind)
	return [
		PackedStringArray(["add", ROOT + t.progid, "/ve", "/d", "OpenBFME " + kind, "/f"]),
		PackedStringArray(["add", ROOT + t.progid + "\\shell\\open\\command", "/ve", "/d", "\"%s\" -- %s \"%%1\"" % [exe, t.argument], "/f"]),
		PackedStringArray(["add", ROOT + t.extension + "\\OpenWithProgids", "/v", t.progid, "/t", "REG_NONE", "/f"]),
	]


static func linux_entry(exe: String, kind := "replay") -> String:
	var t := _type(kind)
	# Desktop Entry Exec has its own quoting, not shell syntax. Literal percent signs are doubled before appending the single file code.
	var base := DesktopEntry.text(exe.replace("%", "%%"))
	var lines := base.split("\n")
	for i in lines.size():
		if lines[i].begins_with("Exec="):
			lines[i] += " -- %s %%f" % t.argument
	return "\n".join(lines) + "NoDisplay=true\nMimeType=%s;\n" % t.mime


static func _run(command: String, args: PackedStringArray) -> Dictionary:
	var output := []
	var code := OS.execute(command, args, output, true)
	return {"ok": code == 0, "error": "" if code == 0 else "%s failed (%d): %s" % [command, code, "".join(output).strip_edges()], "text": "".join(output)}


static func _write(path: String, content: String) -> String:
	var e := DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	if e != OK:
		return "cannot create %s: %s" % [path.get_base_dir(), error_string(e)]
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return "cannot write %s: %s" % [path, error_string(FileAccess.get_open_error())]
	file.store_string(content)
	return ""


static func without_handler(text: String, kind := "replay") -> String:
	var t := _type(kind)
	var lines := text.split("\n")
	for i in lines.size():
		if lines[i].begins_with(t.mime + "="):
			var values := lines[i].substr(t.mime.length() + 1).split(";", false)
			var kept := PackedStringArray()
			for value in values:
				if value != t.desktop:
					kept.append(value)
			lines[i] = t.mime + "=" + ";".join(kept) + (";" if not kept.is_empty() else "")
	return "\n".join(lines)


static func set_enabled(exe: String, enabled: bool, previous := "", kind := "replay") -> Dictionary:
	var t := _type(kind)
	if OS.get_name() == "Windows":
		if enabled:
			for cmd in windows_commands(exe, kind):
				var r := _run("reg", cmd)
				if not r.ok:
					return r
			var current := _run("reg", ["query", ROOT + t.extension, "/ve"])
			# Offer Open With when another app owns the extension; never edit UserChoice or demand elevation.
			if not current.ok or current.text.strip_edges().ends_with("REG_SZ"):
				return _run("reg", ["add", ROOT + t.extension, "/ve", "/d", t.progid, "/f"])
		else:
			var owner := _run("reg", ["query", ROOT + t.progid + "\\shell\\open\\command", "/ve"])
			if not owner.text.contains("\"%s\" -- %s" % [exe, t.argument]):
				return {"ok": true, "error": ""}
			var current := _run("reg", ["query", ROOT + t.extension, "/ve"])
			if current.text.strip_edges().ends_with(t.progid):
				_run("reg", ["delete", ROOT + t.extension, "/ve", "/f"])
			_run("reg", ["delete", ROOT + t.extension + "\\OpenWithProgids", "/v", t.progid, "/f"])
			return _run("reg", ["delete", ROOT + t.progid, "/f"])
		return {"ok": true, "error": ""}
	if OS.get_name() == "Linux":
		var home := DesktopEntry.data_home()
		var apps := home.path_join("applications")
		var mime := home.path_join("mime")
		var definition := mime.path_join("packages/openbfme-" + kind + ".xml")
		var entry := apps.path_join(t.desktop)
		if enabled:
			for path in {definition: mime_xml(kind), entry: linux_entry(exe, kind)}:
				var why := _write(path, mime_xml(kind) if path == definition else linux_entry(exe, kind))
				if why != "":
					return {"ok": false, "error": why}
			var current := _run("xdg-mime", ["query", "default", t.mime])
			if not current.ok:
				return current
			previous = current.text.strip_edges() if current.text.strip_edges() != t.desktop else previous
			for job in [["update-mime-database", mime], ["update-desktop-database", apps]]:
				if not DirAccess.dir_exists_absolute(job[1]):
					continue
				var r := _run(job[0], [job[1]])
				if not r.ok:
					return r
			var result := _run("xdg-mime", ["default", t.desktop, t.mime])
			result.previous = previous
			return result
		else:
			var current := _run("xdg-mime", ["query", "default", t.mime])
			if current.text.strip_edges() == t.desktop and previous != "":
				var restore := _run("xdg-mime", ["default", previous, t.mime])
				if not restore.ok:
					return restore
			var config := OS.get_environment("XDG_CONFIG_HOME")
			if config == "":
				config = OS.get_environment("HOME").path_join(".config")
			for folder in [config, apps]:
				if not DirAccess.dir_exists_absolute(folder):
					continue
				for name in DirAccess.get_files_at(folder):
					if name == "mimeapps.list" or name.ends_with("-mimeapps.list"):
						var path: String = folder.path_join(name)
						var text := FileAccess.get_file_as_string(path)
						var cleaned := without_handler(text, kind)
						if text != cleaned:
							var why := _write(path, cleaned)
							if why != "":
								return {"ok": false, "error": why}
			for path in [entry, definition]:
				if FileAccess.file_exists(path):
					var e := DirAccess.remove_absolute(path)
					if e != OK:
						return {"ok": false, "error": "cannot remove %s: %s" % [path, error_string(e)]}
			for job in [["update-mime-database", mime], ["update-desktop-database", apps]]:
				if not DirAccess.dir_exists_absolute(job[1]):
					continue
				var r := _run(job[0], [job[1]])
				if not r.ok:
					return r
		return {"ok": true, "error": ""}
	if OS.get_name() == "macOS":
		# Launch Services reads the signed bundle's Info.plist; never rewrite it and invalidate the app's signature.
		return {"ok": true, "error": ""}
	return {"ok": false, "error": "Replay associations are not supported on " + OS.get_name()}
