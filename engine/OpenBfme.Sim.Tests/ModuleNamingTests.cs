using System.Text.Json;
using System.Text.RegularExpressions;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class ModuleNamingTests
{
    [Fact]
    public void ModuleDirectoriesSeparateExactSageNamesFromKernelTypes()
    {
        var repoRoot = FindRepoRoot();
        using var census = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(repoRoot, "game", "data", "retail_module_census.json")));
        var censusNames = census.RootElement.GetProperty("members")
            .EnumerateArray()
            .Select(member => member.GetProperty("name").GetString()!)
            .ToHashSet(StringComparer.Ordinal);

        var modulesDirectory = Path.Combine(repoRoot, "engine", "OpenBfme.Sim", "Modules");
        var moduleFiles = Directory.GetFiles(modulesDirectory, "*.cs", SearchOption.TopDirectoryOnly)
            .OrderBy(path => path, StringComparer.Ordinal)
            .ToArray();
        Assert.NotEmpty(moduleFiles);
        foreach (var file in moduleFiles)
        {
            var (name, source) = ReadAttributedName(file);
            Assert.Contains(name, censusNames);
            Assert.Equal(name, Path.GetFileNameWithoutExtension(file));
            Assert.DoesNotContain("kernel: true", source, StringComparison.Ordinal);
        }

        var kernelDirectory = Path.Combine(repoRoot, "engine", "OpenBfme.Sim", "Kernel");
        var kernelFiles = Directory.GetFiles(kernelDirectory, "*.cs", SearchOption.TopDirectoryOnly)
            .OrderBy(path => path, StringComparer.Ordinal)
            .ToArray();
        var kernelModuleFiles = kernelFiles
            .Where(file => File.ReadAllText(file).Contains("[SageModule(\"", StringComparison.Ordinal))
            .ToArray();
        Assert.NotEmpty(kernelModuleFiles);
        foreach (var file in kernelModuleFiles)
        {
            var (name, source) = ReadAttributedName(file);
            Assert.DoesNotContain(name, censusNames);
            Assert.Contains("kernel: true", source, StringComparison.Ordinal);
        }
    }

    private static (string Name, string Source) ReadAttributedName(string file)
    {
        var source = File.ReadAllText(file);
        var match = Regex.Match(source, "\\[SageModule\\(\"([^\"]+)\"[^]]*\\)\\]",
            RegexOptions.CultureInvariant);
        Assert.True(match.Success, $"{file} has no [SageModule(\"...\")] attribute");
        return (match.Groups[1].Value, source);
    }

    private static string FindRepoRoot()
    {
        for (var directory = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName,
                    "game", "data", "retail_module_census.json")))
            {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException(
            $"Could not find the repository root above {AppDomain.CurrentDomain.BaseDirectory}");
    }
}
