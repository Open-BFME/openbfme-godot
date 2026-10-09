// OpenBFME unit tests: ModelConditionFlags, BitFlags and the SparseMatchFinder (lane DRAW-1, spec w3d-and-draw.md 4.3).
// Expectations are derived by hand from the matcher rule (ZH SparseMatchFinder.h:123-186) and from the RotWK name array dump
// (engine/tests/data/draw/draw_field_tables.json), not from running the code under test.

#include "doctest.h"

#include "Common/BitFlags.h"
#include "Common/MiniJson.h"
#include "Common/ModelState.h"
#include "Common/SparseMatchFinder.h"
#include "RetailTestMount.h"
#include "W3DDrawTestUtil.h"

#include <set>

using namespace drawtest;

TEST_CASE("BitFlags: set, test, counts and the two intersection counts")
{
	BitFlags<70> a;
	BitFlags<70> b;
	a.set(0);
	a.set(33);
	a.set(69);
	b.set(33);
	b.set(34);
	CHECK(a.count() == 3);
	CHECK(a.test(69));
	CHECK(!a.test(1));
	CHECK(!a.test(70)); // out of range reads as clear
	// |a & b| = {33}; |~a & b| = {34}
	CHECK(a.countIntersection(b) == 1);
	CHECK(a.countInverseIntersection(b) == 1);
	CHECK(b.countInverseIntersection(a) == 2); // {0, 69}
	BitFlags<70> c = a;
	c.clear(b);
	CHECK(c.count() == 2);
	CHECK(!c.test(33));
	CHECK(a == a);
	CHECK(!(a == b));
	CHECK(BitFlags<70>().any() == false);
	// 70 bits live in 3 words
	CHECK(BitFlags<70>::NUM_WORDS == 3);
}

TEST_CASE("ModelConditionFlags is 19 words, and the table has the 591 names of RW 0xD9FAD8")
{
	CHECK(ModelConditionFlags::NUM_WORDS == 19); // 0x4C bytes, RW 0x4B5E39 memset 0x4C
	CHECK(ModelCondition::count() == 591);
	CHECK(std::string(ModelCondition::nameOf(0)) == "TOPPLED");
	CHECK(std::string(ModelCondition::nameOf(61)) == "MOVING");
	CHECK(std::string(ModelCondition::nameOf(62)) == "DYING");
	CHECK(std::string(ModelCondition::nameOf(590)) == "SPECIAL_WEAPON_SIX");
	CHECK(ModelCondition::nameOf(591) == nullptr);
	CHECK(ModelCondition::bitNames()[591] == nullptr); // NULL terminated for scanIndexList
	// case-insensitive lookup (RW 0x42B914 uses stricmp)
	CHECK(ModelCondition::indexOf("moving") == 61);
	CHECK(ModelCondition::indexOf("FIRING_OR_PREATTACK_A") == 42);
	CHECK(ModelCondition::indexOf("USER_75") == 527);
	CHECK(ModelCondition::indexOf("NOT_A_CONDITION") == -1);
	// names are unique
	std::set<std::string> seen;
	for (int i = 0; i < 591; ++i)
	{
		CHECK(seen.insert(ModelCondition::nameOf(i)).second);
	}
}

TEST_CASE("ModelConditionFlags: the table is a registry, not just the names retail INI uses (PLAN rule 6)")
{
	// RotWK adds names the retail Object INI never writes (for example the BFME2-era DOOR_4_* and the USER_75 run); every
	// one of the 591 is accepted by the parser.
	Harness h;
	for (int i = 0; i < 591; ++i)
	{
		const ModelConditionFlags f = h.parseFlags(ModelCondition::nameOf(i));
		REQUIRE_MESSAGE(h.error.empty(), h.error);
		CHECK(f.count() == 1);
		CHECK(f.test(i));
	}
}

