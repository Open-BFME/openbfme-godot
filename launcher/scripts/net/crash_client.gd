## Lane CRASHACT-1: crash reports to the OpenBFME crash service (POST /v1/reports on the platform API; the receiver and its wire contract are the
## crash service's README and report.schema.json, mirrored in services/crash/). One file, byte for byte the same in the game
## (godot/scripts/crash/crash_client.gd) and the launcher (launcher/scripts/net/crash_client.gd): tools/release/test_crash_upload.py checks they match.
##
##  * build(): the report. The schema's fields only (schema_version, build_kind, version, client, os/cpu/gpu names, backtrace, log_tail, context,
##    settings); every string passes the caller's privacy filter (the game's ReleaseInfo.redact = Common/LogPrivacy.h, the launcher's Log.scrub),
##    then scrub() (e-mails, URLs, paths, host names, IP addresses, keys and secrets, chat), then a last check with the service's own rules: a line
##    the service would refuse is replaced whole, so one stray value never loses the report. JSON with sorted keys, gzip (no name, mtime 0).
##    The report builder and scrub() are lane crashclient2's (Devin, 94418bbe4), reused.
##  * the queue: one <unix time>-<random>.json.gz per report in a folder of the caller's user data; at most QUEUE_MAX_FILES files and
##    QUEUE_MAX_BYTES bytes (the oldest go first), none older than QUEUE_MAX_DAYS. Every drop is logged with its reason.
##  * upload_queue(): sends the queued reports oldest first, at most UPLOADS_PER_RUN a run (the service admits two a source, then one every
##    ten minutes), one request at a time, no redirect, REQUEST_SECONDS each. A 201 with a report id removes the file; a 400 / 413 (the
##    service will never take it) drops it by name; anything else (offline, 401, 429, 5xx) keeps it for the next start and ends the run.
##    Nothing here waits on the caller's thread: the caller's start and quit never block on it (a quit cancels the request; the file stays).
##  * signing: HMAC-SHA256 of the service's canonical text with the service's client key (deliberately extractable, not an access credential:
##    the service README). The key is never in the source: tools/release/crash_key.py writes res://scripts/crash_client_key.gd (git-ignored) into the
##    package's staged project from CRASH_CLIENT_KEY; a build without that file sends nothing (crash.no_client_key, the reports stay queued).
##  * OPENBFME_CRASH_UPLOAD=0 (every CI host's env.sh, the nightly smoke steps) sends nothing at all, whatever else is set.
##    Tests use a loopback endpoint and key from OPENBFME_CRASH_TEST_ENDPOINT / OPENBFME_CRASH_TEST_KEY
##    (http://127.0.0.1:<port>/v1/reports only); nothing else can change where a report goes.
## Minidumps are never read or sent: the service takes no attachments yet.
extends Node

signal finished(summary: Dictionary)

const ENDPOINT := "https://api.openbfme.com/v1/reports"
## The package's generated key script (tools/release/crash_key.py): `const KEY := "<the service's 64-character client key>"`
const KEY_SCRIPT := "res://scripts/crash_client_key.gd"
const UPLOAD_ENV := "OPENBFME_CRASH_UPLOAD"
const TEST_ENDPOINT_ENV := "OPENBFME_CRASH_TEST_ENDPOINT"
const TEST_KEY_ENV := "OPENBFME_CRASH_TEST_KEY"

const QUEUE_MAX_FILES := 8
const QUEUE_MAX_BYTES := 1 << 20
const QUEUE_MAX_DAYS := 30
const UPLOADS_PER_RUN := 2
const REQUEST_SECONDS := 15.0
const MAX_RESPONSE_BYTES := 4096
## the request's limit in running frames (a blocking start-up load does not use it up); relative, so the launcher's copy of this file finds
## the launcher's copy (launcher/scripts/http_timed.gd)
const HttpTimed := preload("../http_timed.gd")

const TAIL_BYTES := 256 * 1024
const MAX_JSON_BYTES := 1024 * 1024
const MAX_WIRE_BYTES := 256 * 1024
const MAX_LINE_CHARS := 2048
const MAX_BACKTRACE := 100
const MAX_LOG_LINES := 200
const NO_BACKTRACE := "unknown: no backtrace was recorded"
const LINE_WITHHELD := "[line withheld: private data]"
const MODES := ["skirmish", "multiplayer", "campaign", "replay", "unknown", "menu"]

