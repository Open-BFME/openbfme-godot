namespace OpenBfme.Sim;

public sealed record SnapshotHorde(
    int Id,
    int Owner,
    int TemplateIndex,
    IReadOnlyList<int> Members,
    int Formation);

public sealed record SnapshotPlayer(
    int Index,
    long Resources,
    long CommandPoints,
    long CommandPointsMax,
    long PowerPoints);

public sealed record SimEvent(
    string Kind,
    int Object,
    int? Target = null,
    Fixed64? Amount = null,
    string? Name = null)
{
    private static readonly string[] AllowedKinds =
    {
        "spawn", "death", "damage", "fire", "ability", "build_start",
        "build_done", "capture", "upgrade", "sound",
    };

    public void Validate()
    {
        if (!AllowedKinds.Contains(Kind, StringComparer.Ordinal))
        {
            throw new ArgumentException($"Unknown snapshot event kind '{Kind}'", nameof(Kind));
        }
        if (Object < 0 || Target < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(Object), "Event object ids must be non-negative");
        }
    }
}
