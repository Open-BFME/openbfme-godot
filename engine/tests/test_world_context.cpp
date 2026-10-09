// OpenBFME unit tests: the process-wide parsing context of RetailObjectWorld (lane AUDIO-1 review round 2). Several worlds may live at once and
// die in any order; each owns TheLocomotorStore, TheCommandStore, the weapon / armor / damage-FX stores and the audio validator, as removable
// registrations (Common/GlobalOwnerChain.h). These tests build tiny synthetic worlds (no retail data) and are meant to run under ASan / UBSan:
// the old pattern restored a destroyed owner's callback and store pointers.

#include "doctest.h"

#include "IniTestUtil.h"

#include "Common/GlobalOwnerChain.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"

#include <memory>
#include <string>

namespace
{
const char kDefaults[] = "AudioEvent DefaultSoundEffect\n  Volume = 100\n  Type = ui everyone\n  Type = +DEFAULT\nEnd\n";

// a world over its own tiny file tree: an audio table with `name` (and the map's HordeContain object when `mapSound` is set)
struct World
{
	initest::Fixture fx;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;

	World(const std::string &audioName, const std::string &mapSound = std::string())
	{
		initest::FileList files = { { "Data\\INI\\Default\\SoundEffects.ini", kDefaults },
			{ "Data\\INI\\SoundEffects.ini", "AudioEvent " + audioName + "\n  Sounds = x\n  Type = ui everyone\nEnd\n" } };
		if (!mapSound.empty())
		{
			files.push_back({ "maps\\m\\map.ini",
				"Object MapUnit\n  AddModule\n    Behavior = HordeContain ModuleTag_Contain\n      EnterSound = " + mapSound + "\n    End\n  End\nEnd\n" });
		}
		fx.mount(files);
		world = std::make_unique<RetailObjectWorld>(fx.fsys);
	}
	bool load() { return world->load(&error); }
	bool knows(const char *name) const
	{
		const auto &hook = TheContainParseHooks().audioEventExists;
		return hook && hook(name);
	}
};
} // namespace

TEST_CASE("GlobalOwnerChain: owners unlink wherever they sit; the slot only ever holds a live owner's value or the baseline")
{
	int base = 0, a = 1, b = 2, c = 3;
	int *slot = &base;
	GlobalOwnerChain<int *> chain(slot);
	chain.install(&a, &a);
	chain.install(&b, &b);
	chain.install(&c, &c);
	CHECK(slot == &c);
	chain.remove(&a); // the oldest dies first: the current one is undisturbed and nothing saved by B can come back
	CHECK(slot == &c);
	chain.remove(&c); // the current one dies: B takes over, never the dead A
	CHECK(slot == &b);
	chain.install(&a, &a);
	CHECK(slot == &a);
	chain.select(&b);
	CHECK(slot == &b);
	CHECK_FALSE(chain.select(&c)); // gone
	chain.remove(&b);
	CHECK(slot == &a);
	chain.remove(&a);
	CHECK(slot == &base); // the baseline
	CHECK(chain.size() == 0);
	slot = &c; // somebody assigns the slot directly while an owner is registered: its removal leaves that alone
	chain.install(&a, &a);
	slot = &b;
	chain.remove(&a);
	CHECK(slot == &b);
}

TEST_CASE("RetailObjectWorld: worlds A, B, C loaded and destroyed as A, C, B never leave a dead owner's callback or store selected")
{
	LocomotorStore *const locoBefore = TheLocomotorStore;
	CommandStore *const cmdBefore = TheCommandStore;
	WeaponStore *const weaponBefore = TheWeaponStore;
	const auto hookBefore = TheContainParseHooks().audioEventExists;
	// other tests keep shared retail worlds alive for the whole run: the state before this test is whatever they left
	const RetailObjectWorld *const contextBefore = RetailObjectWorld::currentContext();

	auto a = std::make_unique<World>("OnlyA");
	auto b = std::make_unique<World>("OnlyB");
	auto c = std::make_unique<World>("OnlyC");
	REQUIRE_MESSAGE(a->load(), a->error);
	REQUIRE_MESSAGE(b->load(), b->error);
	REQUIRE_MESSAGE(c->load(), c->error);
	CHECK(RetailObjectWorld::currentContext() == c->world.get());
	CHECK(c->world->isCurrentContext());
	CHECK(c->knows("OnlyC"));
	CHECK_FALSE(c->knows("OnlyA"));
	CHECK(TheWeaponStore == &c->world->weaponStores().weapons());

	a.reset(); // the oldest first: the current world C is untouched
	CHECK(RetailObjectWorld::currentContext() == c->world.get());
	CHECK(c->knows("OnlyC"));
	CHECK_FALSE(c->knows("OnlyA"));
	CHECK(TheWeaponStore == &c->world->weaponStores().weapons());

	c.reset(); // the current one: B takes over, never the freed A
	REQUIRE(RetailObjectWorld::currentContext() == b->world.get());
	CHECK(b->knows("OnlyB"));
	CHECK_FALSE(b->knows("OnlyA"));
	CHECK_FALSE(b->knows("OnlyC"));
	CHECK(TheWeaponStore == &b->world->weaponStores().weapons());
	CHECK(TheLocomotorStore != nullptr);
	CHECK(TheCommandStore != nullptr);

	b.reset(); // the last: the process state is what it was before any world
	CHECK(RetailObjectWorld::currentContext() == contextBefore);
	CHECK(TheLocomotorStore == locoBefore);
	CHECK(TheCommandStore == cmdBefore);
	CHECK(TheWeaponStore == weaponBefore);
	CHECK(static_cast<bool>(TheContainParseHooks().audioEventExists) == static_cast<bool>(hookBefore));
}

