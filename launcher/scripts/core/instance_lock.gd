## Lane UI-5 (Sol r2, r3, r4): one launcher at a time. Two launchers open together each kept their own copy of state.cfg, so one could
## write back a stale list of running games and the other then pruned a version still being played; a lock file was not atomic (two
## launchers started at the same moment both took it, Sol r3). The lock is a listening socket on 127.0.0.1: the operating system lets
## exactly one process bind a port, frees it when that process ends, crashes or is killed, and only the holder can close it. The port is
## derived from the launcher's data folder (each data folder, e.g. each test, has its own launcher). The holder answers every connection
## with one line, HELLO + its version, and closes it (a small thread, so a command-line run answers too); a launcher that cannot bind
## the port connects and reads that line, so it tells another launcher from another program that happens to use the port (Sol r4).
## Windows exclusions are skipped only after a read-only netsh query confirms them. Published candidate ports are retained so that
## a changed exclusion table cannot hide a running holder. Publish only after listening, then check all published peers before success.
## Nothing else is sent or read (tools/release/net_guard.py permits only this protocol and the exact read-only query). Godot sets neither
## SO_REUSEADDR nor SO_EXCLUSIVEADDRUSE on Windows, and on Linux SO_REUSEADDR never lets a second process listen on a listened port.
## FLAKEFIX-2: the ports lay in 49200..65199, inside the operating systems' ephemeral ranges (Linux 32768..60999, Windows and macOS
## 49152..65535), so another program's outgoing connection could hold a launcher's port and the launcher refused to start. They now lie
## in 17000..31999, below every modern ephemeral range (Windows before Vista used 1025..5000). A launcher still asks the old port
## (LEGACY_FIRST_PORT) whether an older launcher holds it; only that launcher's hello counts there, anything else on it is ignored.
extends RefCounted
const L10n := preload("res://scripts/l10n/l10n.gd")

const FIRST_PORT := 17000
const PORTS := 15000  # 17000..31999, outside every OS's ephemeral port range
const LEGACY_FIRST_PORT := 49200
const LEGACY_PORTS := 16000  # 49200..65199, the launchers before FLAKEFIX-2
const HELLO := "OPENBFME-LAUNCHER "
const PROBE_MS := 2000
const LEGACY_PROBE_MS := 500  # an older launcher answers within milliseconds; a closed port there may take seconds to fail on Windows

var port := 0
var error := ""            # why acquire() failed
var other_launcher := ""   # the version of the launcher that holds the port ("" when the port is held by something else)
var _version := ""
var _server: TCPServer
var _thread: Thread
var _serving := false
var _first_port := 0
var _legacy_port := 0
var _ports_dir := ""
var _probe_busy := false


func _init(data_dir: String, version := "") -> void:
	var digest := data_dir.simplify_path().md5_text().substr(0, 8).hex_to_int()
	port = FIRST_PORT + int(digest % PORTS)
	_first_port = port
	_legacy_port = LEGACY_FIRST_PORT + int(digest % LEGACY_PORTS)
	_ports_dir = data_dir.path_join("launcher-lock-ports")
	_version = version


