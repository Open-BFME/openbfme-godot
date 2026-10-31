## SOCIAL-1 (Devin; landed by lane ONLINEMENU-1). Friends live in RotWK's own Messenger (AptMessenger), nothing drawn over the online screens (owner,
## 2026-10-20: the OpenBFME FRIENDS page "looks out of place and bad"; his choice: the game's Messenger):
##   * FRIENDS tab: the friends with retail's Buddy: status, and incoming friend requests ("Buddy Request"; the second button is RotWK's ACCEPT
##     FRIEND, the first declines); REMOVE FRIEND and INVITE as retail had them;
##   * PLAYERS tab: this room's players, recent players and the ignored ones (ADD FRIEND; IGNORE / UN-IGNORE, RotWK's own strings);
##   * the chat panel: the conversation with the chosen player; with nobody chosen, the player's friend code and how to add a friend. A friend code
##     typed into the Messenger's entry and sent with ENTER asks that player to be a friend; any other text is a message to the chosen player;
##   * a friend's game invite or challenge: GuiFX's own Yes / No box (RotWK's "Friend Game Invite" text); removing a friend and ignoring a player ask
##     there first too;
##   * what waits (requests, invites, unread chat) and the last result show as a line in the online menus' corner (game.gd).
## The recovery code is the launcher's (Multiplayer); who may see and invite the player is Options > Online. The data and the requests are the
## engine's (online_social_status / online_social_apply).
extends Node

var shell: Node
var reports: Node
var _data: Dictionary = {}
var _selected := ""
var _rows_key := ""
var _chat_key := ""
var _head_seen: Dictionary = {}
var _drafts: Dictionary = {}
var _read_ids: Dictionary = {}
var _notice_seen: Dictionary = {}
var _invites_seen: Dictionary = {}
var _requests_seen: Dictionary = {}
var _started := false
var _acknowledged_text := ""
var _messenger_pick := ""   ## the Messenger's selection last seen
var _notices: Array = []    ## the last results and arrivals (newest last), for the chat panel and the corner
var _notice_at := -100000   ## when the newest one arrived (ms): the corner shows it for NOTICE_MS
var _asks: Array = []       ## Yes / No questions waiting for GuiFX's box: {key, title, text, yes: Callable, no: Callable}
var _asking: Dictionary = {} ## the question the box shows now
var _ask_serial := 0
const NOTICE_MS := 8000
const CODE := "^[0-9A-Za-z]{4}-?[0-9A-Za-z]{4}-?[0-9A-Za-z]{2}$"


func _process(_delta: float) -> void:
	if not is_instance_valid(shell): return
	_data = shell.online_social_status()
	var online: Dictionary = shell.online_status()
	if not online.get("signed_in", false):
		if _started:
			if not _asking.is_empty(): shell.online_cancel_ask(_asking.key)
			_started = false
			_data.clear()
			_selected = ""
			_rows_key = ""
			_chat_key = ""
			_head_seen.clear()
			_drafts.clear()
			_read_ids.clear()
			_notice_seen.clear()
			_invites_seen.clear()
			_requests_seen.clear()
			_notices.clear()
			_asks.clear()
			_asking = {}
		return
	if _data.get("unsupported", false):
		_sync_unsupported()
		return
	if not _started and _data.get("available", false):
		_started = true
		_send({"action": "fetch_recent"})
		_send({"action": "fetch_blocks"})
		_send({"action": "fetch_threads"})
		# friend requests already waiting at the sign-in are Messenger rows (the corner counts them); invites are asked in the box as they come
		# (Sol: an invite has no Messenger row, so one only counted could not be answered)
		for request in _incoming_requests(): _requests_seen[request.id] = true
	for event in shell.online_social_events():
		if event.get("kind") == "error":
			push_warning("Online social request %s refused: %s." % [event.get("request", "unknown"), event.get("code", "unknown")])
			_notice(str(event.get("message", "The online action was refused.")))
		elif event.get("kind") == "chat" and not str(event.get("id", "")).is_empty():
			var pid: String = event.get("pid", "")
			if _drafts.has(pid) and _drafts[pid].client_id == event.id:
				if pid == _selected: _acknowledged_text = _drafts[pid].text
				_drafts.erase(pid)
	if not _started: return
	for request in _incoming_requests():
		if not _requests_seen.has(request.id):
			_requests_seen[request.id] = true
			_notice("%s has requested to be put on your friends list." % str(request.from.nick)) # RotWK's Buddy:AddNotification
	for invite in _incoming_invites():
		if not _invites_seen.has(invite.id):
			_invites_seen[invite.id] = true
			_ask_invite(invite)
	for notice in _data.get("notices", []):
		if not _notice_seen.has(notice.id):
			_notice_seen[notice.id] = true
			_notice(_notice_sentence(notice))
	if _notice_seen.size() > 100:
		_notice_seen.clear()
		for notice in _data.get("notices", []): _notice_seen[notice.id] = true
	_sync_messenger()
	_service_asks() # after the Messenger's buttons: a question one of them asks goes up in the same frame


