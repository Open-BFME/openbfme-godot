## WOTR2 milestone 9: the real online strategic page/staging room under a local lobby. Each client uses its own account and its own game files.
extends RefCounted


func run(game: Node, host: bool) -> bool:
	var entry: Dictionary = game._shell.online_status()
	print("GAME ONLINE WAR home status: %s, %d boxes, %d running" % [str(entry.get("page", "")), entry.get("boxes", []).size(),
		entry.get("running", []).size()])
	for box in entry.get("boxes", []):
		if str(box).begins_with("rejoin-offer|"):
			# A saved war is resumed from Load Game in a new staging room, not by rejoining its old relay.
			game._online_apply({"box": "No"})
			await game._wait_seconds(1.0)
	var pointer: Dictionary = game._shell.online_apply({"point": "Toggles.ButtonNav.Strategic"})
	if not pointer.get("ok", false):
		printerr("GAME ONLINE WAR: strategic navigation unavailable: ", pointer.get("errors", []))
		return false
	await game._online_click("Toggles.ButtonNav.Strategic")
	var nav: Dictionary = game._shell.online_status()
	print("GAME ONLINE WAR navigation: page %s state %d, %d boxes" % [str(nav.get("page", "")), int(nav.get("state", -1)), nav.get("boxes", []).size()])
	if not await game._online_wait("OnlineStrategic", func(st): return st.get("page", "") == "OnlineStrategic" and int(st.get("state", -1)) == 2, 60):
		return false
	print("GAME ONLINE WAR strategic page ready")
	if host:
		# The custom-match movie sends this callback on Accept; use the same callback without a timing-dependent headless popup click.
		game._online_apply({"game_password": ""}) # the movie's placeholder must not become a password on the local test room
		if game._lan_wotr_resume.is_empty():
			game._online_apply({"command": "AptOnline::CustomMatch::CreateGame"})
		else:
			var path: String = game._save_flow.save_dir().path_join(game._lan_wotr_resume + ".obfmesave")
			var why: String = await game._online_load_war(path)
			if not why.is_empty():
				printerr("GAME ONLINE WAR: saved room could not be staged: ", why)
				return false
		if not await game._online_wait("the hosted war room", func(st): return st.get("in_game", false) and int(st.get("state", -1)) == 6, 45):
			print("GAME ONLINE WAR create stopped in state ", int(game._shell.online_status().get("state", -1)))
			return false
		if not game._shell.online_status().get("in_game", false):
			return false
		print("GAME ONLINE WAR host room ", game._shell.online_status().get("game_id", ""))
		if not game._lan_wotr_resume.is_empty() and not await game._online_wait("the staged saved war", func(st):
				return not str(st.get("resume_save", "")).is_empty(), 45):
			return false
		if game._lan_wotr_resume.is_empty():
			var chosen: Dictionary
			for attempt in 30:
				chosen = game._shell.lobby_apply({"scenario": game._wotr_scenario})
				if chosen.get("ok", false):
					break
				await game._wait_seconds(0.5)
			if not chosen.get("ok", false):
				printerr("GAME ONLINE WAR: scenario not offered: ", chosen.get("errors", []))
				return false
			# The browser's default scenario may allow only one player. A new, two-player scenario does not silently reopen a previously closed seat.
			var seat_open: Dictionary = game._shell.lobby_apply({"slots": [{"slot": 1, "state": 0}]})
			if not seat_open.get("ok", false):
				printerr("GAME ONLINE WAR: second seat could not be opened: ", seat_open.get("errors", []))
				return false
			print("GAME ONLINE WAR second seat opened")
	else:
		var open_room := func(st: Dictionary):
			for row in st.get("games", []):
				if not str(row).ends_with("|rejoin") and not str(row).ends_with("|watch"):
					return true
			return false
		var listed := false
		for attempt in 20:
			if await game._online_wait("the open war", open_room, 8.0):
				listed = true
				break
			game._online_apply({"command": "AptOnline::CustomMatch::Refresh"})
		if not listed:
			return false
		var listed_rows: Array = game._shell.online_status().get("games", [])
		var row := -1
		for i in listed_rows.size():
			if not str(listed_rows[i]).ends_with("|rejoin") and not str(listed_rows[i]).ends_with("|watch"):
				row = i
				break
		if row < 0:
			printerr("GAME ONLINE WAR: no open room was selectable")
			return false
		game._online_apply({"select_row": row})
		game._online_apply({"command": "AptOnline::CustomMatch::JoinGame"})
		if not await game._online_wait("the joined war room", func(st): return st.get("in_game", false) and int(st.get("state", -1)) == 12, 60):
			var status: Dictionary = game._shell.online_status()
			print("GAME ONLINE WAR join refused at state %d; %d boxes" % [int(status.get("state", -1)), status.get("boxes", []).size()])
			return false
		print("GAME ONLINE WAR joined room ", game._shell.online_status().get("game_id", ""))
	var seat: int = int(game._shell.online_status().get("slot", -1))
	var resume: String = str(game._shell.online_status().get("resume_save", ""))
	if not resume.is_empty():
		if not await game._online_wait("the saved seat and copy check", func(st):
				var positions: Array = st.get("start_pos", [])
				return (positions.size() > seat and int(positions[seat]) >= 0
					and (host or (str(st.get("resume_check_save", "")) == resume and bool(st.get("resume_check_ok", false))))), 90):
			print("GAME ONLINE WAR saved seat check failed, seat %d, positions %s" % [seat, str(game._shell.online_status().get("start_pos", []))])
			return false
	else:
		# The strategic screen offers region indices, not the tactical map's starting positions (RW 0x97572C).
		var offered: Array = []
		for attempt in 120:
			var setup: Dictionary = game._shell.strategic_setup({"kind": "state"})
			var selectable: Array = setup.get("selectable", [])
			offered.clear()
			for i in selectable.size():
				if bool(selectable[i]):
					offered.append(i)
			if offered.size() >= 2:
				break
			await game._wait_seconds(0.25)
		if offered.size() < 2:
			printerr("GAME ONLINE WAR: fewer than two legal start regions: ", offered)
			return false
		print("GAME ONLINE WAR found %d start regions" % offered.size())
		var spec := {"slots": [{"slot": seat, "start_region": int(offered[0 if host else 1])}]}
		var applied: Dictionary
		for attempt in 20:
			applied = game._shell.lobby_apply(spec)
			if applied.get("ok", false):
				break
			await game._wait_seconds(0.5)
		if not applied.get("ok", false):
			printerr("GAME ONLINE WAR: choosing the region failed: ", applied.get("errors", []))
			return false
	print("GAME ONLINE WAR region set for seat %d" % seat)
	if host:
		if not await game._online_wait("the joiner ready", func(st):
				var ready: Array = st.get("ready", [])
				return ready.size() > 1 and ready[1], 90):
			print("GAME ONLINE WAR host ready timeout, seats ", game._shell.online_status().get("ready", []))
			return false
		print("GAME ONLINE WAR host ready, regions ", game._shell.online_status().get("start_pos", []))
		var command: Dictionary = game._shell.online_apply({"command": "AptOnline::CustomMatch::PlayGame"})
		print("GAME ONLINE WAR host PlayGame callback: ", str(command.get("ok", false)), " ", command.get("errors", []))
		await game.get_tree().process_frame
		print("GAME ONLINE WAR host next state ", int(game._shell.online_status().get("state", -1)))
		await game._wait_seconds(2.0)
		var room: Dictionary = game._shell.online_status()
		print("GAME ONLINE WAR host start state %d busy %s connecting %s refusals %s boxes %s" % [
			int(room.get("state", -1)), str(room.get("busy", false)), str(room.get("connecting", false)),
			str(room.get("start_refusals", [])), str(room.get("boxes", []))])
		if not room.get("start_refusals", []).is_empty() or not room.get("boxes", []).is_empty():
			return false
	else:
		# Every room update can unready the joiner; press Ready again until START arrives.
		var until := Time.get_ticks_msec() + 180000
		var ready_tries := 0
		while Time.get_ticks_msec() < until and game._net_state == "":
			var st: Dictionary = game._shell.online_status()
			var ready: Array = st.get("ready", [])
			if ready.size() > seat and not ready[seat] and int(st.get("state", -1)) == 12:
				var sent: Dictionary = game._shell.online_apply({"command": "AptOnline::CustomMatch::PlayGame"})
				ready_tries += 1
				if ready_tries <= 3:
					print("GAME ONLINE WAR ready press %d: %s" % [ready_tries, str(sent.get("errors", []))])
			await game._wait_seconds(1.0)
		if game._net_state == "":
			print("GAME ONLINE WAR joiner start timeout, ready ", game._shell.online_status().get("ready", []))
		return game._net_state != ""
	if not await game._online_wait("the strategic START", func(_st): return game._net_state != "", 120):
		print("GAME ONLINE WAR host START timeout, boxes ", game._shell.online_status().get("boxes", []))
		return false
	return true
