extends SceneTree
## HUD authored text-label gate (Q90 strings sub-lane).
##
## Oracle: the mounted packs' own authored `data/strings.json` documents
## (`openbfme.localized-strings`), NEVER this codebase. Code under test:
##   1. ContentDB's string-table merge — every authored row a mounted pack
##      ships must resolve through `get_retail_string` to an authored value
##      (some pack's text for that id; packs merge last-wins).
##   2. RetailHud.lookup_authored_string — the HUD-side lookup that records a
##      NAMED receipt (`missing_string_receipts`) on every miss instead of
##      silently substituting text, and records each missing id only once.
##
## Context: the "~760 recoverable HUD text labels" parity-ledger gap was a
## string-TABLE gap, not a render-path gap. The importer's pack strings lane
## shipped in the 2026-08 recooks (eight mounted packs now carry
## data/strings.json); this runner proves the shipped rows actually resolve at
## runtime and prints the real counts. The 25-id residue is ratcheted by
## hud_string_completeness_runner.gd, not here.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
## Loaded at runtime, not preloaded: retail_hud.gd references the ModLoader
## autoload at compile time, and compiling it from a --script boot before the
## autoloads register fails ("Identifier not found: ModLoader").
const RETAIL_HUD_SCRIPT_PATH := "res://src/retail_slice/retail_hud.gd"

