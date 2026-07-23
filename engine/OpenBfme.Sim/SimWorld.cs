namespace OpenBfme.Sim;

/// <summary>Immutable per-match configuration: templates, seed, teams. Not part of the state hash.</summary>
public sealed class SimConfig
{
    public IReadOnlyDictionary<string, ObjectTemplate> Templates { get; }
    public ulong RandomSeed { get; }
    public int TeamCount { get; }

    public SimConfig(IEnumerable<ObjectTemplate> templates, ulong randomSeed, int teamCount)
    {
        if (teamCount < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(teamCount));
        }
        var map = new SortedDictionary<string, ObjectTemplate>(StringComparer.Ordinal);
        foreach (var template in templates)
        {
            map.Add(template.Name, template);
        }
        Templates = map;
        RandomSeed = randomSeed;
        TeamCount = teamCount;
    }
}

/// <summary>
/// The deterministic simulation world: fixed integer ticks, command-queue
/// mutation only, canonical hash + snapshot. This is the P0 kernel the module
/// vocabulary grows into.
/// </summary>
public sealed class SimWorld
{
    public const int TicksPerSecond = 30;

    private readonly SimConfig _config;
    private readonly ModuleRegistry _registry;
    private readonly SortedDictionary<int, GameObject> _objects = new();
    private readonly SortedDictionary<int, List<SimCommand>> _pendingCommands = new();
    private readonly long[] _teamResources;
    private readonly SortedDictionary<string, int> _moduleGaps = new(StringComparer.Ordinal);
    private DeterministicRandom _random;
    private int _nextObjectId = 1;
    private bool _inUpdateSweep;
    private readonly List<GameObject> _pendingSpawns = new();

    public int TickIndex { get; private set; }
    public IReadOnlyDictionary<int, GameObject> Objects => _objects;
    /// <summary>Module type names that had no registered implementation, with occurrence counts. Fail-closed accounting.</summary>
    public IReadOnlyDictionary<string, int> ModuleGaps => _moduleGaps;

    public SimWorld(SimConfig config, ModuleRegistry registry)
    {
        _config = config ?? throw new ArgumentNullException(nameof(config));
        _registry = registry ?? throw new ArgumentNullException(nameof(registry));
        _teamResources = new long[config.TeamCount];
        _random = new DeterministicRandom(config.RandomSeed);
    }

    public long TeamResources(int team) => _teamResources[ValidateTeam(team)];

    public void AddTeamResources(int team, long amount) => _teamResources[ValidateTeam(team)] += amount;

    public GameObject SpawnObject(string templateName, int team, FixedVector2 position)
    {
        ValidateTeam(team);
        if (!_config.Templates.TryGetValue(templateName, out var template))
        {
            throw new KeyNotFoundException($"Unknown object template '{templateName}'");
        }
        var modules = new List<ModuleBase>(template.Modules.Count);
        foreach (var spec in template.Modules)
        {
            if (_registry.TryCreate(spec, out var module))
            {
                modules.Add(module!);
            }
            else
            {
                _moduleGaps[spec.TypeName] = _moduleGaps.TryGetValue(spec.TypeName, out var count) ? count + 1 : 1;
            }
        }
        var gameObject = new GameObject(_nextObjectId++, templateName, team, position, modules);
        if (_inUpdateSweep)
        {
            // Spawns requested by modules mid-sweep (production, death rubble)
            // are deferred so scanning modules never see the object dictionary
            // mutate under them; newcomers join at end of sweep, first update
            // next tick — deterministically.
            _pendingSpawns.Add(gameObject);
        }
        else
        {
            _objects.Add(gameObject.Id, gameObject);
        }
        return gameObject;
    }

    public bool SubmitCommand(SimCommand command)
    {
        if (command.Tick <= TickIndex)
        {
            return false;
        }
        if (command.Team < 0 || command.Team >= _config.TeamCount)
        {
            return false;
        }
        if (!_pendingCommands.TryGetValue(command.Tick, out var list))
        {
            list = new List<SimCommand>();
            _pendingCommands.Add(command.Tick, list);
        }
        list.Add(command);
        return true;
    }

