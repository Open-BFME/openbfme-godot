class_name Stage8FormationSystem
extends RefCounted
## Stable entity-id formation assignment for legal-safe Stage 8 proofs.

const KINDS: Array[String] = ["line", "wedge", "column"]


func generate(entity_ids: Array[int], kind: String, anchor: Vector2i) -> Array[Dictionary]:
	if not KINDS.has(kind):
		return []
	var ids: Array[int] = entity_ids.duplicate()
	ids.sort()
	var result: Array[Dictionary] = []
	for index: int in range(ids.size()):
		var offset := Vector2i.ZERO
		match kind:
			"line":
				offset = Vector2i(index - ids.size() / 2, 0)
			"column":
				offset = Vector2i(0, index - ids.size() / 2)
			"wedge":
				if index > 0:
					var depth: int = (index + 1) / 2
					var side: int = -1 if index % 2 == 1 else 1
					offset = Vector2i(side * depth, depth)
		result.append({"entity_id": ids[index], "slot_index": index, "cell": anchor + offset})
	return result
