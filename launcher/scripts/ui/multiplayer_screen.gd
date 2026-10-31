extends Control

signal join_invite(invite: Dictionary)

const W := preload("res://scripts/ui/widgets.gd")
const PlayerMessages := preload("res://scripts/ui/player_messages.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

signal problem_reported(detail: String, operation: int)
const PRIVACY_URL := "https://openbfme.com/privacy"
const Style := preload("res://scripts/ui/style.gd")
const GameList := preload("res://scripts/core/game_list.gd")
const GAME_COLUMNS := ["launcher.multiplayer_screen.col_game", "launcher.multiplayer_screen.col_host", "launcher.multiplayer_screen.col_players", "launcher.multiplayer_screen.col_map"]  # catalog ids

signal host_pressed
signal join_pressed(game_id: String)
signal refresh_pressed

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
# the public game list (GET /v1/games): shown signed in or not, only what the lobby returned (set_games)
var _host: Button
var _join: Button
var _rows: VBoxContainer
var _empty: Label
var _games_status: Label
var _games: Array = []
var _selected := -1
var _play_ready := false


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)


## the provider's name as shown to the player ("Discord" is a product name; "email" is a word)
static func _provider_name(provider: String) -> String:
	return L10n.t("launcher.multiplayer_screen.provider_email") if provider == "email" else "Discord"


