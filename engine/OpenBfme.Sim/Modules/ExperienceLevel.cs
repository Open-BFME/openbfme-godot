namespace OpenBfme.Sim;

/// <summary>
/// Experience / rank state for battalions (retail experiencelevels.ini ladder).
/// Level starts at 1. GrantExperience applies awards; when RequiredExperience
/// thresholds for the next rank are met, Level increases up to LevelCap.
/// Design data: LevelCap (default 10), RequiredExperience:N keys for level N+1
/// thresholds (optional; GrantLevels can force ranks without XP tables).
/// </summary>
[SageModule("ExperienceLevel", ModuleTier.Structural)]
public sealed class ExperienceLevelModule : ModuleBase
{
    public const string TypeName = "ExperienceLevel";

    private readonly long _levelCap;
    private readonly SortedDictionary<int, long> _requiredForLevel = new();

    public int Level { get; private set; }
    public long Experience { get; private set; }

    public ExperienceLevelModule(ModuleSpec spec) : base(spec)
    {
        _levelCap = Math.Max(1, spec.GetLong("LevelCap", 10));
        Level = (int)Math.Clamp(spec.GetLong("InitialLevel", 1), 1, _levelCap);
        foreach (var pair in spec.Data)
        {
            if (pair.Key.StartsWith("RequiredExperience:", StringComparison.Ordinal)
                && int.TryParse(pair.Key.AsSpan("RequiredExperience:".Length), out var lvl)
                && lvl > 1)
            {
                _requiredForLevel[lvl] = Math.Max(0, pair.Value);
            }
        }
    }

    public void GrantExperience(long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount));
        }
        Experience += amount;
        while (Level < _levelCap)
        {
            var next = _requiredForLevel.FirstOrDefault(pair => pair.Key > Level);
            if (next.Key == 0 || Experience < next.Value)
            {
                break;
            }
            Level = next.Key;
        }
    }

    /// <summary>Force-apply LevelsToGain from LevelUpUpgrade (Basic Training).</summary>
    public void GrantLevels(int levelsToGain)
    {
        if (levelsToGain < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(levelsToGain));
        }
        Level = (int)Math.Min(_levelCap, Level + levelsToGain);
    }

    public override void WriteState(CanonicalWriter writer)
    {
        writer.WriteInt(Level);
        writer.WriteLong(Experience);
    }

    public override void ReadState(CanonicalReader reader)
    {
        Level = reader.ReadInt();
        Experience = reader.ReadLong();
    }
}
