extends SceneTree

const Receipt = preload("res://src/wotr/wotr_receipt.gd")
const Session = preload("res://src/wotr/wotr_session.gd")
const CHECKS := 24
var passed := 0
var failed := 0
var fixture_root := "user://wotr-receipt-fixture"

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root.path_join("pack")))
	_write("document.json", '{"schema":"fixture","value":1}')
	_write("selection.json", '{"activePack":"pack"}')
	_write("pack/pack.json", '{"id":"fixture-pack","priority":1}')
	var document := JSON.parse_string(_read("document.json")) as Dictionary
	var identity := _identity()
	var a := _mint(document, identity)
	var b := _mint(document, identity)
	_check("mints_immutable", not a.is_empty() and a.is_read_only())
	_check("deterministic", a == b)
	_check("verifies", Receipt.verify(a))
	_check("input_digest_is_64_hex", Receipt._is_sha256(a.get("inputDigest", "")))
	_check("count_and_no_tree_hash", (a["content"]["packs"] as Array).size() == 1 and not a.has("treeSha256") and not (a["scope"] as Dictionary).has("treeSha256"))
	_check("absent_configs_are_named", String(a["configBundles"]["ai_template"]["reason"]) != "")
	var ablated := identity.duplicate(true); ablated.erase("selection")
	_check("identity_ablation_refuses", _mint(document, ablated).is_empty())
	var changed_setup := Receipt.mint(fixture_root.path_join("document.json"), "env", document, "Campaign", "Other", [], {}, PackedStringArray(), {}, identity)
	_check("setup_sensitive", changed_setup.get("inputDigest", "") != a.get("inputDigest", ""))
	_write("selection.json", '{"activePack":"other"}')
	var changed_selection := _mint(document, identity)
	_check("selection_changes_input_digest", changed_selection.get("inputDigest", "") != a.get("inputDigest", ""))
	_check("selection_drift_refuses", not Receipt.verify(a))
	_write("selection.json", '{"activePack":"pack"}')
	_write("pack/pack.json", '{"id":"fixture-pack","priority":2}')
	var changed_pack := _mint(document, identity)
	_check("pack_changes_input_digest", changed_pack.get("inputDigest", "") != a.get("inputDigest", ""))
	_check("pack_drift_refuses", not Receipt.verify(a))
	_write("pack/pack.json", '{"id":"fixture-pack","priority":1}')
	_write("document.json", '{"schema":"fixture","value":2}')
	var changed_document_value := JSON.parse_string(_read("document.json")) as Dictionary
	var changed_document := _mint(changed_document_value, identity)
	_check("document_changes_input_digest", changed_document.get("inputDigest", "") != a.get("inputDigest", ""))
	_check("document_drift_refuses", not Receipt.verify(a))
	_write("document.json", '{"schema":"fixture","value":1}')
	_check("restored_bytes_verify", Receipt.verify(a))
	var env_route := a["document"] as Dictionary
	_check("env_route_is_explicit_only", bool(env_route["explicitDocumentOnly"]))
	var bad_selection := identity.duplicate(true); bad_selection["selection"] = 7
	_check("malformed_selection_refuses", _mint(document, bad_selection).is_empty())
	var bad_packs := identity.duplicate(true); bad_packs["pack_meta"] = "not an array"
	_check("malformed_pack_meta_refuses", _mint(document, bad_packs).is_empty())
	var bad_configs := identity.duplicate(true); bad_configs["config_bundles"] = []
	_check("malformed_configs_refuse", _mint(document, bad_configs).is_empty())
	var malformed_receipt := a.duplicate(true)
	malformed_receipt["content"] = 9
	var unsigned := malformed_receipt.duplicate(true); unsigned.erase("receiptSha256")
	malformed_receipt["receiptSha256"] = Receipt._sha_text(Receipt._canonical(unsigned))
	_check("malformed_nested_receipt_refuses", not Receipt.verify(malformed_receipt, false))
	var unmounted := identity.duplicate(true)
	unmounted["active_content_source"] = ""; unmounted["pack_meta"] = []
	_check("pack_document_requires_mounted_identity", Receipt.mint(
		fixture_root.path_join("document.json"), "pack", document, "Campaign", "Scenario",
		[], {}, PackedStringArray(), {}, unmounted).is_empty())
	var incomplete := Session.new()
	_check("incomplete_evidenced_begin_leaves_null", not incomplete.begin_evidenced(
		{}, "", "", [], {}, PackedStringArray(), {})
		and incomplete.world == null and incomplete.state == null)
	var reused := Session.new(); reused.input_receipt = a
	reused.begin({}, "", "", [])
	_check("low_level_begin_clears_stale_receipt", reused.input_receipt.is_empty())
	var receiver := Session.new(); receiver.world = Session.WorldScript.new(); receiver.state = Session.StateScript.new()
	var old_world = receiver.world; var old_state = receiver.state
	_check("missing_receipt_adoption_does_not_mutate", not receiver.adopt_evidenced_handoff({})
		and receiver.world == old_world and receiver.state == old_state)
	var ran := passed + failed
	if ran != CHECKS:
		failed += 1; printerr("WOTR_RECEIPT FAIL liveness ran %d expected %d" % [ran, CHECKS])
	print("WOTR_RECEIPT: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _identity() -> Dictionary:
	var absent := {"status":"absent", "reason":"fixture intentionally has no bundle"}
	return {
		"active_content_source":"fixture",
		"selection":{"kind":"selection.json", "path":fixture_root.path_join("selection.json")},
		"pack_meta":[{"root":fixture_root.path_join("pack"), "id":"fixture-pack"}],
		"config_bundles":{"autoresolve":absent.duplicate(), "autoresolve_bindings":absent.duplicate(), "ai_template":absent.duplicate(), "building_catalogue":absent.duplicate()},
	}

func _mint(document: Dictionary, identity: Dictionary) -> Dictionary:
	return Receipt.mint(fixture_root.path_join("document.json"), "env", document, "Campaign", "Scenario", [], {}, PackedStringArray(), {}, identity)

func _write(relative: String, text: String) -> void:
	var f := FileAccess.open(fixture_root.path_join(relative), FileAccess.WRITE); f.store_string(text); f.close()
func _read(relative: String) -> String:
	var f := FileAccess.open(fixture_root.path_join(relative), FileAccess.READ); var text := f.get_as_text(); f.close(); return text
func _check(name: String, condition: bool) -> void:
	if condition: passed += 1; print("WOTR_RECEIPT PASS %s" % name)
	else: failed += 1; printerr("WOTR_RECEIPT FAIL %s" % name)
