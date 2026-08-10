extends RefCounted
## Create-a-Hero: saved hero profiles, the attribute arithmetic, and the
## hero-roster document a saved profile becomes.
##
## THREE THINGS LIVE HERE AND NOTHING ELSE DOES.
##
## 1. THE PROFILE. What the player made, as a schema-versioned JSON document
##    under `user://`. Like retail's `.cah` it stores ONLY a class/subclass pair
##    and one small integer per attribute group - not one stat. Every number a
##    created hero has is derived, at load, from the class table the importer
##    compiled out of retail INI. That is retail's own idiom (a `.cah` is a build
##    order, not a hero) and it is why a rebalanced pack changes every existing
##    saved hero instead of leaving them frozen at the numbers they were made
##    with.
##
##    One difference from `.cah`, deliberate: this stores the 1-based upgrade
##    STEP (`Upgrade_ArmorAttribute16` -> 16) where a `.cah` stores the 0-based
##    `GroupOrder` (15). The INI names steps from 1 and every citation in the
##    corpus reads that way, so storing the step keeps the file legible against
##    the source. `groupOrder = step - 1` is the whole conversion, and the
##    importer asserts the two agree for all 100 upgrades.
##
## 2. THE ATTRIBUTE ARITHMETIC. One point per step above the class minimum, and a
##    legal loadout spends the budget exactly. That rule is authored nowhere; it
##    is recovered from the fact that all sixteen retail subclasses spend their
##    whole `SpendableAttributePoints` on their authored default loadout. See
##    `attribute_spend` and `validate_profile`.
##
## 3. THE ROSTER DOCUMENT. A created hero has to reach the fortress the same way
##    every other hero does, so `roster_document` emits an
##    `openbfme.playable-unit-runtime` - the schema the faction manifest, the
##    runtime adapter and the HUD already consume. Nothing downstream is taught
##    about Create-a-Hero; a created hero IS a playable unit whose numbers were
##    computed here instead of read off an Object.
##
## ALSO HERE, when the mounted system document carries them: appearance
## upgrade choices, a special-power catalog (free selection up to
## maxPowerSlots), awards lists, and tracking-stat keys. Older packs that only
## ship attribute ladders still validate; the screen simply shows empty
## catalogs for the missing lanes.

const PROFILE_SCHEMA := "openbfme.cah-hero-profile"
const PROFILE_SCHEMA_VERSION := 0

const SYSTEM_SCHEMA := "openbfme.cah-system-runtime"
const SYSTEM_SCHEMA_VERSION := 0

const PROFILE_DIR := "user://cah-heroes"

## Cap on saved heroes. Retail's front end has a fixed hero list too; the number
## matters less than there being one, because this directory is swept on every
## visit to MY HEROES and an unbounded sweep is an unbounded stall.
const MAX_PROFILES := 64

const MAX_NAME_LENGTH := 24
const MAX_PROFILE_BYTES := 64 * 1024

## The one shape a hero id may have. Generated here, and required of every
## profile that reaches a roster - including one that arrived over a wire.
const HERO_ID_LENGTH := 24
const HERO_ID_ALPHABET := "0123456789abcdef"

## The modifier kinds the five attribute groups emit, and what each scales.
## `ARMOR` is a damage-taken scalar (INNATE_ARMOR), so a HIGHER armour step
## means MORE damage taken unless the sim inverts it - which is exactly what
## retail authors and why this module reports the scalar rather than a "defence"
## number it invented.
const KIND_ARMOR := "ARMOR"
const KIND_DAMAGE_MULT := "DAMAGE_MULT"
const KIND_HEALTH_MULT := "HEALTH_MULT"
const KIND_AUTO_HEAL := "AUTO_HEAL"
const KIND_VISION := "VISION"
const KIND_SHROUD_CLEARING := "SHROUD_CLEARING"

## What a created hero fights with when the mounted pack predates the retail
## baseline join - i.e. when the compiled system carries no `objectBaseline` and
## its weapon appearance options carry no `combat`.
##
## THESE NUMBERS ARE INVENTED and every document built on them says so, in
## `registration.createAHero.limitations`. They exist so an older pack fields a
## hero that moves and swings instead of failing the roster; they are not a
## retail citation and nothing may treat them as one.
const FALLBACK_SPEED := 50.0
const FALLBACK_COMBAT := {
	"attackRange": 40.0,
	"minimumAttackRange": 0.0,
	"delayBetweenShotsMs": 1250.0,
	"preAttackDelayMs": 1000.0,
	"firingDurationMs": 1400.0,
	"damage": 150.0,
}
const FALLBACK_MOVEMENT := {
	"acceleration": 210.0,
	"braking": 210.0,
	"turnRateDegreesPerSecond": 720.0,
}

const LIMITATION_OBJECT_BASELINE := "cah-object-baseline-absent"
const LIMITATION_WEAPON_COMBAT := "cah-weapon-combat-absent"
const LIMITATION_TURN_RATE := "cah-turn-rate-not-stated"


static func system_is_valid(system: Variant) -> bool:
	if typeof(system) != TYPE_DICTIONARY:
		return false
	var doc := system as Dictionary
	if String(doc.get("schema", "")) != SYSTEM_SCHEMA:
		return false
	if int(doc.get("schemaVersion", -1)) != SYSTEM_SCHEMA_VERSION:
		return false
	var registration: Dictionary = doc.get("registration", {}) as Dictionary
	var classes: Array = registration.get("classes", []) as Array
	var groups: Array = registration.get("attributeGroups", []) as Array
	return not classes.is_empty() and not groups.is_empty()


static func sub_class_row(system: Dictionary, class_index: int, sub_index: int) -> Dictionary:
	## The authored subclass, or {} when the profile names one that no longer
	## exists. A pack that dropped a class must not crash the screen; it must
	## make every hero built on that class visibly unavailable.
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	for class_value in (registration.get("classes", []) as Array):
		var class_row := class_value as Dictionary
		if int(class_row.get("classIndex", -1)) != class_index:
			continue
		for sub_value in (class_row.get("subClasses", []) as Array):
			var sub_row := sub_value as Dictionary
			if int(sub_row.get("subClassIndex", -1)) == sub_index:
				return sub_row
	return {}


