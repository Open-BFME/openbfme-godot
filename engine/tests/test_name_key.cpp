// OpenBFME unit tests: NameKeyGenerator (INI port step 7). GPL-3.0.
// Expected values come from the RotWK disassembly (NameKeyGenerator.h lists the addresses): hash
// h*33 + (signed char)c, 45007 sockets, strcmp chains, constructor id 0, init() id 1.

#include "doctest.h"

#include "Common/NameKeyGenerator.h"

#include <map>
#include <set>
#include <string>

TEST_CASE("NameKeyGenerator: hash is h*33 + signed char, wrapped to 32 bits (RW 0x548538)")
{
	CHECK(NameKeyGenerator::hash("") == 0u);
	CHECK(NameKeyGenerator::hash("a") == 97u);
	CHECK(NameKeyGenerator::hash("ab") == 97u * 33u + 98u);
	CHECK(NameKeyGenerator::hash("abc") == (97u * 33u + 98u) * 33u + 99u);
	// movsx: a byte >= 0x80 adds a NEGATIVE value (0xE9 is -23)
	CHECK(NameKeyGenerator::hash("\xE9") == 0xFFFFFFE9u);
	CHECK(NameKeyGenerator::hash("a\xE9") == 97u * 33u - 23u);
	// 32-bit wrap-around
	std::string longName(40, 'z');
	std::uint32_t h = 0;
	for (char c : longName)
	{
		h = h * 33u + (std::uint32_t)(std::int32_t)(signed char)c;
	}
	CHECK(NameKeyGenerator::hash(longName.c_str()) == h);
}

TEST_CASE("NameKeyGenerator: the socket is the UNSIGNED hash modulo 45007 (RW 0x548822, div ecx)")
{
	CHECK(NameKeyGenerator::SOCKET_COUNT == 45007);
	CHECK(NameKeyGenerator::socketOf("a") == 97u);
	// a hash with the top bit set: an unsigned divide, not a signed one
	CHECK(NameKeyGenerator::socketOf("\xE9") == 0xFFFFFFE9u % 45007u);
	CHECK(NameKeyGenerator::socketOf("\xE9") != (std::uint32_t)((std::int32_t)0xFFFFFFE9 % 45007));
}

TEST_CASE("NameKeyGenerator: keys are case sensitive and stable (strcmp chains, RW 0x5487EC)")
{
	NameKeyGenerator g;
	g.init();
	const NameKeyType a = g.nameToKey("ActiveBody");
	const NameKeyType b = g.nameToKey("activebody");
	CHECK(a != b);
	CHECK(g.nameToKey("ActiveBody") == a);
	CHECK(g.nameToKey(std::string("activebody")) == b);
	CHECK(g.findKey("ActiveBody") == a);
	CHECK(g.findKey("ACTIVEBODY") == NAMEKEY_INVALID);
	CHECK(g.keyCount() == 2);
}

TEST_CASE("NameKeyGenerator: the constructor hands out 0 first, init() restarts at 1 (RW 0x548BBE, 0x5486DF)")
{
	NameKeyGenerator g; // constructed only, as before GameEngine::init calls init() at RW 0x63AE8A
	CHECK(g.nextId() == 0);
	CHECK(g.nameToKey("First") == 0);
	CHECK(g.nameToKey("Second") == 1);
	g.init();
	CHECK(g.nextId() == 1);
	CHECK(g.findKey("First") == NAMEKEY_INVALID); // every chain was freed
	CHECK(g.nameToKey("Second") == 1);            // keys restart: 0 is never assigned after init
	CHECK(g.nameToKey("First") == 2);
	CHECK(g.nameToKey("Third") == 3);
	g.reset();
	CHECK(g.nameToKey("Anything") == 1);
}

TEST_CASE("NameKeyGenerator: keyToName returns the empty string for an unknown key (RW 0x548700 -> 0xDC62B8)")
{
	NameKeyGenerator g;
	g.init();
	const NameKeyType k = g.nameToKey("ModuleTag_01");
	CHECK(g.keyToName(k) == "ModuleTag_01");
	CHECK(g.keyToName(k + 1).empty());
	CHECK(g.keyToName(NAMEKEY_INVALID).empty());
	CHECK(g.keyToName(-5).empty());
	CHECK(g.keyToName(1000000).empty());
}

TEST_CASE("NameKeyGenerator: names that share a socket stay distinct keys")
{
	// find two names with the same socket by brute force over a counter (45007 sockets)
	std::map<std::uint32_t, std::string> bySocket;
	std::string first, second;
	for (int i = 0; i < 200000 && second.empty(); ++i)
	{
		const std::string n = "N" + std::to_string(i);
		const std::uint32_t s = NameKeyGenerator::socketOf(n.c_str());
		auto it = bySocket.find(s);
		if (it == bySocket.end())
		{
			bySocket[s] = n;
		}
		else
		{
			first = it->second;
			second = n;
		}
	}
	REQUIRE(!second.empty());
	NameKeyGenerator g;
	g.init();
	const NameKeyType k1 = g.nameToKey(first);
	const NameKeyType k2 = g.nameToKey(second);
	CHECK(k1 != k2);
	CHECK(g.keyToName(k1) == first);
	CHECK(g.keyToName(k2) == second);
	CHECK(g.nameToKey(first) == k1);
	CHECK(g.nameToKey(second) == k2);
}

TEST_CASE("NameKeyGenerator: keys are dense and sequential")
{
	NameKeyGenerator g;
	g.init();
	std::set<NameKeyType> keys;
	for (int i = 0; i < 1000; ++i)
	{
		keys.insert(g.nameToKey("k" + std::to_string(i)));
	}
	CHECK(keys.size() == 1000);
	CHECK(*keys.begin() == 1);
	CHECK(*keys.rbegin() == 1000);
	CHECK(g.nextId() == 1001);
}