var log_line: Callable  # (text) -> void: where this client's lines go (the game's print, the launcher's Log.line)
var _http: HTTPRequest
var _cancelled := false
var _in_flight := false
const RESULT_NAMES := ["success", "chunked body size mismatch", "cannot connect", "cannot resolve", "connection error", "TLS handshake error",
	"no response", "body size limit exceeded", "body decompression failed", "request failed", "download file cannot open",
	"download file write error", "redirect limit reached", "timed out"]


func _init() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS # a paused tree (the fatal error box) still sends


# ---- the report ---------------------------------------------------------------------------------------------------------------------------

## facts: {version, os_name, os_version, cpu_name, gpu_name, context: {map, mode, factions}, settings: {resolution_width, resolution_height,
## texture_quality, shadows, vsync}, header: [lines put first in log_tail]}; a missing fact is "unknown". lines: the crashed run's log, oldest first.
## -> {ok, error, json, wire, data}
static func build(facts: Dictionary, lines: PackedStringArray, redact: Callable) -> Dictionary:
	if not redact.is_valid():
		return _failure("crash.redactor_invalid")
	var platform := client_platform(str(facts.get("os_name", "")))
	if platform.is_empty():
		return _failure("crash.platform_unsupported")
	var version := _fact(facts, "version", 64)
	var context: Dictionary = facts.get("context", {}) if typeof(facts.get("context")) == TYPE_DICTIONARY else {}
	var factions: Array = []
	var given_factions: Array = context.get("factions", []) if typeof(context.get("factions")) == TYPE_ARRAY else []
	for f in given_factions:
		if factions.size() < 8:
			factions.append(str(f).left(64))
	var mode := str(context.get("mode", "unknown"))
	var settings_in: Dictionary = facts.get("settings", {}) if typeof(facts.get("settings")) == TYPE_DICTIONARY else {}
	var settings := {}
	for key in ["resolution_width", "resolution_height"]:
		var v: Variant = settings_in.get(key, "unknown")
		settings[key] = int(v) if typeof(v) in [TYPE_INT, TYPE_FLOAT] and float(v) >= 0 and float(v) <= 16384 else "unknown"
	var tq := str(settings_in.get("texture_quality", "unknown"))
	settings["texture_quality"] = tq if tq in ["low", "medium", "high"] else "unknown"
	for key in ["shadows", "vsync"]:
		settings[key] = settings_in[key] if typeof(settings_in.get(key)) == TYPE_BOOL else "unknown"
	var tail: Array = []
	var backtrace: Array = []
	var frame := RegEx.create_from_string("^[\\t ]*(?:at:|SCRIPT ERROR:|ERROR:|FATAL:|GAME FATAL:|CrashHandlerException:|Program crashed|\\[\\d+\\][\\t ]+)")
	for raw in lines:
		var line: String = raw.trim_suffix("\r")
		var clean := scrub_line(line, redact)
		if clean.length() > MAX_LINE_CHARS:
			clean = clean.left(MAX_LINE_CHARS)
		tail.append(clean)
		if backtrace.size() < MAX_BACKTRACE and frame.search(clean) != null:
			backtrace.append(clean)
	var header: Array = []
	var given_header: Array = facts.get("header", []) if typeof(facts.get("header")) == TYPE_ARRAY else []
	for h in given_header:
		header.append(scrub_line(str(h), redact).left(MAX_LINE_CHARS))
	var keep := MAX_LOG_LINES - header.size()
	if tail.size() > keep:
		tail = tail.slice(tail.size() - keep)
	if backtrace.is_empty():
		backtrace.append(NO_BACKTRACE)
	var data := {
		"backtrace": backtrace,
		"build_kind": "release",
		"client": {"platform": platform, "version": version},
		"context": {
			"factions": factions,
			"map": scrub_line(str(context.get("map", "unknown")), redact).left(256),
			"mode": mode if mode in MODES else "unknown",
		},
		"cpu_name": scrub_line(_fact(facts, "cpu_name", 256), redact).left(256),
		"gpu_name": scrub_line(_fact(facts, "gpu_name", 256), redact).left(256),
		"log_tail": header + tail,
		"os_name": _fact(facts, "os_name", 128),
		"os_version": scrub_line(_fact(facts, "os_version", 128), redact).left(128),
		"schema_version": 1,
		"settings": settings,
		"version": version,
	}
	for i in factions.size():
		data.context.factions[i] = scrub_line(factions[i], redact).left(64)
	for key in ["cpu_name", "gpu_name", "os_version"]:
		if data[key].is_empty():
			data[key] = "unknown"
	# too large for the service: the log's oldest half goes, until it fits (the header and the backtrace stay)
	while true:
		var json_bytes := JSON.stringify(_canonical(data), "", false).to_utf8_buffer()
		var wire := json_bytes.compress(FileAccess.COMPRESSION_GZIP)
		var shape := wire_problem(wire) if json_bytes.size() <= MAX_JSON_BYTES else "crash.report_too_large"
		if shape.is_empty():
			return {"ok": true, "error": "", "json": json_bytes, "wire": wire, "data": data}
		if shape != "crash.report_too_large" or tail.is_empty():
			return _failure(shape)
		tail = tail.slice(maxi(1, tail.size() / 2)) # at least one line goes each round (Sol r2)
		data.log_tail = header + tail
	return _failure("crash.report_too_large")


