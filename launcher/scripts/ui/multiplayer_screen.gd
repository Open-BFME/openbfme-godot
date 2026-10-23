extends Control

signal join_invite(invite: Dictionary)

const W := preload("res://scripts/ui/widgets.gd")
const PROVIDER_NAMES := {"discord": "Discord", "email": "e-mail"}
const PRIVACY_URL := "https://openbfme.com/privacy"
const Style := preload("res://scripts/ui/style.gd")

var client: Node
var _signin: PanelContainer
var _name: LineEdit
var _recover: LineEdit
var _recover_toggle: CheckBox
var _connect: Button
var _account: VBoxContainer
var _identity: Label
var _code: Button
var _status: Label
var _presence: OptionButton
var _hidden: CheckBox
var _friend_code: LineEdit
var _friends: VBoxContainer
var _requests: VBoxContainer
var _invites: VBoxContainer
var _tabs: TabContainer
var _chat_title: Label
var _chat_log: RichTextLabel
var _chat_text: LineEdit
var _chat_more: Button
var _chat_pid := ""
var _chat_messages: Array = []
var _chat_next: Variant = null
var _chat_last := 0
var _chat_loading := false
var _chat_observed_head := 0
var _pending_send: Dictionary = {}
var _toast: PanelContainer
var _toast_body: VBoxContainer
var _dismissed: Dictionary = {}
var _notices: Dictionary = {}
var _privacy: LinkButton
var _secret_dialog: AcceptDialog
var _secret_label: Label
var _blocked := ""
var _rendered_invites := ""
var _oauth: Node
var _oauth_buttons: Dictionary = {}
var _oauth_providers: Array = []
var _oauth_row: HBoxContainer
var _oauth_hint: Label
var _oauth_check: Button
var _oauth_cancel: Button
var _oauth_adopting := false


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)


