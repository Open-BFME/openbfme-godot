// OpenBFME. GPL-3.0.
//
// GlobalOwnerChain<T>: removable owner registrations for a process-wide slot (TheLocomotorStore, TheCommandStore, TheWeaponStore, the
// ContainParseHooks callbacks, ...). Retail has one owner of each such global; the port lets several worlds exist, and the old pattern
// (each owner copies the predecessor into a member and writes it back on destruction) restores a dead owner's value when the owners die
// in any order other than the reverse of their creation.
//
// Here the registered owners are a list. The slot holds the value of the TOP live owner (the last one installed or selected), or the
// baseline (the slot's value before the first owner registered) when there is none. Removing an owner unlinks it wherever it sits: a
// buried owner leaves the slot alone, the top owner hands the slot to the next live one. Nothing ever refers to a removed owner again.
// A slot somebody else assigned directly while the chain was non-empty is left alone by remove() (pointer slots only: callbacks cannot
// be compared).
//
// Single threaded, like the INI parsers that read the slots. Lane SMOOTH-1 (S-810): the slots are thread_local and every thread has its own chain
// (ThreadOwnerChain below), so the logic worker selects its world for itself without touching the main thread's selection.

#pragma once

#include <algorithm>
#include <thread>
#include <type_traits>
#include <vector>

template <class T>
class GlobalOwnerChain
{
public:
	explicit GlobalOwnerChain(T &slot) : m_slot(slot) {}
	GlobalOwnerChain(const GlobalOwnerChain &) = delete;
	GlobalOwnerChain &operator=(const GlobalOwnerChain &) = delete;

	// Registers `owner` (or re-registers it with a new value) and makes it the current one.
	void install(const void *owner, T value)
	{
		if (m_entries.empty())
		{
			m_baseline = m_slot;
		}
		unlink(owner);
		m_entries.push_back({ owner, value });
		m_slot = m_entries.back().value;
	}

	// Makes a registered owner the current one again (the top of the chain); false when it is not registered.
	bool select(const void *owner)
	{
		const auto it = find(owner);
		if (it == m_entries.end())
		{
			return false;
		}
		Entry e = *it;
		m_entries.erase(it);
		m_entries.push_back(e);
		m_slot = m_entries.back().value;
		return true;
	}

	// Unlinks the owner. When it was the current one the slot passes to the previous live owner, or back to the baseline.
	void remove(const void *owner)
	{
		const auto it = find(owner);
		if (it == m_entries.end())
		{
			return;
		}
		const bool wasTop = (it + 1 == m_entries.end());
		bool slotIsOurs = true;
		if constexpr (std::is_pointer<T>::value)
		{
			slotIsOurs = (m_slot == it->value);
		}
		m_entries.erase(it);
		if (!wasTop || !slotIsOurs)
		{
			if (m_entries.empty())
			{
				m_baseline = T();
			}
			return;
		}
		m_slot = m_entries.empty() ? m_baseline : m_entries.back().value;
		if (m_entries.empty())
		{
			m_baseline = T();
		}
	}

	bool contains(const void *owner) const { return find(owner) != m_entries.end(); }
	const void *current() const { return m_entries.empty() ? nullptr : m_entries.back().owner; }
	size_t size() const { return m_entries.size(); }

private:
	struct Entry
	{
		const void *owner;
		T value;
	};
	typename std::vector<Entry>::iterator find(const void *owner)
	{
		return std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &e) { return e.owner == owner; });
	}
	typename std::vector<Entry>::const_iterator find(const void *owner) const
	{
		return std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &e) { return e.owner == owner; });
	}
	void unlink(const void *owner)
	{
		const auto it = find(owner);
		if (it != m_entries.end())
		{
			m_entries.erase(it);
		}
	}

	T &m_slot;
	T m_baseline{};
	std::vector<Entry> m_entries; ///< the last one is the current owner
};

// The thread that ran the static initialisation (the main thread).
inline const std::thread::id g_globalOwnerChainMainThread = std::this_thread::get_id();

// Lane SMOOTH-1: one chain of a thread_local slot per thread. The main thread's chain is never destroyed (a world may outlive static destruction
// order); another thread's chain goes with its thread when no owner is left on it (a worker leaves its world's context at the end of every frame).
template <class T>
class ThreadOwnerChain
{
public:
	explicit ThreadOwnerChain(T &slot) : m_chain(new GlobalOwnerChain<T>(slot)) {}
	~ThreadOwnerChain()
	{
		if (std::this_thread::get_id() != g_globalOwnerChainMainThread && m_chain->size() == 0)
		{
			delete m_chain;
		}
	}
	ThreadOwnerChain(const ThreadOwnerChain &) = delete;
	ThreadOwnerChain &operator=(const ThreadOwnerChain &) = delete;
	GlobalOwnerChain<T> &get() { return *m_chain; }

private:
	GlobalOwnerChain<T> *m_chain;
};
