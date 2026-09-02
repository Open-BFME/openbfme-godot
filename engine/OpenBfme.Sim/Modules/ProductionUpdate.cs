namespace OpenBfme.Sim;

/// <summary>
/// SAGE ProductionUpdate queue. Money is charged at enqueue, cancellation
/// refunds the exact stored charge, and the head advances in fixed ticks.
/// </summary>
[SageModule("ProductionUpdate", ModuleTier.Structural)]
public sealed class ProductionModule : ModuleBase
{
    public const string TypeName = "ProductionUpdate";
    public const int MaxQueueLength = 9;

    private readonly int _maxQueueEntries;
    private readonly List<ProductionEntry> _queue = new();
    private bool _hasRallyPoint;
    private FixedVector2 _rallyPoint;

    public ProductionModule(ModuleSpec spec) : base(spec)
    {
        _maxQueueEntries = checked((int)Math.Clamp(
            spec.GetLong("MaxQueueEntries", MaxQueueLength),
            1,
            int.MaxValue));
    }

    public int QueueLength => _queue.Count;
    public bool HasRallyPoint => _hasRallyPoint;
    public FixedVector2 RallyPoint => _rallyPoint;
    public long ReservedCommandPoints => _queue.Sum(entry => entry.CommandPoints);
    public bool HasQueuedUpgrade(string name) =>
        _queue.Any(entry => entry.Template == UpgradePrefix + name);

    private const string UpgradePrefix = "@upgrade:";

    public long CostOf(string templateName) => Math.Max(0, Spec.GetLong("Cost:" + templateName, 0));

    public void SetRallyPoint(FixedVector2 point)
    {
        _rallyPoint = point;
        _hasRallyPoint = true;
    }

    public bool TryQueue(SimWorld world, GameObject self, string templateName) =>
        TryQueue(world, self, templateName, 1, out _);

    internal bool TryQueue(
        SimWorld world,
        GameObject self,
        string templateName,
        int count,
        out string refusalCode)
    {
        refusalCode = "";
        if (self.IsUnderConstruction)
        {
            refusalCode = "producer_under_construction";
            return false;
        }
        if (self.IsDying)
        {
            refusalCode = "producer_dying";
            return false;
        }
        if (count < 1 || count > _maxQueueEntries - _queue.Count)
        {
            refusalCode = "queue_full";
            return false;
        }
        if (!world.TryGetTemplate(templateName, out var template))
        {
            refusalCode = "unknown_template";
            return false;
        }
        if (!CanProduce(self, templateName))
        {
            refusalCode = "not_in_command_set";
            return false;
        }
        if (!world.CanSpawnProductionTemplate(template))
        {
            refusalCode = "invalid_horde_template";
            return false;
        }
        var buildTicks = ResolveBuildTicks(world, template);
        if (buildTicks <= 0)
        {
            refusalCode = "missing_build_time";
            return false;
        }
        var cost = ResolveCost(template);
        long totalCost;
        long totalCommandPoints;
        try
        {
            totalCost = checked(cost * count);
            totalCommandPoints = checked(template.Economy.CommandPoints * count);
        }
        catch (OverflowException)
        {
            refusalCode = "economy_overflow";
            return false;
        }
        if (world.TeamResources(self.Team) < totalCost)
        {
            refusalCode = "insufficient_money";
            return false;
        }
        if (!world.CanReserveCommandPoints(self.Team, totalCommandPoints))
        {
            refusalCode = "command_points_exceeded";
            return false;
        }

        world.AddTeamResources(self.Team, -totalCost);
        for (var index = 0; index < count; index++)
        {
            _queue.Add(new ProductionEntry(
                templateName,
                buildTicks,
                cost,
                template.Economy.CommandPoints));
        }
        return true;
    }

