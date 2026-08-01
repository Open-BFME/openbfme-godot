extends RefCounted

## UPnP automatic port forwarding for a hosted game (F2).
##
## Hosting over the open internet needs the router to forward the game's UDP
## port to this machine. Most consumer routers can be asked to do that over
## UPnP-IGD, so this module tries — ONCE, per host attempt — and then reports
## exactly what happened.
##
## HONESTY CONTRACT. UPnP being unavailable is a normal, expected environment
## condition (disabled in the router, carrier-grade NAT, a corporate network, a
## machine that only ever plays over Radmin/LAN). This module therefore never
## reports success it did not get and never quietly degrades:
##   * state is "mapped" ONLY when add_port_mapping() returned
##     UPNP_RESULT_SUCCESS. There is no optimistic path.
##   * every non-success outcome carries a `detail` string naming the concrete
##     cause, including the engine's own UPNP_RESULT_* symbol, so a support log
##     or a screenshot is diagnosable.
##   * "unavailable" (no router answered / no UPnP in this build) is kept
##     distinct from "failed" (a router answered and refused), because the two
##     mean different things to the player and to us.
## The caller is expected to SHOW the outcome. Callers must not treat
## "unavailable" as fatal: LAN and Radmin hosting are unaffected by it.
##
## THREADING. Every UPnP call blocks: discover() for its full timeout, and the
## SOAP calls for however long the router feels like taking — a wedged consumer
## router can sit on one for minutes. So the job runs on WorkerThreadPool and
## the main thread harvests the result in poll(). All cross-thread state passes
## through _mutex; nothing here touches the scene tree.
##
## Teardown NEVER blocks. This used to join the worker on close(), which meant a
## router that stopped answering froze the whole menu until it gave up (it hung
## a headless test run for minutes). An unfinished job is therefore ABANDONED
## rather than waited on: the pool reaps it at engine shutdown. The one cost is
## that a job abandoned between "router accepted the mapping" and "we recorded
## it" leaves a mapping behind that release() never deletes — a stale forward of
## one UDP port, which is strictly better than an unresponsive UI.

## Lifecycle states. Also the exact strings a caller may branch on.
const STATE_IDLE := "idle"
const STATE_WORKING := "working"
const STATE_MAPPED := "mapped"
const STATE_UNAVAILABLE := "unavailable"
const STATE_FAILED := "failed"

## Mapping description shown in the router's admin UI, and the lease length.
## 0 == permanent: many consumer routers reject any finite lease
## (UPNP_RESULT_ONLY_PERMANENT_LEASE_SUPPORTED), and a lease that silently
## expires mid-match is exactly the kind of quiet failure this module exists to
## avoid. release() removes the mapping on teardown.
const MAPPING_DESCRIPTION := "Open BFME"
const MAPPING_PROTOCOL := "UDP"
const MAPPING_DURATION := 0

## Discovery budget. 2000 ms is Godot's own default; ttl 2 covers the router
## plus one bridged hop, which is the realistic home topology.
const DISCOVER_TIMEOUT_MSEC := 2000
const DISCOVER_TTL := 2

const PORT_MIN := 1024
const PORT_MAX := 65535

## Emitted once per attempt when the worker result has been harvested.
signal finished(state: String, detail: String)

var state := STATE_IDLE
## Router-side address/port a remote player would actually connect to. Only
## meaningful when state == STATE_MAPPED, and external_address may still be ""
## when the router mapped the port but refused to report its WAN address —
## that case is surfaced rather than papered over.
var external_address := ""
var external_port := 0
## Player-facing explanation of the current state. Never empty once an attempt
## has run.
var detail := ""
## Engine result symbol for the last UPnP call ("" before any attempt).
var last_result_name := ""

## -1 when no job is in flight. A job that is still running at teardown is
## abandoned, not joined (see the threading note above).
var _task_id := -1
## Shared drop-box between the worker and the main thread. It is a Dictionary
## (a reference type) so the Callable holding it keeps it alive even after this
## module is gone — see _worker_static for why that matters.
var _mutex := Mutex.new()
var _result_box: Dictionary = {}
var _upnp = null
var _mapped_port := 0


# --- pure helpers (no network, no threads: directly unit-testable) ------------

## True when this engine build actually has the UPnP module compiled in.
static func upnp_supported() -> bool:
	return ClassDB.class_exists("UPNP")


## The engine's own symbol for a UPNPResult code, e.g. "UPNP_RESULT_NO_GATEWAY".
## Falls back to a numeric form rather than inventing a name.
static func result_name(code: int) -> String:
	if not upnp_supported():
		return "UPNP_UNSUPPORTED_BUILD"
	for name in ClassDB.class_get_enum_constants("UPNP", "UPNPResult", true):
		if ClassDB.class_get_integer_constant("UPNP", String(name)) == code:
			return String(name)
	return "UPNP_RESULT_%d" % code


