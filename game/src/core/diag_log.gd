extends Node
## The sim half of the shared Open-BFME diagnostic run contract.
##
## WHY THIS EXISTS. The failure this project keeps shipping is not a crash, it is
## a CONFIDENTLY WRONG RUN: a stale durable pack quietly served yesterday's
## content, a PATH ffmpeg quietly failed a pinned-hash attest, and both produced
## numbers that passed review. AGENTS.md rule 5 says a fallback must say so
## loudly - and ModLoader does print that notice - but a playtester's report
## arrives as "it looked wrong", with the console long gone. Nothing on this
## machine survives the process.
##
## So this records ONE RUN to disk, in the same shape the importer, converter and
## launcher use, so a single bug report answers "which pack actually mounted, and
## why" without anyone re-deriving it from a selection document that is itself
## the thing under suspicion (the same reason ModLoader.runtime_pack_report
## exists).
##
## THE CONTRACT (identical for all four components):
##   root      %LOCALAPPDATA%\OpenBFME\logs   (override: OPENBFME_LOG_DIR)
##   run dir   <root>/<UTC yyyyMMddTHHmmssZ>-<component>-<pid>/
##   files     run.log    human-readable, one timestamped line per event
##             run.jsonl  one JSON object per event: ts, level, component,
##                        event, fields
##             env.json   OS, engine build, renderer, allow-listed env vars
##                        (values REDACTED), git commit when resolvable
##             identity.json  active pack id + digest, supplemental packs,
##                        selection.json sha256 - read from the MOUNTED
##                        selection via ModLoader, never guessed
##             error.txt  written only on failure: the failures plus the tail
##   retention newest 20 run dirs kept, pruned on start; a run dir carrying
##             error.txt is never pruned until it is beyond 100 runs old
##
## OFF BY DEFAULT, and that is load-bearing. Normal play must behave exactly as
## it does today: no directories, no file handles, no per-frame spam. Opt in with
## OPENBFME_DIAGNOSTICS=1, a `-- --diagnostics` user argument, or the launcher's
## Diagnostics toggle (which sets the environment variable on the game process).
##
## USABLE WITHOUT THE AUTOLOAD. Test runners `extends SceneTree` and are launched
## with `--script`, which never instantiates autoloads, so every entry point here
## works on a bare `.new()` instance too. The static forwarders are the seam
## production code uses: they are a null check when no recorder is live.
##
## Reached by `preload`, deliberately NOT by `class_name` - same reason
## boot_profile.gd documents: a non-editor run reads global class names from
## `.godot/global_script_class_cache.cfg`, which only the editor regenerates.

const BootProfile = preload("res://src/core/boot_profile.gd")

const COMPONENT := "sim"
const SCHEMA_ENV := "openbfme.diagnostics.env"
const SCHEMA_IDENTITY := "openbfme.diagnostics.identity"
const SCHEMA_VERSION := 1

const ENABLE_ENV := "OPENBFME_DIAGNOSTICS"
const LOG_DIR_ENV := "OPENBFME_LOG_DIR"
const ENABLE_ARG := "--diagnostics"

## Retention. Both numbers come straight from the shared contract.
const RETENTION_KEEP := 20
const RETENTION_HARD_LIMIT := 100

## How much of the run tail error.txt repeats. Enough to see the approach to a
## failure, small enough that a bug-report zip stays mailable.
const TAIL_LINES := 80

## Environment variables a bug report is allowed to carry. An ALLOW LIST, not a
## deny list: a deny list ships the next secret somebody invents. Values still go
## through the redactor, because BFME2_INSTALL is a home path.
const ENV_ALLOW_LIST: Array[String] = [
	"OPENBFME_CONTENT",
	"OPENBFME_DIAGNOSTICS",
	"OPENBFME_LOG_DIR",
	"OPENBFME_PROFILE_BOOT",
	"OPENBFME_STRICT_PARITY_PROFILE",
	"OPENBFME_STRICT_PARITY_PACK_PREFIX",
	"OPENBFME_RUNNER_DEADLINE_MS",
	"OPENBFME_RUNNER_STALL_MS",
	"OPENBFME_CONTROL_PORT",
	"BFME2_INSTALL",
	"ROTWK_INSTALL",
]

