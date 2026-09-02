namespace OpenBfme.Sim;

/// <summary>
/// Periodically evaluates authored InvisibilityNugget rows. StartsActive,
/// UpdatePeriod, RequiredUpgrades, DetectionRange, ForbiddenConditions,
/// InvisibilityType, and DETECTED_BY_FRIENDLIES drive authoritative STEALTHED
/// condition state. Hidden objects are excluded from normal combat acquisition.
/// Broadcast object filtering and stealth transition FX/audio are deferred.
/// </summary>
[SageModule("InvisibilityUpdate", ModuleTier.Structural)]
public sealed class InvisibilityUpdateModule : ModuleBase
{
    public const string TypeName = "InvisibilityUpdate";
    private readonly bool _startsActive;
    private readonly long _periodMilliseconds;
    private readonly string[] _requiredUpgrades;
    private readonly InvisibilityNugget[] _nuggets;
    private int _ticksUntilUpdate;
    private bool _invisible;

    public InvisibilityUpdateModule(ModuleSpec spec) : base(spec)
    {
        _startsActive = spec.GetLong("StartsActive", 0) != 0;
        _periodMilliseconds = Math.Max(1, spec.GetLong("UpdatePeriod", 1_000));
        _requiredUpgrades = spec.GetString("RequiredUpgrades", "")
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        _nuggets = spec.Blocks.Where(block => block.Type.Equals("InvisibilityNugget",
                StringComparison.OrdinalIgnoreCase)).Select(Parse).ToArray();
        _invisible = _startsActive;
    }

    public bool IsInvisible => _invisible;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_ticksUntilUpdate > 0)
        {
            _ticksUntilUpdate--;
            if (_ticksUntilUpdate > 0) return;
        }
        _ticksUntilUpdate = IniValueReader.MillisecondsToTicks(_periodMilliseconds, world.TickMilliseconds);
        var hidden = _startsActive && _nuggets.Length > 0
            && _requiredUpgrades.All(upgrade => world.ObjectHasUpgrade(self, upgrade));
        foreach (var nugget in _nuggets)
        {
            if (nugget.ForbiddenConditions.Any(self.HasConditionToken)) { hidden = false; break; }
            var radiusSquared = nugget.DetectionRange * nugget.DetectionRange;
            foreach (var observer in world.Objects.Values)
            {
                if (observer.Id == self.Id || observer.IsDead || observer.IsDying) continue;
                if (observer.Team == self.Team && !nugget.DetectedByFriendlies) continue;
                if (self.Position.DistanceSquaredTo(observer.Position) <= radiusSquared)
                { hidden = false; break; }
            }
            if (!hidden) break;
        }
        _invisible = hidden;
        self.SetConditionToken("STEALTHED", hidden);
        foreach (var nugget in _nuggets) self.SetConditionToken(nugget.Type, hidden);
    }

    private static InvisibilityNugget Parse(BundleBlock block)
    {
        var type = Text(block, "InvisibilityType", "CAMOUFLAGE");
        var forbidden = Text(block, "ForbiddenConditions", "")
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var options = Text(block, "Options", "");
        return new InvisibilityNugget(type, Fixed(block, "DetectionRange", Fixed64.Zero), forbidden,
            options.Contains("DETECTED_BY_FRIENDLIES", StringComparison.OrdinalIgnoreCase));
    }

    private static string Text(BundleBlock block, string name, string fallback) =>
        block.Fields.TryGetValue(name, out var value) && value.Kind == BundleValueKind.String
            ? value.String ?? fallback : fallback;

    private static Fixed64 Fixed(BundleBlock block, string name, Fixed64 fallback)
    {
        if (!block.Fields.TryGetValue(name, out var value)) return fallback;
        return value.Kind switch
        {
            BundleValueKind.Fixed => value.Fixed,
            BundleValueKind.Integer => Fixed64.FromInt64(value.Integer),
            _ => fallback,
        };
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(_ticksUntilUpdate);
        writer.WriteBool(_invisible);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _ticksUntilUpdate = reader.ReadInt();
        _invisible = reader.ReadBool();
    }

    private sealed record InvisibilityNugget(
        string Type, Fixed64 DetectionRange, string[] ForbiddenConditions, bool DetectedByFriendlies);
}
