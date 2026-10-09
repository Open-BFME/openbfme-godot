## Lane LAUNCH-1: unpacking a verified release archive (tools/release/make_archive.py's .tar.gz / .zip) into a NEW folder.
##
## The archive's SHA-256 was already checked against the signed manifest; these checks stand on their own anyway (a signed archive with a
## bad path must still not escape): exactly one top folder named like the asset; every member name relative, '/'-separated, no `..` or `.`
## component, no empty component, no backslash, no drive letter or other ':' , no character Windows forbids, no reserved Windows device
## name, no control character, no component ending in a dot or space; no two members equal ignoring case; only regular files and
## folders (a tar link, device or extension header is refused; a ZIP member marked as a symbolic link is refused; files are always written
## as new regular files by this code, never through an existing path); at most MAX_MEMBERS members and MAX_TOTAL bytes.
## Writing errors (a full disk) fail the extraction; the caller removes the partial folder.
extends RefCounted

const MAX_MEMBERS := 10000
const MAX_TOTAL := 2 * 1024 * 1024 * 1024
# Windows device names (learn.microsoft.com, "Naming Files, Paths, and Namespaces"): also the superscript digits ¹ ² ³ (Sol r1) and 0
const RESERVED := ["CON", "PRN", "AUX", "NUL", "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
	"COM¹", "COM²", "COM³", "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "LPT¹", "LPT²", "LPT³"]


## {ok, error, rel (the name below the top folder, "" for the top folder itself), dir}
static func check_name(name: String, top: String) -> Dictionary:
	var bad := func(why: String) -> Dictionary: return {"ok": false, "error": "archive member '%s': %s" % [name.left(200).c_escape(), why]}
	if name.is_empty() or name.length() > 400:
		return bad.call("an empty or overlong name")
	if name.begins_with("/"):
		return bad.call("an absolute path")
	if "\\" in name:
		return bad.call("a backslash")
	for c in name:
		var u := c.unicode_at(0)
		if u < 0x20 or u == 0x7f:
			return bad.call("a control character")
		if c in ":*?\"<>|":
			return bad.call("the character '%s' (a drive letter, a stream or a name Windows refuses)" % c)
	var is_dir := name.ends_with("/")
	var parts := name.trim_suffix("/").split("/")
	for p in parts:
		if p == "" :
			return bad.call("an empty path component")
		if p == "." or p == "..":
			return bad.call("a '%s' component" % p)
		if p.ends_with(".") or p.ends_with(" "):
			return bad.call("a component ending in a dot or space")
		if p.get_slice(".", 0).to_upper() in RESERVED:
			return bad.call("a reserved Windows device name")
	if parts[0] != top:
		return bad.call("not inside the package folder %s" % top)
	return {"ok": true, "error": "", "rel": "/".join(parts.slice(1)), "dir": is_dir}


static func _octal(b: PackedByteArray, from: int, to: int) -> int:
	var v := 0
	var seen := false
	for i in range(from, to):
		var c := b[i]
		if c == 0 or c == 0x20:
			if seen:
				break
			continue
		if c < 0x30 or c > 0x37:
			return -1
		v = v * 8 + (c - 0x30)
		seen = true
		if v > MAX_TOTAL:
			return -1
	return v


## {ok, error, members: [{name, dir, data, exec}]} of a .tar.gz (gzip, then ustar / GNU tar headers)
static func read_tar_gz(path: String) -> Dictionary:
	var gz := FileAccess.get_file_as_bytes(path)
	if gz.is_empty():
		return {"ok": false, "error": "cannot read %s (%s)" % [path.get_file(), error_string(FileAccess.get_open_error())]}
	var tar := gz.decompress_dynamic(MAX_TOTAL, FileAccess.COMPRESSION_GZIP)
	if tar.is_empty():
		return {"ok": false, "error": "%s is not a gzip stream (or unpacks to more than %d bytes)" % [path.get_file(), MAX_TOTAL]}
	var members := []
	var pos := 0
	while true:
		if pos + 512 > tar.size():
			return {"ok": false, "error": "the tar archive ends without its end-of-archive block"}
		var h := tar.slice(pos, pos + 512)
		if h.count(0) == 512:
			break
		var stored := _octal(h, 148, 156)
		var sum := 0
		for i in 512:
			sum += 0x20 if i >= 148 and i < 156 else h[i]
		if stored != sum:
			return {"ok": false, "error": "a tar header at %d has a bad checksum" % pos}
		var magic := h.slice(257, 263).get_string_from_ascii()
		if not magic.begins_with("ustar"):
			return {"ok": false, "error": "a tar header at %d is not ustar / GNU" % pos}
		var name := h.slice(0, 100).get_string_from_utf8()
		var prefix := h.slice(345, 500).get_string_from_utf8() if h.slice(257, 263) == "ustar".to_ascii_buffer() + PackedByteArray([0]) else ""
		if prefix != "":
			name = prefix + "/" + name
		var typ := h[156]
		var size := _octal(h, 124, 136)
		if size < 0:
			return {"ok": false, "error": "tar member '%s': a bad size field" % name.c_escape()}
		var mode := _octal(h, 100, 108)
		if typ == 0x35:  # '5' a folder
			if not name.ends_with("/"):
				name += "/"
			members.append({"name": name, "dir": true, "data": PackedByteArray(), "exec": false})
		elif typ == 0x30 or typ == 0:  # '0' / NUL a regular file
			if pos + 512 + size > tar.size():
				return {"ok": false, "error": "tar member '%s' is truncated" % name.c_escape()}
			members.append({"name": name, "dir": false, "data": tar.slice(pos + 512, pos + 512 + size), "exec": mode >= 0 and (mode & 0x40) != 0})
		else:
			return {"ok": false, "error": "tar member '%s' has type '%s' (a link, device or extension header: only files and folders are allowed)" % [name.c_escape(), String.chr(typ) if typ >= 0x20 and typ < 0x7f else str(typ)]}
		if members.size() > MAX_MEMBERS:
			return {"ok": false, "error": "more than %d archive members" % MAX_MEMBERS}
		pos += 512 + (size + 511) / 512 * 512
	return {"ok": true, "error": "", "members": members}


static func _u16(b: PackedByteArray, o: int) -> int:
	return b[o] | (b[o + 1] << 8)


static func _u32(b: PackedByteArray, o: int) -> int:
	return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


## {ok, error, members} of a .zip: the central directory is read here (names, the Unix mode in the external attributes, sizes, flags);
## the data through Godot's ZIPReader
static func read_zip(path: String) -> Dictionary:
	var z := FileAccess.get_file_as_bytes(path)
	if z.size() < 22:
		return {"ok": false, "error": "%s is not a ZIP archive" % path.get_file()}
	var eocd := -1
	for i in range(z.size() - 22, maxi(-1, z.size() - 22 - 65536), -1):
		if _u32(z, i) == 0x06054b50:
			eocd = i
			break
	if eocd < 0:
		return {"ok": false, "error": "%s has no ZIP end-of-central-directory record" % path.get_file()}
	var count := _u16(z, eocd + 10)
	var cd := _u32(z, eocd + 16)
	if count == 0xFFFF or cd == 0xFFFFFFFF:
		return {"ok": false, "error": "ZIP64 archives are not written by the packager"}
	if count > MAX_MEMBERS:
		return {"ok": false, "error": "more than %d archive members" % MAX_MEMBERS}
	var reader := ZIPReader.new()
	if reader.open(path) != OK:
		return {"ok": false, "error": "%s cannot be opened as a ZIP archive" % path.get_file()}
	var members := []
	var pos := cd
	var total := 0
	for n in count:
		if pos + 46 > z.size() or _u32(z, pos) != 0x02014b50:
			reader.close()
			return {"ok": false, "error": "a malformed ZIP central directory"}
		var flags := _u16(z, pos + 8)
		var method := _u16(z, pos + 10)
		var usize := _u32(z, pos + 24)
		var nlen := _u16(z, pos + 28)
		var elen := _u16(z, pos + 30)
		var clen := _u16(z, pos + 32)
		var made_by := _u16(z, pos + 4) >> 8
		var ext := _u32(z, pos + 38)
		var name := z.slice(pos + 46, pos + 46 + nlen).get_string_from_utf8()
		pos += 46 + nlen + elen + clen
		if flags & 1:
			reader.close()
			return {"ok": false, "error": "ZIP member '%s' is encrypted" % name.c_escape()}
		if method != 0 and method != 8:
			reader.close()
			return {"ok": false, "error": "ZIP member '%s' uses compression method %d (stored and deflate only)" % [name.c_escape(), method]}
		var unix_mode := (ext >> 16) & 0xFFFF if made_by == 3 else 0
		if (unix_mode & 0xF000) == 0xA000:
			reader.close()
			return {"ok": false, "error": "ZIP member '%s' is a symbolic link" % name.c_escape()}
		total += usize
		if total > MAX_TOTAL:
			reader.close()
			return {"ok": false, "error": "the archive unpacks to more than %d bytes" % MAX_TOTAL}
		if name.ends_with("/"):
			members.append({"name": name, "dir": true, "data": PackedByteArray(), "exec": false})
			continue
		var data := reader.read_file(name)
		if data.size() != usize:
			reader.close()
			return {"ok": false, "error": "ZIP member '%s' unpacks to %d bytes, its header says %d" % [name.c_escape(), data.size(), usize]}
		members.append({"name": name, "dir": false, "data": data, "exec": (unix_mode & 0x40) != 0})
	reader.close()
	return {"ok": true, "error": "", "members": members}


## Unpack `archive` (kind "tar.gz" or "zip") whose single top folder must be `top` into `dest`, which must not exist yet.
## {ok, error, files: {rel: {size, sha256}}}
static func extract(archive: String, kind: String, top: String, dest: String) -> Dictionary:
	var read := read_tar_gz(archive) if kind == "tar.gz" else read_zip(archive)
	if not read.ok:
		return {"ok": false, "error": read.error, "files": {}}
	var seen := {}
	var plan := []
	for m in read.members:
		var c := check_name(m.name, top)
		if not c.ok:
			return {"ok": false, "error": c.error, "files": {}}
		if c.rel == "":
			if not m.dir:
				return {"ok": false, "error": "archive member '%s' is a file named like the package folder" % m.name, "files": {}}
			continue
		var key: String = c.rel.to_lower()
		if seen.has(key):
			return {"ok": false, "error": "archive member '%s' appears twice (names are compared ignoring case)" % m.name.c_escape(), "files": {}}
		seen[key] = true
		plan.append([c.rel, m])
	# a file may not also be the parent folder of another member
	for key in seen:
		var parent: String = key.get_base_dir()
		while parent != "":
			if seen.has(parent) and not _is_dir_member(plan, parent):
				return {"ok": false, "error": "archive member '%s' is both a file and a folder" % parent, "files": {}}
			parent = parent.get_base_dir()
	if DirAccess.dir_exists_absolute(dest) or FileAccess.file_exists(dest):
		return {"ok": false, "error": "%s already exists" % dest, "files": {}}
	if DirAccess.make_dir_recursive_absolute(dest) != OK:
		return {"ok": false, "error": "cannot create %s" % dest, "files": {}}
	var files := {}
	for item in plan:
		var rel: String = item[0]
		var m: Dictionary = item[1]
		var target := dest.path_join(rel)
		if m.dir:
			if DirAccess.make_dir_recursive_absolute(target) != OK:
				return {"ok": false, "error": "cannot create %s" % target, "files": {}}
			continue
		if DirAccess.make_dir_recursive_absolute(target.get_base_dir()) != OK:
			return {"ok": false, "error": "cannot create %s" % target.get_base_dir(), "files": {}}
		var f := FileAccess.open(target, FileAccess.WRITE)
		if f == null:
			return {"ok": false, "error": "cannot write %s (%s)" % [target, error_string(FileAccess.get_open_error())], "files": {}}
		var ok := f.store_buffer(m.data)
		f.flush()
		var err := f.get_error()
		f.close()
		if not ok or err != OK:
			return {"ok": false, "error": "writing %s failed (%s): is the disk full?" % [target, error_string(err) if err != OK else "short write"], "files": {}}
		if m.exec and OS.get_name() != "Windows":
			FileAccess.set_unix_permissions(target, FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER | FileAccess.UNIX_EXECUTE_OWNER
				| FileAccess.UNIX_READ_GROUP | FileAccess.UNIX_EXECUTE_GROUP | FileAccess.UNIX_READ_OTHER | FileAccess.UNIX_EXECUTE_OTHER)
		var hc := HashingContext.new()
		hc.start(HashingContext.HASH_SHA256)
		if not m.data.is_empty():
			hc.update(m.data)
		files[rel] = {"size": m.data.size(), "sha256": hc.finish().hex_encode()}
	return {"ok": true, "error": "", "files": files}


static func _is_dir_member(plan: Array, key: String) -> bool:
	for item in plan:
		if item[0].to_lower() == key:
			return item[1].dir
	return false
