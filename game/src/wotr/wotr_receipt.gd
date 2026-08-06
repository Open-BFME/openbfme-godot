extends RefCounted

## Frozen, byte-level evidence for the inputs that open a War of the Ring session.
## This deliberately does not attest the whole contents of a pack tree.  It binds
## the selection, every mounted pack manifest, the living-world document and the
## named configuration files that the caller says are present.
const SCHEMA := "openbfme.wotr-input-receipt"
const SCHEMA_VERSION := 1
const CONTRACT_VERSION := "1.1"
const DOCUMENT_MAX_BYTES := 32 * 1024 * 1024
const PACK_TREE_CLAIM := "whole pack tree bytes are explicitly out of scope"
const REQUIRED_CONFIGS := ["autoresolve", "autoresolve_bindings", "ai_template", "building_catalogue"]


static func mint(
		document_path: String, document_source: String, document: Dictionary,
		campaign: String, scenario: String, seats: Array, rules: Dictionary,
		start_regions: PackedStringArray, faction_bindings: Dictionary,
		identity: Dictionary) -> Dictionary:
	if document_path.is_empty() or not FileAccess.file_exists(document_path):
		return {}
	if document_source != "pack" and document_source != "env":
		return {}
	for field in ["active_content_source", "selection", "pack_meta", "config_bundles"]:
		if not identity.has(field):
			return {}
	if typeof(identity["active_content_source"]) != TYPE_STRING 			or typeof(identity["selection"]) != TYPE_DICTIONARY 			or typeof(identity["pack_meta"]) != TYPE_ARRAY 			or typeof(identity["config_bundles"]) != TYPE_DICTIONARY:
		return {}
	var active_source := String(identity["active_content_source"]).strip_edges()
	var meta_rows := identity["pack_meta"] as Array
	if document_source == "pack" and (active_source.is_empty() or meta_rows.is_empty()):
		return {}
	var document_identity := _file_identity(document_path, DOCUMENT_MAX_BYTES)
	if document_identity.is_empty():
		return {}
	# The parsed value handed to begin must be the value in the exact bytes.
	var parsed: Variant = JSON.parse_string(_read_text(document_path))
	if not (parsed is Dictionary) or _canonical(parsed) != _canonical(document):
		return {}
	var selection := _selection_identity(identity["selection"] as Dictionary)
	if selection.is_empty():
		return {}
	var packs := _pack_identities(meta_rows)
	if packs.is_empty() and not meta_rows.is_empty():
		return {}
	var configs := _config_identities(identity["config_bundles"] as Dictionary)
	if configs.is_empty():
		return {}
	var requested := {
		"campaign": campaign, "scenario": scenario,
		"seats": _plain(seats), "rules": _plain(rules),
		"startRegions": _plain(start_regions),
	}
	var receipt := {
		"schema": SCHEMA, "schemaVersion": SCHEMA_VERSION,
		"contractVersion": CONTRACT_VERSION,
		"document": document_identity.merged({
			"source": document_source,
			"explicitDocumentOnly": document_source == "env",
			"canonicalSha256": _sha_text(_canonical(document)),
		}),
		"content": {
			"activeContentSource": active_source,
			"selection": selection, "packs": packs,
		},
		"configBundles": configs,
		"factionBindings": {
			"canonical": _plain(faction_bindings),
			"sha256": _sha_text(_canonical(faction_bindings)),
		},
		"setupInputs": requested,
		# At this seam there has been no stateful normalization yet.  Recording the
		# effective input separately makes that fact explicit and hashable.
		"effectiveInputs": requested.duplicate(true),
		"scope": {"packTreeBytes": "outOfScope", "claim": PACK_TREE_CLAIM},
	}
	receipt["inputDigest"] = _sha_text(_canonical(_input_surface(receipt)))
	receipt["receiptSha256"] = _sha_text(_canonical(receipt))
	return immutable_copy(receipt)


static func immutable_copy(receipt: Dictionary) -> Dictionary:
	var copied := receipt.duplicate(true)
	_freeze(copied)
	return copied


static func build(
		document_path: String, document_source: String, document: Dictionary,
		campaign: String, scenario: String, seats: Array, rules: Dictionary,
		start_regions: PackedStringArray, faction_bindings: Dictionary,
		identity: Dictionary) -> Dictionary:
	return mint(document_path, document_source, document, campaign, scenario, seats,
		rules, start_regions, faction_bindings, identity)


