// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIGroupOrders.h.

#include "GameLogic/AI/AIGroupOrders.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace
{
const float kCloseEnoughSquared = 20.0f; // RW 0xBDBC6C
}

MoveToGroupOrder::MoveToGroupOrder(GameLogic &logic, std::vector<ObjectID> objects, const Coord3D &destination, bool attackMove)
	: GroupOrder(std::move(objects))
	, m_logic(logic)
	, m_destination(destination)
	, m_attackMove(attackMove)
{
}

bool MoveToGroupOrder::closeEnough(const Object &object, const Coord3D &target)
{
	if (object.isEffectivelyDead())
	{
		return true; // RW 0x94DB3A: Object + 0x458 bit 0 (taken as the effectively dead flag, as AIUpdateHordeMember does)
	}
	// RW 0x6CA525 is RW 0x6634BF with one radius: the same x87 arithmetic, the object's own bounding circle (+ 0xB8) taken off the distance
	const float d2 = CombatQueries::edgeDistanceSquared2D(*object.getPosition(), CombatQueries::boundingCircleRadius(object), target, 0.0f);
	return kCloseEnoughSquared > d2;
}

void MoveToGroupOrder::activate(ObjectID id)
{
	Object *object = m_logic.findObjectByID(id);
	if (!object || object->isDestroyed())
	{
		return; // RW 0x94E1CB: no object
	}
	AIUpdateInterface *ai = object->getAIUpdateInterface();
	if (!ai || ai->isContained())
	{
		return; // RW 0x94E1D1 / 0x94E1DA: contained (RW 0x6939DF) or no AI (+ 0x260): nothing
	}
	// lane MODULES-3 r2: the order passes aiDoCommand's gate (RW 0x667174) before anything of the unit changes (the destination reserves a goal)
	if (!ai->acceptCommand(CMD_FROM_PLAYER, 0))
	{
		return;
	}
	Coord3D dest;
	const auto cached = m_positions.find(id);
	if (cached != m_positions.end())
	{
		dest = cached->second; // RW 0x94E1B2
	}
	else
	{
		dest = AIGroup::planningDestination(m_logic, *object, m_destination); // RW 0x94DF97 (rva005476C8): kept for the completion test
		m_positions.emplace(id, dest);
	}
	ai->aiMoveToPosition(dest, CMD_FROM_PLAYER); // RW 0x94DF8C: RW 0x66C4CA(pos, 0)
	if (m_attackMove)
	{
		ai->armAttackMove(ai->stateMachine().goalPosition()); // RW 0x94DF82: the attack-move RW 0x696266(pos, 0x7FFFFFFF, 0), the port's attack-move (S-328)
	}
}

bool MoveToGroupOrder::complete(ObjectID id)
{
	Object *object = m_logic.findObjectByID(id);
	if (!object)
	{
		return true; // RW 0x94DC52
	}
	AIUpdateInterface *ai = object->getAIUpdateInterface();
	if (!ai || ai->isContained())
	{
		return true; // RW 0x94DC58 / 0x94DC61
	}
	const auto cached = m_positions.find(id);
	if (cached == m_positions.end())
	{
		m_stuck = 0; // RW 0x94DC95
		return true;
	}
	if (closeEnough(*object, cached->second))
	{
		m_stuck = 0;
		return true;
	}
	if (ai->isIdle()) // RW 0x94DCA6: AI vslot 0x1B8
	{
		return ++m_stuck > 1; // RW 0x94DCB0: done on the second count
	}
	m_stuck = 0; // RW 0x94DCBF
	return false;
}

std::unique_ptr<AIOrdersManager::GroupOrder> MoveToGroupOrder::clone() const
{
	// the C++ copy (BFME2's MoveToGroupOrder copy constructor was not read); only a patrol reaches it and no live command starts one (S-1831)
	auto copy = std::make_unique<MoveToGroupOrder>(m_logic, objects(), m_destination, m_attackMove);
	copy->m_positions = m_positions;
	copy->m_stuck = m_stuck;
	return copy;
}

void MoveToGroupOrder::setState(const State &state)
{
	m_destination = state.destination;
	m_attackMove = state.attackMove;
	m_positions = state.positions;
	m_stuck = state.stuck;
}

namespace AIGroupOrders
{
namespace
{
const MoveToGroupOrder &moveOrder(const AIOrdersManager::GroupOrder &order)
{
	const auto *move = dynamic_cast<const MoveToGroupOrder *>(&order);
	if (!move)
	{
		throw std::logic_error("AIOrdersManager holds a group order class the native layout does not save (S-1831)");
	}
	return *move;
}

void xferState(Xfer &x, MoveToGroupOrder::State &s)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::coord(x, s.destination);
	x.xferBool(&s.attackMove);
	AIXfer::map(x, s.positions, 16, [&](std::uint32_t &id, Coord3D &c) {
		x.xferUnsignedInt(&id);
		AIXfer::coord(x, c);
	});
	AIXfer::uns(x, s.stuck);
}
} // namespace