## true when this launcher now holds the lock (atomic: bind succeeds for one process only); false when it is held: `other_launcher` is
## the holder's version when it is a launcher, else `error` names the port and the reason
func acquire() -> bool:
	if held():
		return true
	error = ""
	other_launcher = ""
	var windows := OS.get_name() == "Windows"
	if windows and not _check_published():
		return false
	if _older_launcher(_legacy_port):
		return false
	var ranges: Array = []
	var queried := false
	for offset in range(PORTS):
		port = FIRST_PORT + (_first_port - FIRST_PORT + offset) % PORTS
		if _excluded(port, ranges):
			continue
		var s := TCPServer.new()
		var err := s.listen(port, "127.0.0.1")
		if err == OK:
			_server = s
			_serving = true
			_thread = Thread.new()
			if _thread.start(_serve) != OK:
				error = L10n.t("launcher.instance_lock.listener_failed")
				release()
				return false
			if windows:
				var published := DirAccess.make_dir_recursive_absolute(_ports_dir.path_join(str(port)))
				if published != OK and published != ERR_ALREADY_EXISTS:
					error = L10n.t("launcher.instance_lock.publish_failed", {"port": port, "reason": error_string(published)})
					release()
					return false
				# A concurrent candidate must see our listening socket after publishing its own marker, or we see its marker here.
				if not _check_published(port):
					release()
					return false
			if port != _first_port:
				print("LAUNCHER local lock port %d is excluded by Windows; using %d" % [_first_port, port])
			return true
		var hello := _probe(port)
		if hello.begins_with(HELLO) or _probe_busy:
			_refused(port, hello)
			return false
		if windows and hello.is_empty() and not queried:
			var result := _windows_exclusions()
			if not result.ok:
				error = L10n.t("launcher.instance_lock.bind_failed", {"port": port, "reason": result.error})
				return false
			ranges = result.ranges
			queried = true
		if _excluded(port, ranges):
			continue
		_refused(port, hello)
		return false
	error = L10n.t("launcher.instance_lock.all_excluded")
	return false


func _refused(candidate: int, hello: String) -> void:
	port = candidate
	if hello.begins_with(HELLO):
		other_launcher = hello.substr(HELLO.length()).strip_edges()
		error = L10n.t("launcher.instance_lock.other_launcher", {"version": other_launcher, "port": port})
	else:
		if hello == "":
			error = L10n.t("launcher.instance_lock.port_busy_silent", {"port": port})
		else:
			error = L10n.t("launcher.instance_lock.port_busy_other", {"port": port})


func _check_published(own_port := 0) -> bool:
	if not DirAccess.dir_exists_absolute(_ports_dir):
		return true
	var dir := DirAccess.open(_ports_dir)
	if dir == null:
		error = L10n.t("launcher.instance_lock.index_unreadable", {"reason": error_string(DirAccess.get_open_error())})
		return false
	var exclusions := {}
	var exclusions_queried := false
	for name in dir.get_directories():
		var legacy := name.is_valid_int() and int(name) >= LEGACY_FIRST_PORT and int(name) < LEGACY_FIRST_PORT + LEGACY_PORTS
		if not name.is_valid_int() or str(int(name)) != name or (not legacy and (int(name) < FIRST_PORT or int(name) >= FIRST_PORT + PORTS)):
			error = L10n.t("launcher.instance_lock.marker_invalid", {"name": name})
			return false
		var candidate := int(name)
		if candidate == own_port:
			continue
		if legacy:  # published by an older launcher: only its hello counts
			if _older_launcher(candidate):
				return false
			continue
		var proof := TCPServer.new()
		# A successful bind proves this marker is stale; a concurrent owner publishes and rescans after binding.
		if proof.listen(candidate, "127.0.0.1") == OK:
			proof.stop()
			continue
		var hello := _probe(candidate)
		if hello.begins_with(HELLO) or _probe_busy:
			_refused(candidate, hello)
			return false
		if OS.get_name() == "Windows":
			if not exclusions_queried:
				exclusions = _windows_exclusions()
				exclusions_queried = true
			if not exclusions.ok:
				error = L10n.t("launcher.instance_lock.verify_failed", {"port": candidate, "reason": exclusions.error})
				return false
			if _excluded(candidate, exclusions.ranges):
				continue
		_refused(candidate, hello)
		return false
	return true


## true (and refused) when an older launcher answers its hello on `candidate`, a port of the old range; anything else there is ignored.
## A port that can be bound holds no launcher and is not probed.
func _older_launcher(candidate: int) -> bool:
	var proof := TCPServer.new()
	if proof.listen(candidate, "127.0.0.1") == OK:
		proof.stop()
		return false
	var hello := _probe(candidate, LEGACY_PROBE_MS)
	if not hello.begins_with(HELLO):
		return false
	_refused(candidate, hello)
	return true


static func _excluded(candidate: int, ranges: Array) -> bool:
	for span in ranges:
		if candidate >= span[0] and candidate <= span[1]:
			return true
	return false


