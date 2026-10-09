## Lane LAUNCH-1: the signed release manifest (written by tools/release/make_manifest.py, signed by tools/release/sign_manifest.py).
##
## manifest.json is checked byte for byte against its detached Ed25519 signature (manifest.json.sig, 64 raw bytes) under the release key
## compiled into the launcher BEFORE it is parsed; then its schema is checked strictly (unknown or missing keys, a wrong type, a bad
## version, channel, date, commit, asset name, size or digest are errors) and it must belong to the release it came with: the same
## repository, the tag as its version, its channel matching the release's pre-release flag. Format (docs/RELEASE.md, "The launcher"):
##   {"format": 1, "product": "OpenBFME", "repo": "<owner>/<name>", "version": "v0.3.0", "channel": "stable" | "preview",
##    "date": "YYYY-MM-DD", "commit": "<40 hex>",
##    "assets": {"game": {"linux-x64": ASSET, "windows-x64": ASSET}, "launcher": {"linux-x64": ASSET, "windows-x64": ASSET}}}
##   ASSET = {"name": "openbfme[-launcher]-<version>-<platform>.tar.gz | .zip", "size": <bytes>, "sha256": "<64 hex>",
##            "files": {"<path in the package>": {"size": <bytes>, "sha256": "<64 hex>"}, ...}}
## Format 2 (round 3): "files" lists every file of the package, so the launcher checks unpacked files against signed data whenever it is
## about to run them (a staged launcher before the swap, an installed game before Play), not only the downloaded archive.
extends RefCounted

const Ed25519 := preload("res://scripts/crypto/ed25519.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Archive := preload("res://scripts/core/archive.gd")

const FORMAT := 2
const PLATFORMS := ["linux-x64", "windows-x64"]
const KINDS := {"game": "openbfme", "launcher": "openbfme-launcher"}
const MAX_BYTES := 64 * 1024
const MAX_ASSET := 4 * 1024 * 1024 * 1024


static func asset_name(kind: String, version: String, platform: String) -> String:
	return "%s-%s-%s.%s" % [KINDS[kind], version, platform, "zip" if platform.begins_with("windows") else "tar.gz"]


## the archive's single top folder (the asset name without its extension)
static func top_folder(asset: String) -> String:
	return asset.trim_suffix(".tar.gz").trim_suffix(".zip")


## {ok, error, manifest}. `expect`: {repo, tag, prerelease} of the release it was downloaded with ({} for a stored, already matched one
## except `repo`, which is always checked).
static func verify(bytes: PackedByteArray, sig: PackedByteArray, release_key: PackedByteArray, expect: Dictionary) -> Dictionary:
	var fail := func(why: String) -> Dictionary: return {"ok": false, "error": why, "manifest": {}}
	if release_key.size() != 32:
		return fail.call("this launcher has no release key (a development run without --trust-key?)")
	if bytes.size() > MAX_BYTES:
		return fail.call("manifest.json is %d bytes (at most %d)" % [bytes.size(), MAX_BYTES])
	if sig.size() != 64:
		return fail.call("manifest.json.sig is %d bytes (an Ed25519 signature is 64)" % sig.size())
	if not Ed25519.verify(release_key, bytes, sig):
		return fail.call("the manifest's signature is not valid for the release key: the release was not signed by the OpenBFME release key, or it was changed")
	var text := bytes.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(text) != OK or typeof(json.data) != TYPE_DICTIONARY:
		return fail.call("manifest.json is not a JSON object")
	var m: Dictionary = json.data
	var why := _schema(m)
	if why != "":
		return fail.call("manifest.json: " + why)
	if expect.has("repo") and m.repo != expect.repo:
		return fail.call("the manifest is for %s, this launcher updates from %s" % [m.repo, expect.repo])
	if expect.has("tag") and m.version != expect.tag:
		return fail.call("the manifest is for %s but came with the release %s" % [m.version, expect.tag])
	if expect.has("prerelease") and bool(expect.prerelease) != (m.channel == "preview"):
		return fail.call("the manifest's channel (%s) does not match the release's pre-release flag" % m.channel)
	return {"ok": true, "error": "", "manifest": m}


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


static func _schema(m: Dictionary) -> String:
	var k := _keys(m, ["format", "product", "repo", "version", "channel", "date", "commit", "assets"])
	if k != "":
		return k
	if _int(m.format) != FORMAT:
		return "format %s (this launcher reads format %d)" % [str(m.format), FORMAT]
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
	k = _keys(m.assets, KINDS.keys())
	if k != "":
		return "assets: " + k
	for kind in KINDS:
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


## "" when the files of folder `dir` are exactly the signed `files` (the same names, sizes and SHA-256; `extra_ok` names files that may
## also be there), else what differs
static func check_files(dir: String, files: Dictionary, extra_ok: Array = []) -> String:
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
	for rel in _list(dir, ""):
		if not files.has(rel) and not rel in extra_ok:
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