## the corner's line (game.gd, under the ping): the newest result for a few seconds, else what waits in the Messenger; "" for nothing
func corner_line() -> String:
	if not _started: return ""
	if not _notices.is_empty() and Time.get_ticks_msec() - _notice_at < NOTICE_MS:
		return str(_notices.back())
	var parts := PackedStringArray()
	var requests := _incoming_requests().size()
	var invites := _incoming_invites().size()
	var unread := 0
	for head in _data.get("chat", []):
		if int(head.last_id) > int(head.read_id): unread += 1
	if requests: parts.append("%d friend request%s" % [requests, "" if requests == 1 else "s"])
	if invites: parts.append("%d invite%s" % [invites, "" if invites == 1 else "s"])
	if unread: parts.append("%d unread chat%s" % [unread, "" if unread == 1 else "s"])
	return "Messenger: " + ", ".join(parts) if not parts.is_empty() else ""


func _incoming_requests() -> Array:
	var me: String = _data.get("me", {}).get("pid", "")
	return _data.get("requests", []).filter(func(r): return r.to.pid == me)


func _incoming_invites() -> Array:
	var me: String = _data.get("me", {}).get("pid", "")
	var now := int(Time.get_unix_time_from_system())
	return _data.get("invites", []).filter(func(i): return i.to.pid == me and int(i.expires) > now)


## ONLINEMENU-1 (BATCH-P3 live-lobby check): a lobby without the social service (its /v1/health names no "social"): the Messenger says so,
## lists nobody and acts on nothing; nothing is asked of the lobby and nothing shows in the corner
const UNSUPPORTED_TEXT := "Friends and chat are not available on this server yet."


func _sync_unsupported() -> void:
	var spec: Dictionary = {}
	var key := "unsupported"
	if key != _rows_key:
		_rows_key = key
		spec.rows = [[], []]
	if key != _chat_key:
		_chat_key = key
		spec.chat = [[UNSUPPORTED_TEXT], [UNSUPPORTED_TEXT]]
	var messenger: Dictionary = shell.online_messenger_sync(spec)
	if not messenger.get("open", false):
		_rows_key = ""
		_chat_key = ""


func _sync_messenger() -> void:
	var spec: Dictionary = {}
	if not _acknowledged_text.is_empty():
		spec.clear_entry_if = _acknowledged_text
		_acknowledged_text = ""
	var rows := messenger_rows(_data)
	var key := JSON.stringify(rows)
	if key != _rows_key:
		_rows_key = key
		var native := func(row): return {"pid": row.id, "nick": row.name, "status": row.status, "request": row.request, "blocked": row.blocked, "friendable": row.friendable}
		spec.rows = [rows.friends.map(native), rows.players.map(native)]
	for pid in _drafts.keys():
		for message in _conversation(pid).get("messages", []):
			if message.get("client_id") == _drafts[pid].client_id:
				_drafts.erase(pid)
				break
	var lines := _chat_lines()
	key = _selected + JSON.stringify(lines)
	if key != _chat_key:
		_chat_key = key
		spec.chat = [lines, lines]
	if not _selected.is_empty() and _selected != _messenger_pick:
		spec.select = _selected
	var messenger: Dictionary = shell.online_messenger_sync(spec)
	if messenger.get("open", false):
		var selected := str(messenger.get("selected", ""))
		if selected != _messenger_pick:
			_messenger_pick = selected
			if selected != _selected: _select_player(selected)
		for action in messenger.get("actions", []): _messenger_action(action)
		if not _selected.is_empty(): _update_chat_head()
	else:
		_rows_key = ""
		_chat_key = ""
		_messenger_pick = ""


