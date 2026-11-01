// OpenBFME. PERF-1 tests: the noted stops' fast path by text address (GameLogic::noteStop, AIWorld::noteStop). The diagnostics a game reports stay what
// the ordered sets alone gave: a text noted through a temporary string, or noted again after a load replaced the set, is counted and listed once. Synthetic
// data, no retail files.
#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/AI/AIWorld.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using combattest::CombatWorld;

TEST_CASE("perf1 notes: AIWorld::noteStop lists a text once after a load replaced its noted set, and a new text after it")
{
	CombatWorld w;
	w.frames(1);
	AIWorld *ai = w.logic->aiWorld();
	REQUIRE(ai != nullptr);
	static const char kFirst[] = "perf1 notes: the first stop";
	static const char kSecond[] = "perf1 notes: the second stop";
	ai->noteStop(kFirst); // the fast path now holds kFirst's address
	REQUIRE(ai->noted().count(kFirst) == 1);
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(bytes, &error), error);
	REQUIRE_MESSAGE(w.logic->loadSnapshot(bytes, &error), error); // the load rebuilds the noted set (new nodes)
	const size_t loaded = ai->noted().size();
	REQUIRE(ai->noted().count(kFirst) == 1);
	ai->noteStop(kFirst);
	CHECK(ai->noted().size() == loaded);
	ai->noteStop(kSecond);
	CHECK(ai->noted().size() == loaded + 1);
	CHECK(ai->noted().count(kSecond) == 1);
}

TEST_CASE("perf1 notes: GameLogic::noteStop counts a text noted through 5000 temporary strings 5000 times, under one entry")
{
	CombatWorld w;
	const std::string text = "perf1 notes: a stop noted through a temporary string";
	for (int i = 0; i < 5000; ++i)
	{
		const std::string temporary = text; // a new buffer each time
		w.logic->noteStop(temporary);
	}
	const auto &hits = w.logic->notedStopHits();
	const auto it = hits.find(text);
	REQUIRE(it != hits.end());
	CHECK(it->second == 5000u);
}

TEST_CASE("perf1 notes: one buffer reused for two texts of the same length is noted as each text (GameLogic and AIWorld)")
{
	CombatWorld w;
	w.frames(1);
	AIWorld *ai = w.logic->aiWorld();
	REQUIRE(ai != nullptr);
	const std::string first = "perf1 notes: buffer text A";
	const std::string second = "perf1 notes: buffer text B"; // the same length: only the content tells them apart
	REQUIRE(first.size() == second.size());
	char buffer[64] = {};
	const size_t before = ai->noted().size();
	for (int i = 0; i < 6; ++i)
	{
		const std::string &text = (i % 2 == 0) ? first : second;
		std::copy(text.begin(), text.end(), buffer);
		buffer[text.size()] = 0;
		w.logic->noteStop(std::string_view(buffer, text.size())); // the same address every time
		ai->noteStop(buffer);
	}
	const auto &hits = w.logic->notedStopHits();
	REQUIRE(hits.count(first) == 1);
	REQUIRE(hits.count(second) == 1);
	CHECK(hits.find(first)->second == 3u);
	CHECK(hits.find(second)->second == 3u);
	CHECK(ai->noted().size() == before + 2);
	CHECK(ai->noted().count(first) == 1);
	CHECK(ai->noted().count(second) == 1);
}
