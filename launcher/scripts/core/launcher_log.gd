## Lane LAUNCH-1: the launcher's log, user://launcher.log (appended by every process: the launcher, its update helper and the new
## launcher of a self-update write one story; at 1 MiB the initial context and recent tail are retained) and the console. Every line is flushed at
## once (a killed process loses nothing). Diagnostics are scrubbed before either sink. Thread-safe, local only.
extends RefCounted

static var _mutex := Mutex.new()
static var _file: FileAccess
static var _home := ""
static var _user := ""
static var _patterns: Array[RegEx] = []
static var _filter_failed := false
const MAX_FIELD_BYTES := 16 << 10
const MAX_LOG_BYTES := 1 << 20
const HEAD_BYTES := 64 << 10
const OMITTED := "[private diagnostic omitted]"
const OVERSIZED := "[oversized diagnostic omitted]"


static func open() -> void:
	_mutex.lock()
	if _file != null:
		_file.close()
		_file = null
	var dir := OS.get_user_data_dir()
	DirAccess.make_dir_recursive_absolute(dir)
	var path := dir.path_join("launcher.log")
	_file = FileAccess.open(path, FileAccess.READ_WRITE if FileAccess.file_exists(path) else FileAccess.WRITE_READ)
	if _file != null:
		_file.seek_end()
	_home = OS.get_environment("USERPROFILE" if OS.get_name() == "Windows" else "HOME")
	_user = OS.get_environment("USERNAME" if OS.get_name() == "Windows" else "USER")
	if _user.is_empty():
		_user = OS.get_environment("LOGNAME")
	_mutex.unlock()


static func _init_patterns() -> bool:
	if not _patterns.is_empty() or _filter_failed:
		return not _filter_failed
	for pattern in [
		'(?i)\\b(authorization|bearer|cookie|password|passwd|secret|token|(access|refresh|id|auth)[_ -]?token|session[_ -]?(key|token)|api[_ -]?key|recovery[_ -]?code|client[_ -]?secret|player[_ -]?name|display[_ -]?name|nick(name)?)\\b',
		'(?i)([a-z]:[/\\\\]+users[/\\\\]+|/(home|users)/)[^/\\\\\\r\\n"\'<>]+',
		'\\b[0-9]{1,3}(\\.[0-9]{1,3}){3}\\b',
		'(?i)([a-f0-9]{0,4}:){2,8}[a-f0-9:.]*(%[a-z0-9_.-]+)?',
		'[a-zA-Z0-9_.+%=-]+@[a-zA-Z0-9_.-]+',
	]:
		var regex := RegEx.new()
		if regex.compile(pattern, false) != OK:
			_filter_failed = true
			return false
		_patterns.append(regex)
	return true


static func _scrub_values(text: String) -> String:
	var at_url := 0
	while true:
		var colon := text.find("://", at_url)
		if colon < 0:
			break
		var start := colon
		while start > 0 and text[start - 1].to_lower() in "abcdefghijklmnopqrstuvwxyz":
			start -= 1
		var begin := colon + 3
		at_url = begin
		if not text.substr(start, colon - start).to_lower() in ["https", "http", "wss", "ws"]:
			continue
		var end := begin
		while end < text.length() and not text[end] in " \t\r\n/\\?#\"'<>":
			end += 1
		var user_info := text.rfind("@", end - 1)
		if user_info >= begin and user_info < end:
			text = text.substr(0, begin) + OMITTED + text.substr(user_info)
	for label in ["GAME ONLINE signed in as ", "GAME ONLINE remembered name ", "GAME ONLINE chat ", "GAME ONLINE WALK"]:
		var found := text.find(label)
		if found >= 0 and not text.substr(found).begins_with("GAME ONLINE WALK RESULT:"):
			return text.substr(0, found + label.length()) + OMITTED
	var values: Array[Vector2i] = []
	for hit: RegExMatch in _patterns[0].search_all(text):
		var at := hit.get_end()
		var bearer := hit.get_string().to_lower() == "bearer"
		if at < text.length() and text[at] in "\"'":
			at += 1
		while at < text.length() and text[at] in " \t\r\n":
			at += 1
		if not bearer:
			if at == text.length() or not text[at] in ":=":
				continue
			if text[at] == ":" and hit.get_string().to_lower() == "token":
				var before := hit.get_start()
				while before > 0 and text[before - 1] in " \t\r\n":
					before -= 1
				var word := before
				while word > 0 and text[word - 1].to_lower() in "abcdefghijklmnopqrstuvwxyz":
					word -= 1
				if text.substr(word, before - word).to_lower() in ["unexpected", "expected"]:
					continue
			at += 1
			while at < text.length() and text[at] in " \t\r\n":
				at += 1
		var end := at
		if at < text.length() and text[at] in "\"'":
			var quote := text[at]
			at += 1
			end = at
			while end < text.length() and text[end] != quote:
				if text[end] == "\\" and end + 1 < text.length():
					end += 1
				end += 1
		else:
			while end < text.length() and not text[end] in ",;&\r\n}]" and (not bearer or not text[end] in " \t\r\n"):
				end += 1
		if text.substr(at).begins_with(OMITTED):
			continue
		if end > at and (values.is_empty() or at >= values[-1].y):
			values.append(Vector2i(at, end))
	values.reverse()
	for value in values:
		text = text.substr(0, value.x) + OMITTED + text.substr(value.y)
	var at := 0
	while at < text.length():
		var begin := at
		while at < text.length() and _name_char(text[at]):
			at += 1
		var value := text.substr(begin, at - begin)
		var parts := value.split(".")
		if parts.size() == 3 and parts[0].begins_with("eyJ") and parts[0].length() >= 8 and parts[1].begins_with("eyJ") and parts[1].length() >= 8 and parts[2].length() >= 8:
			text = text.substr(0, begin) + OMITTED + text.substr(at)
			at = begin + OMITTED.length()
		if at == begin:
			at += 1
	return text


