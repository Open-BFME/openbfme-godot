// OpenBFME. GPL-3.0.
// Shared group orders and their per-object queues, for both editions.
// TARGET: RW 0x7572FA (addOrderToObjectQueues) and 0x94F19E (remove member),
// caveat S-001. BFME2 1.06: Rva 0x3557B7 (AiOrdersManager.cpp) and
// Rva 0x548984 (Rva00548984Order.cpp), Open-BFME-2 byte-matched rows.
// AIWorld owns one (TheAiOrdersManager, RW 0xDE7860); MoveToGroupOrder (AIGroupOrders.h) is its live order, saved and hashed through AIGroupOrders.
// Not ported (S-1831 / S-281): the other group order classes, the patrol chaining that starts a patrol (RW 0x7571A7), the planning commit.
#pragma once

#include "GameLogic/AI/ObjectOrderQueue.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class AIOrdersManager final : private ObjectOrderQueue::Owner
{
public:
	using ObjectId = ObjectOrderQueue::ObjectId;
	using OrderId = ObjectOrderQueue::OrderId;

	class GroupOrder
	{
	public:
		explicit GroupOrder(std::vector<ObjectId> objects) : m_objects(std::move(objects)) {}
		virtual ~GroupOrder() = default;
		virtual void activate(ObjectId object) = 0;
		virtual bool complete(ObjectId object) = 0;
		virtual void removed(ObjectId object) = 0;
		virtual void preparePatrol(ObjectId object) = 0;
		virtual std::unique_ptr<GroupOrder> clone() const = 0;
		const std::vector<ObjectId> &objects() const { return m_objects; }
		OrderId cloneId() const { return m_clone; }

	private:
		friend class AIOrdersManager;
		std::vector<ObjectId> m_objects;
		OrderId m_clone = 0;
	};

	// Mode 0 replaces immediate orders, mode 1 queues and mode 2 plans.
	// The caller constructs the order with the current selection, not a later one.
	OrderId submit(int mode, std::unique_ptr<GroupOrder> order)
	{
		if (!order || mode < 0 || mode > 2) return 0;
		const OrderId id = allocateId();
		const std::vector<ObjectId> members = order->objects();
		m_orders.emplace(id, std::move(order));
		for (ObjectId object : members)
		{
			auto it = m_queues.try_emplace(object, object).first;
			it->second.clear(mode == 0 ? 1u : mode == 1 ? 2u : 0u, *this);
			it->second.insert(mode == 2 ? 2u : 1u, id, *this);
		}
		return id;
	}

	void process(ObjectId object)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.process(*this);
	}
	void clear(ObjectId object, unsigned flags)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.clear(flags, *this);
	}
	void commitPlanned(ObjectId object)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.commitPlanned(*this);
	}
	void setPatrolStart(ObjectId object, OrderId orderId, unsigned flags)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.setPatrolStart(orderId, flags, *this);
	}
	void discardLastPlanned(ObjectId object)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.discardLastPlanned();
	}
	void discardLastOrder(ObjectId object)
	{
		auto it = m_queues.find(object);
		if (it != m_queues.end()) it->second.discardLastOrder(*this);
	}
	const ObjectOrderQueue *queue(ObjectId object) const
	{
		auto it = m_queues.find(object);
		return it == m_queues.end() ? nullptr : &it->second;
	}
	const GroupOrder *order(OrderId id) const
	{
		auto it = m_orders.find(id);
		return it == m_orders.end() ? nullptr : it->second.get();
	}

	using OrderMap = std::map<OrderId, std::unique_ptr<GroupOrder>>;
	using QueueMap = std::map<ObjectId, ObjectOrderQueue>;
	bool empty() const { return m_orders.empty() && m_queues.empty(); }
	const OrderMap &orders() const { return m_orders; }
	const QueueMap &queues() const { return m_queues; }
	OrderId nextId() const { return m_nextId; }

	// OpenBFME (not in RotWK, which keeps the queue of a dead object): an object that left the world takes its queue with it, every order of it cancelled
	void forget(ObjectId object)
	{
		auto it = m_queues.find(object);
		if (it == m_queues.end()) return;
		it->second.clear(3, *this);
		m_queues.erase(it);
	}
	void reset()
	{
		m_orders.clear();
		m_queues.clear();
		m_nextId = 0;
	}

	// A saved state, validated before anything changes: every order has members and an ID the manager issued, a clone names an order, every queue names an
	// order it holds that exists and lists the queue's object as a member. False with `why` set leaves the manager as it was.
	struct OrderRecord
	{
		OrderId id = 0, clone = 0;
		std::unique_ptr<GroupOrder> order;
	};
	bool restore(OrderId nextId, std::vector<OrderRecord> orders, const std::vector<ObjectOrderQueue::State> &queues, std::string *why)
	{
		const auto refuse = [why](const std::string &text) {
			if (why) *why = text;
			return false;
		};
		OrderMap newOrders;
		for (OrderRecord &r : orders)
		{
			if (!r.id || !r.order) return refuse("an order without an id or a body");
			if (r.id > nextId) return refuse("order " + std::to_string(r.id) + " is beyond the next id " + std::to_string(nextId));
			if (r.order->objects().empty()) return refuse("order " + std::to_string(r.id) + " has no members");
			r.order->m_clone = r.clone;
			if (!newOrders.emplace(r.id, std::move(r.order)).second) return refuse("order " + std::to_string(r.id) + " is saved twice");
		}
		for (const auto &kv : newOrders)
		{
			const OrderId clone = kv.second->m_clone;
			if (clone && (clone == kv.first || !newOrders.count(clone))) return refuse("order " + std::to_string(kv.first) + " names the clone " + std::to_string(clone));
		}
		QueueMap newQueues;
		for (const ObjectOrderQueue::State &s : queues)
		{
			if (!s.object) return refuse("a queue without an object");
			ObjectOrderQueue q(s.object);
			if (!q.restore(s)) return refuse("the queue of object " + std::to_string(s.object) + " is inconsistent");
			for (OrderId id : s.orders)
			{
				auto it = newOrders.find(id);
				if (it == newOrders.end()) return refuse("the queue of object " + std::to_string(s.object) + " holds the missing order " + std::to_string(id));
				const std::vector<ObjectId> &m = it->second->objects();
				if (std::find(m.begin(), m.end(), s.object) == m.end())
					return refuse("the queue of object " + std::to_string(s.object) + " holds order " + std::to_string(id) + ", which does not name it");
			}
			if (!newQueues.emplace(s.object, std::move(q)).second) return refuse("the queue of object " + std::to_string(s.object) + " is saved twice");
		}
		m_nextId = nextId;
		m_orders = std::move(newOrders);
		m_queues = std::move(newQueues);
		return true;
	}