static func verify(receipt: Dictionary, reread_files: bool = true) -> bool:
	if typeof(receipt.get("schema", null)) != TYPE_STRING 			or typeof(receipt.get("schemaVersion", null)) != TYPE_INT 			or typeof(receipt.get("contractVersion", null)) != TYPE_STRING 			or typeof(receipt.get("receiptSha256", null)) != TYPE_STRING 			or typeof(receipt.get("inputDigest", null)) != TYPE_STRING:
		return false
	if receipt["schema"] != SCHEMA or receipt["schemaVersion"] != SCHEMA_VERSION 			or receipt["contractVersion"] != CONTRACT_VERSION:
		return false
	if not _is_sha256(receipt["receiptSha256"]) or not _is_sha256(receipt["inputDigest"]) 			or receipt.has("treeSha256"):
		return false
	var unsigned := receipt.duplicate(true)
	var recorded := String(unsigned.get("receiptSha256", ""))
	unsigned.erase("receiptSha256")
	if recorded.is_empty() or recorded != _sha_text(_canonical(unsigned)):
		return false
	for field in ["scope", "document", "content", "configBundles", "factionBindings", "setupInputs", "effectiveInputs"]:
		if typeof(receipt.get(field, null)) != TYPE_DICTIONARY:
			return false
	if receipt["inputDigest"] != _sha_text(_canonical(_input_surface(receipt))):
		return false
	var scope := receipt["scope"] as Dictionary
	if typeof(scope.get("claim", null)) != TYPE_STRING:
		return false
	if scope["claim"] != PACK_TREE_CLAIM or scope.has("treeSha256"):
		return false
	var document := receipt["document"] as Dictionary
	if not _valid_file_identity(document, reread_files, DOCUMENT_MAX_BYTES):
		return false
	if typeof(document.get("source", null)) != TYPE_STRING 			or typeof(document.get("explicitDocumentOnly", null)) != TYPE_BOOL:
		return false
	if document["explicitDocumentOnly"] != (document["source"] == "env"):
		return false
	var content := receipt["content"] as Dictionary
	if typeof(content.get("activeContentSource", null)) != TYPE_STRING 			or typeof(content.get("selection", null)) != TYPE_DICTIONARY 			or typeof(content.get("packs", null)) != TYPE_ARRAY:
		return false
	var packs := content["packs"] as Array
	if String(document.get("source", "")) == "pack" 			and (String(content["activeContentSource"]).strip_edges().is_empty() or packs.is_empty()):
		return false
	var selection := content["selection"] as Dictionary
	if typeof(selection.get("kind", null)) != TYPE_STRING: return false
	var kind := selection["kind"] as String
	if kind == "selection.json":
		if not _valid_file_identity(selection, reread_files): return false
	elif kind == "immutableBundleRoot":
		if typeof(selection.get("root", null)) != TYPE_STRING 				or typeof(selection.get("explicit", null)) != TYPE_BOOL: return false
		if (selection["root"] as String).is_empty() or not selection["explicit"]: return false
	else:
		return false
	var last_key := ""
	for pack_value in packs:
		if typeof(pack_value) != TYPE_DICTIONARY: return false
		var pack := pack_value as Dictionary
		if typeof(pack.get("manifest", null)) != TYPE_DICTIONARY 				or typeof(pack.get("root", null)) != TYPE_STRING 				or typeof(pack.get("id", null)) != TYPE_STRING: return false
		var key := (pack["root"] as String) + "\n" + (pack["id"] as String)
		if key <= last_key or not _valid_file_identity(pack["manifest"] as Dictionary, reread_files): return false
		last_key = key
	var configs := receipt["configBundles"] as Dictionary
	for name in REQUIRED_CONFIGS:
		if not configs.has(name) or typeof(configs[name]) != TYPE_DICTIONARY: return false
		var config := configs[name] as Dictionary
		if typeof(config.get("status", null)) != TYPE_STRING: return false
		if config["status"] == "present":
			if not _valid_file_identity(config, reread_files): return false
		elif config["status"] == "absent":
			if typeof(config.get("reason", null)) != TYPE_STRING 					or (config["reason"] as String).is_empty(): return false
		else: return false
	var bindings := receipt["factionBindings"] as Dictionary
	if typeof(bindings.get("canonical", null)) != TYPE_DICTIONARY 			or typeof(bindings.get("sha256", null)) != TYPE_STRING: return false
	if not _is_sha256(bindings["sha256"]) 			or bindings["sha256"] != _sha_text(_canonical(bindings["canonical"])): return false
	return true

static func _input_surface(receipt: Dictionary) -> Dictionary:
	return {
		"document": receipt.get("document", null),
		"content": receipt.get("content", null),
		"configBundles": receipt.get("configBundles", null),
		"factionBindings": receipt.get("factionBindings", null),
		"setupInputs": receipt.get("setupInputs", null),
		"effectiveInputs": receipt.get("effectiveInputs", null),
		"scope": receipt.get("scope", null),
	}


