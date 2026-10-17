# Build-mode filter

`bfme.buildMode` is a read-only classifier for build palettes, available through
`bfme.buildMode`, `require('bfme.buildMode')`, or `import('bfme.buildMode')`.
It examines mounted templates and the construction buttons referenced by their
actual command sets, including `CommandSetUpgrade` alternatives. An orphan button
in CommandButton.ini does not prove a building is available.

```lua
-- Default palette: ordinary buildings offered by a builder or native plot.
local buildings = bfme.buildMode.list()

-- Every building a builder offers, including forts and wall hubs.
local builderBuildings = bfme.buildMode.list({building=true, builderBuildable=true})
local plotBuildings = bfme.buildMode.list({building=true, plotBuildable=true})
local units = bfme.buildMode.list({unit=true, marker=false, placeholder=false})
local markers = bfme.buildMode.list({marker=true})
local plots = bfme.buildMode.list({plot=true})

-- Explicitly exclude any known engine implementation gaps as well.
local supported = bfme.buildMode.list({recommended=true, unsupported=false})

local selected = bfme.debug.getSelectedBuilding()
if selected then
    local classification = bfme.buildMode.classify(selected)
    print(classification.underConstruction, classification.unfinishedContent)
    for _, warning in ipairs(classification.warnings) do print(warning) end
end
```

`list(options)` returns template handles, sorted by template name.
`classify(templateNameOrHandle)` returns a detached classification table; it also
accepts a live unit/building handle. `matches(value, options)` tests an individual
value. Options are ANDed boolean fields from the table below. Omitting options
means `{recommended=true}`; passing `{}` requests everything, including props and
internal templates. Unknown option names and nonboolean values are errors.
Template handles also expose the native metadata as `template.buildMode`.

| Flag | Meaning |
| --- | --- |
| `building` | KindOf STRUCTURE |
| `unit` | Unit kinds such as infantry, cavalry, monsters, hordes, builders, ships, aircraft; structures are excluded |
| `plot` | Foundation/expansion-pad kinds or FoundationAIUpdate |
| `builderBuildable` | An actual DOZER/SWARM_DOZER menu offers it with DOZER_CONSTRUCT |
| `plotBuildable` | A native plot menu offers it with FOUNDATION_CONSTRUCT |
| `enhancedPlotBuildable` | One of the generated OpenBFME faction plots offers it |
| `marker` | TACTICAL_MARKER, BASE_SITE, or INERT_SHROUD_REVEALER |
| `placeholder` | Helper/rebuild kinds: REBUILD_HOLE, SPELL_BOOK, or OCL_BIT |
| `scenery` | Tree, shrub, shrubbery, or environment kinds |
| `unfinishedContent` | Explicitly marked DEPRECATED or OBSOLETE |
| `unsupported` | At least one behavior module has no engine implementation registered |
| `underConstruction` | A live building is currently being constructed; false for template queries |
| `unfinished` | Union of unfinishedContent, unsupported, and underConstruction |
| `recommended` | Building listed by a builder/native plot, excluding plots, markers, helpers, scenery, deprecated/obsolete content |

`builders`, `plots`, and `enhancedPlots` list the producer template names that
prove availability. `reasons` contains stable classification reason strings;
`warnings` lists missing command sets, unresolved build commands, missing draw
modules, and unsupported behavior names.

Normal availability is **structural**, not a promise that the current player can
build right now: money, ownership, shroud, prerequisites, upgrades, build limits,
and placement rules still use the native availability/placement checks. Producer
menus include campaign/mod producers; a multiplayer-only palette may additionally
restrict their faction/context. Hidden or disabled menu choices still prove a
construction route, since they may unlock later. Generated plot offers remain
separate so they cannot accidentally promote enhanced-only content to native
availability.

A missing draw module, unknown template name, zero price, or absent construction
route is not proof of unfinished content. Invisible helpers and complete map-only
structures are valid. The classifier reports uncertainty rather than guessing
from words like `Dummy`, `Marker`, or `Placeholder` in a name. It does not validate
that every model/texture exists, identify every unfinished art asset, or certify
retail parity. `unsupported` is an engine implementation fact, separate from
whether the original game content was complete; `recommended` deliberately keeps
that separate so partial engine support does not empty the ordinary build palette.

Validation:

```sh
cmake --build engine/build --target openbfme_build_filter_tests
engine/build/openbfme_build_filter_tests
```
