#!/usr/bin/env python3
"""Generate base content pack JSON for Open BFME stages 5–9."""
from __future__ import annotations
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
ROOT = REPO_ROOT / "game" / "data" / "base"

def w(rel: str, data: dict) -> None:
    path = ROOT / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

def unit(id, name, faction, **kw):
    d = {
        "id": id, "name": name, "faction": faction,
        "cost": kw.get("cost", 200), "build_time": kw.get("build_time", 8),
        "size": kw.get("size", 5), "hp": kw.get("hp", 500), "dmg": kw.get("dmg", 12),
        "range": kw.get("range", 5.5), "attack_interval": kw.get("attack_interval", 1.0),
        "speed": kw.get("speed", 10), "vision": kw.get("vision", 32),
        "ranged": kw.get("ranged", False),
        "damage_type": kw.get("damage_type", "pierce" if kw.get("ranged") else "slash"),
        "armor_type": kw.get("armor_type", "infantry_light"),
        "icon": f"icons/{kw.get('icon', id)}.png",
        "mesh": kw.get("mesh", f"models/units/{id}"),
        "presentation": kw.get("presentation", "glb_or_obj"),
        "desc": kw.get("desc", name),
    }
    for k in ("hero", "monster", "cavalry", "siege", "spear", "abilities", "summon"):
        if k in kw:
            d[k] = kw[k]
    return d

def building(id, name, faction, **kw):
    d = {
        "id": id, "name": name, "faction": faction,
        "cost": kw.get("cost", 300), "build_time": kw.get("build_time", 12),
        "hp": kw.get("hp", 1500), "radius": kw.get("radius", 5.0),
        "vision": kw.get("vision", 32),
        "icon": f"icons/{kw.get('icon', id)}.png",
        "mesh": kw.get("mesh", f"models/buildings/{id}"),
        "presentation": "glb_or_obj",
    }
    for k in ("economy", "income", "fortress", "wall", "gate", "tower",
              "trains", "heroes", "research", "auto_attack", "tower_range",
              "tower_damage", "tower_interval"):
        if k in kw:
            d[k] = kw[k]
    return d

