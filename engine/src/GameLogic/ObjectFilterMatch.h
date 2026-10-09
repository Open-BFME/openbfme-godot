// OpenBFME. GPL-3.0.
//
// ObjectFilterMatch: what a parsed ObjectFilter (GameLogic/ObjectFilter.h, the data) DECIDES for a template and two players. Lane ECON-1 (the economy
// asks it for the resource modifier filter, UpgradeMustBePresent, CostModifierUpgrade's ObjectFilter, RefundDie's BuildingRequired, CommandPointsUpgrade's
// RequiredObject); written against ObjectFilter.h unchanged so the lanes that own that header are not touched. Include it from .cpp files only (ObjectFilter.h
// cannot be included next to Common/ModelState.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read by ECON-1):
//   * the filter handle is a table index, -1 = never set (RW 0x762977 "valid": the index is in range AND the entry's flag byte (+0x88) is set; an unset
//     field is therefore "no filter"). A field parsed with NONE has flag 0: also not valid. This port keeps a filter field as an optional value
//     (`std::shared_ptr<const ObjectFilter>`, null = unset); isValid() is RW 0x762977.
//   * allow (RW 0x763543, thiscall, args template, the object's controlling player A, the other player B; the Object form RW 0x7640C1 passes the object's
//     template and its controlling player): null template -> false. Then, in this order:
//       1. side (+0x90): EVIL needs A's PlayerTemplate to be Evil (RW +0x1BC), GOOD needs it not to be;
//       2. relationships (+0x84) != 0: A and B must both exist; rel = B's relationship to A's default team: ENEMIES needs bit 2, NEUTRAL bit 4, ALLIES
//          bit 1 or (A and B the same player and bit 8); anything else fails;
//       3. the `S:` names of the include list (a name that starts with "S:", resolved without the prefix by findTemplate at RW's resolve pass): the
//          template's own name equal to one -> TRUE; the `S:` names of the exclude list equal -> FALSE;
//       4. the other include names (resolved templates): ThingTemplate::isEquivalentTo (RW 0x73D5C2) true for one -> TRUE; the exclude names likewise -> FALSE;
//       5. the exclude KindOf mask (+0x64) non-empty and sharing a bit with the template's KindOf -> FALSE;
//       6. the rule (+0x80): 3 ALL (also what rule 0 becomes) -> TRUE; 2 ANY -> the include mask (+0x48) non-empty and sharing a bit; 1 NONE -> the include
//          mask non-empty and a subset of the template's KindOf (RW 0x70B8C7 with an empty must-be-clear mask); an empty include mask ends in `rule == ALL`.
//   * isEquivalentTo (RW 0x73D5C2): same template, or the same final override, or one's EquivalentTo list (tt + 0x33C) names the other, or the two EquivalentTo
//     lists share a name, or one's BuildVariations list (tt + 0x330) names the other. Names compare case-sensitively (AsciiString, RW 0x4065AA / 0x4065D4).
// INFERENCE: an include or exclude name that no template carries is skipped here; retail's resolve pass reports it as an INI error (validate() finds them).

#pragma once

#include "Common/StateHash.h"

#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class GameLogic;
class Object;
class Player;
class ThingFactory;
class ThingTemplate;

namespace ObjectFilterMatch
{
// RW 0x762977: set and its flag byte non-zero
bool isValid(const ObjectFilter *filter);
// the OpenBFME state hash of an optional filter (its whole content)
void crc(StateHasher &hasher, const ObjectFilter *filter);

// RW 0x73D5C2
bool isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b);

// RW 0x763543
bool allows(GameLogic &logic, const ObjectFilter &filter, const ThingTemplate *tmpl, const Player *objectPlayer, const Player *otherPlayer);
// RW 0x7640C1: the object's template and controlling player
bool allows(GameLogic &logic, const ObjectFilter &filter, const Object &obj, const Player *otherPlayer);

// names of the include / exclude lists that no template in `things` carries (retail: an INI error at the resolve pass)
std::vector<std::string> unresolvedNames(const ThingFactory &things, const ObjectFilter &filter);
} // namespace ObjectFilterMatch
