// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: BoredUpdate (RW 0x899F9D) and the ActionManager's at-object test it asks (RW 0x82D925). Binary facts read from the RotWK binary
// (RW_GAME_DAT), the retail data (ROTWK_INSTALL) and live objects in a started skirmish. The expectations are retail's rules (each check names its address).

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BoredUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/WeaponSetToggle.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
[[maybe_unused]] float floatAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// the lobby's result: slot 0 a human (Men), slot 1 an easy computer (Mordor) on Evendim, start positions 0 and 4 (test_pickup_modules.cpp's game)
bool startGame(starttest::Shared &s, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, "FactionMen");
	b.tmpl = buildtest::templateIndex(s, "FactionMordor");
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

Coord3D besideFortress(GameLogic &logic, const Player *owner, float dx, float dy)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName("COMMANDCENTER") && !x->isEffectivelyDead())
		{
			return Coord3D{ x->getPosition()->x + dx, x->getPosition()->y + dy, x->getPosition()->z };
		}
	}
	FAIL("no fortress");
	return Coord3D{};
}

BoredUpdate *boredOf(Object &o)
{
	return dynamic_cast<BoredUpdate *>(o.findModule("BoredUpdate"));
}

// steps until `m` has scanned `n` times (at most ScanDelayTime + 5 frames per scan)
void untilScans(buildtest::Game &g, BoredUpdate &m, unsigned long long n)
{
	int frames = 0;
	while (m.scans() < n && frames < (int)m.data()->m_scanDelayTime + 5)
	{
		g.live->stepFrame();
		++frames;
	}
	REQUIRE(m.scans() == n);
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("bored1: BoredUpdate's binary facts: the field table (RW 0xC65CC0), the update (RW 0xC65BF8 slot 0) and the shared xfer (RW 0xC65C04 slot 3)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("bored1 binary facts (set RW_GAME_DAT)");
		return;
	}
	CHECK(floatAt(*pe, 0xBD9E90) == 300.0f); // ScanDistance (RW 0x899EBF)
	const std::uint32_t procs[] = { 0x73A429u, 0x42ED00u, 0x76392Fu, 0x42E558u, 0x73B22Fu };
	const std::uint32_t offsets[] = { 0x08u, 0x0Cu, 0x10u, 0x14u, 0x18u };
	for (int i = 0; i < 5; ++i)
	{
		CHECK(pe->u32At(0xC65CC0u + 16u * i + 4u) == procs[i]);
		CHECK(pe->u32At(0xC65CC0u + 16u * i + 12u) == offsets[i]);
	}
	CHECK(pe->u32At(0xC65CC0u + 16u * 5u) == 0u);
	CHECK(pe->u32At(0xC65BF8u) == 0x899F9Du);
	CHECK(pe->u32At(0xC65C04u + 12u) == 0x8B0125u);
}

TEST_CASE("bored1: the 2.01 data: the Mountain Troll is bored every 40 s within 70 of a troll friend (SpecialAbilityMountainTrollBored, SPECIAL_MTTROLL_BORED)")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "BoredUpdate") == unported.end());
	const ThingTemplate *tt = s->world->things().findTemplate("MordorMountainTroll");
	REQUIRE(tt);
	const BoredUpdateModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
	{
		if (n.name == "BoredUpdate")
		{
			d = dynamic_cast<const BoredUpdateModuleData *>(n.data.get());
		}
	}
	REQUIRE(d);
	CHECK(d->m_scanDistance == 70.0f);
	CHECK(d->m_scanDelayTime == 200u); // 40000 ms (parseDurationUnsignedInt RW 0x73A429)
	CHECK(ObjectFilterMatch::isValid(&d->m_boredFilter));
	REQUIRE(d->m_specialPowerTemplate);
	CHECK(d->m_specialPowerTemplate->getName() == "SpecialAbilityMountainTrollBored");
	CHECK(d->m_specialPowerTemplate->getFinalOverride()->getSpecialPowerType() == 0x40); // SPECIAL_MTTROLL_BORED (RW 0xDA5C90 index 64)
	int found = 0;
	for (const std::string &l : s->world->acceptanceStops())
	{
		found += l.rfind("[S-7161] BoredUpdate", 0) == 0 ? 1 : 0;
		found += l.rfind("[S-7140] ActionManager", 0) == 0 ? 1 : 0;
	}
	CHECK(found == 2);
}

