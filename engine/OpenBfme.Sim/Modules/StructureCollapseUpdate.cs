namespace OpenBfme.Sim;

/// <summary>
/// StructureCollapseUpdate (656 objects): the collapsing building holds for
/// its rubble sequence, then releases for removal.
/// </summary>
[SageModule("StructureCollapseUpdate", ModuleTier.Structural)]
public sealed class StructureCollapseModule : TimedDeathModuleBase
{
    public const string TypeName = "StructureCollapseUpdate";

    public StructureCollapseModule(ModuleSpec spec)
        : base(spec, "CollapseTicks", 45, zeroMeansForever: false)
    {
    }
}
