extends RefCounted

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
			for value in sciences:
				var row := value as Dictionary
				if row.get("purchase", {}) is Dictionary and not (row.get("purchase", {}) as Dictionary).is_empty() and bool(row.get("isGrantable", false)):
					var groups:=row.get("prerequisiteGroups",[]) as Array
					if not groups.is_empty():sim._team_sciences[0]=(groups[0] as Array).duplicate()
					var verdict:=sim.grant_science(0,String(row.get("id","")))
					return {"ok": bool(verdict.get("ok",false)), "detail": "%s:%s"%[String(row.get("id","")),str(verdict)]}
		"field:science.PrerequisiteSciences":
			sim._team_sciences[0]=[]
			for power_id in sim.spellbook_power_ids():
				var power := sim.spellbook_power(power_id)
				var science_id := String(power.get("science_id", ""))
				for value in sciences:
					var row := value as Dictionary
					if String(row.get("id", "")) == science_id and not (row.get("prerequisiteGroups", []) as Array).is_empty():
						var verdict := sim.can_purchase_power(0, power_id)
						return {"ok": String(verdict.get("reason", "")) == "prerequisites-unmet", "detail": "%s:%s" % [power_id, str(row.get("prerequisiteGroups", []))]}
		"field:science.SciencePurchasePointCost":
			for value in sciences:
				var row := value as Dictionary
				if not (row.get("purchase", {}) as Dictionary).is_empty():
					var expected := int((row.get("pointCost", {}) as Dictionary).get("value", -1))
					return {"ok": expected >= 0 and sim.science_purchase_cost(String(row.get("id", "")), false) == expected, "detail": "%s=%d" % [String(row.get("id", "")), expected]}
		"field:science.SciencePurchasePointCostMP":
			for value in sciences:
				var row := value as Dictionary
				if not (row.get("purchase", {}) as Dictionary).is_empty():
					var expected := int((row.get("pointCostMP", {}) as Dictionary).get("value", -1))
					return {"ok": expected > 0 and sim.science_purchase_cost(String(row.get("id", "")), true) == expected, "detail": "%s=%d" % [String(row.get("id", "")), expected]}
		"field:specialpower.ReloadTime":
			for value in powers:
				var row := value as Dictionary
				var power_id := String(row.get("id", ""))
				var expected := int((row.get("reloadTimeMs", {}) as Dictionary).get("value", -1))
				var compiled := sim.spellbook_power(power_id)
				if expected > 0 and not compiled.is_empty():
					return {"ok": int(compiled.get("reload_ms", -1)) == expected and int(compiled.get("reload_ticks", 0)) > 0, "detail": "%s=%d" % [power_id, expected]}
		"field:specialpower.RequiredSciences":
			for value in powers:
				var row := value as Dictionary
				var required: Array = row.get("requiredSciences", []) as Array
				var compiled := sim.spellbook_power(String(row.get("id", "")))
				if not required.is_empty() and not compiled.is_empty():
					return {"ok": required.has(String(compiled.get("science_id", ""))), "detail": "%s:%s" % [String(row.get("id", "")), str(required)]}
	return {"ok": false, "detail": "no matching retail row"}