TEST_CASE("ModelConditionFlags parse: the grammar of RW 0x4B5E05 / 0x4B8B37")
{
	Harness h;
	// plain names set bits; the first plain name clears whatever the set held
	ModelConditionFlags f = h.parseFlags("MOVING ATTACKING");
	CHECK(h.error.empty());
	CHECK(f.count() == 2);
	CHECK(f.test(61));
	CHECK(f.test(37));
	// case-insensitive
	f = h.parseFlags("moving");
	CHECK(f == flagsOf({ "MOVING" }));
	// NONE clears and ends the list
	f = h.parseFlags("NONE");
	CHECK(h.error.empty());
	CHECK(!f.any());
	// the list after a NONE is not read (RW 0x4B8BF7)
	f = h.parseFlags("NONE BOGUS_NAME");
	CHECK(h.error.empty());
	CHECK(!f.any());
	// NONE after a plain name is an error
	h.parseFlags("MOVING NONE");
	CHECK(h.error.find("you may not mix normal and +- ops in bitstring lists") != std::string::npos);
	// +/- start from an empty set here (the caller's value); a plain name after +/- is an error
	f = h.parseFlags("+MOVING +DYING -MOVING");
	CHECK(h.error.empty());
	CHECK(f == flagsOf({ "DYING" }));
	h.parseFlags("+MOVING DYING");
	CHECK(h.error.find("you may not mix normal and +- ops in bitstring lists") != std::string::npos);
	h.parseFlags("MOVING +DYING");
	CHECK(h.error.find("you may not mix normal and +- ops in bitstring lists") != std::string::npos);
	h.parseFlags("NONE");
	// an unknown name is the index list error of RW 0x42B914
	h.parseFlags("MOVING BOGUS_NAME");
	CHECK(h.error.find("Token 'BOGUS_NAME' is not a valid member of the index list") != std::string::npos);
	h.parseFlags("+BOGUS_NAME");
	CHECK(h.error.find("Token 'BOGUS_NAME' is not a valid member of the index list") != std::string::npos);
	// an empty line gives an empty set
	f = h.parseFlags("");
	CHECK(h.error.empty());
	CHECK(!f.any());
}

TEST_CASE("ModelConditionFlags parse: a #define macro expands to several flags (RW 0x4B8B65)")
{
	Harness h;
	INI ini(h.env);
	const std::string defs = "#define COMBO MOVING ATTACKING\nTestFlags COMBO DYING\n";
	// the pre-pass defines COMBO; the line then holds the macro and a plain name
	ini.loadMemory("macro.ini", std::vector<std::uint8_t>(defs.begin(), defs.end()), INI_LOAD_OVERWRITE);
	CHECK(h.flags == flagsOf({ "MOVING", "ATTACKING", "DYING" }));
}

namespace
{
struct TwoSet
{
	std::vector<ModelConditionFlags> sets;
	int id = 0;
	int getConditionsYesCount() const { return (int)sets.size(); }
	const ModelConditionFlags &getNthConditionsYes(int i) const { return sets[(size_t)i]; }
};
typedef SparseMatchFinder<TwoSet, ModelConditionFlags> TwoSetFinder;

TwoSet state(int id, std::initializer_list<std::initializer_list<const char *>> sets)
{
	TwoSet s;
	s.id = id;
	for (auto set : sets)
	{
		s.sets.push_back(flagsOf(set));
	}
	return s;
}
} // namespace