UNITS = [
    unit("soldier", "Gondor Soldiers", "gondor", cost=200, hp=550, dmg=13, size=5, armor_type="infantry_heavy"),
    unit("archer", "Gondor Archers", "gondor", cost=300, hp=380, dmg=9, range=40, attack_interval=1.7, ranged=True, size=5),
    unit("towerguard", "Tower Guards", "gondor", cost=350, hp=720, dmg=11, spear=True, armor_type="pike", size=5),
    unit("knight", "Knights of Gondor", "gondor", cost=450, hp=750, dmg=20, speed=17, cavalry=True, size=3, armor_type="cavalry", damage_type="crush"),
    unit("trebuchet", "Trebuchet", "gondor", cost=700, hp=650, dmg=55, range=62, attack_interval=4.5, ranged=True, siege=True, size=1, armor_type="siege", damage_type="siege"),
    unit("aragorn", "Aragorn", "gondor", cost=800, hp=1500, dmg=70, size=1, hero=True, armor_type="hero", damage_type="hero",
         abilities=["blademaster", "athelas", "anduril"]),
    unit("gandalf", "Gandalf the White", "gondor", cost=1200, hp=1300, dmg=50, range=30, ranged=True, size=1, hero=True,
         armor_type="hero", damage_type="magic", abilities=["wordofpower", "istari_light", "wizardblast"]),
    unit("boromir", "Boromir", "gondor", cost=600, hp=1300, dmg=55, size=1, hero=True, armor_type="hero", damage_type="hero",
         abilities=["horn", "captain"]),
    unit("orc", "Orc Warriors", "mordor", cost=150, hp=480, dmg=10, size=6),
    unit("orcarcher", "Orc Archers", "mordor", cost=200, hp=350, dmg=7, range=36, ranged=True, size=5),
    unit("warg", "Warg Riders", "mordor", cost=350, hp=650, dmg=17, speed=17, cavalry=True, size=3, armor_type="cavalry", damage_type="crush"),
    unit("troll", "Mountain Troll", "mordor", cost=600, hp=1900, dmg=95, range=7.5, attack_interval=1.9, speed=8, size=1, monster=True, armor_type="monster", damage_type="crush"),
    unit("nazgul", "Black Rider", "mordor", cost=800, hp=1700, dmg=80, speed=16, cavalry=True, size=1, hero=True, armor_type="hero", damage_type="hero",
         abilities=["screech", "morgulblade", "dread_buff"]),
    unit("mouthsauron", "Mouth of Sauron", "mordor", cost=1000, hp=1700, dmg=60, size=1, hero=True, armor_type="hero",
         abilities=["screech", "dread_buff"]),
    unit("lorienwarrior", "Lorien Warriors", "elves", cost=300, hp=600, dmg=15, size=5, armor_type="infantry_heavy"),
    unit("lorienarcher", "Lorien Archers", "elves", cost=350, hp=420, dmg=11, range=44, ranged=True, size=5),
    unit("mirkwood", "Mirkwood Sentinels", "elves", cost=500, hp=450, dmg=19, range=50, ranged=True, size=3),
    unit("ent", "Ent", "elves", cost=800, hp=2400, dmg=110, range=8, attack_interval=2.2, speed=5.2, size=1, monster=True, siege=True, armor_type="monster", damage_type="siege"),
    unit("legolas", "Legolas", "elves", cost=1000, hp=1100, dmg=32, range=50, ranged=True, size=1, hero=True, armor_type="hero", damage_type="pierce",
         abilities=["hawkstrike", "arrowwind"]),
    unit("elrond", "Elrond", "elves", cost=1100, hp=1400, dmg=60, size=1, hero=True, armor_type="hero", damage_type="magic",
         abilities=["athelas", "whirlwind"]),
    unit("eagle", "Great Eagle", "elves", cost=0, hp=900, dmg=120, speed=20, size=1, summon=True, armor_type="monster"),
    unit("goblinwarrior", "Goblin Warriors", "goblins", cost=120, hp=330, dmg=9, speed=12, size=6),
    unit("goblinarcher", "Goblin Archers", "goblins", cost=160, hp=280, dmg=7, range=34, ranged=True, size=5),
    unit("spiderrider", "Spider Riders", "goblins", cost=400, hp=600, dmg=20, speed=18, cavalry=True, size=3, armor_type="cavalry", damage_type="crush"),
    unit("cavetroll", "Cave Troll", "goblins", cost=600, hp=2100, dmg=100, size=1, monster=True, armor_type="monster", damage_type="crush"),
    unit("goblinking", "Goblin King", "goblins", cost=900, hp=1500, dmg=65, size=1, hero=True, armor_type="hero",
         abilities=["screech", "blademaster"]),
    unit("shelob", "Shelob", "goblins", cost=1200, hp=2200, dmg=90, size=1, hero=True, monster=True, armor_type="monster",
         abilities=["screech", "morgulblade"]),
]

