// OpenBFME. GPL-3.0.
//
// StealthLook (lanes STEALTH-1, STEALTHLOOK-1): the client side of RotWK invisibility, what the drawable shows for the local player. Presentation only:
// it reads the logic snapshot (ObjectSnapshot::stealthLook, InvisibilityManager::clientLook RW 0x81AA03) and never writes simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; same behaviour in BFME2 1.06: the step is tier A identical, the setter tier B same-shape - the only
// difference is the __LINE__ of its random draw, 0x1075 vs 0x1074; BFME2 sources Drawable_rva00275598.cpp:302-322 / DrawableRva00275DCE.cpp:51,
// readable BFME1 donor DrawableSetStealthLook.cpp:140-200):
//
//   * the look setter RW 0x6760F9(state, min, max, cycle): nothing when the state is +0x164 already; then +0xB8 = +0xBC = 1.0, +0xC0 = the cycle
//     clamped to 0.1 .. 60 (RW 0xBD83D4 / 0xBDC1F8), +0xD4 = ftol(30 * the UNclamped cycle) (fild of RW 0xD9F60C, fmul the argument, _ftol), +0xD0 =
//     +0xB4 (the running opacity is latched: every look change blends from it), +0x164 = state, and per state:
//       0 / 2: +0x43E = 0, +0x358 = 0;
//       1 / 4: +0xB8 = min, +0xBC = max, +0xC4 = (min + max) * 0.5, +0xC8 = (max - min) * 0.5 (0.5 at RW 0xBD869C), +0xCC =
//         GetGameClientRandomValueReal(0, pi) (RW 0x6D33AB; pi at RW 0xBDD388 = 3.1415927410125732f), +0x43E = 0, +0x358 = (state == 4);
//       3: +0x43E = 0, +0x358 = 1.0;
//       5: +0x43E = (+0x3AB == 0), +0x358 = 0; other states leave +0x43E / +0x358 untouched;
//     then the draw modules are refreshed (RW 0x67396E(1) / 0x671841 / 0x67396E(0), the slot-0xEC walk and the hidden-status report - not ported, our
//     render reads the state every frame).
//   * +0x3AB is written by the Drawable constructor (RW 0x679AE8): 0 unless a draw module's ModuleData fails its vtable +0x78 query (the model draw
//     family's is RW 0x464A02 `mov al, [ecx + 0x15E]` = the data's AffectedByStealth; the base ModuleData's is RW 0x8BD372 `return 1`). So a drawable
//     hides for look 5 only while every draw module's data is affected by stealth.
//   * the per-client-frame step RW 0x673114 (called from updateDrawable RW 0x675996 at 0x675F8C, run once per render frame by GameClient::update RW
//     0x64849E, and once by StealthUpdate::changeVisualDisguise RW 0x776F03):
//       - the object's stealth parent is Object::rva002931F5(false) (RW 0x693A1A: itself or its container when that carries the HORDE bit, template
//         +0x114 & 0x2000): a member whose parent is not itself takes the parent drawable's +0xB4 / +0x43E / +0x164 verbatim each frame (a missing
//         parent drawable ends the call early; +0x358 is NOT copied);
//       - else +0xB8 != +0xBC: pulse = +0xC4 + +0xC8 * sin(+0xCC); blend = (30 * +0xC0 - +0xD4) / (30 * +0xC0); +0xD4 > 0 decrements; +0xB4 =
//         pulse * blend + +0xD0 * (1 - blend); +0xCC += (RW 0xD9F62C / +0xC0) * pi;
//       - else +0xB8 == +0xBC and +0xB4 < 1.0: the same blend, +0xB4 = +0xD0 * (1 - blend) + blend (fades back to opaque; a pulse range with
//         min == max goes here too - it blends to 1.0, it does not pulse);
//   * globals: RW 0xD9F60C = BfmeClientFramesPerSecond = 30, RW 0xD9F62C = opacityLogicStepSeconds = 1/30 (0x3D088889; the client frame step), RW
//     0xD9F608 = the logic rate 5 (the invisible refresh RW 0x81AA85 passes cycle = InvisibilityOpacityCycleFrames / 5; StealthUpdate::update RW
//     0x777AF7 passes cycle = PulseFrequency * 0.2f). BFME2 twins: 0xDBA4E8 = 30, 0xDBA508 = 1/30, 0xDBA4E4 = 5 - the same values.
//   * ctor defaults (RW 0x679AE8): +0xB4 / +0xB8 / +0xBC / +0xC0 / +0xC4 / +0xCC = 1.0, +0xC8 = +0xD0 = 0, +0xD4 = 10, +0x164 = 0, +0x358 = 0,
//     +0x43E = +0x3AB = 0.
//   * looks 3 / 4 set +0x358 (the second material pass opacity): renderOneObject RW 0x46F48E (BFME2 0x6FB59, RTS3DSceneRenderOneObject.cpp:579-590)
//     draws a detected enemy as a heat-vision-only silhouette (look 3) and a detected friend as model + heat pass (look 4). Our renderer has no
//     material passes: stop S-1042, the flag is kept for when it does.
//
// THIS PORT: the state lives on the client Drawable (StealthLook::State); the snapshot's stealthLook drives setLook when it changes (retail's callers
// pass (look, min, max, cycle) unconditionally - the setter early-outs on an unchanged look, so calling on a change only is equivalent), and
// DrawableManager::advance runs update() once per client frame. The phase draw uses the draw modules' shared client random (W3DClientRandom, the
// GetGameClientRandomValueReal stream RW 0x6D33AB), never the logic RNG.

