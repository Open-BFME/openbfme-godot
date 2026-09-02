extends SceneTree
## Whole-corpus screen-v1 load/VM oracle. One process emits one result per
## indexed screen and persists identity-bound receipts beside the native index.

const AptVmScript := preload("res://src/apt/apt_vm.gd")
const AptRuntimeHostScript := preload("res://src/apt/apt_runtime_host.gd")

var _passed := 0
var _failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var args := _arguments()
	var index_path := String(args.get("index", ""))
	var content_root := String(args.get("content-root", ""))
	var only_id := String(args.get("id", ""))
	if index_path == "" or content_root == "":
		print("SCREEN_LOAD_ERROR missing --index or --content-root")
		quit(2)
		return
	var index := _read_json(index_path)
	if String(index.get("schema", "")) != "openbfme.native-screens-index":
		print("SCREEN_LOAD_ERROR invalid index")
		quit(2)
		return
	for value in index.get("screens", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var screen_id := String(row.get("id", ""))
		if only_id != "" and screen_id != only_id:
			continue
		_check_screen(row, content_root)
	print("SCREEN_LOAD_SUMMARY passed=%d failed=%d" % [_passed, _failed])
	quit(1 if _failed > 0 else 0)


func _check_screen(row: Dictionary, content_root: String) -> void:
	var screen_id := String(row.get("id", ""))
	var name := String(row.get("name", screen_id.get_file().get_basename()))
	var failure := ""
	var unimplemented: Dictionary = {}
	var document_path := content_root.path_join(String(row.get("document", "")))
	var document := _read_json(document_path)
	if String(row.get("status", "")) != "ok":
		failure = "conversion-failed"
	elif String(document.get("schema", "")) != "openbfme.screen.v1":
		failure = "screen-v1-refused"
	elif String(document.get("id", "")) != screen_id:
		failure = "screen-identity-mismatch"
	elif FileAccess.get_sha256(document_path) != String(row.get("documentSha256", "")):
		failure = "screen-document-digest-mismatch"
	else:
		failure = _verify_textures(document, content_root)
	if failure == "" and String(document.get("kind", "")) == "apt":
		var programs := {}
		for value in document.get("actionScripts", []) as Array:
			if typeof(value) == TYPE_DICTIONARY:
				programs[String((value as Dictionary).get("scriptId", ""))] = value
		var ids := _frame_one_program_ids(document)
		for program_id in ids:
			if not programs.has(program_id):
				failure = "frame-one-program-missing"
				break
			var program := programs[program_id] as Dictionary
			var raw_value: Variant = program.get("vmBytecode", {})
			if typeof(raw_value) != TYPE_DICTIONARY:
				failure = "vm-bytecode-missing"
				break
			var raw := raw_value as Dictionary
			var movie_key := String(program.get("movie", "")).to_lower()
			var constants_value: Variant = document.get("vmConstants", {}).get(movie_key, {})
			if typeof(constants_value) != TYPE_DICTIONARY:
				failure = "vm-constants-missing"
				break
			var host = AptRuntimeHostScript.new(0, "clip")
			_bind_document(host, document)
			var vm = AptVmScript.new()
			vm.host = host
			var result: Dictionary = vm.execute(
				_build_byte_space(raw),
				(constants_value as Dictionary).get("entries", []) as Array,
				int(raw.get("entryOffset", 0))
			)
			for opcode in result.get("unimplemented", {}) as Dictionary:
				unimplemented[opcode] = int(unimplemented.get(opcode, 0)) + int(result["unimplemented"][opcode])
			if not (result.get("unimplemented", {}) as Dictionary).is_empty():
				failure = "unsupported-opcode"
				break
			if not bool(result.get("completed", false)):
				failure = "vm-" + String(result.get("halted_reason", "refused"))
				print("SCREEN_LOAD_VM_FAILURE name=%s program=%s reason=%s entry=%d bytes=%d" % [
					name, program_id, String(result.get("halted_reason", "refused")),
					int(raw.get("entryOffset", 0)), int(raw.get("byteSpaceSize", 0))
				])
				break
	var ok := failure == ""
	var receipt := {
		"schema": "openbfme.screen-load-receipt",
		"schemaVersion": 1,
		"id": screen_id,
		"document": String(row.get("document", "")),
		"documentSha256": String(row.get("documentSha256", "")),
		"passed": ok,
		"failureClass": failure,
		"unimplemented": unimplemented,
		"opcodesUnimplemented": unimplemented.size(),
	}
	_write_json(content_root.path_join(String(row.get("receipt", ""))), receipt)
	if ok:
		_passed += 1
	else:
		_failed += 1
	print("SCREEN_LOAD_RESULT name=%s passed=%d failed=%d opcodes_unimplemented=%d" % [
		name, 1 if ok else 0, 0 if ok else 1, unimplemented.size()
	])
	if not ok:
		print("SCREEN_LOAD_FAILURE name=%s class=%s id=%s" % [name, failure, screen_id])


func _bind_document(host: RefCounted, document: Dictionary) -> void:
	for value in document.get("buttonInstances", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var name := String((value as Dictionary).get("buttonId", ""))
		if name != "":
			host.call("add_clip", "clip", name.replace(":", "_"), {})


func _frame_one_program_ids(document: Dictionary) -> Array[String]:
	var result: Array[String] = []
	for timeline_value in document.get("timelines", []) as Array:
		if typeof(timeline_value) != TYPE_DICTIONARY:
			continue
		for frame_value in (timeline_value as Dictionary).get("frames", []) as Array:
			if typeof(frame_value) != TYPE_DICTIONARY or int((frame_value as Dictionary).get("frameIndex", -1)) != 0:
				continue
			for action_value in (frame_value as Dictionary).get("actionScripts", []) as Array:
				if typeof(action_value) == TYPE_DICTIONARY:
					var script_id := String((action_value as Dictionary).get("scriptId", ""))
					if script_id != "" and script_id not in result:
						result.append(script_id)
	result.sort()
	return result


func _build_byte_space(raw: Dictionary) -> PackedByteArray:
	var space := PackedByteArray()
	for value in raw.get("segments", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var segment := value as Dictionary
		var offset := int(segment.get("offset", 0))
		if offset > space.size():
			space.resize(offset)
		space.append_array(Marshalls.base64_to_raw(String(segment.get("bytesBase64", ""))))
	if space.size() < int(raw.get("byteSpaceSize", 0)):
		space.resize(int(raw.get("byteSpaceSize", 0)))
	return space


func _verify_textures(document: Dictionary, content_root: String) -> String:
	for value in document.get("atlases", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return "missing-texture-reference"
		var atlas := value as Dictionary
		var path := content_root.path_join(String(atlas.get("cookedPng", "")))
		if not FileAccess.file_exists(path):
			return "missing-texture-reference"
		if FileAccess.get_sha256(path) != String(atlas.get("cookedPngSha256", "")):
			return "texture-digest-mismatch"
	return ""


func _arguments() -> Dictionary:
	var result := {}
	var args := OS.get_cmdline_user_args()
	var index := 0
	while index + 1 < args.size():
		if String(args[index]).begins_with("--"):
			result[String(args[index]).trim_prefix("--")] = String(args[index + 1])
			index += 2
		else:
			index += 1
	return result


func _read_json(path: String) -> Dictionary:
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}


func _write_json(path: String, document: Dictionary) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify(document, "", true) + "\n")
		file.close()