static func class_row(system: Dictionary, class_index: int) -> Dictionary:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	for class_value in (registration.get("classes", []) as Array):
		var row := class_value as Dictionary
		if int(row.get("classIndex", -1)) == class_index:
			return row
	return {}


static func attribute_groups(system: Dictionary) -> Array:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return registration.get("attributeGroups", []) as Array


static func default_attributes(sub_row: Dictionary) -> Dictionary:
	## The class's own authored loadout, which by the budget rule already spends
	## every point. This is what a new hero starts on, so a player who allocates
	## nothing still gets the hero EA shipped rather than a minimum-stat one.
	var out := {}
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		out[String(row.get("groupName", ""))] = int(row.get("defaultStep", 0))
	return out


static func attribute_spend(sub_row: Dictionary, attributes: Dictionary) -> int:
	## Points a loadout spends: one per step above the class minimum.
	## THE ONE IMPLEMENTATION OF THE RULE ON THIS SIDE. The importer has the
	## other, and its tests pin them to the same arithmetic.
	var total := 0
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		var minimum := int(row.get("minStep", 0))
		var chosen := int(attributes.get(group, minimum))
		total += chosen - minimum
	return total


static func attribute_budget(sub_row: Dictionary) -> int:
	return int(sub_row.get("spendableAttributePoints", 0))


static func step_modifiers(system: Dictionary, group_name: String, step: int) -> Array:
	## The authored modifier rows for one step of one group, or [] if either the
	## group or the step is outside what the pack compiled.
	for group_value in attribute_groups(system):
		var group := group_value as Dictionary
		if String(group.get("groupName", "")) != group_name:
			continue
		var steps: Array = group.get("steps", []) as Array
		for step_value in steps:
			var step_row := step_value as Dictionary
			if int(step_row.get("step", -1)) == step:
				return step_row.get("modifiers", []) as Array
		return []
	return []


static func modifier_value(system: Dictionary, attributes: Dictionary, kind: String, fallback: float) -> float:
	## The value of one modifier KIND across the whole loadout.
	##
	## Each attribute group contributes at most one row of any given kind, and
	## no two groups emit the same kind, so this is a lookup rather than a
	## product. Returning `fallback` when nothing is found is what makes an
	## unknown kind neutral instead of zero - a zeroed HEALTH_MULT would give a
	## created hero no health at all.
	for group_name_value in attributes.keys():
		var group_name := String(group_name_value)
		var step := int(attributes[group_name_value])
		for modifier_value_row in step_modifiers(system, group_name, step):
			var row := modifier_value_row as Dictionary
			if String(row.get("kind", "")) == kind:
				return float(row.get("value", fallback))
	return fallback


static func object_baseline(system: Dictionary) -> Dictionary:
	## The retail CreateAHero Object's own movement/health numbers, when the
	## importer compiled them. Empty on a pack that predates the join, which is
	## what puts a document on the invented fallbacks and names the limitation.
	##
	## Three homes are accepted because the compiler states the baseline
	## alongside the object row and a reader that pinned one path would go blind
	## the first time it moved.
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	for holder_value in [registration, registration.get("system", {}), system]:
		if typeof(holder_value) != TYPE_DICTIONARY:
			continue
		var candidate: Variant = (holder_value as Dictionary).get("objectBaseline")
		if typeof(candidate) == TYPE_DICTIONARY and not (candidate as Dictionary).is_empty():
			return candidate as Dictionary
	return {}


static func weapon_group_name(system: Dictionary) -> String:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return String((registration.get("system", {}) as Dictionary).get("weaponGroupName", ""))


static func weapon_appearance_row(system: Dictionary, appearance: Dictionary) -> Dictionary:
	## The appearance option the hero wears in the weapon group. Retail prices
	## and models the weapon as bling, and the weapon IS the hero's combat, so
	## the chosen option is where damage, range and reload come from.
	var group := weapon_group_name(system)
	if group == "":
		return {}
	var upgrade := String(appearance.get(group, ""))
	if upgrade == "":
		return {}
	for row_value in appearance_options(system):
		var row := row_value as Dictionary
		if String(row.get("upgradeName", "")) == upgrade:
			return row
	return {}


static func combat_baseline(system: Dictionary, profile: Dictionary) -> Dictionary:
	## The source-unit combat and locomotion a created hero spawns with, plus
	## the named limitations of whatever the mounted pack could not supply.
	##
	## SOURCE UNITS, deliberately. Every playable-unit document states retail
	## numbers and the runtime adapter scales them by the map's transform scale
	## exactly once; converting here would scale them twice.
	var limitations: Array[String] = []
	var baseline := object_baseline(system)
	if baseline.is_empty():
		limitations.append(LIMITATION_OBJECT_BASELINE)
	var weapon := weapon_appearance_row(
		system, profile.get("appearance", {}) as Dictionary
	)
	var weapon_combat: Dictionary = weapon.get("combat", {}) as Dictionary
	if weapon_combat.is_empty() or not weapon_combat.has("damage"):
		limitations.append(LIMITATION_WEAPON_COMBAT)
		weapon_combat = {}
	var combat := FALLBACK_COMBAT.duplicate()
	for key in weapon_combat.keys():
		combat[String(key)] = weapon_combat[key]
	# The compiler spells the pre-attack wind-up `preAttackMs`; the runtime
	# contract every playable-unit document is read against spells it
	# `preAttackDelayMs`. Same number, and translating it here is the difference
	# between a hero on its real weapon and a hero on its real weapon with one
	# invented timing quietly left in the middle of it.
	for spelling in [["preAttackMs", "preAttackDelayMs"]]:
		if weapon_combat.has(spelling[0]):
			combat[spelling[1]] = weapon_combat[spelling[0]]
	var movement := FALLBACK_MOVEMENT.duplicate()
	# Locomotion arrives either as a movement sub-dictionary or flat on the
	# baseline; both spellings are read so a compiler that moves it does not
	# silently drop the hero back onto invented numbers.
	var baseline_movement: Dictionary = baseline.get("movement", {}) as Dictionary
	for key in baseline_movement.keys():
		movement[String(key)] = baseline_movement[key]
	for spelling in [
		["acceleration", "acceleration"],
		["accelerationMs", "acceleration"],
		["braking", "braking"],
		["brakingMs", "braking"],
		["turnRateDegreesPerSecond", "turnRateDegreesPerSecond"],
	]:
		if baseline.has(spelling[0]):
			movement[spelling[1]] = float(baseline[spelling[0]])
	if not baseline.is_empty() and not baseline.has("turnRateDegreesPerSecond") \
		and not baseline_movement.has("turnRateDegreesPerSecond"):
		# A retail Locomotor states `TurnTime` - how long a turn TAKES - and the
		# simulation wants a rate in degrees per second. The arc that time covers
		# is not stated anywhere this module can read, so the turn rate stays on
		# its invented value and SAYS SO rather than being derived from a guess
		# about what TurnTime means.
		limitations.append(LIMITATION_TURN_RATE)
	return {
		"speed": float(baseline.get("speed", FALLBACK_SPEED)),
		"baseHealth": float(baseline.get("baseHealth", 0.0)),
		"combat": combat,
		"movement": movement,
		"weaponUpgradeName": String(weapon.get("upgradeName", "")),
		"limitations": limitations,
	}


