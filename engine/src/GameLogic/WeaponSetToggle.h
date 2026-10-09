// OpenBFME. GPL-3.0.
//
// WeaponSetToggle (lane HUD-4; RotWK, no ZH counterpart): the weapon set toggle of a selection (the Rohirrim's bow / spear, the trolls' and Ents' rock
// throw), MSG_WEAPONSET_TOGGLE (1111), and the Object weapon set flag setters it uses.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the command button (GUI_COMMAND_TOGGLE_WEAPONSET = 45, processCommandUI case RW 0x9412C8): MSG_WEAPONSET_TOGGLE with ONE object id argument, the control
//     bar's context drawable's object id (drawable + 0xFC -> object + 0x74; 0 without one), then the button's client side (RW 0x75D22E(object, 1)); its
//     availability (RW 0x942FC6): restricted when the object's contain (object + 0x258) is not a horde contain (slot 0x7C) and holds someone (slot 0x114),
//     else available;
//   * the logic case (dispatcher RW 0x77B529, jump table RW 0x77D127 index 19): the reference object is argument 0's object (none: each member decides for
//     itself). With a reference, the wanted state is NOT the reference's WEAPONSET_TOGGLE_1 (weapon set flag 24, Object + 0x38C byte 3 bit 0). For each id
//     of the issuing player's group, in group order (RW 0x771A36), the live object:
//       - is skipped when its contain is not a horde contain and holds someone (the same test as the availability);
//       - with a reference: is skipped unless its template is equivalent to the reference's (RW 0x73D5C2), or both templates are KindOf HOBBIT
//         (template + 0x118 bit 1, kind-of mask at + 0x108);
//       - without a reference takes NOT its own flag as the wanted state; with one is skipped when its flag already is the wanted state;
//       - sets (RW 0x691059) or clears (RW 0x691106) flag 24 on itself, or, when it belongs to a horde (RW 0x693A1A(0)), on every member of the horde's
//         contain (slot 0x108's list) and then on the horde (RW 0x6945DA / 0x694636);
//       - then, with an AI (object + 0x260) that is not moving (RW 0x664485): when the object or its container is ATTACKING (model condition 0x25,
//         RW 0x694154) the AI goes idle (RW 0x5E821A, CMD_FROM_AI) and the object ignores AI commands until frame + LOGICFRAMES_PER_SECOND (RW 0x6907BD:
//         status 0x4A and Object + 0x448); and its FiringTracker (object + 0x248) is reset hard (RW 0x8E302A(1)).
//   * Object::setWeaponSetFlag (RW 0x691059) / clearWeaponSetFlag (RW 0x691106): the bit of Object + 0x38C, WeaponSet::updateWeaponSet (RW 0x6C99E2), then
//     the model condition the table RW 0xC16958 maps the weapon set bit to (-1 none) is set / cleared when it changes (notify RW 0x68B53C), and for the model
//     conditions WEAPONSET_TOGGLE_1 .. 3 (0x12D .. 0x12F) the special model condition SWAPPING_TO_WEAPONSET_1 .. 3 (0x1BD .. 0x1BF) for LOGICFRAMES_PER_SECOND
//     frames (Object + 0x238, SMCHelper RW 0x8E2C0F) -- on set AND on clear.
// INFERENCE: the flag of an object without a weapon set lives in Object (Object::getWeaponSetFlags); the toggle button's image choice
// (TOGGLE_IMAGE_ON_WEAPONSET) and its image index update (RW 0x75D22E) are the client's (stop S-1672).

#pragma once

class GameLogicDispatch;
class Object;

#include <string>
#include <vector>

namespace WeaponSetToggle
{
enum
{
	WEAPONSET_TOGGLE_1 = 24 ///< TheWeaponConditionNames index (RW 0xDA1328)
};
// RW 0x691059 (on) / 0x691106 (off)
void setObjectWeaponSetFlag(Object &obj, int bit, bool on);
// RW 0xC16958: the model condition a weapon set bit maps to, -1 none
int modelConditionForWeaponSetBit(int bit);
// RW 0x942FC6: the toggle is restricted while a non-horde contain of the object holds someone
bool isRestrictedByContain(const Object &obj);
// the dispatcher case RW 0x77B529
void registerHandlers(GameLogicDispatch &dispatch);
std::vector<std::string> acceptanceStops();
} // namespace WeaponSetToggle
