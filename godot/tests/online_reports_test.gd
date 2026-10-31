## OpenBFME. GPL-3.0. Asset-free reporting controls and safe retry against a local fake lobby.
extends SceneTree

var _errors: Array[String] = []


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, message: String) -> void:
	if not ok:
		_errors.append(message)


func _wait_request(panel: Node) -> void:
	var deadline := Time.get_ticks_msec() + 10000
	while panel._busy and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(not panel._busy, "report request timed out")


func _run() -> void:
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(load("res://openbfme.gdextension") != null, "openbfme.gdextension did not load")
	var url := "http://127.0.0.1:1"
	var preview := false
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--url="):
			url = argument.trim_prefix("--url=")
		elif argument == "--preview":
			preview = true
	var background := ColorRect.new()
	background.color = Color("18242c")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_child(background)
	var title := Label.new()
	title.text = "Online game — report controls"
	title.position = Vector2(24, 24)
	title.add_theme_font_size_override("font_size", 24)
	root.add_child(title)
	var panel: Node = load("res://scripts/online_reports.gd").new()
	panel.lobby_url = url
	root.add_child(panel)
	panel.set_context({"game_id": "0123456789abcdef", "session_token": "fake-lobby-session", "report_players": [
		{"id": "guest:1111111111111111", "name": "You", "you": true},
		{"id": "guest:2222222222222222", "name": "Other player", "you": false},
	]})
	panel.set_available(true)
	_check(panel._button.visible, "report entry should be available")
	_check(panel._players.size() == 1, "the local player must not be offered as a target")
	panel.open_dialog()
	_check(not panel._dialog.get_ok_button().disabled, "a category is a reason; the additional note is optional")
	panel._reason.text = "Describe the incident and when it happened."
	panel._reason.text_changed.emit()
	_check(not panel._dialog.get_ok_button().disabled, "a reason enables submission")
	if preview:
		await create_timer(600).timeout
		quit(0)
		return
	panel.set_available(false)
	_check(not panel._dialog.visible, "report dialog must not cover gameplay")
	panel.set_available(true)
	panel.open_dialog()
	panel._reason.text = "Reason about this game."
	panel._reason.text_changed.emit()
	var receipts: Array[String] = []
	panel.received.connect(func(id: String): receipts.append(id))
	panel.submit()
	await _wait_request(panel)
	var key: String = panel._key
	_check(not key.is_empty(), "a failed submit retains its idempotency key")
	_check(receipts.is_empty(), "a server failure must not claim receipt")
	panel.submit()
	await _wait_request(panel)
	_check(panel._key == key, "an unchanged retry must reuse its idempotency key")
	_check(receipts == ["R-aaaaaaaaaaaaaaaaaaaaaaaa"], "the confirmed receipt should be delivered once")
	_check(panel._dialog.get_ok_button().disabled, "a received report must not be submitted twice")
	panel._completed(HTTPRequest.RESULT_SUCCESS, 409, PackedStringArray(), JSON.stringify({"v": 1, "error": {"code": "already_reported", "message": "Already reported", "request_id": "test-request"}}).to_utf8_buffer())
	_check("already reported" in panel._status.text, "contract error envelopes should show the duplicate report message")
	panel.set_context({"game_id": "0123456789abcdef", "session_token": "fake-lobby-session", "report_players": []})
	_check(panel._players.size() == 1, "departed players remain reportable in the same game")
	panel.set_context({"game_id": "fedcba9876543210", "session_token": "fake-lobby-session", "report_players": []})
	panel.set_available(true)
	_check(not panel._button.visible, "a different game must not offer the previous game's targets")
	panel.free()
	if not _errors.is_empty():
		for error in _errors:
			printerr("REPORT UI FAIL: ", error)
		quit(1)
	else:
		print("REPORT UI PASS")
		quit(0)
