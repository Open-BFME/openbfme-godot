// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: PickupStuffUpdate (RW 0x8957B7 / 0x895741 / 0x895632 / 0x895572) and the AI's temporary state machine (RW 0x6630A9 / 0x6630D7) it
// runs. Binary facts read from the RotWK binary (RW_GAME_DAT), the retail data (ROTWK_INSTALL) and live objects in a started skirmish. The expectations are
// retail's rules (each check names its address), never this engine's frame-exact output.

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
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PickupStuffUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cstring>
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

// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer of `aiState` (`faction1`) on Evendim, start positions 0 and 4 (test_mod4_modules.cpp's game)
bool startPickupGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, SlotState aiState, std::uint32_t seed, buildtest::Game &out,
                     std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, faction0);
	b.tmpl = buildtest::templateIndex(s, faction1);
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = aiState;
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

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
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

Coord3D nearObject(const Object &o, float dx, float dy)
{
	return Coord3D{ o.getPosition()->x + dx, o.getPosition()->y + dy, o.getPosition()->z };
}

PickupStuffUpdate *pickupOf(Object &o)
{
	return dynamic_cast<PickupStuffUpdate *>(o.findModule("PickupStuffUpdate"));
}

const char *const kHorde = "AngmarOrcWarriors"; // a retail horde with the module (SkirmishAIOnly, NONE +CRATE, 200, 0.5)
const char *const kCrate = "SalvageCrate";      // data\ini\crate.ini: KindOf CRATE; AllowAIPickup No: a computer player's unit never collects it
const char *const kChest = "TreasureChest1";    // data\ini\crate.ini: a SalvageCrate child with AllowAIPickup Yes, LevelUpChance 0% (money 50 .. 100)

// a started game with the AI's horde (start 4's player) `dx` from the human's fortress and a crate `crateDx` further on; the horde is left idle
struct PickupGame
{
	buildtest::Game g;
	const Player *human = nullptr;
	const Player *computer = nullptr;
	Object *horde = nullptr;
	Object *crate = nullptr;
	PickupStuffUpdate *module = nullptr;
	AIUpdateInterface *ai = nullptr;
};