func build(_repo: String) -> void:
	var body := W.vbox(Style.SCREEN_GAP)
	body.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(body)
	body.add_child(W.h2("Multiplayer"))
	_status = W.wrap("Sign in to see your friends and invitations.", "Muted")
	body.add_child(_status)
	var signin := W.panel()
	_signin = signin.panel
	body.add_child(_signin)
	var signin_row := W.hbox()
	_name = _text_input("Your player name", 24)
	_name.name = "PlayerName"
	signin_row.add_child(_name)
	_connect = W.button("Sign in", "", "PrimaryButton")
	_connect.name = "SignIn"
	_connect.pressed.connect(_sign_in)
	_name.text_submitted.connect(func(_text: String) -> void: _sign_in())
	signin_row.add_child(_connect)
	signin.box.add_child(signin_row)
	_recover_toggle = CheckBox.new()
	_recover_toggle.text = "Use a recovery code on this PC"
	for state in ["normal", "hover", "pressed", "hover_pressed", "disabled"]:
		var bg: Color = Style.HOVER if state.begins_with("hover") else Color.TRANSPARENT
		_recover_toggle.add_theme_stylebox_override(state, Style.box(bg, Color.TRANSPARENT, 0, 6, Vector2(0, 4)))
	signin.box.add_child(_recover_toggle)
	_recover = _text_input("Recovery code", 16)
	_recover.name = "RecoveryCode"
	_recover.secret = true
	_recover.visible = false
	_recover_toggle.toggled.connect(func(on: bool) -> void:
		_recover.visible = on
		if not on:
			_recover.clear())
	signin.box.add_child(_recover)
	_oauth_hint = W.wrap("Already linked on another PC? Restore before using ordinary sign-in.", "Muted")
	body.add_child(_oauth_hint)
	_oauth_row = W.hbox()
	# ONLINEMENU-1 (owner 2026-10-20): Discord sign-in and e-mail, both optional; everyone can just play
	for provider in ["discord", "email"]:
		var button := W.button("Sign in with " + PROVIDER_NAMES[provider])
		button.name = "OAuth" + provider.capitalize()
		button.pressed.connect(_oauth_action.bind(provider))
		_oauth_buttons[provider] = button
		_oauth_row.add_child(button)
	_oauth_check = W.button("Check linked accounts")
	_oauth_check.name = "OAuthAccounts"
	_oauth_check.pressed.connect(_oauth_refresh)
	_oauth_row.add_child(_oauth_check)
	# the privacy page, linked from the sign-in screen only (owner 2026-10-20)
	_privacy = LinkButton.new()
	_privacy.name = "PrivacyLink"
	_privacy.text = "What we keep"
	_privacy.uri = PRIVACY_URL
	_oauth_row.add_child(_privacy)
	_oauth_cancel = W.button("Cancel")
	_oauth_cancel.name = "OAuthCancel"
	_oauth_cancel.pressed.connect(func() -> void: _oauth.cancel())
	_oauth_row.add_child(_oauth_cancel)
	body.add_child(_oauth_row)
	_oauth_row.visible = false
	_oauth_hint.visible = false
	_account = W.vbox()
	_account.visible = false
	body.add_child(_account)
	var account_row := W.hbox()
	_identity = W.trim_label("")
	_identity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	account_row.add_child(_identity)
	_code = W.button("Copy friend code")
	_code.name = "CopyFriendCode"
	_code.pressed.connect(func() -> void:
		DisplayServer.clipboard_set(str(client.me.get("code", "")))
		_status.text = "Friend code copied.")
	account_row.add_child(_code)
	var recovery := W.button("Recovery code")
	recovery.name = "GenerateRecovery"
	recovery.pressed.connect(_confirm_recovery)
	account_row.add_child(recovery)
	var out := W.button("Sign out")
	out.name = "SignOut"
	out.pressed.connect(func() -> void:
		if _oauth != null:
			_oauth.clear_session()
		client.sign_out()
		_status.text = "Signed out.")
	account_row.add_child(out)
	_account.add_child(account_row)
	var state_row := W.hbox()
	state_row.add_child(W.label("Presence", "Muted"))
	_presence = OptionButton.new()
	for state in ["Online", "Away", "Looking for a game"]:
		_presence.add_item(state)
	_presence.item_selected.connect(func(_index: int) -> void: _set_status())
	state_row.add_child(_presence)
	_hidden = CheckBox.new()
	_hidden.text = "Appear offline"
	_hidden.toggled.connect(func(_on: bool) -> void: _set_status())
	state_row.add_child(_hidden)
	_account.add_child(state_row)
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tabs.visible = false
	body.add_child(_tabs)
	var friends_page := _page("Friends")
	var add_row := W.hbox()
	_friend_code = _text_input("Friend code: XXXX-XXXX-XX", 12)
	_friend_code.name = "FriendCode"
	add_row.add_child(_friend_code)
	var add := W.button("Add friend")
	add.name = "AddFriend"
	add.pressed.connect(_add_friend)
	_friend_code.text_submitted.connect(func(_text: String) -> void: _add_friend())
	add_row.add_child(add)
	friends_page.add_child(add_row)
	_friends = _list(friends_page)
	_requests = _list(_page("Requests"))
	_invites = _list(_page("Invites"))
	var chat_page := _page("Chat")
	_chat_title = W.wrap("Choose Chat next to a friend.", "Muted")
	chat_page.add_child(_chat_title)
	_chat_log = RichTextLabel.new()
	_chat_log.name = "ChatHistory"
	_chat_log.bbcode_enabled = false
	_chat_log.selection_enabled = true
	_chat_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	chat_page.add_child(_chat_log)
	_chat_more = W.button("Load more messages")
	_chat_more.disabled = true
	_chat_more.pressed.connect(func() -> void:
		if _chat_next != null:
			_load_chat(int(_chat_next)))
	chat_page.add_child(_chat_more)
	var send_row := W.hbox()
	_chat_text = _text_input("Message", 1000)
	_chat_text.name = "ChatMessage"
	_chat_text.text_submitted.connect(func(_text: String) -> void: _send_chat())
	send_row.add_child(_chat_text)
	var send := W.button("Send")
	send.name = "SendChat"
	send.pressed.connect(_send_chat)
	send_row.add_child(send)
	chat_page.add_child(send_row)
	_secret_dialog = AcceptDialog.new()
	_secret_dialog.title = "Your recovery code"
	_secret_dialog.min_size = Vector2i(480, 190)
	var secret_body := W.vbox(16)
	secret_body.add_child(W.wrap("Keep this code somewhere safe. It is shown once and replaces your previous code."))
	_secret_label = W.label("", "Heading")
	secret_body.add_child(_secret_label)
	var copy := W.button("Copy recovery code")
	copy.pressed.connect(func() -> void: DisplayServer.clipboard_set(_secret_label.text))
	secret_body.add_child(copy)
	_secret_dialog.add_child(secret_body)
	_secret_dialog.confirmed.connect(_clear_secret)
	_secret_dialog.canceled.connect(_clear_secret)
	add_child(_secret_dialog)
	var timer := Timer.new()
	timer.wait_time = 1.0
	timer.timeout.connect(_refresh_invites)
	add_child(timer)
	timer.autostart = true


