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
const L10n := preload("res://scripts/l10n/l10n.gd")

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
## lane RELGH-2: `nightly_keys` (BuildInfo.nightly_keys): keys valid for a nightly manifest only (ReleaseSignature.valid_scoped); the
## default [] trusts none, so a call site that forgets it refuses a nightly-key signature.
static func verify(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, expect: Dictionary,
		schema_mode := "standard", nightly_keys := []) -> Dictionary:
	var fail := func(why: String) -> Dictionary: return {"ok": false, "error": why, "manifest": {}}
	if not has_keys(release_keys):
		return fail.call(L10n.t("launcher.manifest.no_release_key_dev"))
	if bytes.size() > MAX_BYTES:
		return fail.call(L10n.t("launcher.manifest.too_big", {"size": bytes.size(), "limit": MAX_BYTES}))
	if sig.size() != 64:
		return fail.call(L10n.t("launcher.manifest.sig_size", {"size": sig.size()}))
	if not ReleaseSignature.valid_scoped(release_keys, nightly_keys, signature_kind(schema_mode), bytes, sig):
		return fail.call(not_signed_error(release_keys))
	var text := bytes.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(text) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return fail.call(L10n.t("launcher.manifest.not_json_object"))
	var m: Dictionary = json.data
	var why := _schema(m, schema_mode)
	if why != "":
		return fail.call(L10n.t("launcher.manifest.manifest_why", {"why": why}))
	if expect.has("repo") and m.repo != expect.repo:
		return fail.call(L10n.t("launcher.manifest.wrong_repo", {"repo": m.repo, "expected": expect.repo}))
	if expect.has("tag") and m.version != expect.tag:
		return fail.call(L10n.t("launcher.manifest.wrong_tag", {"version": m.version, "tag": expect.tag}))
	if expect.has("prerelease") and bool(expect.prerelease) != (m.channel == "preview"):
		return fail.call(L10n.t("launcher.manifest.wrong_channel", {"channel": m.channel}))
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
static func signature_ok(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, schema_mode := "standard", nightly_keys := []) -> bool:
	return has_keys(release_keys) and bytes.size() <= MAX_BYTES and ReleaseSignature.valid_scoped(release_keys, nightly_keys, signature_kind(schema_mode), bytes, sig)


static func not_signed_error(release_keys: Array) -> String:
	return L10n.t("launcher.manifest.not_signed", {"count": release_keys.size()})


