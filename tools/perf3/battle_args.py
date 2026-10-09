#!/usr/bin/env python3
"""Lane PERF-3: the hud_viewer arguments of the 4-player mass battle (scenario perf3_battle).

Four armies of hordes, Men + Elves (team 0, south) against Mordor + Isengard (team 1, north), each a grid of hordes around the battle
centre (an offset from the map focus); about 3000 objects in all with the defaults (42 hordes per army, about 18 objects each).

    python3 tools/perf3/battle_args.py [--hordes 42] [--centre 800,0] > args.txt
    godot --path godot res://scenes/hud_viewer.tscn -- $(cat args.txt) --scenario=perf3_battle
"""
import argparse

ARMIES = {
    1: ["GondorFighterHorde", "GondorArcherHorde", "GondorTowerShieldGuardHorde", "GondorFighterHorde"],
    2: ["MordorFighterHorde", "MordorArcherHorde", "MordorFighterHorde"],
    3: ["ElvenLorienWarriorHorde", "ElvenLorienArcherHorde", "ElvenLorienWarriorHorde"],
    4: ["IsengardFighterHorde", "IsengardUrukCrossbowHorde", "IsengardPikemanHorde"],
}
# block centre of each army relative to the battle centre: team 0 south (negative y), team 1 north
PLACES = {1: (-450, -500), 3: (450, -500), 2: (-450, 500), 4: (450, 500)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hordes", type=int, default=42, help="hordes per army")
    ap.add_argument("--centre", default="800,0", help="battle centre relative to the map focus (Player_1's objects)")
    ap.add_argument("--cols", type=int, default=7)
    ap.add_argument("--dx", type=float, default=110.0)
    ap.add_argument("--dy", type=float, default=90.0)
    a = ap.parse_args()
    cx, cy = (float(v) for v in a.centre.split(","))
    out = []
    for slot in (1, 2, 3, 4):
        px, py = PLACES[slot]
        rows = (a.hordes + a.cols - 1) // a.cols
        for i in range(a.hordes):
            r, c = divmod(i, a.cols)
            x = cx + px + (c - (a.cols - 1) / 2) * a.dx
            # the first row faces the enemy
            y = cy + py + (r - (rows - 1) / 2) * a.dy * (1 if py < 0 else -1) * -1
            out.append("--army=%d:%s:%.0f,%.0f" % (slot, ARMIES[slot][i % len(ARMIES[slot])], x, y))
    print(" ".join(out))


if __name__ == "__main__":
    main()
