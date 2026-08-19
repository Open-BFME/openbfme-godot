class_name W3DTextureMappers
extends RefCounted
## Retail W3D vertex-material TEXTURE MAPPERS, animated at runtime.
##
## SAGE animates many "static" meshes through the vertex material's stage-0
## mapper rather than through a bone animation: the Men fortress banner
## (GBFortress.GBFFLAG) is a 4x4 flip-book (`stage0_mapping = 7` GRID,
## `vm_args_0 = "FPS=15.0; Log2Width=2; Last=16"` over EXGFlagSeq.tga), the
## fortress torches (FLAMES, EXFireTorchSeq.tga) the same, waterfalls scroll
## with LINEAR OFFSET (`UPerSec`/`VPerSec`). The importer already carries
## every W3D vertex material's mapper id and argument string into the GLB as
## material extras (`stage0_mapping`, `vm_args_0`); this module reads them at
## GLB load and drives the material's uv1 transform on a shared clock, exactly
## the W3D GridClassMapper / LinearOffsetTextureMapper arithmetic:
##
##   GRID:  width = 2^Log2Width; frame = floor(t*FPS) mod Last (default w*w)
##          uv' = uv / width + (frame mod width, frame div width) / width
##   LINEAR OFFSET: uv' = uv + (t*UPerSec, t*VPerSec)   (fract)
##
## Nothing here is invented: no mapper args -> no animation. Owner playtest
## 2026-08-19: "Men fortress flags do not sway" (queue Q47).

const MAPPER_GRID := 7
const MAPPER_LINEAR_OFFSET := 4
const META_KEY := "w3d_texture_mapper"

## Materials that carry an animated mapper, shared across every instance that
## uses them (glTF materials are shared resources; the cache duplicates nodes,
## not materials), so one tick moves every flag and torch on the map.
static var _animated: Array[Material] = []
static var _registered: Dictionary = {}


static func tag_gltf_materials(state: GLTFState) -> int:
	## Read the GLB's material extras and tag/register every animated mapper.
	## Returns how many materials were newly registered.
	var json_materials: Array = (state.json as Dictionary).get("materials", []) as Array
	var materials: Array = state.get_materials()
	var added := 0
	for index in mini(json_materials.size(), materials.size()):
		var material: Material = materials[index]
		if material == null or _registered.has(material.get_instance_id()):
			continue
		var extras_value: Variant = (json_materials[index] as Dictionary).get("extras", {})
		if typeof(extras_value) != TYPE_DICTIONARY:
			continue
		var mapper := parse_mapper(extras_value as Dictionary)
		if mapper.is_empty():
			continue
		material.set_meta(META_KEY, mapper)
		if material is BaseMaterial3D:
			_apply(material as BaseMaterial3D, mapper, 0.0)
		_animated.append(material)
		_registered[material.get_instance_id()] = true
		added += 1
	return added


static func parse_mapper(extras: Dictionary) -> Dictionary:
	## {} unless stage-0 authors a mapper this module animates.
	var mapping := int(extras.get("stage0_mapping", 0))
	var args := parse_args(String(extras.get("vm_args_0", "")))
	match mapping:
		MAPPER_GRID:
			var log2_width := int(args.get("log2width", 0))
			var width := 1 << maxi(0, log2_width)
			var fps := float(args.get("fps", 0.0))
			var last := int(args.get("last", width * width))
			if fps <= 0.0 or width <= 1:
				# A grid with one cell or no clock never moves; W3D shows frame 0.
				return {}
			return {"kind": "grid", "fps": fps, "width": width, "last": clampi(last, 1, width * width)}
		MAPPER_LINEAR_OFFSET:
			var u := float(args.get("upersec", 0.0))
			var v := float(args.get("vpersec", 0.0))
			if is_zero_approx(u) and is_zero_approx(v):
				return {}
			return {"kind": "linear", "u_per_sec": u, "v_per_sec": v}
	return {}


static func parse_args(text: String) -> Dictionary:
	## `FPS=15.0; The frames per second, Log2Width=2; So 0=width 1, ...` ->
	## {"fps": 15.0, "log2width": 2, ...}. Retail separates rows with "\r\n"
	## (the exporter folds them to ", ") and follows each value with prose
	## after ';'. Keys are case-folded; values are the first numeric token.
	var out := {}
	var normalized := text.replace("\r\n", "\n").replace("\r", "\n")
	for chunk in normalized.split("\n"):
		for piece in chunk.split(","):
			var eq := piece.find("=")
			if eq <= 0:
				continue
			var key := piece.substr(0, eq).strip_edges().to_lower()
			var rest := piece.substr(eq + 1).strip_edges()
			var semi := rest.find(";")
			if semi >= 0:
				rest = rest.substr(0, semi).strip_edges()
			var token := rest.split(" ")[0] if rest != "" else ""
			if key == "" or token == "" or not token.is_valid_float():
				continue
			if not out.has(key):
				out[key] = token.to_float()
	return out


static func advance(now_seconds: float) -> void:
	## Drive every registered material to the frame/offset for `now_seconds`.
	var alive: Array[Material] = []
	for material in _animated:
		if not is_instance_valid(material):
			continue
		alive.append(material)
		if material is BaseMaterial3D:
			_apply(material as BaseMaterial3D, material.get_meta(META_KEY, {}) as Dictionary, now_seconds)
	if alive.size() != _animated.size():
		_animated = alive
		_registered.clear()
		for material in _animated:
			_registered[material.get_instance_id()] = true


static func frame_for(mapper: Dictionary, now_seconds: float) -> int:
	var fps := float(mapper.get("fps", 0.0))
	var last := int(mapper.get("last", 1))
	if fps <= 0.0 or last <= 0:
		return 0
	return int(floor(now_seconds * fps)) % last


static func uv_transform_for(mapper: Dictionary, now_seconds: float) -> Dictionary:
	## {"scale": Vector3, "offset": Vector3} for the material's uv1.
	match String(mapper.get("kind", "")):
		"grid":
			var width := maxi(1, int(mapper.get("width", 1)))
			var frame := frame_for(mapper, now_seconds)
			var cell := 1.0 / float(width)
			return {
				"scale": Vector3(cell, cell, 1.0),
				"offset": Vector3(float(frame % width) * cell, float(frame / width) * cell, 0.0),
			}
		"linear":
			var u := fposmod(now_seconds * float(mapper.get("u_per_sec", 0.0)), 1.0)
			var v := fposmod(now_seconds * float(mapper.get("v_per_sec", 0.0)), 1.0)
			return {"scale": Vector3.ONE, "offset": Vector3(u, v, 0.0)}
	return {"scale": Vector3.ONE, "offset": Vector3.ZERO}


static func _apply(material: BaseMaterial3D, mapper: Dictionary, now_seconds: float) -> void:
	var transform := uv_transform_for(mapper, now_seconds)
	material.uv1_scale = transform["scale"]
	material.uv1_offset = transform["offset"]


static func animated_count() -> int:
	return _animated.size()


static func clear() -> void:
	_animated.clear()
	_registered.clear()
