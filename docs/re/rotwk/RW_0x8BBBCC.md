# TooltipUpgrade and the campaign statistics help fields

## Provenance

Address-level findings from the supplied RotWK 2.01 image (`game.dat`, S-001
caveat), read with `objdump -d`. No clean-retail checksum is claimed. The BFME2
1.06 `game.dat` is the donor for the same module family.

## TooltipUpgrade (`RW 0x8BBBCC`)

`TooltipUpgrade` is a behavior module registered by `RW 0x65A7F3`; its module
data carries the upgrade mux fields (`TriggeredBy`, `ConflictsWith`) and two
labels:

| Field | Offset | Use |
|---|---|---|
| `DisplayName` | `+0x138` | the help title |
| `Description` | `+0x13C` | the help description |

The implementation at `RW 0x8BBBCC` gets the object's drawable (`RW 0x70E013`)
and writes the labels to the drawable's `+0x348` / `+0x34C` through
`RW 0x6796DA` / `RW 0x674FAE`. `RW 0x8BBC0A` clears both labels when the upgrade
is removed. The four retail templates that use it are
`GondorCastleUpgrade` / `RohanCastleUpgrade` (garrison, trebuchet, postern gate)
and the wall banner.

The port keeps the two labels on `Object` (`m_tooltipDisplayName` /
`m_tooltipDescription`): the simulation must not hold a `Drawable` (S-810).
`TooltipUpgrade` is an `UpgradeModule`; `UnitHelpSource` fetches the labels.
Tests: `hover1 unit help: a TooltipUpgrade's DisplayName / Description`.

## Campaign statistics block (`Object +0xA4` -> `+0x488` / `+0x494`)

`createSingleObject` (`RW 0x780172`) copies the carry-over record's statistics
block at `+0xA4` into the object at `+0x488` with `RW 0x5EA74E`:

```
+0x00 int    -> Object +0x488   // battles fought / missions
+0x04 int    -> Object +0x48C
+0x08 int    -> Object +0x490
+0x0C string -> Object +0x494   // the object's own name
+0x10 string -> Object +0x498
+0x14 byte   -> Object +0x49C
```

`UnitHelpSource` reads `+0x488` for `TOOLTIP:BattlesFought` and `+0x494` for the
title after the drawable caption / Create-a-Hero name. The port keeps those two
fields (`Object::battlesFought`, `Object::ownName`) and carries them through
`CampaignCarryover::Record`; the block's kills, second string and byte remain
unported (S-852 / S-4903). They are help-only and are not hashed when zero, so
existing campaign state hashes are unchanged. Tests:
`hover1 unit help: the campaign statistics own name / battles line` and
`bfme2camp1 CampaignCarryover: the statistics help fields survive the state text`.

## Disguise label (`RW 0x691B15`)

The function calls `InvisibilityManager::isInvisible` (`RW 0x68FC2F`), then asks
the active local player's relationship to the object's team (`RW 0x7A3DAD`);
when the object is invisible and not the player's ally, and the template's
`DisplayNameInvisibleForEnemy` (`+0x5C`) is non-empty, that label replaces the
help title. The port applies the same condition in `UnitHelpSource::compose`
(`InvisibilityManager::isInvisible`, local player active and relationship not
`ALLIES`). Test: `hover1 unit help: an invisible enemy shows
DisplayNameInvisibleForEnemy`.
