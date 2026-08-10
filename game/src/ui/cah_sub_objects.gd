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
## this module existed. `collapse_variant_families` is the honest stopgap, and it
## reads TWO structural facts off the mesh, never a table of retail part names:
##
##   1. NUMBERED SIBLINGS. A mesh named `X_<digits>` with numbered siblings is by
##      construction one variant of one part, so the lowest-numbered one is kept
##      and the rest are put away.
##   2. ONE PROP PER ATTACHMENT BONE. A Create-a-Hero skin also carries every
##      weapon the class can hold - `CHHW_CG_C_SKN` has eight, from `AXE_01` to
##      `WESTRONSWORD` - and most of them are not numbered siblings of anything,
##      so rule 1 left the hero holding a fan of blades. Every one of them is
##      rigidly weighted to a SINGLE bone that no deforming mesh uses (`B_HAND_R`
##      on that skin, which the body's own `B_HANDR` is not): a bone that exists
##      only to hang a prop off. Two props on one hook occupy the same hand, so
##      one is chosen and the rest are put away. An `_FX` mesh follows the prop it
##      is named after, so a chosen weapon keeps its glow and a rejected one takes
##      its glow with it.
##
## Both rules guess no relationship between a garment OPTION and a part - the
## screen says out loud that garment choices are not bound yet - they only refuse
## to draw a stack of alternatives on top of each other. Bones that a deforming
## mesh rides (`B_HEAD`, which the body itself is weighted to) are NOT attachment
## points, which is what keeps rule 2 away from a hero's own head and hair.

## The keys the compiled system document carries. Named here so a contract
## change is one edit rather than six string literals.
const OPTION_KEY := "subObjects"
const DEFAULT_KEY := "defaultSubObjects"
const SHOW_KEY := "show"
const HIDE_KEY := "hide"
const TEXTURE_SWAP_KEY := "textureSwaps"

## The tail retail gives a prop's effect mesh: `FIREBRAND_FX01` is the glow on
## `FIREBRAND`, not a ninth weapon to choose between.
const FX_MARKER := "_FX"


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
	# THE DEFAULT SET IS AUTHORED ON THE SUBCLASS, and may be narrowed per mesh.
	# Retail writes one `DefaultSubObjects` hide list per Create-a-Hero subclass -
	# a hundred names, every weapon and staff and bow the shared skins carry - and
	# reading only the per-model set left the compiled pack's real bindings on the
	# floor: the chosen weapon appeared, and the two the option table happens not
	# to name (`SWRD_05`, `Belthronding`) stayed in the hero's hand beside it.
	for defaults_value in [sub_row.get(DEFAULT_KEY, {}), binding.get(DEFAULT_KEY, {})]:
		var defaults := defaults_value as Dictionary
		for part in _names(defaults, HIDE_KEY):
			hide[part] = true
			show.erase(part)
			mapped = true
		for part in _names(defaults, SHOW_KEY):
			show[part] = true
			hide.erase(part)
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
	## siblings of one stem, so they are alternatives and at most one may show;
	## and every prop hanging off one attachment bone is an alternative to every
	## other prop on it. A part that is neither is left exactly as the pack
	## shipped it - this never decides that some lone mesh is "a garment".
	var families := {}
	for name in mesh_names(root):
		var split := _split_numbered(name)
		if split.is_empty():
			continue
		var stem := String(split["stem"])
		if not families.has(stem):
			families[stem] = []
		(families[stem] as Array).append({"name": name, "index": int(split["index"])})
	var show := {}
	var hide := {}
	var family_sizes := {}
	for stem_value in families.keys():
		var members: Array = families[stem_value] as Array
		members.sort_custom(
			func(a: Dictionary, b: Dictionary) -> bool: return int(a["index"]) < int(b["index"])
		)
		for member_value in members:
			family_sizes[String((member_value as Dictionary)["name"])] = members.size()
		if members.size() < 2:
			continue
		show[String((members[0] as Dictionary)["name"])] = true
		for index in range(1, members.size()):
			hide[String((members[index] as Dictionary)["name"])] = true

	# ONE PROP PER HOOK, decided after the numbered families so that a chosen
	# variant and a chosen prop cannot both claim the same hand.
	var hooks := attachment_groups(root)
	for bone_value in hooks.keys():
		var props: Array = hooks[bone_value] as Array
		var worn := _preferred_prop(props, family_sizes)
		if worn == "":
			continue
		for prop_value in props:
			var prop := String(prop_value)
			if prop == worn or _fx_base(prop) == worn:
				show[prop] = true
				hide.erase(prop)
			else:
				hide[prop] = true
				show.erase(prop)
	return {"show": show.keys(), "hide": hide.keys(), "mapped": false, "groups": {}}


static func attachment_groups(root: Node) -> Dictionary:
	## The hooks this skin hangs props off, and what is hanging off each.
	##
	## An attachment bone is one that EVERY mesh riding it rides alone, and that
	## no deforming mesh rides at all. A helmet is weighted to `B_HEAD` alone but
	## the body is weighted to `B_HEAD` too, so the head is a joint of the
	## skeleton rather than a hook - which is what stops this from deciding that a
	## wizard's head and his eyes are two hats.
	var singles := {}
	var deforming := {}
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is MeshInstance3D):
			continue
		var instance := node as MeshInstance3D
		var bones := _bones_of(instance)
		if bones.is_empty():
			continue
		if bones.size() > 1:
			for bone in bones:
				deforming[bone] = true
			continue
		var hook := bones[0]
		if not singles.has(hook):
			singles[hook] = []
		var riders: Array = singles[hook] as Array
		var key := _part_key(instance)
		if key != "" and not riders.has(key):
			riders.append(key)
	var out := {}
	for hook_value in singles.keys():
		if deforming.has(hook_value):
			continue
		var riders: Array = singles[hook_value] as Array
		if riders.size() < 2:
			continue
		riders.sort()
		out[hook_value] = riders
	return out