## Ids that must be present in some mounted authored strings document AND
## resolve to that document's text. These are load-bearing HUD labels (Men
## build palette + shared commands); if a recook drops them the check is a
## loud failure, never a silent skip.
const KNOWN_LABEL_IDS: Array[String] = [
	"CONTROLBAR:ConstructMenFarm",
	"CONTROLBAR:ConstructMenBarracks",
	"CONTROLBAR:AttackMove",
	"CONTROLBAR:Stop",
	"CONTROLBAR:ToggleGateOpenClose",
]

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "HUD_TEXT_LABELS", 600_000)
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("HUD_TEXT_LABELS FAIL no ContentDB")
		_runner_watchdog.stop()
		quit(1)
		return
	await process_frame
	await process_frame

	var table: Dictionary = (content_db.get("retail_strings") as Dictionary)
	var pack_roots: Array = (content_db.get("pack_roots") as Array)
	_check("string_table_is_mounted", not table.is_empty(), "retail_strings is empty")
	_check("pack_roots_are_mounted", not pack_roots.is_empty())

	# --- 1. Merge fidelity against the authored documents (external oracle) ---
	# authored[id_lower] = { authored text variants across packs (last-wins merge
	# means the table must hold ONE of them, and for single-pack ids exactly it) }
	var authored: Dictionary = {}
	var strings_docs := 0
	for root_value in pack_roots:
		var doc_path := String(root_value).path_join("data").path_join("strings.json")
		if not FileAccess.file_exists(doc_path):
			continue
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(doc_path))
		if typeof(parsed) != TYPE_DICTIONARY:
			continue
		var document := parsed as Dictionary
		if String(document.get("schema", "")) != "openbfme.localized-strings" \
				or int(document.get("schemaVersion", -1)) != 0:
			continue
		var rows: Variant = document.get("strings", {})
		if typeof(rows) != TYPE_DICTIONARY:
			continue
		strings_docs += 1
		for key_value in (rows as Dictionary).keys():
			var id := String(key_value).to_lower()
			var text_value: Variant = (rows as Dictionary).get(key_value)
			if id == "" or typeof(text_value) != TYPE_STRING:
				continue
			if not authored.has(id):
				authored[id] = []
			(authored[id] as Array).append(String(text_value))
	_check(
		"authored_strings_documents_are_mounted", strings_docs >= 8,
		"expected the eight recooked strings documents, found %d" % strings_docs
	)

	var authored_total := authored.size()
	var authored_controlbar := 0
	var resolved_controlbar := 0
	var unresolved_authored: Array[String] = []
	var text_mismatches: Array[String] = []
	for id_value in authored.keys():
		var id := String(id_value)
		var is_controlbar := id.begins_with("controlbar:")
		if is_controlbar:
			authored_controlbar += 1
		if not table.has(id):
			unresolved_authored.append(id)
			continue
		if not (authored[id] as Array).has(String(table[id])):
			text_mismatches.append(id)
			continue
		if is_controlbar:
			resolved_controlbar += 1
	unresolved_authored.sort()
	text_mismatches.sort()
	_check(
		"every_authored_row_resolves_in_the_merged_table",
		unresolved_authored.is_empty(),
		"%d authored ids missing from retail_strings; first 20: %s"
		% [unresolved_authored.size(), ", ".join(unresolved_authored.slice(0, 20))]
	)
	_check(
		"merged_table_text_matches_an_authored_value",
		text_mismatches.is_empty(),
		"%d ids resolve to text no mounted document authored; first 20: %s"
		% [text_mismatches.size(), ", ".join(text_mismatches.slice(0, 20))]
	)
	_check(
		"controlbar_label_population_is_real",
		resolved_controlbar == authored_controlbar and authored_controlbar > 0,
		"authored CONTROLBAR ids=%d resolved=%d" % [authored_controlbar, resolved_controlbar]
	)
	print(
		"HUD_TEXT_LABELS_COUNTS authored_ids=%d authored_controlbar=%d resolved_controlbar=%d strings_docs=%d"
		% [authored_total, authored_controlbar, resolved_controlbar, strings_docs]
	)

	# --- 2. Specific known labels resolve to their authored text -------------
	for known_id in KNOWN_LABEL_IDS:
		var folded := known_id.to_lower()
		var is_authored: bool = authored.has(folded)
		_check(
			"known_label_is_authored:%s" % known_id, is_authored,
			"no mounted strings document authors this id"
		)
		if not is_authored:
			continue
		var resolved := String(content_db.get_retail_string(known_id, ""))
		_check(
			"known_label_resolves_to_authored_text:%s" % known_id,
			resolved != "" and (authored[folded] as Array).has(resolved),
			"table returned '%s', authored: %s" % [resolved, str(authored[folded])]
		)

	# --- 3. The HUD lookup records a named receipt on a miss, once -----------
	var retail_hud_script: GDScript = load(RETAIL_HUD_SCRIPT_PATH)
	if retail_hud_script == null or not retail_hud_script.can_instantiate():
		_check("retail_hud_script_loads", false, "cannot load/instantiate %s" % RETAIL_HUD_SCRIPT_PATH)
		print("HUD_TEXT_LABELS_RESULT passed=%d failed=%d" % [_passed, _failed])
		_runner_watchdog.stop()
		quit(1)
		return
	var hud = retail_hud_script.new()
	hud.set("_bound_content_db", content_db)
	var known := KNOWN_LABEL_IDS[0]
	var hit: Dictionary = hud.lookup_authored_string(known, "runner-hit")
	_check(
		"hud_lookup_finds_authored_label",
		bool(hit.get("found", false))
			and (authored[known.to_lower()] as Array).has(String(hit.get("text", ""))),
		"lookup returned %s" % str(hit)
	)
	var receipts_after_hit: Array = (hud.get("missing_string_receipts") as Array)
	_check("hit_records_no_receipt", receipts_after_hit.is_empty(), str(receipts_after_hit))

	var missing_id := "CONTROLBAR:OpenBfmeRunnerDefinitelyMissingLabel"
	var miss: Dictionary = hud.lookup_authored_string(missing_id, "runner-miss")
	var receipts: Array = (hud.get("missing_string_receipts") as Array)
	_check(
		"miss_returns_not_found_and_no_invented_text",
		not bool(miss.get("found", true)) and String(miss.get("text", "x")) == "",
		str(miss)
	)
	_check(
		"miss_records_named_receipt",
		receipts.size() == 1
			and String(receipts[0]).contains(missing_id)
			and String(receipts[0]).contains("runner-miss"),
		str(receipts)
	)
	hud.lookup_authored_string(missing_id, "runner-miss-again")
	var receipts_again: Array = (hud.get("missing_string_receipts") as Array)
	_check(
		"repeat_miss_is_reported_once", receipts_again.size() == 1, str(receipts_again)
	)
	hud.free()

	print("HUD_TEXT_LABELS_RESULT passed=%d failed=%d" % [_passed, _failed])
	_runner_watchdog.stop()
	quit(0 if _failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("HUD_TEXT_LABELS PASS %s" % name)
	else:
		_failed += 1
		print("HUD_TEXT_LABELS FAIL %s | %s" % [name, detail])
