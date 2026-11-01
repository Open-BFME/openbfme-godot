// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: SalvageCrateCollide and its CrateCollide base (onCollide RW 0x8C06B6, isValidToExecute RW 0x8C080B / 0x8C145D, executeCrateBehavior
// RW 0x8C158B, the money RW 0x8C148B) with AIWorld's crate contact pass. Binary facts read from the RotWK binary (RW_GAME_DAT), the retail data
// (ROTWK_INSTALL) and live objects in a started skirmish on Evendim. The expectations are retail's rules (each check names its address).

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/SalvageCrateCollide.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
float floatAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// the lobby's result: slot 0 a human (Men), slot 1 an Easy computer (Angmar) on Evendim, start positions 0 and 4 (test_pickup_modules.cpp's game)
bool startCrateGame(starttest::Shared &s, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, "FactionMen");
	b.tmpl = buildtest::templateIndex(s, "FactionAngmar");
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}

SalvageCrateCollide *crateOf(Object &o)
{
	return dynamic_cast<SalvageCrateCollide *>(o.findModule("SalvageCrateCollide"));
}

const SalvageCrateCollideModuleData *crateDataOf(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (n.name == "SalvageCrateCollide")
		{
			return dynamic_cast<const SalvageCrateCollideModuleData *>(n.data.get());
		}
	}
	return nullptr;
}

bool alive(GameLogic &logic, ObjectID id)
{
	const Object *o = logic.findObjectByID(id);
	return o && !o->isDestroyed();
}

const char *const kUnit = "AngmarOrcWarrior"; // a horde member: an AI unit without HORDE

struct CrateGame
{
	buildtest::Game g;
	Player *human = nullptr;
	Player *computer = nullptr;
	Coord3D spot{};
};

void startGame(starttest::Shared &s, CrateGame &c, std::uint32_t seed)
{
	std::string err;
	REQUIRE_MESSAGE(startCrateGame(s, seed, c.g, &err), err);
	GameLogic &logic = c.g.live->logic();
	c.human = playerAtStart(logic, 0);
	c.computer = playerAtStart(logic, 4);
	REQUIRE(c.human);
	REQUIRE(c.computer);
	Object *fortress = firstOf(logic, c.human, "COMMANDCENTER");
	REQUIRE(fortress);
	c.spot = Coord3D{ fortress->getPosition()->x + 600.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	c.spot.z = logic.getGroundHeight(c.spot.x, c.spot.y);
}

Object *make(CrateGame &c, const char *name, int playerIndex, const Coord3D &at)
{
	std::string err;
	Object *o = c.g.live->createObject(name, playerIndex, at, 0.0f, &err);
	REQUIRE_MESSAGE(o, err);
	return o;
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("crate1: SalvageCrateCollide's binary facts: vtables (RW 0xC715C0 / 0xC715A8), the data defaults (RW 0x65592C / 0x8C07A0), the onCollide calls")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("crate1 binary facts (set RW_GAME_DAT)");
		return;
	}
	// the module's vtables (constructor RW 0x8C12C2): primary RW 0xC715C0 (slot 3 xfer, vslot 0x30 executeCrateBehavior, vslot 0x34 isValidToExecute), collide RW 0xC715A8
	CHECK(pe->u32At(0xC715C0u + 0x0Cu) == 0x8C1442u);
	CHECK(pe->u32At(0xC715C0u + 0x30u) == 0x8C158Bu);
	CHECK(pe->u32At(0xC715C0u + 0x34u) == 0x8C145Du);
	CHECK(pe->u32At(0xC715A8u) == 0x8C06B6u);
	CHECK(pe->u32At(0xC715A8u + 4u) == 0x8C0664u);
	// onCollide: the two virtual calls (call [eax + 0x34] / [eax + 0x30]), the FX (RW 0x4B1B5A), the ever-executed byte (mov byte [edi + 4], 1: + 0x14), the destroy
	CHECK((pe->u32At(0x8C06CBu) & 0xFFFFFFu) == 0x3450FFu);
	CHECK((pe->u32At(0x8C06DBu) & 0xFFFFFFu) == 0x3050FFu);
	CHECK(pe->u32At(0x8C06EEu) == 0x4B1B5Au - 0x8C06F2u);
	CHECK(pe->u32At(0x8C06F8u) == 0x010447C6u);
	CHECK(pe->u32At(0x8C0703u) == 0x62BBABu - 0x8C0707u);
	// the outcome draw: GetGameLogicRandomValueReal (RW 0x6D332C) at line 0x45; the money draw: GetGameLogicRandomValue (RW 0x6D328E) at line 0xDB
	CHECK(pe->u32At(0x8C1385u) == 0x6D332Cu - 0x8C1389u);
	CHECK((pe->u32At(0x8C1372u) & 0xFFFFu) == 0x456Au);
	CHECK(pe->u32At(0x8C14B6u) == 0x6D328Eu - 0x8C14BAu);
	CHECK(pe->u32At(0x8C14AAu) == 0xDBu);
	// SalvageCrateCollideModuleData (RW 0x65592C): LevelUpChance / ResourceChance 0.5 (RW 0xBD869C), LevelUpRadius 100.0 (RW 0xBD88D8), Min 100, Max 5000
	CHECK(floatAt(*pe, 0xBD869C) == 0.5f);
	CHECK(floatAt(*pe, 0xBD88D8) == 100.0f);
	CHECK(pe->u32At(0x655969u) == 100u);
	CHECK(pe->u32At(0x655970u) == 5000u);
	// CrateCollideModuleData (RW 0x8C07A0): PickupScience -1 (or dword [esi + 0x44], -1), ExecuteAnimationFades 1 (mov byte [esi + 0x58], 1)
	CHECK(pe->u32At(0x8C07CDu) == 0xFF444E83u);
	CHECK(pe->u32At(0x8C07FAu) == 0x015846C6u);
}