void startWithHorde(starttest::Shared &s, PickupGame &p, std::uint32_t seed, bool withCrate)
{
	std::string err;
	REQUIRE_MESSAGE(startPickupGame(s, "FactionMen", "FactionAngmar", SLOT_EASY_AI, seed, p.g, &err), err);
	GameLogic &logic = p.g.live->logic();
	p.human = playerAtStart(logic, 0);
	p.computer = playerAtStart(logic, 4);
	REQUIRE(p.human);
	REQUIRE(p.computer);
	REQUIRE(logic.skirmishAI().findAI(p.computer->getPlayerIndex()) != nullptr);
	REQUIRE(logic.skirmishAI().findAI(p.human->getPlayerIndex()) == nullptr);
	Object *fortress = firstOf(logic, p.human, "COMMANDCENTER");
	REQUIRE(fortress);
	p.horde = p.g.live->createObject(kHorde, p.computer->getPlayerIndex(), nearObject(*fortress, 700.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(p.horde, err);
	p.module = pickupOf(*p.horde);
	REQUIRE(p.module);
	p.ai = p.horde->getAIUpdateInterface();
	REQUIRE(p.ai);
	if (withCrate)
	{
		p.crate = p.g.live->createObject(kCrate, p.human->getPlayerIndex(), nearObject(*p.horde, 120.0f, 0.0f), 0.0f, &err);
		REQUIRE_MESSAGE(p.crate, err);
		REQUIRE(p.crate->isKindOfName("CRATE"));
	}
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pickup1: PickupStuffUpdate's binary facts: the data defaults (RW 0x8954C3), the status mask (RW 0x895661) and the AI's vslots 0x160 / 0x164")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("pickup1 binary facts (set RW_GAME_DAT)");
		return;
	}
	CHECK(floatAt(*pe, 0xBE4170) == 200.0f); // ScanRange (RW 0x8954D4)
	CHECK(floatAt(*pe, 0xBD869C) == 0.5f);   // ScanIntervalSeconds (RW 0x8954F7)
	// RW 0x895661: or dword [ebp - 0x20], 4: the scan's "must be clear" object status mask is dword 3 bit 2, status 0x62 (ATTACHED)
	CHECK((pe->u32At(0x895661) & 0xFFFFFFu) == 0xE04D83u);
	CHECK((pe->u32At(0x895664) & 0xFFu) == 0x04u);
	// AIUpdateInterface's vtable (RW 0xC10590: slot 0x1B8 is isIdle RW 0x6643FC): vslots 0x160 / 0x164, run by RW 0x6630B4 / 0x66311F, are RW 0x63F3BF (ret)
	CHECK(pe->u32At(0xC10590u + 0x1B8u) == 0x6643FCu);
	CHECK(pe->u32At(0xC10590u + 0x160u) == 0x63F3BFu);
	CHECK(pe->u32At(0xC10590u + 0x164u) == 0x63F3BFu);
	// GiantBirdAIUpdate's (RW 0xC2E1C8: slot 0x1B8 RW 0x769E8F): 0x164 is its own RW 0x76D3B1 (not ported, S-4040)
	CHECK(pe->u32At(0xC2E1C8u + 0x1B8u) == 0x769E8Fu);
	CHECK(pe->u32At(0xC2E1C8u + 0x164u) == 0x76D3B1u);
	// SalvageCrateCollide (constructor RW 0x8C12C2: + 0x10 = RW 0xC715A8, the collide interface): slot 0 onCollide RW 0x8C06B6, slot 1 RW 0x8C0664
	// (isValidToExecute). onCollide destroys the crate it executed (TheGameLogic RW 0x62BBAB at RW 0x8C0702): the crate the pick-up walks to is gone after
	CHECK(pe->u32At(0xC715A8u) == 0x8C06B6u);
	CHECK(pe->u32At(0xC715A8u + 4u) == 0x8C0664u);
	CHECK(pe->u32At(0x8C0703u) == 0x62BBABu - 0x8C0707u); // call rel32
}

TEST_CASE("pickup1: the 2.01 data: every horde's PickupStuffUpdate is SkirmishAIOnly, NONE +CRATE within 200 every 0.5 s; the class is bound")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "PickupStuffUpdate") == unported.end());
	int uses = 0;
	for (const ThingTemplate *tt : s->world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			if (n.name != "PickupStuffUpdate")
			{
				continue;
			}
			++uses;
			const auto *d = dynamic_cast<const PickupStuffUpdateModuleData *>(n.data.get());
			REQUIRE_MESSAGE(d, tt->getName());
			CHECK(d->m_skirmishAIOnly);
			CHECK(d->m_scanRange == 200.0f);
			CHECK(d->m_scanIntervalSeconds == 0.5f);
			CHECK(d->m_stuffToPickUp.rule == ObjectFilter::RULE_NONE);
		}
	}
	CHECK(uses >= 113); // tolerance: the 113 templates that list the module, plus the templates that inherit it (ChildObject / reskins)
	int found = 0;
	for (const std::string &l : s->world->acceptanceStops())
	{
		found += l.rfind("[S-1422] PickupStuffUpdate", 0) == 0 ? 1 : 0;
	}
	CHECK(found == 1);
}

