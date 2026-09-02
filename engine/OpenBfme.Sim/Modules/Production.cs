namespace OpenBfme.Sim;

/// <summary>
/// ProductionUpdate-shaped queue (385 objects in the corpus). Design data encodes
/// buildable entries as "Build:template-name" = build ticks and optional
/// "Cost:template-name" = resource cost (default 0). TryQueue debits the owning
/// team's resources and rejects unaffordable requests; TryCancel refunds.
///
/// Spawn phase matches the retail sim (dual-run oracle finding): a unit queued
/// by a command applied at tick T spawns DURING tick T + build_ticks — build
/// ticks T .. T+build_ticks-1 count down, and the completed head spawns on the
/// following update. Optional "RallyXRaw"/"RallyYRaw" design data (producer-
/// relative, Fixed64 raw) sends a spawned unit with a LinearMover walking from
/// the exit point to the rally point — the retail door-walk shape.
/// </summary>
[SageModule("Production", ModuleTier.Structural)]
public sealed class ProductionModule : ModuleBase
{
    public const string TypeName = "Production";

    public const int MaxQueueLength = 9;

    private readonly FixedVector2 _exitOffset;
    private readonly List<(string Template, int TicksRemaining)> _queue = new();

    public ProductionModule(ModuleSpec spec) : base(spec)
    {
        _exitOffset = new FixedVector2(
            spec.GetFixed("ExitOffsetXRaw", Fixed64.FromInt(2)),
            spec.GetFixed("ExitOffsetYRaw", Fixed64.Zero));
    }

    public int QueueLength => _queue.Count;

    public long CostOf(string templateName) => Math.Max(0, Spec.GetLong("Cost:" + templateName, 0));

    public bool TryQueue(SimWorld world, GameObject self, string templateName)
    {
        var buildTicks = Spec.GetLong("Build:" + templateName, -1);
        if (buildTicks <= 0 || _queue.Count >= MaxQueueLength)
        {
            return false;
        }
        var cost = CostOf(templateName);
        if (cost > 0)
        {
            if (world.TeamResources(self.Team) < cost)
            {
                return false;
            }
            world.AddTeamResources(self.Team, -cost);
        }
        _queue.Add((templateName, (int)buildTicks));
        return true;
    }

    /// <summary>Cancels the queue entry at <paramref name="index"/> and refunds its cost.</summary>
    public bool TryCancel(SimWorld world, GameObject self, int index)
    {
        if (index < 0 || index >= _queue.Count)
        {
            return false;
        }
        var (template, _) = _queue[index];
        _queue.RemoveAt(index);
        world.AddTeamResources(self.Team, CostOf(template));
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying || _queue.Count == 0)
        {
            return;
        }
        var (template, ticksRemaining) = _queue[0];
        if (ticksRemaining > 0)
        {
            _queue[0] = (template, ticksRemaining - 1);
            return;
        }
        // ticksRemaining == 0: the head completed last tick; spawn this tick
        // (command_tick + build_ticks). The next entry starts counting next tick.
        _queue.RemoveAt(0);
        var spawned = world.SpawnObject(template, self.Team, self.Position + _exitOffset);
        if (Spec.Data.ContainsKey("RallyXRaw") || Spec.Data.ContainsKey("RallyYRaw"))
        {
            var rally = self.Position + new FixedVector2(
                Spec.GetFixed("RallyXRaw", Fixed64.Zero),
                Spec.GetFixed("RallyYRaw", Fixed64.Zero));
            spawned.FindModule<LinearMoverModule>()?.SetDestination(rally);
        }
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_queue.Count);
        foreach (var (template, ticksRemaining) in _queue)
        {
            writer.WriteString(template);
            writer.WriteInt(ticksRemaining);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        _queue.Clear();
        var count = reader.ReadInt();
        for (var i = 0; i < count; i++)
        {
            var template = reader.ReadString();
            var ticksRemaining = reader.ReadInt();
            _queue.Add((template, ticksRemaining));
        }
    }
}