## True when a result code means "there is no UPnP service to talk to here",
## as opposed to "a router answered and said no". Drives unavailable vs failed.
static func is_environmental(code: int) -> bool:
	var name := result_name(code)
	return name in [
		"UPNP_RESULT_NO_GATEWAY",
		"UPNP_RESULT_NO_DEVICES",
		"UPNP_RESULT_INVALID_GATEWAY",
		"UPNP_RESULT_SOCKET_ERROR",
		"UPNP_RESULT_HTTP_ERROR",
		"UPNP_UNSUPPORTED_BUILD",
	]


## Player-facing sentence for a UPnP result code. Every branch names a cause;
## there is deliberately no generic "something went wrong" default that hides
## which code came back.
static func explain_result(code: int) -> String:
	match result_name(code):
		"UPNP_RESULT_SUCCESS":
			return "The router accepted the port forward."
		"UPNP_RESULT_NO_GATEWAY", "UPNP_RESULT_NO_DEVICES":
			return "No UPnP-capable router answered. It is probably turned off in the router settings, or this connection is behind carrier-grade NAT."
		"UPNP_RESULT_INVALID_GATEWAY":
			return "A device answered but is not a usable internet gateway."
		"UPNP_RESULT_NOT_AUTHORIZED":
			return "The router refused the request: UPnP port mapping is disabled or restricted on it."
		"UPNP_RESULT_CONFLICT_WITH_OTHER_MAPPING", "UPNP_RESULT_CONFLICT_WITH_OTHER_MECHANISM":
			return "That port is already forwarded to another device. Pick a different port, or remove the existing rule."
		"UPNP_RESULT_ONLY_PERMANENT_LEASE_SUPPORTED":
			return "The router only accepts permanent port forwards and rejected this one."
		"UPNP_RESULT_NO_PORT_MAPS_AVAILABLE":
			return "The router has no free port-forwarding slots left."
		"UPNP_RESULT_SOCKET_ERROR", "UPNP_RESULT_HTTP_ERROR":
			return "Could not reach the router to ask (network or firewall problem)."
		"UPNP_RESULT_INVALID_PORT":
			return "The router rejected that port number."
		"UPNP_UNSUPPORTED_BUILD":
			return "This build of the game has no UPnP support compiled in."
		_:
			return "The router rejected the port forward (%s)." % result_name(code)


## The single line a lobby/flyout should display. Pure: same inputs always give
## the same text, so the UI wording is testable headlessly.
static func status_text(status: String, address: String, port: int, reason: String) -> String:
	match status:
		STATE_IDLE:
			return "UPnP: not attempted."
		STATE_WORKING:
			return "UPnP: asking your router to forward the port..."
		STATE_MAPPED:
			if address == "":
				return "UPnP: port forwarded, but the router would not report your external address. %s" % reason
			return "UPnP: forwarded - players outside your network can use %s:%d" % [address, port]
		STATE_UNAVAILABLE:
			return "UPnP unavailable: %s LAN and Radmin players can still join." % reason
		STATE_FAILED:
			return "UPnP failed: %s Forward the port by hand, or use Radmin/LAN." % reason
		_:
			return "UPnP: unknown state '%s'." % status


func status_line() -> String:
	return status_text(state, external_address, external_port, detail)


func is_busy() -> bool:
	return state == STATE_WORKING


# --- attempt lifecycle -------------------------------------------------------

## Begins one discover+map attempt for `port`. Returns false (and sets a
## terminal, explained state) when the attempt cannot even be started; returns
## true when a worker was launched and poll() will produce the outcome.
func start(port: int) -> bool:
	if state == STATE_WORKING:
		return false
	release()
	external_address = ""
	external_port = 0
	last_result_name = ""
	if port < PORT_MIN or port > PORT_MAX:
		state = STATE_FAILED
		detail = "Port %d is outside the allowed %d-%d range." % [port, PORT_MIN, PORT_MAX]
		finished.emit(state, detail)
		return false
	if not upnp_supported():
		state = STATE_UNAVAILABLE
		last_result_name = "UPNP_UNSUPPORTED_BUILD"
		detail = "This build of the game has no UPnP support compiled in."
		finished.emit(state, detail)
		return false
	state = STATE_WORKING
	detail = "Asking the router..."
	_mutex.lock()
	_result_box = {}
	_mutex.unlock()
	_task_id = WorkerThreadPool.add_task(
		Callable(get_script(), "_worker_static").bind(port, _result_box, _mutex), true, "openbfme-upnp")
	if _task_id < 0:
		state = STATE_FAILED
		detail = "Could not queue the UPnP worker task."
		finished.emit(state, detail)
		return false
	return true


