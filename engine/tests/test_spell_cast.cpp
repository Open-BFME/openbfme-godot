// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-1 part 2, retail: every skirmish faction gets its spell book object at the game start, buys a tier-1 power through
// MSG_PURCHASE_SCIENCE, the power is ready at once (RW 0x6AE29F), it is cast through MSG_DO_SPECIAL_POWER_AT_LOCATION from the spell book, its effect
// is measured (a heal for the good factions, the summoned object for the evil ones), the recharge follows the SpecialPower ReloadTime, a second cast
// before it is refused, and two identical runs hash alike while a different target does not.

#include "doctest.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
struct CastFaction
{
	const char *templateName;
	const char *citadel;
	const char *science;  // a tier-1 science (MP cost 5)
	const char *power;    // its SpecialPower
	const char *summoned; // the object the power creates ("" = a heal)
	const char *unit;     // the faction's soldier (healed / counted)
};

const CastFaction kCast[7] = {
	{ "FactionMen", "MenFortressCitadel", "SCIENCE_Heal", "SpellBookHeal", "", "GondorFighter" },
	{ "FactionElves", "ElvenCitadel", "SCIENCE_Heal", "SpellBookHeal", "", "GondorFighter" },
	{ "FactionDwarves", "DwarvenFortressCitadel", "SCIENCE_Heal", "SpellBookHeal", "", "GondorFighter" },
	{ "FactionIsengard", "IsengardFortressCitadel", "SCIENCE_Crebain", "SpellBookCrebain", "Crebain", "" },
	{ "FactionMordor", "MordorFortressCitadel", "SCIENCE_EyeofSauron", "SpellBookEyeofSauron", "EyeOfSauron", "" },
	{ "FactionWild", "WildFortressCitadel", "SCIENCE_CaveBats", "SpellBookCaveBats", "WildCaveBats", "" },
	{ "FactionAngmar", "AngmarFortressCitadel", "SCIENCE_ChillWind", "SpellBookChillWind", "SpellBookChillWind", "" },
};

struct CastGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	spelltest::GameAI ai; // lane SPELL-2: the effect objects fire weapons (combat geometry)
	SpellCommands spell;
	starttest::Shared &s;
	std::vector<Object *> books;
	int bookDraws = 0; // spell books whose creation drew logic random numbers

	explicit CastGame(starttest::Shared &shared)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		for (int i = 0; i < 7; ++i)
		{
			SkirmishPlayer p;
			p.name = "c" + std::to_string(i);
			p.faction = kCast[i].templateName;
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		logic->random().seedRandom(11);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		ai.attach(shared, *logic);
		spell.registerHandlers(*dispatch);
		SpecialPowerModules::installScienceHooks(*logic);
		for (int i = 0; i < 7; ++i)
		{
			place(kCast[i].citadel, i, 1000.0f * (float)i + 500.0f, 300.0f)->friend_onBuildComplete();
			const auto before = logic->random().seedArray();
			books.push_back(SpecialPowerModules::createSpellBook(*logic, *player(i)));
			bookDraws += before != logic->random().seedArray() ? 1 : 0;
		}
	}
	~CastGame() { logic->reset(); } // the objects leave the pathfinder while the AIWorld exists
	Player *player(int i) { return players.findPlayerWithName("c" + std::to_string(i)); }
	Object *place(const char *name, int i, float x, float y)
	{
		const ThingTemplate *t = s.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		Object *o = logic->newObject(t, player(i)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		return o;
	}
	void purchase(int i, ScienceType st)
	{
		GameMessage m(MSG_PURCHASE_SCIENCE, player(i)->getPlayerIndex());
		m.appendIntegerArgument(player(i)->getPlayerIndex());
		m.appendIntegerArgument(st);
		dispatch->dispatch(m);
	}
	void castAt(int i, const SpecialPowerTemplate *t, const Coord3D &where)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, player(i)->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendLocationArgument(where);
		m.appendObjectIDArgument(INVALID_ID);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(books[(size_t)i]->getID());
		dispatch->dispatch(m);
	}
	int count(const char *name, int i)
	{
		int n = 0;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name && o->getControllingPlayer() == player(i) && !o->isDestroyed())
			{
				++n;
			}
		}
		return n;
	}
	std::uint32_t hash()
	{
		StateHasher h;
		for (int i = 0; i < players.getPlayerCount(); ++i)
		{
			players.getNthPlayer(i)->crc(h);
		}
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			h.addU32(o->getID());
			h.addFloat(o->getPosition()->x);
			for (const auto &m : o->modules())
			{
				m->crc(h);
			}
		}
		return h.value();
	}
};

void wound(Object &o, float amount)
{
	ActiveBody *b = dynamic_cast<ActiveBody *>(o.getBodyModule());
	REQUIRE(b);
	b->internalChangeHealth(-amount);
}
} // namespace

