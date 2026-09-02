namespace OpenBfme.Sim;

public sealed record AiDiagnostic(int Player, int Tick, string Action, long Score, string Detail);

public sealed partial class SimWorld
{
    private const int AiExtensionMagic = 0x4149504C;
    private readonly SortedDictionary<int, AiPlayerState> _aiPlayers = new();
    private readonly List<AiDiagnostic> _aiDiagnostics = new();

    public IReadOnlyList<AiDiagnostic> AiDiagnostics => _aiDiagnostics;

    internal SimConfig AiConfig => _config;

    internal bool IsAiTeam(int team) => _aiPlayers.Values.Any(value => value.Team == team && value.Enabled);

    private void InitializeAi(MatchLaunch launch)
    {
        _aiPlayers.Clear();
        _aiDiagnostics.Clear();
        for (var playerIndex = 0; playerIndex < launch.Players.Count; playerIndex++)
        {
            var player = launch.Players[playerIndex];
            if (!string.Equals(player.Controller, "ai", StringComparison.Ordinal)) continue;
            _aiPlayers.Add(playerIndex, AiPlayerState.Create(playerIndex, player));
        }
    }

    public void SetAiEnabled(int player, bool enabled)
    {
        if (!_aiPlayers.TryGetValue(player, out var state))
            throw new ArgumentOutOfRangeException(nameof(player), $"Player {player} is not an AI seat");
        state.Enabled = enabled;
    }

    public int AiPlanIntervalTicks(int player) =>
        _aiPlayers.TryGetValue(player, out var state)
            ? state.PlanIntervalTicks(TickMilliseconds)
            : throw new ArgumentOutOfRangeException(nameof(player));

    public IReadOnlyDictionary<string, int> AiCommandCounts(int player) =>
        _aiPlayers.TryGetValue(player, out var state)
            ? new SortedDictionary<string, int>(state.CommandCounts, StringComparer.Ordinal)
            : throw new ArgumentOutOfRangeException(nameof(player));

    internal (Fixed64 Health, Fixed64 Maximum) AiHealth(GameObject gameObject) => ReadHealth(gameObject);

    internal void RecordAiDecision(AiPlayerState state, int tick, string action, long score, string detail) =>
        _aiDiagnostics.Add(new AiDiagnostic(state.PlayerIndex, tick, action, score, detail));

    private void RunAiForTick(int tick)
    {
        foreach (var state in _aiPlayers.Values)
        {
            if (!state.Enabled || !state.ShouldPlan(tick, TickMilliseconds)) continue;
            AiPlanner.Plan(this, state, tick);
        }
    }

    internal bool SubmitAiCommands(AiPlayerState state, int tick, IReadOnlyList<SimCommand> commands)
    {
        if (commands.Count == 0) return true;
        var bundle = new SimCommandBundle(
            SimCommandBundle.SchemaName,
            tick,
            state.Seat,
            state.BundleSequence++,
            commands);
        if (!SubmitCommandBundle(bundle)) return false;
        foreach (var command in commands)
        {
            state.CommandCounts[command.Type] = state.CommandCounts.TryGetValue(command.Type, out var count)
                ? checked(count + 1)
                : 1;
        }
        return true;
    }

    private void WriteAiExtension(CanonicalWriter writer)
    {
        if (_aiPlayers.Count == 0) return;
        writer.WriteInt(AiExtensionMagic);
        writer.WriteInt(_aiPlayers.Count);
        foreach (var state in _aiPlayers.Values) state.Write(writer);
    }

    private void ReadAiExtension(CanonicalReader reader)
    {
        _aiPlayers.Clear();
        var count = ReadCount(reader, "AI player");
        for (var index = 0; index < count; index++)
        {
            var state = AiPlayerState.Read(reader);
            _aiPlayers.Add(state.PlayerIndex, state);
            _seatTeams[state.Seat] = state.Team;
        }
    }
}
