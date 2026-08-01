namespace OpenBfme.Sim;

/// <summary>
/// PCG32 generator. The stream is part of authoritative state: identical seeds and
/// identical call sequences yield identical values on every platform, and the
/// internal state serializes with the snapshot.
/// </summary>
public sealed class DeterministicRandom
{
    private const ulong Multiplier = 6364136223846793005UL;

    private ulong _state;
    private readonly ulong _increment;

    public DeterministicRandom(ulong seed, ulong sequence = 0)
    {
        _increment = (sequence << 1) | 1UL;
        _state = 0;
        NextUInt32();
        _state += seed;
        NextUInt32();
    }

    private DeterministicRandom(ulong state, ulong increment, bool restored)
    {
        _ = restored;
        _state = state;
        _increment = increment;
    }

    public uint NextUInt32()
    {
        var oldState = _state;
        _state = unchecked(oldState * Multiplier + _increment);
        var xorShifted = (uint)(((oldState >> 18) ^ oldState) >> 27);
        var rotation = (int)(oldState >> 59);
        return (xorShifted >> rotation) | (xorShifted << (-rotation & 31));
    }

    /// <summary>Uniform value in [0, exclusiveUpperBound) without modulo bias.</summary>
    public uint NextBelow(uint exclusiveUpperBound)
    {
        if (exclusiveUpperBound == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(exclusiveUpperBound));
        }
        var threshold = (uint)(-exclusiveUpperBound) % exclusiveUpperBound;
        while (true)
        {
            var candidate = NextUInt32();
            if (candidate >= threshold)
            {
                return candidate % exclusiveUpperBound;
            }
        }
    }

    public (ulong State, ulong Increment) Serialize() => (_state, _increment);

    public static DeterministicRandom Deserialize(ulong state, ulong increment) =>
        new(state, increment, restored: true);
}
