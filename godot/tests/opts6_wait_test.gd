extends SceneTree

const Wait := preload("res://scripts/opts6_wait.gd")
var checks := 0
var failed := 0
var polls := 0
var sends := 0
var sent_at := -1
var nav := {"focus": "row:old", "frame": false, "targets": []}


func _initialize() -> void:
	_run.call_deferred()


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failed += 1
	print("WAIT ", "ok " if ok else "FAIL ", label)


func poll() -> void:
	polls += 1
	if polls == 80:
		nav.targets = [{"id": "row:Retail.Detail", "rect": Rect2()}]
	if polls == 90:
		nav.targets[0].rect = Rect2(1, 1, 20, 20)
	if sent_at >= 0 and polls >= sent_at + 5:
		nav.focus = "row:Retail.Detail"
	if sent_at >= 0 and polls >= sent_at + 12:
		nav.frame = true
	await process_frame


func send() -> void:
	sends += 1
	sent_at = polls


func _run() -> void:
	check(await Wait.until(func(): return true, poll, 0), "already ready needs no frames")
	check(polls == 0, "no unnecessary polling")
	check(await Wait.focus("row:Retail.Detail", func(): return nav, send, poll), "delayed Graphics target and focus frame become ready")
	check(sent_at == 90, "no focus input before the target is visible (even after 60 frames)")
	check(sends == 1 and polls >= sent_at + 12, "waits for the drawn focus instead of advancing past it")
	nav.frame = false
	sent_at = polls
	check(await Wait.focus("row:Retail.Detail", func(): return nav, send, poll), "already focused target waits for its frame")
	check(sends == 1, "never tabs past a focus whose frame is not drawn yet")
	nav.focus = ""
	nav.log = []
	var consumed_sends := [0]
	var close_catcher := func():
		consumed_sends[0] += 1
		if consumed_sends[0] == 1:
			nav.log = ["closed click-away catcher"]
		else:
			nav.focus = "row:Retail.Detail"
	check(await Wait.focus("row:Retail.Detail", func(): return nav, close_catcher, poll), "handles input consumed by a click-away catcher")
	check(consumed_sends[0] == 2, "retries only after observing the consumed input")
	check(Wait.target({"targets": [{"id": "nickname", "rect": Rect2()}]}, "nickname").is_empty(), "hidden nickname is not ready")
	check(not Wait.target({"targets": [{"id": "nickname", "rect": Rect2(1, 1, 2, 2)}]}, "nickname").is_empty(), "visible nickname is ready")
	var started := Time.get_ticks_msec()
	check(not await Wait.until(func(): return false, poll, 20), "missing readiness fails at the deadline")
	check(Time.get_ticks_msec() - started < 1000, "deadline is wall-clock bounded, not a frame count") # tolerance: allow scheduler latency around the 20 ms deadline
	check(not await Wait.focus("missing", func(): return nav, send, poll, 20), "missing focus target fails closed")
	check(sends == 1, "no input sent for the missing target")
	print("WAIT RESULT: %d checks, %d failed" % [checks, failed])
	quit(0 if failed == 0 else 1)
