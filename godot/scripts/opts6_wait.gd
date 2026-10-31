extends RefCounted

const TIMEOUT_MS := 15000


static func until(condition: Callable, poll: Callable, timeout_ms := TIMEOUT_MS) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			return false
		await poll.call()
	return true


static func target(nav: Dictionary, suffix: String) -> String:
	for item in nav.get("targets", []):
		if String(item.id).ends_with(suffix) and item.get("rect", Rect2()).has_area():
			return String(item.id)
	return ""


static func focus(id: String, read: Callable, send: Callable, poll: Callable, timeout_ms := TIMEOUT_MS) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	if not await until(func(): return not target(read.call(), id).is_empty(), poll, timeout_ms):
		return false
	while Time.get_ticks_msec() < deadline:
		var nav: Dictionary = read.call().duplicate(true)
		if nav.get("focus", "") == id:
			if nav.get("frame", false) and not target(nav, id).is_empty():
				return true
			await poll.call()
			continue
		var before := String(nav.get("focus", ""))
		await send.call()
		if not await until(func():
			var current: Dictionary = read.call()
			if current.get("focus", "") != before:
				return current.get("frame", false)
			# A click-away catcher can consume Tab without moving focus.
			return current.get("log", []) != nav.get("log", []) or current.get("targets", []) != nav.get("targets", []) or current.get("catchers", []) != nav.get("catchers", []),
			poll, maxi(0, deadline - Time.get_ticks_msec())):
			return false
	return false
