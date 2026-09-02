using OpenBfme.Sim;
using OpenBfme.Sim.Pathing;
using Xunit;

namespace OpenBfme.Sim.Tests;

public sealed class MovementBoundaryTests
{
    [Fact]
    public void RuntimeMovementClampsAnOutsideTargetToTheFiniteGrid()
    {
        var template = new ObjectTemplate(
            "walker",
            new ModuleSpec[]
            {
                new(LocomotorModule.TypeName, new Dictionary<string, long>
                {
                    ["Speed"] = 30,
                    ["Acceleration"] = 900,
                    ["Braking"] = 900,
                    ["TurnRate"] = 360,
                }),
            });
        var world = new SimWorld(
            new SimConfig(new[] { template }, 23, 1, mapWidthCells: 8, mapHeightCells: 8),
            ModuleRegistry.CreateDefault(),
            33,
            PassabilityGrid.Uniform(8, 8));
        var walker = world.SpawnObject("walker", 0, At(1, 3));
        var locomotor = walker.FindModule<LocomotorModule>()!;
        locomotor.SetOrder(At(-5, 3), MoveOrderKind.AttackMove);

        world.Advance(10);

        Assert.Equal(At(1, 3), walker.Position);
        Assert.False(locomotor.HasOrder);
    }

    private static FixedVector2 At(int x, int y) =>
        new(Fixed64.FromInt(x), Fixed64.FromInt(y));
}
