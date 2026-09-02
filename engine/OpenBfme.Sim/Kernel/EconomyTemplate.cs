using System.Numerics;

namespace OpenBfme.Sim;

/// <summary>
/// Immutable economy-facing fields from an Object block. Durations remain in
/// authored milliseconds until a world resolves them against rules.tick_ms.
/// </summary>
public sealed class EconomyTemplate
{
    public EconomyTemplate(
        long buildCost = 0,
        long buildTimeMilliseconds = 0,
        long commandPoints = 0,
        IReadOnlyList<string>? commandSet = null,
        FixedVector2 productionExitOffset = default,
        long depositAmount = 0,
        long depositTimingMilliseconds = 0,
        Fixed64? crowdingMultiplier = null,
        Fixed64? sellRefundMultiplier = null,
        string buildKind = "",
        string hordeMemberTemplate = "")
    {
        if (buildCost < 0 || buildTimeMilliseconds < 0 || commandPoints < 0
            || depositAmount < 0 || depositTimingMilliseconds < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(buildCost));
        }
        BuildCost = buildCost;
        BuildTimeMilliseconds = buildTimeMilliseconds;
        CommandPoints = commandPoints;
        CommandSet = commandSet?.ToArray() ?? Array.Empty<string>();
        if (CommandSet.Any(string.IsNullOrWhiteSpace))
        {
            throw new ArgumentException("CommandSet template names must be non-empty", nameof(commandSet));
        }
        ProductionExitOffset = productionExitOffset;
        DepositAmount = depositAmount;
        DepositTimingMilliseconds = depositTimingMilliseconds;
        CrowdingMultiplier = crowdingMultiplier ?? Fixed64.One;
        SellRefundMultiplier = sellRefundMultiplier ?? Fixed64.FromFraction(1, 2);
        if (CrowdingMultiplier < Fixed64.Zero || SellRefundMultiplier < Fixed64.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(crowdingMultiplier));
        }
        BuildKind = buildKind ?? throw new ArgumentNullException(nameof(buildKind));
        HordeMemberTemplate = hordeMemberTemplate ?? throw new ArgumentNullException(nameof(hordeMemberTemplate));
    }

    public long BuildCost { get; }
    public long BuildTimeMilliseconds { get; }
    public long CommandPoints { get; }
    public IReadOnlyList<string> CommandSet { get; }
    public FixedVector2 ProductionExitOffset { get; }
    public long DepositAmount { get; }
    public long DepositTimingMilliseconds { get; }
    public Fixed64 CrowdingMultiplier { get; }
    public Fixed64 SellRefundMultiplier { get; }
    public string BuildKind { get; }
    public string HordeMemberTemplate { get; }

    public int BuildTicks(int tickMilliseconds) =>
        MillisecondsToTicks(BuildTimeMilliseconds, tickMilliseconds);

    public int DepositTicks(int tickMilliseconds) =>
        MillisecondsToTicks(DepositTimingMilliseconds, tickMilliseconds);

    public static int MillisecondsToTicks(long milliseconds, int tickMilliseconds) =>
        IniValueReader.MillisecondsToTicks(milliseconds, tickMilliseconds);

    public static EconomyTemplate Parse(ModuleSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        return new EconomyTemplate(
            Math.Max(0, spec.GetLong("BuildCost", 0)),
            Math.Max(0, spec.GetLong("BuildTime", 0)),
            Math.Max(0, spec.GetLong("CommandPoints", 0)),
            Tokens(spec.GetString("CommandSet", "")),
            new FixedVector2(
                spec.GetFixed("ProductionExitXRaw", Fixed64.Zero),
                spec.GetFixed("ProductionExitYRaw", Fixed64.Zero)),
            Math.Max(0, spec.GetLong("DepositAmount", 0)),
            Math.Max(0, spec.GetLong("DepositTiming", 0)),
            spec.GetFixed("CrowdingMultiplierRaw", Fixed64.One),
            ReadRefund(spec),
            spec.GetString("BuildKind", ""),
            spec.GetString("HordeMemberTemplate", spec.GetString("MemberTemplate", "")));
    }

    public static EconomyTemplate Parse(IReadOnlyDictionary<string, object?> row)
    {
        var fields = IniValueReader.Fields(row);
        return new EconomyTemplate(
            Math.Max(0, IniValueReader.Integer64(fields, "BuildCost")),
            Math.Max(0, IniValueReader.Milliseconds(fields, "BuildTime")),
            Math.Max(0, IniValueReader.Integer64(fields, "CommandPoints")),
            IniValueReader.Tokens(IniValueReader.Value(fields, "CommandSet")),
            new FixedVector2(
                IniValueReader.Fixed(fields, "ProductionExitX", Fixed64.Zero),
                IniValueReader.Fixed(fields, "ProductionExitY", Fixed64.Zero)),
            Math.Max(0, IniValueReader.Integer64(fields, "DepositAmount")),
            Math.Max(0, IniValueReader.Milliseconds(fields, "DepositTiming")),
            IniValueReader.Fixed(fields, "CrowdingMultiplier", Fixed64.One),
            ReadRefund(fields),
            IniValueReader.String(fields, "BuildKind", ""),
            IniValueReader.String(fields, "HordeMemberTemplate", ""));
    }

    public static EconomyTemplate FromModules(IReadOnlyList<ModuleSpec> modules)
    {
        ArgumentNullException.ThrowIfNull(modules);
        long buildCost = 0;
        long buildTime = 0;
        long commandPoints = 0;
        long depositAmount = 0;
        long depositTiming = 0;
        var exit = FixedVector2.Zero;
        var crowding = Fixed64.One;
        var refund = Fixed64.FromFraction(1, 2);
        IReadOnlyList<string> commandSet = Array.Empty<string>();
        var buildKind = "";
        var hordeMemberTemplate = "";
        foreach (var spec in modules)
        {
            if (spec.Data.ContainsKey("BuildCost")) buildCost = Math.Max(0, spec.GetLong("BuildCost", 0));
            if (spec.Data.ContainsKey("BuildTime")) buildTime = Math.Max(0, spec.GetLong("BuildTime", 0));
            if (spec.Data.ContainsKey("CommandPoints")) commandPoints = Math.Max(0, spec.GetLong("CommandPoints", 0));
            if (spec.StringData.ContainsKey("CommandSet")) commandSet = Tokens(spec.GetString("CommandSet", ""));
            if (spec.Data.ContainsKey("ProductionExitXRaw") || spec.Data.ContainsKey("ProductionExitYRaw"))
            {
                exit = new FixedVector2(
                    spec.GetFixed("ProductionExitXRaw", Fixed64.Zero),
                    spec.GetFixed("ProductionExitYRaw", Fixed64.Zero));
            }
            if (spec.Data.ContainsKey("DepositAmount")) depositAmount = Math.Max(0, spec.GetLong("DepositAmount", 0));
            if (spec.Data.ContainsKey("DepositTiming")) depositTiming = Math.Max(0, spec.GetLong("DepositTiming", 0));
            if (spec.Data.ContainsKey("CrowdingMultiplierRaw")) crowding = spec.GetFixed("CrowdingMultiplierRaw", Fixed64.One);
            if (spec.Data.ContainsKey("SellRefundMultiplierRaw") || spec.Data.ContainsKey("SellRefundPercent")) refund = ReadRefund(spec);
            if (spec.StringData.ContainsKey("BuildKind")) buildKind = spec.GetString("BuildKind", "");
            if (spec.StringData.ContainsKey("HordeMemberTemplate") || spec.StringData.ContainsKey("MemberTemplate"))
            {
                hordeMemberTemplate = spec.GetString("HordeMemberTemplate", spec.GetString("MemberTemplate", ""));
            }
        }
        return new EconomyTemplate(
            buildCost, buildTime, commandPoints, commandSet, exit,
            depositAmount, depositTiming, crowding, refund, buildKind, hordeMemberTemplate);
    }

    public static long ScaleInteger(long value, Fixed64 multiplier)
    {
        if (value < 0 || multiplier < Fixed64.Zero) throw new ArgumentOutOfRangeException(nameof(value));
        var product = (BigInteger)value * multiplier.Raw;
        var scaled = (product + Fixed64.OneRaw / 2) / Fixed64.OneRaw;
        if (scaled > long.MaxValue) throw new OverflowException("Scaled economy value exceeds Int64");
        return (long)scaled;
    }

    private static Fixed64 ReadRefund(ModuleSpec spec)
    {
        if (spec.Data.ContainsKey("SellRefundMultiplierRaw"))
        {
            return spec.GetFixed("SellRefundMultiplierRaw", Fixed64.FromFraction(1, 2));
        }
        return Fixed64.FromFraction(spec.GetLong("SellRefundPercent", 50), 100);
    }

    private static Fixed64 ReadRefund(IReadOnlyDictionary<string, object?> fields)
    {
        var multiplier = IniValueReader.Value(fields, "SellRefundMultiplier");
        if (multiplier != null) return IniValueReader.Fixed(fields, "SellRefundMultiplier");
        var percentage = IniValueReader.Value(fields, "SellRefundPercent");
        return percentage == null
            ? Fixed64.FromFraction(1, 2)
            : IniValueReader.PercentMultiplier(percentage, "SellRefundPercent");
    }

    private static IReadOnlyList<string> Tokens(string value) =>
        value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
}