func bind_client(value: Node, toast_parent: Control, blocked := "") -> void:
	client = value
	_blocked = blocked
	client.changed.connect(_refresh)
	client.problem.connect(show_problem)
	client.recovery_ready.connect(func(code: String) -> void:
		_secret_label.text = code
		_secret_dialog.popup_centered())
	client.notice.connect(_show_notice)
	client.chat_ready.connect(_chat_received)
	client.action_done.connect(_action_done)
	client.connection_changed.connect(func(connected: bool) -> void:
		if connected:
			_status.text = "Signed in.")
	_toast = PanelContainer.new()
	_toast.name = "SocialInviteToast"
	_toast.add_theme_stylebox_override("panel", Style.box(Color("16151a"), Style.LINE, 1, Style.RADIUS, Vector2(20, 17)))
	_toast.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_toast.offset_left = -392
	_toast.offset_right = -24
	_toast.offset_top = 24
	_toast.visible = false
	_toast_body = W.vbox()
	_toast.add_child(_toast_body)
	toast_parent.add_child(_toast)
	_refresh()


func show_problem(message: String) -> void:
	_status.text = message
	_chat_loading = false


func bind_oauth(value: Node) -> void:
	_oauth = value
	_oauth.status_changed.connect(func(message: String) -> void:
		_refresh()
		_status.text = message)
	_oauth.recovery_code.connect(func(code: String) -> void:
		_secret_label.text = code
		_secret_dialog.popup_centered())
	_oauth.completed.connect(func(_pid: String, providers: Array) -> void:
		_oauth_providers = providers.duplicate()
		_refresh())
	_oauth.session_ready.connect(func(response: Dictionary) -> void:
		_oauth_adopting = true
		var adopted: bool = client.adopt_oauth_session(response)
		_oauth_adopting = false
		if not adopted:
			_oauth.clear_session()
			_oauth_providers.clear())
	client.connection_changed.connect(func(connected: bool) -> void:
		if not connected and client.token == "" and not _oauth_adopting:
			_oauth.clear_session()
			_oauth_providers.clear()
		_refresh())
	_refresh()


func oauth_in_progress() -> bool:
	return _oauth != null and _oauth.busy


func _oauth_refresh() -> void:
	if _oauth == null or client.busy or _oauth.busy or _blocked != "" or not client.is_signed_in():
		return
	_oauth.set_session(client.oauth_session())
	_oauth.refresh()


