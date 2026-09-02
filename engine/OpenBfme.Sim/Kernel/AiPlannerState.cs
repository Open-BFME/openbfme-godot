namespace OpenBfme.Sim;

public enum AiDifficulty : byte
{
    Easy = 1,
    Medium = 2,
    Hard = 3,
    Brutal = 4,
}

public enum AiPhase : byte
{
    Economy = 1,
    Regroup = 2,
    Defend = 3,
    Attack = 4,
    Retreat = 5,
}

internal sealed class AiPlayerState
{
    public int PlayerIndex { get; private init; }
    public int Seat { get; private init; }
    public int Team { get; private init; }
    public string Faction { get; private init; } = "";
    public AiDifficulty Difficulty { get; private init; }
    public Fixed64 Handicap { get; private init; }
    public bool Enabled { get; set; } = true;
    public AiPhase Phase { get; set; } = AiPhase.Economy;
    public int LastPlanTick { get; set; }
    public int TargetObjectId { get; set; }
    public bool Retreating { get; set; }
    public FixedVector2 RallyPoint { get; set; }
    public long PreviousArmyHealth { get; set; }
    public long PreviousEnemyHealth { get; set; }
    public long LastResources { get; set; }
    public long IncomeRate { get; set; }
    public int IncomeSampleTick { get; set; }
    public int CommandSequence { get; set; }
    public int BundleSequence { get; set; }
    public bool TieRandomInitialized { get; set; }
    public ulong TieRandomState { get; set; }
    public ulong TieRandomIncrement { get; set; }
    public SortedDictionary<string, int> CommandCounts { get; } = new(StringComparer.Ordinal);

    public static AiPlayerState Create(int playerIndex, MatchLaunchPlayer player) => new()
    {
        PlayerIndex = playerIndex,
        Seat = player.Seat,
        Team = player.Team,
        Faction = NormalizeFaction(player.Faction),
        Difficulty = ParseDifficulty(player.AiDifficulty),
        Handicap = player.Handicap ?? Fixed64.One,
    };

    public int PlanIntervalTicks(int tickMilliseconds)
    {
        var baseTicks = Difficulty switch
        {
            AiDifficulty.Brutal => 15,
            AiDifficulty.Hard => 30,
            AiDifficulty.Medium => 60,
            _ => 120,
        };
        return Math.Max(1, checked((baseTicks * 33 + tickMilliseconds / 2) / tickMilliseconds));
    }

    public bool ShouldPlan(int tick, int tickMilliseconds) =>
        LastPlanTick == 0 || tick - LastPlanTick >= PlanIntervalTicks(tickMilliseconds);

    public int EconomyTarget => Difficulty switch
    {
        AiDifficulty.Brutal => 5,
        AiDifficulty.Hard => 4,
        AiDifficulty.Medium => 3,
        _ => 2,
    };

    public int AttackMarginPercent => Difficulty switch
    {
        AiDifficulty.Brutal => 100,
        AiDifficulty.Hard => 115,
        AiDifficulty.Medium => 130,
        _ => 150,
    };

    public int AttackCommandPointPercent => Difficulty switch
    {
        AiDifficulty.Brutal => 30,
        AiDifficulty.Hard => 40,
        AiDifficulty.Medium => 50,
        _ => 60,
    };

    public int RetreatHealthPercent => Difficulty switch
    {
        AiDifficulty.Brutal => 30,
        AiDifficulty.Hard => 40,
        AiDifficulty.Medium => 50,
        _ => 60,
    };

    public uint NextTieIndex(SimWorld world, uint count)
    {
        DeterministicRandom random;
        if (!TieRandomInitialized)
        {
            random = new DeterministicRandom(world.NextRandomUInt32(), world.NextRandomUInt32());
            TieRandomInitialized = true;
        }
        else
        {
            random = DeterministicRandom.Deserialize(TieRandomState, TieRandomIncrement);
        }
        var result = random.NextBelow(count);
        (TieRandomState, TieRandomIncrement) = random.Serialize();
        return result;
    }

    public void Write(CanonicalWriter writer)
    {
        writer.WriteInt(PlayerIndex);
        writer.WriteInt(Seat);
        writer.WriteInt(Team);
        writer.WriteString(Faction);
        writer.WriteByte((byte)Difficulty);
        writer.WriteFixed(Handicap);
        writer.WriteBool(Enabled);
        writer.WriteByte((byte)Phase);
        writer.WriteInt(LastPlanTick);
        writer.WriteInt(TargetObjectId);
        writer.WriteBool(Retreating);
        writer.WriteVector(RallyPoint);
        writer.WriteLong(PreviousArmyHealth);
        writer.WriteLong(PreviousEnemyHealth);
        writer.WriteLong(LastResources);
        writer.WriteLong(IncomeRate);
        writer.WriteInt(IncomeSampleTick);
        writer.WriteInt(CommandSequence);
        writer.WriteInt(BundleSequence);
        writer.WriteBool(TieRandomInitialized);
        writer.WriteLong(unchecked((long)TieRandomState));
        writer.WriteLong(unchecked((long)TieRandomIncrement));
        writer.WriteInt(CommandCounts.Count);
        foreach (var (name, count) in CommandCounts)
        {
            writer.WriteString(name);
            writer.WriteInt(count);
        }
    }

    public static AiPlayerState Read(CanonicalReader reader)
    {
        var state = new AiPlayerState
        {
            PlayerIndex = reader.ReadInt(),
            Seat = reader.ReadInt(),
            Team = reader.ReadInt(),
            Faction = reader.ReadString(),
            Difficulty = (AiDifficulty)reader.ReadByte(),
            Handicap = reader.ReadFixed(),
            Enabled = reader.ReadBool(),
            Phase = (AiPhase)reader.ReadByte(),
            LastPlanTick = reader.ReadInt(),
            TargetObjectId = reader.ReadInt(),
            Retreating = reader.ReadBool(),
            RallyPoint = reader.ReadVector(),
            PreviousArmyHealth = reader.ReadLong(),
            PreviousEnemyHealth = reader.ReadLong(),
            LastResources = reader.ReadLong(),
            IncomeRate = reader.ReadLong(),
            IncomeSampleTick = reader.ReadInt(),
            CommandSequence = reader.ReadInt(),
            BundleSequence = reader.ReadInt(),
            TieRandomInitialized = reader.ReadBool(),
            TieRandomState = unchecked((ulong)reader.ReadLong()),
            TieRandomIncrement = unchecked((ulong)reader.ReadLong()),
        };
        var count = reader.ReadInt();
        if (count < 0) throw new InvalidDataException("Negative AI command count length");
        for (var index = 0; index < count; index++) state.CommandCounts.Add(reader.ReadString(), reader.ReadInt());
        return state;
    }

    private static string NormalizeFaction(string faction) =>
        faction.StartsWith("Faction", StringComparison.Ordinal) ? faction[7..] : faction;

    private static AiDifficulty ParseDifficulty(string? value) => value switch
    {
        "brutal" => AiDifficulty.Brutal,
        "hard" => AiDifficulty.Hard,
        "medium" => AiDifficulty.Medium,
        _ => AiDifficulty.Easy,
    };
}