static func _preferred_prop(props: Array, family_sizes: Dictionary) -> String:
	## Which of the props on one hook is worn, decided the same way every time.
	##
	## A numbered part is preferred over a named one because the numbers are the
	## class's own kit (`SWRD_05`, `BOW_03`) while the names are the bling retail
	## awards on top of it (`GURTHANG`, `TROLLBANE`) - a hero who has earned
	## nothing is holding the kit. An `_FX` mesh is never the choice: it is an
	## effect on a prop, and worn on its own it is a glow with no weapon in it.
	var best := ""
	for prop_value in props:
		var prop := String(prop_value)
		if prop.contains(FX_MARKER):
			continue
		if best == "" or _prop_precedes(prop, best, family_sizes):
			best = prop
	return best


static func _prop_precedes(prop: String, other: String, family_sizes: Dictionary) -> bool:
	var numbered := _split_numbered(prop)
	var other_numbered := _split_numbered(other)
	if numbered.is_empty() != other_numbered.is_empty():
		return other_numbered.is_empty()
	var size := int(family_sizes.get(prop, 1))
	var other_size := int(family_sizes.get(other, 1))
	if size != other_size:
		return size > other_size
	if not numbered.is_empty() and int(numbered["index"]) != int(other_numbered["index"]):
		return int(numbered["index"]) < int(other_numbered["index"])
	return prop < other


static func _fx_base(name: String) -> String:
	var cut := name.find(FX_MARKER)
	return "" if cut <= 0 else name.substr(0, cut)


static func _bones_of(instance: MeshInstance3D) -> Array[String]:
	## The bones a mesh is actually weighted to, named as the rig names them.
	var out: Array[String] = []
	var mesh: Mesh = instance.mesh
	if mesh == null:
		return out
	var skeleton := instance.get_node_or_null(instance.skeleton) as Skeleton3D
	var used := {}
	for surface in range(mesh.get_surface_count()):
		var arrays: Array = mesh.surface_get_arrays(surface)
		if arrays.size() <= Mesh.ARRAY_WEIGHTS:
			continue
		if arrays[Mesh.ARRAY_BONES] == null or arrays[Mesh.ARRAY_WEIGHTS] == null:
			continue
		var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
		var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
		for index in range(mini(bones.size(), weights.size())):
			if weights[index] > 0.0:
				used[bones[index]] = true
	for index_value in used.keys():
		var name := _bone_name(instance.skin, skeleton, int(index_value))
		if not out.has(name):
			out.append(name)
	out.sort()
	return out


static func _bone_name(skin: Skin, skeleton: Skeleton3D, index: int) -> String:
	## A vertex's bone index is an index into the SKIN's binds, which glTF import
	## names after the rig's bones; the skeleton is the fallback for a mesh whose
	## binds carry no names, and the raw index is what is left when there is no
	## rig at all.
	if skin != null and index >= 0 and index < skin.get_bind_count():
		var named := String(skin.get_bind_name(index))
		if named != "":
			return named
		var bound := skin.get_bind_bone(index)
		if skeleton != null and bound >= 0 and bound < skeleton.get_bone_count():
			return skeleton.get_bone_name(bound)
	if skeleton != null and index >= 0 and index < skeleton.get_bone_count():
		return skeleton.get_bone_name(index)
	return "#%d" % index


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
		var key := _matched_key(node as Node3D, wanted)
		if key == "":
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
	## THE NODE NAME, and only it. The mesh resource's name used to win, and on
	## the converted skins that name is the FILE's: `CHHW_CG_C_SKN_BOOT_01`, not
	## `BOOT_01`. Every name in the pack's own `subObjects` sets is the retail
	## spelling, so with the file prefix in front of it the compiled bindings
	## matched nothing at all - the screen reported "mapped" and put not one
	## garment on or off. The prefixed spelling is still ACCEPTED when a plan is
	## applied (see `_matched_key`); it is simply not what a part is called.
	return String(node.name).to_upper()


static func _matched_key(node: Node3D, wanted: Dictionary) -> String:
	## Which name in a plan this node answers to, or "" for none.
	##
	## Three spellings are accepted, because two converters disagree about which
	## of them survives: the node's own name, the mesh resource's name, and the
	## mesh resource's name with a file prefix in front of it.
	var name := String(node.name).to_upper()
	if wanted.has(name):
		return name
	var mesh_name := _mesh_name(node)
	if mesh_name == "":
		return ""
	if wanted.has(mesh_name):
		return mesh_name
	for key_value in wanted.keys():
		var key := String(key_value)
		if mesh_name.ends_with("_" + key):
			return key
	return ""


static func _mesh_name(node: Node3D) -> String:
	if not (node is MeshInstance3D):
		return ""
	var mesh: Mesh = (node as MeshInstance3D).mesh
	if mesh == null:
		return ""
	return String(mesh.resource_name).to_upper()


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
