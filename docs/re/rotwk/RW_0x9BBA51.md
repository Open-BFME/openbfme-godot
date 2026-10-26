# RW 0x9BBA51 — AIFarmKillSquad fortress-aware farm selection

> Restored research; port disposition reconciled with current main STOPS.md
> (archive snapshot 1eea93ac66). The 2026-10-23 function map classifies this
> entry as RotWK-only. Historical target reconstruction below is UNVERIFIED
> against a clean target in this lane; it is not new executable parity evidence.

## Provenance / identity

Supplied image and pack, S-001 caveat: added executable sections are present;
clean-retail provenance is not established. No direct call here enters them.
The advertised 1408-byte extent is not a single contiguous body: the supplied
listing ends at RW 0x9BBE43's jump to the epilogue, before update RW 0x9BBE48.
Do not infer function boundaries by adding the map size alone.

`this` is AIFarmKillSquad, argument is the enemy `Player *`, return `Object *`.
Called at RW 0x9BBF4D from its started update RW 0x9BBE48. That update also
chooses RW 0x9BB5E4; this function is not the whole target-selection algorithm.
The donor `AIFarmKillSquad.cpp` establishes the class and `getFarmFinder`,
`findEnemyFortress`, `rva005ACDD2`; the exact original name of this selector is
unknown (`selectFarm` below is **INFERENCE**).

## Data and helpers

* RW 0x6A9999 obtains the enemy player-info record (BFME2 Rva002A8F24.cpp,
  Rva002A8F24::rva002A8F24: player-index map lookup). Record +0xC is the economy
  object collection; RW 0x72B229 returns its vector at +4. The function copies
  that vector, preserving order, via RW 0x5FCDCB. The map's LivingWorld vector
  type is an identical template body, not evidence of a LivingWorld receiver.
* RW 0x8F128B(0) gets tactic team index zero (AITactic::rva004ECECD in
  HLodRva004ECECD.cpp); RW 0x7A0ECD fills its center (Team::rva0039E5B9 in
  TeamDidPartialEnter.cpp). Center starts (0,0,0).
* RW 0x9B58BF scans record +8's vector for the first object with template
  +0x120 bit 2 (DOZER_FACTORY). It does not check dead/destroyed flags.
  This is the donor's `findEnemyFortress` behavior, verified in the supplied
  image, rather than a nearest-fortress query.
* RW 0x9BB578 `getFarmFinder`: resolves tactic +0x5C first, otherwise first
  living CAN_ATTACK member of team zero. This function assumes a finder exists
  in the fortress branch; do not add a draw or silently pick a different unit.
* Object +4 is its template, +0x38 its position; template +0x10E bit 2 is the
  filter retained below as an offset predicate pending a target layout proof.
  Object +0x94 bit 0 is DESTROYED; +0x458 bit 0 is effectively dead.
* RW 0x6F5F65 is Pathfinder::QuickDoesPathExistToStructure
  (PathfinderQuickStructure.cpp); args finder, &finder.position, farm, false.
* RW 0x68C82D(13, &value, 0, 1) is Object::rva0028C15E in
  ObjectRva0028C149.cpp (attribute modifier query). Value starts 1. The
  attribute's semantic name remains unproven here; numeric index 13 is certain.
* RW 0x9BB2A7 is `rva005ACDD2`, Euclidean 3D distance via Coord3D::length.
  RW 0x9BB2EA is `rva005ACE15` (Rva005ACE15Finish.cpp), distance from a point
  to the infinite line through two points, **not a clamped segment**.
* RW 0x77532D is integer-key tree `insert_unique`, not multimap insertion.
  Node +0x10 is score, +0x14 Object*. RW 0x423EA0 / 0x423F10 are STLport
  `_M_increment` / `_M_decrement`. Equal scores keep the first inserted farm,
  but the total is incremented even when insertion loses the duplicate.

## Recovered C++

Typed reconstruction. `record`, `economyIDs`, `templateFlag10E`, and the field
adapters below describe proven offsets, not newly recovered original names.
All scalar operations in the scoring expression are separate SSE float32
operations. Both conversions to integer use truncation (`CVTTSS2SI`), not
round-to-nearest. Signed totals wrap in the target machine; a host port must
not introduce signed-overflow UB. No arbitrary validation guards are added.