    internal bool TryQueueUpgrade(
        SimWorld world,
        GameObject self,
        UpgradeTemplate upgrade,
        out string refusalCode)
    {
        refusalCode = "";
        if (self.IsUnderConstruction)
        {
            refusalCode = "producer_under_construction";
            return false;
        }
        if (self.IsDying)
        {
            refusalCode = "producer_dying";
            return false;
        }
        if (_queue.Count >= _maxQueueEntries)
        {
            refusalCode = "queue_full";
            return false;
        }
        if (upgrade.Type == UpgradeType.Player
            ? world.TeamHasUpgrade(self.Team, upgrade.Name)
            : self.HasObjectUpgrade(upgrade.Name))
        {
            refusalCode = "upgrade_already_owned";
            return false;
        }
        if (world.IsUpgradeInProgress(self, upgrade))
        {
            refusalCode = "upgrade_in_progress";
            return false;
        }
        if (upgrade.Prerequisites.Any(name => world.HasUpgradeTemplate(name)
                && !world.ObjectHasUpgrade(self, name)))
        {
            refusalCode = "upgrade_prerequisite_missing";
            return false;
        }
        if (world.TeamResources(self.Team) < upgrade.BuildCost)
        {
            refusalCode = "insufficient_money";
            return false;
        }
        var ticks = Math.Max(1, upgrade.BuildTicks(world.TickMilliseconds));
        world.AddTeamResources(self.Team, -upgrade.BuildCost);
        _queue.Add(new ProductionEntry(UpgradePrefix + upgrade.Name, ticks, upgrade.BuildCost, 0));
        return true;
    }

    public bool TryCancel(SimWorld world, GameObject self, int index)
    {
        if (index < 0 || index >= _queue.Count)
        {
            return false;
        }
        var entry = _queue[index];
        _queue.RemoveAt(index);
        world.AddTeamResources(self.Team, entry.Cost);
        return true;
    }

    internal void RefundAll(SimWorld world, GameObject self)
    {
        foreach (var entry in _queue) world.AddTeamResources(self.Team, entry.Cost);
        _queue.Clear();
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (self.IsUnderConstruction || self.IsDying || _queue.Count == 0)
        {
            return;
        }
        var entry = _queue[0];
        if (entry.TicksRemaining > 0)
        {
            _queue[0] = entry with { TicksRemaining = entry.TicksRemaining - 1 };
            return;
        }
        _queue.RemoveAt(0);
        if (entry.Template.StartsWith(UpgradePrefix, StringComparison.Ordinal))
        {
            world.CompleteUpgrade(self, world.UpgradeTemplate(entry.Template[UpgradePrefix.Length..]));
            return;
        }
        world.SpawnProducedObject(
            self,
            entry.Template,
            self.Position + ResolveExitOffset(self),
            ResolveRallyPoint(self));
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_queue.Count);
        foreach (var entry in _queue)
        {
            writer.WriteString(entry.Template);
            writer.WriteInt(entry.TicksRemaining);
            writer.WriteLong(entry.Cost);
            writer.WriteLong(entry.CommandPoints);
        }
        writer.WriteBool(_hasRallyPoint);
        writer.WriteVector(_rallyPoint);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _queue.Clear();
        var count = reader.ReadInt();
        if (count < 0 || count > _maxQueueEntries)
        {
            throw new InvalidDataException("Production queue length is invalid");
        }
        for (var index = 0; index < count; index++)
        {
            _queue.Add(new ProductionEntry(
                reader.ReadString(),
                reader.ReadInt(),
                reader.ReadLong(),
                reader.ReadLong()));
        }
        _hasRallyPoint = reader.ReadBool();
        _rallyPoint = reader.ReadVector();
    }

    private bool CanProduce(GameObject self, string templateName)
    {
        if (self.CurrentCommandSet.Length > 0) return true;
        if (self.Template.Economy.CommandSet.Count > 0)
        {
            return self.Template.Economy.CommandSet.Contains(templateName, StringComparer.Ordinal);
        }
        return Spec.Data.ContainsKey("Build:" + templateName);
    }

    private int ResolveBuildTicks(SimWorld world, ObjectTemplate template)
    {
        if (template.Economy.BuildTimeMilliseconds > 0)
        {
            return template.Economy.BuildTicks(world.TickMilliseconds);
        }
        return checked((int)Math.Max(0, Spec.GetLong("Build:" + template.Name, 0)));
    }

    private long ResolveCost(ObjectTemplate template) =>
        template.Economy.BuildCost > 0 ? template.Economy.BuildCost : CostOf(template.Name);

    private FixedVector2 ResolveExitOffset(GameObject self)
        => ProductionSpawnGeometry.ResolveExitOffset(self.Template, Spec);

    private FixedVector2? ResolveRallyPoint(GameObject self)
    {
        if (_hasRallyPoint) return _rallyPoint;
        if (Spec.Data.ContainsKey("RallyXRaw") || Spec.Data.ContainsKey("RallyYRaw"))
        {
            return self.Position + new FixedVector2(
                Spec.GetFixed("RallyXRaw", Fixed64.Zero),
                Spec.GetFixed("RallyYRaw", Fixed64.Zero));
        }
        return null;
    }

    private sealed record ProductionEntry(
        string Template,
        int TicksRemaining,
        long Cost,
        long CommandPoints);
}
