namespace OpenBfme.Sim;

public sealed partial class SimWorld
{
    public void CompleteClaimedDeath(GameObject target, ModuleBase claimant)
    {
        var found = false;
        foreach (var module in target.Modules)
        {
            if (!found)
            {
                found = ReferenceEquals(module, claimant);
                continue;
            }
            if (module.OnDeath(this, target)) return;
        }
        target.MarkDead();
    }

    internal bool IsAttackable(GameObject target)
    {
        if (target.IsDead || target.IsDying) return false;
        if (target.FindModule<InactiveBodyModule>() != null) return false;
        if (target.Combat is { HasBody: true, Health.Raw: > 0 }) return true;
        var (health, maximum) = ReadHealth(target);
        return maximum > Fixed64.Zero && health > Fixed64.Zero;
    }

    internal Fixed64 ApplyCombatDamage(GameObject target, Fixed64 amount, DamageType damageType)
    {
        if (amount <= Fixed64.Zero || target.IsDead || target.IsDying) return Fixed64.Zero;
        if (target.FindModule<InactiveBodyModule>() != null) return Fixed64.Zero;
        if (target.FindModule<PorcupineFormationBodyModule>() is { } porcupine)
            amount = porcupine.ModifyIncomingDamage(amount);
        if (target.Combat is { HasBody: true } combat)
        {
            if (target.FindModule<HighlanderBodyModule>() != null)
                amount = HighlanderBodyModule.ClampIncomingDamage(combat.Health, amount, damageType);
            var applied = Fixed64.Min(combat.Health, amount);
            combat.Health -= applied;
            if (combat.Health == Fixed64.Zero) HandleDeath(target);
            return applied;
        }

        var before = ReadHealth(target).Health;
        var integerAmount = amount.Raw >> Fixed64.FractionBits;
        if (integerAmount <= 0) integerAmount = 1;
        var legacyType = damageType.ToString().ToLowerInvariant();
        foreach (var module in target.Modules)
        {
            integerAmount = module.ModifyIncomingDamage(target, legacyType, integerAmount);
        }
        if (integerAmount <= 0) return Fixed64.Zero;
        foreach (var module in target.Modules)
        {
            if (module.OnDamage(this, target, integerAmount)) break;
        }
        var after = ReadHealth(target).Health;
        return before > after ? before - after : Fixed64.Zero;
    }

    private void PruneDeadHordeMembers(IReadOnlyCollection<int> deadIds)
    {
        var deadSet = new HashSet<int>(deadIds);
        for (var index = _hordes.Count - 1; index >= 0; index--)
        {
            var horde = _hordes[index];
            var livingMembers = horde.Members.Where(member => !deadSet.Contains(member)).ToArray();
            if (livingMembers.Length == 0)
            {
                Movement.RemoveHorde(horde.Id);
                _hordes.RemoveAt(index);
            }
            else if (livingMembers.Length != horde.Members.Count)
            {
                _hordes[index] = horde with { Members = livingMembers };
            }
        }
    }

    private void ExpandHordeDeaths(List<int> deadIds)
    {
        var dead = new SortedSet<int>(deadIds);
        var changed = true;
        while (changed)
        {
            changed = false;
            foreach (var horde in _hordes)
            {
                if (dead.Contains(horde.Id))
                {
                    foreach (var memberId in horde.Members)
                    {
                        if (dead.Add(memberId))
                        {
                            if (_objects.TryGetValue(memberId, out var member)) member.MarkDead();
                            changed = true;
                        }
                    }
                    continue;
                }
                if (_objects.TryGetValue(horde.Id, out var carrier)
                    && horde.Members.All(member => dead.Contains(member) || !_objects.ContainsKey(member)))
                {
                    carrier.MarkDead();
                    if (dead.Add(horde.Id)) changed = true;
                }
            }
        }
        deadIds.Clear();
        deadIds.AddRange(dead);
    }
}
