extends RefCounted
## Base class for every retail_sim_* subsystem module.
##
## ARCHITECTURE (the strangler-fig contract, Q81):
## - STATE LIVES ON THE SIM. A subsystem holds logic only; it reads and writes
##   `sim.<member>`. Tests, serialization, and the lockstep hash never have to
##   know which module owns which behavior.
## - The back-reference is a WeakRef: subsystems must never keep a freed sim
##   alive (RefCounted cycles were caught by the script-wiring orphan
##   contracts).
## - EVENTS are the outward surface: gameplay facts flow through emit_event()
##   into the sim's ordered event stream, which presentation (HUD, EVA, audio)
##   consumes. The TICK PIPELINE stays deterministically ordered — lockstep
##   peers must run identical handler order, so there is deliberately no
##   asynchronous dispatch here (retail's engine updates modules in fixed
##   order for the same reason).

var _sim_ref: WeakRef

var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func emit_event(kind: String, entity_id: int, target_id: int, data: Dictionary = {}) -> void:
	## Raise a gameplay event into the sim's ordered event stream.
	sim._emit_event(kind, entity_id, target_id, data)