// ---- the 2.01 data -----------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("crate1: the 2.01 data: the class is bound with a runtime; SalvageCrate and its treasure chests (crate.ini)")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "SalvageCrateCollide") == unported.end());
	int uses = 0;
	for (const ThingTemplate *tt : s->world->things().templates())
	{
		uses += crateDataOf(*tt) ? 1 : 0;
	}
	CHECK(uses >= 13); // tolerance: the 13 templates that list the module (rotwk201_module_histogram.tsv), plus the reskins that inherit it
	const ThingTemplate *salvage = s->world->things().findTemplate("SalvageCrate");
	const ThingTemplate *chest1 = s->world->things().findTemplate("TreasureChest1");
	REQUIRE(salvage);
	REQUIRE(chest1);
	const SalvageCrateCollideModuleData *d = crateDataOf(*salvage);
	REQUIRE(d);
	CHECK(!d->m_allowAIPickup);
	CHECK(d->m_levelUpChance == 1.0f);
	CHECK(d->m_levelUpRadius == 1.0f); // parsePercentToReal (RW 0x42EEFA): 100.0 -> 1.0
	CHECK(d->m_minResource == 25);
	CHECK(d->m_maxResource == 75);
	CHECK(d->m_executeFX == "FX_GoldChestPickup");
	CHECK(d->m_pickupScience == -1);
	CHECK(d->m_executeAnimationFades); // RW 0x8C07FA default
	const SalvageCrateCollideModuleData *d1 = crateDataOf(*chest1);
	REQUIRE(d1);
	CHECK(d1->m_allowAIPickup);
	CHECK(d1->m_levelUpChance == 0.0f);
	CHECK(d1->m_minResource == 50);
	CHECK(d1->m_maxResource == 100);
	CHECK(d1->m_upgrade.empty());
}

