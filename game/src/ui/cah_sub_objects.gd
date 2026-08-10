extends RefCounted
## THE GARMENT PARTS ON A CREATE-A-HERO SKIN, and which of them are showing.
##
## HOW RETAIL DOES IT. A Create-a-Hero skin is not assembled from attachments at
## runtime. Every helmet, every pair of boots, every shoulder plate and every
## sword the class can wear is BAKED INTO THE ONE MESH as a named sub-object -
## `HLMT_01`, `SLDR_04`, `BOOT_00`, `GURTHANG` - and picking a garment is a
## visibility switch over those names (retail's `SubObjectsUpgrade`). The
## converted GLBs keep the retail names on their nodes and meshes, so the same
## switch works here: this module decides WHICH names are showing and flips
## them, and nothing about the hero's geometry is invented or re-authored.
##
## WHERE THE MAP COMES FROM. The pack, and only the pack. The compiled
## `cah.system` carries, per appearance option, the `subObjects` the option
## shows and hides, and per subclass model binding a `defaultSubObjects` set.
## This file reads that and applies it. It does NOT contain a table of retail
## part names, because a table here would be a second, silently-diverging copy
## of what the importer compiles out of retail INI.
##
## WHAT HAPPENS BEFORE THE MAP LANDS. A skin with every variant baked in and no
## map draws ALL of them at once - a captain wearing four shields, five pairs of
## boots and six helmets simultaneously, which is what the preview showed before
## this module existed. `collapse_variant_families` is the honest stopgap: a
## mesh named `X_<digits>` with numbered siblings is BY CONSTRUCTION one variant
## of one part, and showing more than one of them is never right whatever the
## map turns out to say, so the lowest-numbered one is kept and the rest are put
## away. It guesses no relationship between a garment OPTION and a part - the
## screen says out loud that garment choices are not bound yet - it only refuses
## to draw a stack of alternatives on top of each other.

## The keys the compiled system document carries. Named here so a contract
## change is one edit rather than six string literals.
const OPTION_KEY := "subObjects"
const DEFAULT_KEY := "defaultSubObjects"
const SHOW_KEY := "show"
const HIDE_KEY := "hide"
const TEXTURE_SWAP_KEY := "textureSwaps"


static func system_maps_sub_objects(system: Dictionary) -> bool:
	## Whether the mounted pack binds appearance options to mesh parts at all.
	##
	## The screen shows a different caption on the two answers, so a player can
	## tell "this pack cannot do garments yet" from "your helmet did not change".
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	for option_value in (registration.get("appearanceOptions", []) as Array):
		var option := option_value as Dictionary
		var bound: Dictionary = option.get(OPTION_KEY, {}) as Dictionary
		if _names(bound, SHOW_KEY).size() > 0 or _names(bound, HIDE_KEY).size() > 0:
			return true
	return false


static func plan(
	system: Dictionary, sub_row: Dictionary, appearance: Dictionary, surface: String
) -> Dictionary:
	## The visibility set for one hero: {show, hide, mapped, groups}.
	##
	## `mapped` is false when the pack carries no bindings for this subclass, and
	## the caller is expected to say so rather than to pretend the empty plan was
	## the player's choice.
	var show := {}
	var hide := {}
	var mapped := false

	var models: Dictionary = sub_row.get("models", {}) as Dictionary
	var binding: Dictionary = models.get(surface, {}) as Dictionary
	if binding.is_empty():
		binding = models.get("battlefield", {}) as Dictionary
	var defaults: Dictionary = binding.get(DEFAULT_KEY, {}) as Dictionary
	for part in _names(defaults, HIDE_KEY):
		hide[part] = true
		mapped = true
	for part in _names(defaults, SHOW_KEY):
		show[part] = true
		mapped = true

	var options := _options_by_upgrade(system)
	var choices: Dictionary = sub_row.get("appearanceChoices", {}) as Dictionary
	var groups := {}
	for group_value in choices.keys():
		var group := String(group_value)
		var upgrades: Array = choices[group_value] as Array
		if upgrades.is_empty():
			continue
		var chosen := String(appearance.get(group, upgrades[0]))
		if not upgrades.has(chosen):
			chosen = String(upgrades[0])
		# THE SIBLINGS COME OFF FIRST. Within one group only one option can be
		# worn, so every part any OTHER option in the group would show is hidden
		# before the chosen one is shown - which is what makes cycling replace a
		# helmet instead of stacking a second one on top of it.
		for upgrade_value in upgrades:
			var upgrade := String(upgrade_value)
			if upgrade == chosen:
				continue
			for part in _names(options.get(upgrade, {}).get(OPTION_KEY, {}) as Dictionary, SHOW_KEY):
				hide[part] = true
				mapped = true
		var bound: Dictionary = options.get(chosen, {}).get(OPTION_KEY, {}) as Dictionary
		for part in _names(bound, HIDE_KEY):
			hide[part] = true
			mapped = true
		var shown_here: Array[String] = []
		for part in _names(bound, SHOW_KEY):
			show[part] = true
			hide.erase(part)
			shown_here.append(part)
			mapped = true
		groups[group] = {"upgrade": chosen, "show": shown_here}

	return {
		"show": show.keys(),
		"hide": hide.keys(),
		"mapped": mapped,
		"groups": groups,
	}