// ---- the live module ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("bored1: an idle Mountain Troll clubs its troll friend (RW 0x82D925 / 0x68E67A), not an orc friend nor nobody; the countdown survives a save")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGame(*s, 7401, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	const Player *computer = playerAtStart(logic, 4);
	REQUIRE(human);
	REQUIRE(computer);
	// the human's trolls (no computer player's AI special powers toggling them)
	const Coord3D at = besideFortress(logic, human, 500.0f, 0.0f);
	Object *a = g.live->createObject("MordorMountainTroll", human->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(a, err);
	BoredUpdate *ma = boredOf(*a);
	REQUIRE(ma);
	CHECK(ma->countdown() <= ma->data()->m_scanDelayTime); // tolerance: RW 0x899F1F starts it at GameLogicRandomValue(0, ScanDelayTime)
	// alone: the scan finds nobody (RW 0xA39090), no power
	untilScans(g, *ma, 1);
	CHECK(ma->countdown() == ma->data()->m_scanDelayTime);
	CHECK(ma->uses() == 0);
	// an orc of its own beside it: an ally, but BoredFilter (NONE +MordorMountainTroll +MordorCaveTroll) refuses it (RW 0xBE4CC8), no power
	Object *orc = g.live->createObject("MordorFighter", human->getPlayerIndex(), Coord3D{ at.x + 30.0f, at.y, at.z }, 0.0f, &err);
	REQUIRE_MESSAGE(orc, err);
	untilScans(g, *ma, 2);
	CHECK(ma->uses() == 0);
	CHECK(!ActionManager::canDoSpecialPowerAtObject(*a, nullptr, CMD_FROM_AI, ma->data()->m_specialPowerTemplate, 2u, true)); // no target
	CHECK(!ActionManager::canDoSpecialPowerAtObject(*a, orc, CMD_FROM_AI, nullptr, 2u, true));                                 // no power
	// a troll friend within 70: one of the two clubs the other (each has the module) at its next scan
	Object *b = g.live->createObject("MordorMountainTroll", human->getPlayerIndex(), Coord3D{ at.x, at.y + 30.0f, at.z }, 0.0f, &err);
	REQUIRE_MESSAGE(b, err);
	BoredUpdate *mb = boredOf(*b);
	REQUIRE(mb);
	// a new Mountain Troll switches to rock throwing (DoCommandUpgrade, Upgrade_SwitchToRockThrowing: WEAPONSET_TOGGLE_1), which the bored ability's
	// RejectedConditions WEAPON_TOGGLE refuses (RW 0x851551: "Can't clock your buddy troll when holding a rock")
	CHECK(a->testModelCondition(0x12D));
	CHECK(!ActionManager::canDoSpecialPowerAtObject(*a, b, CMD_FROM_AI, ma->data()->m_specialPowerTemplate, 2u, true));
	// the player toggles both back to their clubs (MSG_WEAPONSET_TOGGLE: clearWeaponSetFlag RW 0x691106)
	WeaponSetToggle::setObjectWeaponSetFlag(*a, WeaponSetToggle::WEAPONSET_TOGGLE_1, false);
	WeaponSetToggle::setObjectWeaponSetFlag(*b, WeaponSetToggle::WEAPONSET_TOGGLE_1, false);
	CHECK(!a->testModelCondition(0x12D));
	g.live->stepFrame(); // the new troll's shroud status for its human owner is computed by the frame's look (RW 0x82C167 refuses a fogged target)
	{
		// the at-object test's inputs (RW 0x82D925): the module, its readiness and requirements, the special power update's answer, the relationship
		const SpecialPowerTemplate *power = ma->data()->m_specialPowerTemplate;
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*a, power);
		REQUIRE(sp);
		bool driverAllows = true;
		int drivers = 0;
		for (const auto &mod : a->modules())
		{
			if (const SpecialPowerUpdateInterface *u = dynamic_cast<const SpecialPowerUpdateInterface *>(mod.get()))
			{
				if (u->drivesSpecialPower(power))
				{
					++drivers;
					driverAllows = u->allowsSpecialPowerUse();
					break;
				}
			}
		}
		MESSAGE("bored power: ready " << sp->getPercentReady() << ", requirements " << sp->requirementsMet() << ", drivers " << drivers << " allow " << driverAllows
		                              << ", relationship " << (int)a->getRelationship(*b) << ", unit cost " << power->m_unitCost << "; MOUNTED "
		                              << a->testModelCondition(0xD6) << " WEAPONSET_TOGGLE_1 " << a->testModelCondition(0x12D) << " MOVING " << a->testModelCondition(0x3D));
	}
	CHECK(ActionManager::canDoSpecialPowerAtObject(*a, b, CMD_FROM_AI, ma->data()->m_specialPowerTemplate, 2u, true)); // type 0x40: yes (RW 0x82DAE9)
	int frames = 0;
	while (ma->uses() + mb->uses() == 0 && frames < 2 * (int)ma->data()->m_scanDelayTime + 10)
	{
		g.live->stepFrame();
		++frames;
	}
	MESSAGE("the first club came after " << frames << " frames");
	CHECK(ma->uses() + mb->uses() == 1);
	// the snapshot: the countdown survives a save and load
	StateHasher h1;
	ma->crc(h1);
	XferSave saved;
	ma->xfer(saved);
	const unsigned savedCountdown = ma->countdown();
	g.live->stepFrame();
	g.live->stepFrame();
	CHECK(ma->countdown() != savedCountdown); // tolerance: two frames on, the countdown has moved (down by two, or reloaded at 0)
	{
		XferLoad load(saved.data());
		ma->xfer(load);
	}
	CHECK(ma->countdown() == savedCountdown);
	StateHasher h2;
	ma->crc(h2);
	CHECK(h2.value() == h1.value());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(ma->xfer(lt), XferException);
}
