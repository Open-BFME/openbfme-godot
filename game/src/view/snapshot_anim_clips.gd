class_name SnapshotAnimClips
extends RefCounted
## Deterministic snapshot slot -> imported W3D clip resolver.
##
## The importer preserves the W3D hierarchy name in each GLB animation name;
## the retail action is the final underscore-delimited token. Actual mounted
## examples include gumanmocap_idla, gumanmocap_runa, gumanmocap_atka,
## gumanmocap_dieb and gumanmocap_chrb.

const SLOT_NAMES := ["idle", "move", "attack", "die", "cheer"]
const PRIMARY_PATTERNS := [
	"(?i)(^|_)(IDLA|IDLE[^_]*)$",
	"(?i)(^|_)(RUNA|WLKA|MOVE[^_]*)$",
	"(?i)(^|_)(ATKA|ATKB[^_]*)$",
	"(?i)(^|_)(DIEA|DIE[^_]*)$",
	"(?i)(^|_)(CHRA|CHER[^_]*)$",
]
const FAMILY_PATTERNS := [
	"(?i)(^|_)IDL[^_]*$",
	"(?i)(^|_)(RUN|WLK|MOVE)[^_]*$",
	"(?i)(^|_)ATK[^_]*$",
	"(?i)(^|_)DIE[^_]*$",
	"(?i)(^|_)(CHR|CHER)[^_]*$",
]


static func resolve(template_name: String, animation_names: Array[String]) -> Dictionary:
	var ordered := animation_names.duplicate()
	ordered.sort_custom(func(a: String, b: String) -> bool:
		return a.naturalnocasecmp_to(b) < 0
	)
	var usable: Array[String] = []
	for name in ordered:
		if not name.is_empty() and name.to_upper() != "RESET":
			usable.append(name)

	var slots: Array[String] = []
	var matched_counts: Array[int] = []
	var fallback_slots: Array[int] = []
	for slot in SLOT_NAMES.size():
		var primary := _matches(usable, PRIMARY_PATTERNS[slot])
		var family := _matches(usable, FAMILY_PATTERNS[slot])
		var candidates := primary if not primary.is_empty() else family
		slots.append(candidates[0] if not candidates.is_empty() else "")
		matched_counts.append(candidates.size())

	var idle_fallback := slots[0]
	if idle_fallback.is_empty() and not usable.is_empty():
		idle_fallback = usable[0]
		slots[0] = idle_fallback
		fallback_slots.append(0)
	for slot in range(1, SLOT_NAMES.size()):
		if slots[slot].is_empty() and not idle_fallback.is_empty():
			slots[slot] = idle_fallback
			fallback_slots.append(slot)

	print(
		"SNAPSHOT_ANIM_CLIPS template=%s available=%d idle=%s move=%s attack=%s die=%s cheer=%s counts=%s fallback=%s"
		% [
			template_name,
			usable.size(),
			_slot_label(slots, 0),
			_slot_label(slots, 1),
			_slot_label(slots, 2),
			_slot_label(slots, 3),
			_slot_label(slots, 4),
			str(matched_counts),
			str(fallback_slots),
		]
	)
	return {
		"slots": slots,
		"matched_counts": matched_counts,
		"available_count": usable.size(),
		"fallback_slots": fallback_slots,
	}


static func _matches(names: Array[String], pattern: String) -> Array[String]:
	var expression := RegEx.new()
	if expression.compile(pattern) != OK:
		return []
	var result: Array[String] = []
	for name in names:
		if expression.search(name) != null:
			result.append(name)
	return result


static func _slot_label(slots: Array[String], index: int) -> String:
	return slots[index] if index < slots.size() and not slots[index].is_empty() else "<missing>"
