namespace OpenBfme.Sim;

/// <summary>
/// CastleBehavior-shaped fortress unpack: on first update, spawn engine
/// every BSE piece at its authored offset and relative angle. Design string
/// keys PieceTemplate:N and exact Q32.32 OffsetXRaw:N / OffsetYRaw:N /
/// OffsetZRaw:N / AngleRadiansRaw:N. Legacy CitadelTemplate/PadTemplate keys
/// remain readable for old fixtures, but retail contracts use PieceTemplate.
/// </summary>
[SageModule("CastleBehavior", ModuleTier.Structural)]
public sealed class CastleBehaviorModule : ModuleBase
{
    public const string TypeName = "CastleBehavior";

    private readonly List<(
        string Template,
        Fixed64 Ox,
        Fixed64 Oy,
        Fixed64 Oz,
        Fixed64 Angle)> _pieces = new();
    private bool _unpacked;

    public CastleBehaviorModule(ModuleSpec spec) : base(spec)
    {
        var authoredCount = spec.GetLong("PieceCount", -1);
        if (authoredCount > 64)
        {
            throw new ArgumentException("CastleBehavior PieceCount exceeds 64");
        }
        var limit = authoredCount >= 0 ? (int)authoredCount : 64;
        for (var i = 0; i < limit; i++)
        {
            var template = spec.GetString($"PieceTemplate:{i}", "");
            if (string.IsNullOrEmpty(template))
            {
                template = spec.GetString($"PadTemplate:{i}", "");
            }
            if (string.IsNullOrEmpty(template))
            {
                if (authoredCount >= 0)
                {
                    throw new ArgumentException(
                        $"CastleBehavior piece {i} is missing its template");
                }
                continue;
            }
            var ox = Fixed64.FromRaw(spec.GetLong($"OffsetXRaw:{i}", 0));
            var oy = Fixed64.FromRaw(spec.GetLong($"OffsetYRaw:{i}", 0));
            var oz = Fixed64.FromRaw(spec.GetLong($"OffsetZRaw:{i}", 0));
            var angle = Fixed64.FromRaw(spec.GetLong($"AngleRadiansRaw:{i}", 0));
            _pieces.Add((template, ox, oy, oz, angle));
        }
        var legacyCitadel = spec.GetString("CitadelTemplate", "");
        if (!string.IsNullOrEmpty(legacyCitadel))
        {
            _pieces.Add((legacyCitadel, Fixed64.Zero, Fixed64.Zero,
                Fixed64.Zero, Fixed64.Zero));
        }
        if (_pieces.Count == 0)
        {
            throw new ArgumentException("CastleBehavior requires at least one BSE piece");
        }
    }

    public bool HasUnpacked => _unpacked;

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_unpacked || self.IsDead || self.IsDying)
        {
            return;
        }
        foreach (var (template, ox, oy, oz, angle) in _pieces)
        {
            var position = new FixedVector2(
                self.Position.X + ox,
                self.Position.Y + oy);
            world.SpawnObject(
                template,
                self.Team,
                position,
                self.Elevation + oz,
                self.HeadingRadians + angle);
        }
        _unpacked = true;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteBool(_unpacked);
    }

    public override void ReadState(CanonicalReader reader)
    {
        _unpacked = reader.ReadBool();
    }
}
