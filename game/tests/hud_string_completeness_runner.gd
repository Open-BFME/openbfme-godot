extends SceneTree
## HUD string-table completeness gate.
##
## Every CONTROLBAR: identifier a mounted pack references is a label or tooltip
## the control bar will try to draw. When the merged string table has no row for
## it the button renders its fallback and the player sees a blank or raw-key
## affordance. Nothing failed before this runner existed: ContentDB resolves
## missing ids to a fallback silently, so 772 of the 1080 CONTROLBAR ids the
## mounted packs reference were unresolved with every matrix green.
##
## The two populations are NOT the same problem and are gated separately:
##
##   retail-absent  - retail's own commandbutton.ini references an id that the
##                    retail .str file does not define (e.g.
##                    CONTROLBAR:ToolTipFaramirMount, introduced by the RotWK
##                    patch at data/ini/commandbutton.ini:3281 and never added
##                    to data/lotr.str). Unfixable by us. NOT pinned by name
##                    here any more: the set is DERIVED from the compiler's own
##                    recorded `sourceNullStringIds` evidence in the mounted
##                    packs, so the runner and the importer can never disagree
##                    about which ids retail is missing.
##   recoverable    - the id IS in retail data/lotr.str, but no mounted pack
##                    ships a strings document containing it. The RotWK faction
##                    slice profile emits no `files.strings` at all
##                    (importer/openbfme_importer/faction_slice_profile.py has
##                    no strings lane; the precedent it needs is
##                    importer/openbfme_importer/faction_profile.py:777). Fixing
##                    that and republishing the seven faction packs drives this
##                    count to zero.
##
## Env: OPENBFME_CONTENT, OPENBFME_HUD_STRINGS_OUT

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

## Recorded 2026-08-04 against selection.json with the seven rebuilt RotWK
## faction packs mounted, using the DERIVED retail-absent set (see below).
## This is a RATCHET, not an equality pin:
##   count > pin  -> regression: HUD text that used to resolve no longer does.
##   count < pin  -> improvement, but the pin must be lowered in the SAME
##                   change so the new floor is what future runs are held to.
const EXPECTED_RECOVERABLE_UNRESOLVED := 764

## TIER POLICY (must match importer/openbfme_importer/playable_unit_import.py):
## an id counts as "retail-absent" when the LAYERED effective string table has
## no row for it -- NOT only when it is missing from every mounted tier.
## Rationale: retail's own file-level layering replaces BFME2's data/lotr.str
## wholesale with RotWK's -- the layered install's data/lotr.str is byte-for-byte
## the RotWK one (12632 rows) and NOT a merge of the BFME2 one (10899 rows). A
## RotWK player therefore sees a blank button for an id that only the BFME2
## table defines, so RotWK-tier absence is the honest verdict and is also the
## only tier the compiler actually reads. The stricter "missing from every
## mounted tier" reading would make the verdict depend on which legacy packs
## happen to be mounted, which is not a property of retail at all.
##
## Worked example: CONTROLBAR:CaptainofGondorBoromir has a row in the BFME2
## data/lotr.str and none in the RotWK/layered one, so it IS retail-absent here
## and the compiler records it as such. It nonetheless RESOLVES at runtime
## today, because the legacy bfme2-men-vslice pack is the only mounted pack
## that ships a strings document (data/strings.json) and that document is
## BFME2-tier. That is not a contradiction -- see the check below.
##
## The set is derived from `sourceNullStringIds`, which the pack compiler
## writes into each unit/spellbook runtime document from evidence gathered
## against the layered table. Deriving instead of pinning removes the class of
## bug where the runner's hand-maintained list and the compiler's evidence
## drift apart: at the time this was written they disagreed on 40 ids.
##
## KNOWN GAP, and why the baseline moved 725 -> 764 when the hand-written pin
## was retired: exactly 39 of the 51 formerly pinned ids are genuinely absent
## from the retail table but are NOT covered by compiler evidence, because no
## mounted pack owns the unit or spell that references them. They therefore sit
## in the "recoverable" bucket today and inflate it by 39. When a rebuilt pack
## set widens the evidence the count DROPS -- which the ratchet reports as the
## conscious-update branch, not as a silent pass.

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0
var _report: Dictionary = {"summary": {}, "checks": {}}


