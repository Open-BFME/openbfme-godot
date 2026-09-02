namespace OpenBfme.Sim;

internal interface IMovementModifierModule
{
    Fixed64 MovementSpeedMultiplier { get; }
    bool PausesMovement { get; }
}

internal interface IOutgoingDamageModifierModule
{
    Fixed64 OutgoingDamageMultiplier { get; }
}

internal interface IPresentationStateModule
{
    int PresentationStateBits { get; }
}

internal static class ModuleRuntime
{
    public static Fixed64 MovementMultiplier(GameObject gameObject)
    {
        var result = Fixed64.One;
        foreach (var module in gameObject.Modules)
        {
            if (module is not IMovementModifierModule modifier) continue;
            if (modifier.PausesMovement) return Fixed64.Zero;
            result *= Fixed64.Max(Fixed64.Zero, modifier.MovementSpeedMultiplier);
        }
        return result;
    }

    public static Fixed64 OutgoingDamageMultiplier(GameObject gameObject)
    {
        var result = Fixed64.One;
        foreach (var module in gameObject.Modules)
            if (module is IOutgoingDamageModifierModule modifier)
                result *= Fixed64.Max(Fixed64.Zero, modifier.OutgoingDamageMultiplier);
        return result;
    }

    public static int PresentationBits(GameObject gameObject)
    {
        var result = 0;
        foreach (var module in gameObject.Modules)
            if (module is IPresentationStateModule state) result |= state.PresentationStateBits;
        return result;
    }

    public static Fixed64 ReadFixed(ModuleSpec spec, string name, Fixed64 fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw)) return Fixed64.FromRaw(raw);
        if (spec.Data.TryGetValue(name, out var integer)) return Fixed64.FromInt64(integer);
        return fallback;
    }

    public static bool ReadBool(ModuleSpec spec, string name, bool fallback = false)
    {
        if (spec.Data.TryGetValue(name, out var value)) return value != 0;
        var text = spec.GetString(name, "").Trim();
        if (text.Length == 0) return fallback;
        return text.Equals("yes", StringComparison.OrdinalIgnoreCase)
            || text.Equals("true", StringComparison.OrdinalIgnoreCase)
            || text.Equals("1", StringComparison.Ordinal);
    }

    public static int MillisecondsToTicks(long milliseconds, int tickMilliseconds) =>
        Math.Max(1, IniValueReader.MillisecondsToTicks(Math.Max(0, milliseconds), tickMilliseconds));

    public static long SecondsFieldToMilliseconds(ModuleSpec spec, string name, long fallback)
    {
        if (spec.Data.TryGetValue(name + "Raw", out var raw))
        {
            var scaled = (System.Numerics.BigInteger)raw * 1_000 / Fixed64.OneRaw;
            return checked((long)scaled);
        }
        return checked(spec.GetLong(name, fallback / 1_000) * 1_000);
    }

    public static string[] Tokens(string text) =>
        text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    public static bool MatchesKindOf(GameObject gameObject, string filter)
    {
        var positives = Tokens(filter).Where(value => value[0] == '+').Select(value => value[1..]).ToArray();
        var negatives = Tokens(filter).Where(value => value[0] == '-').Select(value => value[1..]).ToArray();
        bool Matches(string token) => gameObject.TemplateName.Equals(token, StringComparison.Ordinal)
            || gameObject.Template.KindOf.Contains(token, StringComparer.Ordinal);
        if (negatives.Any(Matches)) return false;
        return positives.Length == 0 || positives.Any(Matches);
    }
}

public sealed partial class SimWorld
{
    internal Fixed64 MovementMultiplier(GameObject gameObject) => ModuleRuntime.MovementMultiplier(gameObject);

    internal Fixed64 OutgoingDamageMultiplier(int objectId) =>
        _objects.TryGetValue(objectId, out var gameObject)
            ? ModuleRuntime.OutgoingDamageMultiplier(gameObject)
            : Fixed64.One;

    internal Fixed64 Heal(GameObject target, Fixed64 amount)
    {
        if (amount <= Fixed64.Zero || target.IsDead || target.IsDying) return Fixed64.Zero;
        var before = target.Health;
        if (target.Combat is { HasBody: true } combat)
            combat.Health = Fixed64.Min(combat.MaxHealth, combat.Health + amount);
        else
        {
            var (health, maximum) = ReadHealth(target);
            target.SetConstructionHealth(Fixed64.Min(maximum, health + amount));
        }
        return Fixed64.Max(Fixed64.Zero, target.Health - before);
    }

    internal bool GrantUpgrade(GameObject target, string name, bool forcePlayer = false)
    {
        if (name.Length == 0) return false;
        var player = forcePlayer || (_config.Tech.Upgrades.TryGetValue(name, out var authored)
            && authored.Type == UpgradeType.Player);
        if (player)
        {
            if (target.Team < 0 || !_teamUpgrades[target.Team].Add(name)) return false;
            foreach (var gameObject in _objects.Values)
                if (gameObject.Team == target.Team) EvaluateUpgradeModules(gameObject);
            return true;
        }
        if (!target.AddObjectUpgrade(name)) return false;
        EvaluateUpgradeModules(target);
        return true;
    }

    internal GameObject ReplaceObject(GameObject oldObject, string templateName)
    {
        var fraction = oldObject.MaxHealth > Fixed64.Zero
            ? oldObject.Health / oldObject.MaxHealth
            : Fixed64.One;
        var replacement = SpawnObjectFrom(
            templateName,
            oldObject.Team,
            oldObject.Position,
            oldObject,
            oldObject.Elevation,
            oldObject.HeadingRadians);
        if (replacement.MaxHealth > Fixed64.Zero)
            replacement.SetConstructionHealth(replacement.MaxHealth * Fixed64.Clamp(fraction, Fixed64.Zero, Fixed64.One));
        oldObject.MarkDead();
        return replacement;
    }

    internal void RegisterFoundation(GameObject foundation)
    {
        if (_buildPlots.ContainsKey((foundation.Id, 0))) return;
        var allowed = foundation.Template.Economy.CommandSet
            .OrderBy(value => value, StringComparer.Ordinal)
            .ToArray();
        _buildPlots.Add((foundation.Id, 0), new BuildPlot(foundation.Id, 0, foundation.Position, allowed));
    }

    internal void UnregisterFoundation(int foundationId) => _buildPlots.Remove((foundationId, 0));
}