BUILDINGS = [
    building("g_fortress", "Gondor Fortress", "gondor", cost=0, hp=6000, radius=10, fortress=True, vision=55,
             auto_attack=True, tower_range=50, tower_damage=12, tower_interval=2.0,
             heroes=["aragorn", "gandalf", "boromir"], research=["masonry", "banners"],
             mesh="models/buildings/g_fortress", icon="gfortress"),
    building("m_fortress", "Dark Citadel", "mordor", cost=0, hp=6000, radius=10, fortress=True, vision=55,
             auto_attack=True, tower_range=50, tower_damage=12, tower_interval=2.0,
             heroes=["nazgul", "mouthsauron"], research=["masonry", "banners"],
             mesh="models/buildings/m_fortress", icon="mfortress"),
    building("e_fortress", "Mallorn Fortress", "elves", cost=0, hp=5800, radius=10, fortress=True, vision=55,
             auto_attack=True, tower_range=50, tower_damage=11, tower_interval=2.0,
             heroes=["legolas", "elrond"], research=["masonry", "banners"],
             mesh="models/buildings/mallorntree", icon="efortress"),
    building("gob_fortress", "Goblin Cave", "goblins", cost=0, hp=5600, radius=10, fortress=True, vision=55,
             auto_attack=True, tower_range=48, tower_damage=11, tower_interval=2.0,
             heroes=["goblinking", "shelob"], research=["masonry", "banners"],
             mesh="models/buildings/goblinden", icon="fortress"),
    building("farm", "Farm", "gondor", cost=300, hp=1200, economy=True, income=10, mesh="models/buildings/farm", icon="farm"),
    building("barracks", "Barracks", "gondor", cost=400, hp=2000, radius=6,
             trains=["soldier", "towerguard"], research=["forged_blades", "heavy_armor"],
             mesh="models/buildings/barracks", icon="barracks"),
    building("archery", "Archery Range", "gondor", cost=450, hp=1800, radius=6,
             trains=["archer"], research=["fire_arrows", "ranged_armor"],
             mesh="models/buildings/archery", icon="archery"),
    building("stables", "Stables", "gondor", cost=500, hp=1800, radius=6, trains=["knight"],
             mesh="models/buildings/stables", icon="stables"),
    building("workshop", "Siege Works", "gondor", cost=600, hp=1600, radius=6, trains=["trebuchet"],
             mesh="models/buildings/workshop", icon="workshop"),
    building("lumbermill", "Lumber Mill", "mordor", cost=300, hp=1200, economy=True, income=10,
             mesh="models/buildings/lumbermill", icon="lumbermill"),
    building("orcpit", "Orc Pit", "mordor", cost=350, hp=1800, radius=6,
             trains=["orc", "orcarcher"], research=["forged_blades", "heavy_armor"],
             mesh="models/buildings/orcpit", icon="orcpit"),
    building("wargpen", "Warg Pen", "mordor", cost=450, hp=1700, radius=6, trains=["warg"],
             mesh="models/buildings/wargpen", icon="wargpen"),
    building("trollcage", "Troll Cage", "mordor", cost=550, hp=2000, radius=7, trains=["troll"],
             mesh="models/buildings/trollcage", icon="trollcage"),
    building("mallorntree", "Mallorn Tree", "elves", cost=300, hp=1300, economy=True, income=11,
             mesh="models/buildings/mallorntree", icon="mallorntree"),
    building("elvenbarracks", "Elven Barracks", "elves", cost=450, hp=2000, radius=6,
             trains=["lorienwarrior", "lorienarcher", "mirkwood"], research=["forged_blades", "fire_arrows"],
             mesh="models/buildings/elvenbarracks", icon="elvenbarracks"),
    building("sacredgrove", "Sacred Grove", "elves", cost=550, hp=1800, radius=6, trains=["ent"],
             mesh="models/buildings/sacredgrove", icon="sacredgrove"),
    building("mineshaft", "Mine Shaft", "goblins", cost=280, hp=1100, economy=True, income=11,
             mesh="models/buildings/mineshaft", icon="mineshaft"),
    building("goblinden", "Goblin Den", "goblins", cost=320, hp=1600, radius=6,
             trains=["goblinwarrior", "goblinarcher"], research=["forged_blades"],
             mesh="models/buildings/goblinden", icon="goblinden"),
    building("spiderpit", "Spider Pit", "goblins", cost=450, hp=1700, radius=6, trains=["spiderrider"],
             mesh="models/buildings/spiderpit", icon="spiderpit"),
    building("gob_trollcage", "Cave Troll Cage", "goblins", cost=550, hp=2000, radius=7, trains=["cavetroll"],
             mesh="models/buildings/trollcage", icon="trollcage"),
    building("tower", "Arrow Tower", "neutral", cost=350, hp=1600, radius=3.5, tower=True,
             tower_range=48, tower_damage=18, tower_interval=2.5, vision=48,
             mesh="models/buildings/tower", icon="tower"),
    building("wall", "Wall", "neutral", cost=50, hp=900, radius=2.2, wall=True, vision=12,
             mesh="models/buildings/wall", icon="wall"),
    building("gate", "Gate", "neutral", cost=150, hp=1400, radius=3.0, gate=True, wall=True, vision=16,
             mesh="models/buildings/gate", icon="gate"),
]