func _oauth_action(provider: String) -> void:
	if _oauth == null or client.busy or _oauth.busy or _blocked != "":
		return
	if client.is_signed_in() and provider in _oauth_providers:
		# ONLINEMENU-1 (PR #8 review, finding 2): unlinking needs the player's recovery code
		_ask_recovery("Unlink " + PROVIDER_NAMES[provider] + "?",
			"This provider will no longer restore your friends on another PC. Your local player and friends remain.\n\nEnter your recovery code to confirm.",
			func(code: String) -> void:
				if not client.busy and not _oauth.busy and client.is_signed_in():
					_oauth.set_session(client.oauth_session())
					_oauth.unlink(provider, code))
		return
	if client.is_signed_in() and not _oauth_providers.is_empty():
		# a second provider next to a linked one needs the recovery code too
		_ask_recovery("Link " + PROVIDER_NAMES[provider] + "?",
			"Another account is already linked to this player. Enter your recovery code to link " + PROVIDER_NAMES[provider] + " as well.",
			func(code: String) -> void:
				if not client.busy and not _oauth.busy and client.is_signed_in():
					_oauth.set_session(client.oauth_session())
					_oauth.begin(provider, "link", {}, code))
		return
	var signed_in: bool = client.is_signed_in()
	var identity: Dictionary = {}
	if not signed_in:
		identity = await client.oauth_restore_identity(_name.text)
		if identity.is_empty():
			return
	_oauth.set_session(client.oauth_session())
	_oauth.begin(provider, "link" if signed_in else "restore", identity)


func _refresh_oauth(signed_in: bool) -> void:
	_oauth_row.visible = _oauth != null
	_oauth_hint.visible = _oauth != null
	if _oauth == null:
		return
	_oauth_hint.text = "Optional: link Discord or an e-mail address to take this player and its friends to another PC." if signed_in \
		else "Played on another PC? Sign in with the Discord account or e-mail address you linked there. Installs with another player cannot be merged."
	for provider in _oauth_buttons:
		var button: Button = _oauth_buttons[provider]
		button.text = ("Unlink " if provider in _oauth_providers else "Link ") + PROVIDER_NAMES[provider] if signed_in \
			else "Sign in with " + PROVIDER_NAMES[provider]
		button.disabled = client.busy or _oauth.busy or _blocked != ""
	_privacy.visible = not signed_in
	_oauth_check.visible = signed_in
	_oauth_check.disabled = client.busy or _oauth.busy or _blocked != ""
	_oauth_cancel.visible = _oauth.busy


func _text_input(hint: String, limit: int) -> LineEdit:
	var edit := LineEdit.new()
	edit.placeholder_text = hint
	edit.max_length = limit
	edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return edit


func _page(title: String) -> VBoxContainer:
	var page := W.vbox()
	page.name = title
	_tabs.add_child(page)
	return page


func _list(parent: Control) -> VBoxContainer:
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	parent.add_child(scroll)
	var list := W.vbox(12)
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	return list


func _clear(container: Node) -> void:
	for child in container.get_children():
		container.remove_child(child)
		child.queue_free()


func _sign_in() -> void:
	if client == null or _blocked != "" or client.busy or oauth_in_progress():
		return
	var recovery := _recover.text if _recover_toggle.button_pressed else ""
	_recover.clear()
	_status.text = "Signing in…"
	client.sign_in(_name.text, recovery)


func _add_friend() -> void:
	if not client.busy:
		client.request_friend(_friend_code.text)


func _set_status() -> void:
	if not client.busy:
		client.set_status(["online", "away", "matching"][_presence.selected], _hidden.button_pressed)


## ONLINEMENU-1: a confirmation that asks for the player's recovery code (unlinking, a second linked account); `done` gets the code as typed,
## upper-cased with spaces and dashes removed
func _ask_recovery(title: String, text: String, done: Callable) -> ConfirmationDialog:
	var dialog := ConfirmationDialog.new()
	dialog.title = title
	var box := W.vbox()
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size = Vector2(420, 0)
	box.add_child(label)
	var entry := LineEdit.new()
	entry.name = "RecoveryCode"
	entry.placeholder_text = "Recovery code"
	entry.secret = true
	entry.max_length = 32
	box.add_child(entry)
	dialog.add_child(box)
	dialog.register_text_enter(entry)
	dialog.confirmed.connect(func() -> void:
		var code := entry.text.strip_edges().to_upper().replace(" ", "").replace("-", "")
		entry.text = ""
		dialog.queue_free()
		done.call(code))
	dialog.canceled.connect(func() -> void:
		entry.text = ""
		dialog.queue_free())
	add_child(dialog)
	dialog.popup_centered()
	entry.grab_focus()
	return dialog


