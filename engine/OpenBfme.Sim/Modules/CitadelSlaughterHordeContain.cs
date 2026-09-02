namespace OpenBfme.Sim;

/// <summary>
/// Citadel/slaughterhouse entry processor. TryEnter enforces same-team/allied,
/// enemy, neutral, ContainMax, and positive/negative PassengerFilter terms.
/// The kernel currently has team-level ownership, so AllowOwnPlayerInsideOverride
/// and AllowAlliesInside are alternative permissions for same-team objects. Ordinary
/// passengers are consumed for CashBackPercent of BuildCost; ring objects are
/// consumed, apply StatusForRingEntry, and grant every UpgradeForRingEntry
/// through the normal evaluator. Entry geometry, sounds, and FX are deferred.
/// </summary>
[SageModule("CitadelSlaughterHordeContain", ModuleTier.Structural)]
public sealed class CitadelSlaughterHordeContainModule : ModuleBase
{
    public const string TypeName = "CitadelSlaughterHordeContain";
    private readonly int _maximum;
    private readonly bool _allowAllies;
    private readonly bool _allowEnemies;
    private readonly bool _allowNeutral;
    private readonly bool _allowOwner;
    private readonly Fixed64 _cashback;
    private readonly string[] _requiredKinds;
    private readonly string[] _forbiddenKinds;
    private readonly string[] _ringKinds;
    private readonly string[] _ringUpgrades;
    private readonly string[] _ringStatuses;
    private int _entriesProcessed;

    public CitadelSlaughterHordeContainModule(ModuleSpec spec) : base(spec)
    {
        _maximum = checked((int)Math.Clamp(spec.GetLong("ContainMax", 0), 0, int.MaxValue));
        _allowAllies = spec.GetLong("AllowAlliesInside", 0) != 0;
        _allowEnemies = spec.GetLong("AllowEnemiesInside", 0) != 0;
        _allowNeutral = spec.GetLong("AllowNeutralInside", 0) != 0;
        _allowOwner = spec.GetLong("AllowOwnPlayerInsideOverride", 1) != 0;
        _cashback = spec.StringData.TryGetValue("CashBackPercent", out var percent)
            ? IniValueReader.PercentMultiplier(percent, "CashBackPercent") : Fixed64.Zero;
        var filter = Tokens(spec.GetString("PassengerFilter", ""));
        _requiredKinds = filter.Where(token => token.StartsWith('+')).Select(token => token[1..]).ToArray();
        _forbiddenKinds = filter.Where(token => token.StartsWith('-')).Select(token => token[1..]).ToArray();
        _ringKinds = Tokens(spec.GetString("ObjectToDestroyForRingEntry", ""))
            .Where(token => token.StartsWith('+')).Select(token => token[1..]).ToArray();
        _ringUpgrades = Tokens(spec.GetString("UpgradeForRingEntry", ""));
        _ringStatuses = Tokens(spec.GetString("StatusForRingEntry", ""));
    }

    public int EntriesProcessed => _entriesProcessed;

    public bool TryEnter(SimWorld world, GameObject self, GameObject passenger)
    {
        if (_maximum > 0 && _entriesProcessed >= _maximum) return false;
        var sameTeam = passenger.Team == self.Team;
        if ((sameTeam && !(_allowOwner || _allowAllies))
            || (passenger.Team < 0 && !_allowNeutral)
            || (!sameTeam && passenger.Team >= 0 && !_allowEnemies)) return false;
        if ((_requiredKinds.Length > 0 && !_requiredKinds.Any(kind => passenger.Template.KindOf.Contains(kind,
                StringComparer.OrdinalIgnoreCase)))
            || _forbiddenKinds.Any(kind => passenger.Template.KindOf.Contains(kind,
                StringComparer.OrdinalIgnoreCase))) return false;
        var ring = _ringKinds.Length > 0 && _ringKinds.All(kind => passenger.Template.KindOf.Contains(kind,
            StringComparer.OrdinalIgnoreCase));
        if (ring)
        {
            foreach (var status in _ringStatuses) self.SetConditionToken(status);
            foreach (var upgrade in _ringUpgrades) world.GrantObjectUpgrade(self, upgrade);
        }
        else
        {
            var cashback = EconomyTemplate.ScaleInteger(passenger.Template.Economy.BuildCost, _cashback);
            if (cashback > 0) world.AddTeamResources(self.Team, cashback);
        }
        _entriesProcessed++;
        world.HandleDeath(passenger);
        return true;
    }

    private static string[] Tokens(string value) =>
        value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_entriesProcessed);
    public override void ReadState(CanonicalReader reader) => _entriesProcessed = reader.ReadInt();
}
