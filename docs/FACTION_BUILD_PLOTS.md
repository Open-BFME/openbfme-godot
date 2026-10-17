# Faction build plots

OpenBFME adds five standalone plot templates for every mounted playable faction
with a builder menu. In the standard RotWK installation this covers Men, Elves,
Dwarves, Isengard, Mordor, Goblins, and Angmar. Templates are generated at object
world load, after the original templates and commands have loaded; they do not
replace starting fortresses or existing expansion pads.

| Suffix | Buildings offered |
| --- | --- |
| `General` | All structures in that faction's builder menu except wall hubs and fortresses |
| `Siege` | Buildings training siege engines or monsters, including trebuchets, ents, trolls, and similar units |
| `Military` | Buildings training ordinary units, such as barracks, archery ranges, and stables |
| `Economy` | Economy structures and buildings with terrain-resource or auto-deposit behavior |
| `Upgrades` | Buildings offering player research without ordinary-unit production, such as blacksmiths |

Roles can overlap: a siege workshop that also researches equipment can appear in
both Siege and Upgrades. Upgraded command sets are inspected as well as the initial
set, so higher-level siege production counts. A building's own level-up button does
not make it an upgrade building. Source builder commands supply icons, descriptions,
upgrade requirements, and other availability settings; normal costs, construction
timing, prerequisites, and Lua timing callbacks still apply. General includes
defensive/support structures that do not fit a specialist role.

Selecting an owned empty plot opens its native building menu. Clicking a building
starts paid construction on the plot immediately, without a builder unit. One
building occupies a plot at a time. The plot hides during occupation and becomes
selectable again after cancellation or destruction of the building.

## Placing plots in scenarios

Names use `OpenBFME_BuildPlot_<PlayerTemplate>_<Suffix>`, for example:

```text
OpenBFME_BuildPlot_FactionMen_General
OpenBFME_BuildPlot_FactionElves_Siege
OpenBFME_BuildPlot_FactionDwarves_Military
OpenBFME_BuildPlot_FactionMordor_Economy
OpenBFME_BuildPlot_FactionIsengard_Upgrades
```

Use the existing scenario object creation API to place them for a player:

```gdscript
var plot_id = world.create_object(
    'OpenBFME_BuildPlot_FactionMen_General', player_index, map_x, map_y, 0.0)
assert(plot_id > 0)
```

They are also discoverable as normal templates in the Lua console:

```lua
for name, template in pairs(bfme.buildingTypes) do
    if name:match('^OpenBFME_BuildPlot_') then
        print(name, template.faction)
    end
end
```

The plot uses its faction's native foundation art. An unavailable foundation, a
reserved-name collision, an unresolved construction command, or a menu larger than
33 buttons is reported as a load error. Factions without any builder construction
commands have no variants; a specialist category with no matching buildings has an
empty menu. The authored category labels can be translated by supplying the STR/CSF
keys `OPENBFME:BuildPlotGeneral`, `BuildPlotSiege`, `BuildPlotMilitary`,
`BuildPlotEconomy`, and `BuildPlotUpgrades` (all with the `OPENBFME:` prefix).

This is enhanced OpenBFME content; BFME1 installs retain their native plot system.
New plot templates are appended, leaving existing retail template IDs intact.
Map-specific replacements of a builder menu after world load do not regenerate
these menus. Installed mods loaded with the world do participate in generation.

Focused synthetic validation:

```sh
cmake --build engine/build --target openbfme_faction_plot_tests
engine/build/openbfme_faction_plot_tests
```