// ---- the live module ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("crate1: a unit that touches a treasure chest pays its player the chest's money once and the chest is destroyed (RW 0x8C06B6 / 0x8C148B)")
{
	OPENBFME_REQUIRE_START(s);
	CrateGame c;
	startGame(*s, c, 6101);
	GameLogic &logic = c.g.live->logic();
	const int neutral = logic.players().getNeutralPlayer()->getPlayerIndex();
	Object *chest = make(c, "TreasureChest1", neutral, c.spot);
	const ObjectID chestId = chest->getID();
	SalvageCrateCollide *m = crateOf(*chest);
	REQUIRE(m);
	REQUIRE(CrateCollide::isCrate(*chest));
	Object *unit = make(c, kUnit, c.human->getPlayerIndex(), c.spot);
	CHECK(m->wouldLikeToCollideWith(*unit)); // RW 0x8C0664 = isValidToExecute
	const std::uint32_t before = c.human->getMoney()->countMoney();
	const unsigned long long fxBefore = logic.fxEvents().perSite().count("CrateCollide") ? logic.fxEvents().perSite().at("CrateCollide") : 0;
	logic.random().clearCallLog();
	logic.random().enableCallLog(true);
	c.g.live->stepFrame();
	logic.random().enableCallLog(false);
	CHECK(!alive(logic, chestId)); // RW 0x62BBAB
	REQUIRE(logic.aiWorld());
	CHECK(logic.aiWorld()->crateContacts() >= 1); // tolerance: the count is for the whole game (any other crate contact counts too)
	// the draws: the outcome (line 0x45, a real in [0, 1)), then the money (line 0xDB, MinResource .. MaxResource), in this order and once each
	std::vector<GameLogicRandom::Call> ours;
	for (const GameLogicRandom::Call &call : logic.random().callLog())
	{
		if (call.file == "SalvageCrateCollide.cpp")
		{
			ours.push_back(call);
		}
	}
	REQUIRE(ours.size() == 2);
	CHECK(ours[0].real);
	CHECK(ours[0].line == 0x45);
	CHECK(!ours[1].real);
	CHECK(ours[1].line == 0xDB);
	CHECK(ours[1].lo == 50);
	CHECK(ours[1].hi == 100);
	// RW 0x8C14DD: a skirmish is a multiplayer game: _ftol(amount * MultiPlayMoneyMult[n - 1]) (fmul at 24 bits), then the handicap (identity in a skirmish)
	const Economy &econ = logic.economy();
	REQUIRE(econ.isMultiplayerGame());
	const float mult = econ.multiPlayMoneyMult(econ.livePlayableCount(false));
	const int expected = (int)(float)((double)ours[1].result * (double)mult);
	MESSAGE("the chest drew " << ours[1].result << ", MultiPlayMoneyMult " << mult << ": " << expected);
	CHECK(c.human->getMoney()->countMoney() == before + (std::uint32_t)expected);
	// (the chest's module went with the chest at the frame's end: its outcome is read from the money and the draws above)
	const unsigned long long fxAfter = logic.fxEvents().perSite().count("CrateCollide") ? logic.fxEvents().perSite().at("CrateCollide") : 0;
	CHECK(fxAfter == fxBefore + 1); // ExecuteFX FX_GoldChestPickup on the unit (RW 0x4B1B5A)
	// collected once: the next frames pay nothing more
	const std::uint32_t paid = c.human->getMoney()->countMoney();
	for (int i = 0; i < 10; ++i)
	{
		c.g.live->stepFrame();
	}
	CHECK(c.human->getMoney()->countMoney() >= paid); // tolerance: the human's own income may arrive meanwhile; no crate pays twice
}

TEST_CASE("crate1: a SalvageCrate levels up the human unit that touches it (LevelUpChance 100%, RW 0x8C1625) and refuses a computer's unit (AllowAIPickup No, RW 0x8C15AF)")
{
	OPENBFME_REQUIRE_START(s);
	CrateGame c;
	startGame(*s, c, 6102);
	GameLogic &logic = c.g.live->logic();
	const int neutral = logic.players().getNeutralPlayer()->getPlayerIndex();
	// the computer's unit: refused (no draw, no FX), the crate stays
	Object *crateA = make(c, "SalvageCrate", neutral, c.spot);
	const ObjectID crateAId = crateA->getID();
	SalvageCrateCollide *ma = crateOf(*crateA);
	REQUIRE(ma);
	Object *orc = make(c, kUnit, c.computer->getPlayerIndex(), c.spot);
	REQUIRE(orc->getExperienceTracker());
	const int orcRank = orc->getExperienceTracker()->getRank();
	for (int i = 0; i < 5; ++i)
	{
		c.g.live->stepFrame();
	}
	CHECK(alive(logic, crateAId));
	CHECK(ma->refusedComputer() >= 1); // tolerance: one refusal per frame the unit still overlaps the crate (it may step off)
	CHECK(ma->lastOutcome() == 0);
	CHECK(!ma->everExecuted());
	CHECK(orc->getExperienceTracker()->getRank() == orcRank);
	// a human's unit on another crate: one level (gainExpForLevel(1, true, false), RW 0x79DA0A), the crate is destroyed
	const Coord3D spot2{ c.spot.x, c.spot.y + 200.0f, logic.getGroundHeight(c.spot.x, c.spot.y + 200.0f) };
	Object *crateB = make(c, "SalvageCrate", neutral, spot2);
	const ObjectID crateBId = crateB->getID();
	SalvageCrateCollide *mb = crateOf(*crateB);
	REQUIRE(mb);
	Object *man = make(c, kUnit, c.human->getPlayerIndex(), spot2);
	const int rank = man->getExperienceTracker()->getRank();
	const std::uint32_t money = c.human->getMoney()->countMoney();
	c.g.live->stepFrame();
	CHECK(!alive(logic, crateBId));
	CHECK(man->getExperienceTracker()->getRank() == rank + 1);
	CHECK(c.human->getMoney()->countMoney() == money); // no money outcome
}

