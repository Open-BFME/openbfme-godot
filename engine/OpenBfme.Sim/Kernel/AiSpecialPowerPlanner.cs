namespace OpenBfme.Sim;

/// <summary>Module-owned AI ability planning hook, kept outside the core planner.</summary>
internal static class AiSpecialPowerPlanner
{
    public static void Plan(
        SimWorld world,
        AiPlayerState state,
        int tick,
        ICollection<SimCommand> commands)
    {
        foreach (var gameObject in world.Objects.Values)
        {
            if (gameObject.Team != state.Team || gameObject.IsDead || gameObject.IsDying
                || gameObject.IsUnderConstruction) continue;
            foreach (var module in gameObject.Modules.OfType<AISpecialPowerUpdateModule>())
                if (module.TryPlan(world, gameObject, state, tick, commands)) break;
        }
    }
}
