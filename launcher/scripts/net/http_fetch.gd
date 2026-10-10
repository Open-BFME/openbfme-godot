## Lane LAUNCH-1: the launcher's HTTP(S) client (Godot's HTTPClient, blocking: the UI runs it on a worker thread).
##
##   * every URL, and every redirect target before it is followed, passes NetPolicy.check (HTTPS to GitHub's release hosts only; a test
##     build may add its loopback test server); TLS uses Godot's bundled CA list with host name verification (TLSOptions.client());
##   * a User-Agent naming the launcher, its version and the project's page, nothing else about the machine or the user; no cookies, no credentials;
##   * timeouts: CONNECT_TIMEOUT to connect, IDLE_TIMEOUT without a byte while reading;
##   * get(): a small body into memory (a size cap), with If-None-Match (ETag) support;
##   * download(): an asset to a file, resuming a partial file with a Range request; never more than the expected size is written.
## Errors are returned as text for the user (never retried silently).
extends RefCounted

const NetPolicy := preload("res://scripts/net/net_policy.gd")

const CONNECT_TIMEOUT_MS := 20000
const IDLE_TIMEOUT_MS := 30000
const MAX_REDIRECTS := 5
const PROJECT_URL := "https://github.com/Open-BFME/openbfme-godot"  # in the User-Agent, so a server operator can find us (lane AIO-1)

var policy: NetPolicy
var user_agent := "OpenBFME-Launcher"
var cancelled := false  # set from the UI thread to abort a transfer


func _init(p: NetPolicy, version: String) -> void:
	policy = p
	user_agent = "OpenBFME-Launcher/%s (+%s)" % [version, PROJECT_URL]


func _wait(client: HTTPClient, busy: Array, timeout_ms: int) -> String:
	var start := Time.get_ticks_msec()
	while client.get_status() in busy:
		if cancelled:
			return "cancelled"
		if client.poll() != OK and not client.get_status() in busy:
			break
		if Time.get_ticks_msec() - start > timeout_ms:
			return "timed out"
		OS.delay_msec(2)
	return ""


## Open `url` (following allowed redirects). {ok, error, client, code, headers (lower-case keys), url (final)}
func _open(url: String, headers: PackedStringArray) -> Dictionary:
	for hop in MAX_REDIRECTS + 1:
		var why := policy.check(url)
		if why != "":
			return {"ok": false, "error": why}
		var u := NetPolicy.parse_url(url)
		var client := HTTPClient.new()
		var tls: TLSOptions = TLSOptions.client() if u.scheme == "https" else null
		var err := client.connect_to_host(u.host, u.port, tls)
		if err != OK:
			return {"ok": false, "error": "cannot connect to %s (%s)" % [u.host, error_string(err)]}
		var w := _wait(client, [HTTPClient.STATUS_RESOLVING, HTTPClient.STATUS_CONNECTING], CONNECT_TIMEOUT_MS)
		if w != "" or client.get_status() != HTTPClient.STATUS_CONNECTED:
			return {"ok": false, "error": "cannot connect to %s (%s)" % [u.host, w if w != "" else _status_text(client.get_status())]}
		var all := PackedStringArray(["User-Agent: " + user_agent])
		all.append_array(headers)
		err = client.request(HTTPClient.METHOD_GET, u.path, all)
		if err != OK:
			return {"ok": false, "error": "request to %s failed (%s)" % [u.host, error_string(err)]}
		w = _wait(client, [HTTPClient.STATUS_REQUESTING], IDLE_TIMEOUT_MS)
		if w != "":
			return {"ok": false, "error": "no answer from %s (%s)" % [u.host, w]}
		if not client.get_status() in [HTTPClient.STATUS_BODY, HTTPClient.STATUS_CONNECTED]:
			return {"ok": false, "error": "the connection to %s failed (%s)" % [u.host, _status_text(client.get_status())]}
		var code := client.get_response_code()
		var h := {}
		for line in client.get_response_headers():
			var c := line.find(":")
			if c > 0:
				h[line.substr(0, c).strip_edges().to_lower()] = line.substr(c + 1).strip_edges()
		if code in [301, 302, 303, 307, 308]:
			var loc: String = h.get("location", "")
			if loc.begins_with("/"):
				loc = "%s://%s%s" % [u.scheme, u.host if not u.explicit_port else "%s:%d" % [u.host, u.port], loc]
			if loc == "":
				return {"ok": false, "error": "a redirect from %s without a Location" % u.host}
			client.close()
			url = loc
			continue
		return {"ok": true, "error": "", "client": client, "code": code, "headers": h, "url": url}
	return {"ok": false, "error": "more than %d redirects" % MAX_REDIRECTS}


static func _status_text(s: int) -> String:
	match s:
		HTTPClient.STATUS_CANT_RESOLVE: return "the host name could not be resolved: offline?"
		HTTPClient.STATUS_CANT_CONNECT: return "the connection was refused or failed: offline?"
		HTTPClient.STATUS_CONNECTION_ERROR: return "the connection broke"
		HTTPClient.STATUS_TLS_HANDSHAKE_ERROR: return "the TLS handshake or certificate check failed"
		HTTPClient.STATUS_DISCONNECTED: return "disconnected"
	return "status %d" % s