static func base_health_value(system: Dictionary) -> float:
	## The unmultiplied health a created hero starts from: the compiled object
	## baseline when the pack carries one, else the CreateAHero Object's own
	## `MaxHealth`, which every pack has stated since the first compile.
	var baseline := object_baseline(system)
	var from_baseline := float(baseline.get("baseHealth", 0.0))
	if from_baseline > 0.0:
		return from_baseline
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return float((registration.get("system", {}) as Dictionary).get("maxHealth", 0.0))


static func computed_stats(
	system: Dictionary,
	sub_row: Dictionary,
	attributes: Dictionary,
	powers: Array = []
) -> Dictionary:
	## Every number a created hero spawns with, and the multiplier each came
	## from. The `*Multiplier` keys are kept alongside the products so a runner
	## - or a player reading a tooltip - can see the arithmetic rather than
	## having to trust it.
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	var base: Dictionary = registration.get("system", {}) as Dictionary

	var health_mult := modifier_value(system, attributes, KIND_HEALTH_MULT, 1.0)
	var vision_mult := modifier_value(system, attributes, KIND_VISION, 1.0)
	var shroud_mult := modifier_value(system, attributes, KIND_SHROUD_CLEARING, 1.0)
	var armor_scalar := modifier_value(system, attributes, KIND_ARMOR, 1.0)
	var damage_mult := modifier_value(system, attributes, KIND_DAMAGE_MULT, 1.0)
	var auto_heal := modifier_value(system, attributes, KIND_AUTO_HEAL, 1.0)

	var base_health := base_health_value(system)
	var base_vision := float(base.get("visionRange", 0.0))
	var base_shroud := float(base.get("shroudClearingRange", 0.0))

	return {
		"health": int(round(base_health * health_mult)),
		"healthMultiplier": health_mult,
		"visionRange": base_vision * vision_mult,
		"visionMultiplier": vision_mult,
		"shroudClearingRange": base_shroud * shroud_mult,
		"shroudClearingMultiplier": shroud_mult,
		"armorScalar": armor_scalar,
		"damageMultiplier": damage_mult,
		"autoHealMultiplier": auto_heal,
		# BUILD COST IS THE BASE PLUS THE POWERS, which is what the retail
		# POWERS screen totals in its Build Cost field. Attributes are free;
		# only powers are priced (CreateAHeroUICostIfSelected).
		"buildCost": int(base.get("buildCost", 0)) + power_cost(system, powers),
		"basePowerCost": power_cost(system, powers),
		"baseBuildCost": int(base.get("buildCost", 0)),
		"buildTimeSeconds": float(base.get("buildTimeSeconds", 0.0)),
		"commandPoints": int(base.get("commandPoints", 0)),
		"reviveCost": int(base.get("reviveCost", 0)),
	}


static func new_profile(system: Dictionary, name: String, class_index: int, sub_index: int) -> Dictionary:
	var sub_row := sub_class_row(system, class_index, sub_index)
	return {
		"schema": PROFILE_SCHEMA,
		"schemaVersion": PROFILE_SCHEMA_VERSION,
		"heroId": _new_hero_id(),
		"name": sanitize_name(name),
		"classIndex": class_index,
		"subClassIndex": sub_index,
		"attributes": default_attributes(sub_row),
		"appearance": default_appearance(sub_row),
		"powers": default_powers(system, class_index),
		"awards": [],
		"trackingStats": {},
		# WHICH TABLE THIS HERO WAS BUILT AGAINST. Not a lock - the hero is
		# revalidated against whatever pack is mounted now - but it is how a
		# "this hero was made on different content" notice can ever be shown
		# instead of the numbers silently moving.
		"systemDescriptorSha256": String(system.get("descriptorSha256", "")),
	}


static func max_power_slots(system: Dictionary) -> int:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	var slots := int(registration.get("maxPowerSlots", 15))
	return maxi(1, slots)


static func power_catalog(system: Dictionary) -> Array:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return registration.get("powerCatalog", []) as Array


static func class_upgrade_name(system: Dictionary, class_index: int) -> String:
	return String(class_row(system, class_index).get("upgradeName", ""))


static func power_trees_for_class(system: Dictionary, class_index: int) -> Array:
	## The rows of the CUSTOMIZE HERO POWERS grid for one class.
	##
	## A tree is offered when ANY of its levels names this class, and each level
	## keeps its own binding - retail authors trees whose upper tiers narrow to
	## fewer classes than their root, so filtering whole trees on the root's
	## binding alone would offer powers a class can never take.
	var upgrade := class_upgrade_name(system, class_index)
	if upgrade == "":
		return []
	var out: Array = []
	for tree_value in power_catalog(system):
		var tree := tree_value as Dictionary
		var levels: Array = []
		for level_value in (tree.get("levels", []) as Array):
			var level := level_value as Dictionary
			if _names_class(level.get("allowedClassUpgrades", []) as Array, upgrade):
				levels.append(level)
		if levels.is_empty():
			continue
		var copy := tree.duplicate(true)
		copy["levels"] = levels
		out.append(copy)
	return out


