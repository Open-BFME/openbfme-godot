using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class LocomotorTests
{
    private static readonly IReadOnlyDictionary<string, long> SageValues =
        new Dictionary<string, long>
        {
            ["Speed"] = 55,
            ["SpeedDamaged"] = 33,
            ["TurnRate"] = 360,
            ["Acceleration"] = 1000,
            ["Braking"] = 800,
            ["MinTurnSpeed"] = 10,
            ["MaxTurnWithoutReform"] = 45,
        };

    [Theory]
    [InlineData(33, 1815, 1000, 1188, 100)]
    [InlineData(100, 11, 2, 36, 1)]
    public void SageValuesConvertToExactPerTickQuantities(
        int tickMilliseconds,
        long speedNumerator,
        long speedDenominator,
        long turnNumerator,
        long turnDenominator)
    {
        var locomotor = new Locomotor(SageValues, tickMilliseconds);

        Assert.Equal(Fixed64.FromFraction(speedNumerator, speedDenominator), locomotor.Speed);
        Assert.Equal(Fixed64.FromFraction(33L * tickMilliseconds, 1000), locomotor.SpeedDamaged);
        Assert.Equal(Fixed64.FromFraction(turnNumerator, turnDenominator), locomotor.TurnRate);
        Assert.Equal(Fixed64.FromFraction(1000L * tickMilliseconds * tickMilliseconds, 1_000_000), locomotor.Acceleration);
        Assert.Equal(Fixed64.FromFraction(800L * tickMilliseconds * tickMilliseconds, 1_000_000), locomotor.Braking);
        Assert.Equal(Fixed64.FromFraction(10L * tickMilliseconds, 1000), locomotor.MinTurnSpeed);
        Assert.Equal(Fixed64.FromInt(45), locomotor.MaxTurnWithoutReform);
    }

    [Fact]
    public void ModuleSpecUsesTheSameSageFieldNames()
    {
        var fromDictionary = new Locomotor(SageValues, 33);
        var fromSpec = new Locomotor(new ModuleSpec(LocomotorModule.TypeName, SageValues), 33);
        Assert.Equal(fromDictionary, fromSpec);
    }
}