func _initialize() -> void:
	_runner_watchdog.start(self, "HUD_STRINGS_RUNNER", 600_000)
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("HUD_STRINGS FAIL no ContentDB")
		_runner_watchdog.stop()
		quit(1)
		return
	await process_frame
	await process_frame

	var table: Dictionary = (content_db.get("retail_strings") as Dictionary)
	var pack_roots: Array = (content_db.get("pack_roots") as Array)
	_check("string_table_is_mounted", not table.is_empty(), "retail_strings is empty")
	_check("pack_roots_are_mounted", not pack_roots.is_empty())

	var references: Dictionary = {}
	var retail_absent_evidence: Dictionary = {}
	for root_value in pack_roots:
		_collect_references(String(root_value), references, retail_absent_evidence)

	var retail_absent_folded: Dictionary = {}
	var retail_absent_ids: Array[String] = []
	for evidence_value in retail_absent_evidence.keys():
		retail_absent_folded[String(evidence_value).to_lower()] = true
		retail_absent_ids.append(String(evidence_value))
	retail_absent_ids.sort()

	# An empty evidence set would silently reclassify every retail-absent id as
	# recoverable and make the ratchet meaningless, so it is its own failure.
	_check(
		"retail_absent_evidence_is_recorded", not retail_absent_ids.is_empty(),
		"no mounted pack records sourceNullStringIds; the packs predate the "
		+ "retail-null evidence contract and must be rebuilt before this gate means anything"
	)

	var unresolved_recoverable: Array[String] = []
	var unresolved_retail_absent: Array[String] = []
	var resolved := 0
	for identifier_value in references.keys():
		var identifier := String(identifier_value)
		if table.has(identifier.to_lower()):
			resolved += 1
			continue
		if retail_absent_folded.has(identifier.to_lower()):
			unresolved_retail_absent.append(identifier)
		else:
			unresolved_recoverable.append(identifier)
	unresolved_recoverable.sort()
	unresolved_retail_absent.sort()

	# Evidence that names an id no mounted pack actually references is drift: the
	# compiler exempted something this pack set never draws. That IS a failure.
	var dangling_evidence: Array[String] = []
	var folded_references: Dictionary = {}
	for identifier_value in references.keys():
		folded_references[String(identifier_value).to_lower()] = true
	for identifier in retail_absent_ids:
		if not folded_references.has(identifier.to_lower()):
			dangling_evidence.append(identifier)
	_check(
		"retail_absent_evidence_ids_are_referenced", dangling_evidence.is_empty(),
		"these compiler-recorded retail-absent ids are referenced by no mounted "
		+ "pack; the evidence has drifted from the shipped documents: %s"
		% ", ".join(dangling_evidence)
	)

	# Under the tier policy above an evidence id MAY still resolve at runtime,
	# because a mounted BFME2-tier pack can ship text the RotWK table lacks
	# (CONTROLBAR:CaptainofGondorBoromir does exactly this today). That is a
	# bonus, not a contradiction: the compiler's verdict is about the RotWK
	# table it compiled against, and sourceNullStringIds only tells the runtime
	# "this pack is not broken" -- it never suppresses a row that does exist.
	# Reported, deliberately not gated.
	var evidence_resolved_by_other_tier: Array[String] = []
	for identifier in retail_absent_ids:
		if table.has(identifier.to_lower()):
			evidence_resolved_by_other_tier.append(identifier)

	# Any unresolved id NOT backed by retail-absent evidence is a recoverable id:
	# retail has the text and no mounted pack ships it. RATCHET, not equality --
	# the name says "within baseline" because that is exactly what it asserts.
	var recoverable := unresolved_recoverable.size()
	var ratchet_detail := ""
	if recoverable > EXPECTED_RECOVERABLE_UNRESOLVED:
		ratchet_detail = (
			"REGRESSION: recoverable unresolved=%d EXCEEDS baseline=%d. HUD text that "
			+ "used to resolve no longer does. Do NOT raise the pin to make this pass. "
			+ "First 40 missing: %s"
		) % [
			recoverable,
			EXPECTED_RECOVERABLE_UNRESOLVED,
			", ".join(unresolved_recoverable.slice(0, 40)),
		]
	elif recoverable < EXPECTED_RECOVERABLE_UNRESOLVED:
		ratchet_detail = (
			"CONSCIOUS UPDATE REQUIRED: recoverable unresolved=%d is BELOW baseline=%d. "
			+ "This is an improvement and the gate is deliberately failing so the new "
			+ "floor is locked in by the same change. Edit "
			+ "game/tests/hud_string_completeness_runner.gd and set "
			+ "EXPECTED_RECOVERABLE_UNRESOLVED := %d"
		) % [recoverable, EXPECTED_RECOVERABLE_UNRESOLVED, recoverable]
	_check(
		"recoverable_unresolved_within_pinned_baseline",
		recoverable == EXPECTED_RECOVERABLE_UNRESOLVED,
		ratchet_detail
	)

	_report["summary"] = {
		"referenced": references.size(),
		"resolved": resolved,
		"unresolved_recoverable": recoverable,
		"unresolved_retail_absent": unresolved_retail_absent.size(),
		"retail_absent_evidence_ids": retail_absent_ids.size(),
		"retail_absent_evidence_resolved_by_other_tier":
			evidence_resolved_by_other_tier.size(),
		"pinned_recoverable_baseline": EXPECTED_RECOVERABLE_UNRESOLVED,
		"passed": _passed,
		"failed": _failed,
	}
	_report["unresolved_recoverable"] = unresolved_recoverable
	_report["unresolved_retail_absent"] = unresolved_retail_absent
	_report["retail_absent_evidence"] = retail_absent_ids
	_report["retail_absent_evidence_resolved_by_other_tier"] = evidence_resolved_by_other_tier
	var out_path := OS.get_environment("OPENBFME_HUD_STRINGS_OUT").strip_edges()
	if out_path != "":
		var file := FileAccess.open(out_path, FileAccess.WRITE)
		if file != null:
			file.store_string(JSON.stringify(_report, "\t"))
			file.close()
	print("HUD_STRINGS_RESULT passed=%d failed=%d referenced=%d resolved=%d recoverable_unresolved=%d retail_absent=%d" % [
		_passed, _failed, references.size(), resolved,
		unresolved_recoverable.size(), unresolved_retail_absent.size(),
	])
	_runner_watchdog.stop()
	quit(0 if _failed == 0 else 1)