POWERS = [
    {"id": "heal", "name": "Heal", "tier": 1, "cost_pp": 5, "kind": "heal_area", "radius": 18, "heal_frac": 0.35, "icon": "icons/heal.png", "target": "point"},
    {"id": "reveal", "name": "Farsight", "tier": 1, "cost_pp": 4, "kind": "reveal", "radius": 40, "duration": 20, "icon": "icons/farsight.png", "target": "point"},
    {"id": "reinforce", "name": "Reinforcements", "tier": 2, "cost_pp": 10, "kind": "summon", "unit": "soldier", "count": 2, "lifetime": 45, "icon": "icons/reinforce.png", "target": "point", "requires_tier_spent": 1},
    {"id": "sunflare", "name": "Sunflare", "tier": 2, "cost_pp": 12, "kind": "damage_area", "radius": 16, "damage": 280, "icon": "icons/sunflare.png", "target": "point", "requires_tier_spent": 1},
    {"id": "darkness", "name": "Darkness", "tier": 2, "cost_pp": 10, "kind": "weather", "weather": "darkness", "duration": 30, "enemy_dmg_mul": 0.75, "icon": "icons/despair.png", "target": "none", "requires_tier_spent": 1},
    {"id": "earthquake", "name": "Earthquake", "tier": 3, "cost_pp": 18, "kind": "quake", "radius": 28, "building_damage": 400, "icon": "icons/cataclysm.png", "target": "point", "requires_tier_spent": 2},
    {"id": "army_dead", "name": "Army of the Dead", "tier": 3, "cost_pp": 22, "kind": "summon", "unit": "soldier", "count": 3, "lifetime": 40, "ghost": True, "icon": "icons/armyofdead.png", "target": "point", "requires_tier_spent": 2},
    {"id": "eagles", "name": "Call the Eagles", "tier": 3, "cost_pp": 20, "kind": "summon", "unit": "eagle", "count": 1, "lifetime": 30, "icon": "icons/calleagles.png", "target": "point", "requires_tier_spent": 2},
]

RESEARCH = [
    {"id": "forged_blades", "name": "Forged Blades", "cost": 400, "time": 20, "melee_dmg_mul": 1.2, "icon": "icons/forgedblades.png"},
    {"id": "heavy_armor", "name": "Heavy Armor", "cost": 400, "time": 20, "melee_armor_mul": 0.85, "icon": "icons/heavyarmor.png"},
    {"id": "fire_arrows", "name": "Fire Arrows", "cost": 450, "time": 22, "ranged_dmg_mul": 1.2, "icon": "icons/firearrows.png"},
    {"id": "ranged_armor", "name": "Ranged Armor", "cost": 400, "time": 20, "ranged_armor_mul": 0.85, "icon": "icons/rangerarmor.png"},
    {"id": "masonry", "name": "Masonry", "cost": 500, "time": 25, "building_hp_mul": 1.25, "icon": "icons/bulwark.png"},
    {"id": "banners", "name": "Banners", "cost": 350, "time": 18, "replenish_mul": 1.5, "icon": "icons/rally.png"},
]