static func _keys(d: Dictionary, want: Array) -> String:
	var have := d.keys()
	have.sort()
	var w := want.duplicate()
	w.sort()
	return "" if have == w else L10n.t("launcher.manifest.keys_expected", {"have": str(have), "want": str(w)})


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
		return L10n.t("launcher.manifest.bad_format", {"format": str(int(f)) if typeof(f) == TYPE_FLOAT and f == floor(f) and absf(f) < 1e15 else str(f)})
	for key in ["product", "repo", "version", "channel", "date", "commit"]:
		if typeof(m[key]) != TYPE_STRING:
			return L10n.t("launcher.manifest.not_a_string", {"key": key})
	if m.product != "OpenBFME":
		return L10n.t("launcher.manifest.bad_product", {"product": m.product})
	if RegEx.create_from_string("^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$").search(m.repo) == null:
		return L10n.t("launcher.manifest.bad_repo", {"repo": m.repo})
	if not Semver.is_valid(m.version):
		return Semver.parse(m.version).error
	if m.channel != Semver.channel_of(m.version):
		return L10n.t("launcher.manifest.bad_channel", {"channel": m.channel, "version": m.version})
	if RegEx.create_from_string("^20[0-9]{2}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$").search(m.date) == null:
		return L10n.t("launcher.manifest.bad_date", {"date": m.date})
	if RegEx.create_from_string("^[0-9a-f]{40}$").search(m.commit) == null:
		return L10n.t("launcher.manifest.bad_commit", {"commit": m.commit})
	if typeof(m.assets) != TYPE_DICTIONARY:
		return L10n.t("launcher.manifest.assets_not_object")
	if schema_mode == "macos":
		if _int(m.format) != 3:
			return L10n.t("launcher.manifest.macos_format")
		k = _keys(m.assets, ["game"])
		if k != "":
			return L10n.t("launcher.manifest.assets_why", {"why": k})
		if typeof(m.assets.game) != TYPE_DICTIONARY:
			return L10n.t("launcher.manifest.game_not_object")
		if m.assets.game.is_empty():
			return ""
		if _keys(m.assets.game, ["macos-arm64"]) != "":
			return L10n.t("launcher.manifest.game_keys", {"keys": str(m.assets.game.keys())})
		var a = m.assets.game["macos-arm64"]
		var where := "assets.game.macos-arm64"
		if typeof(a) != TYPE_DICTIONARY:
			return L10n.t("launcher.manifest.where_not_object", {"where": where})
		k = _keys(a, ["name", "size", "sha256", "files"])
		if k != "":
			return L10n.t("launcher.manifest.where_why", {"where": where, "why": k})
		if typeof(a.name) != TYPE_STRING or a.name != macos_asset_name(m.version):
			return L10n.t("launcher.manifest.bad_name", {"where": where, "name": str(a.name), "expected": macos_asset_name(m.version)})
		var size := _int(a.size)
		if size <= 0 or size > MAX_ASSET:
			return L10n.t("launcher.manifest.bad_size", {"where": where, "size": str(a.size)})
		a.size = size
		if typeof(a.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(a.sha256) == null:
			return L10n.t("launcher.manifest.bad_sha256", {"where": where})
		var file_why := _files_schema(a.get("files"), top_folder(a.name))
		return L10n.t("launcher.manifest.where_files", {"where": where, "why": file_why}) if file_why != "" else ""
	if schema_mode != "standard":
		return L10n.t("launcher.manifest.unknown_mode", {"mode": schema_mode})
	var kinds: Array = KINDS.keys() if _int(m.format) == 2 else ["game"]
	k = _keys(m.assets, kinds)
	if k != "":
		return L10n.t("launcher.manifest.assets_why", {"why": k})
	for kind in kinds:
		if typeof(m.assets[kind]) != TYPE_DICTIONARY:
			return L10n.t("launcher.manifest.kind_not_object", {"kind": kind})
		k = _keys(m.assets[kind], PLATFORMS)
		if k != "":
			return L10n.t("launcher.manifest.kind_why", {"kind": kind, "why": k})
		for platform in PLATFORMS:
			var a = m.assets[kind][platform]
			var where := "assets.%s.%s" % [kind, platform]
			if typeof(a) != TYPE_DICTIONARY:
				return L10n.t("launcher.manifest.where_not_object", {"where": where})
			k = _keys(a, ["name", "size", "sha256", "files"])
			if k != "":
				return L10n.t("launcher.manifest.where_why", {"where": where, "why": k})
			if typeof(a.name) != TYPE_STRING or a.name != asset_name(kind, m.version, platform):
				return L10n.t("launcher.manifest.bad_name", {"where": where, "name": str(a.name), "expected": asset_name(kind, m.version, platform)})
			var size := _int(a.size)
			if size <= 0 or size > MAX_ASSET:
				return L10n.t("launcher.manifest.bad_size", {"where": where, "size": str(a.size)})
			a.size = size
			if typeof(a.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(a.sha256) == null:
				return L10n.t("launcher.manifest.bad_sha256", {"where": where})
			var why := _files_schema(a.get("files"), top_folder(a.name))
			if why != "":
				return L10n.t("launcher.manifest.where_files", {"where": where, "why": why})
	return ""


static func _files_schema(files, top: String) -> String:
	if typeof(files) != TYPE_DICTIONARY or files.is_empty() or files.size() > Archive.MAX_MEMBERS:
		return L10n.t("launcher.manifest.files_count", {"limit": Archive.MAX_MEMBERS})
	var seen := {}
	for rel in files:
		if typeof(rel) != TYPE_STRING:
			return L10n.t("launcher.manifest.file_name_not_string")
		var c := Archive.check_name(top + "/" + rel, top)
		if not c.ok or c.rel != rel or c.dir:
			return L10n.t("launcher.manifest.file_name_bad", {"name": str(rel).left(100).c_escape(), "why": c.error})
		if seen.has(rel.to_lower()):
			return L10n.t("launcher.manifest.file_twice", {"name": rel})
		seen[rel.to_lower()] = true
		var f = files[rel]
		if typeof(f) != TYPE_DICTIONARY or _keys(f, ["size", "sha256"]) != "":
			return L10n.t("launcher.manifest.file_not_entry", {"name": rel})
		var size := _int(f.size)
		if size < 0:
			return L10n.t("launcher.manifest.file_bad_size", {"name": rel})
		f.size = size
		if typeof(f.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(f.sha256) == null:
			return L10n.t("launcher.manifest.file_bad_sha256", {"name": rel})
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


static func verify_payload(bytes: PackedByteArray, sig: PackedByteArray, release_keys: Array, expect: Dictionary, nightly_keys := []) -> Dictionary:
	var fail := func(why: String) -> Dictionary: return {"ok": false, "error": why, "payload": {}}
	if not has_keys(release_keys):
		return fail.call(L10n.t("launcher.manifest.no_release_key"))
	if bytes.is_empty():
		return fail.call(L10n.t("launcher.manifest.payload_missing", {"file": PAYLOAD}))
	if bytes.size() > MAX_BYTES:
		return fail.call(L10n.t("launcher.manifest.payload_too_big", {"file": PAYLOAD, "size": bytes.size(), "limit": MAX_BYTES}))
	if sig.size() != 64:
		return fail.call(L10n.t("launcher.manifest.payload_sig_size", {"file": PAYLOAD_SIG, "size": sig.size()}))
	if not ReleaseSignature.valid_scoped(release_keys, nightly_keys, PAYLOAD, bytes, sig):
		return fail.call(L10n.t("launcher.manifest.payload_not_signed", {"file": PAYLOAD, "count": release_keys.size()}))
	var json := JSON.new()
	if json.parse(bytes.get_string_from_utf8()) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return fail.call(L10n.t("launcher.manifest.payload_not_json", {"file": PAYLOAD}))
	var p: Dictionary = json.data
	var k := _keys(p, ["format", "product", "repo", "version", "platform", "files"])
	if k != "":
		return fail.call(L10n.t("launcher.manifest.payload_why", {"file": PAYLOAD, "why": k}))
	if _int(p.format) != 1:
		return fail.call(L10n.t("launcher.manifest.payload_format", {"file": PAYLOAD, "format": str(p.format)}))
	for key in ["product", "repo", "version", "platform"]:
		if typeof(p[key]) != TYPE_STRING:
			return fail.call(L10n.t("launcher.manifest.payload_not_string", {"file": PAYLOAD, "key": key}))
	if p.product != "OpenBFME":
		return fail.call(L10n.t("launcher.manifest.payload_product", {"file": PAYLOAD, "product": p.product.left(40)}))
	if p.repo != expect.get("repo", p.repo):
		return fail.call(L10n.t("launcher.manifest.payload_repo", {"file": PAYLOAD, "repo": p.repo.left(140), "expected": expect.repo}))
	if not Semver.is_valid(p.version):
		return fail.call(L10n.t("launcher.manifest.payload_why", {"file": PAYLOAD, "why": Semver.parse(p.version).error}))
	if not p.platform in PAYLOAD_PLATFORMS or p.platform != expect.get("platform", p.platform):
		return fail.call(L10n.t("launcher.manifest.payload_platform", {"file": PAYLOAD, "platform": p.platform.left(40)}))
	var why := _files_schema(p.get("files"), "openbfme-%s-%s" % [p.version, p.platform])
	if why != "":
		return fail.call(L10n.t("launcher.manifest.payload_files", {"file": PAYLOAD, "why": why}))
	if p.files.has(PAYLOAD) or p.files.has(PAYLOAD_SIG):
		return fail.call(L10n.t("launcher.manifest.payload_names_itself", {"file": PAYLOAD}))
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
	return {"ok": false, "error": L10n.t("launcher.manifest.no_launcher", {"version": m.version, "exe": exe})}


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
			return L10n.t("launcher.manifest.file_missing", {"name": rel})
		var f := FileAccess.open(p, FileAccess.READ)
		if f == null or f.get_length() != int(files[rel].size):
			return L10n.t("launcher.manifest.file_bad_signed_size", {"name": rel})
		var hc := HashingContext.new()
		hc.start(HashingContext.HASH_SHA256)
		while f.get_position() < f.get_length():
			hc.update(f.get_buffer(1 << 20))
		if hc.finish().hex_encode() != files[rel].sha256:
			return L10n.t("launcher.manifest.file_bad_hash", {"name": rel})
	for rel in ([] if any_extra else _list(dir, "")):
		if not files.has(rel) and not rel in extra_ok and not (finder_ok and finder_metadata(rel.get_file())):
			return L10n.t("launcher.manifest.file_extra", {"name": rel})
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