## ONLINEMENU-1: a sign-in notice (moderation, or the player moved to another PC), shown once
func _show_notice(text: String) -> void:
	var dialog := AcceptDialog.new()
	dialog.name = "OnlineNotice"
	dialog.title = "Online notice"
	dialog.dialog_text = text
	dialog.dialog_autowrap = true
	dialog.min_size = Vector2i(480, 160)
	dialog.confirmed.connect(dialog.queue_free)
	dialog.canceled.connect(dialog.queue_free)
	add_child(dialog)
	dialog.popup_centered()


func _confirm_recovery() -> void:
	if client.busy:
		return
	if _oauth != null and not _oauth_providers.is_empty():
		# ONLINEMENU-1 (Sol r2): a linked player's new code needs the current one, or a fresh sign-in with the linked account
		var ask := _ask_recovery("Create a new recovery code?",
			"An account is linked to this player, so a new code needs your current recovery code. Lost it? Confirm with the linked account instead.",
			func(code: String) -> void: client.create_recovery(code))
		for provider in _oauth_providers:
			ask.add_button("Confirm with " + str(PROVIDER_NAMES.get(provider, provider)), false, str(provider))
		ask.custom_action.connect(func(action: StringName) -> void:
			ask.queue_free()
			if not client.busy and not _oauth.busy and client.is_signed_in():
				_oauth.set_session(client.oauth_session())
				_oauth.begin(String(action), "confirm"))
		return
	var dialog := ConfirmationDialog.new()
	dialog.title = "Create a recovery code?"
	dialog.dialog_text = "This replaces any previous recovery code. Keep the new code private."
	dialog.confirmed.connect(func() -> void:
		dialog.queue_free()
		client.create_recovery())
	dialog.canceled.connect(dialog.queue_free)
	add_child(dialog)
	dialog.popup_centered()


func _clear_secret() -> void:
	_secret_label.text = ""
	_secret_dialog.hide()


