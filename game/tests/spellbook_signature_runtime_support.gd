extends RefCounted
## Shared fixture for the compiled-spellbook signature runners. Every verdict
## is measured against the SELECTED content pack's spellbook runtime document,
## so the numbers come from converted retail data rather than a hand fixture.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")


func run(tree: SceneTree, signature: String) -> Dictionary:
	var content_db = tree.root.get_node_or_null("ContentDB")
	if content_db == null:
		return {"ok": false, "detail": "ContentDB missing"}
	var doc: Dictionary = content_db.get_spellbook_runtime()
	var sim = Sim.new()
	if doc.is_empty() or not sim.configure_spellbook_runtime(doc):
		return {"ok": false, "detail": sim.spellbook_error()}
	var tree_doc := ((doc.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary)
	var sciences: Array = tree_doc.get("sciences", []) as Array
	var powers: Array = tree_doc.get("powers", []) as Array
	match signature:
		"field:science.IsGrantable":
			return _is_grantable(sim, sciences)
		"field:science.PrerequisiteSciences":
			return _prerequisite_sciences(sim, sciences)
		"field:science.SciencePurchasePointCost":
			return _purchase_point_cost(sim, sciences, false)
		"field:science.SciencePurchasePointCostMP":
			return _purchase_point_cost(sim, sciences, true)
		"field:specialpower.ReloadTime":
			return _reload_time(sim, powers)
		"field:specialpower.RequiredSciences":
			return _required_sciences(sim, powers)
	return {"ok": false, "detail": "unknown signature: " + signature}


func _tree_sciences(sciences: Array) -> Array:
	## Only sciences with an authored purchase block form the palantir tree.
	var rows: Array = []
	for value in sciences:
		var row := value as Dictionary
		if not (row.get("purchase", {}) as Dictionary).is_empty():
			rows.append(row)
	return rows


func _is_grantable(sim, sciences: Array) -> Dictionary:
	## IsGrantable = Yes admits a grant outside the purchase flow once the
	## authored prerequisites are owned; a science the document does not carry
	## must be refused.
	var checked := 0
	for row_value in _tree_sciences(sciences):
		var row := row_value as Dictionary
		if not bool(row.get("isGrantable", false)):
			continue
		var science_id := String(row.get("id", ""))
		var groups: Array = row.get("prerequisiteGroups", []) as Array
		sim._team_sciences[0] = [] if groups.is_empty() else (groups[0] as Array).duplicate()
		var verdict: Dictionary = sim.grant_science(0, science_id)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "detail": "%s refused: %s" % [science_id, str(verdict)]}
		if not sim.owned_sciences(0).has(science_id):
			return {"ok": false, "detail": "%s granted but not owned" % science_id}
		var replay: Dictionary = sim.grant_science(0, science_id)
		if String(replay.get("reason", "")) != "already-owned":
			return {"ok": false, "detail": "%s re-granted: %s" % [science_id, str(replay)]}
		checked += 1
		if checked >= 3:
			break
	if checked <= 0:
		return {"ok": false, "detail": "no grantable tree science in the document"}
	var unknown: Dictionary = sim.grant_science(0, "SCIENCE_NotInTheDocument")
	if String(unknown.get("reason", "")) != "unknown-science":
		return {"ok": false, "detail": "an unknown science was granted: %s" % str(unknown)}
	return {"ok": true, "detail": "%d grantable sciences honoured IsGrantable" % checked}


func _prerequisite_sciences(sim, sciences: Array) -> Dictionary:
	## With no sciences owned, a power whose tree science carries authored
	## PrerequisiteSciences must refuse both purchase and grant.
	sim._team_sciences[0] = []
	for power_id in sim.spellbook_power_ids():
		var power: Dictionary = sim.spellbook_power(power_id)
		var science_id := String(power.get("science_id", ""))
		for value in sciences:
			var row := value as Dictionary
			if String(row.get("id", "")) != science_id:
				continue
			var groups: Array = row.get("prerequisiteGroups", []) as Array
			if groups.is_empty():
				continue
			var purchase: Dictionary = sim.can_purchase_power(0, power_id)
			if String(purchase.get("reason", "")) != "prerequisites-unmet":
				return {"ok": false, "detail": "%s purchasable with no prerequisites owned: %s" % [power_id, str(purchase)]}
			if bool(row.get("isGrantable", false)):
				var grant: Dictionary = sim.grant_science(0, science_id)
				if String(grant.get("reason", "")) != "prerequisites-unmet":
					return {"ok": false, "detail": "%s granted with no prerequisites owned: %s" % [science_id, str(grant)]}
			sim._team_sciences[0] = (groups[0] as Array).duplicate()
			var admitted: Dictionary = sim.can_purchase_power(0, power_id)
			if String(admitted.get("reason", "")) == "prerequisites-unmet":
				return {"ok": false, "detail": "%s still blocked with %s owned" % [power_id, str(groups[0])]}
			return {"ok": true, "detail": "%s gated by %s" % [power_id, str(groups)]}
	return {"ok": false, "detail": "no power with authored prerequisites in the document"}


