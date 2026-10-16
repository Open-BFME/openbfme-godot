## Latch Shift during the first startup frames, before mounting game data.
extends Node
var skirmish_requested := false
var _deadline := 0

func _ready() -> void:
	_deadline = Time.get_ticks_msec() + 750
	_sample()

func _sample() -> void:
	if Time.get_ticks_msec() <= _deadline and Input.is_key_pressed(KEY_SHIFT):
		skirmish_requested = true

func _process(_delta: float) -> void:
	_sample()
	if Time.get_ticks_msec() > _deadline:
		set_process(false)
		set_process_input(false)

func _input(event: InputEvent) -> void:
	if Time.get_ticks_msec() <= _deadline and event is InputEventKey and event.pressed and event.keycode == KEY_SHIFT:
		skirmish_requested = true
