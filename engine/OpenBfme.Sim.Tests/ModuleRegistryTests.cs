using System.Reflection;
using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class ModuleRegistryTests
{
    [Fact]
    public void ReflectionRegistryFindsEveryAttributedModule()
    {
        var moduleTypes = typeof(ModuleBase).Assembly.GetTypes()
            .Where(type => !type.IsAbstract && typeof(ModuleBase).IsAssignableFrom(type))
            .OrderBy(type => type.Name, StringComparer.Ordinal)
            .ToArray();
        var registry = ModuleRegistry.CreateDefault();
        var kernelModuleCount = 0;

        foreach (var type in moduleTypes)
        {
            var attribute = type.GetCustomAttribute<SageModuleAttribute>();
            Assert.NotNull(attribute);
            if (attribute!.Kernel)
            {
                kernelModuleCount++;
            }
            Assert.Contains(attribute.Name, registry.RegisteredNames);
        }
        Assert.Equal(6, kernelModuleCount);
        Assert.Equal(moduleTypes.Length, registry.RegisteredNames.Count);
    }

    [Fact]
    public void UnknownStructuralFailsAndCosmeticRecordsQueryableGap()
    {
        var config = new SimConfig(new[]
        {
            new ObjectTemplate("structural", new[]
            {
                new ModuleSpec("MissingStructuralBehavior", tier: ModuleTier.Structural),
            }),
            new ObjectTemplate("cosmetic", new[]
            {
                new ModuleSpec("MissingDraw", tier: ModuleTier.Cosmetic),
            }),
        }, 1, 1);
        var world = new SimWorld(config, ModuleRegistry.CreateDefault());

        var exception = Assert.Throws<ModuleLoadException>(() =>
            world.SpawnObject("structural", 0, FixedVector2.Zero));
        Assert.Contains("MissingStructuralBehavior", exception.Message, StringComparison.Ordinal);

        world.SpawnObject("cosmetic", 0, FixedVector2.Zero);
        Assert.Equal(1, world.ModuleGaps["MissingDraw"]);
    }
}
