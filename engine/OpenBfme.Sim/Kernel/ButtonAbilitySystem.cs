namespace OpenBfme.Sim;

/// <summary>Normal command-path execution for non-special-power ability buttons.</summary>
public sealed partial class SimWorld
{
    internal bool CommandButtonAllows(GameObject issuer, string buttonName)
    {
        if (issuer.CurrentCommandSet.Length == 0) return true;
        if (!_config.Tech.CommandSets.TryGetValue(issuer.CurrentCommandSet, out var set)) return false;
        return set.Entries.Any(entry => entry.ButtonName.Equals(buttonName, StringComparison.Ordinal));
    }

    private void ApplyAbilityCommand(SimCommand command)
    {
        var name = CommandName(command, "ability");
        if (!_config.Tech.CommandButtons.TryGetValue(name, out var button))
        {
            RecordDiagnostic(command, 0, "unknown_ability_button", $"unknown ability button '{name}'");
            return;
        }
        foreach (var id in CommandObjectIds(command))
        {
            if (!TryOwnedObject(command, id, out var gameObject)) continue;
            if (!CommandButtonAllows(gameObject, name))
            {
                RecordDiagnostic(command, gameObject.Id, "not_in_command_set",
                    $"object {gameObject.Id} command set '{gameObject.CurrentCommandSet}' does not offer '{name}'");
                continue;
            }
            if (button.Command.Equals("TOGGLE_WEAPONSET", StringComparison.Ordinal))
                ApplyWeaponToggle(gameObject, button);
            else if (button.Command.Equals("FIRE_WEAPON", StringComparison.Ordinal))
                ApplyWeaponFire(command, gameObject, button);
        }
    }

    private void ApplyWeaponToggle(GameObject gameObject, CommandButtonTemplate button)
    {
        foreach (var flag in ModuleRuntime.Tokens(button.FlagsUsedForToggle))
            gameObject.TrySetConditionToken(flag, !gameObject.ConditionTokens.Contains(flag));
        RaiseEvent(new SimEvent("ability", gameObject.Id, Name: button.Name));
    }

    private void ApplyWeaponFire(SimCommand command, GameObject gameObject, CommandButtonTemplate button)
    {
        var targetId = command.GetLong("target");
        if (targetId is < 1 or > int.MaxValue
            || !_objects.TryGetValue((int)targetId, out var target)
            || target.IsDead || target.IsDying || target.Team == gameObject.Team) return;
        if (!WeaponSlotAllowsTarget(gameObject, button.WeaponSlot, target)
            || !TryWeaponSlot(gameObject, button.WeaponSlot, out var weapon)) return;
        Combat.FireWeaponOnce(this, gameObject, target, weapon);
    }

    internal bool WeaponSlotAllowsTarget(GameObject gameObject, string slotName, GameObject target)
    {
        if (!TryWeaponSlot(gameObject, slotName, out var weaponName)
            || !_config.WeaponTemplates.TryGetValue(weaponName, out var weapon)) return false;
        var distance = gameObject.Position.DistanceSquaredTo(target.Position);
        return distance <= weapon.AttackRange * weapon.AttackRange
            && distance >= weapon.MinimumAttackRange * weapon.MinimumAttackRange;
    }

    private static bool TryWeaponSlot(GameObject gameObject, string slotName, out string weapon)
    {
        weapon = "";
        if (!Enum.TryParse<WeaponSlot>(slotName, true, out var slot)) return false;
        var set = gameObject.Template.WeaponSets.FirstOrDefault(value => value.Matches(gameObject.ConditionTokens))
            ?? gameObject.Template.WeaponSets.FirstOrDefault(value => value.Conditions.Count == 0);
        return set != null && set.Weapons.TryGetValue(slot, out weapon!);
    }
}