static func collapse_variant_families(root: Node) -> Dictionary:
	## The stopgap plan for a skin whose parts nothing has mapped yet.
	##
	## Only structure is used: `HLMT_01`, `HLMT_02`, `HLMT_05` are numbered
	## siblings of one stem, so they are alternatives and at most one may show.
	## A part with no numbered sibling is left exactly as the pack shipped it -
	## this never decides that some lone mesh is "a garment".
	var families := {}
	for name in mesh_names(root):
		var split := _split_numbered(name)
		if split.is_empty():
			continue
		var stem := String(split["stem"])
		if not families.has(stem):
			families[stem] = []
		(families[stem] as Array).append({"name": name, "index": int(split["index"])})
	var show := []
	var hide := []
	for stem_value in families.keys():
		var members: Array = families[stem_value] as Array
		if members.size() < 2:
			continue
		members.sort_custom(
			func(a: Dictionary, b: Dictionary) -> bool: return int(a["index"]) < int(b["index"])
		)
		show.append(String((members[0] as Dictionary)["name"]))
		for index in range(1, members.size()):
			hide.append(String((members[index] as Dictionary)["name"]))
	return {"show": show, "hide": hide, "mapped": false, "groups": {}}


static func apply(root: Node, visibility_plan: Dictionary) -> Dictionary:
	## Flip the plan onto a loaded model. Returns what actually moved.
	##
	## A name the model does not carry is COUNTED, not fatal: retail authors a
	## garment option for parts that only exist on the battlefield mesh, and a
	## preview that failed on the first such name would show no hero at all.
	var wanted := {}
	for part in (visibility_plan.get(HIDE_KEY, []) as Array):
		wanted[String(part).to_upper()] = false
	for part in (visibility_plan.get(SHOW_KEY, []) as Array):
		wanted[String(part).to_upper()] = true
	if root == null or wanted.is_empty():
		return {"shown": 0, "hidden": 0, "unknown": [], "matched": 0}

	var seen := {}
	var shown := 0
	var hidden := 0
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is GeometryInstance3D):
			continue
		var key := _part_key(node as Node3D)
		if not wanted.has(key):
			continue
		seen[key] = true
		var visible_now := bool(wanted[key])
		(node as Node3D).visible = visible_now
		if visible_now:
			shown += 1
		else:
			hidden += 1
	var unknown: Array[String] = []
	for key_value in wanted.keys():
		if not seen.has(key_value):
			unknown.append(String(key_value))
	unknown.sort()
	return {"shown": shown, "hidden": hidden, "unknown": unknown, "matched": seen.size()}


static func mesh_names(root: Node) -> PackedStringArray:
	## Every drawable part name on a loaded model, in the retail spelling.
	var out := PackedStringArray()
	if root == null:
		return out
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if node is GeometryInstance3D:
			var key := _part_key(node as Node3D)
			if key != "" and not out.has(key):
				out.append(key)
	return out


static func _part_key(node: Node3D) -> String:
	## The retail sub-object name a node stands for.
	##
	## The node name is authoritative; the mesh resource's own name is the
	## fallback because a glTF importer that de-duplicates node names (`HLMT_01`
	## becoming `HLMT_012`) leaves the mesh name intact.
	var name := String(node.name).to_upper()
	if node is MeshInstance3D:
		var mesh: Mesh = (node as MeshInstance3D).mesh
		if mesh != null and String(mesh.resource_name) != "":
			var mesh_name := String(mesh.resource_name).to_upper()
			if mesh_name != "":
				return mesh_name
	return name


static func _split_numbered(name: String) -> Dictionary:
	var cut := name.rfind("_")
	if cut <= 0 or cut >= name.length() - 1:
		return {}
	var tail := name.substr(cut + 1)
	if not tail.is_valid_int():
		return {}
	return {"stem": name.substr(0, cut), "index": int(tail)}


static func _options_by_upgrade(system: Dictionary) -> Dictionary:
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	var out := {}
	for option_value in (registration.get("appearanceOptions", []) as Array):
		var option := option_value as Dictionary
		out[String(option.get("upgradeName", ""))] = option
	return out


static func _names(bound: Dictionary, key: String) -> Array[String]:
	var out: Array[String] = []
	for value in (bound.get(key, []) as Array):
		var name := String(value).strip_edges().to_upper()
		if name != "" and not out.has(name):
			out.append(name)
	return out
