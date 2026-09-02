using System.Text.RegularExpressions;

namespace OpenBfme.Sim;

/// <summary>
/// Hero death/respawn controller. Death preserves the ExperienceLevel, then a
/// normal command-v1 `train` against an allied ProductionUpdate at the authored
/// AutoRespawnAtObjectFilter queues the level-specific RespawnRules/RespawnEntry
/// cost and time. AutoSpawn:Yes deterministically chooses the nearest eligible
/// producer. The hero reappears at that producer with retained level and authored
/// health percentage. Animation, image, FX, and audio fields are presentation.
/// </summary>
[SageModule("RespawnUpdate", ModuleTier.Structural)]
public sealed partial class RespawnUpdateModule : ModuleBase
{
    public const string TypeName = "RespawnUpdate";
    private readonly SortedDictionary<int, RespawnRule> _rules = new();
    private readonly string[] _producerKindOf;
    private readonly string _respawnTemplate;
    private readonly int _deathAnimationMilliseconds;
    private bool _waiting;
    private bool _queued;
    private bool _autoSpawn;
    private int _deathTicksRemaining;
    private int _respawnTicksRemaining;
    private int _producerId;
    private int _retainedLevel = 1;

    public RespawnUpdateModule(ModuleSpec spec) : base(spec)
    {
        _producerKindOf = Tokens(spec.GetString("AutoRespawnAtObjectFilter", ""))
            .Where(token => token.StartsWith('+')).Select(token => token[1..]).ToArray();
        _respawnTemplate = spec.GetString("RespawnAsTemplate", "");
        _deathAnimationMilliseconds = checked((int)Math.Clamp(spec.GetLong("DeathAnimationTime", 0), 0, int.MaxValue));
        ParseRule(spec.GetString("RespawnRules", ""), 1);
        foreach (var row in spec.GetString("RespawnEntry", "")
            .Split('\n', StringSplitOptions.RemoveEmptyEntries)) ParseRule(row, 1);
    }

    public bool IsWaiting => _waiting;
    public bool IsQueued => _queued;
    public int RetainedLevel => _retainedLevel;
    public int RespawnTicksRemaining => _respawnTicksRemaining;

    public override bool OnDeath(SimWorld world, GameObject self)
    {
        if (_waiting) return true;
        _waiting = true;
        _retainedLevel = self.FindModule<ExperienceLevelModule>()?.Level ?? 1;
        _deathTicksRemaining = IniValueReader.MillisecondsToTicks(_deathAnimationMilliseconds, world.TickMilliseconds);
        self.MarkDying();
        return true;
    }

    internal bool TryBeginRespawn(SimWorld world, GameObject hero, GameObject producer, out string refusal)
    {
        refusal = "";
        if (!_waiting || _queued || _deathTicksRemaining > 0) { refusal = "hero_not_respawnable"; return false; }
        if (producer.Team != hero.Team || producer.FindModule<ProductionModule>() == null
            || _producerKindOf.Any(required => !producer.Template.KindOf.Contains(required,
                StringComparer.OrdinalIgnoreCase)))
        {
            refusal = "invalid_respawn_producer";
            return false;
        }
        var rule = RuleFor(_retainedLevel);
        if (world.TeamResources(hero.Team) < rule.Cost) { refusal = "insufficient_money"; return false; }
        world.AddTeamResources(hero.Team, -rule.Cost);
        _producerId = producer.Id;
        _respawnTicksRemaining = IniValueReader.MillisecondsToTicks(rule.TimeMilliseconds, world.TickMilliseconds);
        _queued = true;
        world.RaiseEvent(new SimEvent("build_start", hero.Id, producer.Id, Name: hero.TemplateName,
            Amount: Fixed64.FromInt64(rule.Cost)));
        return true;
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (!_waiting) return;
        if (_deathTicksRemaining > 0) { _deathTicksRemaining--; return; }
        if (!_queued && _autoSpawn)
        {
            foreach (var producer in world.Objects.Values
                .Where(value => value.Team == self.Team && !value.IsDying)
                .OrderBy(value => self.Position.DistanceSquaredTo(value.Position)).ThenBy(value => value.Id))
                if (TryBeginRespawn(world, self, producer, out _)) break;
        }
        if (!_queued) return;
        if (_respawnTicksRemaining > 0) _respawnTicksRemaining--;
        if (_respawnTicksRemaining > 0) return;
        var position = world.Objects.TryGetValue(_producerId, out var foundProducer)
            ? foundProducer.Position : self.Position;
        var template = _respawnTemplate.Length > 0 ? _respawnTemplate : self.TemplateName;
        if (!world.TryGetTemplate(template, out _)) template = self.TemplateName;
        var revived = world.SpawnObject(template, self.Team, position);
        var level = revived.FindModule<ExperienceLevelModule>();
        if (level != null && _retainedLevel > 1) level.GrantLevels(_retainedLevel - 1);
        var rule = RuleFor(_retainedLevel);
        revived.SetConstructionHealth(revived.MaxHealth * rule.HealthMultiplier);
        self.MarkDead();
        _waiting = false;
        _queued = false;
    }

    private RespawnRule RuleFor(int level)
    {
        var selected = _rules.First().Value;
        foreach (var (ruleLevel, rule) in _rules)
            if (ruleLevel <= level) selected = rule; else break;
        return selected;
    }

    private void ParseRule(string text, int fallbackLevel)
    {
        if (string.IsNullOrWhiteSpace(text)) return;
        var values = RulePart().Matches(text).ToDictionary(
            match => match.Groups[1].Value, match => match.Groups[2].Value,
            StringComparer.OrdinalIgnoreCase);
        var level = values.TryGetValue("Level", out var levelText) && int.TryParse(levelText, out var parsedLevel)
            ? parsedLevel : fallbackLevel;
        var cost = values.TryGetValue("Cost", out var costText) && long.TryParse(costText, out var parsedCost)
            ? Math.Max(0, parsedCost) : 0;
        var time = values.TryGetValue("Time", out var timeText) && long.TryParse(timeText, out var parsedTime)
            ? Math.Max(0, parsedTime) : 0;
        var health = values.TryGetValue("Health", out var healthText)
            ? ParsePercent(healthText) : Fixed64.One;
        if (values.TryGetValue("AutoSpawn", out var auto)) _autoSpawn = auto.Equals("Yes", StringComparison.OrdinalIgnoreCase);
        _rules[level] = new RespawnRule(cost, time, health);
    }

    private static Fixed64 ParsePercent(string text)
    {
        var trimmed = text.Trim().TrimEnd('%');
        return long.TryParse(trimmed, out var value)
            ? Fixed64.Clamp(Fixed64.FromFraction(value, 100), Fixed64.Zero, Fixed64.One)
            : Fixed64.One;
    }

    private static string[] Tokens(string value) =>
        value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    [GeneratedRegex(@"(Level|Cost|Time|Health|AutoSpawn):([^\s]+)", RegexOptions.IgnoreCase)]
    private static partial Regex RulePart();

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_waiting);
        writer.WriteBool(_queued);
        writer.WriteInt(_deathTicksRemaining);
        writer.WriteInt(_respawnTicksRemaining);
        writer.WriteInt(_producerId);
        writer.WriteInt(_retainedLevel);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _waiting = reader.ReadBool();
        _queued = reader.ReadBool();
        _deathTicksRemaining = reader.ReadInt();
        _respawnTicksRemaining = reader.ReadInt();
        _producerId = reader.ReadInt();
        _retainedLevel = reader.ReadInt();
    }

    private sealed record RespawnRule(long Cost, long TimeMilliseconds, Fixed64 HealthMultiplier);
}
