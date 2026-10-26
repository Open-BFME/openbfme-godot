## Lane LAUNCH-1: the signed release manifest (written by tools/release/make_manifest.py, signed by tools/release/sign_manifest.py).
##
## manifest.json is checked byte for byte against its detached Ed25519 signature (manifest.json.sig, 64 raw bytes) under the release keys
## compiled into the launcher BEFORE it is parsed (lane SIGNKMS-1: valid under ANY trusted key, raw or over the signature envelope of kind
## "manifest.json" / "manifest-macos.json", scripts/crypto/release_signature.gd); then its schema is checked strictly (unknown or missing keys, a wrong type, a bad
## version, channel, date, commit, asset name, size or digest are errors) and it must belong to the release it came with: the same
## repository, the tag as its version, its channel matching the release's pre-release flag. Format (docs/RELEASE.md, "The launcher"):
##   {"format": 1, "product": "OpenBFME", "repo": "<owner>/<name>", "version": "v0.3.0", "channel": "stable" | "preview",
##    "date": "YYYY-MM-DD", "commit": "<40 hex>",
##    "assets": {"game": {"linux-x64": ASSET, "windows-x64": ASSET}, "launcher": {"linux-x64": ASSET, "windows-x64": ASSET}}}
##   ASSET = {"name": "openbfme[-launcher]-<version>-<platform>.tar.gz | .zip", "size": <bytes>, "sha256": "<64 hex>",
##            "files": {"<path in the package>": {"size": <bytes>, "sha256": "<64 hex>"}, ...}}
## Format 2 (round 3): "files" lists every file of the package, so the launcher checks unpacked files against signed data whenever it is
## about to run them (a staged launcher before the swap, an installed game before Play), not only the downloaded archive.
## Lane ONEZIP-1: one package per platform. From the first combined release on, the "game" package also holds the launcher's executable
## (OpenBFMELauncher.x86_64 / .exe next to the game's), so a launcher takes its own update from the game package (launcher_source) and
## the player downloads one archive. Format 2 still carries assets.launcher, the launcher-only archives the launchers published before
## ONEZIP-1 update themselves from (they refuse a manifest without them); format 3 is format 2 without assets.launcher, for the day
## those launchers are retired (docs/RELEASE.md, "One archive"). This launcher reads both. The separately signed
## manifest-macos.json uses the same metadata/inventory contract with the explicit "macos" schema mode.
extends RefCounted

