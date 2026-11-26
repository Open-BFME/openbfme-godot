// OpenBFME. GPL-3.0.
// Per-object part of AiOrdersManager's order queue. Both editions.
// RotWK: RW 0x94EBA8 (ctor), RW 0x94EBD1 (clear), RW 0x94ECF2 (xfer), RW 0x94ED4C (insert),
// RW 0x94EE0F (commit), RW 0x94EF1A (process); addresses from the inspected RotWK image, caveat S-001.
// BFME2 1.06: ObjectOrderQueue rows Rva 0x5482E9, 0x548464, 0x548527,
// 0x548632 in the byte-matched Open-BFME-2 Rva0054840AXfer.cpp.
// The group-order owner supplies activation, completion and lifetime. This
// component is not yet connected to live commands or native saves (S-281).
#pragma once

// Local 174: RW 0x94EA54 (ObjectOrderQueue ?checkForPatrol@ObjectOrderQueue@@QAEPBVArmorTemplate@@PAXPBVRva00548800@@@Z); RW 0x94EACA (ObjectOrderQueue::setPatrolStart)
#include <algorithm>
#include <cstdint>
#include <list>
#include <vector>

class ObjectOrderQueue
{
public:
	using OrderId = std::uint32_t;
	using ObjectId = std::uint32_t;

	class Owner
	{
	public:
		virtual ~Owner() = default;
		virtual void activate(OrderId order, ObjectId object) = 0;
		// A missing order is not complete: retail leaves it at the queue front.
		virtual bool complete(OrderId order, ObjectId object) = 0;
		virtual void removed(OrderId order, ObjectId object, bool cancelled) = 0;
		virtual OrderId cloneForPatrol(OrderId order) = 0;
		virtual void preparePatrol(OrderId order, ObjectId object) = 0;
	};

	explicit ObjectOrderQueue(ObjectId object = 0) : m_object(object) {}
	const std::list<OrderId> &orders() const { return m_orders; }
	OrderId planned() const { return m_planned; }
	OrderId patrolStart() const { return m_patrolStart; }
	OrderId nextPatrolStart() const { return m_nextPatrolStart; }

	// A data-only snapshot; the manager must additionally validate that every
	// order ID exists and names this object before installing a loaded state.
	struct State
	{
		ObjectId object = 0;
		std::vector<OrderId> orders;
		OrderId planned = 0, patrolStart = 0, nextPatrolStart = 0;
	};
	State state() const
	{
		return { m_object, std::vector<OrderId>(m_orders.begin(), m_orders.end()), m_planned, m_patrolStart, m_nextPatrolStart };
	}
	bool restore(const State &s)
	{
		if (!s.object && !s.orders.empty()) return false;
		if (std::find(s.orders.begin(), s.orders.end(), 0) != s.orders.end()) return false;
		const auto present = [&s](OrderId id) { return !id || std::find(s.orders.begin(), s.orders.end(), id) != s.orders.end(); };
		if (!present(s.planned) || !present(s.patrolStart) || !present(s.nextPatrolStart)) return false;
		m_object = s.object;
		m_orders.assign(s.orders.begin(), s.orders.end());
		m_planned = s.planned;
		m_patrolStart = s.patrolStart;
		m_nextPatrolStart = s.nextPatrolStart;
		return true;
	}

	void setPatrolStart(OrderId id, unsigned flags, Owner &owner)
	{
		auto it = std::find(m_orders.begin(), m_orders.end(), id);
		if (it == m_orders.end()) return;
		for (; it != m_orders.end(); ++it) owner.preparePatrol(*it, m_object);
		if (flags & 1) m_patrolStart = id;
		if (flags & 2) m_nextPatrolStart = id;
	}

	// RW 0x94ECAE / BFME2 0x9483C6: remove the last order and notify its owner.
	void discardLastOrder(Owner &owner)
	{
		if (m_orders.empty()) return;
		const OrderId last = m_orders.back();
		if (!m_planned) m_patrolStart = 0;
		m_nextPatrolStart = 0;
		if (last == m_planned) m_planned = 0;
		owner.removed(last, m_object, true);
		m_orders.pop_back();
	}

	// RW 0x94EFE8 / BFME2 0x948700: discard a planned tail without notifying its owner.
	void discardLastPlanned()
	{
		if (m_orders.empty() || !m_planned) return;
		m_nextPatrolStart = 0;
		if (m_orders.back() == m_planned) m_planned = 0;
		m_orders.pop_back();
	}

	// flags 1 and 2 choose the ranges before and after the planned marker.
	void clear(unsigned flags, Owner &owner)
	{
		auto first = m_orders.begin();
		auto last = m_orders.end();
		if (!(flags & 2) && m_planned)
			last = std::find(m_orders.begin(), m_orders.end(), m_planned);
		if (!(flags & 1))
		{
			first = last;
			if (m_planned) first = std::find(m_orders.begin(), m_orders.end(), m_planned);
		}
		for (auto it = first; it != last; ++it)
		{
			owner.removed(*it, m_object, true);
			if (*it == m_planned) m_planned = 0;
			if (*it == m_patrolStart) m_patrolStart = 0;
			if (*it == m_nextPatrolStart) m_nextPatrolStart = 0;
		}
		m_orders.erase(first, last);
	}

	void insert(unsigned flags, OrderId id, Owner &owner)
	{
		if (!id) return;
		if (flags & 2)
		{
			m_orders.push_back(id);
			if (!m_planned) m_planned = id;
			m_nextPatrolStart = 0;
		}
		else if (flags & 1)
		{
			m_patrolStart = 0;
			if (!m_planned) m_orders.push_back(id);
			else
			{
				auto pos = std::find(m_orders.begin(), m_orders.end(), m_planned);
				if (pos == m_orders.end()) m_planned = 0;
				m_orders.insert(pos, id);
			}
			if (m_orders.front() == id) owner.activate(id, m_object);
		}
	}

	void commitPlanned(Owner &owner)
	{
		if (m_orders.empty() || !m_planned) return;
		if (m_patrolStart)
		{
			auto active = std::find(m_orders.begin(), m_orders.end(), m_planned);
			auto first = std::find(m_orders.begin(), m_orders.end(), m_patrolStart);
			if (first != m_orders.end() && first != active)
			{
				++first;
				auto last = first;
				while (last != m_orders.end() && last != active)
				{
					owner.removed(*last, m_object, true);
					++last;
				}
				m_orders.erase(first, last);
			}
		}
		m_patrolStart = m_nextPatrolStart;
		m_nextPatrolStart = 0;
		if (m_planned == m_orders.front()) owner.activate(m_orders.front(), m_object);
		m_planned = 0;
	}

	void process(Owner &owner)
	{
		if (m_orders.empty()) return;
		const OrderId id = m_orders.front();
		if (m_planned == id || !owner.complete(id, m_object)) return;
		if (m_patrolStart == id)
		{
			const OrderId clone = owner.cloneForPatrol(id);
			if (clone)
			{
				insert(1, clone, owner);
				owner.removed(id, m_object, false);
				m_orders.pop_front();
				m_patrolStart = m_orders.front();
			}
			else m_patrolStart = 0;
		}
		else
		{
			owner.removed(id, m_object, false);
			m_orders.pop_front();
		}
		if (!m_orders.empty() && m_planned != m_orders.front()) owner.activate(m_orders.front(), m_object);
	}

private:
	ObjectId m_object = 0;
	std::list<OrderId> m_orders;
	OrderId m_planned = 0, m_patrolStart = 0, m_nextPatrolStart = 0;
};