```cpp
Object *AIFarmKillSquad::selectFarm(Player *enemy)
{
    // RW 0x9BBA61..0x9BBAB3
    PlayerInfo *info = record(enemy);
    Coord3D center = {0, 0, 0};
    getTeam(0)->getCenter(&center);
    std::vector<ObjectID> ids = info->economyIDs;
    Object *fortress = findEnemyFortress(enemy);
    if (!fortress) {
        // RW 0x9BBDC3..0x9BBE43; no reachability test in this branch.
        if (ids.empty()) return 0;
        Object *result = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            int i = GetGameLogicRandomValue(0, ids.size()-1, file, 0x21D);
            result = TheGameLogic->findObjectByID(ids[i]);
            if (result && !templateFlag10E(result, 4) &&
                !result->isDestroyed() && !result->isEffectivelyDead()) break;
        }
        return result; // YES: the last invalid non-null object is returned!
    }
    // RW 0x9BBAC0..0x9BBB62
    Coord3D base = *fortress->getPosition();
    float distance = rva005ACDD2(&center, &base);
    Coord3D direction = base - center;
    int baseline = truncToInt(distance < 5000.0f ? 5000.0f : distance);
    Object *finder = getFarmFinder();
    std::map<int, Object *> choices;
    unsigned total = 0;
    for (ObjectID id : ids) {
        // RW 0x9BBB68..0x9BBBC3
        Object *farm = TheGameLogic->findObjectByID(id);
        if (!farm || templateFlag10E(farm, 4) || farm->isDestroyed() ||
            farm->isEffectivelyDead()) continue;
        if (!TheAI->getPathfinder()->QuickDoesPathExistToStructure(
                finder, finder->getPosition(), farm, false)) continue;
        const Coord3D *p = farm->getPosition();
        // RW 0x9BBBC9..0x9BBCC0
        float toBase = rva005ACDD2(p, &base);
        if (!(toBase > 275.0f)) continue;
        float toTeam = rva005ACDD2(p, &center);
        float z = (p->z-center.z)*direction.z;
        float y = (p->y-center.y)*direction.y;
        float x = (p->x-center.x)*direction.x;
        float dot = (z+y)+x;
        float attribute = 1.0f;
        farm->rva0028C15E(13, &attribute, 0, 1);
        float factor = attribute > 2.0f ? attribute*0.5f : 1.0f;
        float projection = dot/(distance*distance);
        if (!(projection < 1.0f) && rva005ACE15(p, &center, &base) < 275.0f)
            continue;
        // RW 0x9BBCB6..0x9BBCF8: add/subtract/multiply/multiply in this order.
        float score = (float(baseline)+toBase)-toTeam;
        score = score*factor;
        score = score*0.01f;
        int weight = truncToInt(score);
        if (weight < 5) weight = 5;
        if (weight > 0) {
            choices.insert(std::make_pair(weight, farm));
            total += unsigned(weight); // even on duplicate score
        }
    }
    // RW 0x9BBD0F..0x9BBD90
    if (choices.empty()) return 0;
    if (GetGameLogicRandomValue(0, 3, file, 0x203) == 0) {
        unsigned draw = GetGameLogicRandomValue(0, total, file, 0x20A);
        unsigned cumulative = 0;
        for (auto it = choices.begin(); it != choices.end(); ++it) {
            cumulative += unsigned(it->first);
            if (draw < cumulative) return it->second;
        }
    }
    return (--choices.end())->second;
}
```

`file` is the game's AIFarmKillSquad.cpp random-stream diagnostic file
(RW 0xC8A218). Draw upper bounds are inclusive. The weighted branch has a
deliberate fall-through to the highest score for draw==total, and also for the
excess total created by duplicate weights. Nonzero draw at line 0x203 takes
the highest score directly. Empty candidate maps consume no RNG. A missing
fortress makes at most three line-0x21D draws, stopping early on a valid farm.

Unordered caveats: the `toBase > 275` test rejects unordered; the line-distance
comparison rejects only ordered `<275`, whereas projection must be ordered
`<1` to skip that call. Preserve instruction predicates if supporting NaNs.

## Port disposition / S-893

**Not integrated:** there is no QuickDoesPathExistToStructure in the current
engine, and the complementary selector RW 0x9BB5E4 plus the caller's line-0x8B
selector draw remain unported. A forced-success path probe would change both
candidates and RNG. The current nearest-living-economy stand-in remains
explicitly stopped. Implement the exact player-record collection order and
path probe, identify the template flag and attribute index, recover the other
selector, then replace the entire choice site. Tests should pin duplicate
weights, inclusive endpoint fall-through, no-fortress invalid-third return,
and the exact line-tagged draw sequence. No simulation/hash change here.
