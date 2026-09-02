namespace OpenBfme.Sim;

/// <summary>
/// HordeContain-shaped container (122 objects in the union corpus): the horde
/// object holds MemberCount member slots as DATA — this tier spawns no child
/// sim objects (member positions/formations are presentation; child-object
/// containment arrives with the horde AI lane). Aggregate health delegates to
/// the members: incoming damage fills member slots in ascending slot order
/// (lowest living slot first, overflow kills through to the next slot), which
/// is deterministic by construction. When every member is at zero the horde
/// routes through the normal death pipeline.
/// Design data: MemberCount (default 1), MemberHealth per member (default 100).
/// </summary>
[SageModule("HordeContain", ModuleTier.Structural)]
public sealed class HordeContainModule : ModuleBase
{
    public const string TypeName = "HordeContain";

    private readonly long _memberMaxHealth;
    private readonly long[] _memberHealth;

    public HordeContainModule(ModuleSpec spec) : base(spec)
    {
        var memberCount = (int)Math.Clamp(spec.GetLong("MemberCount", 1), 1, 1024);
        _memberMaxHealth = Math.Max(1, spec.GetLong("MemberHealth", 100));
        _memberHealth = new long[memberCount];
        for (var i = 0; i < _memberHealth.Length; i++)
        {
            _memberHealth[i] = _memberMaxHealth;
        }
    }

    public int MemberCount => _memberHealth.Length;
    public string MemberTemplateName => Spec.GetString("MemberTemplate", "");
    public long MemberMaxHealth => _memberMaxHealth;
    public long MemberHealthAt(int slot) => _memberHealth[slot];

    public int AliveMemberCount
    {
        get
        {
            var alive = 0;
            foreach (var health in _memberHealth)
            {
                if (health > 0)
                {
                    alive++;
                }
            }
            return alive;
        }
    }

    public long TotalHealth
    {
        get
        {
            long total = 0;
            foreach (var health in _memberHealth)
            {
                total += health;
            }
            return total;
        }
    }

    public override bool OnDamage(SimWorld world, GameObject self, long amount)
    {
        if (amount < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(amount), "Damage must be non-negative");
        }
        if (TotalHealth == 0)
        {
            return true;
        }
        for (var slot = 0; slot < _memberHealth.Length && amount > 0; slot++)
        {
            if (_memberHealth[slot] == 0)
            {
                continue;
            }
            var applied = Math.Min(_memberHealth[slot], amount);
            _memberHealth[slot] -= applied;
            amount -= applied;
        }
        if (TotalHealth == 0)
        {
            world.HandleDeath(self);
        }
        return true;
    }

    public override void WriteState(CanonicalWriter writer)
    {
        // Member count is config; only the healths are mutable state.
        foreach (var health in _memberHealth)
        {
            writer.WriteLong(health);
        }
    }

    public override void ReadState(CanonicalReader reader)
    {
        for (var i = 0; i < _memberHealth.Length; i++)
        {
            _memberHealth[i] = reader.ReadLong();
        }
    }
}
