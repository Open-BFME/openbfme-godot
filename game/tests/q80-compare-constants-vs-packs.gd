extends SceneTree
## Compare invented constants with real pack-derived manifest values.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const FactionManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    # Get the real manifest from packs
    var pack_manifest := FactionManifestScript.from_registries("men", {}, {}, false)

    # Get the default (invented) manifest
    var default_manifest := FactionManifestScript.default_manifest()

    print("[COMPARISON] Comparing pack-derived vs default (invented) manifests\n")

    var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds",
        "structure_max_health", "structure_build_rules", "unit_damage_types",
        "structure_armor", "spawn_roster"]

    var differences_found := false

    for key in required_keys:
        var pack_val = pack_manifest.get(key)
        var default_val = default_manifest.get(key)

        var pack_str = _value_to_string(pack_val)
        var default_str = _value_to_string(default_val)

        if pack_str != default_str:
            differences_found = true
            print("[DIFFER] %s:" % key)
            print("  Pack:    %s" % pack_str)
            print("  Default: %s" % default_str)
            print("")

    if not differences_found:
        print("[SAME] All required fields have identical values in pack vs default manifests")
    else:
        print("[FINDING] Invented constants DO NOT match pack data - explain hash divergence")

    quit(0)

func _value_to_string(val) -> String:
    if val == null:
        return "NULL"
    match typeof(val):
        TYPE_DICTIONARY:
            var d = val as Dictionary
            return "dict[%d items]" % d.size()
        TYPE_ARRAY:
            var a = val as Array
            return "array[%d items]" % a.size()
        TYPE_STRING:
            return "\"%s\"" % val
        _:
            return str(val)
