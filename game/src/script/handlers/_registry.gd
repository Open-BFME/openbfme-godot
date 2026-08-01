class_name SageScriptHandlerRegistry
extends RefCounted

## Deterministic auto-registration for SAGE script handler packages.
##
##
## ============================================================================
## IF YOU ARE IMPLEMENTING A WORK PACKAGE, READ THIS AND NOTHING ELSE FIRST
## ============================================================================
##
## Ship exactly TWO new files and edit NO existing ones:
##
##   1. game/src/script/handlers/wpNN_your_package.gd
##   2. game/tests/handlers_wpNN_your_package_runner.gd
##
## Never edit script_dispatch.gd, script_world.gd, script_world_stub.gd, this
## file, or another package's file. ~17 packages are implemented concurrently;
## every shared-file edit is a merge conflict with someone you cannot see. The
## world surface your package needs was designed up front and already exists in
## script_world.gd. If a world method you need is genuinely missing, that is a
## FINDING TO REPORT, not a line to add.
##
## Your handler file is picked up automatically. It must look like this:
##
##     extends RefCounted
##
##     const PACKAGE := "WP05-camera"
##     const Dispatch := preload("res://src/script/script_dispatch.gd")
##
##     static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
##         reg.action("CAMERA_MOVE_TO_SELECTION", _camera_move_to_selection)
##         reg.condition("CAMERA_IS_MOVING", _camera_is_moving)
##
##     static func _camera_move_to_selection(ctx: Dictionary) -> int:
##         var args: SageScriptArgs = ctx["args"]
##         ...
##         return Dispatch.Status.OK
##
## RULES THAT ARE ENFORCED, NOT SUGGESTED
## --------------------------------------
##   * `PACKAGE` must be declared and unique. Two files claiming one package, or
##     a file with no PACKAGE, is a loud startup error.
##   * Registering a name another package (or tranche 1 in
##     script_handlers_core.gd) already registered is a loud startup error
##     naming BOTH packages. The FIRST registration wins and the second is
##     dropped - never last-writer-wins, because a silently overridden handler
##     is the worst possible outcome of a merge.
##   * Registration is by canonical InternalName only. `reg.action()` refuses
##     any name absent from the sourced vocabulary. Numeric action ids are never
##     used to dispatch: BFME2 renumbered both enums relative to C&C Generals,
##     and no BFME2 id table could be sourced. See script_vocabulary.gd.
##   * Read arguments POSITIONALLY through `ctx.args` (SageScriptArgs), in the
##     order the signature declares. Do NOT search the argument list for a type:
##     several signatures repeat a type or put the value before the name
##     (INCREMENT_COUNTER(INT, COUNTER)), and a type-search silently reads the
##     wrong argument. Arity is validated before your handler runs.
##   * Condition handlers write their answer to `ctx.result` and MUST NOT mutate
##     ctx.env or the world. Conditions are evaluated an unpredictable number of
##     times, so a side effect is a determinism bug.
##   * Never return OK after failing to do the thing. Return the honest status
##     (WORLD_REFUSED / BAD_ARGUMENTS / BLOCKED) and a structured gap is recorded
##     with full identity. Silence is the one unacceptable outcome.
##
## Scan order is the sorted file name, so registration order is identical on
## every machine and in every run. Files beginning with "_" are infrastructure
## and are skipped.
##
##
## ============================================================================
## BLOCKED PACKAGES
## ============================================================================
## Some packages name real actions whose SIMULATION SUBSYSTEM does not exist
## (WP02 fog of war, WP03 garrison/transport, WP04 capture/teleport). They are
## not implemented and not stubbed. They are declared blocked:
##
##     static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
##         reg.blocked_actions(MAP_REVEAL_NAMES, "fog of war")
##
## One shared handler serves every blocked name, so there is no per-name
## function body that could be mistaken for an implementation. A map that uses
## one gets a `blocked-subsystem` gap naming the action and the missing
## subsystem - never a silent skip, and never counted as coverage.

const Dispatch := preload("res://src/script/script_dispatch.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")

const HANDLER_DIRECTORY := "res://src/script/handlers"


static func register_all(dispatch: SageScriptDispatch) -> Dictionary:
	## Loads every package module in HANDLER_DIRECTORY, in sorted file-name
	## order, and registers it. Returns
	##   {"packages": Array[String], "errors": Array[String], "modules": int}
	## `errors` is empty on a healthy startup; a caller that ignores it will
	## still have seen every entry as a push_error.
	var registrar := Registrar.new()
	registrar.dispatch = dispatch

	for path: String in module_paths():
		var module: Resource = load(path)
		if module == null:
			registrar.errors.append("could not load handler module '%s'" % path)
			push_error("SageScriptHandlerRegistry: could not load handler module '%s'" % path)
			continue
		var package := String(module.get("PACKAGE") if module.get("PACKAGE") != null else "")
		if package.strip_edges() == "":
			registrar.errors.append("handler module '%s' declares no PACKAGE" % path)
			push_error(
				(
					"SageScriptHandlerRegistry: handler module '%s' declares no PACKAGE "
					+ "constant; every package file must name itself."
				) % path
			)
			continue
		if registrar.package_paths.has(package):
			registrar.errors.append(
				"package '%s' is claimed by both '%s' and '%s'"
				% [package, String(registrar.package_paths[package]), path]
			)
			push_error(
				"SageScriptHandlerRegistry: package '%s' is claimed by both '%s' and '%s'"
				% [package, String(registrar.package_paths[package]), path]
			)
			continue
		if not module.has_method("register"):
			registrar.errors.append("handler module '%s' has no static register()" % path)
			push_error(
				"SageScriptHandlerRegistry: handler module '%s' has no static register()" % path
			)
			continue
		registrar.package_paths[package] = path
		registrar.current_package = package
		module.call("register", registrar)
		registrar.current_package = ""

	var packages: Array[String] = []
	for name: Variant in registrar.package_paths:
		packages.append(String(name))
	packages.sort()
	return {
		"packages": packages,
		"errors": registrar.errors,
		"modules": registrar.package_paths.size(),
	}


