namespace OpenBfme.Sim;

/// <summary>Authored model-space production create point, natural rally point, and exit delay.</summary>
[SageModule("QueueProductionExitUpdate", ModuleTier.Cosmetic)]
public sealed class QueueProductionExitUpdateModule : ModuleBase
{
    public const string TypeName = "QueueProductionExitUpdate";
    private int _delayTicksRemaining;

    public QueueProductionExitUpdateModule(ModuleSpec spec) : base(spec) { }

    public bool CanExit => _delayTicksRemaining <= 0;
    public int DelayTicksRemaining => _delayTicksRemaining;

    public FixedVector2 ExitPosition(GameObject producer) => producer.Position
        + Rotate(ReadVector("UnitCreatePoint"), producer.HeadingRadians);

    public FixedVector2 NaturalRallyPoint(GameObject producer) => producer.Position
        + Rotate(ReadVector("NaturalRallyPoint"), producer.HeadingRadians);

    public void NotifyExit(SimWorld world)
    {
        var milliseconds = Math.Max(0, Spec.GetLong("ExitDelay", 0));
        _delayTicksRemaining = milliseconds == 0
            ? 0
            : checked((int)Math.Max(1,
                (milliseconds + world.TickMilliseconds - 1) / world.TickMilliseconds));
    }

    public override void OnUpdate(SimWorld world, GameObject self)
    {
        if (_delayTicksRemaining > 0) _delayTicksRemaining--;
    }

    public override void WriteState(CanonicalWriter writer) => writer.WriteInt(_delayTicksRemaining);
    public override void ReadState(CanonicalReader reader) => _delayTicksRemaining = reader.ReadInt();

    private FixedVector2 ReadVector(string name)
    {
        if (Spec.Data.ContainsKey(name + "XRaw") || Spec.Data.ContainsKey(name + "YRaw"))
        {
            return new FixedVector2(
                Spec.GetFixed(name + "XRaw", Fixed64.Zero),
                Spec.GetFixed(name + "YRaw", Fixed64.Zero));
        }
        var text = Spec.GetString(name, "");
        return new FixedVector2(ReadAxis(text, 'X'), ReadAxis(text, 'Y'));
    }

    private static FixedVector2 Rotate(FixedVector2 value, Fixed64 heading) =>
        heading == Fixed64.Zero ? value : FixedAngles.Rotate(value, heading);

    private static Fixed64 ReadAxis(string text, char axis)
    {
        foreach (var token in text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (token.Length > 2 && char.ToUpperInvariant(token[0]) == axis && token[1] == ':')
                return ParseDecimal(token[2..]);
        }
        return Fixed64.Zero;
    }

    private static Fixed64 ParseDecimal(string text)
    {
        var negative = text.Length > 0 && text[0] == '-';
        if (negative || (text.Length > 0 && text[0] == '+')) text = text[1..];
        var pieces = text.Split('.', 2);
        var whole = pieces[0].Length == 0 ? 0 : long.Parse(pieces[0]);
        var fraction = pieces.Length == 1 || pieces[1].Length == 0 ? 0 : long.Parse(pieces[1]);
        var denominator = 1L;
        if (pieces.Length == 2)
            for (var index = 0; index < pieces[1].Length; index++) denominator = checked(denominator * 10);
        var numerator = checked(whole * denominator + fraction);
        if (negative) numerator = -numerator;
        return Fixed64.FromFraction(numerator, denominator);
    }
}