ABILITIES = [
    {"id": "blademaster", "name": "Blademaster", "kind": "nova_damage", "cooldown": 30, "req_rank": 1, "damage": 320, "radius": 15, "hotkey": "F", "icon": "icons/blademaster.png"},
    {"id": "athelas", "name": "Athelas", "kind": "heal_nova", "cooldown": 45, "req_rank": 2, "heal_frac": 0.45, "radius": 24, "hotkey": "G", "icon": "icons/athelas.png"},
    {"id": "anduril", "name": "Anduril", "kind": "single_target", "cooldown": 25, "req_rank": 3, "damage": 500, "range": 8, "hotkey": "B", "icon": "icons/anduril.png"},
    {"id": "screech", "name": "Screech", "kind": "fear_nova", "cooldown": 35, "req_rank": 1, "radius": 18, "duration": 4, "hotkey": "F", "icon": "icons/screech.png"},
    {"id": "morgulblade", "name": "Morgul Blade", "kind": "single_target", "cooldown": 22, "req_rank": 2, "damage": 280, "range": 8, "hotkey": "G", "icon": "icons/morgulblade.png"},
    {"id": "dread_buff", "name": "Dread", "kind": "buff_self", "cooldown": 40, "req_rank": 3, "duration": 10, "dmg_mul": 1.35, "hotkey": "V", "icon": "icons/dread.png"},
    {"id": "wordofpower", "name": "Word of Power", "kind": "nova_damage", "cooldown": 45, "req_rank": 1, "damage": 500, "radius": 24, "hotkey": "F", "icon": "icons/wordofpower.png"},
    {"id": "istari_light", "name": "Istari Light", "kind": "single_target", "cooldown": 18, "req_rank": 2, "damage": 250, "range": 40, "hotkey": "G", "icon": "icons/istarilight.png"},
    {"id": "wizardblast", "name": "Wizard Blast", "kind": "nova_damage", "cooldown": 12, "req_rank": 3, "damage": 80, "radius": 10, "knockback": 7, "hotkey": "B", "icon": "icons/wizardblast.png"},
    {"id": "horn", "name": "Horn of Gondor", "kind": "fear_nova", "cooldown": 40, "req_rank": 1, "radius": 22, "duration": 3.5, "hotkey": "F", "icon": "icons/horn.png"},
    {"id": "captain", "name": "Captain of Gondor", "kind": "buff_self", "cooldown": 50, "req_rank": 2, "duration": 12, "dmg_mul": 1.4, "hotkey": "G", "icon": "icons/captain.png"},
    {"id": "hawkstrike", "name": "Hawk Strike", "kind": "single_target", "cooldown": 20, "req_rank": 1, "damage": 400, "range": 45, "hotkey": "F", "icon": "icons/hawkstrike.png"},
    {"id": "arrowwind", "name": "Arrow Wind", "kind": "nova_damage", "cooldown": 35, "req_rank": 2, "damage": 180, "radius": 14, "hotkey": "G", "icon": "icons/arrowwind.png"},
    {"id": "whirlwind", "name": "Whirlwind", "kind": "nova_damage", "cooldown": 50, "req_rank": 2, "damage": 120, "radius": 20, "hotkey": "G", "icon": "icons/whirlwind.png"},
]

