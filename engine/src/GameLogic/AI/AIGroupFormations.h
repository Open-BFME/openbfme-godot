// OpenBFME. GPL-3.0.
//
// AIGroup::createFormation (lane BACKLOG-8): the formation layout of an AI group, RW 0x94FCBA, and the helpers around it.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; tier B same-shape with BFME2 1.06: only struct offsets differ, tools/re/pair_diff.py):
//   * RW 0x94FCBA createFormation (the group's member list + 4, its path start + 0x18 and end + 0x24): a formation id from the AI (RW 0x6FF265); every
//     member goes into two near-to-far lists (SimpleObjectIterator) by its squared planar distance to the path end, loses its
//     formation id (Object + 0x42C) and adds max(FormationDepth * FormationWidth, 1) (template + 0x574 / + 0x578) to the area. With the squad width
//     w = 2 * AIData FormationColumns (+ 0xB4) the squad count is ceil(area / (6 w)), at most 12; each squad's row width is w clamped to 2 .. 6
//     (RW 0x94FA51). For the locomotor FormationPriority 0 .. 12 (current locomotor template + 0x78) in turn, the members of that priority in list order
//     are dealt round-robin to the squads (RW 0x94F869: at most 36 a squad).
//   * a squad's rows (RW 0x94FA6C): for each priority in turn its members fill a row until the summed FormationWidth passes the row width (at most 6
//     members a row; at most 10 rows); a row ends after the priorities 0, 3, 6, 9 and 12; the squad's height is the widest row's sum.
//   * a squad's size (RW 0x94FC3F): (rows * the FIRST row's largest FormationDepth * FormationRowDepth (AIData + 0xA4), height * FormationColumnWidth
//     (+ 0xA0)): the loop reads row 0 every time. Even squads are laid to the left (y = -size.y - FormationSquadSpacing / 2 (+ 0xA8)), odd ones to the
//     right (y = spacing / 2), each side advancing in x by its size.x + spacing; one squad alone is centred (y = -size.y / 2).
//   * the slots (RW 0x94FB5F / 0x94F90B): row i starts at x = origin.x - (the earlier rows' largest depths) * rowDepth + rowDepth / 2 and
//     y = origin.y + (height - the row's summed width) * columnWidth / 2; a member's slot is x - its depth * rowDepth / 2, y + (the widths before it +
//     its width / 2) * columnWidth, both halved for priority 0; the member gets the formation id (+ 0x42C) and the offset (+ 0x430 / + 0x434).
//   * the swap pass (the frame rotation RW 0x94F7C6): the anchor is the path start moved along the path direction by the first member's projection plus half a row; every
//     member in list order, at most six times, swaps its slot with the first later member of the same priority for which the swap brings it closer
//     (gain >= 0) and costs less than the old pair plus 0.3 * gain (the cost counts y twice, x once and a quarter of the x imbalance).
//   * the shift: the member with the largest x among those with y >= 0 (ties: the smallest y) is the front; every offset moves so that the front stands
//     at y = 0 (FormationWidth 1), or at y = columnWidth / 4 (unless its y is 0) and x = -rowDepth / 4 (a wider front).
//   * RW 0x76E90A rotateOffset(position, goal, offset): the offset turned from the formation's frame (x along the direction position -> goal) to the world.
//   * RW 0x76FFF0: the members a formation command keeps: not held, an AI with a current locomotor (AI + 0x1F0), not KindOf IMMOBILE.
// DONOR: BFME2 decomp Code/GameEngine/Source/GameLogic/AI/AIGroupFormations.cpp (AIGroup::createFormation and its helpers, byte-matched),
// AIGroup.cpp (AIGroup::groupSetPositionInFormation and AIGroup::rva0036E357).
// INFERENCE (stop S-4460): the port's near-to-far order of members at equal distance is the group's order (RW sorts with STLport's unstable sort).
// lane BACKLOG-8 r2: RW 0x76FFF0's other two tests are identified: Object + 0x1C8 bit 3 is DISABLED_HELD, template + 0x108 bit 2 is KindOf IMMOBILE.

#pragma once

#include "Common/INIDataTypes.h"

#include <vector>

class GameLogic;
class Object;

namespace AIGroupFormations
{
// RW 0x76FFF0
std::vector<Object *> validMembers(const std::vector<Object *> &members);
// RW 0x94FCBA: `members` in group order (every one with an AI and a current locomotor: validMembers), `start` / `end` the group's path ends
void createFormation(GameLogic &logic, const std::vector<Object *> &members, const Coord3D &start, const Coord3D &end);
// RW 0x76E90A
void rotateOffset(const Coord3D &position, const Coord3D &goal, Coord2D &offset);
// the stop line S-4460 (raised by every formation)
const char *stopLine();
} // namespace AIGroupFormations
