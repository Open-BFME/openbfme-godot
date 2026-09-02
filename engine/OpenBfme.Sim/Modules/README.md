# Modules

One file per SAGE module type, named exactly as the INI names it
(`AutoHealBehavior.cs`, `HordeContain.cs`, `SlowDeathBehavior.cs`). The
registry discovers them by reflection over this namespace; nothing is
listed by hand.

Each module file contains:

1. the data record parsed from the module's INI fields,
2. the behavior class deriving from `ModuleBase`,
3. a `[SageModule("Name", Tier.Structural|Tier.Cosmetic)]` attribute.

Each module ships `OpenBfme.Sim.Tests/Modules/<Name>Tests.cs` with a golden
trace: an INI fixture in, a command list, an expected state trace out.
Reference behavior comes from the pseudo-C in
`workspace/reference/open-bfme-1`; numbers come from the INI.

The existing modules in `../Modules.cs`, `../CombatModules.cs`, and
`../GapModules.cs` predate this layout and move here one file at a time as
they are touched.
