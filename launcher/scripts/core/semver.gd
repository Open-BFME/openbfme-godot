## Lane LAUNCH-1: release versions. A release tag is exactly `v<major>.<minor>.<patch>` (the stable channel) or
## `v<major>.<minor>.<patch>-preview.<n>` / `v<major>.<minor>.<patch>-alpha.<n>` (the pre-release channel): numbers are digits only,
## without leading zeros, at most 9 digits. Lane RELNAME-1 (owner, 2026-10-10): builds after v0.3.0-preview.4 are alpha builds,
## v0.4.0-alpha.<n>; the earlier previews stay valid (installed versions, the release history). Both pre-release words belong to the one
## pre-release channel, whose id stays "preview" (state.cfg, --channel=preview, the manifests' "channel"): a player on it gets the alpha
## builds with nothing to do; the launcher shows that channel as "Alpha" (channel_name()).
## Anything else is not a release version (an error, never guessed): other pre-release words, build metadata, signs, whitespace or a
## trailing newline (the whole tag must match: Sol r1 got `v1.0.0-1` installed over `v1.0.0--1` through the general SemVer grammar,
## whose numeric identifiers were read with String.is_valid_int, which accepts "-1").
## Order (SemVer 2.0.0 section 11): by major, minor, patch; a pre-release below its release; between pre-releases of one release
## "alpha" < "preview" (ASCII), then by their number. So v0.3.0-preview.4 < v0.4.0-alpha.1 < v0.4.0-alpha.2 < v0.4.0.
## Lane VERSION-1 (owner, 2026-10-14): the releases after v0.4.0-alpha.4 are plain numbers, v0.4.5, v0.4.6, ... (a stable-channel tag,
## above every v0.4.0-alpha.<n>), shown to players as "Alpha 0.4.5" (display()); the stable channel is shown as "Alpha", the pre-release
## channel as "Preview" (channel_name()). The grammar and the order are unchanged, so every launcher already installed takes them.
extends RefCounted

## the word before a plain number in what players see ("Alpha 0.4.5"; tools/release/autorelease.py STAGE)
const STAGE := "Alpha"

const _PATTERN := "^v(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})(?:-(alpha|preview)\\.(0|[1-9][0-9]{0,8}))?$"

static var _rx: RegEx


## {ok, error, core: [major, minor, patch], kind: "alpha" / "preview" or "" for a stable version, preview: <n> or -1 for a stable version}
## (`preview` is the pre-release number of either word)
static func parse(tag: String) -> Dictionary:
	if _rx == null:
		_rx = RegEx.create_from_string(_PATTERN)
	var m := _rx.search(tag) if tag.length() <= 40 else null
	# the match must be the whole tag ('$' also matches before a final newline)
	if m == null or m.get_start() != 0 or m.get_end() != tag.length():
		return {"ok": false, "error": "'%s' is not a release version (v<major>.<minor>.<patch>, v<major>.<minor>.<patch>-alpha.<n> or v<major>.<minor>.<patch>-preview.<n>)" % tag.left(80).c_escape()}
	var pre := m.get_string(5)
	return {"ok": true, "error": "", "core": [m.get_string(1).to_int(), m.get_string(2).to_int(), m.get_string(3).to_int()],
		"kind": m.get_string(4), "preview": pre.to_int() if pre != "" else -1}


static func is_valid(tag: String) -> bool:
	return parse(tag).ok


## -1, 0 or 1; both must be valid (callers check), an invalid one is an error
static func compare(a: String, b: String) -> int:
	var pa := parse(a)
	var pb := parse(b)
	assert(pa.ok and pb.ok, "compare of an invalid version")
	for i in 3:
		if pa.core[i] != pb.core[i]:
			return -1 if pa.core[i] < pb.core[i] else 1
	if pa.kind == pb.kind and pa.preview == pb.preview:
		return 0
	if pa.preview < 0 or pb.preview < 0:
		return 1 if pa.preview < 0 else -1  # the release is above its pre-releases
	if pa.kind != pb.kind:
		return -1 if pa.kind < pb.kind else 1  # "alpha" < "preview": identifiers compared in ASCII order
	return -1 if pa.preview < pb.preview else 1


## a pre-release version (alpha or preview) belongs to the pre-release channel (id "preview"), a plain one to stable
static func channel_of(tag: String) -> String:
	return "preview" if parse(tag).preview >= 0 else "stable"


## the name a player sees for a channel id. Lane VERSION-1: the plain releases ("Alpha 0.4.5") are the stable channel, shown as "Alpha";
## the pre-release channel "preview" (the earlier alpha builds and previews, and any later test build) is shown as "Preview" (RELNAME-1
## had shown it as "Alpha"). The ids are unchanged, so a player's remembered pick and --channel=stable / preview keep working.
static func channel_name(ch: String) -> String:
	return {"stable": STAGE, "preview": "Preview"}.get(ch, ch)


## lane VERSION-1: a version as players read it: "v0.4.5" -> "Alpha 0.4.5", "v0.4.0-alpha.3" -> "v0.4.0 Alpha 3" (its GitHub title), a
## preview keeps its tag; anything that is not a release version (a development run) is shown as it is
static func display(tag: String) -> String:
	var p := parse(tag)
	if not p.ok:
		return tag
	var core := "%d.%d.%d" % p.core
	if p.kind == "":
		return "%s %s" % [STAGE, core]
	if p.kind == "alpha":
		return "v%s Alpha %d" % [core, p.preview]
	return tag
