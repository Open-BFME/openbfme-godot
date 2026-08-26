extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Spellbook effect-support compiler carved out of retail_sim_powers.gd (crack-2 finish): per-power effect analysis (OCL, summons, weather, groves, pings, cloudbreak) that decides what each authored power can execute.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _spellbook_effect_support(power_row: Dictionary, fields: Array, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Evidence gate: a power becomes castable only when its converted leaves
	## fully determine the runtime effect. Anything unresolved stays locked
	## with the gap recorded (never an invented effect).
	var module := String(power_row.get("module", ""))
	var field_values: Dictionary = {}
	var field_resolved: Dictionary = {}
	for field_value in fields:
		if typeof(field_value) == TYPE_DICTIONARY:
			var field_row := field_value as Dictionary
			var field_key := String(field_row.get("key", ""))
			if field_row.has("resolvedText"):
				field_values[field_key] = String(field_row.get("resolvedText", ""))
			else:
				field_values[field_key] = String(field_row.get("value", ""))
			if field_row.has("resolved"):
				field_resolved[field_key] = float(field_row.get("resolved", 0.0))
			elif field_row.has("resolvedMax"):
				field_resolved[field_key] = float(field_row.get("resolvedMax", 0.0))
	match module:
		"PlayerHealSpecialPower":
			# HealAmount is the resolved fraction of member health restored; the
			# authored HealRadius define now resolves in the doc (cursor radius
			# remains the fallback when it does not). Rebuild-class powers are
			# the flat structure-heal shape: HealAsPercent No + STRUCTURE affects
			# + a flat authored amount.
			var amount := _spellbook_field_float(field_values, "HealAmount", 0.0)
			var radius = float(field_resolved.get("HealRadius", float(power_row.get("radius_cursor_source", 0.0))))
			var as_percent := String(field_values.get("HealAsPercent", "Yes")) != "No"
			if radius <= 0.0:
				return {"ok": false, "reason": "heal radius did not resolve in the document"}
			if as_percent and (amount <= 0.0 or amount > 1.0):
				return {"ok": false, "reason": "heal amount did not resolve in the document"}
			if not as_percent and amount <= 0.0:
				return {"ok": false, "reason": "flat structure-heal amount did not resolve in the document"}
			return {"ok": true, "effect": {"kind": "heal", "amount": amount, "as_percent": as_percent, "radius_source": radius, "affects": String(field_values.get("HealAffects", ""))}}
		"SpecialPowerModule":
			var modifier_id := String(field_values.get("AttributeModifier", ""))
			if not field_values.has("AttributeModifier"):
				# Retail sometimes ships a SpecialPowerModule whose whole payload is
				# COMMENTED OUT upstream and re-homed on another object. Fuel the Fires
				# is the case in this corpus: object/system/system.ini:369-378 comments
				# out AttributeModifier/Range/Affects with the note "Done in science
				# check on the lumber mill", and the live bonus is
				# SupplyCenterDockUpdate BonusScience = SCIENCE_FueltheFires /
				# BonusScienceMultiplier = 200% on Object LumberMill
				# (object/civilian/civilianbuildings.ini:18831-18836). The sim has no
				# worker/supply-dock economy for that multiplier to act on.
				return {"ok": false, "reason": "SpecialPowerModule authors no AttributeModifier: the payload is commented out on the module and re-homed on a SupplyCenterDockUpdate BonusScience, which the sim does not model"}
			if modifier_id == "" or not modifier_leaves.has(modifier_id):
				return {"ok": false, "reason": "attribute modifier '%s' is not a converted leaf" % modifier_id}
			# Retail AttributeModifier leaves author repeated Modifier lines
			# (DAMAGE_MULT, ARMOR, PRODUCTION, …). Collapsing by key kept only
			# the last row and falsely rejected powers that still carried a
			# resolved DAMAGE_MULT earlier in the list.
			var modifier_fields: Dictionary = {}
			var modifier_rows: Array = []
			for field_value in Array((modifier_leaves[modifier_id] as Dictionary).get("fields", [])):
				if typeof(field_value) != TYPE_DICTIONARY:
					continue
				var field_row := field_value as Dictionary
				var field_key := String(field_row.get("key", ""))
				var field_text := String(field_row.get("value", ""))
				if field_key == "Modifier":
					modifier_rows.append(field_text)
				elif field_key != "":
					modifier_fields[field_key] = field_text
			var damage_mult := 0.0
			var armor_mult := 0.0
			var production_mult := 0.0
			var invulnerable := false
			var unsupported_rows: Array = []
			# WHICH ROWS THE LEAF ACTUALLY AUTHORED. The three multipliers below
			# all default to the neutral 1.0 when their row is absent, so a
			# consumer reading only the numbers cannot tell an AUTHORED 100% from
			# a row that was never written. That distinction is not academic: the
			# spellbook matrix classifies a power as "castable but inert" when its
			# damage and armor multipliers are neutral and its production
			# multiplier is not, and "neutral" meant "absent OR authored-100%".
			# The grove-aura lane already publishes exactly this table for the
			# same reason (see the `authored_rows` key on the grove_aura effect).
			var authored_rows: Dictionary = {
				"DAMAGE_MULT": false, "ARMOR": false, "PRODUCTION": false
			}
			for modifier_text_value in modifier_rows:
				var parsed := _parse_modifier_row(String(modifier_text_value))
				# Fail-closed on an UNREADABLE row (see _parse_modifier_row). An
				# unconsumed KIND still falls through: this probe only asks whether the
				# leaf carries an effect the sim can apply, and `has_effect` below is
				# the verdict for that.
				if not parsed.get("ok", false):
					return {"ok": false, "reason": "attribute modifier '%s': %s" % [
						modifier_id, String(parsed.get("reason", "")),
					]}
				if not bool(parsed.get("supported", false)):
					# READ but not modelled — named and counted, never a silent drop and
					# never a shape error that takes the readable rows beside it down
					# with it. angmar/SpellBookSnowbind is exactly this case: its
					# `INVULNERABLE 0% SLASH PIERCE …` damage-type scope list has no
					# runtime here, while the `PRODUCTION 1%` row on the same leaf is
					# perfectly readable and used to be lost with it.
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": String(parsed.get("reason", "")),
					})
					continue
				var kind := String(parsed.get("kind", ""))
				# Only the percent shape is a multiplier; KIND_PLAIN is an absolute
				# magnitude (HEALTH 400) and must not be read as one.
				if String(parsed.get("shape", "")) != "percent":
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": "absolute-magnitude '%s' row has no multiplier runtime here" % kind,
					})
					continue
				var percent := float(parsed.get("value", 0.0))
				if kind == "DAMAGE_MULT":
					damage_mult = percent
					authored_rows["DAMAGE_MULT"] = true
				elif kind == "ARMOR":
					armor_mult = percent
					authored_rows["ARMOR"] = true
				elif kind == "PRODUCTION":
					production_mult = percent
					authored_rows["PRODUCTION"] = true
			var duration_ms := _spellbook_field_float(modifier_fields, "Duration", 0.0)
			# Duration may be an unresolved define name on the leaf; power-level
			# field_resolved already carries numeric AttributeModifierRange.
			if duration_ms <= 0.0 and modifier_fields.has("Duration"):
				var duration_text := String(modifier_fields.get("Duration", ""))
				# Common retail define pattern: NAME_EFFECT_DURATION — resolve via
				# power field table when the leaf only stores the define token.
				if duration_text != "" and field_resolved.has(duration_text):
					duration_ms = float(field_resolved.get(duration_text, 0.0))
			var range_source := _spellbook_field_float(field_values, "AttributeModifierRange", 0.0)
			if range_source <= 0.0 and field_resolved.has("AttributeModifierRange"):
				range_source = float(field_resolved.get("AttributeModifierRange", 0.0))
			# `invulnerable` can no longer be set from a row: retail authors ZERO bare
			# `INVULNERABLE` flags (census in _parse_modifier_row) — every authored
			# INVULNERABLE carries `0%` plus a damage-type scope list, which is
			# read-but-not-supported above. The field is kept (always false) so the
			# effect shape and its consumers do not move; blanket invulnerability was
			# never actually authored in this corpus.
			var has_effect := damage_mult > 0.0 or armor_mult > 0.0 or production_mult > 0.0 or invulnerable
			# Economy PRODUCTION auras (Industry / Dwarven Riches) are permanent
			# while the model condition holds — no Duration row. Combat auras
			# require a positive duration.
			var duration_ok := duration_ms > 0.0 or production_mult > 0.0
			if not has_effect or not duration_ok or range_source <= 0.0:
				return {"ok": false, "reason": "attribute modifier leaf lacks a resolved damage mult, duration, or range"}
			var duration_ticks := 0
			if duration_ms > 0.0:
				duration_ticks = maxi(1, roundi(duration_ms / 1000.0 / sim.TICK_SECONDS))
			return {"ok": true, "effect": {
				"kind": "attribute_modifier",
				"damage_mult": damage_mult if damage_mult > 0.0 else 1.0,
				"armor_mult": armor_mult if armor_mult > 0.0 else 1.0,
				"production_mult": production_mult if production_mult > 0.0 else 1.0,
				"invulnerable": invulnerable,
				"duration_ticks": duration_ticks,
				"permanent": duration_ticks == 0 and production_mult > 0.0,
				"range_source": range_source,
				"affects": String(field_values.get("AttributeModifierAffects", "")),
				# Which of the three multiplier rows the leaf actually authored, so
				# an authored-neutral 100% is distinguishable from an absent row.
				"authored_rows": authored_rows,
				# Named residual rows carried onto the effect so the runner and the
				# report can COUNT them instead of losing them.
				"unsupported_modifier_rows": unsupported_rows,
			}}
		"OCLSpecialPower":
			return _spellbook_ocl_support(
				power_row, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves,
				String(field_values.get("CreateLocation", "")),
				String(field_values.get("NearestSecondaryObjectFilter", ""))
			)
		"DarknessSpecialPower":
			return _spellbook_weather_modifier_support(field_values, field_resolved, modifier_leaves)
		"FreezingRainSpecialPower":
			return _spellbook_weather_anticategory_support(field_values, field_resolved)
		"UntamedAllegianceSpecialPower":
			return _spellbook_untamed_allegiance_support(field_values, field_resolved)
		"DevastateSpecialPower":
			# Retail's Devastation squeezes resources out of the map's TREE objects
			# (TreeValueMultiplier 50%, TreeValueTotalCap 1500 -
			# object/system/system.ini:241-252) and fires DevastationEntWeapon, whose
			# only damage nugget is filtered to `NONE +RohanGenericEnt +RohanTreeBerd
			# ENEMIES` (weapon.ini DevastationEntWeapon). The sim models neither trees
			# as harvestable objects nor Ent units, so BOTH halves have no target.
			return {"ok": false, "reason": "DevastateSpecialPower has no target in the sim: its resource half converts TREE objects (TreeValueMultiplier %s, TreeValueTotalCap %d), which the sim does not model, and its damage half (FireWeapon '%s') is filtered to Ents only" % [
				String(field_values.get("TreeValueMultiplier", "")),
				int(field_resolved.get("TreeValueTotalCap", 0)),
				String(field_values.get("FireWeapon", "")),
			]}
		"ScavengerSpecialPower":
			var bounty_percent := _spellbook_field_float(field_values, "BountyPercent", -1.0)
			if bounty_percent < 0.0:
				return {"ok": false, "reason": "ScavengerSpecialPower BountyPercent did not resolve in the document"}
			return {"ok": true, "effect": {"kind": "scavenger_bounty", "bounty_percent": bounty_percent}}
		"ElvenWoodSpecialPower":
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "ElvenGroveObject")
		"TaintSpecialPower":
			# Retail's TaintSpecialPower and ElvenWoodSpecialPower are the SAME
			# module shape with a different planted object: `TaintObject/TaintRadius/
			# TaintFX/TaintOCL` against `ElvenGroveObject/ElvenWoodRadius/
			# ElvenWoodFX/ElvenWoodOCL` (data/ini/object/system/system.ini:31-38 vs
			# :829-837). TaintLand and ElvenGrove are byte-identical objects apart
			# from Side and the RequiredConditions cell type
			# (object/evilfaction/sim.structures/taintland.ini:3-46 vs
			# object/goodfaction/sim.structures/elven/grove.ini:3-47), so one resolver
			# serves both.
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "TaintObject")
		"CloudBreakSpecialPower":
			return _spellbook_cloudbreak_support(field_values, field_resolved)
		_:
			return {"ok": false, "reason": "unsupported effect module '%s'" % module}


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	var raw := String(fields.get(key, ""))
	if raw == "" or not raw.is_valid_float():
		return fallback
	return float(raw)


