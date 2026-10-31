# RotWK `0x92BFED`: hero-select attacked effect and builder timeout

This note records the supplied RotWK 2.01 investigation for the remaining S-2521
hero-bar gap. The image used for the investigation is supplied-modified; its
provenance and exact retail build identity are not independently established.
The addresses below are therefore address-level evidence from that image, not a
claim that an unmodified public executable was used.

## `OnHeroAttacked`

`0x92BFED` scans the sixteen hero-select slots at the implementation's `+0x48`,
with a stride of `0x18`. Sentinel list iterators are skipped. For each visible
slot, the hero object ID at `+0x74` is compared with the ID in the list node at
`+8`. The first matching slot calls `PlayButtonAttackedEffect` with the
one-based slot index.

The client call chain is:

```text
Drawable::damage 0x67B4B7
  -> AptPalantir 0x6D4789
     (non-null object and Palantir +0x10 -> +0xC4 hero select)
  -> wrapper 0x92C321
  -> OnHeroAttacked 0x92BFED
```

The hit reaches this path only when the target template is not both
`UNATTACKABLE` and `MOVE_ONLY`, actual damage is positive, damage is neither
`PENALTY` (10) nor `HEALING` (7), death type is not `DEATH_FADED` (`0x16`),
damage subtype is not 2, the owner's bit is absent from the source-player mask,
and the owner is the local player (`0x6AA7B2`, `Player::isLocalPlayer`). The
hero notification is outside `ShouldPlayUnderAttackEva` and outside the radar
membership gate. It is also reached only when the shock-wave handler reaches
the normal Drawable tail: `Object::doAttemptDamage` calls `shockWave` at
`0x6968BC`; the normal tail at `0x696E4D` reaches the Drawable, while
`RESIST_KNOCKBACK >= 1` exits before that tail.

The port preserves the simulation-side distinction by recording an ordinary
frame/sequence/object-ID client event from `ObjectClientHooks::heroAttacked`.
`DrawableManager::applyEvents` dispatches only that ID to a render-side
callback. A missing callback consumes and ignores the event, matching the
missing-interface behavior rather than retaining an unbounded queue. InGameHud
installs the callback after constructing the hero select and clears it before
destroying the hero select.

Pseudocode:

```text
if template is UNATTACKABLE && MOVE_ONLY: return
if actualDamage <= 0 or penalty/healing or faded or subtype == 2: return
if owner bit is in sourcePlayerMask: return
if shouldPlayUnderAttackEva: update radar attack frame
if shockWave reached Drawable tail && owner is local && template is HERO:
    record HERO_ATTACKED(frame, sequence, object.id)

when render applies the event:
    if hero-select handler exists:
        handler(object.id)

OnHeroAttacked(id):
    for slot = 0 .. 15:
        if displayed slot hero id == id:
            PlayButtonAttackedEffect(slot + 1)
            return
```

Repeated events in the same logic frame remain ordered events and are not
coalesced. An ID not present in a displayed slot produces no movie call.

## Builder-cycle timeout

RotWK `0x92BA91` reads `InGameUI + 0x988`, the unsigned
`SelectNearestBuilderCycleTimeOut` field, multiplies it by the float `0.03`
(the value at private PE `0xD9F624`), truncates to client frames, adds the
current `GameClient` client frame (vslot `0x7C`), and stores the deadline at
the implementation's `+0x1DC`. The field table is at `0xC12850` and its
parser is `parseUnsignedInt` at `0x42ECB2`. Both RotWK and BFME2 retail INI
data set the field to 3500 ms, producing 105 client frames with the retail
24-bit multiply.

`0x92D9F4` resets the used round when more than one object is selected or when
a builder was used and `deadline <= current client frame`. It then retains the
existing selection and reset-on-exhaustion behavior. A successful selection
marks the builder used and rearms the deadline from the current client frame.
The port loads the nested `InGameUI` block through the project INI parser,
reports missing/malformed data as a HUD boot error, and feeds the hero select
from Radar's 30 Hz client clock before deferred presses.

## Script-driven hero flashing

`HERO_SELECT_BUTTON_FLASH` is action ordinal 512, with a template-name parameter
(type 62) and integer seconds (type 0). `0x7BDD7A` multiplies seconds by the
30 Hz client rate with 32-bit integer wrapping, then calls `0x6D46E1`. That
Palantir wrapper checks its hero-select interface at `+0x10 -> +0xC4`, then
`0x92CB6B` reaches `FlashHeroButton` at `0x92C876`.

The implementation walks every hero list node, resolves its object ID (`+8`),
compares the object's template name case-sensitively, and replaces the node's
signed flash countdown (`+0x10`) only when the requested count is greater.
There is no player parameter, substring matching, or single-match early exit.
Negative requests cannot reduce an existing nonnegative countdown.

```cpp
// RW 0x7BDD7A, 0x6D46E1, 0x92CB6B, 0x92C876
void FlashHeroButton(const AsciiString &templateName, int frames)
{
    for (Hero &hero : heroes) {
        Object *object = TheGameLogic->findObjectByID(hero.id);
        if (object && object->getTemplate()->getName() == templateName
            && hero.flashFrames < frames)
            hero.flashFrames = frames;
    }
}
```