TEST_CASE("crate1: CrateCollide's tests (RW 0x8C080B / 0x8C145D): no other, an IMMOBILE or a neutral other, a crate above the ground, an executed crate")
{
	OPENBFME_REQUIRE_START(s);
	CrateGame c;
	startGame(*s, c, 6103);
	GameLogic &logic = c.g.live->logic();
	const int neutral = logic.players().getNeutralPlayer()->getPlayerIndex();
	Object *crate = make(c, "TreasureChest1", neutral, c.spot);
	SalvageCrateCollide *m = crateOf(*crate);
	REQUIRE(m);
	CHECK(!m->isValidToExecute(nullptr));
	Object *unit = make(c, kUnit, c.human->getPlayerIndex(), Coord3D{ c.spot.x + 500.0f, c.spot.y, c.spot.z });
	CHECK(m->isValidToExecute(unit));
	// a neutral controlled other (RW 0x68B760)
	Object *neutralUnit = make(c, kUnit, neutral, Coord3D{ c.spot.x + 500.0f, c.spot.y + 100.0f, c.spot.z });
	CHECK(!m->isValidToExecute(neutralUnit));
	// an other without an AI, or IMMOBILE (template + 0x108 bit 2): a crate does not collect a crate
	Object *other = make(c, "TreasureChest1", c.human->getPlayerIndex(), Coord3D{ c.spot.x + 500.0f, c.spot.y + 200.0f, c.spot.z });
	CHECK(!m->isValidToExecute(other));
	// the crate above the terrain (RW 0x4B12E8: height > 0)
	const Coord3D raised{ c.spot.x, c.spot.y, c.spot.z + 5.0f };
	crate->setPosition(&raised);
	CHECK(!m->isValidToExecute(unit));
	crate->setPosition(&c.spot);
	CHECK(m->isValidToExecute(unit));
	// executed: the ever-executed byte refuses every later contact (+ 0x14)
	m->onCollide(unit, nullptr, nullptr);
	CHECK(m->everExecuted());
	CHECK(!m->isValidToExecute(unit));
	CHECK(crate->isDestroyed());
}

// ---- the snapshot ------------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("crate1: the ever-executed byte survives a save and load (RW 0x8C1442 / 0x8C0760); a truncated snapshot is refused")
{
	OPENBFME_REQUIRE_START(s);
	CrateGame c;
	startGame(*s, c, 6104);
	GameLogic &logic = c.g.live->logic();
	const int neutral = logic.players().getNeutralPlayer()->getPlayerIndex();
	Object *a = make(c, "TreasureChest1", neutral, c.spot);
	Object *b = make(c, "TreasureChest1", neutral, Coord3D{ c.spot.x + 300.0f, c.spot.y, c.spot.z });
	SalvageCrateCollide *ma = crateOf(*a);
	SalvageCrateCollide *mb = crateOf(*b);
	REQUIRE(ma);
	REQUIRE(mb);
	Object *unit = make(c, kUnit, c.human->getPlayerIndex(), Coord3D{ c.spot.x + 600.0f, c.spot.y, c.spot.z });
	ma->onCollide(unit, nullptr, nullptr); // executed (and destroyed at the frame's end)
	REQUIRE(ma->everExecuted());
	StateHasher hashA;
	ma->crc(hashA);
	XferSave saved;
	ma->xfer(saved);
	StateHasher hashB;
	mb->crc(hashB);
	CHECK(hashB.value() != hashA.value());
	{
		XferLoad load(saved.data());
		mb->xfer(load);
	}
	CHECK(mb->everExecuted());
	CHECK(!mb->isValidToExecute(unit));
	StateHasher hashLoaded;
	mb->crc(hashLoaded);
	CHECK(hashLoaded.value() == hashA.value());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(mb->xfer(lt), XferException);
}

// ---- the map's crates --------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("crate1: Evendim's crates at the start: each one's template and whether a computer player's unit may collect it (AllowAIPickup)")
{
	OPENBFME_REQUIRE_START(s);
	CrateGame c;
	startGame(*s, c, 6105);
	GameLogic &logic = c.g.live->logic();
	std::map<std::string, int> crates;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOfName("CRATE"))
		{
			++crates[o->getTemplate()->getName()];
		}
	}
	for (const auto &kv : crates)
	{
		const ThingTemplate *tt = s->world->things().findTemplate(kv.first);
		REQUIRE(tt);
		const SalvageCrateCollideModuleData *d = crateDataOf(*tt);
		MESSAGE("Evendim crate " << kv.first << " x" << kv.second << ": " << (d ? (d->m_allowAIPickup ? "SalvageCrateCollide, AllowAIPickup" : "SalvageCrateCollide, no AI pick-up") : "another crate collide"));
	}
	c.g.live->stepFrame();
	REQUIRE(logic.aiWorld());
	int live = 0;
	for (const auto &kv : crates)
	{
		const ThingTemplate *tt = s->world->things().findTemplate(kv.first);
		live += crateDataOf(*tt) ? kv.second : 0;
	}
	CHECK(logic.aiWorld()->crateCount() == (size_t)live); // the crate pass's list after its first rebuild (every map crate with a SalvageCrateCollide)
}
