# Easy roaming and the scripted retail scenes

Scope: RotWK retail test fixtures only. No runtime or BFME2 edition rules change.
Comparison: upstream `c8aa4603` and roaming PR `a1bd3e08`, Release Linux builds,
with the RotWK and BFME2 installs present. Temporary instrumentation recorded
end-of-frame object IDs, templates, owners, AI states and positions in both
builds, and targetless picks / roam destinations in the PR build. Instrumentation
is not part of the change. Frame numbers below are absolute logic frames;
the siege assertions count frames since the attack order instead.

## Retail checks

The binary evidence is from RotWK `game.dat` (the repository's S-001 provenance
caveat still applies):

- RW 0x6AA030 / 0x6AA0AE and 0x8EDAAB / 0x8EDADE: playable computer sides,
  or `MakeAllSkirmishSidesAIControlled`, get the tactical AI. Human test heroes
  are not roaming recipients.
- RW 0x9B7F56 calls 0x6AA61B and requires difficulty zero (Easy). The getter
  reads the AI player's difficulty via 0x9A5E0C, or the global difficulty when
  there is no AIPlayer. It schedules the first attempt five seconds later.
- RW 0x90C65C–0x90C679 draws for every nonempty targetless pool, **including
  a singleton**. Eligibility does not require an idle unit or a destination.
- RW 0x9B83A6–0x9B8431 selects the first idle army object, excludes status
  0x5A (leaving factory), and compares its actual team with the player's
  default team. RW 0x9B8160 chooses a nearby economy structure, otherwise
  (only for an empty economy list) a point around the first dozer factory.
  RW 0x9B8286 / 0x9B8390 issues command 0xF with source zero (player).

The new singleton roam pick occurs at frame 35, then 63, 91, etc. in these
fixtures. Upstream has no roaming candidate. Thus a scene can change without
any roam order: the shared RNG changes ambient wandering, construction paths,
combat and projectile scatter. Assigning only the scripted opponent to another
team does not prevent the pool draws.

## The seven failures: all category (b)

| Failing case | Paired trace and consequence |
| --- | --- |
| BACKLOG2 S-653 dead structure rebuild movement | No roam recipient in the 66-frame run. First differing objects at frame 37 include creep archer 120 (y 1026.5 on main, 1030.5 on PR). Human Boromir 134 first differs at frame 45: (731.057007, 4659.86914) versus (725.994873, 4660.5). The 17-moving-frame pin becomes 20 and its final position changes. The rebuild's original two draws and displacement order are unchanged. The separately named **r1 determinism** case passes on both builds. |
| play4 all faction heroes, own acquisition and ordered combat | Shared RNG first moves creeps at frame 37. Haldir 176 first differs at frame 235; his own-acquisition damage becomes 970 instead of 50. The ordered Gloin, Gimli and Dain scenes each have scripted `MordorFighterHorde` 177, player 4, roaming at frame 290 toward (4661.69824, 860.794983, 80); their hero 175 differs at frame 294 and fails to strike the fleeing horde. Drogoth's scene also changes production/object ordering and combat (900 rather than 825 own-acquisition damage, four rather than three ordered shots). |
| attack1 mounted Witch-king and Mouth of Sauron | Same frame-35 pool / frame-37 creep divergence. In Witch-king's control, human `AngmarWitchking` 177 first differs at frame 392; the scripted `MordorFighterHorde` 199 roams at frame 402 toward (4829.64551, 412.440399, 80). The duel's `MordorMountainTroll` 199 also roams at 402. Mouth 177 first differs at frame 311; his horde 178 roams at 318 toward (4184.11963, 221.485596, 80); his duel troll 178 roams at 318. Exact crush, weapon-shot and approach-distance assertions therefore measure different encounters. |
| siege1 ram against Helm's Deep wall | No roam recipient. First differing ambient object: Chicken 172 at frame 37 (main idle state 0, PR moving state 1). MenPorter 237 diverges at 187. Ram 251 differs at 254 (main state 26, PR state 50). First hit shifts 397 → 366; final wall health 3500 → 3250. |
| siege1 trebuchet against Helm's Deep wall | No roam recipient. HorseGrazing2 142 diverges at frame 36 (idle → moving); MenPorter 237 at 187. Shared random draws change projectile results: the first-hit pin remains, final health 5578.68506 changes to approximately 5528.51. |
| siege1 catapult inside minimum range | No roam recipient. Chicken 160 diverges at frame 38 (idle → moving); MenPorter 237 at 288. Final gate health changes from 2869.33984 to approximately 2869.33 through changed random projectile results. |
| siege1 mumak / demolisher / flanking catapult | No roam recipients. Mumak scene: HorseGrazing1 144 differs at frame 36, porter 237 at 287; first graze changes 3997.75 → 3998.5. Demolisher scene: Chicken 170 differs at 37, porter 237 at 187, demolisher 251 at 192 (state 26 → 50); first hit 249 → 251, gate death 317 → 319. The flanking-catapult assertions already pass. |

## Isolation and regression proof

`AISceneTestUtil.h` defers only roaming via its existing
`AIRoamingDefenseTactic_NextLogicFrameRun` counter to `INT_MAX`, before any tactic
starts. This is fixture setup, not a gameplay exception. Difficulty, other
tactics, construction, gates, unit AI, acquisition, crush and damage remain
active. The previous Isengard/Sauron-only non-default-team workaround is removed:
all the scripted hero opponents now use the same isolation.

The rebuild test, hero fight helper, and AI-defended `siegeAttack` helper opt in.
The dedicated roaming tests and other skirmish fixtures continue to exercise
roaming normally. No assertion or expected value is weakened or re-recorded.
The unisolated negative control reproduces all seven failures (43 cases,
36 pass, 7 fail, 53 failing assertions); upstream passes all 43. The isolated
fixtures are checked with the same selection and the full suites on both PRs.
There is no production code fix to mutation-check.