    public void Tick()
    {
        TickIndex++;
        ApplyPendingCommands();
        // The object dictionary is frozen for the whole sweep: mid-sweep spawns
        // divert to _pendingSpawns (so modules scanning Objects never see it
        // mutate) and join afterwards, first updating next tick. Dead objects
        // never update.
        var updateList = new List<GameObject>(_objects.Values);
        _inUpdateSweep = true;
        try
        {
            foreach (var gameObject in updateList)
            {
                if (gameObject.IsDead)
                {
                    continue;
                }
                foreach (var module in gameObject.Modules)
                {
                    module.OnUpdate(this, gameObject);
                    if (gameObject.IsDead)
                    {
                        break;
                    }
                }
            }
        }
        finally
        {
            _inUpdateSweep = false;
        }
        foreach (var spawned in _pendingSpawns)
        {
            _objects.Add(spawned.Id, spawned);
        }
        _pendingSpawns.Clear();
        RemoveDeadObjects();
    }

    public void Advance(int ticks)
    {
        for (var i = 0; i < ticks; i++)
        {
            Tick();
        }
    }

    private void ApplyPendingCommands()
    {
        if (!_pendingCommands.TryGetValue(TickIndex, out var commands))
        {
            return;
        }
        _pendingCommands.Remove(TickIndex);
        commands.Sort(static (a, b) =>
        {
            var byTeam = a.Team.CompareTo(b.Team);
            return byTeam != 0 ? byTeam : a.Seq.CompareTo(b.Seq);
        });
        foreach (var command in commands)
        {
            ApplyCommand(command);
        }
    }

