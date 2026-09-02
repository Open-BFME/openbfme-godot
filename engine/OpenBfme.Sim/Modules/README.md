# Modules

One file per SAGE module type, named exactly as the INI names it
(`AutoHealBehavior.cs`, `HordeContain.cs`, `SlowDeathBehavior.cs`). The
registry discovers them by reflection over this namespace; nothing is
listed by hand.

Each module file contains:

1. the data record parsed from the module's INI fields,
2. the behavior class deriving from `ModuleBase`,
3. a `[SageModule("Name", ModuleTier.Structural|ModuleTier.Cosmetic)]` attribute.

Each module ships `OpenBfme.Sim.Tests/Modules/<Name>Tests.cs` with a golden
trace: an INI fixture in, a command list, an expected state trace out.
Reference behavior comes from the pseudo-C in
`workspace/reference/open-bfme-1`; numbers come from the INI.

The registry and shared module base stay in `../Modules.cs`; every concrete
SAGE module implementation lives here and is discovered from its attribute.

## Runtime tier policy

Tiering follows the carrier without a hand-maintained module-name list. When
the reflection registry has an implementation, its `SageModule` attribute is
authoritative. For an unknown type, only the `Body` carrier is Structural: a
template with an unknown or unparseable body fails with that type named.
Unknown `Behavior`, `Draw`, `ClientUpdate`, `ClientBehavior`, `Flasher`, and
all other carriers are Cosmetic. Their templates load, while `BundleLoadReport`
records each gap with template, type, and carrier and aggregates counts by type.

A file here that only records a gap (its effect calls `RecordTechGap`/`RecordGap`
or derives from a `Cosmetic*Gap` placeholder) is a stub, not an implementation.
`tools/fleet/work.py` keeps such types open in the core-modules queue until the
real effect lands.