static func _version_at(text: String, at: int) -> bool:
	while at > 0 and text[at - 1] in " \t\r\n:=\"'":
		at -= 1
	var end := at
	while at > 0 and _name_char(text[at - 1]):
		at -= 1
	return text.substr(at, end - at).to_lower() in ["version", "engine", "godot", "openbfme", "engine_version", "build_version"]


static func _scrub_line(text: String) -> String:
	if text.to_utf8_buffer().size() > MAX_FIELD_BYTES:
		return OVERSIZED
	if not _init_patterns():
		return OMITTED
	text = _scrub_values(text)
	if _home.length() > 1 and OS.get_name() == "Windows":
		text = text.replacen(_home, "<user>").replacen(_home.replace("\\", "/"), "<user>")
	elif _home.length() > 1:
		text = text.replace(_home, "<user>").replace(_home.replace("\\", "/"), "<user>")
	for i in range(1, _patterns.size()):
		if i == 2 or i == 3:
			var matches := _patterns[i].search_all(text)
			matches.reverse()
			for hit: RegExMatch in matches:
				var address := hit.get_string()
				var before := text.substr(hit.get_start() - 1, 1) if hit.get_start() > 0 else ""
				var after := text.substr(hit.get_end(), 1)
				var valid := address.contains("::") or address.count(":") == 7
				if i == 2:
					valid = not _version_at(text, hit.get_start())
					for octet in address.split("."):
						valid = valid and int(octet) <= 255
				if valid and (i == 2 or (not _name_char(before) and not _name_char(after))):
					text = text.substr(0, hit.get_start()) + "<address>" + text.substr(hit.get_end())
		else:
			text = _patterns[i].sub(text, "<user>" if i == 1 else "<address>", true)
	if not _user.is_empty():
		var regex := RegEx.new()
		var escaped := ""
		for ch in _user:
			escaped += "\\" + ch if ch in "\\.^$|?*+()[]{}" else ch
		if regex.compile('(?i)(?<![a-z0-9_])' + escaped + '(?![a-z0-9_])', false) != OK:
			return OMITTED
		text = regex.sub(text, "<user>", true)
	return OVERSIZED if text.to_utf8_buffer().size() > MAX_FIELD_BYTES else text


static func _name_char(ch: String) -> bool:
	return not ch.is_empty() and (ch.to_lower() in "abcdefghijklmnopqrstuvwxyz0123456789_.-" or ch.unicode_at(0) >= 128)


static func scrub(text: String) -> String:
	if text.to_utf8_buffer().size() > MAX_LOG_BYTES:
		return OVERSIZED
	var lines := PackedStringArray()
	for part in text.split("\n"):
		lines.append(_scrub_line(part))
	return "\n".join(lines)


static func line(text: String) -> void:
	_mutex.lock()
	var s := scrub(text)
	print(s)
	if _file != null:
		var record := "%s [%d] %s\n" % [Time.get_datetime_string_from_system(true), OS.get_process_id(), s]
		var data := record.to_utf8_buffer()
		if data.size() > MAX_LOG_BYTES - HEAD_BYTES:
			data = (OVERSIZED + "\n").to_utf8_buffer()
		if (_file.get_length() + data.size() <= MAX_LOG_BYTES or _compact(data.size())):
			_file.store_buffer(data)
			_file.flush()
			if _file.get_error() != OK:
				_file.close()
				_file = null
		else:
			_file.close()
			_file = null
	_mutex.unlock()


static func _compact(incoming: int) -> bool:
	var length := _file.get_length()
	_file.seek(0)
	var head := _file.get_buffer(mini(length, HEAD_BYTES))
	head = head.slice(0, head.rfind(10) + 1)
	var start := maxi(head.size(), length - maxi((MAX_LOG_BYTES - HEAD_BYTES) >> 1, incoming))
	_file.seek(start)
	var tail := _file.get_buffer(length - start)
	if start > head.size():
		var cut := tail.find(10)
		tail = tail.slice(cut + 1) if cut >= 0 else PackedByteArray()
	# Reserve room for the new record as well as preserving complete old lines.
	while head.size() + tail.size() + incoming > MAX_LOG_BYTES:
		var cut := tail.find(10)
		tail = tail.slice(cut + 1) if cut >= 0 else PackedByteArray()
	_file.seek(0)
	_file.store_buffer(head)
	_file.store_buffer(tail)
	_file.flush()
	if _file.get_error() != OK or _file.resize(head.size() + tail.size()) != OK:
		return false
	_file.seek_end()
	return true