static func _failure(error: String) -> Dictionary:
	return {"ok": false, "error": error, "json": PackedByteArray(), "wire": PackedByteArray(), "data": {}}


static func _fact(facts: Dictionary, key: String, limit: int) -> String:
	var v := str(facts.get(key, "")).strip_edges()
	return "unknown" if v.is_empty() else v.left(limit)


static func client_platform(os_name: String) -> String:
	return {"Windows": "windows", "Linux": "linux", "macOS": "macos"}.get(os_name, "")


## "" or why these bytes are not a report the service takes: gzip with no optional fields and mtime 0, at most MAX_WIRE_BYTES
static func wire_problem(wire: PackedByteArray) -> String:
	if wire.size() < 18 or wire[0] != 0x1f or wire[1] != 0x8b or wire[2] != 8 or wire[3] != 0:
		return "crash.report_gzip_invalid"
	if wire.slice(4, 8) != PackedByteArray([0, 0, 0, 0]):
		return "crash.report_gzip_invalid"
	if wire.size() > MAX_WIRE_BYTES:
		return "crash.report_too_large"
	return ""


## The last lines of a log file (at most TAIL_BYTES read; the first, cut line dropped). -> {ok, error, lines}
static func read_tail(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"ok": false, "error": "crash.log_unreadable (%s)" % error_string(FileAccess.get_open_error()), "lines": PackedStringArray()}
	var length := file.get_length()
	var start := maxi(0, length - TAIL_BYTES)
	file.seek(start)
	var raw := file.get_buffer(length - start)
	file.close()
	if start > 0:
		var newline := raw.find(10)
		raw = raw.slice(newline + 1) if newline >= 0 else PackedByteArray()
	var lines := raw.get_string_from_utf8().split("\n")
	if not lines.is_empty() and lines[-1].is_empty():
		lines.remove_at(lines.size() - 1)
	return {"ok": true, "error": "", "lines": lines}


## One string through the caller's filter, scrub() and the service's own rules (a line it would still refuse is withheld whole).
static func scrub_line(value: String, redact: Callable) -> String:
	var filtered: Variant = redact.call(value)
	var clean := scrub(str(filtered) if typeof(filtered) == TYPE_STRING else LINE_WITHHELD)
	return LINE_WITHHELD if service_would_refuse(clean) else clean