## Names whose VALUE never leaves this machine whatever the allow list says.
## Belt and braces: the allow list above already excludes them.
const SECRET_NAME_FRAGMENTS: Array[String] = [
	"token", "secret", "password", "passwd", "apikey", "api_key",
	"privatekey", "private_key", "signingkey", "signing_key",
	"credential", "bearer", "authorization", "session",
]

const LEVEL_DEBUG := "debug"
const LEVEL_INFO := "info"
const LEVEL_WARN := "warn"
const LEVEL_ERROR := "error"

## The live recorder, or null. Static so production code can reach it through the
## forwarders below without every call site knowing whether autoloads exist.
static var _active: Node = null

var enabled := false
var run_dir := ""
var component := COMPONENT

var _log_file: FileAccess = null
var _jsonl_file: FileAccess = null
var _tail: Array[String] = []
var _failures: Array[String] = []
var _sample_last_ms: Dictionary = {}
var _phase_started_ms: Dictionary = {}
var _event_count := 0
var _dropped_samples := 0
var _identity_captured := false
var _closed := false


# ---------------------------------------------------------------------------
# Lifecycle
# ---------------------------------------------------------------------------

func _ready() -> void:
	# Autoload path. A bare `.new()` in a test runner calls begin() itself.
	if requested():
		begin()


func _exit_tree() -> void:
	close()


static func requested() -> bool:
	## Either OPENBFME_DIAGNOSTICS=1 or a `-- --diagnostics` user argument. The
	## argument form exists for the same reason boot_profile.gd has one: a runner
	## that launches the game as a child process can pass argv deterministically
	## where an inherited environment is a guess.
	if OS.get_environment(ENABLE_ENV) == "1":
		return true
	return OS.get_cmdline_user_args().has(ENABLE_ARG)


func begin(root_override: String = "") -> bool:
	## Create this process's run directory and start recording. Returns false and
	## changes NOTHING when the directory cannot be made - a diagnostics feature
	## that takes the game down with it is worse than no diagnostics.
	if enabled:
		return true
	var root := root_override if root_override != "" else log_root()
	if root == "":
		push_warning("[DiagLog] no writable log root; diagnostics stay off.")
		return false
	if DirAccess.make_dir_recursive_absolute(root) != OK and not DirAccess.dir_exists_absolute(root):
		push_warning("[DiagLog] could not create log root %s; diagnostics stay off." % root)
		return false

	# Prune BEFORE creating this run's directory, so "keep the newest 20" means
	# 20 finished runs plus this one, not 19 plus this one.
	prune(root)

	var candidate := root.path_join(run_directory_name(component))
	if DirAccess.make_dir_recursive_absolute(candidate) != OK and not DirAccess.dir_exists_absolute(candidate):
		push_warning("[DiagLog] could not create run directory %s; diagnostics stay off." % candidate)
		return false

	_log_file = FileAccess.open(candidate.path_join("run.log"), FileAccess.WRITE)
	_jsonl_file = FileAccess.open(candidate.path_join("run.jsonl"), FileAccess.WRITE)
	if _log_file == null or _jsonl_file == null:
		push_warning("[DiagLog] could not open run files in %s; diagnostics stay off." % candidate)
		_log_file = null
		_jsonl_file = null
		return false

	run_dir = candidate
	enabled = true
	_closed = false
	_active = self
	_write_env_document()
	event(LEVEL_INFO, "run.begin", {
		"runDirectory": redact(run_dir),
		"pid": OS.get_process_id(),
		"component": component,
	})
	# Autoloads are constructed before this node reaches the tree, so a few boot
	# stages have already been marked. Replay them rather than lose the earliest
	# and most interesting part of the timeline, then subscribe for the rest.
	for entry in BootProfile.marks():
		var mark: Dictionary = entry
		boot_phase(String(mark.get("label", "")), int(mark.get("delta_ms", -1)), int(mark.get("at_ms", -1)))
	BootProfile.add_sink(boot_phase)
	BootProfile.add_failure_sink(_on_boot_failure)
	return true


