class_name TransitionDamageFX
extends RefCounted
## Typed TransitionDamageFX consumer: FXList / PSys ids fired when a body
## crosses damaged, really-damaged, or rubble. OCL spawn rows stay deferred.
##
## Lives in its own file so the combat lane can keep editing retail_slice_sim.gd.
## The structure presenter calls `select_crossing` on an authored phase change.

const STAGES := {
	"damaged": "Damaged",
	"really-damaged": "ReallyDamaged",
	"really_damaged": "ReallyDamaged",
	"reallyDamaged": "ReallyDamaged",
	"rubble": "Rubble",
}


static func select_crossing(contracts: Array, from_stage: String, to_stage: String) -> Dictionary:
	var prefix := String(STAGES.get(to_stage, ""))
	if prefix == "" or from_stage == to_stage:
		return {
			"source": "typed-transition-damage-fx",
			"stage": to_stage,
			"applied": 0,
			"fxLists": [],
			"particleSystems": [],
		}
	var fx_lists: Array = []
	var particles: Array = []
	var applied := 0
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "TransitionDamageFX":
			continue
		if String(contract.get("extraction", "")) != "typed":
			continue
		if String(contract.get("runtimeStatus", contract.get("runtime_status", ""))) != "executable":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		if fields.has("deferredFields") and (fields.get("deferredFields", []) as Array).size() > 0 and not _has_closed_effects(fields, prefix):
			continue
		for row_value in fields.get("effects", []) as Array:
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := row_value as Dictionary
			if String(row.get("stage", "")) != prefix:
				continue
			var kind := String(row.get("kind", ""))
			if kind == "FXList":
				var fx_id := String(row.get("fxList", ""))
				if fx_id == "":
					continue
				fx_lists.append({
					"id": fx_id,
					"loc": (row.get("loc", {}) as Dictionary).duplicate(true),
				})
				applied += 1
			elif kind == "ParticleSystem":
				var psys := String(row.get("particleSystem", ""))
				if psys == "":
					continue
				particles.append({
					"id": psys,
					"bone": String(row.get("bone", "")),
					"randomBone": bool(row.get("randomBone", false)),
				})
				applied += 1
	return {
		"source": "typed-transition-damage-fx",
		"stage": to_stage,
		"applied": applied,
		"fxLists": fx_lists,
		"particleSystems": particles,
	}


static func _has_closed_effects(fields: Dictionary, prefix: String) -> bool:
	for row_value in fields.get("effects", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		if String(row.get("stage", "")) != prefix:
			continue
		if String(row.get("kind", "")) in ["FXList", "ParticleSystem"]:
			return true
	return false
