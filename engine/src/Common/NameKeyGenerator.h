// OpenBFME. GPL-3.0.
//
// NameKeyGenerator: case-sensitive string -> integer keys, as RotWK builds them.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * RW 0x548538 hash: h = 0; for each byte c: h = h * 33 + (signed char)c   (imul by 0x21, movsx),
//     32-bit wrap-around; the socket is h (as UNSIGNED) % 45007 (0xAFCF, `div ecx` at RW 0x548827).
//   * RW 0x5487EC nameToKey(const char *): walk the socket's chain comparing with strcmp (RW 0xA3CF40),
//     so keys are CASE SENSITIVE ("ActiveBody" and "activebody" are two keys). A name not found gets
//     key m_nextID (RW 0x548878-0x548883: `id = m_nextID; ++m_nextID`), is pushed at the head of its
//     chain and recorded in the id vector.
//   * The constructor (RW 0x548BBE) stores m_nextID = 0; init() (vtable slot 1, RW 0x5486AC, called from
//     GameEngine::init at RW 0x63AE8A) frees every chain and stores m_nextID = 1. So 0 is never
//     assigned after init (NAMEKEY_INVALID, as in Zero Hour); a generator that was only constructed
//     hands out 0 first. Both are ported.
//   * keyToName (RW 0x548700): the name of a known key; for an unknown key it returns the shared EMPTY
//     string (RW 0xDC62B8). Ported as "".
// DONOR: ZH Source/Common/NameKeyGenerator.cpp (init and the 4096-socket table differ; RW is the target).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

typedef int NameKeyType;
enum
{
	NAMEKEY_INVALID = 0
};

class NameKeyGenerator
{
public:
	enum
	{
		SOCKET_COUNT = 45007 ///< RW 0x548822 (mov ecx, 0xAFCF)
	};

	NameKeyGenerator();

	// RW 0x5486AC (vtable slot 1): reset every key, next id := 1.
	void init();
	// Same effect as init() on the key table (a subsystem reset); kept as a separate name for callers.
	void reset() { init(); }

	// RW 0x5487EC. Creates the key when the name is new.
	NameKeyType nameToKey(const std::string &name);
	NameKeyType nameToKey(const char *name) { return nameToKey(std::string(name)); }
	// The key of a name that already has one, or NAMEKEY_INVALID (no creation). RW has no such
	// function; it is the lookup half of nameToKey, for tests and diagnostics.
	NameKeyType findKey(const std::string &name) const;

	// RW 0x548700; "" for an unknown key.
	const std::string &keyToName(NameKeyType key) const;

	// RW 0x548538 (the unsigned 32-bit value before the modulo) and the socket it selects.
	static std::uint32_t hash(const char *name);
	static std::uint32_t socketOf(const char *name) { return hash(name) % SOCKET_COUNT; }

	// m_nextID: the key the next new name receives.
	NameKeyType nextId() const { return m_nextID; }
	size_t keyCount() const { return m_nodes.size(); }

private:
	struct Node
	{
		std::string name;
		NameKeyType id;
		int next; ///< index in m_nodes, -1 at the end of the chain
	};
	std::vector<int> m_sockets; ///< head node index per socket, -1 empty
	std::vector<Node> m_nodes;
	std::vector<int> m_names; ///< key -> node index (only keys handed out; sparse for key 0 before init)
	NameKeyType m_nextID;
};