## lane crashclient2's scrubber: private values a log line can hold besides the home folder and the user name
static func scrub(value: String) -> String:
	var clean := value
	var resource_paths: Array[String] = []
	var resource_regex := RegEx.create_from_string("res://[A-Za-z0-9._/-]+")
	var resource_match := resource_regex.search(clean)
	while resource_match != null:
		var source_path: String = resource_match.get_string()
		if not source_path.contains("/../") and not source_path.ends_with("/.."):
			var placeholder := "__OPENBFME_RESOURCE_%d__" % resource_paths.size()
			resource_paths.append(source_path)
			clean = clean.substr(0, resource_match.get_start()) + placeholder + clean.substr(resource_match.get_end())
			resource_match = resource_regex.search(clean, resource_match.get_start() + placeholder.length())
		else:
			resource_match = resource_regex.search(clean, resource_match.get_end())
	clean = RegEx.create_from_string("(?i)\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}\\b").sub(clean, "[redacted-email]", true)
	clean = RegEx.create_from_string("(?i)\\b[a-z][a-z0-9+.-]*://[^\\s<>\"']+").sub(clean, "[redacted-url]", true)
	clean = RegEx.create_from_string("(?i)(?:\"(?:[A-Za-z]:[\\\\/]|/|\\\\\\\\)[^\"\\r\\n]*\"|'(?:[A-Za-z]:[\\\\/]|/|\\\\\\\\)[^'\\r\\n]*')").sub(clean, "[redacted-path]", true)
	clean = RegEx.create_from_string("(?i)(?:\\\\\\\\|(?<!:)//)[^\\\\/\\s,;]+[\\\\/][^\\s,;]+").sub(clean, "[redacted-network-path]", true)
	clean = RegEx.create_from_string("(?i)\\b(?:host(?:name)?|computer(?:name)?|machine(?:name)?)\\s*[:=]\\s*[^\\s,;]+").sub(clean, "[redacted-host]", true)
	for hostname in [OS.get_environment("HOSTNAME"), OS.get_environment("COMPUTERNAME")]:
		if hostname.length() >= 2:
			clean = _replace_case_insensitive(clean, hostname, "[redacted-host]")
	var dotted_host := RegEx.create_from_string("(?i)(?<![A-Za-z0-9_.-])(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+[A-Za-z][A-Za-z0-9-]{0,62}(?![A-Za-z0-9_.-])")
	var hash_like := RegEx.create_from_string("(?i)^[0-9a-f]{8,}(?:\\.[0-9a-f]{4,})+$")
	var safe_extensions := ["gd", "cpp", "cc", "c", "h", "hpp", "png", "jpg", "jpeg", "webp", "tga", "dds", "xml", "json", "ini", "map", "apt",
		"w3d", "str", "big", "log", "txt", "cfg", "dat", "wav", "ogg", "mp3", "shader", "glsl", "tres", "tscn", "res", "uid", "dll", "so",
		"exe", "pck", "csf", "inc", "bik", "scb", "wnd", "replay", "running"]
	var dotted_match := dotted_host.search(clean)
	while dotted_match != null:
		var token: String = dotted_match.get_string()
		if token.get_extension().to_lower() in safe_extensions or hash_like.search(token) != null:
			dotted_match = dotted_host.search(clean, dotted_match.get_end())
		else:
			clean = clean.substr(0, dotted_match.get_start()) + "[redacted-host]" + clean.substr(dotted_match.get_end())
			dotted_match = dotted_host.search(clean, dotted_match.get_start() + "[redacted-host]".length())
	# what players typed (chat, beacons, the message lines: "GAME CHAT line slot 2: ...", "lines [...]"): everything after the word goes
	var said := RegEx.create_from_string("(?i)\\b(?:chat|beacons?|whisper|player[_ -]?message|message[_ ]lines|lines state)\\b").search(clean)
	if said != null:
		clean = clean.substr(0, said.get_end()) + " [redacted-chat]"
	var path_regex := RegEx.create_from_string("(?i)(?:[A-Za-z]:[\\\\/][^\\s,;\"']+|(?<![\\w:/\\]])/(?!/)[^\\s,;\"']+)")
	var source_basename := RegEx.create_from_string("(?i)^([A-Za-z0-9_.-]+\\.(?:gd|cpp|cc|c|h|hpp|inc))(?:[:(]\\d+\\)?)?$")
	var path_match := path_regex.search(clean)
	while path_match != null:
		var basename := path_match.get_string().replace("\\", "/").get_file()
		var replacement := "[redacted-path]"
		var source_match := source_basename.search(basename)
		if source_match != null:
			replacement += "/" + source_match.get_string(1)
		clean = clean.substr(0, path_match.get_start()) + replacement + clean.substr(path_match.get_end())
		path_match = path_regex.search(clean, path_match.get_start() + replacement.length())
	clean = RegEx.create_from_string("(?<![\\w.])(?:\\d{1,3}\\.){3}\\d{1,3}(?![\\w.])").sub(clean, "[redacted-ip]", true)
	clean = RegEx.create_from_string("(?<![\\w:])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![\\w:])").sub(clean, "[redacted-ip]", true)
	clean = RegEx.create_from_string("(?i)\\b(?:access[_ -]?key|api[_ -]?key|client[_ -]?key|credential|secret|token|ticket|password|passphrase|authorization|bearer|signature|private[_ -]?(?:field|file|data)|user(?:name|[-_ ]name)|player(?:name|[-_ ]name)|e?mail)\\b\\s*(?:[:=]|\\s+)\\s*[^,;\\r\\n]+").sub(clean, "[redacted-private]", true)
	for index in resource_paths.size():
		clean = clean.replace("__OPENBFME_RESOURCE_%d__" % index, resource_paths[index])
	return clean