TEST_CASE("SparseMatchFinder: most yes bits, then fewest extra bits, ties keep the earlier state")
{
	// Hand derivation. States: 0 = {} (the default / idle), 1 = {MOVING}, 2 = {MOVING, ATTACKING}, 3 = {ATTACKING}.
	std::vector<TwoSet> v = { state(0, { {} }), state(1, { { "MOVING" } }), state(2, { { "MOVING", "ATTACKING" } }), state(3, { { "ATTACKING" } }) };
	TwoSetFinder finder;

	// current {}: state 0 is (yes 0, extra 0); the others have extra >= 1 and yes 0: none beats (0,0), 0 wins
	CHECK(finder.findBestInfo(v, flagsOf({}))->id == 0);
	// current {MOVING}: 1 = (1,0); 2 = (1,1) loses on extra; 0 = (0,0): 1 wins
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING" }))->id == 1);
	// current {MOVING, ATTACKING}: 2 = (2,0) beats 1 = (1,0) and 3 = (1,0)
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING", "ATTACKING" }))->id == 2);
	// current {ATTACKING}: 3 = (1,0); 2 = (1,1): 3 wins
	CHECK(finder.findBestInfo(v, flagsOf({ "ATTACKING" }))->id == 3);
	// current {DYING}: nothing has a yes bit; state 0 is (0,0) first and nothing has fewer extras
	CHECK(finder.findBestInfo(v, flagsOf({ "DYING" }))->id == 0);
	// current {MOVING, DYING}: 1 = (1,0) wins over 0 = (0,0) by yes count; the bits the states do not know are ignored
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING", "DYING" }))->id == 1);
	// a state is never rejected for demanding bits the object lacks: with only state 2 present, {MOVING} still gets it
	// (a finder caches per list: use one finder per vector)
	std::vector<TwoSet> only2 = { state(2, { { "MOVING", "ATTACKING" } }) };
	TwoSetFinder finder2;
	CHECK(finder2.findBestInfo(only2, flagsOf({ "MOVING" }))->id == 2);
	CHECK(finder2.findBestInfo(only2, flagsOf({}))->id == 2); // yes 0, extra 2 < 999
}

TEST_CASE("SparseMatchFinder: a tie between two states keeps the earlier one")
{
	// both are (1, 0) for {MOVING}
	std::vector<TwoSet> v = { state(10, { { "MOVING" } }), state(11, { { "MOVING" } }) };
	TwoSetFinder finder;
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING" }))->id == 10);
	// but a strictly better later state wins even with the same yes count when it has fewer extras
	std::vector<TwoSet> w = { state(20, { { "MOVING", "DYING" } }), state(21, { { "MOVING" } }) };
	TwoSetFinder finderW;
	CHECK(finderW.findBestInfo(w, flagsOf({ "MOVING" }))->id == 21);
}

TEST_CASE("SparseMatchFinder: every condition set of a state competes, last set first")
{
	// state 7 has sets {DYING} and {MOVING, ATTACKING}; state 8 has {MOVING}
	std::vector<TwoSet> v = { state(7, { { "DYING" }, { "MOVING", "ATTACKING" } }), state(8, { { "MOVING" } }) };
	TwoSetFinder finder;
	CHECK(finder.findBestInfo(v, flagsOf({ "DYING" }))->id == 7);
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING", "ATTACKING" }))->id == 7);
	CHECK(finder.findBestInfo(v, flagsOf({ "MOVING" }))->id == 8); // (1,0) beats state 7's second set (1,1)
	// slow path and cache agree, and the cache is keyed by the exact set
	for (const char *name : { "DYING", "MOVING" })
	{
		const ModelConditionFlags f = flagsOf({ name });
		CHECK(finder.findBestInfo(v, f) == TwoSetFinder::findBestInfoSlow(v, f));
		CHECK(finder.findBestInfo(v, f) == finder.findBestInfo(v, f));
	}
	// an empty list matches nothing and is not cached
	std::vector<TwoSet> none;
	TwoSetFinder finderNone;
	CHECK(finderNone.findBestInfo(none, flagsOf({ "MOVING" })) == nullptr);
}

TEST_CASE("draw tables: the extracted name array equals the committed golden, and nothing is added or dropped")
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/draw/draw_field_tables.json", bytes, &error), error);
	JsonValue doc;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), doc, &error), error);
	const JsonValue *count = doc.get("conditionNameCount");
	REQUIRE(count != nullptr);
	CHECK((int)count->number == ModelCondition::count());
	const JsonValue *addr = doc.get("nameArray");
	REQUIRE(addr != nullptr);
	CHECK(addr->string == "0xd9fad8");
}