func _on_boot_failure(label: String, detail: String) -> void:
	failure("boot.failed", label, detail)


func close() -> void:
	## Idempotent. Writes error.txt when this run recorded any failure, then lets
	## go of the handles. Called from _exit_tree, from a runner's teardown, and
	## defensively from _notification(PREDELETE).
	if _closed:
		return
	_closed = true
	if not enabled:
		return
	BootProfile.remove_sink(boot_phase)
	BootProfile.remove_failure_sink(_on_boot_failure)
	event(LEVEL_INFO, "run.end", {
		"events": _event_count,
		"failures": _failures.size(),
		"droppedSamples": _dropped_samples,
	})
	if not _failures.is_empty():
		_write_error_document()
	if _log_file != null:
		_log_file.flush()
		_log_file = null
	if _jsonl_file != null:
		_jsonl_file.flush()
		_jsonl_file = null
	enabled = false
	if _active == self:
		_active = null


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		close()


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

static func _timestamp() -> String:
	## ISO 8601 with the T separator and an explicit Z. The contract's other three
	## components write .NET's "o" round-trip format; a bug report whose four
	## run.jsonl files cannot be merged on one time axis is four bug reports.
	return Time.get_datetime_string_from_system(true, false) + "Z"


static func log_root() -> String:
	## OPENBFME_LOG_DIR wins, so a test (or a support technician) can point a run
	## somewhere disposable. Otherwise the contract root. On a platform without
	## LOCALAPPDATA we use the engine's own user data directory rather than
	## inventing a Windows path that cannot exist.
	var override_root := OS.get_environment(LOG_DIR_ENV).strip_edges()
	if override_root != "":
		return override_root.replace("\\", "/").trim_suffix("/")
	var local_app_data := OS.get_environment("LOCALAPPDATA").strip_edges()
	if local_app_data != "":
		return local_app_data.replace("\\", "/").trim_suffix("/").path_join("OpenBFME/logs")
	return OS.get_user_data_dir().replace("\\", "/").trim_suffix("/").path_join("logs")


static func run_directory_name(for_component: String, at_unix_time: int = -1) -> String:
	## <UTC yyyyMMddTHHmmssZ>-<component>-<pid>. The stamp is deliberately the
	## compact basic ISO form with no separators: it is a DIRECTORY NAME on
	## Windows, where ':' is illegal, and it sorts lexicographically in the same
	## order it sorts chronologically - which is what makes retention a sort.
	var stamp := (
		Time.get_datetime_string_from_system(true, false)
		if at_unix_time < 0
		else Time.get_datetime_string_from_unix_time(at_unix_time, false)
	)
	stamp = stamp.replace("-", "").replace(":", "")
	if not stamp.ends_with("Z"):
		stamp += "Z"
	return "%s-%s-%d" % [stamp, for_component, OS.get_process_id()]


# ---------------------------------------------------------------------------
# Retention
# ---------------------------------------------------------------------------