static func _replace_case_insensitive(value: String, token: String, replacement: String) -> String:
	var position := value.to_lower().find(token.to_lower())
	while position >= 0:
		value = value.substr(0, position) + replacement + value.substr(position + token.length())
		position = value.to_lower().find(token.to_lower(), position + replacement.length())
	return value


## The crash service refuses a whole report when one string holds what looks like an IP address, a home folder or a credential label
## (services/crash protocol.py _contains_private_marker; mirrored here, erring on the side of refusing)
static func service_would_refuse(value: String) -> bool:
	if RegEx.create_from_string("(?i)(?:[A-Za-z]:\\\\User[s]\\\\[^\\\\\\s]+|/hom[e]/[^/\\s]+|/User[s]/[^/\\s]+)").search(value) != null:
		return true
	if RegEx.create_from_string("(?i)\\b(?:access[_ -]?key|api[_ -]?key|credential|secret|token|ticket|password|authorization|bearer|user(?:name|[-_ ]name))\\b\\s*(?:[:=]|\\s)").search(value) != null:
		return true
	for m in RegEx.create_from_string("(?<![\\w.])(?:\\d{1,3}\\.){3}\\d{1,3}(?![\\w.])").search_all(value):
		if Array(m.get_string().split(".")).all(func(o: String) -> bool: return int(o) <= 255):
			return true
	for m in RegEx.create_from_string("(?<![\\w:])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![\\w:])").search_all(value):
		var text := m.get_string()
		if text.contains("::") or text.split(":").size() == 8:
			return true
	return false


static func _canonical(value: Variant) -> Variant:
	if typeof(value) == TYPE_ARRAY:
		return (value as Array).map(func(v: Variant) -> Variant: return _canonical(v))
	if typeof(value) != TYPE_DICTIONARY:
		return value
	var keys: Array = value.keys()
	keys.sort()
	var out := {}
	for key in keys:
		out[key] = _canonical(value[key])
	return out


# ---- the queue ----------------------------------------------------------------------------------------------------------------------------

## Adds a report's wire bytes to the queue folder (made when missing), then applies the limits. -> {ok, error, path, dropped: [[name, reason]]}
static func enqueue(dir: String, wire: PackedByteArray, now: int = -1) -> Dictionary:
	var shape := wire_problem(wire)
	if not shape.is_empty():
		return {"ok": false, "error": shape, "path": "", "dropped": []}
	var made := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(dir))
	if made != OK and made != ERR_ALREADY_EXISTS:
		return {"ok": false, "error": "crash.queue_folder (%s)" % error_string(made), "path": "", "dropped": []}
	var stamp := now if now >= 0 else int(Time.get_unix_time_from_system())
	var name := "%010d-%s.json.gz" % [stamp, Crypto.new().generate_random_bytes(6).hex_encode()]
	var path := dir.path_join(name)
	var temporary := path + ".part"
	var file := FileAccess.open(temporary, FileAccess.WRITE)
	if file == null:
		return {"ok": false, "error": "crash.queue_write (%s)" % error_string(FileAccess.get_open_error()), "path": "", "dropped": []}
	file.store_buffer(wire)
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
		return {"ok": false, "error": "crash.queue_write (%s)" % error_string(write_error), "path": "", "dropped": []}
	var renamed := DirAccess.rename_absolute(ProjectSettings.globalize_path(temporary), ProjectSettings.globalize_path(path))
	if renamed != OK:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(temporary))
		return {"ok": false, "error": "crash.queue_write (%s)" % error_string(renamed), "path": "", "dropped": []}
	var pruned := prune(dir, stamp)
	return {"ok": FileAccess.file_exists(path), "error": "" if FileAccess.file_exists(path) else "crash.queue_full", "path": path,
		"dropped": pruned.dropped}


