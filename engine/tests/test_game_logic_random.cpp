// OpenBFME unit tests: the game-logic RNG (Common/RandomValue). GPL-3.0. Lane HORDE-1, stop S-080.
// Expected values come from tools/horde_oracle/rng_reference.py (an independent implementation written
// from the RotWK disassembly and ZH source, with the x87 chain in exact rational arithmetic), stored in
// tests/data/horde1/rng_golden.json. tools/horde_oracle/test_rng_reference.py checks the file and the
// binary facts (RW 0x6D315D etc.).

#include "doctest.h"

#include "Common/MiniJson.h"
#include "Common/RandomValue.h"
#include "RetailTestMount.h"

#include <climits>
#include <cstring>
#include <stdexcept>

namespace
{
const JsonValue &goldenRoot()
{
	static JsonValue root;
	static bool loaded = false;
	if (!loaded)
	{
		std::vector<unsigned char> bytes;
		std::string error;
		REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/horde1/rng_golden.json", bytes, &error), error);
		REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), root, &error), error);
		loaded = true;
	}
	return root;
}

std::uint32_t u32(const JsonValue &v)
{
	return (std::uint32_t)(std::uint64_t)v.number;
}

float floatFromBits(std::uint32_t b)
{
	float f;
	std::memcpy(&f, &b, sizeof(f));
	return f;
}

std::uint32_t bitsOfFloat(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}

RandomAlgorithm algorithmByName(const std::string &name)
{
	if (name == "RotWK_GameDat_LCG")
	{
		return RandomAlgorithm::RotWK_GameDat_LCG;
	}
	REQUIRE(name == "ZH_CarryChain");
	return RandomAlgorithm::ZH_CarryChain;
}
}

TEST_CASE("RNG golden: both generators reproduce the independent reference (raw draws, wrappers, call order)")
{
	const JsonValue *algos = goldenRoot().get("algorithms");
	REQUIRE(algos);
	REQUIRE(algos->object.size() == 2);
	for (const auto &entry : algos->object)
	{
		INFO("algorithm " << entry.first);
		const RandomAlgorithm algorithm = algorithmByName(entry.first);
		REQUIRE(entry.second.array.size() == 4);
		for (const JsonValue &c : entry.second.array)
		{
			const std::uint32_t seed = u32(*c.get("seed"));
			INFO("seed " << seed);
			{
				GameLogicRandom rng(algorithm);
				rng.initGameLogicRandom(seed, -1);
				REQUIRE(rng.baseSeed() == seed);
				const JsonValue &arr = *c.get("seed_array");
				for (size_t i = 0; i < 6; ++i)
				{
					CHECK(rng.seedArray()[i] == u32(arr.array[i]));
				}
				for (const JsonValue &r : c.get("raw")->array)
				{
					CHECK(rng.randomValue() == u32(r));
				}
			}
			GameLogicRandom rng(algorithm);
			rng.initGameLogicRandom(seed, -1);
			rng.enableCallLog(true);
			size_t draws = 0;
			size_t n = 0;
			for (const JsonValue &op : c.get("sequence")->array)
			{
				INFO("op " << n);
				const bool drew = op.get("drew")->boolean;
				if (op.get("op")->string == "i")
				{
					const int lo = (int)op.get("lo")->number;
					const int hi = (int)op.get("hi")->number;
					CHECK(rng.getValue(lo, hi, "golden", (int)n) == (int)op.get("result")->number);
				}
				else
				{
					const float lo = floatFromBits(u32(*op.get("lo_bits")));
					const float hi = floatFromBits(u32(*op.get("hi_bits")));
					CHECK(bitsOfFloat(rng.getValueReal(lo, hi, "golden", (int)n)) == u32(*op.get("result_bits")));
				}
				REQUIRE(rng.callLog().size() == n + 1);
				CHECK(rng.callLog().back().drewNumber == drew);
				CHECK(rng.callLog().back().line == (int)n);
				draws += drew ? 1 : 0;
				++n;
			}
			CHECK(n == 24);
			CHECK(draws > 12);
		}
	}
}