The port dispatches an ordered `HERO_FLASH` client event, not a simulation-thread
HUD call. `DrawableManager` consumes it exactly once; an absent HUD callback
consumes and ignores it, as the absent retail interface does. The HUD installs
and clears the callback with its hero-select lifetime. The hero record caches
the immutable template identity while logic is held, avoiding live-object reads
from the render-side callback. The existing per-logic-frame list synchronization
remains a port inference, not a claim of retail creation/destruction ordering.
The existing update drives `SetButtonFlashEffectState` and decrements the count.

The required-retail test executes action 512 through `ScriptActions`, publishes
through `LiveGame`, and checks two same-template heroes, a different template,
case sensitivity, disabled/negative/shorter requests, extension, and movie
show/hide calls.

Verification on the existing `engine/build`: `cmake --build engine/build -j2`
passed. The focused retail test passed on the authored patch (**1 case, 25/25
assertions**). For the red baseline, only `HERO_SELECT_BUTTON_FLASH` was removed
from `ScriptActions::clientActions`; the APIs and test remained present. That
run failed behaviorally (**1 case, 18/25 assertions passed; 7 failed**), including
action recognition, countdown publication, and movie flash calls. Restoring the
action registration produced the focused green result (**1 case, 25/25
assertions passed**). The bounded regression
`ui4*,hud6*,script2 camera*` passed (**32 cases, 1,427/1,427 assertions**), with
no fixture-skip messages; doctest's 3,222 filtered-out cases are not skips.

## Remaining S-2521 blockers

The exact Create-a-Hero runtime-image getter remains unported:
`0x73D0BA -> 0x619562` looks up a
record by OBJECT ID and returns the non-null runtime record's `+0x140` image,
otherwise falling back to the template path at `0x73CFEF`. The current
`InGameHeroSelect::buttonImage` instead resolves the object's model-state
`ButtonImageName` for a custom hero, then falls back to the template's
`ButtonImage`. `PalantirCommandUI::objectPortrait` likewise resolves
model-state `PortraitImageName` for the Palantir. Neither path implements that
runtime record's `+0x140` image cache. These images remain separate from
recruitment button icons: RW 0x7810BC uses the
selected subclass ButtonImage, falling back to BuildingNoArt when absent.

Idle-builder rollover help is ported by S-2703. Full hero-object help
(`0x92BF34 -> 0x8EBC61`) still needs its separate model/renderer; neither that
renderer nor the Create-a-Hero portrait resource is claimed closed by the flash
tests.

## Follow-up dependency audit (S-2521 remains open)

The supplied modified-image caveat applies to this recovery too. The portrait
getter RW 0x73D0BA is understood, but the provider is not available in the port:
`CreateAHeroGame::heroOfObject` returns the serialized-style hero record,
which has no runtime Image corresponding to +0x140. The Create-a-Hero screen's
`OnTakePicture` now arms a window screenshot after the guard clip has drawn
twice. `AptMenuPlayer::takeCreateAHeroPicture` saves that frame as PNG in the
system pictures directory plus the localized `APPDATA:PictureFolder` subfolder;
it reports capture/write errors and completes the movie callback. This is a
saved screenshot, not a generated hero portrait resource. The model-state
images described above are already delivered to the HUD; the exact runtime
image cache and its lifetime/loading contract remain a separate gap.

Full hero help is also not a command-button tooltip. RW 0x92BF34 selects the
object-help provider, whose RW 0x8EBC61 produces two strings for the separate
renderer constructed by RW 0x9402E7 (implementation constructor RW 0x940211).
The title resolution includes disguise-template selection (RW 0x69194E),
drawable label override (RW 0x674F88), custom/Create-a-Hero naming (RW 0x68FA0F),
template DisplayName, veteran-horde member naming, and object display override
(RW 0x691B15). Its body conditionally appends rank/max rank, maximum health,
melee/ranged display damage, upgrade descriptions in mask-bit order, positive
battles-fought count and the template/drawable description (RW 0x674FD8).

In particular, RW 0x8EBA26 reads the object's cached melee/ranged display
integers at +0x5CC / +0x5D0, applies the damage-add and damage-multiplier queries
(RW 0x68C818 / 0x68C82D), and rounds at the recovered points. It is not a lookup
of one weapon's base damage. RW 0x8EBC61 also reads battles fought at object
+0x488. The current Object/Drawable model does not expose those display caches,
the battles counter or both drawable string-override contracts. Their writers
and lifecycle still need recovery; zero/default values would hide missing state.
RW 0x8EBB14's maximum-rank iterator/cap contract also needs integration rather
than an assumed fixed maximum. Existing experience and body-health accessors
alone are therefore insufficient for full help.

No portrait or full-help closure is claimed, and no replacement subclass icon,
invented damage value or command-button help model is introduced by this audit.
There is no retail red/green result for these blocked paths.
