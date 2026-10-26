# RW 0x88CFFE — DozerAIUpdate removes an unplaced structure

> Restored research; port disposition reconciled with current main STOPS.md
> (archive snapshot 1eea93ac66). The 2026-10-23 function map classifies this
> entry as RotWK-only. Historical target reconstruction below is UNVERIFIED
> against a clean target in this lane; it is not new executable parity evidence.

## Ownership and uses

This is the dozer interface vslot +0x6C (data reference RW 0xC62344), on the
DozerAIUpdate subobject at module +0x4A8. It is called directly at RW 0x88D23B
from destructor RW 0x88D1AC, and virtually from placement/cancel paths
(existing engine identifies RW 0x88D44F and 0x88E3CE). Thus it belongs to
existing S-304, not skirmish tactic selection. Interface +0xC0 is unplaced
ObjectID (module +0x568), interface -0x4A0 is module +8, the owner Object*.
Interface +0x28 (module +0x4D0) suppresses refund/score reversal when nonzero;
its original member name and full set/clear lifecycle remain unproven.
Object +4 template, +0x33C paid build price (float). Player +0x90 Money and
+0x3DC ScoreKeeper match the donor interfaces.

S-001 supplied-image caveat applies. No direct patched-section calls.

## Recovered C++

```cpp
// RW 0x88CFFE..0x88D0C5; offset-derived member names where not recovered.
void DozerAIUpdate::removeUnplacedStructure()
{
    if (!m_unplacedStructure) return;
    Object *structure=TheGameLogic->findObjectByID(m_unplacedStructure);
    if (structure) {
        Player *owner=structure->getControllingPlayer(); // RW 0x68B678
        if (!m_suppressRefund28) {
            int paid=ftol2(structure->m_buildCostPaid); // RW 0x88D041
            owner->money.deposit(paid,&owner->score,true); // RW 0x7B18B8
            owner->score.addMoneySpentByKind(structure->thingTemplate,-paid);
            // RW 0x88D06B -> 0x79DFC2, then RW 0x88D077 -> 0x79F0E1:
            owner->score.addObjectBuilt(structure,-1);
        }
        // RW 0x88D07D..0x88D0B6; cleanup also runs with suppressRefund set.
        TheGameLogic->destroyObject(structure); // RW 0x62BBAB
        TheBuildAssistant->removeID(m_unplacedStructure); // RW 0x6A038F
        if (m_object) {
            Player *builderOwner=m_object->getControllingPlayer();
            if (builderOwner) builderOwner->removeID(m_unplacedStructure); // RW 0x6ADA08
        }
    }
    m_unplacedStructure=0; // RW 0x88D0BB, including failed ID lookup
}
```

No liveness check after ID resolution, and no owner-null guard before the
refund. The current engine's destroyed-object guard is an adaptation that
prevents duplicate refund while its deferred deletion still resolves the ID;
it is not an extra retail predicate. The stored ID is cleared **last** in the
image, not before notification. Both list-removal calls use the stored ID
after destroyObject, not a dereference of the destroyed object.

__ftol2 RW 0xA3CFA4 truncates the stored price; there is no recomputation from
the template and no percent-built factor. x86 NEG wraps when negating paid.
The function itself does not require paid>0 before calling Money's helper.
Matched donors: GameLogicFindObjectByID.cpp, MoneyRva003B0D7C.cpp (deposit),
Rva0039BAD2Method.cpp (money-spent categories), ScoreKeeperReset.cpp
(ScoreKeeper::addObjectBuilt), stlport_list_int_remove_guard.cpp (global list
remove), Rva002ACECCRemove.cpp (player list remove, same-shape). TheGameLogic
destroyObject and Object owner accessor are same-shape donor mappings.

## Current port / remaining S-304

The current engine removal refunds money and reverses money-spent score
but omits `ScoreKeeper::addObjectBuilt(...,-1)`. The old branch added that
call; this notes restoration does not port it. Object::initObject counts the foundation
as built, so removing it must reverse both structures-built and structures-
alive (and the keeper's per-template accounting), not record a combat loss.

The old branch's BUILD-3 cancellation test had separate dispatcher and dozer-owned
cancellation subcases. The dozer subcase checks the foundation was counted,
then that cancellation rolls back built/alive exactly once; a second cancel
does not change them. It fails without the added call. This test requires
retail game assets and can only execute with that fixture (normally CI);
an asset-free skip is not a passing regression run.

That old change would alter simulation state/hash after dozer-owned unplaced
structure cancellation; no such change or regression test is imported here.
Current STOPS.md keeps S-304 open for the PHANTOM placement
model, suppress-refund lifecycle, destructor removal and retail cleanup-list
semantics. The change does not claim those are implemented or that the whole
function is byte-equivalent. Existing clear-early/deferred-destroy adaptation
is retained rather than changed without the missing lifecycle support.