#pragma once

#include <cmath>

struct ObjectSnapshot;

class ModuleData;

namespace StealthLook
{

// the RotWK client frame rate and per-frame opacity step (RW 0xD9F60C = 30, RW 0xD9F62C = 1/30)
inline constexpr int kClientFramesPerSecond = 30;
inline constexpr float kStepSeconds = 0.03333333507180214f; // RW 0xD9F62C = 0x3D088889 (float32 1/30)
inline constexpr float kPi = 3.1415927410125732f;        // RW 0xBDD388
inline constexpr float kMinCycle = 0.1f;                 // RW 0xBD83D4
inline constexpr float kMaxCycle = 60.0f;                // RW 0xBDC1F8

// ZH's enum order (DrawableSetStealthLook.cpp / StealthLookType): 1 / 4 the friend's pulse, 3 the detected enemy, 5 the invisible enemy
enum Look
{
	LOOK_NONE = 0,
	LOOK_VISIBLE_FRIENDLY = 1,
	LOOK_DISGUISED_ENEMY = 2,
	LOOK_VISIBLE_DETECTED = 3,
	LOOK_VISIBLE_FRIENDLY_DETECTED = 4,
	LOOK_INVISIBLE = 5,
};

// Drawable +0xB4 .. +0xD4, +0x164, +0x358, +0x43E with the ctor's values (RW 0x679AE8)
struct State
{
	float opacity = 1.0f;      ///< +0xB4: the drawn opacity factor
	float inner = 1.0f;        ///< +0xB8: the pulse's min (1.0 on non-pulse looks)
	float outer = 1.0f;        ///< +0xBC: the pulse's max
	float cycle = 1.0f;        ///< +0xC0: the cycle argument clamped to 0.1 .. 60
	float middle = 1.0f;       ///< +0xC4: (min + max) / 2
	float halfRange = 0.0f;    ///< +0xC8: (max - min) / 2
	float phase = 1.0f;        ///< +0xCC: the sin phase, drawn in [0, pi) on each change to a pulse look, stepped per client frame
	float latched = 0.0f;      ///< +0xD0: the opacity the last look change blends from
	int blendLeft = 10;        ///< +0xD4: client frames of blend left (30 * the UNclamped cycle at the change)
	int look = LOOK_NONE;      ///< +0x164
	float secondPass = 0.0f;   ///< +0x358: the heat-vision second material pass opacity (not drawn: S-1042)
	bool hidden = false;       ///< +0x43E: hidden by stealth (drawn nothing) - only a look-5 set on a drawable that can hide
};

// RW 0x6760F9. `phaseDraw` is the client's random value for +0xCC, drawn by the caller only when the look actually changes to 1 / 4 (retail draws it
// inside the setter, after the early out); `cannotHide` is the drawable's +0x3AB (a draw module's data is not affected by stealth). Returns whether the
// hidden flag changed (retail reports that through RW 0x671841).
bool setLook(State &s, int look, float min, float max, float cycle, float phaseDraw, bool cannotHide);

// the +0xB8 != +0xBC / +0xB8 == +0xBC part of RW 0x673114: one client-frame step
void update(State &s);

// Compose the drawable's other presentation opacity (e.g. projectile fade) with its stealth opacity.
// Kept in the client-only translation unit so this arithmetic never enters the simulation audit.
float drawOpacity(float baseOpacity, const State &s);

// the parent branch of RW 0x673114: the member takes the parent drawable's +0xB4 / +0x43E / +0x164. Returns whether +0x43E changed
bool imitate(State &s, const State &parent);

// the draw module data's +0x78 query the ctor ANDs into +0x3AB (RW 0x464A02 for the model draw family, RW 0x8BD372 default 1): true unless the entry's
// data is a W3DModelDrawModuleData with AffectedByStealth clear
bool moduleDataAffectedByStealth(const ::ModuleData *data);

} // namespace StealthLook
