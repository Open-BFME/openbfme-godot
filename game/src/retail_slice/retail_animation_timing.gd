class_name RetailAnimationTiming
extends RefCounted


static func speed_factor(values: Variant, entity_id: int, member_index: int, action_token: int, clip: String) -> float:
	if typeof(values) != TYPE_ARRAY or (values as Array).size() != 2:
		return 1.0
	var low := float((values as Array)[0])
	var high := float((values as Array)[1])
	if not is_finite(low) or not is_finite(high) or low <= 0.0 or high < low:
		return 1.0
	if is_equal_approx(low, high):
		return low
	var sample := posmod(entity_id * 73856093 + member_index * 19349663 + action_token * 83492791 + clip.hash(), 1000003)
	return lerpf(low, high, float(sample) / 1000002.0)