## The queued reports, oldest first (file names)
static func queued(dir: String) -> PackedStringArray:
	var names := PackedStringArray()
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(dir)):
		return names
	for name in DirAccess.get_files_at(dir):
		if _queue_name(name):
			names.append(name)
	names.sort()
	return names


static func _queue_name(name: String) -> bool:
	return RegEx.create_from_string("^\\d{10}-[0-9a-f]{12}\\.json\\.gz$").search(name) != null


## Applies the limits: anything that is not a queued report (a .part a crash left) goes; then reports older than QUEUE_MAX_DAYS, then the oldest
## while there are more than QUEUE_MAX_FILES or QUEUE_MAX_BYTES. -> {dropped: [[name, reason]], errors: [text]}
static func prune(dir: String, now: int = -1) -> Dictionary:
	var stamp := now if now >= 0 else int(Time.get_unix_time_from_system())
	var dropped: Array = []
	var errors: Array = []
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(dir)):
		return {"dropped": dropped, "errors": errors}
	for name in DirAccess.get_files_at(dir):
		if not _queue_name(name) and name.ends_with(".part"):
			_drop(dir, name, "an unfinished write", dropped, errors)
	var names := queued(dir)
	var keep: Array = []
	for name in names:
		if stamp - int(name.substr(0, 10)) > QUEUE_MAX_DAYS * 86400:
			_drop(dir, name, "older than %d days" % QUEUE_MAX_DAYS, dropped, errors)
		else:
			keep.append(name)
	var total := 0
	var sizes := {}
	for name in keep:
		var f := FileAccess.open(dir.path_join(name), FileAccess.READ)
		sizes[name] = f.get_length() if f != null else 0
		total += sizes[name]
	while keep.size() > QUEUE_MAX_FILES or (total > QUEUE_MAX_BYTES and not keep.is_empty()):
		var oldest: String = keep.pop_front()
		total -= sizes[oldest]
		_drop(dir, oldest, "the queue is full (at most %d reports, %d bytes)" % [QUEUE_MAX_FILES, QUEUE_MAX_BYTES], dropped, errors)
	return {"dropped": dropped, "errors": errors}


static func _drop(dir: String, name: String, reason: String, dropped: Array, errors: Array) -> void:
	var e := DirAccess.remove_absolute(ProjectSettings.globalize_path(dir.path_join(name)))
	if e == OK:
		dropped.append([name, reason])
	else:
		errors.append("crash.queue_remove %s (%s)" % [name, error_string(e)])


## This install's crash id (a random UUID4 the service limits submissions by; nothing else uses it), made on first use. -> {ok, error, id}
static func install_id(path: String) -> Dictionary:
	if FileAccess.file_exists(path):
		var text := FileAccess.get_file_as_string(path).strip_edges()
		if _is_uuid4(text):
			return {"ok": true, "error": "", "id": text}
	var id := uuid4()
	var made := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
	if made != OK and made != ERR_ALREADY_EXISTS:
		return {"ok": false, "error": "crash.install_id_folder (%s)" % error_string(made), "id": ""}
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return {"ok": false, "error": "crash.install_id_write (%s)" % error_string(FileAccess.get_open_error()), "id": ""}
	file.store_string(id + "\n")
	file.close()
	return {"ok": true, "error": "", "id": id}