## the chat panel: the conversation with the chosen player, or (nobody chosen) the friend code, how to add a friend, and the last results
func _chat_lines() -> Array:
	if not _selected.is_empty():
		var chat: Array = _conversation(_selected).get("messages", [])
		var me: String = _data.get("me", {}).get("pid", "")
		var lines: Array = chat.slice(maxi(0, chat.size() - 200)).map(func(message): return "%s: %s" % ["You" if message.get("from") == me else _nick(_selected), message.get("text", "")])
		return lines if not lines.is_empty() else ["Type a message to %s below and press ENTER." % _nick(_selected)]
	var lines: Array = []
	var code := _code_text(str(_data.get("me", {}).get("code", "")))
	if not code.is_empty():
		lines.append("Your friend code: %s" % code)
		lines.append("To add a friend, type their code below and press ENTER.")
	lines.append("Select Buddy to chat.") # RotWK's Buddy:SelectBuddyToChat
	if not _notices.is_empty():
		lines.append("")
		lines.append_array(_notices)
	return lines


static func messenger_rows(data: Dictionary) -> Dictionary:
	var friends: Array = []
	var players: Array = []
	var seen: Dictionary = {}
	var blocked: Dictionary = {}
	var me: String = data.get("me", {}).get("pid", "")
	for row in data.get("blocks", []): blocked[row.pid] = row
	for row in data.get("room_players", []):
		if not row.get("pid") is String: continue
		if row.get("blocked", false) and not blocked.has(row.pid): blocked[row.pid] = {"pid": row.pid, "nick": row.get("nick", "Player")}
	var presence: Dictionary = {}
	for row in data.get("presence", []): presence[row.pid] = row
	var related: Dictionary = {} # a friend or a request either way: no ADD FRIEND
	for player in data.get("friends", []):
		related[player.pid] = true
		if blocked.has(player.pid): continue
		friends.append(_row(player.pid, player.nick, presence.get(player.pid, {}).get("state", "offline")))
	for request in data.get("requests", []):
		related[request.from.pid] = true
		related[request.to.pid] = true
		if request.to.pid == me and not blocked.has(request.from.pid):
			var row := _row(request.from.pid, request.from.nick, "")
			row.request = true
			friends.append(row)
	for player in data.get("room_players", []):
		if not player.get("pid") is String or blocked.has(player.pid): continue
		var row := _row(player.pid, player.nick, "In this room")
		row.friendable = not related.has(player.pid)
		players.append(row)
		seen[player.pid] = true
	for game in data.get("recent", []):
		for player in game.get("players", []):
			if not blocked.has(player.pid) and not seen.has(player.pid) and player.pid != me:
				var row := _row(player.pid, player.nick, "Recent player")
				row.friendable = not related.has(player.pid)
				players.append(row)
				seen[player.pid] = true
	for pid in blocked:
		var row := _row(pid, str(blocked[pid].get("nick", "Player")), "Ignored")
		row.blocked = true
		row.friendable = false
		players.append(row)
	return {"friends": friends, "players": players}


static func _row(pid: String, nick: String, status: String) -> Dictionary:
	return {"id": pid, "name": nick, "status": status, "request": false, "blocked": false, "friendable": true}


func _send(spec: Dictionary) -> bool:
	var result: Dictionary = shell.online_social_apply(spec)
	if not result.get("ok", false):
		_notice(str(result.get("error", "This action is unavailable; retry after the current request finishes.")))
		return false
	return true


func _notice(message: String) -> void:
	_notices.append(message)
	if _notices.size() > 5: _notices.pop_front()
	_notice_at = Time.get_ticks_msec()


## a request sent, then `done` as the notice
func _act(spec: Dictionary, done: String) -> bool:
	if not _send(spec): return false
	_notice(done)
	return true


func _notice_sentence(notice: Dictionary) -> String:
	var who := ""
	for invite in _data.get("invites", []):
		if invite.id == notice.get("invite"): who = str(invite.to.nick)
	match str(notice.get("kind", "")):
		"declined": return "%s declined your invite." % who if not who.is_empty() else "Your invite was declined."
		"busy": return "%s is busy and cannot answer your invite now." % who if not who.is_empty() else "The player you invited is busy right now."
		"expired": return "Your invite to %s expired." % who if not who.is_empty() else "Your invite expired."
		"recovered": return "Your account was restored with a recovery code."
	return "You have a new friends notice."