static func _names_class(allowed: Array, upgrade: String) -> bool:
	for value in allowed:
		if String(value).to_lower() == upgrade.to_lower():
			return true
	return false


static func power_row(system: Dictionary, power_id: String) -> Dictionary:
	for tree_value in power_catalog(system):
		for level_value in ((tree_value as Dictionary).get("levels", []) as Array):
			var level := level_value as Dictionary
			if String(level.get("powerId", "")) == power_id:
				return level
	return {}


static func power_cost(system: Dictionary, powers: Array) -> int:
	## What the selected powers add to the hero's price. Retail shows the sum on
	## the POWERS screen as Build Cost, so this is the same arithmetic the
	## player is reading.
	var total := 0
	for power_value in powers:
		total += int(power_row(system, String(power_value)).get("costIfSelected", 0))
	return total


static func power_refusals(
	system: Dictionary, class_index: int, powers: Array
) -> Array[String]:
	## Every reason this power loadout is illegal, or [] when it is not.
	##
	## THREE RULES, ALL AUTHORED, none invented here:
	## 1. the power must name this class in CreateAHeroUIAllowableUpgrades;
	## 2. its prerequisite must also be selected - the arrows on the screen are
	##    a real constraint, not decoration, so "Great Call Reinforcements"
	##    without "Improved" is refused; and
	## 3. no more than maxPowerSlots.
	## The required hero level is NOT checked here: a level-10 power is a legal
	## thing to BUILD a hero with, and it simply stays greyed out in the palette
	## until the hero reaches that level in a match.
	var refusals: Array[String] = []
	var catalog := power_catalog(system)
	if catalog.is_empty():
		return refusals
	var upgrade := class_upgrade_name(system, class_index)
	var selected := {}
	for power_value in powers:
		selected[String(power_value)] = true
	if powers.size() > max_power_slots(system):
		refusals.append(
			"the loadout equips %d powers; the limit is %d"
			% [powers.size(), max_power_slots(system)]
		)
	for power_value in powers:
		var power_id := String(power_value)
		if power_id == "":
			continue
		var row := power_row(system, power_id)
		if row.is_empty():
			refusals.append("%s is not in the mounted power catalog" % power_id)
			continue
		if upgrade != "" and not _names_class(
			row.get("allowedClassUpgrades", []) as Array, upgrade
		):
			refusals.append("%s is not offered to this class" % power_id)
		var prerequisite := String(row.get("prerequisitePowerId", ""))
		if prerequisite != "" and not selected.has(prerequisite):
			refusals.append("%s requires %s" % [power_id, prerequisite])
	return refusals


## What a power carries when the importer could not compile its behaviour.
## Shaped like a compiled row so no consumer branches on presence.
const _UNCOMPILED_IMPLEMENTATION := {
	"status": "unimplemented",
	"reason": "no compiled SpecialPower behaviour for this button",
	"limitations": [],
}


static func ability_rows(system: Dictionary, powers: Array) -> Array:
	## The selected powers as the `registration.abilities` rows the runtime
	## adapter projects. Slots are 1-based and follow selection order, which is
	## the numbering retail shows in the Current Powers list.
	var out: Array = []
	var slot := 0
	for power_value in powers:
		var power_id := String(power_value)
		var row := power_row(system, power_id)
		if row.is_empty():
			continue
		slot += 1
		out.append({
			"id": power_id,
			"slot": slot,
			"specialPowerId": String(row.get("specialPowerId", "")),
			# The importer states targeting when it compiled the effect; the
			# button's own Options are the fallback for a power it could not.
			"targeting": (
				String(row.get("targeting", ""))
				if String(row.get("targeting", "")) != ""
				else _targeting_for(row.get("options", []) as Array)
			),
			"cooldownMs": float(row.get("reloadTimeMs", 0)),
			"levelGate": {"requiredLevel": int(row.get("requiredHeroLevel", 1))},
			"button": {
				"iconIds": [String(row.get("buttonImageId", ""))],
				"labelIds": [String(row.get("nameStringId", ""))],
				"tooltipIds": [String(row.get("descriptionStringId", ""))],
				"radiusCursorType": String(row.get("radiusCursorType", "")),
				"options": (row.get("options", []) as Array).duplicate(),
			},
			# WHAT THE POWER DOES, as the importer compiled it out of the
			# CreateAHero Object's SpecialPower behaviour modules - the same
			# lane that compiles every retail hero's abilities. A power whose
			# effect did not compile carries the neutral shape the importer
			# supplies and is reported not castable, so it stays selectable,
			# priced and gated without pretending to fire.
			"effect": (row.get("effect", {"kind": "none"}) as Dictionary).duplicate(true),
			"implementation": (
				row.get("implementation", _UNCOMPILED_IMPLEMENTATION) as Dictionary
			).duplicate(true),
			"initiateSoundId": String(row.get("initiateSoundId", "")),
			"unitSpecificSoundId": String(row.get("unitSpecificSoundId", "")),
		})
	return out


static func _targeting_for(options: Array) -> String:
	## Retail states targeting on the button's Options, and the runtime contract
	## admits three shapes. Object targeting of any allegiance projects onto
	## `enemy-object`, which is the only object-targeting shape the contract has.
	for option_value in options:
		var option := String(option_value)
		if option in [
			"NEED_TARGET_ENEMY_OBJECT",
			"NEED_TARGET_NEUTRAL_OBJECT",
			"NEED_TARGET_ALLY_OBJECT",
		]:
			return "enemy-object"
	for option_value in options:
		if String(option_value) == "NEED_TARGET_POS":
			return "point"
	return "self"


static func experience_ladder(system: Dictionary) -> Dictionary:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return registration.get("experience", {}) as Dictionary