static func uuid4() -> String:
	var b := Crypto.new().generate_random_bytes(16)
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	var h := b.hex_encode()
	return "%s-%s-%s-%s-%s" % [h.substr(0, 8), h.substr(8, 4), h.substr(12, 4), h.substr(16, 4), h.substr(20, 12)]


static func _is_uuid4(text: String) -> bool:
	return RegEx.create_from_string("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$").search(text) != null


# ---- the upload ---------------------------------------------------------------------------------------------------------------------------

## Where reports go and the key that signs them: the service, or a test's loopback endpoint. -> {ok, error, url, key}
static func target() -> Dictionary:
	if OS.get_environment(UPLOAD_ENV) == "0":
		return {"ok": false, "error": "crash.upload_off (OPENBFME_CRASH_UPLOAD=0: a CI host or test run sends nothing)", "url": "", "key": ""}
	var test_url := OS.get_environment(TEST_ENDPOINT_ENV)
	if not test_url.is_empty():
		var m := RegEx.create_from_string("^http://127\\.0\\.0\\.1:([1-9][0-9]{0,4})/v1/reports$").search(test_url)
		if m == null or int(m.get_string(1)) > 65535:
			return {"ok": false, "error": "crash.test_endpoint_refused (only http://127.0.0.1:<port>/v1/reports)", "url": "", "key": ""}
		return {"ok": true, "error": "", "url": test_url, "key": OS.get_environment(TEST_KEY_ENV)}
	if not ResourceLoader.exists(KEY_SCRIPT):
		return {"ok": false, "error": "crash.no_client_key (this build has no crash-report signing key)", "url": "", "key": ""}
	var script := load(KEY_SCRIPT) as Script
	var key := str(script.get_script_constant_map().get("KEY", "")) if script != null else ""
	if RegEx.create_from_string("^[A-Za-z0-9]{64}$").search(key) == null:
		return {"ok": false, "error": "crash.client_key_invalid (%s holds no 64-character KEY)" % KEY_SCRIPT, "url": "", "key": ""}
	return {"ok": true, "error": "", "url": ENDPOINT, "key": key}


## Sends the queue (see the header). id_path: the install id file. Emits `finished` and returns
## {sent: [[name, report_id]], dropped: [[name, reason]], kept: count, error: "" or why the run stopped}.
func upload_queue(dir: String, id_path: String) -> Dictionary:
	_cancelled = false
	var summary := {"sent": [], "dropped": [], "kept": 0, "error": "", "errors": []}
	var pruned := prune(dir)
	summary.dropped.append_array(pruned.dropped)
	summary.errors.append_array(pruned.errors)
	var names := queued(dir)
	if names.is_empty():
		return _finish(summary, names)
	var where := target()
	var identity := install_id(id_path) if where.ok else {}
	if not where.ok:
		summary.error = where.error
	elif not identity.ok:
		summary.error = identity.error
	var attempts := 0
	for name in names:
		if not summary.error.is_empty() or _cancelled or attempts >= UPLOADS_PER_RUN:
			break
		attempts += 1
		var wire := FileAccess.get_file_as_bytes(dir.path_join(name))
		var shape := wire_problem(wire)
		if not shape.is_empty():
			_drop(dir, name, "not a report the service takes: " + shape, summary.dropped, summary.errors)
			continue
		var r: Dictionary = await _post(where.url, where.key, identity.id, wire)
		if r.status == 201 and not r.report_id.is_empty():
			var removed := DirAccess.remove_absolute(ProjectSettings.globalize_path(dir.path_join(name)))
			summary.sent.append([name, r.report_id])
			if removed != OK:
				summary.error = "crash.queue_remove %s (%s): it would be sent again" % [name, error_string(removed)]
		elif r.status in [400, 413]:
			_drop(dir, name, "refused by the crash service: %d %s" % [r.status, r.code], summary.dropped, summary.errors)
		else:
			summary.error = r.error
	return _finish(summary, names)


