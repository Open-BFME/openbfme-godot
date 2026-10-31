# OpenBFME. GPL-3.0. Score-screen graph and authored timeline markers.
extends Control
var shell: Node2D
var graph := {}
var rect := Rect2()
func _draw() -> void:
	if graph.is_empty() or rect.size.x <= 0.0:
		return
	var lines: Array = graph.get("lines", [])
	for l in lines:
		for pass_i in 2:
			var c := Color.hex(((int(l.color) & 0xFFFFFF) << 8) | 0xFF)
			var pts: PackedVector2Array = l.points
			if pts.size() < 2:
				continue
			var out := PackedVector2Array()
			var stride := maxi(1, pts.size() / 600)
			for i in range(0, pts.size(), stride):
				var p: Vector2 = pts[i]
				out.append(rect.position + Vector2(p.x * rect.size.x, (1.0 - clampf(p.y, 0.0, 1.0)) * rect.size.y))
			var last: Vector2 = pts[pts.size() - 1]
			out.append(rect.position + Vector2(last.x * rect.size.x, (1.0 - clampf(last.y, 0.0, 1.0)) * rect.size.y))
			# RW 0x924DD1 twice: width 3 with alpha, then width 1 opaque
			if pass_i == 0:
				draw_polyline(out, Color(c, 0.35), 3.0, true)
			else:
				draw_polyline(out, c, 1.0, true)
		# RW 0x9257F2: finish this player before drawing the next player.
		for marker in marker_commands([l]):
			draw_texture_rect_region(marker.texture, marker.rect, marker.region)

# RotWK RenderGraph RW 0x9257F2: result at the final sample, fortress marks
# at their recorded sample. Results 3/4 have no outcome image.
var _images := {}
func marker_commands(lines: Array = []) -> Array:
	var commands: Array = []
	if lines.is_empty(): lines = graph.get("lines", [])
	for line in lines:
		var points: PackedVector2Array = line.get("points", PackedVector2Array())
		if points.is_empty(): continue
		var result := int(line.get("result", 3))
		var outcomes := ["AptTimelineVictorious", "AptTimelineDefeated", "AptTimelineDisconnected"]
		if result >= 0 and result < outcomes.size():
			_append_marker(commands, outcomes[result], points[-1])
		for point in line.get("marks", PackedVector2Array()):
			_append_marker(commands, "AptTimelineFortress", point)
	return commands

func _append_marker(commands: Array, name: String, point: Vector2) -> void:
	if shell == null: return
	if not _images.has(name):
		_images[name] = shell.shell_mapped_image(name)
		if not _images[name].get("ok", false):
			push_error("Timeline icon %s: %s" % [name, _images[name].get("error", "missing image")])
	var image: Dictionary = _images[name]
	if not image.get("ok", false): return
	var size: Vector2 = shell.stage_to_window(image.size) - shell.stage_to_window(Vector2.ZERO)
	var centre := rect.position + Vector2(point.x * rect.size.x, (1.0-clampf(point.y,0.0,1.0))*rect.size.y)
	commands.append({"name":name,"texture":image.texture,"region":image.region,"rect":Rect2(centre-size*0.5,size)})