static func _is_sha256(value: Variant) -> bool:
	if typeof(value) != TYPE_STRING or (value as String).length() != 64:
		return false
	var allowed := "0123456789abcdef"
	for index in range(64):
		if allowed.find((value as String).substr(index, 1)) < 0:
			return false
	return true


static func _selection_identity(value: Dictionary) -> Dictionary:
	var kind := String(value.get("kind", ""))
	if kind == "selection.json":
		return _file_identity(String(value.get("path", ""))).merged({"kind": kind})
	if kind == "immutableBundleRoot":
		var root := String(value.get("root", "")).strip_edges()
		if root.is_empty(): return {}
		return {"kind": kind, "root": root, "explicit": true}
	return {}


static func _pack_identities(meta_rows: Array) -> Array:
	var rows: Array = []
	var seen: Dictionary = {}
	for value in meta_rows:
		if not (value is Dictionary): return []
		var meta := value as Dictionary
		var root := String(meta.get("root", "")).strip_edges()
		var pack_id := String(meta.get("id", "")).strip_edges()
		if root.is_empty() or pack_id.is_empty(): return []
		var manifest := _file_identity(root.path_join("pack.json"))
		if manifest.is_empty(): return []
		var raw: Variant = JSON.parse_string(_read_text(root.path_join("pack.json")))
		if not (raw is Dictionary) or String((raw as Dictionary).get("id", "")) != pack_id: return []
		var key := root + "\n" + pack_id
		if seen.has(key): return []
		seen[key] = true
		rows.append({"root": root, "id": pack_id, "manifest": manifest})
	rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return String(a["root"]) + "\n" + String(a["id"]) < String(b["root"]) + "\n" + String(b["id"]))
	return rows


static func _config_identities(values: Dictionary) -> Dictionary:
	var result := {}
	for name in REQUIRED_CONFIGS:
		if not values.has(name) or not (values[name] is Dictionary): return {}
		var supplied := values[name] as Dictionary
		var status := String(supplied.get("status", ""))
		if status == "present":
			var file := _file_identity(String(supplied.get("path", "")))
			if file.is_empty(): return {}
			file["status"] = "present"
			result[name] = file
		elif status == "absent":
			var reason := String(supplied.get("reason", "")).strip_edges()
			if reason.is_empty(): return {}
			result[name] = {"status": "absent", "reason": reason}
		else: return {}
	return result


static func _file_identity(path: String, maximum_bytes: int = -1) -> Dictionary:
	if path.is_empty() or not FileAccess.file_exists(path): return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null: return {}
	var size := file.get_length()
	if size < 0 or (maximum_bytes >= 0 and size > maximum_bytes):
		file.close()
		return {}
	var bytes := file.get_buffer(size)
	file.close()
	if bytes.size() != size: return {}
	var hashing := HashingContext.new()
	if hashing.start(HashingContext.HASH_SHA256) != OK: return {}
	if hashing.update(bytes) != OK: return {}
	return {"path": path, "size": size, "sha256": hashing.finish().hex_encode()}


static func _valid_file_identity(identity: Dictionary, reread: bool, maximum_bytes: int = -1) -> bool:
	if typeof(identity.get("path", null)) != TYPE_STRING 			or typeof(identity.get("size", null)) != TYPE_INT 			or typeof(identity.get("sha256", null)) != TYPE_STRING:
		return false
	if (identity["path"] as String).is_empty() or identity["size"] < 0 			or not _is_sha256(identity["sha256"]):
		return false
	if not reread: return true
	return _file_identity(identity["path"], maximum_bytes) == {
		"path": identity["path"], "size": identity["size"], "sha256": identity["sha256"]}


static func _read_text(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null: return ""
	var text := file.get_as_text()
	file.close()
	return text


static func _sha_text(value: String) -> String:
	return value.sha256_text()


static func _canonical(value: Variant) -> String:
	return JSON.stringify(_plain(value), "", true)


static func _freeze(value: Variant) -> void:
	if value is Dictionary:
		for child in (value as Dictionary).values(): _freeze(child)
		(value as Dictionary).make_read_only()
	elif value is Array:
		for child in value as Array: _freeze(child)
		(value as Array).make_read_only()


static func _plain(value: Variant) -> Variant:
	if value is Dictionary:
		var source := value as Dictionary
		var keys: Array[String] = []
		for key in source.keys(): keys.append(String(key))
		keys.sort()
		var result := {}
		for key in keys: result[key] = _plain(source[key])
		return result
	if value is Array or value is PackedStringArray or value is PackedInt32Array or value is PackedFloat32Array:
		var result: Array = []
		for item in value: result.append(_plain(item))
		return result
	return value
