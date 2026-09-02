namespace OpenBfme.Sim;

/// <summary>Deterministic Q32.32 angle helpers and CORDIC rotation.</summary>
internal static class FixedAngles
{
    public static readonly Fixed64 Pi = Fixed64.FromRaw(13_493_037_705L);
    public static readonly Fixed64 TwoPi = Fixed64.FromRaw(26_986_075_410L);
    public static readonly Fixed64 HalfPi = Fixed64.FromRaw(6_746_518_853L);
    public static readonly Fixed64 QuarterPi = Fixed64.FromRaw(3_373_259_426L);

    private static readonly long[] AtanRaw =
    {
        3_373_259_426L, 1_991_351_318L, 1_052_175_346L, 534_100_635L,
        268_086_748L, 134_174_063L, 67_103_403L, 33_553_749L,
        16_777_131L, 8_388_597L, 4_194_303L, 2_097_152L,
        1_048_576L, 524_288L, 262_144L, 131_072L,
        65_536L, 32_768L, 16_384L, 8_192L,
        4_096L, 2_048L, 1_024L, 512L,
        256L, 128L, 64L, 32L,
        16L, 8L, 4L, 2L,
    };

    private const long CordicGainInverseRaw = 2_608_131_496L;

    public static Fixed64 DegreesToRadians(Fixed64 degrees) =>
        degrees * Pi / Fixed64.FromInt(180);

    public static Fixed64 Normalize(Fixed64 angle)
    {
        var raw = angle.Raw % TwoPi.Raw;
        if (raw > Pi.Raw) raw -= TwoPi.Raw;
        if (raw < -Pi.Raw) raw += TwoPi.Raw;
        return Fixed64.FromRaw(raw);
    }

    public static Fixed64 ShortestDelta(Fixed64 from, Fixed64 to) => Normalize(to - from);

    public static Fixed64 TurnTowards(Fixed64 current, Fixed64 target, Fixed64 cap)
    {
        var delta = ShortestDelta(current, target);
        var step = Fixed64.Clamp(delta, -cap, cap);
        return Normalize(current + step);
    }

    public static Fixed64 ForDirection(sbyte x, sbyte y) => (x, y) switch
    {
        (1, 0) => Fixed64.Zero,
        (1, 1) => QuarterPi,
        (0, 1) => HalfPi,
        (-1, 1) => Pi - QuarterPi,
        (-1, 0) => Pi,
        (-1, -1) => -Pi + QuarterPi,
        (0, -1) => -HalfPi,
        (1, -1) => -QuarterPi,
        _ => Fixed64.Zero,
    };

    public static (Fixed64 Cos, Fixed64 Sin) SinCos(Fixed64 angle)
    {
        var normalized = Normalize(angle);
        var negate = false;
        if (normalized > HalfPi)
        {
            normalized -= Pi;
            negate = true;
        }
        else if (normalized < -HalfPi)
        {
            normalized += Pi;
            negate = true;
        }

        long x = CordicGainInverseRaw;
        long y = 0;
        var z = normalized.Raw;
        for (var index = 0; index < AtanRaw.Length; index++)
        {
            var priorX = x;
            if (z >= 0)
            {
                x -= y >> index;
                y += priorX >> index;
                z -= AtanRaw[index];
            }
            else
            {
                x += y >> index;
                y -= priorX >> index;
                z += AtanRaw[index];
            }
        }
        if (negate)
        {
            x = -x;
            y = -y;
        }
        return (Fixed64.FromRaw(x), Fixed64.FromRaw(y));
    }

    public static FixedVector2 Rotate(FixedVector2 value, Fixed64 angle)
    {
        var (cos, sin) = SinCos(angle);
        return new FixedVector2(
            value.X * cos - value.Y * sin,
            value.X * sin + value.Y * cos);
    }
}