TEST_CASE("RetailObjectWorld: a refused second load and a load over an empty tree leave the context consistent")
{
	LocomotorStore *const locoBefore = TheLocomotorStore;
	WeaponStore *const weaponBefore = TheWeaponStore;
	{
		// a world over a file system without the INI tree: whatever load() answers, a failed one must leave nothing registered
		initest::Fixture empty({ { "Data\\readme.txt", "x" } });
		RetailObjectWorld lonely(empty.fsys);
		std::string err;
		const bool ok = lonely.load(&err);
		INFO("load over an empty tree: ok=" << ok << " " << err);
		CHECK(lonely.isCurrentContext() == ok);
		CHECK((RetailObjectWorld::currentContext() == &lonely) == ok);
	}
	CHECK(TheLocomotorStore == locoBefore);
	CHECK(TheWeaponStore == weaponBefore);
	auto good = std::make_unique<World>("OnlyGood");
	REQUIRE_MESSAGE(good->load(), good->error);
	auto loadTwice = std::make_unique<World>("OnlyTwice");
	REQUIRE_MESSAGE(loadTwice->load(), loadTwice->error);
	CHECK_FALSE(loadTwice->load()); // "called twice": refused, the context stays selected as it was
	CHECK(loadTwice->world->isCurrentContext());
	loadTwice.reset();
	CHECK(good->world->isCurrentContext());
	CHECK(good->knows("OnlyGood"));
	good.reset();
	CHECK(TheLocomotorStore == locoBefore);
	CHECK(TheWeaponStore == weaponBefore);
}

TEST_CASE("RetailObjectWorld: applyMapIni selects this world's stores and audio validator together, then the live context returns")
{
	auto a = std::make_unique<World>("OnlyA", "OnlyA");
	auto b = std::make_unique<World>("OnlyB");
	auto c = std::make_unique<World>("OnlyC");
	REQUIRE_MESSAGE(a->load(), a->error);
	REQUIRE_MESSAGE(b->load(), b->error);
	REQUIRE_MESSAGE(c->load(), c->error);
	REQUIRE(RetailObjectWorld::currentContext() == c->world.get());
	// A's map object has a contain sound that only A's table knows: it must validate against A's table although C is the live context
	const RetailObjectWorld::MapIniResult result = a->world->applyMapIni("maps\\m");
	CHECK(result.mapIniFound);
	std::string errors;
	for (const std::string &e : result.errors)
	{
		errors += e + "\n";
	}
	CHECK_MESSAGE(errors.find("Invalid Sound") == std::string::npos, errors);
	CHECK_MESSAGE(result.definitions.size() == 1, errors);
	CHECK(RetailObjectWorld::currentContext() == c->world.get()); // C is selected again, with its own stores and validator
	CHECK(c->world->isCurrentContext());
	CHECK(c->knows("OnlyC"));
	CHECK_FALSE(c->knows("OnlyA"));

	// control: a sound that only C knows is refused when A's map names it
	auto a2 = std::make_unique<World>("OnlyA2", "OnlyC");
	REQUIRE_MESSAGE(a2->load(), a2->error);
	const RetailObjectWorld::MapIniResult refused = a2->world->applyMapIni("maps\\m");
	bool invalid = false;
	for (const std::string &e : refused.errors)
	{
		invalid |= e.find("Invalid Sound") != std::string::npos;
	}
	CHECK_MESSAGE(invalid, (refused.errors.empty() ? std::string("no errors") : refused.errors[0]) << " definitions " << refused.definitions.size());
	CHECK(RetailObjectWorld::currentContext() == a2->world.get()); // a2 was the live context before the call
}