## Harvests a finished worker. Cheap to call every frame; does nothing until
## the worker has published a result.
func poll() -> void:
	if _task_id < 0:
		return
	_mutex.lock()
	var has_result: bool = _result_box.has("result")
	var result: Dictionary = (_result_box.get("result", {}) as Dictionary)
	var upnp_object = _result_box.get("upnp", null)
	_mutex.unlock()
	if not has_result:
		return
	# The worker published before returning, so the task is finished or about to
	# be; this reaps it without any meaningful wait.
	WorkerThreadPool.wait_for_task_completion(_task_id)
	_task_id = -1
	state = String(result.get("state", STATE_FAILED))
	detail = String(result.get("detail", ""))
	last_result_name = String(result.get("result_name", ""))
	external_address = String(result.get("external_address", ""))
	external_port = int(result.get("external_port", 0))
	_mapped_port = int(result.get("mapped_port", 0))
	# Held only so release() can delete the mapping it created.
	_upnp = upnp_object
	_mutex.lock()
	_result_box = {}
	_mutex.unlock()
	finished.emit(state, detail)


## Removes a mapping this module created. Safe to call when nothing is mapped.
##
## The delete runs INLINE, unlike the discover/map job. Two reasons: it only
## ever runs against a gateway that has just answered us successfully, so it is
## a single bounded SOAP call (measured at well under a frame here); and handing
## it to WorkerThreadPool instead reliably segfaulted the process on exit, while
## the identical inline call shut down cleanly. The unbounded hang this module
## has to defend against is a wedged discover()/add_port_mapping(), and that one
## is abandoned rather than joined just above.
func release() -> void:
	_abandon_worker()
	if _upnp != null and _mapped_port > 0:
		_upnp.delete_port_mapping(_mapped_port, MAPPING_PROTOCOL)
	_mapped_port = 0
	_upnp = null
	if state == STATE_MAPPED:
		state = STATE_IDLE
		detail = ""
		external_address = ""
		external_port = 0


func close() -> void:
	release()
	state = STATE_IDLE
	detail = ""
	last_result_name = ""


## Drops our handle on an in-flight job WITHOUT waiting for it. See the
## threading note at the top: waiting here is what froze the UI.
func _abandon_worker() -> void:
	_task_id = -1
	_mutex.lock()
	_result_box = {}
	_mutex.unlock()


# --- worker thread -----------------------------------------------------------

## Runs OFF the main thread, and is deliberately STATIC: it must not touch this
## module at all.
##
## A job can outlive the module — teardown abandons an unfinished one rather
## than freezing on it — and a Godot Callable keeps only a WEAK ObjectID for a
## RefCounted. A worker bound to `self` therefore writes into freed memory once
## the module is released, which is a genuine use-after-free and really does
## segfault the process on exit. Everything the worker needs is passed in, and
## `box`/`mutex` are reference types the Callable itself keeps alive.
static func _worker_static(port: int, box: Dictionary, mutex: Mutex) -> void:
	var result := _attempt_static(port)
	var upnp_object = result.get("upnp", null)
	result.erase("upnp")
	mutex.lock()
	box["result"] = result
	box["upnp"] = upnp_object
	mutex.unlock()


static func _attempt_static(port: int) -> Dictionary:
	var upnp = ClassDB.instantiate("UPNP")
	if upnp == null:
		return {
			"state": STATE_UNAVAILABLE,
			"result_name": "UPNP_UNSUPPORTED_BUILD",
			"detail": "This build of the game has no UPnP support compiled in.",
		}
	var discover_code := int(upnp.discover(DISCOVER_TIMEOUT_MSEC, DISCOVER_TTL, "InternetGatewayDevice"))
	if discover_code != 0:
		return {
			"state": STATE_UNAVAILABLE if is_environmental(discover_code) else STATE_FAILED,
			"result_name": result_name(discover_code),
			"detail": explain_result(discover_code),
		}
	var gateway = upnp.get_gateway()
	if gateway == null or not gateway.is_valid_gateway():
		return {
			"state": STATE_UNAVAILABLE,
			"result_name": "UPNP_RESULT_NO_GATEWAY",
			"detail": explain_result(_code_for("UPNP_RESULT_NO_GATEWAY")),
		}
	var map_code := int(upnp.add_port_mapping(port, port, MAPPING_DESCRIPTION, MAPPING_PROTOCOL, MAPPING_DURATION))
	if map_code != 0:
		return {
			"state": STATE_UNAVAILABLE if is_environmental(map_code) else STATE_FAILED,
			"result_name": result_name(map_code),
			"detail": explain_result(map_code),
		}
	# Mapped. The WAN address is a separate query and is allowed to fail without
	# invalidating the mapping — but the caller is told when it did. The UPNP
	# object rides back in the result so release() can delete what was created.
	var wan_address := String(upnp.query_external_address())
	if not wan_address.is_valid_ip_address():
		return {
			"state": STATE_MAPPED,
			"result_name": result_name(map_code),
			"detail": "Ask a player to look up this network's public IP.",
			"external_address": "",
			"external_port": port,
			"mapped_port": port,
			"upnp": upnp,
		}
	return {
		"state": STATE_MAPPED,
		"result_name": result_name(map_code),
		"detail": explain_result(map_code),
		"external_address": wan_address,
		"external_port": port,
		"mapped_port": port,
		"upnp": upnp,
	}


static func _code_for(name: String) -> int:
	if not upnp_supported():
		return -1
	return ClassDB.class_get_integer_constant("UPNP", name)
