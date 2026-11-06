// OpenBFME. GPL-3.0.
//
// AIPositionSampler: the skirmish AI player info record's position sampler (record + 0x10, constructed by RW 0x8E4042, filled by RW 0x8E4456,
// rebuilt by RW 0x8E4475, removed by RW 0x8E46A5). It groups the player's structures into bounding spheres with DefenseTreeNodeRadius padding;
// the DEFENSIVE target heuristic (RW 0x97D74F) asks each group for its centre, radius and member count.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly):
//   * the object (0x24 bytes, ctor RW 0x8E4042): + 0 the vtable RW 0xC78294, + 4 the registration list (a vector of ObjectIDs in registration
//     order), + 0x10 the groups (a vector of group pointers), + 0x1C the padding (DefenseTreeNodeRadius, manager + 0x984 = data + 0x974), + 0x20
//     the manager; the vtable's slot 0 (RW 0x8E4082's call at RW 0x8E4319) returns the padding.
//   * a group (0x28 bytes, ctor RW 0x68223E with the padding): + 0 a "selected" byte, + 4 the grown radius (RW 0x8E4082 writes its weight there),
//     + 8 the centre, + 0x14 the group's copy of the padding, + 0x18 a "used" byte (set / read only by RW 0x9E7095 / 0x9E70A6, not by the decisions this
//     class serves), + 0x1C the members (the Group's fields below in memory order: the grown radius first, then the centre).
//   * insert RW 0x8E42C8: every group is marked unselected; RW 0x8E41EB selects each group that has a member within 2 * padding^2 of the new
//     object (squared 3D, RW 0x8E3DD6) and appends the object to the first selected group (RW 0x8E4082); if none was selected a new group is
//     made; every other selected group is then appended to the first and destroyed (RW 0x8E4376).
//   * append RW 0x8E4082: a group's centre and radius grow to contain the new point: when the point is outside (radius - padding)^2 the
//     centre moves by half the difference along the direction to the point and the radius becomes (padding + old radius + distance) / 2,
//     all in SSE float32 (RW 0x8E4082's movss / mulss / addss), the direction and distance through the x87 length RW 0x403111 / 0x403175.
//   * remove RW 0x8E4475: destroy every group, then insert every object of the registration list again (the caller RW 0x8E46A5 erased the
//     object from that list first).
//   * the registration add RW 0x8E4456 pushes the id onto + 4 and inserts it; the OFFENSIVE BUILDINGS of the army definition (+ 0x98) are
//     filtered by the caller RW 0x8E3EF8, not here.
//
// DONOR (reading aid, not the authority): Open-BFME-2 Rva00283114GroupMerge.cpp / Rva00282B26.cpp / Rva004DFA43Sphere.cpp /
// Rva004DFCE6SphereMerge.cpp; the port follows the RW disassembly above. The same class is upstream PR #151's AIPositionSampler (BFME2 donor).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <functional>
#include <vector>

class StateHasher;
class Xfer;

class AIPositionSampler
{
public:
	struct Group
	{
		float radius = 0.0f;      ///< + 0x14
		Coord3D center{};         ///< + 0x04
		bool selected = false;    ///< + 0x00 (RW's per-insert mark)
		bool used = false;        ///< + 0x18: only the terrain's tree index sets it (RW 0x9E7095); its owner hashes and saves it (TerrainTrees), not this class
		std::vector<ObjectID> members; ///< + 0x1C
	};
	// the position of an object at the time it is inserted (retail asks the game for both objects, RW 0x8E3DD6 -> RW 0x8E4082's + 0x20)
	using Position = std::function<Coord3D(ObjectID)>;

	// RW 0x8E4456 / 0x8E4082
	void add(ObjectID id, float padding, const Position &position);
	// RW 0x8E4475
	void remove(ObjectID id, const Position &position);
	// RW 0x8E42C8
	void insert(ObjectID id, const Position &position);
	// RW 0x682F3D (the tree index's copy): destroy every group and drop the registrations
	void clear()
	{
		m_members.clear();
		m_groups.clear();
	}

	const std::vector<ObjectID> &members() const { return m_members; } ///< + 0x04
	const std::vector<Group> &groups() const { return m_groups; }       ///< + 0x10
	std::vector<Group> &groups() { return m_groups; }

	void crc(StateHasher &h) const;
	void xfer(Xfer &x);

private:
	void append(Group &group, ObjectID id, const Position &position);

	float m_padding = 0.0f;          ///< + 0x1C
	std::vector<ObjectID> m_members; ///< + 0x04
	std::vector<Group> m_groups;     ///< + 0x10
};