func _refresh() -> void:
	if client == null:
		return
	var signed_in: bool = client.is_signed_in()
	_refresh_oauth(signed_in)
	_signin.visible = not signed_in
	_account.visible = signed_in
	_tabs.visible = signed_in
	_connect.disabled = client.busy or oauth_in_progress() or _blocked != ""
	_set_action_buttons(_account, client.busy)
	_set_action_buttons(_tabs, client.busy)
	if _toast != null:
		_set_action_buttons(_toast, client.busy)
	if _blocked != "":
		_status.text = _blocked
	if not signed_in:
		_identity.text = ""
		_code.text = "Copy friend code"
		_clear(_friends)
		_clear(_requests)
		_clear_secret()
		_chat_pid = ""
		_chat_text.clear()
		_chat_log.text = ""
		_chat_messages.clear()
		_chat_last = 0
		_chat_loading = false
		_chat_observed_head = 0
		_pending_send.clear()
		_dismissed.clear()
		_notices.clear()
		_refresh_invites()
		return
	_identity.text = str(client.me.get("nick", ""))
	_code.text = str(client.me.get("code", "")) + " · Copy"
	_hidden.set_pressed_no_signal(bool(client.me.get("hidden", false)))
	_clear(_friends)
	var presences := {}
	for item in client.snapshot.get("presence", []):
		presences[item.pid] = item
	for friend in client.friends:
		var row := W.hbox()
		var presence: Dictionary = presences.get(friend.pid, {})
		var label := W.trim_label(str(friend.nick) + " · " + str(presence.get("state", "offline")).replace("_", " "))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		var chat := W.button("Chat")
		chat.pressed.connect(_open_chat.bind(str(friend.pid), str(friend.nick)))
		chat.disabled = client.busy
		row.add_child(chat)
		var remove := W.button("Remove")
		remove.pressed.connect(_remove_friend.bind(str(friend.pid), str(friend.nick)))
		remove.disabled = client.busy
		row.add_child(remove)
		_friends.add_child(row)
	if client.friends.is_empty():
		_friends.add_child(W.wrap("No friends yet. Share your code or add a friend above.", "Muted"))
	_clear(_requests)
	for request in client.snapshot.get("requests", []):
		var incoming: bool = request.to.pid == client.me.get("pid")
		var row := W.hbox()
		var peer: Dictionary = request.from if incoming else request.to
		var label := W.trim_label(str(peer.nick) + (" wants to be friends" if incoming else " · request sent"))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		if incoming:
			for accept in [true, false]:
				var button := W.button("Accept" if accept else "Decline")
				button.pressed.connect(func() -> void: client.respond_request(str(request.id), accept))
				button.disabled = client.busy
				row.add_child(button)
		_requests.add_child(row)
	if _requests.get_child_count() == 0:
		_requests.add_child(W.wrap("No pending friend requests.", "Muted"))
	for notice in client.snapshot.get("notices", []):
		if not _notices.has(notice.id):
			_status.text = {"declined": "An invitation was declined.", "busy": "Your friend is busy.",
				"expired": "An invitation expired.", "recovered": "Your account was recovered on this PC."}.get(notice.kind, "")
	var active_notices := {}
	for notice in client.snapshot.get("notices", []):
		active_notices[notice.id] = true
	_notices = active_notices
	_refresh_invites()
	for head in client.snapshot.get("chat", []):
		if head.pid == _chat_pid and int(head.last_id) > maxi(_chat_last, _chat_observed_head) and _chat_next == null and not _chat_loading and not client.busy:
			_chat_observed_head = int(head.last_id)
			_load_chat(_chat_last)


func _remove_friend(pid: String, nick: String) -> void:
	if client.busy:
		return
	var dialog := ConfirmationDialog.new()
	dialog.title = "Remove friend?"
	dialog.dialog_text = "Remove %s from your friends?" % nick
	dialog.confirmed.connect(func() -> void:
		dialog.queue_free()
		client.remove_friend(pid))
	dialog.canceled.connect(dialog.queue_free)
	add_child(dialog)
	dialog.popup_centered()


func _refresh_invites(force := false) -> void:
	if client == null or _toast == null:
		return
	var pending: Array = []
	if client.is_signed_in():
		for invite in client.snapshot.get("invites", []):
			if client.me.get("pid") in [invite.to.pid, invite.from.pid] and int(invite.expires) > int(Time.get_unix_time_from_system()):
				pending.append(invite)
	var fingerprint := JSON.stringify(pending)
	if not force and fingerprint == _rendered_invites:
		return
	_rendered_invites = fingerprint
	_clear(_invites)
	_clear(_toast_body)
	_toast.visible = false
	var live_ids := {}
	for invite in pending:
		live_ids[invite.id] = true
		_invites.add_child(_invite_card(invite, false))
		if invite.to.pid == client.me.get("pid") and not _toast.visible and not _dismissed.has(invite.id):
			_toast_body.add_child(_invite_card(invite, true))
			_toast.visible = true
	for id in _dismissed.keys():
		if not live_ids.has(id):
			_dismissed.erase(id)
	if pending.is_empty():
		_invites.add_child(W.wrap("No pending invitations.", "Muted"))