    private void ApplyCommand(SimCommand command)
    {
        switch (command.Type)
        {
            case "spawn":
                SpawnObject(command.GetString("template"), command.Team,
                    new FixedVector2(command.GetFixed("x"), command.GetFixed("y")));
                break;
            case "move":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var mover) && mover.Team == command.Team)
                {
                    mover.FindModule<LinearMoverModule>()?.SetDestination(
                        new FixedVector2(command.GetFixed("x"), command.GetFixed("y")));
                }
                break;
            case "damage":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var victim))
                {
                    DealDamage(victim, command.GetLong("amount"));
                }
                break;
            case "set_resources":
                _teamResources[command.Team] = command.GetLong("amount");
                break;
            case "roll":
                // Consumes randomness so tests prove the RNG stream is authoritative state.
                _teamResources[command.Team] += _random.NextBelow(100);
                break;
            case "queue_production":
                if (_objects.TryGetValue((int)command.GetLong("id"), out var producer)
                    && producer.Team == command.Team)
                {
                    producer.FindModule<ProductionModule>()?.TryQueue(command.GetString("template"));
                }
                break;
            default:
                // Unknown command types are ignored deterministically (validated upstream
                // by the lockstep layer); they still affected the hash while queued.
                break;
        }
    }

    public void DealDamage(GameObject target, long amount) => DealDamage(target, amount, DamageTypes.Default);

    public void DealDamage(GameObject target, long amount, string damageType)
    {
        if (target.IsDead || target.IsDying)
        {
            return;
        }
        // Crush damage only lands on objects that declare themselves crushable.
        if (damageType == DamageTypes.Crush && target.FindModule<SquishCollideModule>() == null)
        {
            return;
        }
        foreach (var module in target.Modules)
        {
            amount = module.ModifyIncomingDamage(target, damageType, amount);
        }
        if (amount <= 0)
        {
            return;
        }
        foreach (var module in target.Modules)
        {
            if (module.OnDamage(this, target, amount))
            {
                return;
            }
        }
    }

    /// <summary>
    /// Death pipeline: the first module claiming the death (SlowDeath-shaped)
    /// owns removal timing; otherwise the object is removed at end of tick.
    /// </summary>
    public void HandleDeath(GameObject target)
    {
        foreach (var module in target.Modules)
        {
            if (module.OnDeath(this, target))
            {
                return;
            }
        }
        target.MarkDead();
    }

    private void RemoveDeadObjects()
    {
        List<int>? deadIds = null;
        foreach (var (id, gameObject) in _objects)
        {
            if (gameObject.IsDead)
            {
                (deadIds ??= new List<int>()).Add(id);
            }
        }
        if (deadIds == null)
        {
            return;
        }
        foreach (var id in deadIds)
        {
            _objects.Remove(id);
        }
    }

    private int ValidateTeam(int team) =>
        team >= 0 && team < _config.TeamCount
            ? team
            : throw new ArgumentOutOfRangeException(nameof(team), $"Team {team} outside 0..{_config.TeamCount - 1}");

    private void WriteAuthoritativeState(CanonicalWriter writer)
    {
        writer.WriteInt(TickIndex);
        writer.WriteInt(_nextObjectId);
        writer.WriteInt(_teamResources.Length);
        foreach (var resources in _teamResources)
        {
            writer.WriteLong(resources);
        }
        var (randomState, randomIncrement) = _random.Serialize();
        writer.WriteLong(unchecked((long)randomState));
        writer.WriteLong(unchecked((long)randomIncrement));
        writer.WriteInt(_objects.Count);
        foreach (var gameObject in _objects.Values)
        {
            gameObject.WriteState(writer);
        }
        writer.WriteInt(_pendingCommands.Count);
        foreach (var (tick, commands) in _pendingCommands)
        {
            writer.WriteInt(tick);
            writer.WriteInt(commands.Count);
            foreach (var command in commands)
            {
                command.WriteTo(writer);
            }
        }
    }

    public string StateHash()
    {
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        return writer.ToSha256Hex();
    }

    public byte[] Snapshot()
    {
        var writer = new CanonicalWriter();
        WriteAuthoritativeState(writer);
        return writer.ToArray();
    }

    /// <summary>Reconstructs a world from a snapshot. Requires the same config and registry the snapshot was taken with.</summary>
    public static SimWorld Restore(byte[] snapshot, SimConfig config, ModuleRegistry registry)
    {
        var world = new SimWorld(config, registry);
        var reader = new CanonicalReader(snapshot);
        world.TickIndex = reader.ReadInt();
        world._nextObjectId = reader.ReadInt();
        var teamCount = reader.ReadInt();
        if (teamCount != config.TeamCount)
        {
            throw new InvalidDataException($"Snapshot has {teamCount} teams, config has {config.TeamCount}");
        }
        for (var i = 0; i < teamCount; i++)
        {
            world._teamResources[i] = reader.ReadLong();
        }
        var randomState = unchecked((ulong)reader.ReadLong());
        var randomIncrement = unchecked((ulong)reader.ReadLong());
        world._random = DeterministicRandom.Deserialize(randomState, randomIncrement);
        var objectCount = reader.ReadInt();
        for (var i = 0; i < objectCount; i++)
        {
            world.ReadObject(reader);
        }
        var pendingTickCount = reader.ReadInt();
        for (var i = 0; i < pendingTickCount; i++)
        {
            var tick = reader.ReadInt();
            var commandCount = reader.ReadInt();
            var commands = new List<SimCommand>(commandCount);
            for (var j = 0; j < commandCount; j++)
            {
                commands.Add(SimCommand.ReadFrom(reader));
            }
            world._pendingCommands.Add(tick, commands);
        }
        reader.ExpectEnd();
        return world;
    }

    private void ReadObject(CanonicalReader reader)
    {
        var id = reader.ReadInt();
        var templateName = reader.ReadString();
        var team = reader.ReadInt();
        var position = reader.ReadVector();
        var isDead = reader.ReadBool();
        var isDying = reader.ReadBool();
        var isUnderConstruction = reader.ReadBool();
        if (!_config.Templates.TryGetValue(templateName, out var template))
        {
            throw new InvalidDataException($"Snapshot references unknown template '{templateName}'");
        }
        var modules = new List<ModuleBase>(template.Modules.Count);
        foreach (var spec in template.Modules)
        {
            if (_registry.TryCreate(spec, out var module))
            {
                modules.Add(module!);
            }
            else
            {
                _moduleGaps[spec.TypeName] = _moduleGaps.TryGetValue(spec.TypeName, out var count) ? count + 1 : 1;
            }
        }
        var gameObject = new GameObject(id, templateName, team, position, modules);
        foreach (var module in modules)
        {
            module.ReadState(reader);
        }
        if (isDead)
        {
            gameObject.MarkDead();
        }
        if (isDying)
        {
            gameObject.MarkDying();
        }
        gameObject.SetUnderConstruction(isUnderConstruction);
        _objects.Add(id, gameObject);
    }
}
