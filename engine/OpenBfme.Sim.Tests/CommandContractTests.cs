using OpenBfme.Sim;
using Xunit;

namespace OpenBfme.Sim.Tests;

public class CommandContractTests
{
    [Fact]
    public void GoldenFixtureRoundTripsByteIdentically()
    {
        var path = MatchLaunchTests.RepoPath("contracts", "fixtures", "command-v1.json");
        // Git may check the fixture out with CRLF on Windows; the wire form is
        // LF-only, so compare after trimming trailing line-ending bytes.
        var expected = TrimTrailingNewlines(File.ReadAllBytes(path));

        var bundle = SimCommand.ParseBundle(expected);
        var actual = TrimTrailingNewlines(SimCommand.SerializeBundle(bundle));

        Assert.Equal(expected, actual);
        Assert.Equal(SimCommandBundle.SchemaName, bundle.Schema);
        Assert.Equal(12, bundle.Tick);
        Assert.Equal(0, bundle.Seat);
        Assert.Equal(7, bundle.Seq);
        Assert.Equal(new long[] { 1, 2 }, bundle.Commands[0].GetLongList("objects"));
        Assert.Equal(Fixed64.FromInt(200), bundle.Commands[0].GetFixed("x"));
    }

    [Fact]
    public void MergeOrdersByResolvedTeamThenSequenceAndRetainsBundleOrder()
    {
        var highTeamFirst = SimCommandBundle.Parse("""
            {"schema":"openbfme.command.v1","tick":9,"seat":0,"seq":8,"commands":[
              {"type":"stop","args":{"objects":[4]}},
              {"type":"hold","args":{"objects":[4]}}
            ]}
            """);
        var lowTeamSecond = SimCommandBundle.Parse("""
            {"schema":"openbfme.command.v1","tick":9,"seat":1,"seq":3,"commands":[
              {"type":"move","args":{"objects":[2],"x":8,"y":3}}
            ]}
            """);

        var merged = SimCommandBundle.MergeForTick(
            new[] { highTeamFirst, lowTeamSecond },
            seat => seat == 0 ? 2 : 1);

        Assert.Equal(new[] { 1, 2, 2 }, merged.Select(command => command.Team));
        Assert.Equal(new[] { 3, 8, 8 }, merged.Select(command => command.Seq));
        Assert.Equal(new[] { "move", "stop", "hold" }, merged.Select(command => command.Type));
    }

    [Fact]
    public void InvalidCommandTypeAndNumericShapeAreRejected()
    {
        Assert.Throws<CommandContractException>(() => SimCommandBundle.Parse("""
            {"schema":"openbfme.command.v1","tick":1,"seat":0,"seq":0,"commands":[{"type":"dance","args":{}}]}
            """));
        Assert.Throws<CommandContractException>(() => SimCommandBundle.Parse("""
            {"schema":"openbfme.command.v1","tick":1,"seat":0,"seq":0,"commands":[{"type":"move","args":{"objects":[1],"x":"bad","y":2}}]}
            """));
    }

    private static byte[] TrimTrailingNewlines(byte[] bytes)
    {
        var end = bytes.Length;
        while (end > 0 && (bytes[end - 1] == 0x0A || bytes[end - 1] == 0x0D))
        {
            end--;
        }
        return bytes[..end];
    }
}