func _windows_exclusions() -> Dictionary:
	if not OS.get_environment("SystemRoot").is_absolute_path():
		return {"ok": false, "error": L10n.t("launcher.instance_lock.systemroot_relative")}
	var output := []
	var code := OS.execute(OS.get_environment("SystemRoot").path_join("System32/netsh.exe"), ["int", "ipv4", "show", "excludedportrange", "protocol=tcp"], output, true)
	if code != 0:
		return {"ok": false, "error": L10n.t("launcher.instance_lock.query_failed", {"code": code})}
	return parse_exclusions("\n".join(output))


static func parse_exclusions(text: String) -> Dictionary:
	var separator := RegEx.create_from_string("^\\s*-+\\s+-+\\s*$")
	var row := RegEx.create_from_string("^\\s*(\\d+)\\s+(\\d+)(?:\\s+\\*)?\\s*$")
	var table := false
	var footer := false
	var ranges := []
	for line in text.split("\n"):
		var clean := line.strip_edges()
		if not table:
			if separator.search(clean) != null:
				table = true
			continue
		if clean.is_empty():
			continue
		if not footer and clean.begins_with("*"):
			footer = true
			continue
		var matched := row.search(clean)
		if footer or matched == null:
			return {"ok": false, "error": L10n.t("launcher.instance_lock.row_unrecognized", {"row": clean.left(120)})}
		var first := int(matched.get_string(1))
		var last := int(matched.get_string(2))
		if first < 1 or last > 65535 or first > last:
			return {"ok": false, "error": L10n.t("launcher.instance_lock.range_invalid", {"row": clean.left(120)})}
		ranges.append([first, last])
	if not table:
		return {"ok": false, "error": L10n.t("launcher.instance_lock.no_table")}
	return {"ok": true, "ranges": ranges}


func held() -> bool:
	return _server != null and _server.is_listening()


## closes this launcher's own socket; a launcher that does not hold the lock has nothing to release
func release() -> void:
	if _server != null:
		_serving = false
		if _thread != null and _thread.is_started():
			_thread.wait_to_finish()
		_server.stop()
		_server = null


## the holder's side: answer each connection with the hello line, then wait for the prober to close it first (Godot listens without
## SO_REUSEADDR: a connection the holder closed first would leave the port in TIME_WAIT, and the next launcher could not bind it for a
## minute after this one ended); a prober that never closes is dropped after PROBE_MS
func _serve() -> void:
	var open := []  # [connection, accepted at]
	while _serving:
		if _server.is_connection_available():
			var c := _server.take_connection()
			if c != null:
				c.put_data((HELLO + _version + "\n").to_utf8_buffer())
				open.append([c, Time.get_ticks_msec()])
		for item in open.duplicate():
			var c: StreamPeerTCP = item[0]
			c.poll()
			if c.get_status() != StreamPeerTCP.STATUS_CONNECTED or Time.get_ticks_msec() - int(item[1]) > PROBE_MS * 2:
				c.disconnect_from_host()
				open.erase(item)
		OS.delay_msec(50)
	for item in open:
		item[0].disconnect_from_host()


## the first line the port's holder sends within `wait_ms` ("" when it sends nothing)
func _probe(candidate: int, wait_ms := PROBE_MS) -> String:
	_probe_busy = false
	var c := StreamPeerTCP.new()
	if c.connect_to_host("127.0.0.1", candidate) != OK:
		return ""
	var got := PackedByteArray()
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < wait_ms and got.size() < 256 and not got.has(10):
		c.poll()
		var st := c.get_status()
		# A connected socket proves a listener; STATUS_CONNECTING at timeout does not.
		if st == StreamPeerTCP.STATUS_CONNECTED:
			_probe_busy = true
		if st == StreamPeerTCP.STATUS_CONNECTED and c.get_available_bytes() > 0:
			var r := c.get_partial_data(mini(c.get_available_bytes(), 256 - got.size()))
			if r[0] == OK:
				got.append_array(r[1])
		elif st == StreamPeerTCP.STATUS_ERROR or st == StreamPeerTCP.STATUS_NONE:
			break
		OS.delay_msec(20)
	c.disconnect_from_host()
	return got.get_string_from_utf8().get_slice("\n", 0)