FACTIONS = {
    "gondor": {
        "id": "gondor", "name": "Gondor", "color": "#4a7ab5", "accent": "#c9a227",
        "fortress": "g_fortress",
        "buildings": ["g_fortress", "farm", "barracks", "archery", "stables", "workshop", "tower", "wall", "gate"],
        "starting_units": ["soldier", "soldier", "archer"],
        "powers": ["heal", "reveal", "reinforce", "sunflare", "earthquake", "army_dead", "eagles"],
        "ai_plan": {
            "economy": ["farm", "farm", "farm"],
            "production": ["barracks", "archery", "stables"],
            "army": ["soldier", "soldier", "archer", "knight", "towerguard"],
            "hero": "aragorn",
            "wave_size": 4,
        },
    },
    "mordor": {
        "id": "mordor", "name": "Mordor", "color": "#8b2a2a", "accent": "#3a1a12",
        "fortress": "m_fortress",
        "buildings": ["m_fortress", "lumbermill", "orcpit", "wargpen", "trollcage", "tower", "wall", "gate"],
        "starting_units": ["orc", "orc", "orcarcher"],
        "powers": ["heal", "reveal", "darkness", "sunflare", "earthquake", "army_dead"],
        "ai_plan": {
            "economy": ["lumbermill", "lumbermill", "lumbermill"],
            "production": ["orcpit", "wargpen", "trollcage"],
            "army": ["orc", "orc", "orcarcher", "warg", "troll"],
            "hero": "nazgul",
            "wave_size": 5,
        },
    },
    "elves": {
        "id": "elves", "name": "Elves", "color": "#2d6b4f", "accent": "#d4af37",
        "fortress": "e_fortress",
        "buildings": ["e_fortress", "mallorntree", "elvenbarracks", "sacredgrove", "tower", "wall", "gate"],
        "starting_units": ["lorienwarrior", "lorienarcher"],
        "powers": ["heal", "reveal", "reinforce", "sunflare", "earthquake", "eagles"],
        "ai_plan": {
            "economy": ["mallorntree", "mallorntree"],
            "production": ["elvenbarracks", "sacredgrove"],
            "army": ["lorienwarrior", "lorienarcher", "mirkwood", "ent"],
            "hero": "legolas",
            "wave_size": 4,
        },
    },
    "goblins": {
        "id": "goblins", "name": "Goblins", "color": "#5a6b3a", "accent": "#c9b896",
        "fortress": "gob_fortress",
        "buildings": ["gob_fortress", "mineshaft", "goblinden", "spiderpit", "gob_trollcage", "tower", "wall", "gate"],
        "starting_units": ["goblinwarrior", "goblinwarrior", "goblinarcher"],
        "powers": ["heal", "reveal", "darkness", "sunflare", "earthquake", "army_dead"],
        "ai_plan": {
            "economy": ["mineshaft", "mineshaft", "mineshaft"],
            "production": ["goblinden", "spiderpit", "gob_trollcage"],
            "army": ["goblinwarrior", "goblinarcher", "spiderrider", "cavetroll"],
            "hero": "goblinking",
            "wave_size": 6,
        },
    },
}

MAPS = [
    {"id": "anduin_sandbox", "name": "Anduin Vale", "half_size": 120, "player_start": [-70, -70], "enemy_start": [70, 70], "seed": 1337},
    {"id": "misty_foothills", "name": "Misty Foothills", "half_size": 120, "player_start": [-80, -40], "enemy_start": [80, 40], "seed": 27182},
    {"id": "fangorn_edge", "name": "Fangorn Edge", "half_size": 120, "player_start": [-60, -80], "enemy_start": [60, 80], "seed": 31415},
    {"id": "brown_lands", "name": "Brown Lands", "half_size": 110, "player_start": [-65, 50], "enemy_start": [65, -50], "seed": 42424},
]

