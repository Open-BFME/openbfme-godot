// OpenBFME unit tests (lane MODULES-1, review r1): parse-time facts of the module data classes, on synthetic objects (no retail data).

#include "doctest.h"

#include "ObjectTestUtil.h"

#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/Module/ExtraModules.h"
#include "GameLogic/Module/FlammableUpdate.h"

using objtest::contains;

namespace
{
struct ModWorld : objtest::World
{
	ModWorld() { ExtraModules::registerAll(modules); }
	template <class D>
	const D *dataOf(const char *name, const char *cls)
	{
		for (const auto &n : get(name)->behaviorModules().nuggets())
		{
			if (n.name == cls)
			{
				return dynamic_cast<const D *>(n.data.get());
			}
		}
		return nullptr;
	}
};

struct HookScope // installs an audio lookup for the test and restores the previous one
{
	std::function<bool(const std::string &)> saved = TheContainParseHooks().audioEventExists;
	explicit HookScope(std::function<bool(const std::string &)> f) { TheContainParseHooks().audioEventExists = std::move(f); }
	~HookScope() { TheContainParseHooks().audioEventExists = saved; }
};
} // namespace

TEST_CASE("ExperienceLevelCreate: LevelToGrant starts at -1 (RW 0x8BD363) and is parsed when given")
{
	ModWorld w;
	REQUIRE(w.load("Object A\n  Behavior = ExperienceLevelCreate T\n  End\nEnd\n"
	               "Object B\n  Behavior = ExperienceLevelCreate T\n    LevelToGrant = 7\n    MPOnly = Yes\n  End\nEnd\n").empty());
	const auto *a = w.dataOf<ExperienceLevelCreateModuleData>("A", "ExperienceLevelCreate");
	const auto *b = w.dataOf<ExperienceLevelCreateModuleData>("B", "ExperienceLevelCreate");
	REQUIRE(a);
	REQUIRE(b);
	CHECK(a->m_levelToGrant == -1);
	CHECK_FALSE(a->m_mpOnly);
	CHECK(b->m_levelToGrant == 7);
	CHECK(b->m_mpOnly);
}

TEST_CASE("FlammableUpdate: BurningSoundName is an audio event (RW 0x73B217 -> 0x73AA94): NoSound clears, an unknown name is Invalid Sound")
{
	HookScope hook([](const std::string &n) { return n == "KnownFire"; });
	ModWorld w;
	REQUIRE(w.load("Object A\n  Behavior = FlammableUpdate T\n    BurningSoundName = KnownFire\n  End\nEnd\n"
	               "Object B\n  Behavior = FlammableUpdate T\n    BurningSoundName = nosound\n  End\nEnd\n").empty());
	CHECK(w.dataOf<FlammableUpdateModuleData>("A", "FlammableUpdate")->m_burningSoundName == "KnownFire");
	CHECK(w.dataOf<FlammableUpdateModuleData>("B", "FlammableUpdate")->m_burningSoundName.empty());
	int code = 0;
	const std::string err = w.load("Object C\n  Behavior = FlammableUpdate T\n    BurningSoundName = Bogus\n  End\nEnd\n", INI_LOAD_OVERWRITE, "c.ini", &code);
	CHECK(contains(err, "Invalid Sound 'Bogus'"));
	CHECK(code == 3);
}

TEST_CASE("FlammableUpdate: without an audio lookup a sound name cannot be resolved: an error, not a silent accept")
{
	HookScope hook(nullptr);
	ModWorld w;
	int code = 0;
	CHECK(contains(w.load("Object A\n  Behavior = FlammableUpdate T\n    BurningSoundName = Anything\n  End\nEnd\n", INI_LOAD_OVERWRITE, "a.ini", &code), "cannot be resolved"));
	CHECK(code == 8);
	CHECK(w.load("Object B\n  Behavior = FlammableUpdate T\n    BurningSoundName = NoSound\n  End\nEnd\n").empty());
}