const ReleaseSignature := preload("res://scripts/crypto/release_signature.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Archive := preload("res://scripts/core/archive.gd")

const FORMATS := [2, 3]
const PLATFORMS := ["linux-x64", "windows-x64"]
const PAYLOAD_PLATFORMS := PLATFORMS + ["macos-universal", "macos-arm64"]  # MACPLAY-1: the Apple Silicon package is sealed too
const KINDS := {"game": "openbfme", "launcher": "openbfme-launcher"}
const MAX_BYTES := 64 * 1024
const MAX_ASSET := 4 * 1024 * 1024 * 1024


static func asset_name(kind: String, version: String, platform: String) -> String:
	return "%s-%s-%s.%s" % [KINDS[kind], version, platform, "zip" if platform.begins_with("windows") or platform == "macos-arm64" else "tar.gz"]


static func macos_asset_name(version: String) -> String:
	return "openbfme-%s-macos-arm64.zip" % version


## the archive's single top folder (the asset name without its extension)
static func top_folder(asset: String) -> String:
	return asset.trim_suffix(".tar.gz").trim_suffix(".zip")


## {ok, error, manifest}. `expect`: {repo, tag, prerelease} of the release it was downloaded with ({} for a stored, already matched one
## except `repo`, which is always checked). `release_keys`: the trusted keys (BuildInfo.release_keys, each 32 bytes); `schema_mode`
## "standard" (manifest.json) or "macos" (manifest-macos.json) also names the signature envelope's kind.
static func verify(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, expect: Dictionary,
		schema_mode := "standard") -> Dictionary:
	var fail := func(why: String) -> Dictionary: return {"ok": false, "error": why, "manifest": {}}
	if not has_keys(release_keys):
		return fail.call("this launcher has no release key (a development run without --trust-key?)")
	if bytes.size() > MAX_BYTES:
		return fail.call("manifest.json is %d bytes (at most %d)" % [bytes.size(), MAX_BYTES])
	if sig.size() != 64:
		return fail.call("manifest.json.sig is %d bytes (an Ed25519 signature is 64)" % sig.size())
	if not ReleaseSignature.valid(release_keys, signature_kind(schema_mode), bytes, sig):
		return fail.call(not_signed_error(release_keys))
	var text := bytes.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(text) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return fail.call("manifest.json is not a JSON object")
	var m: Dictionary = json.data
	var why := _schema(m, schema_mode)
	if why != "":
		return fail.call("manifest.json: " + why)
	if expect.has("repo") and m.repo != expect.repo:
		return fail.call("the manifest is for %s, this launcher updates from %s" % [m.repo, expect.repo])
	if expect.has("tag") and m.version != expect.tag:
		return fail.call("the manifest is for %s but came with the release %s" % [m.version, expect.tag])
	if expect.has("prerelease") and bool(expect.prerelease) != (m.channel == "preview"):
		return fail.call("the manifest's channel (%s) does not match the release's pre-release flag" % m.channel)
	return {"ok": true, "error": "", "manifest": m}


## lane SIGNKMS-1: the signature envelope's kind of a manifest checked in `schema_mode`
static func signature_kind(schema_mode: String) -> String:
	return "manifest-macos.json" if schema_mode == "macos" else "manifest.json"


## true when `release_keys` is a usable trusted list: 1 or more keys, each 32 bytes
static func has_keys(release_keys: Array) -> bool:
	if release_keys.is_empty():
		return false
	for k in release_keys:
		if typeof(k) != TYPE_PACKED_BYTE_ARRAY or k.size() != 32:
			return false
	return true


## true when `sig` is a valid signature of the manifest `bytes` (kind of `schema_mode`) under one of `release_keys`; the signature alone
## (Updater's choice between a release's X.sig and X.sig2 slots; the schema is Manifest.verify's)
static func signature_ok(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, schema_mode := "standard") -> bool:
	return has_keys(release_keys) and bytes.size() <= MAX_BYTES and ReleaseSignature.valid(release_keys, signature_kind(schema_mode), bytes, sig)


static func not_signed_error(release_keys: Array) -> String:
	return "the manifest's signature is not valid for the release key: the release was not signed by an OpenBFME release key (this launcher trusts %d), or it was changed" % release_keys.size()


static func _keys(d: Dictionary, want: Array) -> String:
	var have := d.keys()
	have.sort()
	var w := want.duplicate()
	w.sort()
	return "" if have == w else "keys %s, expected %s" % [str(have), str(w)]


static func _int(v) -> int:
	# JSON numbers are floats in Godot: an integral, non-negative value below 2^53 only
	if typeof(v) != TYPE_FLOAT and typeof(v) != TYPE_INT:
		return -1
	var f := float(v)
	if f < 0 or f > 9007199254740991.0 or f != floor(f):
		return -1
	return int(f)


static func _schema(m: Dictionary, schema_mode := "standard") -> String:
	var k := _keys(m, ["format", "product", "repo", "version", "channel", "date", "commit", "assets"])
	if k != "":
		return k
	if not _int(m.format) in FORMATS:
		var f = m.format
		return "format %s (this launcher reads formats 2 and 3)" % (str(int(f)) if typeof(f) == TYPE_FLOAT and f == floor(f) and absf(f) < 1e15 else str(f))
	for key in ["product", "repo", "version", "channel", "date", "commit"]:
		if typeof(m[key]) != TYPE_STRING:
			return "%s is not a string" % key
	if m.product != "OpenBFME":
		return "product '%s'" % m.product
	if RegEx.create_from_string("^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$").search(m.repo) == null:
		return "repo '%s' is not <owner>/<name>" % m.repo
	if not Semver.is_valid(m.version):
		return Semver.parse(m.version).error
	if m.channel != Semver.channel_of(m.version):
		return "channel '%s' for version %s (a pre-release version is preview, a plain one stable)" % [m.channel, m.version]
	if RegEx.create_from_string("^20[0-9]{2}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$").search(m.date) == null:
		return "date '%s'" % m.date
	if RegEx.create_from_string("^[0-9a-f]{40}$").search(m.commit) == null:
		return "commit '%s' is not a 40-digit hex id" % m.commit
	if typeof(m.assets) != TYPE_DICTIONARY:
		return "assets is not an object"
	if schema_mode == "macos":
		if _int(m.format) != 3:
			return "macOS manifest format must be 3"
		k = _keys(m.assets, ["game"])
		if k != "":
			return "assets: " + k
		if typeof(m.assets.game) != TYPE_DICTIONARY:
			return "assets.game is not an object"
		if m.assets.game.is_empty():
			return ""
		if _keys(m.assets.game, ["macos-arm64"]) != "":
			return "assets.game: keys %s, expected [] or [macos-arm64]" % str(m.assets.game.keys())
		var a = m.assets.game["macos-arm64"]
		var where := "assets.game.macos-arm64"
		if typeof(a) != TYPE_DICTIONARY:
			return where + " is not an object"
		k = _keys(a, ["name", "size", "sha256", "files"])
		if k != "":
			return "%s: %s" % [where, k]
		if typeof(a.name) != TYPE_STRING or a.name != macos_asset_name(m.version):
			return "%s.name '%s', expected %s" % [where, str(a.name), macos_asset_name(m.version)]
		var size := _int(a.size)
		if size <= 0 or size > MAX_ASSET:
			return "%s.size %s" % [where, str(a.size)]
		a.size = size
		if typeof(a.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(a.sha256) == null:
			return "%s.sha256 is not 64 lower-case hex digits" % where
		var file_why := _files_schema(a.get("files"), top_folder(a.name))
		return "%s.files: %s" % [where, file_why] if file_why != "" else ""
	if schema_mode != "standard":
		return "unknown manifest schema mode '%s'" % schema_mode
	var kinds: Array = KINDS.keys() if _int(m.format) == 2 else ["game"]
	k = _keys(m.assets, kinds)
	if k != "":
		return "assets: " + k
	for kind in kinds:
		if typeof(m.assets[kind]) != TYPE_DICTIONARY:
			return "assets.%s is not an object" % kind
		k = _keys(m.assets[kind], PLATFORMS)
		if k != "":
			return "assets.%s: %s" % [kind, k]
		for platform in PLATFORMS:
			var a = m.assets[kind][platform]
			var where := "assets.%s.%s" % [kind, platform]
			if typeof(a) != TYPE_DICTIONARY:
				return where + " is not an object"
			k = _keys(a, ["name", "size", "sha256", "files"])
			if k != "":
				return "%s: %s" % [where, k]
			if typeof(a.name) != TYPE_STRING or a.name != asset_name(kind, m.version, platform):
				return "%s.name '%s', expected %s" % [where, str(a.name), asset_name(kind, m.version, platform)]
			var size := _int(a.size)
			if size <= 0 or size > MAX_ASSET:
				return "%s.size %s" % [where, str(a.size)]
			a.size = size
			if typeof(a.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(a.sha256) == null:
				return "%s.sha256 is not 64 lower-case hex digits" % where
			var why := _files_schema(a.get("files"), top_folder(a.name))
			if why != "":
				return "%s.files: %s" % [where, why]
	return ""


static func _files_schema(files, top: String) -> String:
	if typeof(files) != TYPE_DICTIONARY or files.is_empty() or files.size() > Archive.MAX_MEMBERS:
		return "not an object of 1 to %d files" % Archive.MAX_MEMBERS
	var seen := {}
	for rel in files:
		if typeof(rel) != TYPE_STRING:
			return "a file name is not a string"
		var c := Archive.check_name(top + "/" + rel, top)
		if not c.ok or c.rel != rel or c.dir:
			return "the file name '%s' is not a path inside the package (%s)" % [str(rel).left(100).c_escape(), c.error]
		if seen.has(rel.to_lower()):
			return "'%s' appears twice (ignoring case)" % rel
		seen[rel.to_lower()] = true
		var f = files[rel]
		if typeof(f) != TYPE_DICTIONARY or _keys(f, ["size", "sha256"]) != "":
			return "'%s' is not {size, sha256}" % rel
		var size := _int(f.size)
		if size < 0:
			return "'%s' has a bad size" % rel
		f.size = size
		if typeof(f.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(f.sha256) == null:
			return "'%s' has a bad sha256" % rel
	return ""


## Lane ONEZIP-1 r2: the signed payload inventory inside a combined package (tools/release/seal_package.py writes it on the machine with
## the release key, before the archive is finished, so it never names itself): payload.json, checked byte for byte against
## payload.json.sig under the trusted release keys (raw or envelope of kind "payload.json", lane SIGNKMS-1) before it is parsed, then strictly:
##   {"format": 1, "product": "OpenBFME", "repo": "<owner>/<name>", "version": <release version>, "platform": "linux-x64" | "windows-x64" | "macos-arm64",
##    "files": {<path in the package>: {"size", "sha256"}, ...}}     every file of the package but payload.json and payload.json.sig
## `expect`: {repo, platform}. {ok, error, payload}
const PAYLOAD := "payload.json"
const PAYLOAD_SIG := "payload.json.sig"


## Lane ONEZIP-1 r3 (Sol r2): a file read with its size checked FIRST: at most `max` bytes are read ({bytes, size}; size is the file's
## real length, -1 when it cannot be opened). A larger file is never read whole: bytes then holds max + 1 bytes, so every size check
## after it refuses it (a 4 MiB payload.json allocated ~93 MB before its 64 KiB limit refused it).
static func read_bounded(path: String, max: int) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {"bytes": PackedByteArray(), "size": -1}
	var size := f.get_length()
	var bytes := f.get_buffer(mini(size, max + 1))
	f.close()
	return {"bytes": bytes, "size": size}


static func read_limited(path: String, max: int) -> PackedByteArray:
	return read_bounded(path, max).bytes


static func verify_payload(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, expect: Dictionary) -> Dictionary:
	var fail := func(why: String) -> Dictionary: return {"ok": false, "error": why, "payload": {}}
	if not has_keys(release_keys):
		return fail.call("this launcher has no release key")
	if bytes.is_empty():
		return fail.call("there is no %s (not a signed package)" % PAYLOAD)
	if bytes.size() > MAX_BYTES:
		return fail.call("%s is %d bytes (at most %d)" % [PAYLOAD, bytes.size(), MAX_BYTES])
	if sig.size() != 64:
		return fail.call("%s is %d bytes (an Ed25519 signature is 64)" % [PAYLOAD_SIG, sig.size()])
	if not ReleaseSignature.valid(release_keys, PAYLOAD, bytes, sig):
		return fail.call("the signature of %s is not valid for the release key: the package was not made by the OpenBFME release (this launcher trusts %d key%s), or it was changed" % [PAYLOAD, release_keys.size(), "" if release_keys.size() == 1 else "s"])
	var json := JSON.new()
	if json.parse(bytes.get_string_from_utf8()) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return fail.call("%s is not a JSON object" % PAYLOAD)
	var p: Dictionary = json.data
	var k := _keys(p, ["format", "product", "repo", "version", "platform", "files"])
	if k != "":
		return fail.call("%s: %s" % [PAYLOAD, k])
	if _int(p.format) != 1:
		return fail.call("%s: format %s (this launcher reads format 1)" % [PAYLOAD, str(p.format)])
	for key in ["product", "repo", "version", "platform"]:
		if typeof(p[key]) != TYPE_STRING:
			return fail.call("%s: %s is not a string" % [PAYLOAD, key])
	if p.product != "OpenBFME":
		return fail.call("%s: product '%s'" % [PAYLOAD, p.product.left(40)])
	if p.repo != expect.get("repo", p.repo):
		return fail.call("%s is for %s, this launcher updates from %s" % [PAYLOAD, p.repo.left(140), expect.repo])
	if not Semver.is_valid(p.version):
		return fail.call("%s: %s" % [PAYLOAD, Semver.parse(p.version).error])
	if not p.platform in PAYLOAD_PLATFORMS or p.platform != expect.get("platform", p.platform):
		return fail.call("%s is for the platform '%s'" % [PAYLOAD, p.platform.left(40)])
	var why := _files_schema(p.get("files"), "openbfme-%s-%s" % [p.version, p.platform])
	if why != "":
		return fail.call("%s.files: %s" % [PAYLOAD, why])
	if p.files.has(PAYLOAD) or p.files.has(PAYLOAD_SIG):
		return fail.call("%s names itself" % PAYLOAD)
	return {"ok": true, "error": "", "payload": p}


## Lane ONEZIP-1: where a launcher's own update comes from in the verified manifest `m`: the launcher executable `exe` inside the game
## package when it is there (a combined package: {ok, combined: true, file: {size, sha256}}), else the launcher-only package of a
## release made before the combined one ({ok, combined: false, asset}); a manifest with neither is an error.
static func launcher_source(m: Dictionary, platform: String, exe: String) -> Dictionary:
	var game: Dictionary = m.assets.game[platform]
	if game.files.has(exe):
		return {"ok": true, "error": "", "combined": true, "file": game.files[exe], "asset": game}
	if m.assets.has("launcher"):
		return {"ok": true, "error": "", "combined": false, "asset": m.assets.launcher[platform]}
	return {"ok": false, "error": "the release %s has no launcher: %s is not in its package" % [m.version, exe]}


## MACFIX-1 (issue #47): a file Finder writes into any folder it shows (its view settings .DS_Store, a custom folder icon "Icon\r") or
## an AppleDouble "._<name>" (extended attributes copied to a volume without them). No release package holds one, nothing runs one, and
## inside an app bundle the code seal rules on them by itself (an AppleDouble file breaks the seal, .DS_Store is outside it).
static func finder_metadata(name: String) -> bool:
	return name == ".DS_Store" or name == "Icon\r" or name.begins_with("._")


## "" when the files of folder `dir` are exactly the signed `files` (the same names, sizes and SHA-256; `extra_ok` names files that may
## also be there; `any_extra`: other files are not looked at; `finder_ok`: Finder's own files, finder_metadata, may be there), else what
## differs
static func check_files(dir: String, files: Dictionary, extra_ok: Array = [], any_extra := false, finder_ok := false) -> String:
	for rel in files:
		var p := dir.path_join(rel)
		if not FileAccess.file_exists(p):
			return "%s is missing" % rel
		var f := FileAccess.open(p, FileAccess.READ)
		if f == null or f.get_length() != int(files[rel].size):
			return "%s does not have its signed size" % rel
		var hc := HashingContext.new()
		hc.start(HashingContext.HASH_SHA256)
		while f.get_position() < f.get_length():
			hc.update(f.get_buffer(1 << 20))
		if hc.finish().hex_encode() != files[rel].sha256:
			return "%s does not match its signed SHA-256" % rel
	for rel in ([] if any_extra else _list(dir, "")):
		if not files.has(rel) and not rel in extra_ok and not (finder_ok and finder_metadata(rel.get_file())):
			return "%s is not a file of the signed package" % rel
	return ""


static func _list(dir: String, prefix: String) -> Array:
	var out := []
	var d := DirAccess.open(dir.path_join(prefix))
	if d == null:
		return out
	d.include_hidden = true
	for f in d.get_files():
		out.append(prefix.path_join(f) if prefix != "" else f)
	for sub in d.get_directories():
		out.append_array(_list(dir, prefix.path_join(sub) if prefix != "" else sub))
	return out