static func prune(root: String) -> Array[String]:
	## Keep the newest RETENTION_KEEP run directories. A directory carrying
	## error.txt is EXEMPT until it is beyond RETENTION_HARD_LIMIT runs old,
	## because the failing run is the one the bug report is about - deleting it to
	## make room for twenty uneventful successes is exactly backwards.
	##
	## Returns the directories it removed, so a test can assert the decision
	## rather than the side effect.
	var removed: Array[String] = []
	if not DirAccess.dir_exists_absolute(root):
		return removed
	var dir := DirAccess.open(root)
	if dir == null:
		return removed
	var names: Array[String] = []
	for name in dir.get_directories():
		if _looks_like_run_directory(name):
			names.append(name)
	names.sort()
	names.reverse()  # newest first: the stamp sorts chronologically.
	for index in names.size():
		if index < RETENTION_KEEP:
			continue
		var path := root.path_join(names[index])
		if index < RETENTION_HARD_LIMIT and FileAccess.file_exists(path.path_join("error.txt")):
			continue
		if _remove_tree(path):
			removed.append(names[index])
	return removed


static func _looks_like_run_directory(name: String) -> bool:
	## <stamp>-<component>-<pid>. Conservative on purpose: retention must never
	## delete something a human put in the log root by hand.
	var parts := name.split("-")
	if parts.size() != 3:
		return false
	var stamp := String(parts[0])
	if stamp.length() != 16 or not stamp.ends_with("Z") or stamp[8] != "T":
		return false
	for index in stamp.length():
		var character := stamp[index]
		if index == 8 or index == 15:
			continue
		if not (character >= "0" and character <= "9"):
			return false
	if not String(parts[1]) in ["importer", "converter", "sim", "launcher"]:
		return false
	return String(parts[2]).is_valid_int()


static func _remove_tree(path: String) -> bool:
	var dir := DirAccess.open(path)
	if dir == null:
		return false
	for name in dir.get_files():
		dir.remove(name)
	for name in dir.get_directories():
		_remove_tree(path.path_join(name))
	return DirAccess.remove_absolute(path) == OK


# ---------------------------------------------------------------------------
# Recording
# ---------------------------------------------------------------------------

func event(level: String, name: String, fields: Dictionary = {}) -> void:
	if not enabled:
		return
	var scrubbed := _scrub_fields(fields)
	var stamp := _timestamp()
	var line := "%s %-5s %s %s" % [stamp, level.to_upper(), name, JSON.stringify(scrubbed)]
	_tail.append(line)
	if _tail.size() > TAIL_LINES:
		_tail.pop_front()
	_event_count += 1
	if _log_file != null:
		_log_file.store_line(line)
		_log_file.flush()
	if _jsonl_file != null:
		_jsonl_file.store_line(JSON.stringify({
			"ts": stamp,
			"level": level,
			"component": component,
			"event": name,
			"fields": scrubbed,
		}))
		# Flushed per event ON PURPOSE. The runs worth reporting are the ones that
		# ended in a crash, and a buffered final second is exactly the second that
		# matters. A diagnostics-only run can afford the syscall.
		_jsonl_file.flush()


func failure(name: String, message: String, detail: String = "") -> void:
	## An event that also earns this run an error.txt. Use for anything a bug
	## report should open on: a refusal, a desync, an unhandled error.
	_failures.append("%s: %s%s" % [name, message, ("\n" + detail) if detail != "" else ""])
	event(LEVEL_ERROR, name, {"message": message, "detail": detail})


func sampled(key: String, interval_ms: int, level: String, name: String, fields: Dictionary = {}) -> void:
	## Rate-limited event, for anything that can fire per frame. Even inside
	## diagnostics mode a 60 Hz event would bury the run in noise and make the
	## recorder itself the slow thing; the drops are counted and reported at
	## run.end so the sample rate is never mistaken for the real rate.
	if not enabled:
		return
	var now := Time.get_ticks_msec()
	# GDScript only allows non-negative bit-shift operands; use a plain sentinel.
	var last: int = int(_sample_last_ms.get(key, -1_000_000_000_000))
	if now - last < maxi(1, interval_ms):
		_dropped_samples += 1
		return
	_sample_last_ms[key] = now
	event(level, name, fields)


func phase_begin(name: String, fields: Dictionary = {}) -> void:
	if not enabled:
		return
	_phase_started_ms[name] = Time.get_ticks_msec()
	event(LEVEL_DEBUG, "phase.begin", _merge({"phase": name}, fields))


