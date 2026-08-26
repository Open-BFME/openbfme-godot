extends SceneTree
## Q83 phase 2, seam gate: the selected pack's raw skirmish-AI document
## (openbfme.skirmish-ai) surfaces through ContentDB with its census honest
## and every ArmyDefinition carrying an authored Side. This proves the
## loader boundary only — interpretation stays in the sim's own lane.

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_check(false, "ContentDB autoload is present")
		_finish()
		return
	var document: Dictionary = content_db.skirmish_ai_runtime
	_check(not document.is_empty(), "selected pack carries the skirmish-AI document")
	if document.is_empty():
		_finish()
		return
	_check(String(document.get("schema", "")) == "openbfme.skirmish-ai", "schema is openbfme.skirmish-ai")
	var census := document.get("census", {}) as Dictionary
	var armies := document.get("armies", {}) as Dictionary
	var chains := document.get("combatChains", []) as Array
	var bases := document.get("aiBases", []) as Array
	_check(int(census.get("armyDefinitionCount", -1)) == armies.size(), "census armyDefinitionCount matches the army table")
	_check(int(census.get("combatChainDefinitionCount", -1)) == chains.size(), "census combatChainDefinitionCount matches the chain table")
	_check(int(census.get("aiBaseCount", -1)) == bases.size(), "census aiBaseCount matches the base table")
	_check(armies.size() >= 6, "every playable faction has an army definition (>= 6)")
	var member_total := 0
	for army_name in armies:
		var army := armies[army_name] as Dictionary
		var side := army.get("side", {}) as Dictionary
		_check(String(side.get("value", "")) != "", "army %s carries an authored Side" % army_name)
		var members := army.get("armyMembers", []) as Array
		member_total += members.size()
		_check(not members.is_empty(), "army %s has ArmyMemberDefinitions" % army_name)
	_check(int(census.get("armyMemberDefinitionCount", -1)) == member_total, "census armyMemberDefinitionCount matches the member rows")
	var difficulty := document.get("difficultyTuning", {}) as Dictionary
	_check(not difficulty.is_empty(), "difficultyTuning rows are present")
	_check(document.get("brutalDifficultyCheats") != null, "BrutalDifficultyCheats block is present")
	for chain_value in chains:
		var chain := chain_value as Dictionary
		if not chain.has("targetTypes"):
			_check(false, "combat chain missing targetTypes matrix")
			break
	_finish()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("SKIRMISH_AI_DOCUMENT PASS %s" % label)
	else:
		failed += 1
		push_error("SKIRMISH_AI_DOCUMENT FAIL %s" % label)


func _finish() -> void:
	print("SKIRMISH_AI_DOCUMENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
