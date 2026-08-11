extends RefCounted
## WHICH BUILD IS THIS. One answer, derived from the repository, read by the
## shell.
##
## The menu used to advertise `build dev` forever, because "dev" was the literal
## in `project.godot` and nothing ever replaced it. A playtester's "which build?"
## is unanswerable from a string that is the same in every build ever shipped, so
## the number comes from git instead:
##
##     tools/Write-BuildInfo.ps1  ->  game/data/build_info.json
##       {"build": "<git rev-list --count HEAD>", "commit": "<git rev-parse --short HEAD>"}
##
## THE FILE IS THE SOURCE AT RUNTIME, not git. An export ships no `.git`, so a
## shell that shelled out would have no answer in the only build that matters -
## and calling git on the boot path would put a process spawn in front of the
## menu. The generated file is committed and read; git is consulted ONLY when the
## file is absent AND a checkout is present, which is a developer who has not run
## the generator yet.
##
## EVERY PATH ENDS IN A STRING. Missing file, unreadable file, malformed JSON, no
## git: the caller gets `dev` back, never "" and never a crash.

const BUILD_INFO_PATH := "res://data/build_info.json"
const FALLBACK_BUILD_ID := "dev"
## Bounded because it is parsed before anything has validated it.
const MAX_BUILD_INFO_BYTES := 4096

static var _cached_build_id := ""


static func build_id(path: String = BUILD_INFO_PATH) -> String:
	## `412 (bfb414a)` when the repository answered, `dev` when nothing did.
	if path == BUILD_INFO_PATH and _cached_build_id != "":
		return _cached_build_id
	var id := _format(read_build_info(path))
	if id == "":
		id = _git_build_id()
	if id == "":
		id = FALLBACK_BUILD_ID
	if path == BUILD_INFO_PATH:
		_cached_build_id = id
	return id


static func product_version(path: String = BUILD_INFO_PATH) -> String:
	## `0.2.1` - the beta number this build was published under, or "" when this
	## checkout has not been stamped yet.
	##
	## Same contract as `build_id()`: the FILE is the source at runtime, because
	## an export ships neither `.git` nor the repository's `VERSION`. It is
	## written by `tools/Write-BuildInfo.ps1` from `VERSION`, which is the one
	## place the product version is decided; `tools/Publish-DistBuild.ps1`
	## refuses to publish a build whose copies of it disagree.
	##
	## Returns "" rather than a placeholder. A caller that wants a fallback must
	## choose one out loud - a version string invented here would be a silent
	## fallback, and this project has paid for those.
	var info := read_build_info(path)
	if info.is_empty():
		return ""
	return String(info.get("version", "")).strip_edges()


static func read_build_info(path: String = BUILD_INFO_PATH) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() > MAX_BUILD_INFO_BYTES:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	return parsed as Dictionary


static func _format(info: Dictionary) -> String:
	if info.is_empty():
		return ""
	var build := String(info.get("build", "")).strip_edges()
	var commit := String(info.get("commit", "")).strip_edges()
	if build == "" and commit == "":
		return ""
	if commit == "":
		return build
	if build == "":
		return commit
	return "%s (%s)" % [build, commit]


static func _git_build_id() -> String:
	## The developer-checkout path, and only that. Skipped outright in an export,
	## where `res://` is inside a .pck and there is no working tree to ask.
	var project_root := ProjectSettings.globalize_path("res://")
	if project_root == "" or not DirAccess.dir_exists_absolute(project_root):
		return ""
	var count := _git(project_root, ["rev-list", "--count", "HEAD"])
	var commit := _git(project_root, ["rev-parse", "--short", "HEAD"])
	return _format({"build": count, "commit": commit})


static func _git(working_directory: String, arguments: Array) -> String:
	var argv: PackedStringArray = PackedStringArray(["-C", working_directory])
	for argument in arguments:
		argv.append(String(argument))
	var output: Array = []
	if OS.execute("git", argv, output, false) != 0:
		return ""
	if output.is_empty():
		return ""
	return String(output[0]).strip_edges()