func _finish(summary: Dictionary, before: PackedStringArray) -> Dictionary:
	summary.kept = before.size() - summary.sent.size() - summary.dropped.filter(func(d: Array) -> bool: return d[0] in before).size()
	for s in summary.sent:
		_log("CRASH report %s sent: report id %s" % s)
	for d in summary.dropped:
		_log("CRASH report %s dropped: %s" % d)
	for e in summary.errors:
		_log("CRASH queue: " + e)
	if not summary.error.is_empty():
		_log("CRASH upload stopped: %s; %d report(s) stay queued for the next start" % [summary.error, summary.kept])
	elif summary.kept > 0:
		_log("CRASH %d report(s) stay queued for the next start" % summary.kept)
	finished.emit(summary)
	return summary


## One signed POST. -> {status, code, report_id, error}
func _post(url: String, key: String, install: String, wire: PackedByteArray) -> Dictionary:
	if _http == null:
		_http = HTTPRequest.new()
		_http.max_redirects = 0
		# REQUEST_SECONDS of running frames (scripts/http_timed.gd): HTTPRequest's own timeout counts the game's blocking start-up frames (a
		# 10 s load is 10 s of a 15 s timeout, the reply never polled), so a slow PC timed out at every start (lane CRASHACT-1 r3, seen on a
		# loaded CI host); no answer within them ends the request with "timed out", its report kept for the next start
		HttpTimed.set_budget(_http, REQUEST_SECONDS)
		_http.accept_gzip = false
		_http.body_size_limit = MAX_RESPONSE_BYTES
		add_child(_http)
	var nonce := uuid4()
	var now := int(Time.get_unix_time_from_system())
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(wire)
	var canonical := "crash-v1\nPOST\n/v1/reports\n%d\n%s\n%s\n%s" % [now, install, nonce, digest.finish().hex_encode()]
	var signature := Crypto.new().hmac_digest(HashingContext.HASH_SHA256, key.to_utf8_buffer(), canonical.to_utf8_buffer()).hex_encode()
	var headers := PackedStringArray(["Content-Type: application/octet-stream", "Content-Encoding: gzip", "X-Crash-Time: %d" % now,
		"X-Crash-Install-Id: " + install, "X-Crash-Nonce: " + nonce, "X-Crash-Signature: " + signature])
	var started := _http.request_raw(url, headers, HTTPClient.METHOD_POST, wire)
	if started != OK:
		return {"status": 0, "code": "", "report_id": "", "error": "crash.request_failed (%s)" % error_string(started)}
	_in_flight = true
	var done: Array = await _http.request_completed
	_in_flight = false
	var result: int = done[0]
	var status: int = done[1]
	var body: PackedByteArray = done[3]
	if _cancelled:
		return {"status": 0, "code": "", "report_id": "", "error": "crash.cancelled (the program is closing)"}
	if result != HTTPRequest.RESULT_SUCCESS:
		var what: String = RESULT_NAMES[result] if result >= 0 and result < RESULT_NAMES.size() else "result %d" % result
		return {"status": 0, "code": "", "report_id": "", "error": "crash.network (%s)" % what}
	var reply: Variant = JSON.parse_string(body.get_string_from_utf8()) if body.size() <= MAX_RESPONSE_BYTES else null
	var code := str(reply.get("error", "")) if reply is Dictionary else ""
	var id := str(reply.get("report_id", "")) if reply is Dictionary else ""
	if status == 201 and RegEx.create_from_string("^[0-9a-f]{24}$").search(id) == null:
		return {"status": 0, "code": "", "report_id": "", "error": "crash.reply_invalid (201 without a report id)"}
	return {"status": status, "code": code.left(64), "report_id": id,
		"error": "" if status == 201 else "crash.refused (%d %s)" % [status, code.left(64)]}


## The program is closing: the request in flight is dropped (its report stays queued), nothing more is sent.
func cancel() -> void:
	_cancelled = true
	_stop_request(HTTPRequest.RESULT_CANT_CONNECT)


func _stop_request(result: int) -> void:
	if _http != null and _in_flight:
		_in_flight = false
		_http.cancel_request()
		_http.request_completed.emit(result, 0, PackedStringArray(), PackedByteArray())


func _exit_tree() -> void:
	cancel()


func _log(text: String) -> void:
	if log_line.is_valid():
		log_line.call(text)
	else:
		print(text)