// ---- the live module ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pickup1: a computer player's horde walks to a treasure chest in a temporary AI machine, collects it once (CrateCollide RW 0x8C06B6) and goes back to its own machine (RW 0x895741 / 0x895572)")
{
	OPENBFME_REQUIRE_START(s);
	PickupGame p;
	startWithHorde(*s, p, 5101, false);
	GameLogic &logic = p.g.live->logic();
	const AIStateMachine *own = p.ai->stateMachineOrNull();
	REQUIRE(own);
	// a chest a computer player's unit may collect (crate.ini TreasureChest1: AllowAIPickup, LevelUpChance 0%: money), 120 from the horde, the map's neutral one
	std::string err;
	Object *chest = p.g.live->createObject(kChest, logic.players().getNeutralPlayer()->getPlayerIndex(), nearObject(*p.horde, 120.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(chest, err);
	const ObjectID chestId = chest->getID();
	CHECK(PickupStuffUpdate::crateCollideRuns(*chest)); // SalvageCrateCollide runs (lane MODCOMPAT-MOD1, S-260)
	const std::uint32_t moneyBefore = p.computer->getMoney()->countMoney();
	// RW 0x895741: the first scan is due once frame > 0 + 5 * 0.5; it finds the chest (CRATE, alive, not ATTACHED) within 200
	int frames = 0;
	while (p.module->pickups() == 0 && frames < 200)
	{
		p.g.live->stepFrame();
		++frames;
	}
	MESSAGE("the pick-up began after " << frames << " frames");
	REQUIRE(p.module->pickups() == 1);
	CHECK(p.module->pickingUp());
	CHECK(p.module->lastScanFrame() >= 3u); // tolerance: the scan that found the chest is the first one or a later one, depending on the start frame
	// RW 0x895715: the AI's machine is kept (AI + 0x34) and a new one runs aiMoveToObject(chest, CMD_FROM_AI)
	CHECK(p.ai->hasTemporaryStateMachine());
	CHECK(p.ai->stateMachineOrNull() != own);
	CHECK(p.ai->stateMachine().goalObjectID() == chestId);
	CHECK(p.ai->lastCommandSource() == CMD_FROM_AI);
	// while the machine is kept, no further scan (RW 0x89578C) and no mood target (RW 0x668489)
	CHECK(p.ai->getNextMoodTarget(false, false) == nullptr);
	// the horde's members reach the chest: the first one that touches it collects it (RW 0x8C06B6 -> 0x8C158B -> 0x8C148B) and it is destroyed (RW 0x62BBAB)
	frames = 0;
	while (logic.findObjectByID(chestId) && !logic.findObjectByID(chestId)->isDestroyed() && frames < 3000)
	{
		p.g.live->stepFrame();
		++frames;
	}
	MESSAGE("the chest was collected after " << frames << " more frames");
	const Object *gone = logic.findObjectByID(chestId);
	REQUIRE((gone == nullptr || gone->isDestroyed()));
	// the chest paid the computer player (RW 0x8C148B: 50 .. 100 times MultiPlayMoneyMult; the AI spends its money meanwhile, so the payment is read from the income log)
	int paid = 0;
	for (const Economy::IncomeEvent &e : logic.economy().incomeLog())
	{
		paid += (e.object == chestId && e.playerIndex == p.computer->getPlayerIndex()) ? e.amount : 0;
	}
	CHECK(paid >= 50); // tolerance: the chest's draw is 50 .. 100 (RW 0x8C14B5) times MultiPlayMoneyMult (1.0 or more)
	CHECK(paid <= 1000); // tolerance: the same range's top with the largest multiplier
	(void)moneyBefore;
	// RW 0x895572: once the AI is idle (vslot 0x1B8) the kept machine is the machine again and the flag clears
	frames = 0;
	while (p.module->pickingUp() && frames < 3000)
	{
		p.g.live->stepFrame();
		++frames;
	}
	CHECK(!p.module->pickingUp());
	CHECK(!p.ai->hasTemporaryStateMachine());
	CHECK(p.ai->stateMachineOrNull() == own);
	// the chest is gone: the next scans find nothing and begin no pick-up
	const unsigned long long scansBefore = p.module->scans();
	for (int i = 0; i < 60; ++i)
	{
		p.g.live->stepFrame();
	}
	CHECK(p.module->scans() > scansBefore); // tolerance: a scan every 3 frames (frame > last + 2.5)
	CHECK(p.module->pickups() == 1);
	CHECK(p.module->blocked() == 0);
	CHECK(!p.ai->hasTemporaryStateMachine());
	// the horde takes its orders again: an order of the AI's own runs on its own machine
	const Coord3D goal = nearObject(*p.horde, -150.0f, 0.0f);
	p.ai->aiMoveToPosition(goal, CMD_FROM_AI);
	CHECK(p.ai->stateMachineOrNull() == own);
	CHECK(p.ai->currentStateId() == (unsigned)AI_MOVE_TO);
}

TEST_CASE("pickup1: the AI's temporary machine (RW 0x6630A9 / 0x6630D7): an order of the AI's own runs in it, a player's order ends it (aiDoCommand RW 0x66A91A); a human's horde scans nothing")
{
	OPENBFME_REQUIRE_START(s);
	PickupGame p;
	startWithHorde(*s, p, 5102, true);
	const AIStateMachine *own = p.ai->stateMachineOrNull();
	REQUIRE(own);
	// the human's horde: the module sleeps for ever (SkirmishAIOnly, RW 0x6A950B)
	std::string err;
	Object *humanHorde = p.g.live->createObject(kHorde, p.human->getPlayerIndex(), nearObject(*p.crate, 0.0f, 80.0f), 0.0f, &err);
	REQUIRE_MESSAGE(humanHorde, err);
	PickupStuffUpdate *humanModule = pickupOf(*humanHorde);
	REQUIRE(humanModule);
	// RW 0x895715's two calls, as a pick-up of a crate with a crate collide runtime would make them
	p.ai->beginTemporaryStateMachine();
	p.ai->aiMoveToObject(p.crate, CMD_FROM_AI);
	CHECK(p.ai->hasTemporaryStateMachine());
	CHECK(p.ai->stateMachineOrNull() != own);
	CHECK(p.ai->stateMachine().goalObjectID() == p.crate->getID());
	CHECK(p.ai->lastCommandSource() == CMD_FROM_AI);
	// a second begin keeps the machine already kept (RW 0x6630AC)
	const AIStateMachine *temporary = p.ai->stateMachineOrNull();
	p.ai->beginTemporaryStateMachine();
	CHECK(p.ai->stateMachineOrNull() == temporary);
	// while the machine is kept: no mood target (RW 0x668489) and no scan of the module (RW 0x89578C)
	CHECK(p.ai->getNextMoodTarget(false, false) == nullptr);
	for (int i = 0; i < 30; ++i)
	{
		p.g.live->stepFrame();
	}
	CHECK(p.module->blocked() == 0);
	CHECK(p.module->pickups() == 0);
	CHECK(humanModule->scans() == 0);
	CHECK(!humanHorde->getAIUpdateInterface()->hasTemporaryStateMachine());
	// an order of the AI's own goes to the temporary machine (RW 0x66A91A: only another source ends it)
	p.ai->aiMoveToPosition(nearObject(*p.crate, 0.0f, 0.0f), CMD_FROM_AI);
	CHECK(p.ai->hasTemporaryStateMachine());
	// a player's order: the temporary machine ends first (RW 0x6630D7: the kept machine is back), then the order runs on it
	const Coord3D goal = nearObject(*p.horde, -150.0f, 0.0f);
	p.ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	CHECK(!p.ai->hasTemporaryStateMachine());
	CHECK(p.ai->stateMachineOrNull() == own);
	CHECK(p.ai->currentStateId() == (unsigned)AI_MOVE_TO);
	// an end without a kept machine does nothing (RW 0x6630DD)
	p.ai->endTemporaryStateMachine();
	CHECK(p.ai->stateMachineOrNull() == own);
	// a crate that is ATTACHED (status 0x62) is not stuff (RW 0xC2E0B0): the scans find nothing
	p.crate->setStatus(0x62u, true);
	const unsigned long long scansBefore = p.module->scans(), blockedBefore = p.module->blocked();
	for (int i = 0; i < 60; ++i)
	{
		p.g.live->stepFrame();
	}
	CHECK(p.module->scans() > scansBefore); // tolerance: it scans every 3 frames (frame > last + 2.5)
	CHECK(p.module->blocked() == blockedBefore);
	CHECK(p.module->pickups() == 0);
}

// ---- the snapshot ------------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("pickup1: the module's fields (RW 0x8955A4) and the AI's kept machine (AI + 0x34) survive a save and load")
{
	OPENBFME_REQUIRE_START(s);
	PickupGame p;
	startWithHorde(*s, p, 5103, true);
	int frames = 0;
	while (p.module->pickups() == 0 && frames < 200)
	{
		p.g.live->stepFrame();
		++frames;
	}
	REQUIRE(p.module->lastScanFrame() != 0u);
	// the scan began a pick-up of the salvage crate (RW 0x895715's two calls; a computer's unit never collects it: AllowAIPickup No, RW 0x8C15AF)
	REQUIRE(p.module->pickups() == 1);
	REQUIRE(p.ai->hasTemporaryStateMachine());
	const bool savedPicking = p.module->pickingUp();
	// the AI: saved with its kept machine; a player's order ends the machine (the kept one is back); the load gives the temporary one back
	StateHasher aiBefore;
	p.ai->crc(aiBefore);
	XferSave aiSaved;
	p.ai->xferAIState(aiSaved);
	p.ai->aiIdle(CMD_FROM_PLAYER);
	CHECK(!p.ai->hasTemporaryStateMachine());
	{
		XferLoad load(aiSaved.data());
		p.ai->xferAIState(load);
	}
	CHECK(p.ai->hasTemporaryStateMachine());
	StateHasher aiAfter;
	p.ai->crc(aiAfter);
	CHECK(aiAfter.value() == aiBefore.value());
	p.ai->aiIdle(CMD_FROM_PLAYER);
	CHECK(!p.ai->hasTemporaryStateMachine());
	// the module: saved; the scans go on; the load gives the scan frame back
	StateHasher moduleBefore;
	p.module->crc(moduleBefore);
	XferSave moduleSaved;
	p.module->xfer(moduleSaved);
	const unsigned savedScan = p.module->lastScanFrame();
	for (int i = 0; i < 10; ++i)
	{
		p.g.live->stepFrame();
	}
	CHECK(p.module->lastScanFrame() != savedScan);
	{
		XferLoad load(moduleSaved.data());
		p.module->xfer(load);
	}
	CHECK(p.module->pickingUp() == savedPicking);
	CHECK(p.module->lastScanFrame() == savedScan);
	StateHasher moduleAfter;
	p.module->crc(moduleAfter);
	CHECK(moduleAfter.value() == moduleBefore.value());
	// a truncated module snapshot is refused
	std::vector<std::uint8_t> truncated = moduleSaved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(p.module->xfer(lt), XferException);
}
