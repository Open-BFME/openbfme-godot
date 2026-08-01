extends RefCounted

## Lua 4.0.1 table with a DETERMINISTIC iteration order.
##
## Why not a bare Dictionary: this VM runs inside a lockstep-deterministic
## simulation, so `next()`, `foreach()` and `sort()` must visit keys in an
## order that is identical on every machine, every build and every run.
## Godot's Dictionary happens to preserve insertion order today, but that is an
## implementation detail with no cross-version contract, and rehashing after
## erase is explicitly unspecified. So this class keeps its OWN insertion-
## ordered slot array and iterates that. Real Lua 4.0's `next` order is a hash
## walk and is not reproducible anyway, so matching it is neither possible nor
## desirable - what matters is that two peers running the same script see the
## same order, which insertion order guarantees.
##
## Removing a key (t[k] = nil) leaves a TOMBSTONE in the slot array instead of
## compacting it. That is deliberate: Lua permits clearing fields during a
## `next` traversal, and compaction would silently shift slots under a walk in
## progress. Re-inserting a previously removed key reuses its old slot, so slot
## growth is bounded by the number of DISTINCT keys the table has ever held
## rather than by the number of set/erase cycles.
##
## Number keys are normalised to float (Lua 4.0 has exactly one number type, a
## double) so t[1] and t[1.0] are the same slot no matter which GDScript
## numeric type the host handed us.

## Marker parked in `_order` where a key used to live. Never a valid Lua value.
const TOMBSTONE := &"__lua_table_dead_slot__"

## Lua tag, as returned by tag() and set by settag(). Starts at 4, which is
## LuaValue.TAG_TABLE (lua.h 4.0's LUA_TTABLE); anything else was minted by
## newtag(). Written as a literal rather than referencing LuaValue so that this
## file stays dependency-free and cannot form a preload cycle.
var tag: int = 4

## key -> value. Only LIVE keys are present; this is the source of truth for
## membership.
var _map: Dictionary = {}

## Insertion-ordered slots. Live slots hold the key; dead slots hold TOMBSTONE.
var _order: Array = []

## key -> its index in `_order`. Entries survive removal so a re-inserted key
## lands back in its original slot instead of appending a new one.
var _slot: Dictionary = {}


## Normalises a Lua value for use as a table key.
##
## Returns the normalised key, or the string "?nan" sentinel check is left to
## the caller: NaN keys are rejected upstream (see LuaInterp._table_set) because
## NaN != NaN makes them unfindable and therefore a silent-data-loss hazard.
static func normalize_key(key: Variant) -> Variant:
	if key is int:
		return float(key)
	if key is float:
		# Collapse -0.0 to 0.0 so t[-0.0] and t[0.0] cannot become two slots.
		if key == 0.0:
			return 0.0
		return key
	return key


## Raw read - no `index`/`gettable` tag method, no coercion. Returns null (nil)
## when absent.
func rawget(key: Variant) -> Variant:
	return _map.get(normalize_key(key))


## Raw write - no `settable` tag method. Assigning null (nil) removes the key,
## exactly as in Lua.
func rawset(key: Variant, value: Variant) -> void:
	var k: Variant = normalize_key(key)
	if value == null:
		if _map.has(k):
			_map.erase(k)
			_order[int(_slot[k])] = TOMBSTONE
		return
	if not _map.has(k):
		if _slot.has(k):
			# Resurrect the key's original slot rather than appending.
			_order[int(_slot[k])] = k
		else:
			_slot[k] = _order.size()
			_order.append(k)
	_map[k] = value


func has(key: Variant) -> bool:
	return _map.has(normalize_key(key))


## Number of live keys. This is NOT Lua's getn() - see LuaStdlib._getn, which
## honours the `n` field the way Lua 4.0's luaL_getn does.
func count() -> int:
	return _map.size()


## Live keys in deterministic insertion order. Returns a fresh array, so a
## caller may mutate the table while walking its snapshot.
func keys_in_order() -> Array:
	var out: Array = []
	out.resize(_map.size())
	var i := 0
	for k in _order:
		if k is StringName and k == TOMBSTONE:
			continue
		out[i] = k
		i += 1
	return out


## Lua's next(): returns [key, value], or [] at the end of the traversal.
## Passing null starts a new traversal. Returns null (not []) when `key` is not
## a key of this table, which the caller must report as an error the way Lua
## does ("invalid key to 'next'").
func next_entry(key: Variant) -> Variant:
	var start := 0
	if key != null:
		var k: Variant = normalize_key(key)
		if not _slot.has(k):
			return null
		start = int(_slot[k]) + 1
	for i in range(start, _order.size()):
		var candidate: Variant = _order[i]
		if candidate is StringName and candidate == TOMBSTONE:
			continue
		return [candidate, _map[candidate]]
	return []


## The "border" Lua 4.0 computes when a table has no numeric `n` field: the
## largest k such that t[1] .. t[k] are all non-nil.
func border() -> int:
	var n := 0
	while _map.has(float(n + 1)):
		n += 1
	return n