func build(_repo: String) -> void:
	var body := W.vbox(Style.SCREEN_GAP)
	body.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(body)
	body.add_child(W.h2(L10n.t("launcher.multiplayer_screen.title")))
	_status = W.wrap(L10n.t("launcher.multiplayer_screen.sign_in_prompt"), "Muted")
	body.add_child(_status)
	var signin := W.panel()
	_signin = signin.panel
	body.add_child(_signin)
	var signin_row := W.hbox()
	_name = _text_input(L10n.t("launcher.multiplayer_screen.player_name"), 24)
	_name.name = "PlayerName"
	signin_row.add_child(_name)
	_connect = W.button(L10n.t("launcher.multiplayer_screen.sign_in"), "", "PrimaryButton")
	_connect.name = "SignIn"
	_connect.pressed.connect(_sign_in)
	_name.text_submitted.connect(func(_text: String) -> void: _sign_in())
	signin_row.add_child(_connect)
	signin.box.add_child(signin_row)
	_name.focus_neighbor_right = _name.get_path_to(_connect)
	_connect.focus_neighbor_left = _connect.get_path_to(_name)
	_recover_toggle = CheckBox.new()
	_recover_toggle.text = L10n.t("launcher.multiplayer_screen.use_recovery")
	for state in ["normal", "hover", "pressed", "hover_pressed", "disabled"]:
		var bg: Color = Style.HOVER if state.begins_with("hover") else Color.TRANSPARENT
		_recover_toggle.add_theme_stylebox_override(state, Style.box(bg, Color.TRANSPARENT, 0, 6, Vector2(0, 4)))
	signin.box.add_child(_recover_toggle)
	_recover = _text_input(L10n.t("launcher.multiplayer_screen.recovery_code"), 16)
	_recover.name = "RecoveryCode"
	_recover.secret = true
	_recover.visible = false
	_recover_toggle.toggled.connect(func(on: bool) -> void:
		_recover.visible = on
		if not on:
			_recover.clear())
	signin.box.add_child(_recover)
	_oauth_hint = W.wrap(L10n.t("launcher.multiplayer_screen.oauth_hint_restore"), "Muted")
	body.add_child(_oauth_hint)
	_oauth_row = W.hbox()
	# ONLINEMENU-1 (owner 2026-10-20): Discord sign-in and e-mail, both optional; everyone can just play
	for provider in ["discord", "email"]:
		var button := W.button(L10n.t("launcher.multiplayer_screen.sign_in_with", {"provider": _provider_name(provider)}))
		button.name = "OAuth" + provider.capitalize()
		button.pressed.connect(_oauth_action.bind(provider))
		_oauth_buttons[provider] = button
		_oauth_row.add_child(button)
	_oauth_check = W.button(L10n.t("launcher.multiplayer_screen.check_linked"))
	_oauth_check.name = "OAuthAccounts"
	_oauth_check.pressed.connect(_oauth_refresh)
	_oauth_row.add_child(_oauth_check)
	# the privacy page, linked from the sign-in screen only (owner 2026-10-20)
	_privacy = LinkButton.new()
	_privacy.name = "PrivacyLink"
	_privacy.text = L10n.t("launcher.multiplayer_screen.what_we_keep")
	_privacy.uri = PRIVACY_URL
	_oauth_row.add_child(_privacy)
	_oauth_cancel = W.button(L10n.t("common.cancel"))
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
	_code = W.button(L10n.t("launcher.multiplayer_screen.copy_friend_code"))
	_code.name = "CopyFriendCode"
	_code.pressed.connect(func() -> void:
		DisplayServer.clipboard_set(str(client.me.get("code", "")))
		_status.text = L10n.t("launcher.multiplayer_screen.friend_code_copied"))
	account_row.add_child(_code)
	var recovery := W.button(L10n.t("launcher.multiplayer_screen.recovery_code"))
	recovery.name = "GenerateRecovery"
	recovery.pressed.connect(_confirm_recovery)
	account_row.add_child(recovery)
	var out := W.button(L10n.t("launcher.multiplayer_screen.sign_out"))
	out.name = "SignOut"
	out.pressed.connect(func() -> void:
		if _oauth != null:
			_oauth.clear_session()
		client.sign_out()
		_status.text = L10n.t("launcher.multiplayer_screen.signed_out"))
	account_row.add_child(out)
	_account.add_child(account_row)
	var state_row := W.hbox()
	state_row.add_child(W.label(L10n.t("launcher.multiplayer_screen.presence"), "Muted"))
	_presence = OptionButton.new()
	for state in ["launcher.multiplayer_screen.presence_online", "launcher.multiplayer_screen.presence_away", "launcher.multiplayer_screen.presence_matching"]:
		_presence.add_item(L10n.t(state))
	_presence.item_selected.connect(func(_index: int) -> void: _set_status())
	state_row.add_child(_presence)
	_hidden = CheckBox.new()
	_hidden.text = L10n.t("launcher.multiplayer_screen.appear_offline")
	_hidden.toggled.connect(func(_on: bool) -> void: _set_status())
	state_row.add_child(_hidden)
	_account.add_child(state_row)
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tabs.visible = false
	body.add_child(_tabs)
	_build_games(body)
	var friends_page := _page("launcher.multiplayer_screen.tab_friends")
	var add_row := W.hbox()
	_friend_code = _text_input(L10n.t("launcher.multiplayer_screen.friend_code_hint"), 12)
	_friend_code.name = "FriendCode"
	add_row.add_child(_friend_code)
	var add := W.button(L10n.t("launcher.multiplayer_screen.add_friend"))
	add.name = "AddFriend"
	add.pressed.connect(_add_friend)
	_friend_code.text_submitted.connect(func(_text: String) -> void: _add_friend())
	add_row.add_child(add)
	friends_page.add_child(add_row)
	_friends = _list(friends_page)
	_requests = _list(_page("launcher.multiplayer_screen.tab_requests"))
	_invites = _list(_page("launcher.multiplayer_screen.tab_invites"))
	var chat_page := _page("launcher.multiplayer_screen.tab_chat")
	_chat_title = W.wrap(L10n.t("launcher.multiplayer_screen.choose_chat"), "Muted")
	chat_page.add_child(_chat_title)
	_chat_log = RichTextLabel.new()
	_chat_log.name = "ChatHistory"
	_chat_log.bbcode_enabled = false
	_chat_log.selection_enabled = true
	_chat_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	chat_page.add_child(_chat_log)
	_chat_more = W.button(L10n.t("launcher.multiplayer_screen.load_more"))
	_chat_more.disabled = true
	_chat_more.pressed.connect(func() -> void:
		if _chat_next != null:
			_load_chat(int(_chat_next)))
	chat_page.add_child(_chat_more)
	var send_row := W.hbox()
	_chat_text = _text_input(L10n.t("launcher.multiplayer_screen.message_hint"), 1000)
	_chat_text.name = "ChatMessage"
	_chat_text.text_submitted.connect(func(_text: String) -> void: _send_chat())
	send_row.add_child(_chat_text)
	var send := W.button(L10n.t("launcher.multiplayer_screen.send"))
	send.name = "SendChat"
	send.pressed.connect(_send_chat)
	send_row.add_child(send)
	chat_page.add_child(send_row)
	_secret_dialog = AcceptDialog.new()
	_secret_dialog.title = L10n.t("launcher.multiplayer_screen.secret_title")
	_secret_dialog.min_size = Vector2i(480, 190)
	var secret_body := W.vbox(16)
	secret_body.add_child(W.wrap(L10n.t("launcher.multiplayer_screen.secret_body")))
	_secret_label = W.label("", "Heading")
	secret_body.add_child(_secret_label)
	var copy := W.button(L10n.t("launcher.multiplayer_screen.copy_recovery"))
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
			_status.text = L10n.t("launcher.multiplayer_screen.signed_in"))
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
	var operation := PlayerMessages.Operation.LOBBY if client != null and client.is_signed_in() else PlayerMessages.Operation.SIGN_IN
	_status.text = PlayerMessages.social_error(message, operation)
	_chat_loading = false
	problem_reported.emit(message, operation)


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
		_ask_recovery(L10n.t("launcher.multiplayer_screen.unlink_title", {"provider": _provider_name(provider)}),
			L10n.t("launcher.multiplayer_screen.unlink_body"),
			func(code: String) -> void:
				if not client.busy and not _oauth.busy and client.is_signed_in():
					_oauth.set_session(client.oauth_session())
					_oauth.unlink(provider, code))
		return
	if client.is_signed_in() and not _oauth_providers.is_empty():
		# a second provider next to a linked one needs the recovery code too
		_ask_recovery(L10n.t("launcher.multiplayer_screen.link_title", {"provider": _provider_name(provider)}),
			L10n.t("launcher.multiplayer_screen.link_body", {"provider": _provider_name(provider)}),
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
	_oauth_hint.text = L10n.t("launcher.multiplayer_screen.oauth_hint_signed_in") if signed_in \
		else L10n.t("launcher.multiplayer_screen.oauth_hint_other_pc")
	for provider in _oauth_buttons:
		var button: Button = _oauth_buttons[provider]
		button.text = L10n.t("launcher.multiplayer_screen.unlink_button" if provider in _oauth_providers else "launcher.multiplayer_screen.link_button", {"provider": _provider_name(provider)}) if signed_in \
			else L10n.t("launcher.multiplayer_screen.sign_in_with", {"provider": _provider_name(provider)})
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


func _page(title_id: String) -> VBoxContainer:
	var page := W.vbox()
	page.name = L10n.t(title_id)
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
	_status.text = L10n.t("launcher.multiplayer_screen.signing_in")
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
	entry.placeholder_text = L10n.t("launcher.multiplayer_screen.recovery_code")
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
	dialog.title = L10n.t("launcher.multiplayer_screen.notice_title")
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
		var ask := _ask_recovery(L10n.t("launcher.multiplayer_screen.new_code_title"),
			L10n.t("launcher.multiplayer_screen.new_code_linked"),
			func(code: String) -> void: client.create_recovery(code))
		for provider in _oauth_providers:
			ask.add_button(L10n.t("launcher.multiplayer_screen.confirm_with", {"provider": _provider_name(str(provider))}), false, str(provider))
		ask.custom_action.connect(func(action: StringName) -> void:
			ask.queue_free()
			if not client.busy and not _oauth.busy and client.is_signed_in():
				_oauth.set_session(client.oauth_session())
				_oauth.begin(String(action), "confirm"))
		return
	var dialog := ConfirmationDialog.new()
	dialog.title = L10n.t("launcher.multiplayer_screen.new_code_confirm_title")
	dialog.dialog_text = L10n.t("launcher.multiplayer_screen.new_code_confirm_body")
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
		_code.text = L10n.t("launcher.multiplayer_screen.copy_friend_code")
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
	_code.text = L10n.t("launcher.multiplayer_screen.code_copy", {"code": str(client.me.get("code", ""))})
	_hidden.set_pressed_no_signal(bool(client.me.get("hidden", false)))
	_clear(_friends)
	var presences := {}
	for item in client.snapshot.get("presence", []):
		presences[item.pid] = item
	for friend in client.friends:
		var row := W.hbox()
		var presence: Dictionary = presences.get(friend.pid, {})
		var label := W.trim_label(L10n.t("launcher.multiplayer_screen.friend_line", {"nick": str(friend.nick), "state": str(presence.get("state", "offline")).replace("_", " ")}))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		var chat := W.button(L10n.t("launcher.multiplayer_screen.chat"))
		chat.pressed.connect(_open_chat.bind(str(friend.pid), str(friend.nick)))
		chat.disabled = client.busy
		row.add_child(chat)
		var remove := W.button(L10n.t("launcher.multiplayer_screen.remove"))
		remove.pressed.connect(_remove_friend.bind(str(friend.pid), str(friend.nick)))
		remove.disabled = client.busy
		row.add_child(remove)
		_friends.add_child(row)
	if client.friends.is_empty():
		_friends.add_child(W.wrap(L10n.t("launcher.multiplayer_screen.no_friends"), "Muted"))
	_clear(_requests)
	for request in client.snapshot.get("requests", []):
		var incoming: bool = request.to.pid == client.me.get("pid")
		var row := W.hbox()
		var peer: Dictionary = request.from if incoming else request.to
		var label := W.trim_label(L10n.t("launcher.multiplayer_screen.request_incoming" if incoming else "launcher.multiplayer_screen.request_sent", {"nick": str(peer.nick)}))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		if incoming:
			for accept in [true, false]:
				var button := W.button(L10n.t("launcher.multiplayer_screen.accept") if accept else L10n.t("launcher.multiplayer_screen.decline"))
				button.pressed.connect(func() -> void: client.respond_request(str(request.id), accept))
				button.disabled = client.busy
				row.add_child(button)
		_requests.add_child(row)
	if _requests.get_child_count() == 0:
		_requests.add_child(W.wrap(L10n.t("launcher.multiplayer_screen.no_requests"), "Muted"))
	for notice in client.snapshot.get("notices", []):
		if not _notices.has(notice.id):
			_status.text = {"declined": L10n.t("launcher.multiplayer_screen.notice_declined"), "busy": L10n.t("launcher.multiplayer_screen.notice_busy"),
				"expired": L10n.t("launcher.multiplayer_screen.notice_expired"), "recovered": L10n.t("launcher.multiplayer_screen.notice_recovered")}.get(notice.kind, "")
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
	dialog.title = L10n.t("launcher.multiplayer_screen.remove_title")
	dialog.dialog_text = L10n.t("launcher.multiplayer_screen.remove_body", {"nick": nick})
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
		_invites.add_child(W.wrap(L10n.t("launcher.multiplayer_screen.no_invites"), "Muted"))


func _invite_card(invite: Dictionary, toast: bool) -> VBoxContainer:
	var card := W.vbox()
	var title := L10n.t("launcher.multiplayer_screen.invite_challenge" if invite.kind == "challenge" else "launcher.multiplayer_screen.invite_invited", {"nick": str(invite.from.nick)})
	var incoming: bool = invite.to.pid == client.me.get("pid")
	if not incoming:
		title = L10n.t("launcher.multiplayer_screen.challenge_sent" if invite.kind == "challenge" else "launcher.multiplayer_screen.invitation_sent", {"nick": str(invite.to.nick)})
	if invite.get("mode") != null:
		title += L10n.t("launcher.multiplayer_screen.invite_mode", {"mode": str(invite.mode)})
	card.add_child(W.wrap(title))
	if str(invite.get("note", "")) != "":
		card.add_child(W.wrap(str(invite.note), "Muted"))
	if not incoming:
		return card
	var buttons := W.hbox()
	var join := W.button(L10n.t("launcher.multiplayer_screen.join"), "", "PrimaryButton")
	join.name = "JoinInvite"
	join.disabled = client.busy
	join.pressed.connect(func() -> void:
		if int(invite.expires) > int(Time.get_unix_time_from_system()):
			join_invite.emit(invite))
	buttons.add_child(join)
	var decline := W.button(L10n.t("launcher.multiplayer_screen.decline"))
	decline.disabled = client.busy
	decline.pressed.connect(func() -> void: client.decline_invite(str(invite.id)))
	buttons.add_child(decline)
	if toast:
		var dismiss := W.button(L10n.t("launcher.multiplayer_screen.later"))
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
	_chat_title.text = L10n.t("launcher.multiplayer_screen.chat_with", {"nick": nick})
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
		lines.append(L10n.t("launcher.multiplayer_screen.chat_you" if message.from == client.me.get("pid") else "launcher.multiplayer_screen.chat_friend", {"text": str(message.text)}))
		_chat_last = maxi(_chat_last, int(message.id))
	_chat_log.text = "\n\n".join(lines) if not lines.is_empty() else L10n.t("launcher.multiplayer_screen.no_messages")
	if _chat_messages.size() == 200:
		_chat_log.text = L10n.t("launcher.multiplayer_screen.last_200") + "\n\n" + _chat_log.text
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


## Open games on the lobby. Addresses stay behind the relay. A locked, started or full game is listed and is not joined from here
## (there is no password field: a locked game is joined inside the game). LAN games stay in the game's own Multiplayer menu.
func _build_games(body: VBoxContainer) -> void:
	var p := W.panel(0)
	p.panel.name = "PublicGames"
	p.panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(p.panel)
	var top := W.hbox(8)
	var what := W.wrap(L10n.t("launcher.multiplayer_screen.games_intro"), "Muted")
	what.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	what.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_host = W.button(L10n.t("launcher.multiplayer_screen.host_game"))
	_host.name = "HostGame"
	_join = W.button(L10n.t("launcher.multiplayer_screen.join_game"))
	_join.name = "JoinGame"
	var refresh := W.button(L10n.t("launcher.multiplayer_screen.refresh_games"))
	refresh.name = "RefreshGames"
	for b in [_host, _join, refresh]:
		b.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_host.pressed.connect(func() -> void: host_pressed.emit())
	_join.pressed.connect(_on_join)
	refresh.pressed.connect(func() -> void: refresh_pressed.emit())
	top.add_child(_host)
	top.add_child(_join)
	top.add_child(refresh)
	top.add_child(what)  # the buttons first: on the left edge, as the sign-in panel's name field above
	p.box.add_child(top)
	var gap := Control.new()
	gap.custom_minimum_size.y = 12
	p.box.add_child(gap)
	var head := W.hbox(0)
	for c in GAME_COLUMNS.size():
		var l := W.label(L10n.t(String(GAME_COLUMNS[c])).to_upper(), "Section")
		l.custom_minimum_size = Vector2(0, 30)
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		l.size_flags_stretch_ratio = 3.0 if c == 0 else 1.0
		head.add_child(l)
	p.box.add_child(head)
	var rule := ColorRect.new()
	rule.color = Style.LINE
	rule.custom_minimum_size.y = 1
	p.box.add_child(rule)
	_empty = W.label(L10n.t("launcher.multiplayer_screen.no_open_games"))
	Style.text(_empty, "CormorantGaramond-Italic", 500, 22, Style.TEXT2)
	_empty.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_empty.custom_minimum_size.y = 80
	_empty.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	p.box.add_child(_empty)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	p.box.add_child(scroll)
	_rows = W.vbox(0)
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_rows)
	_games_status = W.label("", "Muted")
	_games_status.visible = false
	p.box.add_child(_games_status)
	set_games([])


