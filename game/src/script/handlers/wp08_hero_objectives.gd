extends RefCounted

## Retail objective condition:
##   ANY_HERO_REACHED_RANK(PLAYER, INT hero_count, INT rank)
##
## The sourced WorldBuilder text is "Does <PLAYER> have <INT> Heroes that
## have reached rank <INT>". The count is therefore a threshold, not a spare
## argument: every slot is forwarded positionally to the authoritative
## progression query.

const PACKAGE := "WP08-hero-objectives"
const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.condition("ANY_HERO_REACHED_RANK", _any_hero_reached_rank)


static func _any_hero_reached_rank(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	if args.size() != 3:
		ctx["detail"] = "ANY_HERO_REACHED_RANK requires PLAYER, hero count, and rank"
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).progression().any_hero_reached_rank(
		args.text(0), args.integer(1), args.integer(2)
	)
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK
