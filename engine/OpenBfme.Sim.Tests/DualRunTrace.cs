using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace OpenBfme.Sim.Tests;

/// <summary>
/// Reader for the tier-1 dual-run semantic trace written by the GDScript
/// runner game/tests/retail_dualrun_trace_runner.gd (schema
/// "openbfme.dualrun-trace"). Scalar constants that feed simulation math
/// (speed, tick seconds, coordinates) are read as exact base-10 rationals —
/// raw JSON text through decimal.Parse, never GetDouble — mirroring the
/// PackTemplateLoader float-avoidance strategy, so the mirrored Fixed64
/// constants are bit-exact. Metrics used only for tolerance reporting
/// (recorded positions) are exposed as double.
/// </summary>
public sealed class DualRunTrace
{
    public const string ExpectedSchema = "openbfme.dualrun-trace";

    public int TotalTicks { get; private init; }
    public int SampleInterval { get; private init; }
    public long StartingResources { get; private init; }
    public long FarmPayoutIntervalTicks { get; private init; }
    public long FarmIncomePerPayout { get; private init; }
    public string UnitType { get; private init; } = "";
    public long UnitCost { get; private init; }
    public long UnitBuildTicks { get; private init; }
    public long UnitMemberHealth { get; private init; }
    public long UnitMemberCount { get; private init; }
    /// <summary>Recorded battalion speed in units/second, as an exact rational.</summary>
    public (long Numerator, long Denominator) SpeedUnitsPerSecond { get; private init; }
    /// <summary>Recorded seconds per simulation tick, as an exact rational.</summary>
    public (long Numerator, long Denominator) TickSeconds { get; private init; }
    public IReadOnlyList<TraceStructure> Structures { get; private init; } = Array.Empty<TraceStructure>();
    public IReadOnlyList<TraceEntity> InitialEntities { get; private init; } = Array.Empty<TraceEntity>();
    public IReadOnlyList<TraceCommand> Commands { get; private init; } = Array.Empty<TraceCommand>();
    public IReadOnlyList<TraceSample> Samples { get; private init; } = Array.Empty<TraceSample>();

    /// <summary>Per-tick movement as an exact Fixed64: speed (u/s) x tick seconds (s/tick).</summary>
    public Fixed64 SpeedPerTick()
    {
        var numerator = checked(SpeedUnitsPerSecond.Numerator * TickSeconds.Numerator);
        var denominator = checked(SpeedUnitsPerSecond.Denominator * TickSeconds.Denominator);
        return Fixed64.FromFraction(numerator, denominator);
    }

    public static DualRunTrace Load(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        if (root.GetProperty("schema").GetString() != ExpectedSchema)
        {
            throw new InvalidDataException($"trace schema is not '{ExpectedSchema}'");
        }
        var constants = root.GetProperty("constants");
        var unit = constants.GetProperty("unit");

        var structures = new List<TraceStructure>();
        foreach (var row in constants.GetProperty("structures").EnumerateArray())
        {
            structures.Add(new TraceStructure(
                ReadLong(row.GetProperty("id")),
                (int)ReadLong(row.GetProperty("team")),
                row.GetProperty("structure_kind").GetString()!,
                ReadFixed(row.GetProperty("x")),
                ReadFixed(row.GetProperty("y")),
                ReadLong(row.GetProperty("maximum_health")),
                ReadLong(row.GetProperty("income_per_payout"))));
        }

        var initialEntities = new List<TraceEntity>();
        foreach (var row in root.GetProperty("initial").GetProperty("entities").EnumerateArray())
        {
            initialEntities.Add(new TraceEntity(
                ReadLong(row.GetProperty("id")),
                (int)ReadLong(row.GetProperty("team")),
                ReadFixed(row.GetProperty("x")),
                ReadFixed(row.GetProperty("y")),
                ReadLong(row.GetProperty("health"))));
        }

        var commands = new List<TraceCommand>();
        foreach (var row in root.GetProperty("commands").EnumerateArray())
        {
            var args = row.GetProperty("args");
            var type = row.GetProperty("type").GetString()!;
            var command = new TraceCommand(
                (int)ReadLong(row.GetProperty("tick")),
                (int)ReadLong(row.GetProperty("team")),
                (int)ReadLong(row.GetProperty("seq")),
                type,
                row.TryGetProperty("out_of_band", out var oob) && oob.GetBoolean());
            switch (type)
            {
                case "issue_move":
                    command = command with
                    {
                        TargetIds = args.GetProperty("ids").EnumerateArray().Select(ReadLong).ToArray(),
                        DestinationX = ReadFixed(args.GetProperty("destination").GetProperty("x")),
                        DestinationY = ReadFixed(args.GetProperty("destination").GetProperty("y")),
                    };
                    break;
                case "queue_unit":
                    command = command with
                    {
                        ProducerId = ReadLong(args.GetProperty("producer")),
                        UnitType = args.GetProperty("unit_type").GetString(),
                    };
                    break;
                case "grant_resources":
                    command = command with { Amount = ReadLong(args.GetProperty("amount")) };
                    break;
                default:
                    throw new InvalidDataException($"trace command type '{type}' has no replay mapping");
            }
            commands.Add(command);
        }

        var samples = new List<TraceSample>();
        foreach (var row in root.GetProperty("samples").EnumerateArray())
        {
            var resources = new SortedDictionary<int, long>();
            foreach (var teamProperty in row.GetProperty("team_resources").EnumerateObject())
            {
                resources.Add(int.Parse(teamProperty.Name, CultureInfo.InvariantCulture), ReadLong(teamProperty.Value));
            }
            var entities = new List<TraceSampleEntity>();
            foreach (var entity in row.GetProperty("entities").EnumerateArray())
            {
                entities.Add(new TraceSampleEntity(
                    ReadLong(entity.GetProperty("id")),
                    (int)ReadLong(entity.GetProperty("team")),
                    entity.GetProperty("x").GetDouble(),
                    entity.GetProperty("y").GetDouble(),
                    ReadLong(entity.GetProperty("health"))));
            }
            samples.Add(new TraceSample(
                (int)ReadLong(row.GetProperty("tick")),
                resources,
                (int)ReadLong(row.GetProperty("entity_count")),
                entities));
        }

        return new DualRunTrace
        {
            TotalTicks = (int)ReadLong(root.GetProperty("total_ticks")),
            SampleInterval = (int)ReadLong(root.GetProperty("sample_interval")),
            StartingResources = ReadLong(constants.GetProperty("starting_resources")),
            FarmPayoutIntervalTicks = ReadLong(constants.GetProperty("farm_payout_interval_ticks")),
            FarmIncomePerPayout = ReadLong(constants.GetProperty("farm_income_per_payout")),
            UnitType = unit.GetProperty("unit_type").GetString()!,
            UnitCost = ReadLong(unit.GetProperty("cost")),
            UnitBuildTicks = ReadLong(unit.GetProperty("build_ticks")),
            UnitMemberHealth = ReadLong(unit.GetProperty("member_health")),
            UnitMemberCount = ReadLong(unit.GetProperty("member_count")),
            SpeedUnitsPerSecond = ReadFraction(unit.GetProperty("speed_units_per_second")),
            TickSeconds = ReadFraction(constants.GetProperty("tick_seconds")),
            Structures = structures,
            InitialEntities = initialEntities,
            Commands = commands,
            Samples = samples,
        };
    }

    private static long ReadLong(JsonElement element)
    {
        var (numerator, denominator) = ReadFraction(element);
        if (denominator != 1)
        {
            throw new InvalidDataException($"expected an integer, found {element.GetRawText()}");
        }
        return numerator;
    }

    private static Fixed64 ReadFixed(JsonElement element)
    {
        var (numerator, denominator) = ReadFraction(element);
        return Fixed64.FromFraction(numerator, denominator);
    }

    /// <summary>
    /// Exact rational read of a JSON number: raw text -> decimal (exact
    /// base-10) -> reduced integer fraction. Same discipline as
    /// PackTemplateLoader.TryReadFraction; no float/double on this path.
    /// </summary>
    private static (long Numerator, long Denominator) ReadFraction(JsonElement element)
    {
        if (element.ValueKind != JsonValueKind.Number)
        {
            throw new InvalidDataException($"expected a JSON number, found {element.ValueKind}");
        }
        var value = decimal.Parse(element.GetRawText(), NumberStyles.Float, CultureInfo.InvariantCulture);
        var bits = decimal.GetBits(value);
        var scale = (bits[3] >> 16) & 0xFF;
        var negative = (bits[3] & unchecked((int)0x80000000)) != 0;
        var magnitude = ((BigInteger)(uint)bits[2] << 64)
            | ((BigInteger)(uint)bits[1] << 32)
            | (uint)bits[0];
        var num = negative ? -magnitude : magnitude;
        var den = BigInteger.Pow(10, scale);
        if (!num.IsZero)
        {
            var gcd = BigInteger.GreatestCommonDivisor(BigInteger.Abs(num), den);
            num /= gcd;
            den /= gcd;
        }
        else
        {
            den = BigInteger.One;
        }
        if (num > long.MaxValue || num < long.MinValue || den > long.MaxValue)
        {
            throw new InvalidDataException($"JSON number {element.GetRawText()} does not fit the long rational");
        }
        return ((long)num, (long)den);
    }
}

public sealed record TraceStructure(long Id, int Team, string Kind, Fixed64 X, Fixed64 Y, long MaximumHealth, long IncomePerPayout);

public sealed record TraceEntity(long Id, int Team, Fixed64 X, Fixed64 Y, long Health);

public sealed record TraceCommand(int Tick, int Team, int Seq, string Type, bool OutOfBand)
{
    public IReadOnlyList<long>? TargetIds { get; init; }
    public Fixed64 DestinationX { get; init; }
    public Fixed64 DestinationY { get; init; }
    public long ProducerId { get; init; }
    public string? UnitType { get; init; }
    public long Amount { get; init; }
}

public sealed record TraceSample(int Tick, IReadOnlyDictionary<int, long> TeamResources, int EntityCount, IReadOnlyList<TraceSampleEntity> Entities);

public sealed record TraceSampleEntity(long Id, int Team, double X, double Y, long Health);