## Host and Join follow whether a version can be started. Join also needs a joinable row.
func set_ready(can_play: bool) -> void:
	_play_ready = can_play
	_apply_ready()


## replace the list. Only rows the lobby returned. Pass [] for none.
func set_games(games: Array) -> void:
	_games = games
	_selected = -1
	for c in _rows.get_children():
		_rows.remove_child(c)
		c.free()
	_empty.visible = games.is_empty()
	_rows.get_parent().visible = not games.is_empty()
	for i in games.size():
		var g: Dictionary = games[i]
		var row := Button.new()
		# a Button does not grow to fit the labels inside it: without this the rows are about 12 px and overlap
		row.custom_minimum_size.y = 32
		row.theme_type_variation = "FlatButton"
		row.focus_mode = Control.FOCUS_ALL
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var box := W.hbox(0)
		box.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.set_anchors_preset(Control.PRESET_FULL_RECT)
		row.add_child(box)
		var cells := [GameList.display_name(g), str(g.host), "%d" % int(g.players), str(g.map)]
		for c in cells.size():
			var l := W.trim_label(cells[c])
			l.tooltip_text = cells[c]
			l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			l.size_flags_stretch_ratio = 3.0 if c == 0 else 1.0
			l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			l.custom_minimum_size = Vector2(0, 32)
			box.add_child(l)
		var index := i
		row.pressed.connect(_select.bind(index))
		row.gui_input.connect(_on_row_input.bind(index))
		_rows.add_child(row)
	_apply_ready()


func set_games_status(text: String) -> void:
	_games_status.text = text
	_games_status.visible = text != ""


func _select(index: int) -> void:
	_selected = index
	for i in _rows.get_child_count():
		var row := _rows.get_child(i) as Button
		if row == null:
			continue
		row.modulate = Color(1, 1, 1, 1) if i != index else Color(1.15, 1.08, 0.85, 1)
	_apply_ready()


func _on_row_input(event: InputEvent, index: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.double_click and event.button_index == MOUSE_BUTTON_LEFT:
		_select(index)
		if _can_join():
			join_pressed.emit(str(_games[index].id))


func _on_join() -> void:
	if _can_join():
		join_pressed.emit(str(_games[_selected].id))


func _can_join() -> bool:
	return _play_ready and _selected >= 0 and _selected < _games.size() and GameList.joinable(_games[_selected])


func _apply_ready() -> void:
	if _host == null:
		return
	_host.disabled = not _play_ready
	_join.disabled = not _can_join()
	_host.tooltip_text = "" if _play_ready else L10n.t("launcher.multiplayer_screen.host_needs_version")
	_join.tooltip_text = "" if _can_join() else L10n.t("launcher.multiplayer_screen.select_game")
