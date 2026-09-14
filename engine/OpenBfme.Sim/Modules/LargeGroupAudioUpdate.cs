namespace OpenBfme.Sim;

/// <summary>
/// Client-side large-group voice weighting (retail: when a command group is
/// ordered, one member vocalizes for its audio category). The authored fields
/// are Key — the category token list — and UnitWeight, the member's share of
/// its category. The sim-side effect is the deterministic selection the client
/// needs: dominant category by summed weight (ordinal-first key on ties), then
/// a weighted walk over ascending member ids. The caller supplies the pick as
/// an integer taken modulo the group weight, so the roll stays a presentation
/// sequence and never consumes the gameplay RNG or moves the state hash.
/// </summary>
[SageModule("LargeGroupAudioUpdate", ModuleTier.Cosmetic)]
public sealed class LargeGroupAudioUpdateModule : ModuleBase
{
    public const string TypeName = "LargeGroupAudioUpdate";

    private readonly string _categoryKey;
    private readonly int _unitWeight;

    public LargeGroupAudioUpdateModule(ModuleSpec spec) : base(spec)
    {
        _categoryKey = string.Join(' ', spec.GetString("Key", "")
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
        _unitWeight = checked((int)Math.Max(1, spec.GetLong("UnitWeight", 1)));
    }

    /// <summary>Category tokens joined single-spaced ("" when unauthored).</summary>
    public string CategoryKey => _categoryKey;

    /// <summary>The member's share of its category weight, minimum 1.</summary>
    public int UnitWeight => _unitWeight;

    /// <summary>The chosen category, its summed weight, and the selected member.</summary>
    public readonly record struct LargeGroupAudioSelection(
        string CategoryKey, int GroupWeight, int ObjectId);

    /// <summary>
    /// Deterministic spokesperson selection over a commanded group. Dead,
    /// dying, module-less, key-less, and unknown ids contribute nothing. The
    /// pick wraps modulo the winning category's summed weight, so any unsigned
    /// presentation roll selects exactly one member; returns null when no
    /// object in the group carries a keyed module.
    /// </summary>
    public static LargeGroupAudioSelection? SelectWeightedSpeaker(
        SimWorld world, IEnumerable<int> objectIds, ulong pick)
    {
        var groups = new SortedDictionary<string, Group>(StringComparer.Ordinal);
        foreach (var id in objectIds)
        {
            if (!world.Objects.TryGetValue(id, out var member)
                || member.IsDead || member.IsDying
                || member.FindModule<LargeGroupAudioUpdateModule>() is not { } audio
                || audio._categoryKey.Length == 0)
            {
                continue;
            }
            if (!groups.TryGetValue(audio._categoryKey, out var group))
            {
                group = new Group();
                groups.Add(audio._categoryKey, group);
            }
            group.Weight += audio._unitWeight;
            group.MemberIds.Add(id);
        }
        if (groups.Count == 0) return null;

        var bestKey = groups.Keys.First();
        foreach (var key in groups.Keys)
        {
            if (groups[key].Weight > groups[bestKey].Weight) bestKey = key;
        }
        var winners = groups[bestKey];
        winners.MemberIds.Sort();
        var target = pick % (ulong)winners.Weight;
        var cursor = 0;
        var selected = winners.MemberIds[0];
        foreach (var id in winners.MemberIds)
        {
            cursor += world.Objects[id].FindModule<LargeGroupAudioUpdateModule>()!._unitWeight;
            if ((ulong)cursor > target)
            {
                selected = id;
                break;
            }
        }
        return new LargeGroupAudioSelection(bestKey, winners.Weight, selected);
    }

    private sealed class Group
    {
        public int Weight;
        public readonly List<int> MemberIds = new();
    }
}
