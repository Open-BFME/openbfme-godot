extends Node
## Fixed-step simulation clock. Gameplay time is independent of FPS.

const TICK_HZ := 10.0
const TICK_DT := 1.0 / TICK_HZ

var time: float = 0.0
var tick_index: int = 0
var paused: bool = false
var game_speed: float = 1.0
var _accum: float = 0.0
var _running: bool = false

signal ticked(dt: float)
signal frame_interpolated(alpha: float)

func start() -> void:
	_running = true
	paused = false

func stop() -> void:
	_running = false

func reset() -> void:
	time = 0.0
	tick_index = 0
	_accum = 0.0
	paused = false
	game_speed = 1.0

func _process(delta: float) -> void:
	if not _running or paused:
		return
	_accum += delta * game_speed
	var guard := 0
	while _accum >= TICK_DT and guard < 8:
		_accum -= TICK_DT
		time += TICK_DT
		tick_index += 1
		ticked.emit(TICK_DT)
		guard += 1
	frame_interpolated.emit(clampf(_accum / TICK_DT, 0.0, 1.0))

## Advance N ticks without rendering (tests / headless).
func step(n: int = 1) -> void:
	for i in n:
		time += TICK_DT
		tick_index += 1
		ticked.emit(TICK_DT)