func _collect_references(
	pack_root: String, references: Dictionary, retail_absent: Dictionary
) -> void:
	## Only `data/` is scanned: that is where every command/label document lives.
	## Asset trees hold no localization ids and walking them would dominate cost.
	_collect_from_directory(pack_root.path_join("data"), references, retail_absent, 0)
	var pack_json := pack_root.path_join("pack.json")
	if FileAccess.file_exists(pack_json):
		_collect_from_text(
			FileAccess.get_file_as_string(pack_json), references, retail_absent
		)


func _collect_from_directory(
	path: String, references: Dictionary, retail_absent: Dictionary, depth: int
) -> void:
	if depth > 8 or not DirAccess.dir_exists_absolute(path):
		return
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var entry := directory.get_next()
	while entry != "":
		if entry != "." and entry != "..":
			var child := path.path_join(entry)
			if directory.current_is_dir():
				_collect_from_directory(child, references, retail_absent, depth + 1)
			elif entry.ends_with(".json"):
				_collect_from_text(
					FileAccess.get_file_as_string(child), references, retail_absent
				)
		entry = directory.get_next()
	directory.list_dir_end()


func _collect_from_text(
	text: String, references: Dictionary, retail_absent: Dictionary
) -> void:
	if text == "":
		return
	if text.contains("CONTROLBAR:"):
		for match_result in _reference_regex().search_all(text):
			references[match_result.get_string(1)] = true
	if text.contains("sourceNullStringIds"):
		# The compiler's own evidence: ids it proved the layered retail string
		# table has no row for. Arrays hold plain strings only, so bounding the
		# match at the closing bracket cannot swallow neighbouring documents.
		for block in _source_null_block_regex().search_all(text):
			for item in _quoted_item_regex().search_all(block.get_string(1)):
				retail_absent[item.get_string(1)] = true


var _cached_regex: RegEx = null
var _cached_source_null_regex: RegEx = null
var _cached_quoted_regex: RegEx = null


func _reference_regex() -> RegEx:
	if _cached_regex == null:
		_cached_regex = RegEx.new()
		# Ids are JSON string values; the closing quote bounds the id and keeps
		# a run-on match from swallowing the rest of the document.
		_cached_regex.compile("\"(CONTROLBAR:[^\"]+)\"")
	return _cached_regex


func _source_null_block_regex() -> RegEx:
	if _cached_source_null_regex == null:
		_cached_source_null_regex = RegEx.new()
		_cached_source_null_regex.compile("\"sourceNullStringIds\"\\s*:\\s*\\[([^\\]]*)\\]")
	return _cached_source_null_regex


func _quoted_item_regex() -> RegEx:
	if _cached_quoted_regex == null:
		_cached_quoted_regex = RegEx.new()
		_cached_quoted_regex.compile("\"([^\"]+)\"")
	return _cached_quoted_regex


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("HUD_STRINGS PASS %s" % name)
	else:
		_failed += 1
		print("HUD_STRINGS FAIL %s | %s" % [name, detail])
	_report["checks"][name] = {"ok": condition, "detail": detail}
