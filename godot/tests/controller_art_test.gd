extends SceneTree
func _initialize() -> void:
	var art := preload("res://scripts/controller_art.gd")
	for name_text in ["select_ring", "select_ring_good", "a_button", "reticle", "reticle_yellow", "reticle_attack", "reticle_capture", "reticle_invalid", "reticle_ability", "reticle_garrison", "reticle_ring", "reticle_a"]:
		var texture := art.texture(name_text)
		assert(texture != null, name_text)
		var frame := art.frame(name_text)
		assert(frame != null and frame.get_width() == frame.get_height(), name_text)
	assert(art.texture("select_ring").get_width() / art.texture("select_ring").get_height() == 16)
	print("CONTROLLER ORIGINAL ART PASS")
	quit()