private:
	OrderId allocateId()
	{
		if (++m_nextId == 0) ++m_nextId; // GroupOrder ctor skips the zero id
		return m_nextId;
	}
	void activate(OrderId id, ObjectId object) override
	{
		auto it = m_orders.find(id);
		if (it != m_orders.end()) it->second->activate(object);
	}
	bool complete(OrderId id, ObjectId object) override
	{
		auto it = m_orders.find(id);
		return it != m_orders.end() && it->second->complete(object);
	}
	void removed(OrderId id, ObjectId object, bool cancelled) override
	{
		auto it = m_orders.find(id);
		if (it == m_orders.end()) return;
		GroupOrder &order = *it->second;
		order.removed(object);
		order.m_objects.erase(std::remove(order.m_objects.begin(), order.m_objects.end(), object), order.m_objects.end());
		const OrderId clone = cancelled ? order.m_clone : 0;
		if (clone) removed(clone, object, true);
		if (order.m_objects.empty()) m_orders.erase(it);
	}
	OrderId cloneForPatrol(OrderId id) override
	{
		auto it = m_orders.find(id);
		if (it == m_orders.end()) return 0;
		if (it->second->m_clone) return it->second->m_clone;
		auto clone = it->second->clone();
		if (!clone) return 0;
		const OrderId cloneId = allocateId();
		clone->m_clone = 0;
		it->second->m_clone = cloneId;
		m_orders.emplace(cloneId, std::move(clone));
		return cloneId;
	}
	void preparePatrol(OrderId id, ObjectId object) override
	{
		auto it = m_orders.find(id);
		if (it != m_orders.end()) it->second->preparePatrol(object);
	}

	OrderId m_nextId = 0;
	std::map<OrderId, std::unique_ptr<GroupOrder>> m_orders;
	std::map<ObjectId, ObjectOrderQueue> m_queues;
};