static func experience_contract(system: Dictionary) -> Dictionary:
	## The compiled level chain, in the shape the runtime adapter's
	## ExperienceLevel projection accepts. Returns {} when the mounted pack
	## predates the ladder, which makes the hero unlevelled rather than unfieldable.
	var ladder := experience_ladder(system)
	var levels: Array = ladder.get("levels", []) as Array
	if levels.is_empty():
		return {}
	return {
		"status": "compiled",
		"maxLevel": int(ladder.get("maxLevel", levels.size())),
		"initialRank": int(ladder.get("initialRank", 1)),
		"levels": levels.duplicate(true),
		"sourceIni": "data/ini/experiencelevels_createahero.inc",
	}


static func model_binding(sub_row: Dictionary, surface: String) -> Dictionary:
	## The mesh this subclass wears on one surface ("battlefield" or
	## "creationScreen"). Empty when the pack predates the model join.
	var models: Dictionary = sub_row.get("models", {}) as Dictionary
	return models.get(surface, {}) as Dictionary


static func appearance_groups(system: Dictionary) -> Array:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return registration.get("appearanceGroups", []) as Array


static func appearance_options(system: Dictionary) -> Array:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	return registration.get("appearanceOptions", []) as Array


static func default_appearance(sub_row: Dictionary) -> Dictionary:
	## First authored upgrade per appearance group, or empty when the pack has
	## no appearanceChoices (attribute-only systems).
	var out := {}
	var choices: Dictionary = sub_row.get("appearanceChoices", {}) as Dictionary
	for group_value in choices.keys():
		var upgrades: Array = choices[group_value] as Array
		if upgrades.is_empty():
			continue
		out[String(group_value)] = String(upgrades[0])
	return out


static func default_powers(_system: Dictionary, _class_index: int) -> Array:
	## Nothing. Retail opens CUSTOMIZE HERO POWERS with an empty Current Powers
	## list and the prompt "Select a Level 1 power to go in your Hero's Current
	## Powers", so seeding a selection would both mis-state the screen and
	## silently charge the player for powers they never chose.
	return []


static func option_label_for_upgrade(system: Dictionary, upgrade_name: String) -> String:
	for row_value in appearance_options(system):
		var row := row_value as Dictionary
		if String(row.get("upgradeName", "")) == upgrade_name:
			return _tail_label(String(row.get("nameStringId", upgrade_name)))
	return _tail_label(upgrade_name)


static func _tail_label(string_id: String) -> String:
	if string_id == "":
		return "?"
	var tail := string_id.get_slice(":", string_id.get_slice_count(":") - 1)
	for prefix in ["BlingName_", "SubClassName_", "ClassName_", "CreateAHero_"]:
		if tail.begins_with(prefix):
			tail = tail.substr(prefix.length())
	return tail.replace("_", " ")


static func sanitize_name(name: String) -> String:
	## Names are shown in the HUD and written into a filename-adjacent document,
	## so they are stripped to printable text and bounded. Refusing the empty
	## result is the caller's job; this only ever narrows.
	var out := ""
	for character in name.strip_edges():
		var code := character.unicode_at(0)
		if code >= 32 and code != 127:
			out += character
		if out.length() >= MAX_NAME_LENGTH:
			break
	return out


static func validate_profile(system: Dictionary, profile: Dictionary) -> Array[String]:
	## Every reason this profile cannot be used, or [] when it can.
	##
	## Returns ALL of them rather than the first, because the screen shows them
	## together: a player who over-spent AND picked a dropped class should be
	## told both at once instead of fixing one to discover the other.
	var refusals: Array[String] = []
	if String(profile.get("schema", "")) != PROFILE_SCHEMA:
		refusals.append("not a %s document" % PROFILE_SCHEMA)
		return refusals
	if int(profile.get("schemaVersion", -1)) != PROFILE_SCHEMA_VERSION:
		refusals.append(
			"profile schema version %s is not %d"
			% [profile.get("schemaVersion", "?"), PROFILE_SCHEMA_VERSION]
		)
		return refusals
	if sanitize_name(String(profile.get("name", ""))) == "":
		refusals.append("the hero has no name")
	if not hero_id_valid(String(profile.get("heroId", ""))):
		# NOT MERELY NON-EMPTY. A hero id becomes the object id, the command id,
		# the sim's unit-type key, a filename and a line in the exclusion log, and
		# in a match it arrives from another machine. Constraining it to exactly
		# what `_new_hero_id` produces closes path traversal, command-id forgery
		# and log injection with one shape check, and removes the case-tie that a
		# case-insensitive ordering would otherwise leave in the roster sort.
		refusals.append("the hero id is not a %d-character lowercase hex id" % HERO_ID_LENGTH)

	var class_index := int(profile.get("classIndex", -1))
	var sub_index := int(profile.get("subClassIndex", -1))
	var sub_row := sub_class_row(system, class_index, sub_index)
	if sub_row.is_empty():
		refusals.append(
			"the mounted content has no class %d subclass %d" % [class_index, sub_index]
		)
		return refusals

	var attributes: Dictionary = profile.get("attributes", {}) as Dictionary
	var authored: Array = sub_row.get("attributes", []) as Array
	for row_value in authored:
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		if not attributes.has(group):
			refusals.append("no value for %s" % group)
			continue
		var step := int(attributes[group])
		var low := int(row.get("minStep", 0))
		var high := int(row.get("maxStep", 0))
		if step < low or step > high:
			refusals.append(
				"%s is at step %d, outside the authored %d..%d" % [group, step, low, high]
			)
	for group_value in attributes.keys():
		var group_name := String(group_value)
		var known := false
		for row_value in authored:
			if String((row_value as Dictionary).get("groupName", "")) == group_name:
				known = true
				break
		if not known:
			refusals.append("%s is not an attribute of this class" % group_name)

	if refusals.is_empty():
		# THE BUDGET, CHECKED LAST so a loadout that is out of range is reported
		# as out of range rather than as an arithmetic mismatch caused by it.
		var spend := attribute_spend(sub_row, attributes)
		var budget := attribute_budget(sub_row)
		if spend != budget:
			refusals.append(
				"the loadout spends %d of %d attribute points" % [spend, budget]
			)

	var powers: Array = profile.get("powers", []) as Array
	refusals.append_array(power_refusals(system, class_index, powers))

	var appearance: Dictionary = profile.get("appearance", {}) as Dictionary
	var choices: Dictionary = sub_row.get("appearanceChoices", {}) as Dictionary
	if not choices.is_empty():
		for group_value in appearance.keys():
			var group := String(group_value)
			var upgrade := String(appearance[group_value])
			var allowed: Array = choices.get(group, []) as Array
			var ok := false
			for candidate in allowed:
				if String(candidate) == upgrade:
					ok = true
					break
			if not ok and upgrade != "":
				refusals.append("%s is not a legal %s upgrade for this subclass" % [upgrade, group])
	return refusals