static func _code_text(code: String) -> String:
	var clean := code.replace("-", "").to_upper()
	return "%s-%s-%s" % [clean.substr(0, 4), clean.substr(4, 4), clean.substr(8)] if clean.length() == 10 else clean


# ---- GuiFX's Yes / No box ------------------------------------------------------------------------------------------------------------------------

func _ask(title: String, text: String, yes: Callable, no := Callable(), invite := "") -> void:
	_ask_serial += 1
	_asks.append({"key": "social%d" % _ask_serial, "title": title, "text": text, "yes": yes, "no": no, "invite": invite})


## one question at a time: the next one goes up once the box is free; an answer runs its action
func _service_asks() -> void:
	# Retire before consuming answers: a click can arrive in the same frame as the withdrawal.
	if not _asking.is_empty() and not str(_asking.invite).is_empty() and not _invite_open(_asking.invite):
		shell.online_cancel_ask(_asking.key)
		_asking = {}
		_notice("The invite is no longer available.")
	for answer in shell.online_answers():
		var parts := str(answer).split("|")
		if _asking.is_empty() or parts.size() != 2 or parts[0] != _asking.key: continue
		var action: Callable = _asking.yes if parts[1] == "yes" else _asking.no
		_asking = {}
		if action.is_valid(): action.call()
	# Sol (delta review): the box no longer shows the question and no answer came (another box took its place: the room closed under the player; or
	# the online screens went): it is asked again later, never acted on
	if not _asking.is_empty() and not shell.online_asking(_asking.key):
		_asks.push_front(_asking)
		_asking = {}
	# an invite withdrawn or expired before its question went up is not asked
	var open_invites: Dictionary = {}
	for invite in _incoming_invites(): open_invites[invite.id] = true
	_asks = _asks.filter(func(q): return str(q.invite).is_empty() or open_invites.has(q.invite))
	if _asking.is_empty() and not _asks.is_empty():
		var next: Dictionary = _asks.front()
		if shell.online_ask(next.key, next.title, next.text):
			_asking = _asks.pop_front()


func _ask_invite(invite: Dictionary) -> void:
	var nick := str(invite.from.nick)
	var challenge := str(invite.kind) == "challenge"
	var title := "Challenge" if challenge else "Friend Game Invite" # RotWK's APT:BuddyInviteTitle
	var text := ("%s challenges you to a 1v1 game (%s). Do you want to play?" % [nick, str(invite.get("mode", "normal")).capitalize()]) if challenge \
		else "%s has invited you to play a Open Play game. Do you want to jump to their game?" % nick # RotWK's APT:BuddyInviteTextOpenPlay
	var declined := "%s from %s declined." % ["Challenge" if challenge else "Invite", nick]
	_ask(title, text, _accept_invite.bind(str(invite.id), nick), _act.bind({"action": "decline_invite", "id": invite.id}, declined), str(invite.id))


## Sol (delta2 follow-up): an invite withdrawn while its box was up is answered with a notice, never accepted; the "Leave room?" question is
## tagged with the invite, so it is dropped with it, and its YES checks again before the player leaves the room
func _accept_invite(id: String, nick: String) -> void:
	if not _invite_open(id):
		_notice("The invite from %s is no longer open." % nick)
	elif shell.online_status().get("in_game", false):
		_ask("Leave room?", "To join %s you leave your current room. If you host it, it closes. Continue?" % nick, _leave_accept_invite.bind(id, nick), Callable(), id)
	else:
		_send({"action": "accept_invite", "id": id})


func _leave_accept_invite(id: String, nick: String) -> void:
	if not _invite_open(id):
		_notice("The invite from %s is no longer open." % nick)
		return
	_send({"action": "leave_accept_invite", "id": id})


func _invite_open(id: String) -> bool:
	for invite in _incoming_invites():
		if str(invite.id) == id: return true
	return false


# ---- the Messenger's buttons and entry ----------------------------------------------------------------------------------------------------------