func _invite_card(invite: Dictionary, toast: bool) -> VBoxContainer:
	var card := W.vbox()
	var title := str(invite.from.nick) + (" challenges you" if invite.kind == "challenge" else " invited you")
	var incoming: bool = invite.to.pid == client.me.get("pid")
	if not incoming:
		title = ("Challenge sent to " if invite.kind == "challenge" else "Invitation sent to ") + str(invite.to.nick)
	if invite.get("mode") != null:
		title += " · " + str(invite.mode)
	card.add_child(W.wrap(title))
	if str(invite.get("note", "")) != "":
		card.add_child(W.wrap(str(invite.note), "Muted"))
	if not incoming:
		return card
	var buttons := W.hbox()
	var join := W.button("Join", "", "PrimaryButton")
	join.name = "JoinInvite"
	join.disabled = client.busy
	join.pressed.connect(func() -> void:
		if int(invite.expires) > int(Time.get_unix_time_from_system()):
			join_invite.emit(invite))
	buttons.add_child(join)
	var decline := W.button("Decline")
	decline.disabled = client.busy
	decline.pressed.connect(func() -> void: client.decline_invite(str(invite.id)))
	buttons.add_child(decline)
	if toast:
		var dismiss := W.button("Later")
		dismiss.pressed.connect(func() -> void:
			_dismissed[invite.id] = true
			_refresh_invites(true))
		buttons.add_child(dismiss)
	card.add_child(buttons)
	return card


func _open_chat(pid: String, nick: String) -> void:
	if client.busy:
		return
	_chat_pid = pid
	_chat_messages.clear()
	_chat_last = 0
	_chat_observed_head = 0
	_chat_next = null
	_chat_title.text = "Chat with " + nick
	_chat_log.text = ""
	_tabs.current_tab = 3
	_load_chat(0)


func _load_chat(after: int) -> void:
	if _chat_loading or client.busy:
		return
	_chat_loading = true
	client.get_chat(_chat_pid, after)


func _chat_received(pid: String, result: Dictionary) -> void:
	if pid != _chat_pid:
		return
	_chat_loading = false
	if int(result.get("after", 0)) == 0:
		_chat_messages.clear()
	var seen := {}
	for message in _chat_messages:
		seen[int(message.id)] = true
	for message in result.get("messages", []):
		if not seen.has(int(message.id)):
			_chat_messages.append(message)
	while _chat_messages.size() > 200:
		_chat_messages.pop_front()
	_chat_next = result.get("next")
	_chat_more.disabled = _chat_next == null
	var lines := PackedStringArray()
	for message in _chat_messages:
		lines.append(("You: " if message.from == client.me.get("pid") else "Friend: ") + str(message.text))
		_chat_last = maxi(_chat_last, int(message.id))
	_chat_log.text = "\n\n".join(lines) if not lines.is_empty() else "No messages yet."
	if _chat_messages.size() == 200:
		_chat_log.text = "Showing the last 200 loaded messages.\n\n" + _chat_log.text
	if visible and _tabs.current_tab == 3 and _chat_last > int(result.get("read_id", 0)):
		client.mark_chat_read.call_deferred(pid, _chat_last)


func _send_chat() -> void:
	if _chat_pid == "" or client.busy or _chat_text.text.is_empty():
		return
	_pending_send = {"pid": _chat_pid, "text": _chat_text.text}
	client.send_chat(_chat_pid, _chat_text.text)


func _action_done(kind: String, success: bool) -> void:
	if kind == "chat_send":
		if success and _pending_send.get("pid", "") == _chat_pid:
			if _chat_text.text == _pending_send.get("text", ""):
				_chat_text.clear()
			_load_chat(_chat_last)
		_pending_send.clear()


func _set_action_buttons(node: Node, disabled: bool) -> void:
	for child in node.get_children():
		if child is Button and child.name not in ["SignOut", "CopyFriendCode"]:
			child.disabled = disabled
		_set_action_buttons(child, disabled)
	_chat_more.disabled = disabled or _chat_next == null


func _exit_tree() -> void:
	if is_instance_valid(_toast):
		_toast.queue_free()