func _purchase_point_cost(sim, sciences: Array, multiplayer: bool) -> Dictionary:
	## Every tree science's compiled cost must reach the sim unchanged, and the
	## two authored costs must be read from their own fields.
	var key := "pointCostMP" if multiplayer else "pointCost"
	var field := "SciencePurchasePointCostMP" if multiplayer else "SciencePurchasePointCost"
	var checked := 0
	for row_value in _tree_sciences(sciences):
		var row := row_value as Dictionary
		var science_id := String(row.get("id", ""))
		var expected := int((row.get(key, {}) as Dictionary).get("value", -1))
		if expected < 0:
			return {"ok": false, "detail": "%s carries no %s" % [science_id, field]}
		var measured: int = sim.science_purchase_cost(science_id, multiplayer)
		if measured != expected:
			return {"ok": false, "detail": "%s %s = %d, document says %d" % [science_id, field, measured, expected]}
		checked += 1
	if checked <= 0:
		return {"ok": false, "detail": "no tree science in the document"}
	var absent: int = sim.science_purchase_cost("SCIENCE_NotInTheDocument", multiplayer)
	if absent != -1:
		return {"ok": false, "detail": "an unknown science reported a cost: %d" % absent}
	return {"ok": true, "detail": "%d sciences matched %s" % [checked, field]}


func _reload_time(sim, powers: Array) -> Dictionary:
	## ReloadTime is the spell recharge: milliseconds in, ticks out, and the
	## cooldown surface must agree with the compiled row.
	var checked := 0
	for value in powers:
		var row := value as Dictionary
		var power_id := String(row.get("id", ""))
		var expected := int((row.get("reloadTimeMs", {}) as Dictionary).get("value", -1))
		var compiled: Dictionary = sim.spellbook_power(power_id)
		if expected <= 0 or compiled.is_empty():
			continue
		if int(compiled.get("reload_ms", -1)) != expected:
			return {"ok": false, "detail": "%s reload_ms = %s, document says %d" % [power_id, str(compiled.get("reload_ms")), expected]}
		if int(compiled.get("reload_ticks", 0)) <= 0:
			return {"ok": false, "detail": "%s resolved no recharge ticks" % power_id}
		var cooldown: Dictionary = sim.power_cooldown_state(0, power_id)
		if int(cooldown.get("total_ticks", 0)) <= 0:
			return {"ok": false, "detail": "%s has no cooldown surface: %s" % [power_id, str(cooldown)]}
		checked += 1
	if checked <= 0:
		return {"ok": false, "detail": "no power with an authored ReloadTime in the document"}
	return {"ok": true, "detail": "%d powers matched ReloadTime" % checked}


func _required_sciences(sim, powers: Array) -> Dictionary:
	## The tree science a power resolves against must be one of its authored
	## RequiredSciences, never an invented one.
	var checked := 0
	for value in powers:
		var row := value as Dictionary
		var power_id := String(row.get("id", ""))
		var required: Array = row.get("requiredSciences", []) as Array
		var compiled: Dictionary = sim.spellbook_power(power_id)
		if required.is_empty() or compiled.is_empty():
			continue
		var science_id := String(compiled.get("science_id", ""))
		if science_id == "" or not required.has(science_id):
			return {"ok": false, "detail": "%s resolved '%s', authored %s" % [power_id, science_id, str(required)]}
		checked += 1
	if checked <= 0:
		return {"ok": false, "detail": "no power with authored RequiredSciences in the document"}
	return {"ok": true, "detail": "%d powers matched RequiredSciences" % checked}
