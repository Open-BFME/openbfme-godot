namespace OpenBfme.Stage1;

public sealed class Member
{
    public required int EntityId { get; init; }
    public required int HordeId { get; init; }
    public required TeamId Team { get; init; }
    public required int FormationSlot { get; init; }
    public required bool IsRanged { get; set; }
    public required WorldPos Position { get; set; }
    public int MaximumHealth { get; init; } = 100;
    public int Health { get; set; } = 100;
    public int AttackCooldownTicks { get; set; }
    public int TargetEntityId { get; set; }
    public bool IsAlive => Health > 0;
}

public sealed class Horde
{
    public required int EntityId { get; init; }
    public required TeamId Team { get; init; }
    public required WorldPos Anchor { get; set; }
    public WorldPos Destination { get; set; }
    public OrderKind Order { get; set; } = OrderKind.Stop;
    public int TargetEntityId { get; set; }
    public int EngagedHordeId { get; set; }
    public List<GridCell> Path { get; } = [];
    public int PathIndex { get; set; }
    public int PathRevision { get; set; }
    public int PopulationCost { get; set; } = 1;
    public bool PopulationReleased { get; set; }
    public List<Member> Members { get; } = [];
    public int AliveCount => Members.Count(static member => member.IsAlive);
}

public sealed class Fortress
{
    public required int EntityId { get; init; }
    public required TeamId Team { get; init; }
    public required WorldPos Position { get; init; }
    public int MaximumHealth { get; init; } = 5_000;
    public int Health { get; set; } = 5_000;
    public bool IsAlive => Health > 0;
}

public sealed class Projectile
{
    public required int EntityId { get; init; }
    public required TeamId Team { get; init; }
    public required int SourceEntityId { get; init; }
    public required int TargetEntityId { get; init; }
    public required WorldPos Position { get; set; }
    public required WorldPos LastKnownTarget { get; set; }
    public int Damage { get; init; } = 18;
    public bool IsActive { get; set; } = true;
}
