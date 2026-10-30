// OpenBFME. GPL-3.0.
// DONOR: Open-BFME-2 Common/Rva00283114GroupMerge.cpp, Rva00282B26.cpp,
// Rva004DFA43Sphere.cpp, Rva004DFCE6SphereMerge.cpp. TARGET: RW 0x8E42C8,
// 0x8E4456 / 0x8E4475 / 0x8E46A5 (S-001 provenance caveat).
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
		float radius = 0.0f;
		Coord3D center{};
		bool selected = false;
		std::vector<ObjectID> members;
	};
	using Position = std::function<Coord3D(ObjectID)>;
	void add(ObjectID id, float padding, const Position &position);
	void remove(ObjectID id, const Position &position);
	const std::vector<ObjectID> &members() const
	{
		return m_members;
	}
	const std::vector<Group> &groups() const
	{
		return m_groups;
	}
	std::vector<Group> &groups()
	{
		return m_groups;
	}
	void crc(StateHasher &h) const;
	void xfer(Xfer &x);

  private:
	float m_padding = 0.0f;
	std::vector<ObjectID> m_members;
	std::vector<Group> m_groups;
	void insert(ObjectID id, const Position &position);
	void append(Group &group, ObjectID id, const Position &position);
};
