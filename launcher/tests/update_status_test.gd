extends SceneTree

const MainScene := preload("res://scenes/main.tscn")
const Main := preload("res://scripts/main.gd")

var _failures := PackedStringArray()


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures.append(what)
		print("UPDATE STATUS fail: " + what)


func _expect(main: Control, head: String, detail: String) -> void:
	var pill: String = main._play_screen.state_chip.get_child(0).text
	_check(pill == "●  " + head, "pill: " + pill + " (expected " + head + ")")
	# lane NIGHTLY-2: the bar may name a nightly by its short name, the full heading then being its tooltip
	_check(main._status_head.text == head or main._status_head.tooltip_text == head, "bottom heading: " + main._status_head.text)
	_check(main._status.text == "" if detail == "" else main._status.text.begins_with(detail), "bottom detail: " + main._status.text)
	_check(main._status.text == main._status_text, "detail was not rendered from the stored state")
	print("UPDATE STATUS pill: " + pill)
	print("UPDATE STATUS bottom: " + main._status_head.text + main._status_tail.text + " / " + main._status.text)


func _run() -> void:
	var mode := ""
	var head := ""
	var detail := ""
	var diagnostic := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--status-case="):
			mode = a.trim_prefix("--status-case=")
		elif a.begins_with("--status-head="):
			head = a.trim_prefix("--status-head=")
		elif a.begins_with("--status-detail="):
			detail = a.trim_prefix("--status-detail=")
		elif a.begins_with("--status-diagnostic="):
			diagnostic = a.trim_prefix("--status-diagnostic=")
	var early := Main.new()
	early._set_update_state("current", "", "Up to date.")
	_check(early._update_state == "current" and early._status_text == "Up to date.", "result before UI creation was lost")
	early.free()
	var main := MainScene.instantiate()
	root.add_child(main)
	await main._settle()
	_check(main._job_kind == "", "startup check did not finish")
	if mode.begins_with("manual"):
		_expect(main, "Offline — updates not checked", "")
		for i in 2:
			main._check.pressed.emit()
			_expect(main, "Checking for updates", "")
			_check(main._status.text == "", "a manual check kept the previous result")
			await main._settle()
			_check(main._job_kind == "", "manual check did not finish")
			_expect(main, head, detail)
	elif mode.begins_with("completion"):
		main._job_kind = "check"
		main._set_update_state("checking")
		_expect(main, "Checking for updates", "")
		var problem := "Update rejected: test failure" if mode == "completion-error" else ""
		main._finish(problem if problem != "" else detail, problem, "", "", main.PlayerMessages.Operation.UPDATE_CHECK)
		_expect(main, head, detail)
		# A queued worker message must not overwrite a completed result or start Updating again.
		main._on_progress("Checking for updates", 0, 0)
		main._on_progress("Downloading openbfme-v0.5.0-linux-x64.tar.gz", 1, 2)
		_expect(main, head, detail)
	else:
		_expect(main, head, detail)
	_check(main._update_state not in ["checking", "updating"], "completed check kept a transient state")
	_check(not main._status.text.begins_with("Checking for updates"), "completed check kept its progress message")
	if diagnostic != "":
		_check(main._message.visible, "update failure dialog was not shown")
		_check(main._message._details.contains(diagnostic), "Copy details lost the update diagnostic")
		_check(not main._message._text.text.contains(diagnostic), "dialog exposed the update diagnostic")
		_check(not main._status.text.contains(diagnostic), "status exposed the update diagnostic")
	if mode == "offline":
		_check(not main._status.visible, "offline retained a previous result")
	# Repainting after the UI was absent must use the stored result, not a label's old text.
	var label: Label = main._status_head
	main._status_head = null
	main._set_status("Saved update detail")
	main._status_head = label
	main._refresh_head()
	_expect(main, head, "Saved update detail")
	main.queue_free()
	await process_frame
	if _failures.is_empty():
		print("UPDATE STATUS OK")
	quit(0 if _failures.is_empty() else 1)