## Read the body of an opened response, at most `cap` bytes. {ok, error, body}
func _read_all(client: HTTPClient, cap: int) -> Dictionary:
	var body := PackedByteArray()
	var last := Time.get_ticks_msec()
	while client.get_status() == HTTPClient.STATUS_BODY:
		if cancelled:
			return {"ok": false, "error": "cancelled", "body": body}
		client.poll()
		var chunk := client.read_response_body_chunk()
		if chunk.is_empty():
			if Time.get_ticks_msec() - last > IDLE_TIMEOUT_MS:
				return {"ok": false, "error": "the download stalled (no data for %d s)" % (IDLE_TIMEOUT_MS / 1000), "body": body}
			OS.delay_msec(2)
			continue
		last = Time.get_ticks_msec()
		body.append_array(chunk)
		if body.size() > cap:
			return {"ok": false, "error": "the response is larger than %d bytes" % cap, "body": PackedByteArray()}
	if client.get_status() != HTTPClient.STATUS_CONNECTED and client.get_status() != HTTPClient.STATUS_DISCONNECTED:
		return {"ok": false, "error": "the connection broke during the transfer (%s)" % _status_text(client.get_status()), "body": body}
	return {"ok": true, "error": "", "body": body}


## GET a small resource. {ok, error, code, headers, body}. `etag` != "" sends If-None-Match (a 304 has an empty body). `extra`: more
## request headers ("Name: value"; lane AIO-1 sends the two its service's own client sends).
func get_small(url: String, cap: int, accept := "", etag := "", extra := PackedStringArray()) -> Dictionary:
	var hs := extra.duplicate()
	if accept != "":
		hs.append("Accept: " + accept)
	if etag != "":
		hs.append("If-None-Match: " + etag)
	var o := _open(url, hs)
	if not o.ok:
		return {"ok": false, "error": o.error, "code": 0, "headers": {}, "body": PackedByteArray()}
	var r := _read_all(o.client, cap)
	o.client.close()
	if not r.ok:
		return {"ok": false, "error": r.error, "code": o.code, "headers": o.headers, "body": PackedByteArray()}
	if o.headers.has("content-length") and o.code == 200 and int(o.headers["content-length"]) != r.body.size():
		return {"ok": false, "error": "the response from %s was cut short" % NetPolicy.parse_url(o.url).host, "code": o.code, "headers": o.headers, "body": PackedByteArray()}
	return {"ok": true, "error": "", "code": o.code, "headers": o.headers, "body": r.body}


## Download `url` to `dest` (resuming an existing partial `dest`), expecting exactly `size` bytes. `progress.call(done, total)` is
## called while it runs. {ok, error, resumed (bytes kept from before)}. A failed transfer keeps the partial file for the next resume.
func download(url: String, dest: String, size: int, progress: Callable) -> Dictionary:
	var have := 0
	if FileAccess.file_exists(dest):
		have = FileAccess.open(dest, FileAccess.READ).get_length()
	if have > size:
		DirAccess.remove_absolute(dest)
		have = 0
	if have == size:
		return {"ok": true, "error": "", "resumed": have}
	var hs := PackedStringArray(["Accept: application/octet-stream"])
	if have > 0:
		hs.append("Range: bytes=%d-" % have)
	var o := _open(url, hs)
	if not o.ok:
		return {"ok": false, "error": o.error, "resumed": 0}
	var client: HTTPClient = o.client
	var f: FileAccess
	if o.code == 206 and have > 0:
		var cr: String = o.headers.get("content-range", "")
		if not cr.begins_with("bytes %d-" % have) or not cr.ends_with("/%d" % size):
			client.close()
			return {"ok": false, "error": "the server resumed the download at the wrong place (%s)" % cr, "resumed": 0}
		f = FileAccess.open(dest, FileAccess.READ_WRITE)
		if f != null:
			f.seek_end()
	elif o.code == 200:
		have = 0  # the server sent the whole file: start over
		f = FileAccess.open(dest, FileAccess.WRITE)
	else:
		client.close()
		if o.code == 416:
			DirAccess.remove_absolute(dest)  # the partial file does not fit the server's file: start over next time
		return {"ok": false, "error": "the download failed: HTTP %d from %s" % [o.code, NetPolicy.parse_url(o.url).host], "resumed": 0}
	if f == null:
		client.close()
		return {"ok": false, "error": "cannot write %s (%s)" % [dest, error_string(FileAccess.get_open_error())], "resumed": 0}
	var kept := have
	var done := have
	var last := Time.get_ticks_msec()
	var error := ""
	while client.get_status() == HTTPClient.STATUS_BODY:
		if cancelled:
			error = "cancelled"
			break
		client.poll()
		var chunk := client.read_response_body_chunk()
		if chunk.is_empty():
			if Time.get_ticks_msec() - last > IDLE_TIMEOUT_MS:
				error = "the download stalled (no data for %d s); it resumes from here next time" % (IDLE_TIMEOUT_MS / 1000)
				break
			OS.delay_msec(2)
			continue
		last = Time.get_ticks_msec()
		if done + chunk.size() > size:
			error = "the server sent more than the manifest's %d bytes" % size
			break
		if not f.store_buffer(chunk) or f.get_error() != OK:
			error = "writing %s failed: is the disk full?" % dest
			break
		done += chunk.size()
		progress.call(done, size)
	f.close()
	client.close()
	if error == "" and done != size:
		error = "the download was cut short at %d of %d bytes; it resumes from here next time" % [done, size]
	if error != "":
		return {"ok": false, "error": error, "resumed": kept}
	return {"ok": true, "error": "", "resumed": kept}