TEST_CASE("RNG: a call with an empty range returns hi without drawing, so the next draw is unchanged")
{
	for (RandomAlgorithm a : { RandomAlgorithm::RotWK_GameDat_LCG, RandomAlgorithm::ZH_CarryChain })
	{
		GameLogicRandom x(a), y(a);
		x.initGameLogicRandom(77, -1);
		y.initGameLogicRandom(77, -1);
		CHECK(x.getValue(0, -1, "f", 1) == -1);      // delta 0 (RW 0x6D329B: je -> returns hi)
		CHECK(x.getValueReal(2.5f, 2.0f, "f", 2) == 2.0f); // delta < 0 returns hi
		CHECK(x.getValueReal(3.0f, 3.0f, "f", 3) == 3.0f); // delta == 0 returns hi
		CHECK(x.randomValue() == y.randomValue());
	}
}

TEST_CASE("RNG: getValue wraps its 32-bit add like retail (RW 0x6D32AD) instead of overflowing a signed int")
{
	// LCG seed 1, bounds (INT_MIN, INT_MAX-1): delta = 0xFFFFFFFF, draw % delta + (uint32)INT_MIN wraps; retail gives 1666871715.
	GameLogicRandom r(RandomAlgorithm::RotWK_GameDat_LCG);
	r.initGameLogicRandom(1, -1);
	CHECK(r.getValue(INT_MIN, INT_MAX - 1, "f", 1) == 1666871715);
}

TEST_CASE("RNG: the frame override replaces the seed (RW 0x6D3261 reads TheGameLogic+0x1228) unless it is -1")
{
	GameLogicRandom a(RandomAlgorithm::RotWK_GameDat_LCG), b(RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(a.initGameLogicRandom(5, -1) == 5);
	CHECK(b.initGameLogicRandom(5, 100) == 100);
	CHECK(b.baseSeed() == 100);
	GameLogicRandom c(RandomAlgorithm::RotWK_GameDat_LCG);
	c.initGameLogicRandom(100, -1);
	CHECK(b.randomValue() == c.randomValue());
	CHECK(a.randomValue() != c.randomValue());
}

TEST_CASE("RNG: the static initial seed array is the one in all three retail arrays, and the instance starts from it")
{
	const GameLogicRandom::Seed init = GameLogicRandom::initialSeedArray();
	CHECK(init[0] == 0xf22d0e56u);
	CHECK(init[5] == 0x6fdf3b64u);
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(rng.seedArray() == init);
	CHECK(rng.baseSeed() == 0);
}

TEST_CASE("RNG: the process-wide generator has no default (PLAN rule 10)")
{
	// TheGameLogicRandom() before init must throw; afterwards the ZH-named functions work.
	// (This test owns the global: nothing else in the suite initialises it before this runs.)
	CHECK_THROWS_AS(GetGameLogicRandomValue(0, 9, "f", 1), std::logic_error);
	InitGameLogicRandomGlobal(RandomAlgorithm::RotWK_GameDat_LCG, 12345, -1);
	GameLogicRandom ref(RandomAlgorithm::RotWK_GameDat_LCG);
	ref.initGameLogicRandom(12345, -1);
	CHECK(GameLogicRandomValue(0, 99) == ref.getValue(0, 99, "f", 1));
	CHECK(bitsOfFloat(GameLogicRandomValueReal(-1.0f, 1.0f)) == bitsOfFloat(ref.getValueReal(-1.0f, 1.0f, "f", 1)));
}

TEST_CASE("RNG stop S-080: both algorithms report the unproven-algorithm stop with the evidence")
{
	for (RandomAlgorithm a : { RandomAlgorithm::RotWK_GameDat_LCG, RandomAlgorithm::ZH_CarryChain })
	{
		GameLogicRandom rng(a);
		const std::vector<std::string> stops = rng.unverified();
		REQUIRE(stops.size() == 1);
		CHECK(stops[0].rfind("S-080:", 0) == 0);
		CHECK(stops[0].find("0x08088405") != std::string::npos);
		CHECK(stops[0].find(a == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain" : "RotWK_GameDat_LCG") != std::string::npos);
	}
}