func _messenger_action(action: Dictionary) -> void:
	var pid := str(action.get("pid", ""))
	match action.get("kind", ""):
		"select": pass
		"accept_friend":
			for request in _incoming_requests():
				if request.from.pid == pid:
					_act({"action": "accept_friend", "id": request.id}, "You and %s are now friends." % str(request.from.nick))
		"remove_friend":
			for request in _incoming_requests():
				if request.from.pid == pid:
					_act({"action": "decline_friend", "id": request.id}, "Friend request from %s declined." % str(request.from.nick))
					return
			_remove_friend(pid)
		"add_friend": _act({"action": "request_friend", "pid": pid}, "Friend request sent to %s." % _nick(pid))
		"invite": _act({"action": "send_invite", "pid": pid, "kind": "invite"}, "Invite sent to %s." % _nick(pid))
		"block": _block(pid)
		"unblock": _act({"action": "unblock_player", "pid": pid}, "%s is no longer ignored." % _nick(pid))
		"send": _entered(pid, str(action.get("text", "")))


## ENTER in the Messenger's entry: a friend code asks that player to be a friend (whoever is chosen); other text is a message to the chosen player
func _entered(pid: String, text: String) -> void:
	var typed := text.strip_edges()
	var regex := RegEx.create_from_string(CODE)
	if regex.search(typed) != null:
		var clean := typed.replace("-", "").to_upper()
		if clean == str(_data.get("me", {}).get("code", "")).replace("-", "").to_upper():
			_notice("That is your own friend code: give it to a friend so they can add you.")
		elif _act({"action": "request_friend_code", "code": clean}, "Friend request sent to %s." % _code_text(clean)):
			_acknowledged_text = text
		return
	if pid.is_empty():
		_notice("Select a friend to chat with, or type a friend code (XXXX-XXXX-XX) to add one.")
		return
	_message(pid, text)


func _remove_friend(pid: String) -> void:
	var nick := _nick(pid)
	_ask("Remove friend", "Remove %s from your friends? Your chat history stays." % nick, func(): _act({"action": "remove_friend", "pid": pid}, "%s removed from friends." % nick))


func _block(pid: String) -> void:
	var nick := _nick(pid)
	_ask("Ignore", "Ignore %s? You will not see their messages, invites or friend requests." % nick,
		func(): _act({"action": "block_player", "pid": pid, "note": ""}, "%s is ignored." % nick))


func _message(pid: String, text: String) -> void:
	if not _drafts.has(pid) or _drafts[pid].text != text:
		_drafts[pid] = {"text": text, "client_id": Crypto.new().generate_random_bytes(16).hex_encode()}
	var draft: Dictionary = _drafts[pid]
	_send({"action": "send_message", "pid": pid, "client_id": draft.client_id, "text": draft.text})


func _nick(pid: String) -> String:
	for row in messenger_rows(_data).friends + messenger_rows(_data).players:
		if row.id == pid: return str(row.name)
	for thread in _data.get("threads", []):
		if thread.get("pid") == pid: return str(thread.get("nick", "Player"))
	return "Player"


func _conversation(pid: String) -> Dictionary:
	return _data.get("conversations", {}).get(pid, {})


func _select_player(pid: String) -> void:
	_selected = pid
	if pid.is_empty(): return
	for player in _data.get("friends", []) + _data.get("threads", []) + _data.get("chat", []):
		if player.pid == pid:
			_fetch_messages(pid, 0)
			return
	if not _conversation(pid).get("messages", []).is_empty(): _fetch_messages(pid, 0)


func _fetch_messages(pid: String, after: int) -> bool:
	return _send({"action": "fetch_messages", "pid": pid, "after": after, "limit": 50})


func _update_chat_head() -> void:
	for head in _data.get("chat", []):
		if head.pid == _selected and int(head.last_id) != int(_head_seen.get(_selected, -1)):
			var existing: Array = _conversation(_selected).get("messages", [])
			var after := maxi(int(existing.back().id) if not existing.is_empty() else 0, int(head.last_id) - 50)
			if _fetch_messages(_selected, after):
				_head_seen[_selected] = int(head.last_id)
	var messages: Array = _conversation(_selected).get("messages", [])
	if not messages.is_empty():
		var latest := int(messages.back().id)
		if latest > int(_read_ids.get(_selected, 0)) and _send({"action": "mark_read", "pid": _selected, "through": latest}):
			_read_ids[_selected] = latest