TEST_CASE("SPELL-1 retail: each faction's spell book casts a bought tier-1 power through the message path; effect, recharge and refusal")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	CastGame g(*shared);
	for (int i = 0; i < 7; ++i)
	{
		const CastFaction &f = kCast[i];
		INFO(std::string(f.templateName) << " " << std::string(f.power));
		REQUIRE(g.books[(size_t)i]);
		CHECK(g.books[(size_t)i]->getTemplate()->getName().find("SpellBook") != std::string::npos);
		CHECK(SpecialPowerModules::getSpellBookObject(*g.logic, *g.player(i)) == g.books[(size_t)i]);
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(f.power);
		REQUIRE(t);
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*g.books[(size_t)i], t);
		REQUIRE(sp);
		CHECK(sp->requirementsMet()); // the citadel is a COMMANDCENTER (SPELL_BOOK_REQUIREMENTS_FILTER)
		CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*g.books[(size_t)i], t)); // the science is not owned yet
		const Coord3D where{ 1000.0f * (float)i + 500.0f, 1500.0f, 0.0f };
		Object *wounded = nullptr;
		float before = 0.0f;
		if (*f.unit)
		{
			wounded = g.place(f.unit, i, where.x + 20.0f, where.y);
			wound(*wounded, wounded->getBodyModule()->getMaxHealth() * 0.8f);
			before = wounded->getBodyModule()->getHealth();
		}
		// a cast before the purchase is refused (the reload is still running and the science is missing)
		g.castAt(i, t, where);
		if (wounded)
		{
			CHECK(wounded->getBodyModule()->getHealth() == before);
		}
		// buy, ready at once (skirmish: setReadyFrame(now), RW 0x6AE29F)
		g.purchase(i, store.getScienceFromInternalName(f.science));
		CHECK(g.player(i)->hasScience(store.getScienceFromInternalName(f.science)));
		CHECK(sp->isReady());
		CHECK(sp->getPercentReady() == 1.0f);
		const int summonedBefore = *f.summoned ? g.count(f.summoned, i) : 0;
		g.castAt(i, t, where);
		if (wounded)
		{
			CHECK(wounded->getBodyModule()->getHealth() > before);
			MESSAGE(std::string(f.templateName) << ": " << std::string(f.power) << " healed " << std::string(f.unit) << " " << before << " -> " << wounded->getBodyModule()->getHealth());
		}
		else
		{
			CHECK(g.count(f.summoned, i) == summonedBefore + 1);
			MESSAGE(std::string(f.templateName) << ": " << std::string(f.power) << " summoned " << std::string(f.summoned) << " (" << g.count(f.summoned, i) << ")");
		}
		// the recharge: not ready, the reload in frames (ReloadTime at 5 frames a second), a second cast refused
		CHECK_FALSE(sp->isReady());
		CHECK(sp->getReadyFrame() == g.logic->getFrame() + t->getReloadTime());
		CHECK(sp->getPercentReady() == 0.0f);
		const unsigned long long refusedBefore = g.spell.refused();
		g.castAt(i, t, where);
		CHECK(g.spell.refused() == refusedBefore + 1);
	}
	MESSAGE("spell book creations that drew logic random numbers: " << g.bookDraws);
	CHECK(g.spell.casts() == 7u);
	CHECK(g.spell.malformed() == 0u);
	// the reload runs out on the logic clock
	const SpecialPowerTemplate *heal = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookHeal");
	SpecialPowerModuleInterface *sp0 = SpecialPowerModules::findModule(*g.books[0], heal);
	const unsigned ready = sp0->getReadyFrame();
	while (g.logic->getFrame() < ready)
	{
		CHECK_FALSE(sp0->isReady());
		g.logic->runLogicFrame();
	}
	CHECK(sp0->isReady());
}

TEST_CASE("SPELL-1 retail: two identical cast sequences hash alike; another target changes the hash")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	auto run = [&](float x) {
		CastGame g(*shared);
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookCrebain");
		g.purchase(3, store.getScienceFromInternalName("SCIENCE_Crebain"));
		g.castAt(3, t, Coord3D{ x, 1500.0f, 0.0f });
		for (int f = 0; f < 10; ++f)
		{
			g.logic->runLogicFrame();
		}
		return g.hash();
	};
	const std::uint32_t a = run(3500.0f), b = run(3500.0f), c = run(3600.0f);
	CHECK(a == b);
	CHECK(a != c);
}

TEST_CASE("SPELL-1 retail: display reads of the spell book never change the state (Sol review r2: two worlds, one polled)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	CastGame polled(*shared), quiet(*shared);
	// GondorGandalf carries SuperweaponPartTheHeavens, a SharedSyncedTimer power: the logic getters would insert its Player + 0x724 entry
	Object *g1 = polled.place("GondorGandalf", 0, 2000.0f, 2000.0f);
	Object *g2 = quiet.place("GondorGandalf", 0, 2000.0f, 2000.0f);
	const SpecialPowerTemplate *parted = TheSpecialPowerStore->findSpecialPowerTemplate("SuperweaponPartTheHeavens");
	REQUIRE(parted);
	REQUIRE(parted->isSharedNSync());
	SpecialPowerModuleInterface *shared1 = SpecialPowerModules::findModule(*g1, parted);
	REQUIRE(shared1);
	auto poll = [&](CastGame &g) {
		for (int i = 0; i < 7; ++i)
		{
			Object *book = SpecialPowerModules::findSpellBookObject(*g.logic, *g.player(i));
			REQUIRE(book);
			for (const auto &m : book->modules())
			{
				if (SpecialPowerModuleInterface *sp = m->getSpecialPower())
				{
					(void)sp->isReadyForDisplay();
					(void)sp->getPercentReadyForDisplay();
				}
			}
		}
		(void)shared1->isReadyForDisplay();
		(void)shared1->getPercentReadyForDisplay();
	};
	CHECK(polled.hash() == quiet.hash());
	poll(polled);
	CHECK(polled.hash() == quiet.hash());
	const SpecialPowerTemplate *heal = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookHeal");
	for (CastGame *g : { &polled, &quiet })
	{
		g->purchase(0, store.getScienceFromInternalName("SCIENCE_Heal"));
		g->castAt(0, heal, Coord3D{ 500.0f, 1500.0f, 0.0f });
		g->logic->runLogicFrame();
	}
	poll(polled);
	CHECK(polled.hash() == quiet.hash());
	// the logic getter does insert (retail RW 0x6AD26F): asked in one world only, the hashes part
	(void)shared1->isReady();
	CHECK(polled.hash() != quiet.hash());
}