Image capture(const AIOrdersManager &manager)
{
	Image image;
	image.nextId = manager.nextId();
	for (const auto &kv : manager.orders())
	{
		const MoveToGroupOrder &move = moveOrder(*kv.second);
		OrderImage o;
		o.id = kv.first;
		o.clone = kv.second->cloneId();
		o.objects = kv.second->objects();
		o.move.destination = move.destination();
		o.move.attackMove = move.attackMove();
		o.move.positions = move.positions();
		o.move.stuck = move.stuckCount();
		image.orders.push_back(std::move(o));
	}
	for (const auto &kv : manager.queues())
	{
		image.queues.push_back(kv.second.state());
	}
	return image;
}

void crc(const AIOrdersManager &manager, StateHasher &h)
{
	h.addU32(manager.nextId());
	h.addU32((std::uint32_t)manager.orders().size());
	for (const auto &kv : manager.orders())
	{
		const MoveToGroupOrder &move = moveOrder(*kv.second);
		h.addU32(kv.first);
		h.addU32(kv.second->cloneId());
		h.addU32((std::uint32_t)kv.second->objects().size());
		for (AIOrdersManager::ObjectId id : kv.second->objects())
		{
			h.addU32(id);
		}
		h.addFloat(move.destination().x);
		h.addFloat(move.destination().y);
		h.addFloat(move.destination().z);
		h.addBool(move.attackMove());
		h.addU32((std::uint32_t)move.positions().size());
		for (const auto &p : move.positions())
		{
			h.addU32(p.first);
			h.addFloat(p.second.x);
			h.addFloat(p.second.y);
			h.addFloat(p.second.z);
		}
		h.addU32(move.stuckCount());
	}
	h.addU32((std::uint32_t)manager.queues().size());
	for (const auto &kv : manager.queues())
	{
		const ObjectOrderQueue::State s = kv.second.state();
		h.addU32(s.object);
		h.addU32((std::uint32_t)s.orders.size());
		for (AIOrdersManager::OrderId id : s.orders)
		{
			h.addU32(id);
		}
		h.addU32(s.planned);
		h.addU32(s.patrolStart);
		h.addU32(s.nextPatrolStart);
	}
}

void xfer(Xfer &x, Image &image, GameLogic &logic)
{
	(void)logic;
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::uns(x, image.nextId);
	x.xferVectorSize(image.orders, 12);
	for (OrderImage &o : image.orders)
	{
		AIXfer::uns(x, o.id);
		AIXfer::uns(x, o.clone);
		AIXfer::ids(x, o.objects);
		if (x.isLoading())
		{
			for (std::uint32_t id : o.objects)
			{
				AIXfer::issued(id, "orders: an order's object id");
			}
		}
		xferState(x, o.move);
	}
	x.xferVectorSize(image.queues, 16);
	for (ObjectOrderQueue::State &s : image.queues)
	{
		AIXfer::objectId(x, s.object, "orders: a queue's object id");
		AIXfer::ids(x, s.orders);
		AIXfer::uns(x, s.planned);
		AIXfer::uns(x, s.patrolStart);
		AIXfer::uns(x, s.nextPatrolStart);
	}
}

void install(AIOrdersManager &manager, Image &&image, GameLogic &logic)
{
	for (const ObjectOrderQueue::State &s : image.queues)
	{
		if (!logic.findObjectByID(s.object))
		{
			throw XferException("orders: the queue of object " + std::to_string(s.object) + ", which the loaded game lacks");
		}
	}
	std::vector<AIOrdersManager::OrderRecord> records;
	for (OrderImage &o : image.orders)
	{
		for (AIOrdersManager::ObjectId id : o.objects)
		{
			if (!logic.findObjectByID(id))
			{
				throw XferException("orders: order " + std::to_string(o.id) + " names object " + std::to_string(id) + ", which the loaded game lacks");
			}
		}
		auto order = std::make_unique<MoveToGroupOrder>(logic, std::move(o.objects), o.move.destination, o.move.attackMove);
		order->setState(o.move);
		AIOrdersManager::OrderRecord r;
		r.id = o.id;
		r.clone = o.clone;
		r.order = std::move(order);
		records.push_back(std::move(r));
	}
	std::string why;
	if (!manager.restore(image.nextId, std::move(records), image.queues, &why))
	{
		throw XferException("orders: " + why);
	}
}
} // namespace AIGroupOrders
