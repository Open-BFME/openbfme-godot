namespace OpenBfme.Sim;

public enum UnitStance : byte
{
    Aggressive = 1,
    Battle = 2,
    HoldGround = 3,
}

internal enum CombatOrderKind : byte
{
    None,
    Attack,
    AttackMove,
}

internal enum WeaponCyclePhase : byte
{
    Ready,
    PreAttack,
    BetweenShots,
    Reload,
}

internal sealed record PendingDamageImpact(
    int TargetId,
    FixedVector2 ImpactPoint,
    string WeaponName,
    int NuggetIndex,
    int TicksRemaining);

/// <summary>Authoritative per-object combat state; present only on combat/body templates.</summary>
internal sealed class CombatState
{
    private readonly SortedSet<string> _conditions = new(StringComparer.Ordinal);
    private readonly List<PendingDamageImpact> _pendingImpacts = new();

    public CombatState(ObjectTemplate template)
    {
        ArgumentNullException.ThrowIfNull(template);
        if (template.BodyHealth is { } body)
        {
            HasBody = true;
            MaxHealth = body.MaxHealth;
            Health = body.InitialHealth;
        }
    }

    public bool HasBody { get; }
    public Fixed64 Health { get; set; }
    public Fixed64 MaxHealth { get; }
    public IReadOnlySet<string> Conditions => _conditions;
    public UnitStance Stance { get; set; } = UnitStance.Battle;
    public CombatOrderKind OrderKind { get; set; }
    public int OrderedTargetId { get; set; }
    public int EngagedTargetId { get; set; }
    public FixedVector2 AttackMoveGoal { get; set; }
    public bool HasAttackMoveGoal { get; set; }
    public bool AcquiredAutomatically { get; set; }
    public bool HasFiredAtTarget { get; set; }
    public WeaponCyclePhase CyclePhase { get; set; }
    public int CycleTicksRemaining { get; set; }
    public int ShotsInClip { get; set; }
    public IList<PendingDamageImpact> PendingImpacts => _pendingImpacts;

    public void SetCondition(string token, bool enabled)
    {
        if (string.IsNullOrWhiteSpace(token)) throw new ArgumentException("Condition token is required", nameof(token));
        if (enabled) _conditions.Add(token);
        else _conditions.Remove(token);
    }

    public void ClearOrder()
    {
        OrderKind = CombatOrderKind.None;
        OrderedTargetId = 0;
        EngagedTargetId = 0;
        HasAttackMoveGoal = false;
        AcquiredAutomatically = false;
        HasFiredAtTarget = false;
        ResetWeaponCycle();
    }

    public void DropTarget()
    {
        EngagedTargetId = 0;
        AcquiredAutomatically = false;
        HasFiredAtTarget = false;
        ResetWeaponCycle();
    }

    public void ResetWeaponCycle()
    {
        CyclePhase = WeaponCyclePhase.Ready;
        CycleTicksRemaining = 0;
        ShotsInClip = 0;
    }

    public void Write(CanonicalWriter writer)
    {
        writer.WriteBool(HasBody);
        if (HasBody) writer.WriteFixed(Health);
        writer.WriteInt(_conditions.Count);
        foreach (var condition in _conditions) writer.WriteString(condition);
        writer.WriteByte((byte)Stance);
        writer.WriteByte((byte)OrderKind);
        writer.WriteInt(OrderedTargetId);
        writer.WriteInt(EngagedTargetId);
        writer.WriteVector(AttackMoveGoal);
        writer.WriteBool(HasAttackMoveGoal);
        writer.WriteBool(AcquiredAutomatically);
        writer.WriteBool(HasFiredAtTarget);
        writer.WriteByte((byte)CyclePhase);
        writer.WriteInt(CycleTicksRemaining);
        writer.WriteInt(ShotsInClip);
        writer.WriteInt(_pendingImpacts.Count);
        foreach (var impact in _pendingImpacts)
        {
            writer.WriteInt(impact.TargetId);
            writer.WriteVector(impact.ImpactPoint);
            writer.WriteString(impact.WeaponName);
            writer.WriteInt(impact.NuggetIndex);
            writer.WriteInt(impact.TicksRemaining);
        }
    }

    public void Read(CanonicalReader reader)
    {
        var hasBody = reader.ReadBool();
        if (hasBody != HasBody) throw new InvalidDataException("Snapshot combat body shape does not match template");
        if (HasBody) Health = reader.ReadFixed();
        _conditions.Clear();
        var count = reader.ReadInt();
        if (count < 0) throw new InvalidDataException("Negative combat condition count");
        for (var index = 0; index < count; index++) _conditions.Add(reader.ReadString());
        Stance = (UnitStance)reader.ReadByte();
        OrderKind = (CombatOrderKind)reader.ReadByte();
        OrderedTargetId = reader.ReadInt();
        EngagedTargetId = reader.ReadInt();
        AttackMoveGoal = reader.ReadVector();
        HasAttackMoveGoal = reader.ReadBool();
        AcquiredAutomatically = reader.ReadBool();
        HasFiredAtTarget = reader.ReadBool();
        CyclePhase = (WeaponCyclePhase)reader.ReadByte();
        CycleTicksRemaining = reader.ReadInt();
        ShotsInClip = reader.ReadInt();
        _pendingImpacts.Clear();
        var pendingCount = reader.ReadInt();
        if (pendingCount < 0) throw new InvalidDataException("Negative pending combat impact count");
        for (var index = 0; index < pendingCount; index++)
        {
            _pendingImpacts.Add(new PendingDamageImpact(
                reader.ReadInt(),
                reader.ReadVector(),
                reader.ReadString(),
                reader.ReadInt(),
                reader.ReadInt()));
        }
    }
}
