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
## WHAT IS NOT HERE, and why the screen says so out loud: the seven APPEARANCE
## bling groups (helmet, shoulders, body, gauntlets, weapon, shield, boots),
## hero colours, and per-class special-power purchase. The importer does not
## compile them - see the `limitations` list on the system document.

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


static func computed_stats(system: Dictionary, sub_row: Dictionary, attributes: Dictionary) -> Dictionary:
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

	var base_health := float(base.get("maxHealth", 0.0))
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
		"buildCost": int(base.get("buildCost", 0)),
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
		# WHICH TABLE THIS HERO WAS BUILT AGAINST. Not a lock - the hero is
		# revalidated against whatever pack is mounted now - but it is how a
		# "this hero was made on different content" notice can ever be shown
		# instead of the numbers silently moving.
		"systemDescriptorSha256": String(system.get("descriptorSha256", "")),
	}


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
	if String(profile.get("heroId", "")) == "":
		refusals.append("the hero has no id")

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
	var stats := computed_stats(system, sub_row, attributes)
	var object_id := "CreateAHero__%s" % String(profile.get("heroId", ""))
	var command_id := "__engine__/HERO_BUILD/%s" % object_id
	var name := sanitize_name(String(profile.get("name", "")))
	var button_image := String(sub_row.get("buttonImageId", ""))
	var portrait_image := String(sub_row.get("iconImageId", ""))

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
				"speed": 50.0,
				"visionRange": stats["visionRange"],
				"combat": {
					"attackRange": 40.0,
					"minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1250.0,
					"preAttackDelayMs": 1000.0,
					"firingDurationMs": 1400.0,
					"damage": int(round(150.0 * float(stats["damageMultiplier"]))),
				},
				"movement": {
					"acceleration": 210.0,
					"braking": 210.0,
					"turnRateDegreesPerSecond": 720.0,
				},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
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
				"computed": stats,
			},
		},
	}


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


static func _new_hero_id() -> String:
	var out := ""
	for _index in range(24):
		out += "0123456789abcdef"[randi() % 16]
	return out
