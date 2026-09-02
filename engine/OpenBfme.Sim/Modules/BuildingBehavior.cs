namespace OpenBfme.Sim;

/// <summary>Persistent structure foundation and authored automatic RebuildTime hole recovery.</summary>
[SageModule("BuildingBehavior", ModuleTier.Structural)]
public sealed class BuildingBehaviorModule : ModuleBase, IPresentationStateModule
{
    public const string TypeName = "BuildingBehavior";
    public const int PresentationRebuildHole = 1 << 7;
    private readonly long _rebuildMilliseconds;
    private int _ticksRemaining;
    private bool _hole;

    public BuildingBehaviorModule(ModuleSpec spec) : base(spec) =>
        _rebuildMilliseconds = Math.Max(0, spec.GetLong("RebuildTime", 0));

    public bool IsRebuildHole => _hole;
    public int PresentationStateBits => _hole ? PresentationRebuildHole : 0;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_hole) return true;
        _hole = true;
        self.MarkDying();
        self.TrySetConditionToken("RUBBLE");
        _ticksRemaining = _rebuildMilliseconds == 0
            ? 0
            : ModuleRuntime.MillisecondsToTicks(_rebuildMilliseconds, world.TickMilliseconds);
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_hole || _ticksRemaining == 0) return;
        if (--_ticksRemaining > 0) return;
        _hole = false;
        self.TrySetConditionToken("RUBBLE", false);
        self.RestoreFromRebuild(self.MaxHealth > Fixed64.Zero ? self.MaxHealth : Fixed64.One);
        world.RaiseEvent(new SimEvent("build_done", self.Id, Name: self.TemplateName));
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksRemaining);
        writer.WriteBool(_hole);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksRemaining = reader.ReadInt();
        _hole = reader.ReadBool();
    }
}
