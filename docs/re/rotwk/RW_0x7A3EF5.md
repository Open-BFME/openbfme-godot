# RW 0x7A3EF5 — Team special-power registry lookup

> Restored research; port disposition reconciled with current main STOPS.md
> (archive snapshot 1eea93ac66). The 2026-10-23 function map classifies this
> entry as RotWK-only. Historical target reconstruction below is UNVERIFIED
> against a clean target in this lane; it is not new executable parity evidence.

## Identity / evidence

Team receiver, power-template pointer and optional Object pointer arguments,
Object pointer result, callee cleanup of eight argument bytes. Call sites:
RW 0x9E6941 (ATTACK_HEAL_AOE) and RW 0x9E9B62 (SOUL_FREEZE), both pass a null
Object. This is S-1421, not a new module. Supplied image retains the S-001
provenance caveat. No patched-section call occurs here.

Team +0x140 holds a circular list sentinel; node +0 next, +4 previous,
+8 power-template pointer, +0x18 caster ObjectID. These are the first and
fifth words of Soul Freeze's registry insertion payload. Object +0x74 is ID.
The sole callee RW 0x449681 is byte-matched GameLogic::findObjectByID
(BFME2 GameLogicFindObjectByID.cpp). Original lookup name is unproven.

## Recovered C++

The supplied pack advertises 130 bytes but lists an entry-through-terminal
jump range of 68 bytes. The function below reconstructs that observed CFG,
not arbitrary adjacent instructions. No float arithmetic or RNG.

```cpp
// RW 0x7A3EF5..0x7A3F37; descriptive member/type names (INFERENCE).
Object *Team::lookupPower(SpecialPowerTemplate *power, Object *object)
{
    Node *end = m_powerRegistry;
    Node *node = end->next;
    while (node != end) {
        if (node->power != power) {
            node = node->next;
            continue;
        }
        if (!object)
            return TheGameLogic->findObjectByID(node->casterID);
        if (node->casterID == object->getID()) return object;
        // No advance here in the supplied image. See below.
    }
    return 0;
}
```

Important literal behavior: a matching power whose object ID does not match
the non-null argument loops forever. RW 0x7A3F1B branches past the only
node-advance instruction at RW 0x7A3F1D. Do not silently "repair" this in a
parity port, or assume Ghidra's loop is merely an artifact. Both observed
callers pass null, so that path is not reached by these decisions. The null
path returns the lookup of the **first** matching record, even when its ID
is stale and resolves to null; it does not search subsequent matching records.
There is no liveness, cooldown or timestamp test in this helper.

## Port disposition / S-1421

No isolated engine implementation: the surrounding Team registry is missing,
including its writers, cleanup and serialization. Adding an empty container
and a lookup would falsely make both decision gates permissive. Recover those
lifecycle paths before integration; then test empty/miss, first stale match,
first live match and pointer-identity keying. Non-null mismatched use must be
an explicit compatibility decision if a new caller is introduced, not an
accidental infinite loop in a new API. No simulation change or test claim.
