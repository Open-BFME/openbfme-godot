#!/usr/bin/env python3
"""Port middle-earth-rts config export into Open BFME data/base JSON pack."""
from __future__ import annotations
import json
from pathlib import Path

SRC = Path(r"C:\Users\Jonathan\Desktop\open-bfme\tools\mert_export\full_config.json")
BASE = Path(r"C:\Users\Jonathan\Desktop\open-bfme\game\data\base")
ICONS = BASE / "assets" / "icons"


def icon_for(key: str) -> str:
    k = key.lower()
    aliases = {
        "fortress": "gfortress.png",
        "mfortress": "mfortress.png",
        "efortress": "efortress.png",
        "gfortress": "fortress.png",
        "spikedwall": "wall.png",
        "spikedgate": "gate.png",
        "spostern": "gate.png",
        "postern": "gate.png",
        "hedgewall": "hedgewall.png" if (ICONS / "hedgewall.png").exists() else "wall.png",
        "hedgegate": "hedgegate.png" if (ICONS / "hedgegate.png").exists() else "gate.png",
        "epostern": "gate.png",
        "gspikedwall": "wall.png",
        "gspikedgate": "gate.png",
        "gpostern": "gate.png",
        "mtower": "tower.png",
        "etower": "tower.png",
        "gtower": "tower.png",
        "mwell": "well.png",
        "ewell": "well.png",
        "gwell": "well.png",
        "gtrollcage": "trollcage.png",
        "forgedblades": "forgedblades.png",
        "heavyarmor": "heavyarmor.png",
        "firearrows": "firearrows.png",
        "rangedarmor": "rangerarmor.png",
        "garrison": "barracks.png",
        "masonry": "bulwark.png",
        "banners": "rally.png",
        "sunflare": "sunflare.png",
        "armyofdead": "armyofdead.png",
        "calleagles": "calleagles.png",
    }
    candidates = []
    if key in aliases:
        candidates.append(aliases[key])
    if k in aliases:
        candidates.append(aliases[k])
    candidates.append(f"{k}.png")
    candidates.append(f"{key}.png")
    for c in candidates:
        if c and (ICONS / c).exists():
            return f"icons/{c}"
    return f"icons/{k}.png"


def clear_json_dir(sub: str) -> Path:
    d = BASE / sub
    d.mkdir(parents=True, exist_ok=True)
    for p in d.glob("*.json"):
        p.unlink()
    return d


def write(sub: str, name: str, obj: dict) -> None:
    p = BASE / sub / f"{name}.json"
    p.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")


def ability_kind(a: dict) -> str:
    if a.get("summons"):
        return "summon"
    if a.get("healFrac") and a.get("radius"):
        return "heal_nova"
    if a.get("fear") or (a.get("stun") and not a.get("dmg") and not a.get("targetDmg")):
        return "fear_nova"
    if a.get("targetDmg") or (a.get("dmg") and a.get("range") and not a.get("radius")):
        return "single_target"
    if a.get("dmgMul") or a.get("selfDmgMul") or a.get("selfSpeedMul") or a.get("form"):
        return "buff_self"
    if a.get("dmg") and a.get("radius"):
        return "nova_damage"
    return "nova_damage"


