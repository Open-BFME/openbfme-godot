namespace OpenBfme.Sim;

/// <summary>Immutable produced-horde membership authored by the bundle horde table.</summary>
public sealed record HordeProductionRank(
    long Rank,
    string UnitType,
    IReadOnlyList<FixedVector2> Positions);

public sealed class HordeProductionTemplate
{
    public HordeProductionTemplate(string name, IEnumerable<HordeProductionRank> ranks)
    {
        Name = string.IsNullOrWhiteSpace(name)
            ? throw new ArgumentException("Horde template name must be non-empty", nameof(name))
            : name;
        Ranks = ranks
            .Select(value => new HordeProductionRank(
                value.Rank,
                value.UnitType,
                value.Positions.ToArray()))
            .OrderBy(value => value.Rank)
            .ToArray();
        if (Ranks.Any(value => value.Rank < 1 || string.IsNullOrWhiteSpace(value.UnitType)
                || value.Positions.Count == 0))
        {
            throw new ArgumentException("Horde ranks require a positive rank, member type, and positions", nameof(ranks));
        }
        MemberCount = Ranks.Aggregate(0, (count, value) => checked(count + value.Positions.Count));
    }

    public string Name { get; }
    public IReadOnlyList<HordeProductionRank> Ranks { get; }
    public int MemberCount { get; }
}