func phase_end(name: String, fields: Dictionary = {}) -> void:
	if not enabled:
		return
	var started: int = int(_phase_started_ms.get(name, -1))
	var elapsed := (Time.get_ticks_msec() - started) if started >= 0 else -1
	_phase_started_ms.erase(name)
	event(LEVEL_INFO, "phase.end", _merge({"phase": name, "elapsedMs": elapsed}, fields))


func boot_phase(label: String, delta_ms: int, at_ms: int) -> void:
	## Boot stages, mirrored from boot_profile.gd so the timeline the owner's
	## "several seconds of black window" report was measured with lands in the
	## bug report too, instead of only on a console nobody kept.
	event(LEVEL_INFO, "boot.phase", {"phase": label, "deltaMs": delta_ms, "atMs": at_ms})


# ---------------------------------------------------------------------------
# Identity
# ---------------------------------------------------------------------------

func capture_content_identity(report: Dictionary, supplements: Array = []) -> Dictionary:
	## Write identity.json from ModLoader.runtime_pack_report(). The report is
	## derived from the MOUNTED roots, never from the selection document, which is
	## the whole point: a run that mounted something other than what the selection
	## names has to be visible in the artifact (RULE P8).
	##
	## Returns the document it wrote, so a test can assert the content without
	## re-reading the file, and so a caller can log a summary line.
	var active_root := String(report.get("activePackRoot", ""))
	var active_identity: Dictionary = {}
	var roots: Array = []
	var ordered: Array = report.get("orderedPacks", []) as Array
	var source := String(report.get("activeContentSource", ""))
	for entry in ordered:
		var identity: Dictionary = entry
		if bool(identity.get("active", false)):
			active_identity = identity
		roots.append({
			"packRoot": redact(String(identity.get("packRoot", ""))),
			"packId": String(identity.get("packId", "")),
			"bundleDigest": String(identity.get("bundleDigest", "")),
			"contentAddressed": bool(identity.get("contentAddressed", false)),
			"reason": _mount_reason(identity, source),
		})
	var selection_path := String(report.get("activeSelectionPath", ""))
	var document := {
		"schema": SCHEMA_IDENTITY,
		"schemaVersion": SCHEMA_VERSION,
		"component": component,
		"capturedFrom": "ModLoader.runtime_pack_report",
		"capturedAt": _timestamp(),
		"activeContentSource": source,
		"activeContentSourceReason": source_reason(source),
		"activeSelectionPath": redact(selection_path),
		"activeSelectionSha256": file_sha256(selection_path),
		"activePackRoot": redact(active_root),
		"activePackId": String(active_identity.get("packId", "")),
		"activePackDigest": String(active_identity.get("bundleDigest", "")),
		"activePackContentAddressed": bool(active_identity.get("contentAddressed", false)),
		"supplementalPacks": _redact_list(supplements),
		"orderedPackIds": report.get("orderedPackIds", []),
		"mountedRoots": roots,
		"strictParityProfile": bool(report.get("strictParityProfile", false)),
		"strictParityErrors": _redact_list(report.get("strictParityErrors", []) as Array),
		"refusedForeignPacks": _redact_list(report.get("refusedForeignPacks", []) as Array),
		"suppressedAmbientRoots": _redact_list(report.get("suppressedAmbientRoots", []) as Array),
		"loaderDiagnostics": _redact_list(report.get("diagnostics", []) as Array),
	}
	if enabled:
		_write_json("identity.json", document)
		_identity_captured = true
		event(LEVEL_INFO, "content.identity", {
			"source": source,
			"packId": document["activePackId"],
			"digest": document["activePackDigest"],
			"selectionSha256": document["activeSelectionSha256"],
			"mountedRoots": roots.size(),
		})
	return document