static func module_paths() -> Array[String]:
	## Sorted absolute res:// paths of the package modules. Sorting is what makes
	## registration order reproducible; do not replace it with directory order.
	var out: Array[String] = []
	var dir := DirAccess.open(HANDLER_DIRECTORY)
	if dir == null:
		push_error(
			"SageScriptHandlerRegistry: handler directory '%s' is missing" % HANDLER_DIRECTORY
		)
		return out
	var names := dir.get_files()
	names.sort()
	for file_name: String in names:
		# Exported builds rename scripts to *.gd.remap; source runs see *.gd.
		var logical := file_name.trim_suffix(".remap")
		if not logical.ends_with(".gd"):
			continue
		if logical.begins_with("_"):
			continue
		out.append("%s/%s" % [HANDLER_DIRECTORY, logical])
	return out


static func blocked_handler(ctx: Dictionary) -> int:
	## The ONE handler behind every blocked name. Deliberately shared: 32
	## individual bodies that all refuse would look like 32 implementations in
	## any file listing or diff.
	var dispatch: SageScriptDispatch = ctx["dispatch"]
	var subsystem := dispatch.blocked_subsystem_for(String(ctx["name"]))
	ctx["detail"] = (
		"blocked: the '%s' simulation subsystem does not exist yet, so this "
		+ "action cannot be served by anything"
	) % subsystem
	return Dispatch.Status.BLOCKED


class Registrar:
	extends RefCounted

	## Handed to each package's `register()`. Enforces the rules above.

	var dispatch: SageScriptDispatch = null
	var current_package: String = ""

	## package name -> module path.
	var package_paths: Dictionary = {}

	## "action|NAME" / "condition|NAME" -> owning package.
	var owners: Dictionary = {}

	var errors: Array[String] = []

	func action(name: String, handler: Callable) -> void:
		_register(Vocabulary.Kind.ACTION, name, handler, "")

	func condition(name: String, handler: Callable) -> void:
		_register(Vocabulary.Kind.CONDITION, name, handler, "")

	func deliberate_action(name: String, handler: Callable) -> void:
		## For an action that is implementable but refused ON PURPOSE (the
		## client-random family is desync-prone in a lockstep build). Dispatched
		## normally so a map using it is visible; never counted as coverage.
		if not _claim(Vocabulary.Kind.ACTION, name):
			return
		dispatch.register_deliberate_refusal(name, handler)

	func blocked_actions(names: Array, subsystem: String) -> void:
		for name: Variant in names:
			_register_blocked(Vocabulary.Kind.ACTION, String(name), subsystem)

	func blocked_conditions(names: Array, subsystem: String) -> void:
		for name: Variant in names:
			_register_blocked(Vocabulary.Kind.CONDITION, String(name), subsystem)

	func _register_blocked(kind: int, name: String, subsystem: String) -> void:
		if not _claim(kind, name):
			return
		dispatch.register_blocked(
			kind, name, subsystem, SageScriptHandlerRegistry.blocked_handler
		)

	func _register(kind: int, name: String, handler: Callable, _unused: String) -> void:
		if not _claim(kind, name):
			return
		if kind == Vocabulary.Kind.ACTION:
			dispatch.register_action(name, handler)
		else:
			dispatch.register_condition(name, handler)

	func _claim(kind: int, name: String) -> bool:
		## Returns whether `current_package` may register `name`. The first
		## claimant keeps it; a later one is an error, not an override.
		var label := Vocabulary.kind_label(kind)
		var key := "%s|%s" % [label, name]

		if owners.has(key):
			var message := (
				"SageScriptHandlerRegistry: %s '%s' is registered by both package "
				+ "'%s' and package '%s'. The first registration is kept and the "
				+ "second is DROPPED - resolve the overlap, do not rely on order."
			) % [label, name, String(owners[key]), current_package]
			errors.append(message)
			push_error(message)
			return false

		# Tranche 1 (script_handlers_core.gd) registers directly on the dispatch,
		# before any package module runs. Colliding with it is the same error.
		var existing: Dictionary = (
			dispatch.action_handlers
			if kind == Vocabulary.Kind.ACTION
			else dispatch.condition_handlers
		)
		if existing.has(name):
			var core_message := (
				"SageScriptHandlerRegistry: %s '%s' claimed by package '%s' is "
				+ "already registered outside the package system (tranche 1, "
				+ "script_handlers_core.gd). The existing handler is kept."
			) % [label, name, current_package]
			errors.append(core_message)
			push_error(core_message)
			return false

		owners[key] = current_package
		return true