static func profile_path(hero_id: String) -> String:
	return "%s/%s.json" % [PROFILE_DIR, hero_id]


static func save_profile(profile: Dictionary) -> String:
	## Writes one profile, returning "" on success or a reason.
	var hero_id := String(profile.get("heroId", ""))
	if not _is_safe_hero_id(hero_id):
		return "hero id %s is not a plain hexadecimal id" % hero_id
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(PROFILE_DIR)):
		var error := DirAccess.make_dir_recursive_absolute(
			ProjectSettings.globalize_path(PROFILE_DIR)
		)
		if error != OK:
			return "could not create %s (error %d)" % [PROFILE_DIR, error]
	var file := FileAccess.open(profile_path(hero_id), FileAccess.WRITE)
	if file == null:
		return "could not write %s (error %d)" % [profile_path(hero_id), FileAccess.get_open_error()]
	file.store_string(JSON.stringify(profile, "  ", true))
	file.close()
	return ""


static func load_profile(hero_id: String) -> Dictionary:
	if not _is_safe_hero_id(hero_id):
		return {}
	return _read_profile(profile_path(hero_id))


static func delete_profile(hero_id: String) -> bool:
	if not _is_safe_hero_id(hero_id):
		return false
	var path := ProjectSettings.globalize_path(profile_path(hero_id))
	if not FileAccess.file_exists(profile_path(hero_id)):
		return false
	return DirAccess.remove_absolute(path) == OK


static func load_profiles() -> Array[Dictionary]:
	## Every saved profile, oldest id first, bounded by MAX_PROFILES.
	## A file that does not parse is skipped, not fatal: one corrupt hero must
	## not take the whole MY HEROES screen down with it.
	var out: Array[Dictionary] = []
	var dir := DirAccess.open(PROFILE_DIR)
	if dir == null:
		return out
	var names := dir.get_files()
	names.sort()
	for file_name in names:
		if not file_name.ends_with(".json"):
			continue
		if out.size() >= MAX_PROFILES:
			break
		var profile := _read_profile("%s/%s" % [PROFILE_DIR, file_name])
		if not profile.is_empty():
			out.append(profile)
	return out