static func source_reason(source: String) -> String:
	match source:
		"external":
			return "OPENBFME_CONTENT named this content root."
		"workspace":
			return "No OPENBFME_CONTENT; the repository workspace/content-packs selection won."
		"durable":
			return (
				"FALLBACK: neither OPENBFME_CONTENT nor the workspace selection was usable, "
				+ "so the durable user:// pack cache was mounted. This pack may be STALE - "
				+ "it is the recorded cause of runs that served yesterday's content."
			)
		"external-invalid":
			return "OPENBFME_CONTENT was set but unusable; the loader mounted NOTHING rather than substitute."
		"":
			return "No selection was active; only ambient roots (if any) were mounted."
	return "Unrecognised content source."


static func _mount_reason(identity: Dictionary, source: String) -> String:
	if bool(identity.get("embeddedResource", false)):
		return "ambient: pack shipped inside the game resources (res://)"
	if bool(identity.get("ambientMod", false)):
		return "ambient: loose mod directory"
	if bool(identity.get("active", false)):
		return "selection activePack, resolved from the %s source" % (source if source != "" else "unknown")
	return "selection supplementalPack, resolved from the %s source" % (source if source != "" else "unknown")


static func file_sha256(path: String) -> String:
	## "" when the file is absent or unreadable. Never a fabricated digest: a bug
	## report saying the selection hashed to nothing is a fact; one saying it
	## hashed to a plausible-looking constant is a lie.
	if path == "" or not FileAccess.file_exists(path):
		return ""
	var digest := FileAccess.get_sha256(path)
	return digest if digest != null else ""


# ---------------------------------------------------------------------------
# Redaction
# ---------------------------------------------------------------------------

static func redact(text: String) -> String:
	## Home paths become tokens rather than disappearing. The importer's launcher
	## redactor replaces a whole path with <private-path>, which is right for a
	## line of untrusted subprocess output and wrong here: "which pack root did it
	## mount" IS the evidence, and a bug report that answers it with
	## <private-path> is worthless. Replacing the user's home prefix with
	## %USERPROFILE% removes the identifying part and keeps the shape.
	if text == "":
		return text
	var result := text
	# LONGEST PREFIX FIRST. LOCALAPPDATA lives under USERPROFILE, so redacting the
	# home directory first would leave every AppData path reading
	# "%USERPROFILE%/AppData/Local/..." and the more specific token would never
	# match anything.
	for entry in [
		["LOCALAPPDATA", "%LOCALAPPDATA%"],
		["APPDATA", "%APPDATA%"],
		["USERPROFILE", "%USERPROFILE%"],
		["HOME", "%HOME%"],
	]:
		var value := OS.get_environment(String(entry[0])).strip_edges()
		if value.length() < 4:
			continue
		result = _replace_path_prefix(result, value, String(entry[1]))
	# Any other machine's user directory (a path pasted from a teammate's log).
	result = _redact_foreign_user_dirs(result)
	return _redact_secret_assignments(result)


static func _replace_path_prefix(text: String, value: String, token: String) -> String:
	var forward := value.replace("\\", "/")
	var back := value.replace("/", "\\")
	var result := text.replacen(forward, token)
	if back != forward:
		result = result.replacen(back, token)
	return result


static func _redact_foreign_user_dirs(text: String) -> String:
	## C:\Users\somebody\... -> C:\Users\<user>\... (and the /Users/ form).
	var result := text
	for marker in ["/Users/", "\\Users\\", "/home/"]:
		var search := 0
		while true:
			var start: int = result.findn(marker, search)
			if start < 0:
				break
			var name_start: int = start + marker.length()
			var name_end: int = name_start
			while name_end < result.length() and not (result[name_end] in ["/", "\\", "\"", " ", "'"]):
				name_end += 1
			if name_end <= name_start:
				search = name_start
				continue
			result = result.substr(0, name_start) + "<user>" + result.substr(name_end)
			search = name_start + 6
	return result