func _parse_modifier_row(value: String) -> Dictionary:
	## THE one reader of an AttributeModifier leaf's `Modifier =` row. Every
	## resolver that used to split the text itself is routed through here.
	##
	## Retail authors these rows TAB-separated as often as space-separated
	## (attributemodifier.ini:74 `Modifier = ARMOR<TAB>50%` against :75
	## `Modifier = DAMAGE_MULT 150%`) and the converter preserves the tab
	## verbatim, so a space-only split silently dropped every tabbed row. That
	## is exactly what kept Elven Wood reading as "modifier is not converted",
	## and duplicating the normalization at four call sites is how one of them
	## kept missing it.
	##
	## SHAPES ARE A CENSUS, NOT A GUESS. Round 18 counted every `Modifier =` row
	## in the PURE RETAIL oracle tree
	## (workspace/retail-work/editions/rotwk/cache/effective-assets, all .ini/.inc,
	## comments stripped): 894 rows total, and they fall into exactly three
	## authored shapes —
	##
	##   KIND_PCT   386  `ARMOR<TAB>50%`, `DAMAGE_MULT 150%`
	##                     -> shape "percent", value = n / 100.0
	##   KIND_PLAIN 363  `HEALTH 400`, `CRUSHABLE_LEVEL 3`, and the far commoner
	##                   `ARMOR <DEFINE_TOKEN>` / `HEALTH <DEFINE_TOKEN>` form
	##                     -> shape "plain", value = n (NOT divided); an
	##                        unresolved define token is unreadable, ok=false
	##   MULTI      145  `INVULNERABLE 0% SLASH PIERCE …` (a damage-type scope
	##                   list), `ARMOR <TOKEN> CRUSH`, `DAMAGE_MULT
	##                   #MULTIPLY( X 0.60 )`, `PRODUCTION <TOKEN> %` (the
	##                   percent sign detached by whitespace)
	##                     -> shape "percent_scoped"/"plain_scoped"/"expression",
	##                        ok=true, supported=false, with a NAMED reason
	##
	##   BARE_FLAG    0  ZERO rows in the corpus are a single bare token. The old
	##                   `parts.size() == 1 -> flag row` branch was dead code
	##                   invented from the *appearance* of `INVULNERABLE`, whose
	##                   real authored form always carries `0%` plus a scope list
	##                   (attributemodifier.ini:909, :1079). It is deleted: one
	##                   token is now unreadable and fails closed.
	##
	## THREE-WAY VERDICT, because two different mistakes are possible:
	##   ok=false                -> UNREADABLE. Fail closed at the call site: a
	##                              row nobody can read is a buff that would go
	##                              missing in silence.
	##   ok=true, supported=false -> READ, but its runtime is not modelled. The
	##                              caller may explicitly not-support it by name
	##                              (`reason`) and carry on with the rows it can
	##                              apply. A shape error here used to lock whole
	##                              powers (angmar/SpellBookSnowbind lost its
	##                              readable `PRODUCTION 1%` because the
	##                              INVULNERABLE row beside it was called a shape
	##                              error).
	##   ok=true, supported=true  -> a plain scalar the generic consumers apply.
	##
	## `flag` is retained on every result (always false) so callers written
	## against the old contract keep compiling; nothing sets it any more.
	var parts_raw := value.replace("\t", " ").split(" ", false)
	var parts: Array[String] = []
	for part_value in parts_raw:
		parts.append(String(part_value))
	if parts.is_empty():
		return {"ok": false, "reason": "modifier row is empty"}
	var kind: String = parts[0]
	var rest: Array[String] = parts.slice(1)
	if rest.size() >= 2 and rest[rest.size() - 1] == "%":
		# `PRODUCTION ROHAN_FARM_LVL2_PRODUCTION  %` (attributemodifier.ini:1406):
		# retail lets the percent sign drift off its magnitude. Reattach it before
		# anything else so the row is judged on its real shape.
		rest = rest.slice(0, rest.size() - 1)
		rest[rest.size() - 1] = rest[rest.size() - 1] + "%"
	if rest.is_empty():
		return {"ok": false, "reason": "modifier row '%s' is a single token with no magnitude; retail authors none (census: 0 of 894)" % value}
	var head: String = rest[0]
	var scope: Array = rest.slice(1)
	if head.begins_with("#"):
		# `DAMAGE_MULT #MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER 0.60 )`.
		# The pack ships the expression verbatim; the sim has no INI expression
		# evaluator, so this is read-but-not-supported rather than a shape error.
		return {
			"ok": true, "supported": false, "flag": false, "shape": "expression",
			"kind": kind, "value": 0.0, "scope": rest,
			"reason": "modifier row '%s' is an authored INI expression; the pack ships it unevaluated and the sim has no expression evaluator" % value,
		}
	var magnitude := 0.0
	var shape := ""
	if head.ends_with("%"):
		var percent_text: String = head.trim_suffix("%")
		if not percent_text.is_valid_float():
			return {"ok": false, "reason": "modifier row '%s' has a non-numeric percent" % value}
		magnitude = float(percent_text) / 100.0
		shape = "percent"
	elif head.is_valid_float():
		# KIND_PLAIN carries an ABSOLUTE magnitude (HEALTH 400), not a percent.
		# Callers must read `shape` before treating `value` as a multiplier.
		magnitude = float(head)
		shape = "plain"
	else:
		return {"ok": false, "reason": "modifier row '%s' magnitude '%s' is an unresolved define token" % [value, head]}
	if scope.is_empty():
		return {
			"ok": true, "supported": true, "flag": false, "shape": shape,
			"kind": kind, "value": magnitude, "scope": [],
		}
	return {
		"ok": true, "supported": false, "flag": false, "shape": shape + "_scoped",
		"kind": kind, "value": magnitude, "scope": scope,
		"reason": "modifier row '%s' scopes %s to the damage-type list %s; the sim applies modifiers globally and does not model per-damage-type scoping" % [
			value, kind, " ".join(scope),
		],
	}


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary, create_location: String = "", secondary_object_filter: String = "") -> Dictionary:
	## OCL powers dispatch on the spawned objects' converted evidence:
	## fire-weapon receptacles (volley/quake), summon eggs, or sim.structures.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {"ok": false, "reason": "power references no object-creation list"}
	var ocl_id := String(ocl_ids[0])
	var ocl: Dictionary = ocl_leaves.get(ocl_id, {}) as Dictionary
	if ocl.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' is not a converted leaf" % ocl_id}
	var spawns: Array = []
	var missing: Array = []
	var creates: Array = ocl.get("createObjects", []) as Array
	for create_index in range(creates.size()):
		var create_value: Variant = creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if leaf.is_empty():
				missing.append(object_name)
			else:
				spawns.append({"create": create, "create_index": create_index, "leaf": leaf})
	if not missing.is_empty():
		return {"ok": false, "reason": "spawned object(s) %s are not converted leaves" % ", ".join(missing)}
	if spawns.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' creates no objects" % ocl_id}
	var first_leaf: Dictionary = (spawns[0] as Dictionary)["leaf"]
	if not Array(first_leaf.get("fireWeapons", [])).is_empty():
		return _spellbook_fire_weapon_support(spawns, weapon_leaves)
	# Reveal/field "ping" objects are not units and must not be routed through the
	# summon resolver, which rejected them for the irrelevant reason that an
	# IMMOBILE object authors no locomotor. Returns {} for anything else.
	var ping_verdict := _spellbook_field_ping_support(spawns, modifier_leaves)
	if not ping_verdict.is_empty():
		return ping_verdict
	# Shape detectors that own a MORE PRECISE reason than the generic summon or
	# structure paths would produce. Each names the single authored module that
	# carries the whole power, so a future converter change can be aimed at it.
	var shaped := _spellbook_ocl_named_gap(spawns, object_leaves, ocl_leaves, create_location, secondary_object_filter)
	if not shaped.is_empty():
		return shaped
	for spawn_value in spawns:
		var hatch_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if typeof(hatch_leaf.get("hatch", null)) == TYPE_DICTIONARY:
			var summon_verdict := _spellbook_summon_support(spawns, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			if bool(summon_verdict.get("ok", false)):
				return summon_verdict
			var preview := _spellbook_summon_literal_preview(spawns, object_leaves, ocl_leaves)
			if bool(preview.get("ok", false)):
				summon_verdict["effect"] = preview.get("effect", {})
			return summon_verdict
	var body_kinds: Array = first_leaf.get("bodyKinds", []) as Array
	if body_kinds.has("StructureBody"):
		return _spellbook_structure_summon_support(spawns[0], weapon_leaves)
	# Direct summon OCLs (Ents, Eagles, Crebain/Cave Bats) create their live
	# units without an egg. Dragon Strike is identified by the spawned leaf's
	# authored-but-unconverted StrafeAreaUpdate, never by an unrelated OCL flag.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if Array(leaf.get("unconvertedBehaviors", [])).has("StrafeAreaUpdate"):
			var preview := _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)
			var verdict := {"ok": false, "reason": "spawned object '%s' requires StrafeAreaUpdate attack-run runtime" % String(leaf.get("id", ""))}
			if bool(preview.get("ok", false)):
				verdict["effect"] = preview.get("effect", {})
			return verdict
	# A direct summoned unit may author a SlowDeath corpse-effect OCL. Only an
	# actual egg is blocked here: its unconverted SlowDeath OCL creates the live
	# summon payload. Scan every leaf so a later egg cannot evade the gate.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if _spellbook_has_unconverted_hatch_payload(leaf, object_leaves, ocl_leaves):
			return {"ok": false, "reason": "spawned object '%s' has an unconverted SlowDeathBehavior hatch OCL" % String(leaf.get("id", ""))}
	return _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_has_unconverted_hatch_payload(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> bool:
	if not Array(leaf.get("unconvertedBehaviors", [])).has("SlowDeathBehavior"):
		return false
	for ocl_id_value in Array(leaf.get("unconvertedSlowDeathOcls", [])):
		var ocl: Dictionary = ocl_leaves.get(String(ocl_id_value), {}) as Dictionary
		for create_value in Array(ocl.get("createObjects", [])):
			if typeof(create_value) != TYPE_DICTIONARY:
				continue
			for object_id_value in Array((create_value as Dictionary).get("objects", [])):
				var payload: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
				if payload.is_empty():
					continue
				if payload.has("horde") or payload.has("weaponId") or int(payload.get("maxHealth", 0)) > 1:
					return true
	return false


func _spellbook_hatch_payload_leaves(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Array:
	## One hatch level down: the objects an egg's CreateObjectDie OCL creates.
	## Several powers park their entire runtime behind the egg (Avalanche's
	## FloodUpdate wave, the Citadel's CastleBehavior), so a named-gap scan that
	## only looked at the power OCL's direct spawns would miss them.
	var payload: Array = []
	var hatch_value: Variant = leaf.get("hatch", null)
	if typeof(hatch_value) != TYPE_DICTIONARY:
		return payload
	var ocl: Dictionary = ocl_leaves.get(String((hatch_value as Dictionary).get("ocl", "")), {}) as Dictionary
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		for object_id_value in Array((create_value as Dictionary).get("objects", [])):
			var child: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
			if not child.is_empty():
				payload.append(child)
	return payload


func _spellbook_ocl_named_gap(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, create_location: String, secondary_object_filter: String) -> Dictionary:
	## Precise fail-closed verdicts for OCL powers whose blocker is a single
	## authored module, reported BEFORE the generic summon/structure resolvers
	## produce an incidental (and misleading) reason such as "locomotion is not
	## converted" for an object that is deliberately IMMOBILE.
	## Returns {} when no named shape matches.
	var chain: Array = []
	for spawn_value in spawns:
		var spawn_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if spawn_leaf.is_empty():
			continue
		chain.append(spawn_leaf)
		chain.append_array(_spellbook_hatch_payload_leaves(spawn_leaf, object_leaves, ocl_leaves))
	for leaf_value in chain:
		var leaf: Dictionary = leaf_value as Dictionary
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if unconverted.has("FloodUpdate"):
			# Avalanche / Flood: the spawned object is an IMMOBILE INERT NO_COLLIDE
			# marker with an ImmortalBody, maxHealth 1 and a DeletionUpdate. Every
			# gameplay value of the power - the sweep path, its damage, the units it
			# throws - lives in FloodUpdate, which the compiler does not emit.
			return {"ok": false, "reason": "spawned object '%s' carries the whole power in FloodUpdate (wave path, damage and throw), which is not converted; the converted leaf is an immortal maxHealth-1 marker with only a %d ms DeletionUpdate" % [
				String(leaf.get("id", "")),
				int((leaf.get("deletion", {}) as Dictionary).get("maxMs", 0)),
			]}
		if unconverted.has("CastleBehavior"):
			# Citadel: the summoned object is a castle FOUNDATION. Its value is the
			# plot layout CastleBehavior declares (which expansion sites exist, what
			# may be built on them); without it the leaf is an UNATTACKABLE,
			# maxHealth-1 immortal marker with nothing to build on.
			return {"ok": false, "reason": "summoned object '%s' is a castle foundation whose CastleBehavior (plot layout and expansion sites) is not converted; the leaf converts to an UNATTACKABLE immortal maxHealth-%d marker with no plots" % [
				String(leaf.get("id", "")), int(leaf.get("maxHealth", 0)),
			]}
	if create_location == "USE_SECONDARY_OBJECT_LOCATION":
		# Bombard / Evil Bombard. The payload IS fully converted (20 seeds,
		# SpreadFormation, each seed hatching a projectile that fires
		# BombardProjectileWeapon at +600 ms for 400 SIEGE over radius 100), but the
		# barrage FOOTPRINT is the power, and where the seeds are laid out is not
		# determinable: CreateLocation names the secondary object while the same
		# CreateObject block also authors OrientInSecondaryDirection, so "at the
		# keep" and "at the cast point, oriented away from the keep" both read
		# consistently. The OpenSAGE oracle does not settle it either -
		# OCLSpecialPower.cs:35 throws NotImplementedException for this enum.
		# Guessing would move every impact, so the power stays locked.
		# TWO further runtime blockers ride the same payload, both verified against
		# the converted dwarves pack's BombardPhaseInitialWeapon /
		# BombardProjectileWeapon leaves:
		#   - the fear half is a LuaEventNugget (`LuaEvent = BeUncontrollablyAfraid`,
		#     Radius 200, SendToEnemies/SendToNeutral Yes) - retail routes it through
		#     the Lua event bus, which this sim does not have at all;
		#   - the impact half carries a MetaImpactNugget (ShockWaveAmount 75.0,
		#     ShockWaveTaperOff 1.0, ShockWaveRadius 50) - physics knockback with no
		#     model here.
		# So even a settled spread origin would leave the power partly unmodelled;
		# recording only the footprint ambiguity understated the gap.
		return {"ok": false, "reason": "OCLSpecialPower CreateLocation = USE_SECONDARY_OBJECT_LOCATION (NearestSecondaryObjectFilter '%s') is not modelled: the spread origin of the seed formation - caster keep or cast point - is not determinable from the converted pack, and the barrage footprint is the whole power. Two further halves have no runtime here either: the LuaEventNugget fear pulse (LuaEvent BeUncontrollablyAfraid, radius 200) needs retail's Lua event bus, and the MetaImpactNugget shockwave (amount 75.0, taper 1.0, radius 50) needs physics knockback" % secondary_object_filter}
	return {}


## --- Weather-based global spells (Darkness, Freezing Rain) -------------------
## Retail models these as a WEATHER change plus a global, range-less effect that
## holds for WeatherDuration. Neither power authors an AttributeModifierRange or
## a NEED_TARGET_POS cast option: the scope is the whole map. The sim therefore
## keeps a live window rather than doing a one-shot sweep, and re-applies it on
## the shared aura cadence so units that appear during the window are covered
## exactly as they are in retail (AttributeModifierWeatherBased = Yes).
## EMPTY-IS-ABSENT in the serialized state (see _serialize_state).


func _spellbook_weather_modifier_support(field_values: Dictionary, field_resolved: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	## RECORDED, redundant-but-unconsumed: SpellBookDarkness also authors a
	## SpecialPower-LEVEL filter (specialpower.ini:1446-1455 `ObjectFilter = ANY
	## -STRUCTURE -DwarvenZerker -NoldorWarrior -GondorKnightsofDol
	## -WildBabyDrake -IsengardFanatic -MordorBlackRider`). The converter does not
	## carry it onto the power row, and it changes nothing: the MODULE filter read
	## below (AttributeModifierAffects) is strictly narrower - it is ALLIES-only,
	## admits only +INFANTRY +CAVALRY +MONSTER (so -STRUCTURE is already implied),
	## and repeats every one of those unit exclusions. Noted here so the absence
	## reads as verified-inert rather than overlooked.
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var modifier_id := String(field_values.get("AttributeModifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "weather attribute modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var category := ""
	var leaf_duration_ms := 0.0
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			leaf_duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "weather modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane: a weather modifier is a whole-army buff and every authored
			# row of it has to land, so a read-but-not-modelled row still locks the
			# power — under its own name, not as a shape error.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "weather modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "weather modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
	if modifiers.is_empty():
		return {"ok": false, "reason": "weather modifier '%s' carries no converted stat rows" % modifier_id}
	# The authored leaf is INFINITE (Duration = 0): the weather window is what
	# bounds it. A positive leaf duration would be a different, shorter contract
	# than the one implemented here, so it fails closed rather than being
	# silently widened to the weather window.
	if leaf_duration_ms > 0.0:
		return {"ok": false, "reason": "weather modifier '%s' authors its own Duration %.0f ms; only the infinite (Duration = 0) weather-bounded form is modelled" % [modifier_id, leaf_duration_ms]}
	return {"ok": true, "effect": {
		"kind": "weather_modifier",
		"modifier_id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / sim.TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": String(field_values.get("AttributeModifierAffects", "")),
	}}


func _spellbook_weather_anticategory_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var anti_category := String(field_values.get("AntiCategory", ""))
	if anti_category != "LEADERSHIP":
		# LEADERSHIP is the one modifier category the sim can suppress
		# (leadership_suppressed_until_tick, shared with Horn of Gondor).
		return {"ok": false, "reason": "AntiCategory '%s' has no suppression runtime in the sim" % anti_category}
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	if affects == "":
		return {"ok": false, "reason": "AttributeModifierAffects is absent from the document"}
	# The burn-rate half of Freezing Rain (BurnRateModifier / BurnDecayModifier)
	# acts on retail's FireLogicSystem, which the sim does not model. It is
	# carried as evidence on the effect and named, never silently dropped.
	var unconverted: Array = []
	if field_resolved.has("BurnRateModifier") or field_resolved.has("BurnDecayModifier"):
		unconverted.append("FireLogicSystem burn rate/decay")
	return {"ok": true, "effect": {
		"kind": "weather_anticategory",
		"anti_category": anti_category,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / sim.TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": affects,
		"burn_rate_modifier": float(field_resolved.get("BurnRateModifier", 0.0)),
		"burn_decay_modifier": float(field_resolved.get("BurnDecayModifier", 0.0)),
		"unconverted_behaviors": unconverted,
	}}


func _spellbook_untamed_allegiance_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Lair conversion. The authored payload is exactly three things: TargetEnemy,
	## an object filter naming every creep lair and slaved creep, and a radius.
	## There is no AttributeModifier and no duration - the allegiance is
	## permanent, which is why the module carries neither.
	var range_source := float(field_resolved.get("AttributeModifierRange", 0.0))
	if range_source <= 0.0:
		return {"ok": false, "reason": "AttributeModifierRange did not resolve in the document"}
	var filter := String(field_values.get("AttributeModifierAffects", ""))
	if filter == "" or filter.ends_with("_OBJECTFILTER") or filter.ends_with("_OBJECT_FILTER"):
		return {"ok": false, "reason": "creep object filter '%s' did not resolve to its member list in the document" % filter}
	var lair_types: Array = []
	for term_value in filter.split(" ", false):
		var term := String(term_value)
		if term.begins_with("+") and term.contains("Lair"):
			lair_types.append(term.trim_prefix("+"))
	if lair_types.is_empty():
		return {"ok": false, "reason": "resolved creep filter names no lair objects"}
	lair_types.sort()
	return {"ok": true, "effect": {
		"kind": "creep_allegiance",
		"range_source": range_source,
		"filter": filter,
		"lair_types": lair_types,
		"target_enemy": String(field_values.get("TargetEnemy", "")) == "Yes",
	}}


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	var strikes: Array = []
	var seen_weapons: Array = []
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		for fw_value in Array((spawn["leaf"] as Dictionary).get("fireWeapons", [])):
			var fw := fw_value as Dictionary
			var weapon_id := String(fw.get("weapon", ""))
			var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
			if weapon.is_empty():
				return {"ok": false, "reason": "fire-weapon '%s' is not a converted leaf" % weapon_id}
			if not seen_weapons.has(weapon_id):
				seen_weapons.append(weapon_id)
			var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
			if nuggets.is_empty():
				# Warning-shot phase: an authored fire entry with no damage.
				continue
			var delay_ms := float(fw.get("fireDelayMs", 0.0))
			for nugget_value in nuggets:
				var nugget := nugget_value as Dictionary
				strikes.append({
					"delay_ms": delay_ms + float(nugget.get("delaytime", 0.0)),
					"damage": float(nugget.get("damage", 0.0)),
					"radius_source": float(nugget.get("radius", 0.0)),
					"damage_type": String(nugget.get("damagetype", "")).to_lower(),
					"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
				})
	if strikes.is_empty():
		# Undermine is this case: DwarvenUndermineSpawnWeapon has no DamageNugget
		# at all, only a MetaImpactNugget whose payload is an instant-death filter
		# plus a shock wave, and whose ShockWaveRadius is still the unresolved
		# define SPELL_UNDERMINE_SPAWN_DAMAGE_RADIUS in the pack. Naming the nugget
		# kinds points the fix at the compiler rather than at "no damage".
		var nugget_kinds: Array = []
		for weapon_id_value in seen_weapons:
			var seen_weapon: Dictionary = weapon_leaves.get(String(weapon_id_value), {}) as Dictionary
			for nugget_value in Array(seen_weapon.get("nuggets", [])):
				var nugget_kind := String((nugget_value as Dictionary).get("kind", ""))
				if nugget_kind != "" and not nugget_kinds.has(nugget_kind):
					nugget_kinds.append(nugget_kind)
		if nugget_kinds.is_empty():
			return {"ok": false, "reason": "fire-weapon chain (%s) carries no resolved damage nuggets" % ", ".join(seen_weapons)}
		return {"ok": false, "reason": "fire-weapon chain (%s) authors no DamageNugget; its only payload is %s, which has no sim runtime" % [", ".join(seen_weapons), ", ".join(nugget_kinds)]}
	return {"ok": true, "effect": {"kind": "fire_weapon", "strikes": strikes}}


func _spellbook_weapon_damage_nuggets(weapon: Dictionary, weapon_leaves: Dictionary) -> Array:
	## Direct damage nuggets, else the first projectile nugget's warhead chain.
	var direct: Array = weapon.get("damageNuggets", []) as Array
	if not direct.is_empty():
		return direct
	for nugget_value in Array(weapon.get("nuggets", [])):
		var nugget := nugget_value as Dictionary
		if String(nugget.get("kind", "")).to_lower() != "projectilenugget":
			continue
		var warhead: Dictionary = weapon_leaves.get(String(nugget.get("warheadId", "")), {}) as Dictionary
		if not warhead.is_empty():
			return warhead.get("damageNuggets", []) as Array
	return []


func _spellbook_weapon_field(weapon: Dictionary, key: String) -> float:
	for field_value in Array(weapon.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) != key:
			continue
		if field_row.has("resolvedMax"):
			return float(field_row.get("resolvedMax", 0.0))
		if field_row.has("resolved"):
			return float(field_row.get("resolved", 0.0))
		return 0.0
	return 0.0


func _spellbook_structure_summon_support(spawn: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	var leaf: Dictionary = spawn["leaf"]
	var health := int(leaf.get("maxHealth", 0))
	var weapon_id := String(leaf.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange")
	if health <= 0:
		return {"ok": false, "reason": "summoned structure health is not converted"}
	if weapon_id == "":
		# The Barricade is this case. It is SPAWNS_ARE_THE_WEAPONS: the structure
		# itself never shoots, its garrison does, and that garrison is the
		# unconverted SpawnBehavior (barricade.ini:175-184 - SpawnNumber 4,
		# InitialBurst 4, SpawnTemplateName MordorArcherBarricade_Slaved,
		# SpawnedRequireSpawner Yes). Summoning a silent 3000 HP wall would ship
		# half a power as if it were whole.
		var kind_of: Array = leaf.get("kindOf", []) as Array
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if kind_of.has("SPAWNS_ARE_THE_WEAPONS") and unconverted.has("SpawnBehavior"):
			return {"ok": false, "reason": "summoned structure '%s' is SPAWNS_ARE_THE_WEAPONS: its entire combat payload is the unconverted SpawnBehavior garrison and the leaf authors no weapon of its own" % String(leaf.get("id", ""))}
		return {"ok": false, "reason": "summoned structure '%s' authors no weapon" % String(leaf.get("id", ""))}
	if weapon.is_empty() or nuggets.is_empty() or attack_range <= 0.0:
		return {"ok": false, "reason": "summoned structure weapon '%s' is not fully converted" % weapon_id}
	var build_ms := 0.0
	for field_value in Array((spawn["create"] as Dictionary).get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		var field_key := String(field_row.get("key", ""))
		if field_key == "JustBuiltDuration" or field_key == "StartingBusyTime":
			build_ms = maxf(build_ms, float(field_row.get("resolved", 0.0)))
	return {"ok": true, "effect": {
		"kind": "structure_summon",
		"object_id": String(leaf.get("id", "")),
		"health": health,
		"build_ticks": maxi(1, roundi(build_ms / 1000.0 / sim.TICK_SECONDS)),
		"weapon": {
			"damage": float((nuggets[0] as Dictionary).get("damage", 0.0)),
			"range_source": attack_range,
			"damage_type": String((nuggets[0] as Dictionary).get("damagetype", "")).to_lower(),
			"period_ms": _spellbook_weapon_field(weapon, "DelayBetweenShots"),
			"pre_attack_ms": _spellbook_weapon_field(weapon, "PreAttackDelay"),
			"firing_ms": _spellbook_weapon_field(weapon, "FiringDuration"),
			"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
		},
	}}


func _spellbook_direct_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Direct OCL summon: validate every authored object and apply the same
	## Count/ObjectNames choice-pool semantics used by egg hatch OCLs. Choice
	## groups remain declarative here; the shared logic RNG is touched per cast.
	var groups_by_index: Dictionary = {}
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		var create: Dictionary = spawn.get("create", {}) as Dictionary
		var leaf: Dictionary = spawn.get("leaf", {}) as Dictionary
		var create_index := int(spawn.get("create_index", -1))
		var object_id := String(leaf.get("id", ""))
		if not Array(create.get("objects", [])).has(object_id):
			return {"ok": false, "reason": "direct summon object '%s' is absent from its CreateObject choice list" % String(leaf.get("id", ""))}
		var verdict := _spellbook_summon_rule(leaf, modifier_leaves, object_leaves, weapon_leaves)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": String(verdict.get("reason", "direct summon stats unresolved"))}
		if not groups_by_index.has(create_index):
			groups_by_index[create_index] = {
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create),
				"choices": [],
			}
		var group: Dictionary = groups_by_index[create_index]
		(group["choices"] as Array).append({
				"object_id": object_id,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
	var target_groups: Array = []
	var group_indices: Array = groups_by_index.keys()
	group_indices.sort()
	for group_index in group_indices:
		target_groups.append(groups_by_index[group_index])
	if target_groups.is_empty():
		return {"ok": false, "reason": "direct summon OCL resolves to no live targets"}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": 0,
		"target_groups": target_groups,
	}}


func _spellbook_create_pick_count(create: Dictionary, multiplier: int = 1) -> int:
	var count := 1
	for field_value in Array(create.get("fields", [])):
		if (
			typeof(field_value) == TYPE_DICTIONARY
			and String((field_value as Dictionary).get("key", "")) == "Count"
		):
			count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	return count * maxi(1, multiplier)


func _spellbook_create_enabled(create: Dictionary, owned_upgrades: Dictionary = {}) -> bool:
	## CreateObject rows can be mutually exclusive presentation variants. The
	## default spellbook cast owns no CE graphics upgrades, so a RequiredUpgrades
	## row is not an additional summon while its ForbiddenUpgrades sibling is.
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var upgrades := String(field.get("value", "")).split(" ", false)
		if key == "RequiredUpgrades":
			for upgrade in upgrades:
				if not owned_upgrades.has(String(upgrade)):
					return false
		elif key == "ForbiddenUpgrades":
			for upgrade in upgrades:
				if owned_upgrades.has(String(upgrade)):
					return false
	return true


func _spellbook_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Egg powers: the power OCL creates eggs; each egg hatches an OCL of
	## summoned battalions. The full chain must convert: egg hatch, hatch OCL,
	## target horde/member stats, member weapon, locomotion, summon lifetime.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false, "reason": "summon OCL has no converted hatch leaf"}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl_id := String(hatch.get("ocl", ""))
	var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' is not a converted leaf" % hatch_ocl_id}
	var egg_count := 1
	for field_value in Array((hatch_spawn.get("create", {}) as Dictionary).get("fields", [])):
		if typeof(field_value) == TYPE_DICTIONARY and String((field_value as Dictionary).get("key", "")) == "Count":
			egg_count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	var hatch_delay_ms := float(hatch.get("destructionDelayMs", 0.0))
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create_value: Variant = hatch_creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false, "reason": "summon target '%s' is not a converted leaf" % object_name}
			var verdict := _spellbook_summon_rule(target_leaf, modifier_leaves, object_leaves, weapon_leaves)
			if not bool(verdict.get("ok", false)):
				return {"ok": false, "reason": String(verdict.get("reason", "summon stats unresolved"))}
			choices.append({
				"object_id": object_name,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' spawns no converted targets" % hatch_ocl_id}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(hatch_delay_ms / 1000.0 / sim.TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_literal_preview(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Preserve the exact hatch payload and LifetimeUpdate literals when a live
	## summon remains unsupported for an orthogonal reason (for example, the
	## Watcher's unresolved weapon runtime). This is inspection data only: the
	## original failed verdict remains authoritative and cannot be cast.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl: Dictionary = ocl_leaves.get(String(hatch.get("ocl", "")), {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false}
	var egg_count := _spellbook_create_pick_count(hatch_spawn.get("create", {}) as Dictionary)
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create: Dictionary = hatch_creates[create_index] as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false}
			var lifetime: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
			if lifetime.is_empty():
				var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
				var member: Dictionary = object_leaves.get(String(horde.get("memberObject", "")), {}) as Dictionary
				lifetime = member.get("lifetime", {}) as Dictionary
			var lifetime_ms := float(lifetime.get("maxMs", 0.0))
			choices.append({
				"object_id": object_name,
				"rule": {},
				"lifetime_ticks": maxi(0, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
				"lifetime_death_type": String(lifetime.get("deathType", "")).to_upper(),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(float(hatch.get("destructionDelayMs", 0.0)) / 1000.0 / sim.TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_rule(target_leaf: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Project one summon target into the sim's unit-rule shape; every value
	## traces to the converted object/weapon/locomotor leaves.
	var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
	var member_id := String(target_leaf.get("id", "")) if horde.is_empty() else String(horde.get("memberObject", ""))
	var member_count := 1 if horde.is_empty() else int(horde.get("memberCount", 1))
	var member: Dictionary = object_leaves.get(member_id, {}) as Dictionary
	if member.is_empty():
		return {"ok": false, "reason": "summoned member '%s' is not a converted leaf" % member_id}
	if not member.has("maxHealth") and member.has("buildVariations"):
		for variation_value in Array(member.get("buildVariations", [])):
			var candidate: Dictionary = object_leaves.get(String(variation_value), {}) as Dictionary
			if candidate.has("maxHealth"):
				member = candidate
				break
	var member_health := int(member.get("maxHealth", 0))
	if member_health <= 0:
		return {"ok": false, "reason": "summoned member '%s' health is not converted" % member_id}
	var locomotor: Dictionary = member.get("locomotor", {}) as Dictionary
	var speed := float(locomotor.get("speed", 0.0))
	# Wyrm is intentionally stationary (WyrmLocomotor Speed = 0) but still has
	# a fully authored locomotor and ranged fire-breath runtime.
	if locomotor.is_empty() or speed < 0.0:
		return {"ok": false, "reason": "summoned member '%s' locomotion is not converted" % member_id}
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not locomotor.has(authored_field):
			push_error(
				"unauthored locomotor field %s for spellbook member %s"
				% [authored_field, member_id]
			)
			return {
				"ok": false,
				"reason": "summoned member '%s' locomotor field %s is unauthored" % [member_id, authored_field],
			}
	var weapon_id := String(member.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var kind_of: Array = member.get("kindOf", []) as Array
	var move_only := kind_of.has("MOVE_ONLY")
	if weapon_id == "" and not move_only:
		# Distinct from "the weapon leaf did not convert": the object authors NO
		# WeaponSet at all. The Watcher is this case - its attack runtime is
		# GrabPassengerSpecialPower + SpecialAbilityUpdate + TransportContain
		# (grab-and-devour), none of which the compiler emits, so reporting a
		# missing weapon leaf would aim a fix at the wrong module.
		var attack_behaviors: Array = []
		for behavior_value in Array(member.get("unconvertedBehaviors", [])):
			var behavior := String(behavior_value)
			if behavior in ["GrabPassengerSpecialPower", "SpecialAbilityUpdate", "TransportContain", "AutoPickUpUpdate"]:
				attack_behaviors.append(behavior)
		if attack_behaviors.is_empty():
			return {"ok": false, "reason": "summoned member '%s' authors no weapon and no unconverted attack runtime" % member_id}
		return {"ok": false, "reason": "summoned member '%s' authors no weapon: its attack runtime is %s, which is not converted" % [member_id, ", ".join(attack_behaviors)]}
	if weapon.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member '%s' weapon '%s' is not a converted leaf" % [member_id, weapon_id]}
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves) if not weapon.is_empty() else []
	if nuggets.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' has no resolved damage" % weapon_id}
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange") if not weapon.is_empty() else 0.0
	if attack_range <= 0.0 and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' range is not converted" % weapon_id}
	var lifetime_ms := 0.0
	var lifetime_death_type := ""
	var lifetime_row: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
	if not lifetime_row.is_empty():
		lifetime_ms = float(lifetime_row.get("maxMs", 0.0))
		lifetime_death_type = String(lifetime_row.get("deathType", ""))
	if lifetime_ms <= 0.0:
		var member_lifetime: Dictionary = member.get("lifetime", {}) as Dictionary
		lifetime_ms = float(member_lifetime.get("maxMs", 0.0))
		lifetime_death_type = String(member_lifetime.get("deathType", ""))
	if lifetime_ms <= 0.0:
		return {"ok": false, "reason": "summon target '%s' is missing converted LifetimeUpdate" % String(target_leaf.get("id", ""))}
	var scale = sim._spellbook_world_scale()
	var source_positions: Array[Vector2] = []
	for rank_value in Array(horde.get("ranks", [])):
		for position_value in Array((rank_value as Dictionary).get("positions", [])):
			var pair: Array = position_value as Array
			if pair.size() >= 2:
				source_positions.append(Vector2(float(pair[0]), float(pair[1])))
	if source_positions.size() != member_count:
		source_positions.clear()
		for index in range(member_count):
			source_positions.append(Vector2(10.0 + float(index % 4) * 15.0, float(index / 4) * 15.0))
	var center := Vector2.ZERO
	for position in source_positions:
		center += position
	center /= float(maxi(1, source_positions.size()))
	var positions: Array[Vector3] = []
	for position in source_positions:
		positions.append(Vector3((position.y - center.y) * scale, 0.0, (position.x - center.x) * scale))
	var damage_nugget: Dictionary = nuggets[0] if not nuggets.is_empty() else {}
	var delay_ms := _spellbook_weapon_field(weapon, "DelayBetweenShots")
	var clip_reload_ms := _spellbook_weapon_field(weapon, "ClipReloadTime")
	var period_ms := delay_ms if delay_ms > 0.0 else clip_reload_ms
	var pre_attack_ms := _spellbook_weapon_field(weapon, "PreAttackDelay")
	var firing_ms := _spellbook_weapon_field(weapon, "FiringDuration")
	var category := "infantry"
	if kind_of.has("HERO"):
		category = "hero"
	elif kind_of.has("CAVALRY"):
		category = "cavalry"
	var vision := float(member.get("visionRange", 0.0))
	if vision <= 0.0:
		vision = attack_range
	var rule := {
		"horde_id": String(target_leaf.get("id", "")),
		"member_count": member_count,
		"member_health": member_health,
		"member_damage": 0 if move_only else maxi(1, int(damage_nugget.get("damage", 0))),
		"category": category,
		"speed": speed * scale,
		"speed_source": speed,
		# Authored only. All 126 shipped spellbook locomotor rows carry the three
		# fields; the guard above refuses the summon outright if one is missing.
		"acceleration": float(locomotor["acceleration"]) * scale,
		"acceleration_source": float(locomotor["acceleration"]),
		"turn_rate_degrees_per_second": float(locomotor["turnRateDegreesPerSecond"]),
		"braking": float(locomotor["braking"]) * scale,
		"braking_source": float(locomotor["braking"]),
		"attack_range": attack_range * scale,
		"attack_range_source": attack_range,
		"minimum_attack_range": _spellbook_weapon_field(weapon, "MinimumAttackRange") * scale,
		"minimum_attack_range_source": _spellbook_weapon_field(weapon, "MinimumAttackRange"),
		"vision_range": vision * scale,
		"vision_range_source": vision,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / (sim.TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (sim.TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (sim.TICK_SECONDS * 1000.0))),
		"clip_size": int(_spellbook_weapon_field(weapon, "ClipSize")),
		"clip_reload_time_ms": clip_reload_ms,
		"formation_positions": positions,
		"default_weapon_mode": "default",
		"default_weapon_slot": String(member.get("weaponSlot", "")).to_lower(),
		"damage_type": String(damage_nugget.get("damagetype", "")).to_lower(),
		"provenance": {"source": "spellbook-summon", "object_id": String(target_leaf.get("id", ""))},
	}
	var aura_verdict := _spellbook_summon_aura_rules(member, modifier_leaves)
	if not bool(aura_verdict.get("ok", false)):
		return {"ok": false, "reason": String(aura_verdict.get("reason", "summon aura is not converted"))}
	var summon_auras: Array = aura_verdict.get("auras", []) as Array
	var summon_aura_skips: Array = aura_verdict.get("skipped_auras", []) as Array
	if move_only and summon_auras.is_empty() and summon_aura_skips.is_empty():
		return {"ok": false, "reason": "move-only summoned member '%s' has no converted aura payload" % member_id}
	if not summon_auras.is_empty():
		rule["summon_auras"] = summon_auras
	if not summon_aura_skips.is_empty():
		rule["summon_aura_skips"] = summon_aura_skips
	# Carry authored lifecycle policy into the synthesized unit rule. FADED is a
	# lifetime-only removal: combat death still follows the ordinary corpse rule.
	var destroy_die: Array = []
	for policy_value in Array(member.get("destroyDie", [])):
		var policy := policy_value as Dictionary
		destroy_die.append({
			"owner_role": "object",
			"death_types": String(policy.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(policy.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(policy.get("includedDeathTypes", [])).duplicate(),
		})
	if lifetime_death_type.to_upper() == "FADED":
		destroy_die.append({
			"owner_role": "object",
			"death_types": "NONE",
			"excluded_death_types": [],
			"included_death_types": ["FADED"],
		})
	if not destroy_die.is_empty():
		rule["destroy_die"] = destroy_die
	var keep_rows: Array = member.get("keepObjectDie", []) as Array
	if not keep_rows.is_empty():
		var keep := keep_rows[0] as Dictionary
		rule["keep_object_die"] = true
		rule["keep_object_die_policy"] = {
			"death_types": String(keep.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(keep.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(keep.get("includedDeathTypes", [])).duplicate(),
		}
	var permanent_locks: Array[String] = []
	for lock_value in Array(member.get("permanentWeaponLocks", [])):
		if typeof(lock_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has a malformed permanent weapon lock" % member_id}
		var lock_row := lock_value as Dictionary
		var slot := String(lock_row.get("slot", "")).to_lower()
		var lock_line: Variant = lock_row.get("line")
		if (
			slot != "primary"
			or String(lock_row.get("state", "")) != "LOCKED_PERMANENTLY"
			or String(lock_row.get("module", "")) != "LockWeaponCreate"
			or String(lock_row.get("sourceIni", "")).strip_edges() == ""
			or typeof(lock_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(lock_line), float(int(lock_line)))
			or int(lock_line) <= 0
			or permanent_locks.has(slot)
			or slot != String(rule.get("default_weapon_slot", ""))
		):
			return {"ok": false, "reason": "summoned member '%s' permanent weapon lock is unsupported" % member_id}
		permanent_locks.append(slot)
	if not permanent_locks.is_empty():
		rule["permanent_weapon_locks"] = permanent_locks
	var creation_leaf: Dictionary = (
		target_leaf
		if target_leaf.has("experienceLevelCreate")
		else member
	)
	var creation_grant: Variant = creation_leaf.get("experienceLevelCreate")
	if creation_grant != null:
		if typeof(creation_grant) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has malformed ExperienceLevelCreate evidence" % member_id}
		var grant := creation_grant as Dictionary
		var grant_rank: Variant = grant.get("rank")
		var grant_line: Variant = grant.get("line")
		if (
			String(grant.get("module", "")) != "ExperienceLevelCreate"
			or grant.get("mpOnly") != false
			or typeof(grant_rank) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_rank), float(int(grant_rank)))
			or int(grant_rank) < 1
			or String(grant.get("sourceIni", "")).strip_edges() == ""
			or typeof(grant_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_line), float(int(grant_line)))
			or int(grant_line) <= 0
		):
			return {"ok": false, "reason": "summoned member '%s' ExperienceLevelCreate evidence is unsupported" % member_id}
		# JSON numbers enter Godot as floats; the shared adapter deliberately
		# expects source line metadata as an integer. Normalize only the proven
		# integral creation line at this boundary.
		var experience_contract: Dictionary = (creation_leaf.get("experience", {}) as Dictionary).duplicate(true)
		var normalized_grant: Dictionary = (experience_contract.get("experienceLevelCreate", {}) as Dictionary).duplicate(true)
		if not normalized_grant.is_empty():
			normalized_grant["line"] = int(normalized_grant.get("line", 0))
			experience_contract["experienceLevelCreate"] = normalized_grant
		var creation_experience_rule = sim.PlayableUnitAdapter.experience_rule_from_contract(
			experience_contract
		)
		if (
			creation_experience_rule.is_empty()
			or int(creation_experience_rule.get("initial_rank", 0)) != int(grant_rank)
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience chain is unsupported" % member_id}
		var creation_effects: Dictionary = {}
		for level_value in Array(creation_experience_rule.get("levels", [])):
			var level_row := level_value as Dictionary
			if int(level_row.get("rank", 0)) == int(grant_rank):
				creation_effects = level_row
				break
		if (
			creation_effects.is_empty()
			or not Array(creation_effects.get("unsupported_modifiers", [])).is_empty()
			or float(creation_effects.get("production_multiplier", 1.0)) != 1.0
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience effects are unsupported" % member_id}
		rule["creation_experience_rank"] = int(grant_rank)
		rule["creation_experience_effects"] = creation_effects.duplicate(true)
	return {
		"ok": true,
		"rule": rule,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
		"lifetime_death_type": lifetime_death_type.to_upper(),
	}


func _spellbook_summon_aura_rules(member: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	var source_auras: Array = member.get("auras", []) as Array
	if source_auras.is_empty() and typeof(member.get("aura", null)) == TYPE_DICTIONARY:
		source_auras.append(member.get("aura", {}))
	var compiled: Array = []
	var skipped: Array = []
	for aura_value in source_auras:
		var verdict := _spellbook_one_summon_aura_rule(
			member, aura_value as Dictionary, modifier_leaves
		)
		if not bool(verdict.get("ok", false)):
			return verdict
		if bool(verdict.get("skip", false)):
			skipped.append({
				"modifier": String((aura_value as Dictionary).get("modifier", "")),
				"reason": String(verdict.get("reason", "summon-aura-skipped")),
			})
			continue
		compiled.append(verdict.get("aura", {}))
	return {"ok": true, "auras": compiled, "skipped_auras": skipped}


func _spellbook_one_summon_aura_rule(member: Dictionary, aura: Dictionary, modifier_leaves: Dictionary, allow_marker_modifiers: bool = false) -> Dictionary:
	## allow_marker_modifiers: retail authors at least one ModifierList whose stat
	## rows are all commented out and only its Duration survives — PalantirVision
	## (attributemodifier.ini:1139-1146). On a summon that is evidence of an
	## unconverted payload and stays fail-closed; on a reveal ping it is the
	## authored truth (the aura genuinely changes no stat), so the ping lane opts
	## in and the row is kept as a zero-modifier marker.
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "summon aura modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var duration_ms := 0.0
	var category := ""
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "summon aura modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane, same reasoning as the weather modifier above: a summon's
			# aura is the whole reason the summon is worth casting.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "summon aura modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "summon aura modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
		elif key == "ModelCondition" and value.strip_edges() != "":
			modifiers.append({"kind": "MODEL_CONDITION", "value": value.strip_edges()})
	var range_source := float(aura.get("range", 0.0))
	var refresh_ms := float(aura.get("refreshDelayMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	var modifiers_missing := modifiers.is_empty() and not allow_marker_modifiers
	if category not in ["LEADERSHIP", "SPELL", "DEBUFF"] or modifiers_missing or duration_ms <= 0.0 or range_source <= 0.0 or refresh_ms <= 0.0 or filter == "":
		return {"ok": false, "reason": "summon aura '%s' is incomplete (category=%s modifiers=%d duration_ms=%.1f range=%.1f refresh_ms=%.1f filter=%s)" % [modifier_id, category, modifiers.size(), duration_ms, range_source, refresh_ms, "present" if filter != "" else "missing"]}
	var starts_active := String(aura.get("startsActive", "Yes")).to_lower() != "no"
	var enabled_on_create := starts_active
	if not enabled_on_create:
		var creation_rank := int((member.get("experienceLevelCreate", {}) as Dictionary).get("rank", 0))
		for trigger_value in Array(aura.get("triggeredBy", [])):
			var trigger := String(trigger_value)
			if trigger.begins_with("Upgrade_ObjectLevel"):
				var level_text := trigger.trim_prefix("Upgrade_ObjectLevel")
				if level_text.is_valid_int() and creation_rank >= int(level_text):
					enabled_on_create = true
					break
	if not enabled_on_create:
		return {
			"ok": true,
			"skip": true,
			"reason": "upgrade-gated-aura-inert-at-summon-creation:%s" % modifier_id,
		}
	return {"ok": true, "aura": {
		"id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(duration_ms / (sim.TICK_SECONDS * 1000.0))),
		"refresh_ticks": maxi(1, roundi(refresh_ms / (sim.TICK_SECONDS * 1000.0))),
		"range_source": range_source,
		"filter": filter,
		"target_enemy": String(aura.get("targetEnemy", "")).to_lower() == "yes" if aura.has("targetEnemy") else null,
		"target_allies": String(aura.get("targetAllies", "")).to_lower() == "yes" if aura.has("targetAllies") else null,
		"starts_active": starts_active,
		"triggered_by": Array(aura.get("triggeredBy", [])).duplicate(),
	}}


func _spellbook_grove_chain(references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Resolve Elven Wood's authored geometry chain, or return {} for "none".
	##
	## Retail plants the grove through an ObjectCreationList, not through the
	## ElvenGrove object (which authors `Model = None` and is pure particle):
	## OCL_ElvenWoodSeed creates N ElvenWoodTreeSeed at an authored spread
	## radius, each seed's SlowDeath hatches OCL_ElvenWoodTree -> ElvenWoodTree,
	## which itself hatches OCL_ElvenWoodTreeSpawn -> ElvenWoodTreeOpt (the tree
	## that stays). Every number below is read off that chain; nothing is
	## assumed, and an unresolvable link simply yields no trees rather than an
	## invented grove.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {}
	var ocl: Dictionary = ocl_leaves.get(String(ocl_ids[0]), {}) as Dictionary
	if ocl.is_empty():
		return {}
	var creates: Array = ocl.get("createObjects", []) as Array
	if creates.is_empty() or typeof(creates[0]) != TYPE_DICTIONARY:
		return {}
	var create := creates[0] as Dictionary
	var names: Array = create.get("objects", []) as Array
	if names.is_empty():
		return {}
	var count := 1
	var min_radius := 0.0
	var max_radius := 0.0
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		match String(field_row.get("key", "")):
			"Count":
				count = maxi(1, int(field_row.get("resolved", 1)))
			"MinDistanceAFormation":
				min_radius = float(field_row.get("resolved", 0.0))
			"MaxDistanceFormation":
				max_radius = float(field_row.get("resolved", 0.0))
	# Follow every authored hatch to the object that actually remains standing.
	var object_id := String(names[0])
	var guard := 0
	while guard < 4:
		guard += 1
		var leaf: Dictionary = object_leaves.get(object_id, {}) as Dictionary
		if leaf.is_empty():
			return {}
		var hatch: Dictionary = leaf.get("hatch", {}) as Dictionary
		var hatch_ocl_id := String(hatch.get("ocl", ""))
		if hatch_ocl_id == "":
			break
		var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
		var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
		if hatch_creates.is_empty() or typeof(hatch_creates[0]) != TYPE_DICTIONARY:
			break
		var hatch_names: Array = (hatch_creates[0] as Dictionary).get("objects", []) as Array
		if hatch_names.is_empty():
			break
		object_id = String(hatch_names[0])
	if object_id == "" or not object_leaves.has(object_id):
		return {}
	if max_radius <= 0.0:
		max_radius = min_radius
	return {
		"object_id": object_id,
		"count": count,
		"min_radius_source": min_radius,
		"max_radius_source": max_radius,
	}


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary = {}, object_field: String = "ElvenGroveObject") -> Dictionary:
	## Terrain-taint family (Elven Wood, Taint, Isengard Taint): the converted
	## grove/taint-land leaf carries the whole effect — modifier leaf, refresh,
	## range, filter, the authored terrain cell type, and the object lifetime.
	var grove_id := String(field_values.get(object_field, ""))
	var grove: Dictionary = object_leaves.get(grove_id, {}) as Dictionary
	if grove.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' is not a converted leaf" % grove_id}
	var aura: Dictionary = grove.get("aura", {}) as Dictionary
	var deletion: Dictionary = grove.get("deletion", {}) as Dictionary
	if aura.is_empty() or deletion.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' aura or lifetime is not converted" % grove_id}
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "grove aura modifier '%s' is not a converted leaf" % modifier_id}
	# ROW ABSENT is not ROW UNREADABLE. A modifier list that never authors a
	# DAMAGE_MULT row means the neutral 1.0 (BFME2 `ModifierList
	# GenericArmorLeadership` is `ARMOR 50%` and nothing else,
	# attributemodifier.ini:159-166, and it is what the BFME2 ElvenGrove actually
	# binds: grove.ini:31 `BonusName = GenericArmorLeadership`). A row that IS
	# authored and cannot be read is still fail-closed, because that is the case
	# where half an authored buff goes missing in silence.
	var armor_mult := 1.0
	var damage_mult := 1.0
	var saw_armor := false
	var saw_damage := false
	var buff_duration_ms := 0.0
	# READABLE BUT UNMATCHED KIND. A row this resolver parses cleanly and whose
	# shape is a plain percent, but whose KIND is neither ARMOR nor DAMAGE_MULT
	# (retail authors e.g. `EXPERIENCE 150%` on buff leaves), used to fall
	# straight through the `match` below and vanish. That is the same silent drop
	# the SpecialPowerModule lane was fixed for in round 18 — it just took a
	# different route to it, so the fix did not reach here. Named and counted on
	# the effect instead, in the SAME shape that lane uses.
	var unsupported_rows: Array = []
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) == "Modifier":
			# FAIL-CLOSED, not `continue`: a row this resolver cannot read is a row
			# whose buff would silently go missing, and the grove would still plant.
			var parsed := _parse_modifier_row(String(field_row.get("value", "")))
			if (
				not parsed.get("ok", false)
				or not bool(parsed.get("supported", false))
				or String(parsed.get("shape", "")) != "percent"
			):
				return {"ok": false, "reason": "grove aura modifier '%s' has an unsupported modifier row '%s' (%s)" % [
					modifier_id, String(field_row.get("value", "")),
					String(parsed.get("reason", "shape is not a plain percent")),
				]}
			var percent := float(parsed.get("value", 0.0))
			match String(parsed.get("kind", "")):
				"ARMOR":
					armor_mult = percent
					saw_armor = true
				"DAMAGE_MULT":
					damage_mult = percent
					saw_damage = true
				_:
					unsupported_rows.append({
						"row": String(field_row.get("value", "")),
						"shape": String(parsed.get("shape", "")),
						"reason": "kind '%s' has no grove-aura runtime here" % String(parsed.get("kind", "")),
					})
		elif String(field_row.get("key", "")) == "Duration":
			buff_duration_ms = float(field_row.get("resolved", field_row.get("value", 0.0)))
	var aura_range := float(aura.get("range", 0.0))
	var lifetime_ms := float(deletion.get("maxMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	# AT LEAST ONE stat row is required, not both. Round 16 required both and it
	# was wrong twice over: it locked men/SpellBookElvenWoodMP, whose authored
	# leaf (GenericArmorLeadership) legitimately carries only ARMOR 50%, and it
	# conflated "absent" with "unreadable" — the case the round-16 note was
	# actually written about (a row that IS authored and cannot be parsed) is
	# still fail-closed, in the loop above. A leaf with NO readable stat row at
	# all still fails here: that is a grove with nothing to apply.
	if not (saw_armor or saw_damage) or armor_mult <= 0.0 or damage_mult <= 0.0 or buff_duration_ms <= 0.0 or aura_range <= 0.0 or lifetime_ms <= 0.0:
		return {"ok": false, "reason": "grove aura modifier '%s' carries no readable stat row (ARMOR seen=%s %.3f, DAMAGE_MULT seen=%s %.3f), range, or lifetime is not converted" % [
			modifier_id, saw_armor, armor_mult, saw_damage, damage_mult,
		]}
	if filter == "" or (not filter.contains("ANY") and not filter.contains("+")):
		return {"ok": false, "reason": "grove aura object filter is not converted"}
	return {"ok": true, "effect": {
		"kind": "grove_aura",
		"armor_mult": armor_mult,
		# Usually authored alongside ARMOR on the same modifier leaf (RotWK
		# GenericBuff: ARMOR 50% + DAMAGE_MULT 150%). When the leaf authors no
		# DAMAGE_MULT row at all (BFME2 GenericArmorLeadership) this stays at the
		# neutral 1.0, which is what "absent" means — a row that is present and
		# unreadable never reaches here.
		"damage_mult": damage_mult,
		# Which rows the leaf actually authored, so a consumer can tell an
		# authored-neutral 1.0 from a defaulted one.
		"authored_rows": {"ARMOR": saw_armor, "DAMAGE_MULT": saw_damage},
		# Named residual rows carried onto the effect so the runner and the report
		# can COUNT them instead of losing them — same key and same shape as the
		# SpecialPowerModule lane's.
		"unsupported_modifier_rows": unsupported_rows,
		"buff_duration_ticks": maxi(1, roundi(buff_duration_ms / 1000.0 / sim.TICK_SECONDS)),
		"range_source": aura_range,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
		"filter": filter,
		"modifier": modifier_id,
		"terrain_object_id": grove_id,
		# Retail's RequiredConditions cell type (TAINT / ELVEN_WOOD). Presentation
		# reads this to pick the ground decal; "" when the leaf does not author it.
		"terrain_condition": String(aura.get("requiredConditions", "")),
		# Presentation-only: the authored tree chain behind the grove. Absent
		# when the chain does not fully convert, and then no geometry is placed.
		"trees": _spellbook_grove_chain(references, object_leaves, ocl_leaves),
	}}


func _spellbook_field_ping_support(spawns: Array, modifier_leaves: Dictionary) -> Dictionary:
	## Reveal/field family (Farsight, Palantir Vision, Frozen Land, and the
	## Enshrouding Mist gap). Retail spawns a "ping": an object that is not a unit
	## at all — IMMOBILE, UNATTACKABLE, no weapon, no hatch — whose entire runtime
	## is a bounded VisionRange reveal plus optional AttributeModifierAuraUpdate
	## rows, ended by its authored LifetimeUpdate
	## (data/ini/object/system/system.ini:1905-1955, :1997-2062;
	## FrozenLandPing lifetime FROZEN_LAND_EFFECT_DURATION = gamedata.ini:3595).
	##
	## Returns {} when the spawn is not this shape, so the caller falls through to
	## the summon/structure resolvers unchanged.
	if spawns.is_empty():
		return {}
	var leaf: Dictionary = (spawns[0] as Dictionary).get("leaf", {}) as Dictionary
	if leaf.is_empty():
		return {}
	var kind_of: Array = leaf.get("kindOf", []) as Array
	var lifetime: Dictionary = leaf.get("lifetime", {}) as Dictionary
	var auras: Array = leaf.get("auras", []) as Array
	if auras.is_empty() and typeof(leaf.get("aura", null)) == TYPE_DICTIONARY:
		auras = [leaf.get("aura", {})]
	var vision_range := float(leaf.get("visionRange", 0.0))
	var is_ping := (
		kind_of.has("IMMOBILE")
		and kind_of.has("UNATTACKABLE")
		and (leaf.get("locomotor", {}) as Dictionary).is_empty()
		and Array(leaf.get("fireWeapons", [])).is_empty()
		and String(leaf.get("weaponId", "")) == ""
		and typeof(leaf.get("hatch", null)) != TYPE_DICTIONARY
		and (leaf.get("horde", {}) as Dictionary).is_empty()
		and float(lifetime.get("maxMs", 0.0)) > 0.0
		and (vision_range > 0.0 or not auras.is_empty())
	)
	if not is_ping:
		return {}
	var object_id := String(leaf.get("id", ""))
	var unconverted: Array = Array(leaf.get("unconvertedBehaviors", [])).duplicate()
	var invisibility_result := _spellbook_ping_invisibility_rules(leaf)
	if not bool(invisibility_result.get("ok", false)):
		return {"ok": false, "reason": "ping '%s' invisibility update is not executable: %s" % [object_id, String(invisibility_result.get("reason", ""))]}
	var invisibility_updates := invisibility_result.get("rules", []) as Array
	if unconverted.has("InvisibilityUpdate") and invisibility_updates.is_empty():
		return {"ok": false, "reason": "ping '%s' retains InvisibilityUpdate only as an unconverted marker" % object_id}
	if not invisibility_updates.is_empty():
		unconverted.erase("InvisibilityUpdate")
	var compiled_auras: Array = []
	var effect_auras := 0
	for aura_value in auras:
		var verdict := _spellbook_one_summon_aura_rule(
			leaf, aura_value as Dictionary, modifier_leaves, true
		)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": "ping '%s' aura is not converted: %s" % [object_id, String(verdict.get("reason", ""))]}
		if bool(verdict.get("skip", false)):
			continue
		var aura: Dictionary = verdict.get("aura", {}) as Dictionary
		compiled_auras.append(aura)
		if not Array(aura.get("modifiers", [])).is_empty():
			effect_auras += 1
	var reveal_source := vision_range
	if reveal_source <= 0.0 and effect_auras == 0 and invisibility_updates.is_empty():
		return {"ok": false, "reason": "ping '%s' has neither a converted VisionRange reveal nor a stat-bearing aura" % object_id}
	var radius_source := reveal_source
	for aura_value in compiled_auras:
		radius_source = maxf(radius_source, float((aura_value as Dictionary).get("range_source", 0.0)))
	for invisibility_value in invisibility_updates:
		radius_source = maxf(radius_source, float((invisibility_value as Dictionary).get("broadcast_range_source", 0.0)))
	return {"ok": true, "effect": {
		"kind": "field_ping",
		"object_id": object_id,
		"lifetime_ticks": maxi(1, roundi(float(lifetime.get("maxMs", 0.0)) / 1000.0 / sim.TICK_SECONDS)),
		"reveal_radius_source": reveal_source,
		"radius_source": radius_source,
		"auras": compiled_auras,
		"invisibility_updates": invisibility_updates,
		# Named residual gaps carried onto the effect so the runner and the report
		# can count them instead of losing them (StealthDetectorUpdate on the
		# Palantir/Farsight base object: this reveal does NOT unmask stealth).
		"unconverted_behaviors": unconverted,
	}}


func _spellbook_ping_invisibility_rules(leaf: Dictionary) -> Dictionary:
	var rows := leaf.get("invisibilityUpdates", []) as Array
	var rules: Array[Dictionary] = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY: return {"ok": false, "reason": "row is not a dictionary"}
		var row := row_value as Dictionary
		var type := String(row.get("invisibilityType", "")).to_upper()
		var update_ms := float(row.get("updatePeriodMs", 0.0))
		var broadcast_value: Variant = row.get("broadcast", false)
		var broadcast := false
		if typeof(broadcast_value) == TYPE_BOOL: broadcast = broadcast_value
		elif String(broadcast_value).to_upper() == "YES": broadcast = true
		if type not in ["CAMOUFLAGE", "STEALTH"] or update_ms <= 0.0: return {"ok": false, "reason": "type/update cadence missing"}
		if not broadcast: return {"ok": false, "reason": "field ping row does not author Broadcast Yes"}
		if typeof(row.get("broadcastRange")) not in [TYPE_INT, TYPE_FLOAT] or float(row.get("broadcastRange", -1.0)) < 0.0: return {"ok": false, "reason": "broadcast range unresolved"}
		if typeof(row.get("detectionRange")) not in [TYPE_INT, TYPE_FLOAT] or float(row.get("detectionRange", -1.0)) < 0.0: return {"ok": false, "reason": "detection range unresolved"}
		var filter_text := String(row.get("broadcastObjectFilter", "")).strip_edges()
		if filter_text == "" or (not filter_text.contains("ANY") and not filter_text.contains("ALL") and not filter_text.contains("+")): return {"ok": false, "reason": "broadcast object filter unresolved"}
		var starts_value: Variant = row.get("startsActive", false)
		var enabled := false
		if typeof(starts_value) == TYPE_BOOL: enabled = starts_value
		elif String(starts_value).to_upper() == "YES": enabled = true
		rules.append({"enabled":enabled, "update_ticks":maxi(1,sim._ship_contract_delay_ticks(update_ms)), "next_update_tick":sim.tick_index, "broadcast":true, "broadcast_range_source":float(row.get("broadcastRange")), "broadcast_filter":Array(filter_text.split(" ", false)), "invisibility_type":type, "forbidden_conditions":[], "forbidden_weapon_conditions":[], "hint_detectable_conditions":[], "options":[], "detection_range_source":float(row.get("detectionRange")), "become_fx_id":"", "exit_fx_id":"", "granted_ids":[], "tag":"field-ping", "source_ini":String(row.get("sourceIni", "")), "line":int(row.get("line", 0)), "unsupported_semantics":[]})
	return {"ok": true, "rules": rules}


func _spellbook_cloudbreak_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Cloud Break: WeatherDuration resolves in the doc; the enemy disruption
	## is the module's authored affects filter over the weather duration.
	var duration_ms := float(field_resolved.get("WeatherDuration", 0.0))
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	var weather := String(field_values.get("ChangeWeather", ""))
	if duration_ms <= 0.0 or affects == "" or weather == "":
		return {"ok": false, "reason": "cloud break duration, affects filter, or weather is not converted"}
	var required := {
		"ReEnableAntiCategory": "Yes",
		"AttributeModifierWeatherBased": "Yes",
		"SunbeamObject": "CloudBreakSunbeam",
	}
	for authored in required:
		if String(field_values.get(authored, "")) != String(required[authored]):
			return {"ok": false, "reason": "cloud break %s is not the supported authored value" % authored}
	if int(field_resolved.get("ObjectSpacing", -1)) != 300:
		return {"ok": false, "reason": "cloud break ObjectSpacing is not the supported authored value"}
	return {"ok": true, "effect": {
		"kind": "cloudbreak_stun",
		"duration_ticks": maxi(1, roundi(duration_ms / 1000.0 / sim.TICK_SECONDS)),
		"affects": affects,
		"weather": weather,
		"sunbeam_object_id": String(field_values.get("SunbeamObject", "")),
		"object_spacing_source": float(field_resolved.get("ObjectSpacing", 0.0)),
	}}