DAMAGE_MATRIX = {
    "slash": {"infantry_light": 1.0, "infantry_heavy": 0.85, "pike": 0.9, "cavalry": 1.0, "monster": 0.75, "hero": 0.9, "building": 0.35, "wall": 0.25, "siege": 0.6},
    "pierce": {"infantry_light": 1.15, "infantry_heavy": 0.9, "pike": 0.85, "cavalry": 0.7, "monster": 0.55, "hero": 0.85, "building": 0.25, "wall": 0.15, "siege": 0.5},
    "crush": {"infantry_light": 1.4, "infantry_heavy": 1.15, "pike": 0.55, "cavalry": 0.9, "monster": 0.8, "hero": 0.95, "building": 0.5, "wall": 0.35, "siege": 0.7},
    "siege": {"infantry_light": 0.5, "infantry_heavy": 0.5, "pike": 0.5, "cavalry": 0.5, "monster": 0.6, "hero": 0.4, "building": 2.0, "wall": 2.2, "siege": 1.0},
    "magic": {"infantry_light": 1.1, "infantry_heavy": 1.05, "pike": 1.05, "cavalry": 1.0, "monster": 1.15, "hero": 1.0, "building": 0.8, "wall": 0.6, "siege": 0.9},
    "hero": {"infantry_light": 1.2, "infantry_heavy": 1.15, "pike": 1.1, "cavalry": 1.1, "monster": 1.2, "hero": 1.0, "building": 0.7, "wall": 0.5, "siege": 0.8},
}

def main():
    # clear regenerated unit/building dirs carefully - rewrite files
    for u in UNITS:
        w(f"units/{u['id']}.json", u)
    for b in BUILDINGS:
        # fix icon paths that used bare names
        if not str(b["icon"]).startswith("icons/"):
            b["icon"] = f"icons/{b['icon']}.png" if not b["icon"].endswith(".png") else f"icons/{b['icon']}"
        # normalize icon if we passed short form
        ic = b.get("icon", "")
        if ic and not ic.startswith("icons/"):
            b["icon"] = f"icons/{ic}.png" if "." not in ic else f"icons/{ic}"
        w(f"buildings/{b['id']}.json", b)
    for a in ABILITIES:
        w(f"abilities/{a['id']}.json", a)
    for p in POWERS:
        w(f"powers/{p['id']}.json", p)
    for r in RESEARCH:
        w(f"research/{r['id']}.json", r)
    for fid, f in FACTIONS.items():
        w(f"factions/{fid}.json", f)
    for m in MAPS:
        w(f"maps/{m['id']}.json", m)
    w("damage_matrix.json", DAMAGE_MATRIX)
    w("globals.json", {
        "start_resources": 1500,
        "max_battalions": 24,
        "farm_efficiency_radius": 40,
        "farm_neighbor_mul": 0.75,
        "farm_efficiency_floor": 0.4,
        "spear_vs_mounted": 2.5,
        "siege_vs_building": 2.0,
        "pierce_vs_monster": 0.65,
        "pp_per_battalion": 8,
        "pp_per_building": 20,
        "veterancy_thresholds": [0, 300, 800, 1600, 2800],
        "replenish_delay": 15,
        "replenish_frac": 0.02,
        "terror_units": ["nazgul", "mouthsauron", "shelob"],
        "terror_radius": 20,
        "terror_duration": 3,
        "terror_immune": 30,
        "cower_damage_mul": 0.75,
        "cower_speed_mul": 0.85,
        "flee_speed_mul": 2.25,
        "monster_knockback": 3.5,
        "monster_knockdown": 1.0,
        "hero_revive_base_mul": 0.6,
        "hero_revive_death_mul": 0.2,
        "power_tier_requires": { "1": 0, "2": 1, "3": 2 },
        "difficulties": {
            "easy": {"income_mul": 0.75, "dmg_mul": 0.8, "armor_mul": 1.15, "wave_mul": 0.7},
            "normal": {"income_mul": 1.0, "dmg_mul": 1.0, "armor_mul": 1.0, "wave_mul": 1.0},
            "hard": {"income_mul": 1.25, "dmg_mul": 1.15, "armor_mul": 0.9, "wave_mul": 1.35},
        },
    })
    w("pack.json", {"id": "base", "name": "Open BFME Base", "version": "1.0.0", "priority": 0, "author": "Open BFME"})
    print(f"units={len(UNITS)} buildings={len(BUILDINGS)} powers={len(POWERS)} research={len(RESEARCH)} factions={len(FACTIONS)} maps={len(MAPS)}")

if __name__ == "__main__":
    main()