static func _redact_secret_assignments(text: String) -> String:
	## key=value / "key": "value" where the key names a credential. Whole-value
	## replacement, because a partial reveal of a signing key is still a reveal.
	var result := text
	for fragment in SECRET_NAME_FRAGMENTS:
		var search := 0
		while true:
			var at := result.findn(fragment, search)
			if at < 0:
				break
			var cursor := at + fragment.length()
			# Skip the separator run between the name and its value.
			while cursor < result.length() and result[cursor] in ["\"", "'", " ", ":", "=", "\t"]:
				cursor += 1
			if cursor >= result.length() or cursor == at + fragment.length():
				search = at + fragment.length()
				continue
			var value_end := cursor
			while value_end < result.length() and not (result[value_end] in ["\"", "'", ",", " ", "\n", "}", ";"]):
				value_end += 1
			if value_end <= cursor:
				search = at + fragment.length()
				continue
			result = result.substr(0, cursor) + "<redacted>" + result.substr(value_end)
			search = cursor + 10
	return result


static func is_secret_name(name: String) -> bool:
	var folded := name.to_lower()
	for fragment in SECRET_NAME_FRAGMENTS:
		if folded.contains(fragment):
			return true
	return false


func _scrub_fields(fields: Dictionary) -> Dictionary:
	var scrubbed := {}
	for key in fields:
		var name := String(key)
		if is_secret_name(name):
			scrubbed[name] = "<redacted>"
			continue
		scrubbed[name] = _scrub_value(fields[key])
	return scrubbed


func _scrub_value(value: Variant) -> Variant:
	match typeof(value):
		TYPE_STRING, TYPE_STRING_NAME:
			return redact(String(value))
		TYPE_ARRAY:
			var out: Array = []
			for entry in (value as Array):
				out.append(_scrub_value(entry))
			return out
		TYPE_DICTIONARY:
			return _scrub_fields(value as Dictionary)
	return value


static func _redact_list(values: Array) -> Array:
	var out: Array = []
	for value in values:
		out.append(redact(String(value)))
	return out


# ---------------------------------------------------------------------------
# Documents
# ---------------------------------------------------------------------------

func _write_env_document() -> void:
	var variables := {}
	for name in ENV_ALLOW_LIST:
		var value := OS.get_environment(name)
		if value == "":
			continue
		variables[name] = "<redacted>" if is_secret_name(name) else redact(value)
	var document := {
		"schema": SCHEMA_ENV,
		"schemaVersion": SCHEMA_VERSION,
		"component": component,
		"capturedAt": _timestamp(),
		"pid": OS.get_process_id(),
		"os": {
			"name": OS.get_name(),
			"version": OS.get_version(),
			"distribution": OS.get_distribution_name(),
			"processorName": OS.get_processor_name(),
			"processorCount": OS.get_processor_count(),
			"locale": OS.get_locale(),
		},
		"engine": {
			"godot": Engine.get_version_info().get("string", ""),
			"debugBuild": OS.is_debug_build(),
			"projectVersion": String(ProjectSettings.get_setting("application/config/version", "")),
			"buildId": String(ProjectSettings.get_setting("application/config/build_id", "")),
			"featureTags": _feature_tags(),
		},
		"renderer": _renderer_info(),
		"environment": variables,
		"git": {"commit": _git_commit()},
	}
	_write_json("env.json", document)


func _feature_tags() -> Array:
	var tags: Array = []
	for tag in ["editor", "template_debug", "template_release", "windows", "linux", "macos", "movie"]:
		if OS.has_feature(tag):
			tags.append(tag)
	return tags