def main() -> None:
    data = json.loads(SRC.read_text(encoding="utf-8"))
    for sub in ["units", "buildings", "factions", "abilities", "powers", "research", "maps"]:
        clear_json_dir(sub)

    units = data["UNIT_TYPES"]
    for key, u in units.items():
        d = {
            "id": key,
            "name": u.get("name", key),
            "faction": u.get("faction", "neutral"),
            "cost": u.get("cost", 0),
            "build_time": u.get("buildTime", 8),
            "size": u.get("size", 1),
            "hp": u.get("hp", 100),
            "dmg": u.get("dmg", 10),
            "range": u.get("range", 5),
            "attack_interval": u.get("attackInterval", 1.0),
            "speed": u.get("speed", 10),
            "vision": u.get("vision", 32),
            "ranged": bool(u.get("ranged", False)),
            "desc": u.get("desc", ""),
            "icon": icon_for(key),
            "mesh": f"models/units/{key}",
            "presentation": "glb_or_obj",
        }
        if u.get("ranged"):
            d["damage_type"] = "pierce"
        elif u.get("siege"):
            d["damage_type"] = "siege"
        elif u.get("cavalry") or u.get("monster"):
            d["damage_type"] = "crush"
        elif u.get("hero"):
            d["damage_type"] = "hero"
        else:
            d["damage_type"] = "slash"
        if u.get("hero"):
            d["armor_type"] = "hero"
        elif u.get("monster"):
            d["armor_type"] = "monster"
        elif u.get("cavalry"):
            d["armor_type"] = "cavalry"
        elif u.get("spear"):
            d["armor_type"] = "pike"
        elif u.get("siege"):
            d["armor_type"] = "siege"
        else:
            d["armor_type"] = "infantry_heavy" if key in ("soldier", "towerguard", "lorienwarrior") else "infantry_light"
        for flag in ("hero", "monster", "cavalry", "siege", "spear", "fly", "summon", "projectile"):
            if u.get(flag):
                d[flag] = u[flag]
        abs_keys = []
        for a in u.get("abilities") or []:
            if not isinstance(a, dict):
                abs_keys.append(str(a))
                continue
            ak = a.get("key") or a.get("id")
            if not ak:
                continue
            abs_keys.append(ak)
            ab = {
                "id": ak,
                "name": a.get("name", ak),
                "kind": ability_kind(a),
                "cooldown": a.get("cd", 30),
                "req_rank": 1,
                "damage": a.get("dmg") or a.get("targetDmg") or 0,
                "radius": a.get("radius", 12),
                "range": a.get("range", 8),
                "heal_frac": a.get("healFrac", 0),
                "duration": a.get("duration") or a.get("stun") or a.get("fear") or 0,
                "dmg_mul": a.get("dmgMul") or a.get("selfDmgMul") or 1.0,
                "knockback": a.get("knockback", 0),
                "knockdown": a.get("knockdown", 0),
                "hotkey": a.get("hotkey"),
                "icon": icon_for(str(ak)),
            }
            if a.get("summons"):
                ab["summons"] = a["summons"]
            # rank gates like mert: 2nd ability rank 2, 3rd rank 3+
            # leave 1 for now; match can enforce by index later
            write("abilities", ak, ab)
        if abs_keys:
            d["abilities"] = abs_keys
        write("units", key, d)

    for key, b in data["BUILDING_TYPES"].items():
        d = {
            "id": key,
            "name": b.get("name", key),
            "faction": b.get("faction", "neutral"),
            "cost": b.get("cost", 0),
            "build_time": b.get("buildTime", 10),
            "hp": b.get("hp", 1000),
            "radius": b.get("radius", 5),
            "vision": 55 if b.get("fortress") else (48 if b.get("tower") else 28),
            "desc": b.get("desc", ""),
            "icon": icon_for(key),
            "mesh": f"models/buildings/{key}",
            "presentation": "glb_or_obj",
        }
        for flag in ("fortress", "wall", "gate", "postern", "tower", "well"):
            if b.get(flag):
                d[flag] = True
        if b.get("income"):
            d["economy"] = True
            d["income"] = b["income"]
        trains = list(b.get("trains") or [])
        heroes = [t for t in trains if units.get(t, {}).get("hero")]
        non_heroes = [t for t in trains if t not in heroes]
        if non_heroes:
            d["trains"] = non_heroes
        if heroes:
            d["heroes"] = heroes
        if b.get("researches"):
            d["research"] = b["researches"]
        if b.get("auraRadius"):
            d["aura_radius"] = b["auraRadius"]
        if b.get("tower") or b.get("fortress"):
            d["auto_attack"] = True
            d["tower_range"] = 50 if b.get("fortress") else 48
            d["tower_damage"] = 12 if b.get("fortress") else 18
            d["tower_interval"] = 2.0 if b.get("fortress") else 2.5
        write("buildings", key, d)

    for key, f in data["FACTIONS"].items():
        plan = f.get("aiPlan") or {}
        army = []
        for m in plan.get("unitMix") or []:
            army.extend([m["key"]] * int(m.get("weight", 1)))
        d = {
            "id": key,
            "name": f.get("name", key),
            "alignment": f.get("alignment", "good"),
            "desc": f.get("desc", ""),
            "fortress": f.get("fortressKey"),
            "buildings": [f.get("fortressKey")] + list(f.get("buildKeys") or []),
            "wall_keys": f.get("wallKeys") or {},
            "hero_keys": f.get("heroKeys") or [],
            "starting_units": f.get("starterUnitKeys") or [],
            "reinforce_keys": f.get("reinforceKeys") or [],
            "powers": list((data.get("POWERS") or {}).keys()),
            "ai_plan": {
                "economy": [plan.get("econKey")] * 3 if plan.get("econKey") else [],
                "production": plan.get("trainBuildingKeys") or [],
                "army": army or ["soldier"],
                "hero": plan.get("lateHeroKey"),
                "monster": plan.get("monsterKey"),
                "wave_size": 4,
                "unit_mix": plan.get("unitMix") or [],
                "econ_key": plan.get("econKey"),
                "well_key": plan.get("wellKey"),
                "tower_key": plan.get("towerKey"),
                "tech_keys": plan.get("techKeys") or [],
                "fortress_tech_keys": plan.get("fortressTechKeys") or [],
                "pit_slots": plan.get("pitSlots") or [],
                "econ_slots": plan.get("econSlots") or [],
            },
        }
        write("factions", key, d)

    powers = data.get("POWERS") or {}
    for key, p in powers.items():
        kl = key.lower()
        d = {
            "id": key,
            "name": p.get("name", key),
            "tier": p.get("tier", 1),
            "cost_pp": p.get("cost", p.get("pp", 5)),
            "kind": "damage_area",
            "radius": p.get("radius", 18),
            "damage": p.get("damage") or p.get("dmg") or 0,
            "heal_frac": p.get("healFrac") or 0.35,
            "duration": p.get("duration", 0),
            "icon": icon_for(key),
            "target": p.get("target", "point"),
            "requires_tier_spent": max(0, int(p.get("tier", 1)) - 1),
        }
        if "heal" in kl:
            d["kind"] = "heal_area"
        elif "reveal" in kl or "farsight" in kl or "scout" in kl:
            d["kind"] = "reveal"
        elif "reinforce" in kl:
            d["kind"] = "summon"
            d["unit"] = "soldier"
            d["count"] = 2
        elif "sun" in kl or "flare" in kl:
            d["kind"] = "damage_area"
            d["damage"] = d["damage"] or 280
        elif "dark" in kl:
            d["kind"] = "weather"
            d["weather"] = "darkness"
            d["enemy_dmg_mul"] = 0.75
        elif "quake" in kl or "earth" in kl:
            d["kind"] = "quake"
            d["building_damage"] = d["damage"] or 400
        elif "dead" in kl or "army" in kl:
            d["kind"] = "summon"
            d["unit"] = "soldier"
            d["count"] = 3
            d["ghost"] = True
        elif "eagle" in kl:
            d["kind"] = "summon"
            d["unit"] = "eagle"
            d["count"] = 1
        write("powers", key, d)

    for key, u in (data.get("UPGRADES") or {}).items():
        kl = key.lower()
        d = {
            "id": key,
            "name": u.get("name", key),
            "cost": u.get("cost", 400),
            "time": u.get("time", 20),
            "icon": icon_for(key),
            "melee_dmg_mul": u.get("dmgMul", 1.2) if "forged" in kl or "blade" in kl else 1.0,
            "ranged_dmg_mul": u.get("dmgMul", 1.2) if "fire" in kl or "arrow" in kl else 1.0,
            "melee_armor_mul": u.get("armorMul", 0.85) if "heavy" in kl else 1.0,
            "ranged_armor_mul": u.get("armorMul", 0.85) if "ranged" in kl else 1.0,
            "building_hp_mul": u.get("hpMul", 1.25) if "mason" in kl else 1.0,
            "replenish_mul": 1.5 if "banner" in kl else 1.0,
        }
        write("research", key, d)

    maps = data.get("MAPS") or {}
    starts = {
        "anduin": ([-70, -70], [70, 70]),
        "misty": ([-80, -40], [80, 40]),
        "fangorn": ([-60, -80], [60, 80]),
        "brown": ([-65, 50], [65, -50]),
    }
    for key, m in maps.items():
        ps, es = starts.get(key, ([-70, -70], [70, 70]))
        write(
            "maps",
            key,
            {
                "id": key,
                "name": m.get("name", key),
                "half_size": 140,
                "seed": m.get("seed", 1337),
                "player_start": ps,
                "enemy_start": es,
                "trees": m.get("trees", 80),
            },
        )

    g = {
        "start_resources": data.get("START_RESOURCES", 1500),
        "max_battalions": data.get("MAX_BATTALIONS", 16),
        "map_half": data.get("MAP_HALF", 210),
        "player_base": data.get("PLAYER_BASE", {"x": -128, "z": -128}),
        "enemy_base": data.get("ENEMY_BASE", {"x": 128, "z": 128}),
        "pp_per_battalion": data.get("PP_PER_BATTALION", 2),
        "pp_per_building": data.get("PP_PER_BUILDING", 3),
        "farm_efficiency_radius": (data.get("FARM_EFFICIENCY") or {}).get("radius", 40),
        "farm_neighbor_mul": (data.get("FARM_EFFICIENCY") or {}).get("neighborMul", 0.75),
        "farm_efficiency_floor": (data.get("FARM_EFFICIENCY") or {}).get("floor", 0.4),
        "spear_vs_mounted": (data.get("COMBAT_MODIFIERS") or {}).get("spearVsMounted", 2.5),
        "siege_vs_building": (data.get("COMBAT_MODIFIERS") or {}).get("siegeVsBuilding", 2),
        "pierce_vs_monster": 0.65,
        "veterancy_thresholds": (data.get("VETERANCY") or {}).get("thresholds", [0, 300, 800, 1600, 2800]),
        "replenish_delay": (data.get("REPLENISHMENT") or {}).get("outOfCombat", 15),
        "replenish_frac": (data.get("REPLENISHMENT") or {}).get("passiveHealFrac", 0.02),
        "terror_units": (data.get("TERROR") or {}).get("units", ["nazgul", "mouthsauron", "shelob", "balrog"]),
        "terror_radius": (data.get("TERROR") or {}).get("radius", 20),
        "terror_duration": (data.get("TERROR") or {}).get("duration", 3),
        "terror_immune": (data.get("TERROR") or {}).get("immune", 30),
        "cower_damage_mul": (data.get("TERROR") or {}).get("cowerDamageMul", 0.75),
        "cower_speed_mul": (data.get("TERROR") or {}).get("cowerSpeedMul", 0.85),
        "flee_speed_mul": (data.get("TERROR") or {}).get("fleeSpeedMul", 2.25),
        "monster_knockback": (data.get("MONSTER_KNOCKBACK") or {}).get("min", 3.5),
        "monster_knockdown": (data.get("MONSTER_KNOCKBACK") or {}).get("knockdown", 1.1),
        "trample_damage": (data.get("TRAMPLE") or {}).get("damagePerCavalry", 8),
        "trample_rehit": (data.get("TRAMPLE") or {}).get("rehitDelay", 2.5),
        "hero_revive_base_mul": (data.get("HERO_REVIVAL") or {}).get("baseCostMul", 0.6),
        "hero_revive_death_mul": (data.get("HERO_REVIVAL") or {}).get("deathCostMul", 0.2),
        "difficulties": {
            "easy": {"income_mul": 0.75, "dmg_mul": 0.8, "armor_mul": 1.15, "wave_mul": 0.7},
            "normal": {"income_mul": 1.0, "dmg_mul": 1.0, "armor_mul": 1.0, "wave_mul": 1.0},
            "hard": {"income_mul": 1.25, "dmg_mul": 1.15, "armor_mul": 0.9, "wave_mul": 1.35},
        },
        "ai_rules": data.get("AI_RULES") or {},
        "towers": data.get("TOWERS") or {},
        "garrison": data.get("GARRISON") or {},
    }
    (BASE / "globals.json").write_text(json.dumps(g, indent=2) + "\n", encoding="utf-8")
    (BASE / "pack.json").write_text(
        json.dumps(
            {
                "id": "base",
                "name": "Open BFME (ported from middle-earth-rts)",
                "version": "1.1.0",
                "priority": 0,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(
        "ported",
        "units",
        len(list((BASE / "units").glob("*.json"))),
        "buildings",
        len(list((BASE / "buildings").glob("*.json"))),
        "abilities",
        len(list((BASE / "abilities").glob("*.json"))),
        "powers",
        len(list((BASE / "powers").glob("*.json"))),
        "factions",
        len(list((BASE / "factions").glob("*.json"))),
    )


if __name__ == "__main__":
    main()
