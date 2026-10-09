// OpenBFME. GPL-3.0.
// NameKeyGenerator: see NameKeyGenerator.h for the RotWK addresses this ports.

#include "Common/NameKeyGenerator.h"

#include <cstring>

NameKeyGenerator::NameKeyGenerator()
	: m_sockets(SOCKET_COUNT, -1)
	, m_nextID(0) // RW 0x548BBE: the constructor leaves m_nextID at 0
{
}

void NameKeyGenerator::init()
{
	// RW 0x5485AD frees every chain, then 0x5486DF stores m_nextID = 1
	m_sockets.assign(SOCKET_COUNT, -1);
	m_nodes.clear();
	m_names.clear();
	m_nextID = 1;
}

std::uint32_t NameKeyGenerator::hash(const char *name)
{
	std::uint32_t h = 0;
	for (const char *p = name; *p; ++p)
	{
		h = h * 0x21u + (std::uint32_t)(std::int32_t)(signed char)*p; // imul eax,eax,0x21 ; movsx ecx,cl ; add
	}
	return h;
}

NameKeyType NameKeyGenerator::findKey(const std::string &name) const
{
	for (int n = m_sockets[socketOf(name.c_str())]; n >= 0; n = m_nodes[(size_t)n].next)
	{
		if (std::strcmp(m_nodes[(size_t)n].name.c_str(), name.c_str()) == 0)
		{
			return m_nodes[(size_t)n].id;
		}
	}
	return NAMEKEY_INVALID;
}

NameKeyType NameKeyGenerator::nameToKey(const std::string &name)
{
	const std::uint32_t socket = socketOf(name.c_str());
	for (int n = m_sockets[socket]; n >= 0; n = m_nodes[(size_t)n].next)
	{
		// RW 0xA3CF40 is strcmp: names compare case-sensitively and stop at an embedded NUL
		if (std::strcmp(m_nodes[(size_t)n].name.c_str(), name.c_str()) == 0)
		{
			return m_nodes[(size_t)n].id;
		}
	}
	Node node;
	node.name = name;
	node.id = m_nextID++;
	node.next = m_sockets[socket]; // pushed at the head of the chain (RW 0x54888F-0x548892)
	m_nodes.push_back(node);
	const int index = (int)m_nodes.size() - 1;
	m_sockets[socket] = index;
	// the id vector: a key is a plain index into it; key 0 (constructor state, no init) leaves a hole at
	// no other index because ids are handed out sequentially from m_nextID
	if ((size_t)node.id >= m_names.size())
	{
		m_names.resize((size_t)node.id + 1, -1);
	}
	m_names[(size_t)node.id] = index;
	return node.id;
}

const std::string &NameKeyGenerator::keyToName(NameKeyType key) const
{
	static const std::string empty; // RW returns the shared empty AsciiString for an unknown key
	if (key < 0 || (size_t)key >= m_names.size() || m_names[(size_t)key] < 0)
	{
		return empty;
	}
	return m_nodes[(size_t)m_names[(size_t)key]].name;
}
