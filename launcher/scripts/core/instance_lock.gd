## Lane UI-5 (Sol r2, r3, r4): one launcher at a time. Two launchers open together each kept their own copy of state.cfg, so one could
## write back a stale list of running games and the other then pruned a version still being played; a lock file was not atomic (two
## launchers started at the same moment both took it, Sol r3). The lock is a listening socket on 127.0.0.1: the operating system lets
## exactly one process bind a port, frees it when that process ends, crashes or is killed, and only the holder can close it. The port is
## derived from the launcher's data folder (each data folder, e.g. each test, has its own launcher). The holder answers every connection
## with one line, HELLO + its version, and closes it (a small thread, so a command-line run answers too); a launcher that cannot bind
## the port connects and reads that line, so it tells another launcher from another program that happens to use the port (Sol r4).
## Nothing else is sent or read (tools/release/net_guard.py allows TCPServer and StreamPeerTCP in this file alone). Godot sets neither
## SO_REUSEADDR nor SO_EXCLUSIVEADDRUSE on Windows, and on Linux SO_REUSEADDR never lets a second process listen on a listened port.
extends RefCounted

const FIRST_PORT := 49200
const PORTS := 16000  # 49200..65199, inside the dynamic range
const HELLO := "OPENBFME-LAUNCHER "
const PROBE_MS := 2000

var port := 0
var error := ""            # why acquire() failed
var other_launcher := ""   # the version of the launcher that holds the port ("" when the port is held by something else)
var _version := ""
var _server: TCPServer
var _thread: Thread
var _serving := false


func _init(data_dir: String, version := "") -> void:
	port = FIRST_PORT + int(data_dir.simplify_path().md5_text().substr(0, 8).hex_to_int() % PORTS)
	_version = version


## true when this launcher now holds the lock (atomic: bind succeeds for one process only); false when it is held: `other_launcher` is
## the holder's version when it is a launcher, else `error` names the port and the reason
func acquire() -> bool:
	var s := TCPServer.new()
	var err := s.listen(port, "127.0.0.1")
	if err == OK:
		_server = s
		_serving = true
		_thread = Thread.new()
		_thread.start(_serve)
		return true
	var hello := _probe()
	if hello.begins_with(HELLO):
		other_launcher = hello.substr(HELLO.length()).strip_edges()
		error = "another OpenBFME Launcher (%s) holds local port %d" % [other_launcher, port]
	else:
		error = "local port %d is in use by another program (%s)" % [port, "it did not answer as a launcher" if hello == "" else "it answered something else"]
	return false


func held() -> bool:
	return _server != null and _server.is_listening()


## closes this launcher's own socket; a launcher that does not hold the lock has nothing to release
func release() -> void:
	if _server != null:
		_serving = false
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


## the first line the port's holder sends within PROBE_MS ("" when it sends nothing)
func _probe() -> String:
	var c := StreamPeerTCP.new()
	if c.connect_to_host("127.0.0.1", port) != OK:
		return ""
	var got := PackedByteArray()
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < PROBE_MS and got.size() < 256 and not got.has(10):
		c.poll()
		var st := c.get_status()
		if st == StreamPeerTCP.STATUS_CONNECTED and c.get_available_bytes() > 0:
			var r := c.get_partial_data(mini(c.get_available_bytes(), 256 - got.size()))
			if r[0] == OK:
				got.append_array(r[1])
		elif st == StreamPeerTCP.STATUS_ERROR or st == StreamPeerTCP.STATUS_NONE:
			break
		OS.delay_msec(20)
	c.disconnect_from_host()
	return got.get_string_from_utf8().get_slice("\n", 0)
