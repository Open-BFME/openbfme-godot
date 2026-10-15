## Lane LAUNCHERUI-1: a one-line label that ends in an ellipsis when its box is too narrow, and whose tooltip is then the whole text
## (a trimmed line must never be the only place a message exists). Made by widgets.gd trim_label.
extends Label


func _init() -> void:
	clip_text = true
	text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	mouse_filter = Control.MOUSE_FILTER_PASS   # a label ignores the mouse by default, which hides its tooltip


func _get_tooltip(_at_position: Vector2) -> String:
	return text
