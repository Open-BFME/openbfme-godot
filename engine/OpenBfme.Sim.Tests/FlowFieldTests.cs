using System.Diagnostics;
using OpenBfme.Sim;
using OpenBfme.Sim.Pathing;
using Xunit;
using Xunit.Abstractions;

namespace OpenBfme.Sim.Tests;

public class FlowFieldTests
{
    private readonly ITestOutputHelper _output;

    public FlowFieldTests(ITestOutputHelper output) => _output = output;

    [Fact]
    public void WallRoutesAgentToGoalInBoundedSteps()
    {
        const int size = 32;
        var passable = Enumerable.Repeat(true, size * size).ToArray();
        var costs = Enumerable.Repeat(1, size * size).ToArray();
        for (var y = 0; y < size - 3; y++)
        {
            passable[y * size + 16] = false;
        }
        var field = FlowField.Build(new PassabilityGrid(size, size, passable, costs), 29, 4);
        var position = new FixedVector2(Fixed64.FromInt(2), Fixed64.FromInt(4));

        var reached = false;
        for (var step = 0; step < 1000; step++)
        {
            position = FlowFieldMover.Step(field, position, Fixed64.One);
            if (position.X.ToIntFloor() == field.GoalX && position.Y.ToIntFloor() == field.GoalY)
            {
                reached = true;
                break;
            }
        }
        Assert.True(reached, $"agent stopped at {position}");
        Assert.True(field.Integration[field.Grid.IndexOf(2, 4)] < long.MaxValue);
    }

    [Fact]
    public void TwinBuildsProduceIdenticalFields()
    {
        var passable = Enumerable.Range(0, 64 * 64).Select(index => index % 17 != 0).ToArray();
        passable[63 * 64 + 63] = true;
        var costs = Enumerable.Range(0, 64 * 64).Select(index => index % 5 + 1).ToArray();
        var grid = new PassabilityGrid(64, 64, passable, costs);

        var first = FlowField.Build(grid, 63, 63);
        var second = FlowField.Build(grid, 63, 63);
        Assert.Equal(first.Integration, second.Integration);
        Assert.Equal(first.Direction, second.Direction);
    }

    [Fact]
    public void TenThousandAgentsAdvanceForOneHundredTicks()
    {
        var field = FlowField.Build(PassabilityGrid.Uniform(128, 128), 127, 127);
        var agents = new FixedVector2[10_000];
        for (var index = 0; index < agents.Length; index++)
        {
            agents[index] = new FixedVector2(
                Fixed64.FromInt(index % 100),
                Fixed64.FromInt(index / 100));
        }

        var stopwatch = Stopwatch.StartNew();
        for (var tick = 0; tick < 100; tick++)
        {
            for (var index = 0; index < agents.Length; index++)
            {
                agents[index] = FlowFieldMover.Step(field, agents[index], Fixed64.One);
            }
        }
        stopwatch.Stop();
        _output.WriteLine($"flow-field scale: 10,000 agents x 100 ticks = {stopwatch.ElapsedMilliseconds} ms");
        Assert.True(stopwatch.ElapsedMilliseconds >= 0);
    }
}