func _renderer_info() -> Dictionary:
	## Headless runs have no adapter and must SAY so rather than report a blank
	## GPU that reads like a driver fault.
	var display_name := DisplayServer.get_name()
	var info := {
		"displayServer": display_name,
		"headless": display_name == "headless",
		"renderingMethod": String(ProjectSettings.get_setting("rendering/renderer/rendering_method", "")),
	}
	if display_name == "headless":
		info["adapterName"] = "<none: headless display server>"
		return info
	info["adapterName"] = RenderingServer.get_video_adapter_name()
	info["adapterVendor"] = RenderingServer.get_video_adapter_vendor()
	info["adapterApiVersion"] = RenderingServer.get_video_adapter_api_version()
	info["screenSize"] = "%dx%d" % [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y]
	return info


func _git_commit() -> String:
	## Best effort, and honestly empty when it cannot be resolved. An exported
	## build has no .git, so this is a source-run convenience, not a promise.
	var repo := ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir()
	var head_path := repo.path_join(".git/HEAD")
	if not FileAccess.file_exists(head_path):
		return ""
	var head := FileAccess.get_file_as_string(head_path).strip_edges()
	if not head.begins_with("ref:"):
		return head
	var ref := head.substr(4).strip_edges()
	var ref_path := repo.path_join(".git").path_join(ref)
	if FileAccess.file_exists(ref_path):
		return FileAccess.get_file_as_string(ref_path).strip_edges()
	# Packed refs: a freshly cloned checkout keeps branch tips in one file.
	var packed := repo.path_join(".git/packed-refs")
	if FileAccess.file_exists(packed):
		for line in FileAccess.get_file_as_string(packed).split("\n"):
			var parts := String(line).strip_edges().split(" ")
			if parts.size() == 2 and String(parts[1]) == ref:
				return String(parts[0])
	return ""


func _write_error_document() -> void:
	var lines: Array[String] = []
	lines.append("Open BFME %s failure report" % component)
	lines.append("captured %s" % _timestamp())
	lines.append("run directory: %s" % redact(run_dir))
	lines.append("")
	lines.append("FAILURES (%d)" % _failures.size())
	for entry in _failures:
		lines.append("  - %s" % redact(entry))
	lines.append("")
	lines.append("LAST %d EVENTS" % _tail.size())
	for line in _tail:
		lines.append("  %s" % line)
	var file := FileAccess.open(run_dir.path_join("error.txt"), FileAccess.WRITE)
	if file == null:
		return
	file.store_string("\n".join(lines) + "\n")
	file.flush()


func _write_json(name: String, document: Dictionary) -> void:
	var file := FileAccess.open(run_dir.path_join(name), FileAccess.WRITE)
	if file == null:
		push_warning("[DiagLog] could not write %s" % name)
		return
	file.store_string(JSON.stringify(document, "  ", false) + "\n")
	file.flush()


static func _merge(base: Dictionary, extra: Dictionary) -> Dictionary:
	var out := base.duplicate()
	for key in extra:
		out[key] = extra[key]
	return out


# ---------------------------------------------------------------------------
# Static forwarders - the seam production code uses.
#
# Each one is a null check when no recorder is live, which is the default. They
# exist so a call site never has to know whether autoloads were instantiated
# (they are not, under `--script` test runners).
# ---------------------------------------------------------------------------

static func instance() -> Node:
	return _active


static func emit(level: String, name: String, fields: Dictionary = {}) -> void:
	if _active != null:
		_active.event(level, name, fields)


static func emit_failure(name: String, message: String, detail: String = "") -> void:
	if _active != null:
		_active.failure(name, message, detail)


static func emit_sampled(key: String, interval_ms: int, level: String, name: String, fields: Dictionary = {}) -> void:
	if _active != null:
		_active.sampled(key, interval_ms, level, name, fields)


static func emit_boot_phase(label: String, delta_ms: int, at_ms: int) -> void:
	if _active != null:
		_active.boot_phase(label, delta_ms, at_ms)


static func emit_content_identity(report: Dictionary, supplements: Array = []) -> void:
	if _active != null:
		_active.capture_content_identity(report, supplements)


static func is_recording() -> bool:
	return _active != null and (_active as Node).get("enabled") == true
