// OpenBFME. GPL-3.0.
// BFME1 1.03 collision system variant; later editions keep AIWorld's existing pass.
#pragma once
#include "GameLogic/ObjectTypes.h"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>
class GameLogic;
class Object;
class StateHasher;
class Xfer;
class Bfme1ContactManager
{
public:
	explicit Bfme1ContactManager(GameLogic &logic):m_logic(logic){}
	void changed(Object &);
	void removed(Object &);
	void update();
	void crc(StateHasher &) const;
	void xfer(Xfer &);
	unsigned contacts() const { return (unsigned)m_contacts.size(); }
	unsigned tests() const { return m_tests; }
	unsigned responses() const { return m_responses; }
private:
	using Key=std::pair<ObjectID,ObjectID>;
	struct Node {ObjectID id=0;std::uint64_t group=0;bool source=false,dirty=false;float lo[3]{},hi[3]{};};
	struct Endpoint {ObjectID id=0;bool maximum=false;};
	struct Contact {unsigned state=0;}; // 0 unknown, 1 hit, 2 miss (0x009A30B0).
	GameLogic &m_logic;
	std::map<ObjectID,Node> m_nodes;
	std::vector<ObjectID> m_dirty; // last marked first; already marked nodes keep their place.
	std::array<std::vector<Endpoint>,3> m_axes;
	std::map<Key,unsigned> m_pairs; // axis overlap count, not a traversal order.
	std::map<Key,Contact> m_contacts;
	std::array<std::vector<Key>,0x493> m_buckets;
	unsigned m_tests=0,m_responses=0;
	void dirty(Node &);
	Object *source(ObjectID) const;
	float value(const Endpoint &,unsigned) const;
	void add(ObjectID,ObjectID);
	void drop(ObjectID,ObjectID);
	void eraseContact(Key);
	void unlink(Node &);
	void insert(Node &,unsigned);
	void mergeSort(unsigned);
	unsigned refresh(Node &,bool);
	void respond(Object &,Object &);
};
