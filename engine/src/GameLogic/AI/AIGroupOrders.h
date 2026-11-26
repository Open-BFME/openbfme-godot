// OpenBFME. GPL-3.0.
// The group orders AIWorld's AIOrdersManager queues. Both editions.
//
// MoveToGroupOrder, the order MSG_DO_MOVETO / MSG_DO_ATTACKMOVETO hand the group manager (RW 0x75748C -> the constructor RW 0x94E232, vtable RW 0xC81430):
//   * the fields: destination + 0x18, attack flag + 0x24, the flag + 0x25 (the message was 0x442, MSG_DO_SALVAGE here: never set by a live command), the
//     per-object destinations (hash_map ObjectID -> Coord3D, + 0x28) and the stuck counter (+ 0x3C);
//   * activation, slot 0x10 RW 0x94E184 (BFME2 MoveToGroupOrder::rva005478B5, Rva 0x5478B5): the object's cached destination or the one RW 0x94DF97 works out
//     (rva005476C8, AIGroup::planningDestination) and keeps; an object that is gone, contained or without an AI takes nothing; then RW 0x94DF1B (rva0054764C):
//     the attack flag picks the attack-move (RW 0x696266) or aiMoveToPosition (RW 0x66C4CA) from source CMD_FROM_PLAYER;
//   * completion, slot 0x14 RW 0x94DC37 (BFME2 rva00547368, Rva 0x547368): an object that is gone, contained or without an AI is done; with no cached destination it
//     is done and the counter is cleared; at the destination (RW 0x94DB32 -> 0x6CA525: the squared distance from the edge of the object's bounding circle
//     below 20.0, RW 0xBDBC6C; Rva 0x547263) it is done and the counter is cleared; otherwise an idle AI (vslot 0x1B8, isIdle) counts up and is done on
//     the second count, a busy one clears the counter;
//   * member removal, slot 0x18 RW 0x9F3A3C: `ret 4`, nothing.
// The ObjectOrderQueue / AIOrdersManager core is RW 0x94EBD1 .. 0x94F19E and 0x7572FA (BFME2 Rva 0x5482E9 .. 0x548984, 0x3557B7).
//
// NOT PORTED (stop S-1831): the other order classes (attack object, garrison, formation move ...), the patrol start (RW 0x7571A7 / 0x756F76 / 0x756D05:
// clone and preparePatrol are the queue core's contract and no live command reaches them), the SHIP / AMPHIBIOUS destination branch (RW 0x94DA57 ->
// 0x770D27), the object + 0x27C branch (RW 0x754B18, the physics object's move), the pending-command block between the two queue passes of the AI update
// (RW 0x66E5C3 .. 0x66E653), the planning commit (RW 0x6AAC85) and the callers that clear a queue (RW 0x77B0DD, 0x8760F9, 0x88C704; stop is ported).
// INFERENCE: RotWK keeps the queue of an object that left the world; AIWorld drops it (AIOrdersManager::forget), so a save names live objects only.

#pragma once

#include "GameLogic/AI/AIOrdersManager.h"
#include "GameLogic/ObjectTypes.h"

#include "Common/INIDataTypes.h"

#include <map>
#include <vector>

class GameLogic;
class Object;
class StateHasher;
class Xfer;

class MoveToGroupOrder final : public AIOrdersManager::GroupOrder
{
public:
	MoveToGroupOrder(GameLogic &logic, std::vector<ObjectID> objects, const Coord3D &destination, bool attackMove);

	void activate(ObjectID object) override;
	bool complete(ObjectID object) override;
	void removed(ObjectID) override {}
	void preparePatrol(ObjectID) override {}
	std::unique_ptr<GroupOrder> clone() const override;

	const Coord3D &destination() const { return m_destination; }
	bool attackMove() const { return m_attackMove; }
	const std::map<ObjectID, Coord3D> &positions() const { return m_positions; }
	unsigned stuckCount() const { return m_stuck; }

	// RW 0x94DB32 (BFME2 Rva00547263, Rva 0x547263): true for an object that is effectively dead or whose edge is within the 20.0 of the target
	static bool closeEnough(const Object &object, const Coord3D &target);

	// the saved fields of one order (AIGroupOrders::Image)
	struct State
	{
		Coord3D destination{};
		bool attackMove = false;
		std::map<ObjectID, Coord3D> positions;
		unsigned stuck = 0;
	};
	void setState(const State &state);

private:
	GameLogic &m_logic;
	Coord3D m_destination;
	bool m_attackMove;
	std::map<ObjectID, Coord3D> m_positions;
	unsigned m_stuck = 0;
};

// The hash and the native save of the manager's state (stop S-4220: OpenBFME layout, not retail's CHUNK_AiOrdersManager). The save is one image: loading
// reads and checks it whole, install() then replaces the manager's state.
namespace AIGroupOrders
{
struct OrderImage
{
	AIOrdersManager::OrderId id = 0, clone = 0;
	std::vector<AIOrdersManager::ObjectId> objects;
	MoveToGroupOrder::State move;
};
struct Image
{
	AIOrdersManager::OrderId nextId = 0;
	std::vector<OrderImage> orders;
	std::vector<ObjectOrderQueue::State> queues;
};
// the manager's state as an image; a group order class this layout does not hold is an error
Image capture(const AIOrdersManager &manager);
void crc(const AIOrdersManager &manager, StateHasher &hasher);
// saving writes `image`, loading fills it (every id checked against the issued ids, every order against the live game); nothing of the manager changes
void xfer(Xfer &x, Image &image, GameLogic &logic);
// replaces the manager's state with a loaded image (the manager refuses an inconsistent one: an XferException naming why)
void install(AIOrdersManager &manager, Image &&image, GameLogic &logic);
} // namespace AIGroupOrders