static func roster_document(
	system: Dictionary,
	profile: Dictionary,
	producer_object_id: String,
	roster_ordinal: int
) -> Dictionary:
	## A created hero, as the `openbfme.playable-unit-runtime` the rest of the
	## game already knows how to field.
	##
	## The object id is namespaced by hero id so two created heroes never collide
	## with each other or with a retail Object, and the roster ordinal is passed
	## in rather than chosen here: ordinals are unique per producer across the
	## whole mounted set, and only the caller holding the faction's roster knows
	## what is free.
	var sub_row := sub_class_row(
		system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
	)
	var attributes: Dictionary = profile.get("attributes", {}) as Dictionary
	var powers: Array = profile.get("powers", []) as Array
	var stats := computed_stats(system, sub_row, attributes, powers)
	var baseline := combat_baseline(system, profile)
	var baseline_combat: Dictionary = baseline["combat"] as Dictionary
	var object_id := "CreateAHero__%s" % String(profile.get("heroId", ""))
	var command_id := "__engine__/HERO_BUILD/%s" % object_id
	var name := sanitize_name(String(profile.get("name", "")))
	var button_image := String(sub_row.get("buttonImageId", ""))
	var portrait_image := String(sub_row.get("iconImageId", ""))
	var battlefield := model_binding(sub_row, "battlefield")

	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "hero",
		"descriptorSha256": String(system.get("descriptorSha256", "")),
		"recipeSha256": String(system.get("runtimeSha256", "")),
		"resourceIds": [],
		"registration": {
			"production": [{
				"producerObjectId": producer_object_id,
				"commandSetId": "__engine__/BuildableHeroesMP",
				"commandId": command_id,
				"surface": "hero-roster",
				"slot": 0,
				"rosterOrdinal": roster_ordinal,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
			"simulation": {
				"displayName": name,
				"buildCost": stats["buildCost"],
				"buildTimeSeconds": stats["buildTimeSeconds"],
				"commandPoints": stats["commandPoints"],
				"memberCount": 1,
				"memberHealth": stats["health"],
				"speed": float(baseline["speed"]),
				"visionRange": stats["visionRange"],
				# THE WEAPON THE HERO IS WEARING, times the damage slider. The
				# bling a player picked on the APPEARANCE screen is the weapon
				# retail equips, so its compiled damage/range/reload is the base
				# every DAMAGE_MULT step multiplies.
				"combat": {
					"attackRange": float(baseline_combat.get("attackRange", 0.0)),
					"minimumAttackRange": float(baseline_combat.get("minimumAttackRange", 0.0)),
					"delayBetweenShotsMs": float(baseline_combat.get("delayBetweenShotsMs", 0.0)),
					"preAttackDelayMs": float(baseline_combat.get("preAttackDelayMs", 0.0)),
					"firingDurationMs": float(baseline_combat.get("firingDurationMs", 0.0)),
					"damage": maxi(1, int(round(
						float(baseline_combat.get("damage", 0.0))
						* float(stats["damageMultiplier"])
					))),
					"damageType": String(baseline_combat.get("damageType", "")),
				},
				"movement": (baseline["movement"] as Dictionary).duplicate(),
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
				# THE OTHER TWO SLIDERS, on the document rather than only in the
				# arithmetic record. INNATE_ARMOR is a damage-TAKEN scalar and
				# AUTO_HEAL scales the object's own regeneration, so both reach
				# the sim as body properties of this one unit.
				"innateArmorScalar": float(stats["armorScalar"]),
				"autoHealMultiplier": float(stats["autoHealMultiplier"]),
			},
			# THE POWERS AND THE LEVEL CHAIN, in the shapes the runtime adapter
			# already projects for every retail hero. A created hero reaches the
			# sim's ability and experience engines through the same doors rather
			# than through a Create-a-Hero-shaped side entrance.
			"abilities": ability_rows(system, powers),
			"experience": experience_contract(system),
			# THE MESH. `model` is the W3D the battlefield state selects for this
			# subclass, joined out of createaheromodels.inc by the importer; the
			# skeleton and animation prefix travel with it so the rig binds.
			"visual": {
				"model": String(battlefield.get("model", "")),
				"skeleton": String(battlefield.get("skeleton", "")),
				"animationPrefix": String(battlefield.get("animationPrefix", "")),
				"conditionFlag": String(battlefield.get("conditionFlag", "")),
				"weaponLaunchBones": (
					battlefield.get("weaponLaunchBones", []) as Array
				).duplicate(),
				"mounted": (battlefield.get("mounted", {}) as Dictionary).duplicate(true),
			},
			"ui": {
				"portraitImageIds": [portrait_image],
				"commands": [{
					"commandId": command_id,
					"fields": {
						"ButtonImage": [button_image],
						"TextLabel": [name],
						"DescriptLabel": [String(sub_row.get("descriptionStringId", ""))],
					},
					"audioRoutes": [],
				}],
			},
			# The arithmetic, carried on the document so a runner can assert the
			# spawn against the INI ladders without recomputing them, and so a
			# support report says which multipliers produced a hero's numbers.
			"createAHero": {
				"heroId": String(profile.get("heroId", "")),
				"classIndex": int(profile.get("classIndex", -1)),
				"subClassIndex": int(profile.get("subClassIndex", -1)),
				"attributes": attributes.duplicate(),
				"powers": powers.duplicate(),
				"computed": stats,
			},
		},
	}


## Faction slugs as the manifest spells them, against the `UsableFactions`
## spelling retail authors on a subclass. Retail writes "Men"/"Elves"/"Dwarves";
## the runtime uses lowercase slugs.
static func subclass_allows_faction(sub_row: Dictionary, faction: String) -> bool:
	## Whether this subclass may be fielded by a faction.
	##
	## `UsableFactions` is authored per subclass precisely because a Captain of
	## Gondor is buildable by Men, Elves AND Dwarves while an Orc Raider is not.
	## A subclass that authors none is treated as unrestricted, which is what
	## retail does with the field absent.
	var allowed: Array = sub_row.get("usableFactions", []) as Array
	if allowed.is_empty():
		return true
	var wanted := faction.strip_edges().to_lower()
	for value in allowed:
		if String(value).strip_edges().to_lower() == wanted:
			return true
	return false


static func hero_roster_producer(unit_runtimes: Dictionary) -> Dictionary:
	## The producer every hero of this faction trains from, and the ordinals
	## already taken on it.
	##
	## Read off the loaded documents rather than named here: the producer is
	## `MenFortress` for Men and something else for every other faction, and the
	## ordinals retail authors are sparse (Boromir 4, Faramir 6), so a created
	## hero must be given a free one rather than a guessed next-in-sequence.
	var producer := ""
	var used: Dictionary = {}
	for document_value in unit_runtimes.values():
		var document := document_value as Dictionary
		if String(document.get("category", "")) != "hero":
			continue
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		for route_value in (registration.get("production", []) as Array):
			var route := route_value as Dictionary
			if String(route.get("surface", "")) != "hero-roster":
				continue
			if producer == "":
				producer = String(route.get("producerObjectId", ""))
			if String(route.get("producerObjectId", "")) == producer:
				used[int(route.get("rosterOrdinal", 0))] = true
	return {"producerObjectId": producer, "usedOrdinals": used}


static func admitted_seat_heroes(
	system: Dictionary, seat_rows: Array, require_descriptor_match: bool = true
) -> Dictionary:
	## Which of the match's announced heroes may actually be fielded, and one
	## stated reason per hero that may not.
	##
	## RUN IDENTICALLY ON EVERY PEER, on the bytes the lobby agreed, against the
	## locally mounted table. Admission is therefore a pure function of (agreed
	## payload, mounted pack) - and since lockstep already requires every peer to
	## mount the same pack, every peer admits and refuses exactly the same set.
	## That is the whole determinism argument: no peer may field a hero another
	## peer refused, so no peer can disagree about the production roster.
	##
	## FAIL CLOSED. A hero that fails ANY rule is dropped, not repaired: an
	## over-budget loadout, a power its class cannot take, a hero built against
	## different content. Dropping one hero costs one player one unit; admitting
	## a hero one peer computes differently costs the whole match.
	var admitted: Array = []
	var refusals: Array[String] = []
	var claimed: Dictionary = {}
	var rows: Array = seat_rows.duplicate()
	rows.sort_custom(
		func(a: Dictionary, b: Dictionary) -> bool:
			return int(a.get("seat", 0)) < int(b.get("seat", 0))
	)
	var descriptor := String(system.get("descriptorSha256", ""))
	for row_value in rows:
		var row := row_value as Dictionary
		var seat := int(row.get("seat", 0))
		var profiles: Array[Dictionary] = []
		for hero_value in (row.get("heroes", []) as Array):
			var profile := _decoded_hero(hero_value)
			if profile.is_empty():
				refusals.append("seat %d announced a hero document that is not readable" % seat)
				continue
			profiles.append(profile)
		profiles.sort_custom(
			func(a: Dictionary, b: Dictionary) -> bool:
				return String(a.get("heroId", "")) < String(b.get("heroId", ""))
		)
		for profile in profiles:
			var hero_id := String(profile.get("heroId", ""))
			var reasons := validate_profile(system, profile)
			# THE CONTENT LOCK, and only on the match path. A saved hero is
			# deliberately NOT frozen to the table it was made on - a rebalanced
			# pack moves every existing hero, which is the whole point of storing
			# a build order rather than stats. In a lockstep match that same
			# leniency is a desync, so there the descriptor must match exactly.
			if require_descriptor_match \
				and String(profile.get("systemDescriptorSha256", "")) != descriptor:
				reasons.append("the hero was built against different content")
			# THE ID IS THE SLOT. A created hero occupies an object id derived
			# from its hero id, and every seat can read every other seat's ids off
			# the lobby table - so a peer that re-announced a victim's id would
			# overwrite the victim's document and delete their hero from the
			# match, identically on every peer and therefore invisibly. First
			# claim wins, in the same (seat, hero-id) order the roster is built
			# in, and the loser is refused by name.
			if claimed.has(hero_id):
				refusals.append(
					"seat %d hero %s excluded: that hero id is already claimed by seat %d"
					% [seat, hero_id, int(claimed[hero_id])]
				)
				continue
			if not reasons.is_empty():
				refusals.append(
					"seat %d hero %s excluded: %s" % [seat, hero_id, ", ".join(reasons)]
				)
				continue
			claimed[hero_id] = seat
			admitted.append({
				"seat": seat,
				"team": int(row.get("team", seat)),
				"faction": String(row.get("faction", "")),
				"profile": profile,
			})
	return {"admitted": admitted, "refusals": refusals}


static func _decoded_hero(value: Variant) -> Dictionary:
	## A hero as announced: either the canonical JSON document the lobby carries
	## or, on the single-player path, the profile dictionary itself.
	if typeof(value) == TYPE_DICTIONARY:
		return value as Dictionary
	if typeof(value) != TYPE_STRING:
		return {}
	if String(value).length() > MAX_PROFILE_BYTES:
		return {}
	var parsed: Variant = JSON.parse_string(String(value))
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	return parsed as Dictionary


static func seat_roster_documents(
	system: Dictionary,
	unit_runtimes: Dictionary,
	seat_rows: Array,
	faction: String,
	require_descriptor_match: bool = true
) -> Dictionary:
	## Every announced hero this faction may field, as playable-unit documents
	## keyed by object id, ready to merge into the faction's roster.
	##
	## DETERMINISTIC BY CONSTRUCTION. Heroes are walked in (seat, hero-id) order
	## and ordinals are handed out in that order, so every peer builds the same
	## roster in the same slots. A roster that depended on directory order - or
	## on which heroes happened to be saved on THIS machine - would desync a
	## lockstep match on the first hero purchase.
	var out: Dictionary = {}
	if system.is_empty():
		return out
	var binding := hero_roster_producer(unit_runtimes)
	var producer := String(binding.get("producerObjectId", ""))
	if producer == "":
		# No hero producer in this faction's slice: nothing to hang a created
		# hero off, so none are offered rather than inventing a producer.
		return out
	var used: Dictionary = (binding.get("usedOrdinals", {}) as Dictionary).duplicate()
	var next_ordinal := 1
	var admission := admitted_seat_heroes(system, seat_rows, require_descriptor_match)
	for entry_value in (admission.get("admitted", []) as Array):
		var entry := entry_value as Dictionary
		var seat_faction := String(entry.get("faction", ""))
		if seat_faction != "" and seat_faction != faction:
			continue
		var profile := entry.get("profile", {}) as Dictionary
		var sub_row := sub_class_row(
			system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
		)
		if not subclass_allows_faction(sub_row, faction):
			continue
		while used.has(next_ordinal):
			next_ordinal += 1
		used[next_ordinal] = true
		var document := roster_document(system, profile, producer, next_ordinal)
		var record: Dictionary = (
			(document["registration"] as Dictionary)["createAHero"] as Dictionary
		)
		record["ownerSeat"] = int(entry.get("seat", 0))
		record["ownerTeam"] = int(entry.get("team", 0))
		out[String(document.get("objectId", ""))] = document
	return out


static func roster_documents(
	system: Dictionary, unit_runtimes: Dictionary, faction: String, owner_team: int = 0
) -> Dictionary:
	## The single-player roster: this machine's own saved heroes, on one seat.
	## One implementation with the multiplayer path so a skirmish hero and a
	## lockstep hero are never built by different arithmetic.
	##
	## THE OWNER TEAM IS PASSED IN, not assumed. A skirmish roster hands each row
	## its own sim team id and the human is not always on the first row, so a
	## hero owned by a hardcoded team 0 would be offered to whichever AI sits
	## there and refused to the player who made it.
	var profiles: Array = []
	for profile in load_profiles():
		profiles.append(profile)
	return seat_roster_documents(
		system,
		unit_runtimes,
		[{"seat": 0, "team": owner_team, "faction": faction, "heroes": profiles}],
		faction,
		false
	)


static func _read_profile(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	if file.get_length() > MAX_PROFILE_BYTES:
		file.close()
		return {}
	var text := file.get_as_text()
	file.close()
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	return parsed as Dictionary


static func _is_safe_hero_id(hero_id: String) -> bool:
	## Hero ids reach the filesystem, so they are restricted to hex rather than
	## merely checked for separators. Nothing that is not [0-9a-f] can name a
	## file this module reads or writes.
	if hero_id.length() < 8 or hero_id.length() > 32:
		return false
	for character in hero_id:
		if not (character >= "0" and character <= "9") and not (character >= "a" and character <= "f"):
			return false
	return true


static func hero_id_valid(hero_id: String) -> bool:
	## EXACTLY what `_new_hero_id` produces, and nothing else. Wider than this
	## and a hero id off another machine can spell a path, a command id or a log
	## line; `_is_safe_hero_id` stays wider only so an older saved file can still
	## be read off disk rather than becoming unopenable.
	if hero_id.length() != HERO_ID_LENGTH:
		return false
	for character in hero_id:
		if not HERO_ID_ALPHABET.contains(character):
			return false
	return true


static func _new_hero_id() -> String:
	var out := ""
	for _index in range(HERO_ID_LENGTH):
		out += HERO_ID_ALPHABET[randi() % HERO_ID_ALPHABET.length()]
	return out
