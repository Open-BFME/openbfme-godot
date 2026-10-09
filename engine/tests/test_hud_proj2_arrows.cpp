// File name: shares the retail world of the HUD tests (hudtest::shared(); doctest runs the test_hud_* files in name order). PROJ-2 round 3 retail tests: the arrows of every
// faction's archers with and without the faction's arrow upgrade (Upgrade_GondorFireArrows and its kin). The projectile objects the volleys launch are compared with the retail
// weapon data (the ProjectileNugget pairs with RequiredUpgradeNames / ForbiddenUpgradeNames, RW 0x90D6F5: the object's upgrade mask OR its player's), and the drawables of
// the arrows and the archers are checked against the retail draw data (the arrow's W3DStreakDraw modules: texture, width, length, additive, colour; the archer's
// FireArowTip sub object shown by its SubObjectsUpgrade). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).

#include "doctest.h"
#include "HudTestUtil.h"
#include "PeImage.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LogicSnapshot.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

struct ArrowArena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<DrawableManager> drawables;
	GameLogic logic;
	std::unique_ptr<ClientEventRecorder> events;
	std::shared_ptr<const LogicSnapshot> lastSnapshot;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	ArrowArena(SharedWorld &s, const char *factionA, const char *factionB)
		: context(s.world->enterContext())
		, source(*s.mount->fs)
		, assets(source)
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.productionSettings() = s.world->productionSettings();
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		drawables = std::make_unique<DrawableManager>(assets, logic);
		events = std::make_unique<ClientEventRecorder>(logic);
		logic.setClientHooks(events.get());
	}
	~ArrowArena()
	{
		logic.reset();
		logic.setClientHooks(nullptr);
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const std::string &templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(side == 0 ? 0.0f : 3.14159265f);
		return o;
	}
	void clientFrame()
	{
		drawables->applyEvents(events->take());
		lastSnapshot = LogicSnapshot::build(logic, lastSnapshot.get(), false, 0);
		drawables->advance(1000.0 / 30.0);
		drawables->syncTransforms(*lastSnapshot, 0.5, true);
	}
};

struct Faction
{
	const char *faction, *horde, *upgrade, *enemyFaction, *victims;
	const char *normal, *upgraded; // the retail projectile templates (weapon.ini ProjectileNugget pairs)
};
const Faction kFactions[] = {
	{ "FactionMen", "GondorArcherHorde", "Upgrade_GondorFireArrows", "FactionMordor", "MordorFighterHorde", "GondorArcherArrow", "GondorArcherFireArrow" },
	{ "FactionMen", "RohanArcherHorde", "Upgrade_RohanFireArrows", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "GoodFactionFireArrow" },
	{ "FactionMen", "GondorRangerHorde", "Upgrade_GondorFireArrows", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "GoodFactionFireArrow" },
	{ "FactionElves", "ElvenLorienArcherHorde", "Upgrade_ElvenSilverthornArrows", "FactionIsengard", "IsengardFighterHorde", "GoodFactionArrow", "MirkwoodArcherSilverthornProjectile" },
	{ "FactionMordor", "MordorArcherHorde", "Upgrade_MordorFireArrows", "FactionElves", "ElvenLorienWarriorHorde", "EvilFactionArrow", "EvilFactionFireArrow" },
	{ "FactionMordor", "MordorHaradrimArcherHorde", "Upgrade_MordorFireArrows", "FactionElves", "ElvenLorienWarriorHorde", "EvilFactionArrow", "EvilFactionFireArrow" },
	{ "FactionIsengard", "IsengardUrukCrossbowHorde", "Upgrade_IsengardFireArrows", "FactionMen", "GondorFighterHorde", "GoodFactionArrow", "GoodFactionFireArrow" },
	{ "FactionAngmar", "AngmarDarkRangerHorde", "Upgrade_AngmarIceArrows", "FactionWild", "GoblinFighterHorde", "EvilFactionArrow", "EvilFactionIceArrow" },
	{ "FactionWild", "GoblinArcherHorde", "Upgrade_WildFireArrows", "FactionDwarves", "DwarvenPhalanxHorde", "EvilFactionArrow", "EvilFactionFireArrow" },
	{ "FactionDwarves", "DwarvenAxeThrowerHorde", "Upgrade_DwarvenForgedBlades", "FactionAngmar", "AngmarDarkDunedainHorde", "DwarvenAxe", "DwarvenAxeForgedBlade" },
};

struct StreakLook
{
	std::string texture;
	float width = 0.0f, length = 0.0f;
	bool additive = true;
	RGBColor color{};
};

struct Volley
{
	std::map<std::string, int> projectiles;               // template -> launched
	std::map<std::string, std::vector<StreakLook>> looks; // template -> its drawable's streak entries
	std::map<std::string, bool> drawableMade;             // template -> a drawable exists for it
	std::vector<std::string> archerHidden;                // the first archer member's hidden sub objects after the volleys
	bool archerFound = false;
};

Volley runVolley(const Faction &fc, bool upgraded, int frames, const char *weaponSetFlag = nullptr)
{
	SharedWorld &s = shared();
	ArrowArena a(s, fc.faction, fc.enemyFaction);
	if (upgraded)
	{
		a.player(0)->addCompletedUpgrade(fc.upgrade); // the player's upgrade (bought at the forge / the upgrade building in a game)
	}
	Object *h = a.place(fc.horde, 0, 500.0f, 500.0f);
	Object *v = a.place(fc.victims, 1, 700.0f, 500.0f);
	const ObjectID hordeId = h->getID();
	if (weaponSetFlag && h->getWeapons())
	{
		h->getWeapons()->setWeaponSetFlag(CombatNames::weaponSetBit(weaponSetFlag), true); // lane STEALTH-2: a hero whose bow is a toggled weapon set (Haldir)
	}
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	REQUIRE_MESSAGE(h->getAIUpdateInterface() != nullptr, "no AI update on " << fc.horde);
	REQUIRE_MESSAGE(h->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER), fc.horde);
	Volley out;
	std::set<ObjectID> seen;
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		a.clientFrame();
		if (f == 6 && !out.archerFound)
		{
			// the first archer of the horde while it is alive (a horde object ends when its members are dead: the Mordor fighters can win within the run):
			// its model draw's hidden sub objects (FireArowTip is shown by the upgrade's SubObjectsUpgrade)
			Object *alive = a.logic.findObjectByID(hordeId);
			if (const ContainModuleInterface *c = alive ? alive->getContain() : nullptr)
			{
				for (const Object *m : *c->getContainedItemsList())
				{
					if (const Drawable *d = a.drawables->findByObject(m->getID()))
					{
						for (const DrawEntry &e : d->entries())
						{
							if (e.draw)
							{
								out.archerHidden = e.draw->frame().hiddenSubObjects;
								out.archerFound = true;
								break;
							}
						}
					}
					break;
				}
			}
		}
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->isKindOf((unsigned)CombatNames::kinds().projectile) || seen.count(o->getID()))
			{
				continue;
			}
			bool bezier = false;
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				bezier = bezier || m->getProjectileUpdateInterface() != nullptr;
			}
			if (!bezier)
			{
				continue;
			}
			seen.insert(o->getID());
			const std::string name = o->getTemplate()->getName();
			++out.projectiles[name];
			if (const Drawable *d = a.drawables->findByObject(o->getID()))
			{
				out.drawableMade[name] = true;
				std::vector<StreakLook> looks;
				for (const DrawEntry &e : d->entries())
				{
					if (const W3DStreakDrawModuleData *sd = dynamic_cast<const W3DStreakDrawModuleData *>(e.data))
					{
						looks.push_back({ sd->m_texture, sd->m_width, sd->m_length, sd->m_additive, sd->m_color });
					}
				}
				out.looks[name] = looks;
			}
		}
	}
	return out;
}

std::string upper(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::toupper((unsigned char)c);
	}
	return s;
}
} // namespace

TEST_CASE("proj2 arrows: every faction's archers fire their retail projectile - the plain arrow without the arrow upgrade, the fire / ice / silverthorn arrow or forged axe with it")
{
	if (!hudtest::haveWorld("proj2 arrows"))
	{
		return;
	}
	for (const Faction &fc : kFactions)
	{
		INFO(std::string(fc.horde));
		for (bool upgraded : { false, true })
		{
			INFO("upgraded " << upgraded);
			const Volley r = runVolley(fc, upgraded, 120);
			std::string got;
			for (const auto &kv : r.projectiles)
			{
				got += kv.first + " x" + std::to_string(kv.second) + " ";
			}
			std::string tip = "-";
			if (r.archerFound)
			{
				tip = std::find(r.archerHidden.begin(), r.archerHidden.end(), "FIREAROWTIP") != r.archerHidden.end() ? "hidden" : "shown";
			}
			std::printf("  info: %s %s: %s| FireArowTip %s\n", fc.horde, upgraded ? "with the upgrade" : "plain", got.c_str(), tip.c_str());
			const char *want = upgraded ? fc.upgraded : fc.normal;
			CHECK_MESSAGE(r.projectiles.count(want) == 1, got);
			CHECK(r.projectiles.size() == 1);
			for (const auto &kv : r.looks)
			{
				for (const StreakLook &l : kv.second)
				{
					std::printf("  info:   %s streak %s width %.1f length %.1f additive %d colour (%.2f %.2f %.2f)\n", kv.first.c_str(), l.texture.c_str(), l.width, l.length,
						(int)l.additive, l.color.red, l.color.green, l.color.blue);
				}
			}
			// the drawable of the launched projectile carries the retail streak draws (goodfactionsubobjects.ini / evilfactionsubobjects.ini)
			CHECK(r.drawableMade.count(want) == 1);
			const std::string w = want;
			const auto looks = r.looks.count(w) ? r.looks.at(w) : std::vector<StreakLook>{};
			if (w == "DwarvenAxe" || w == "DwarvenAxeForgedBlade")
			{
				CHECK(looks.empty()); // a thrown axe is a model, no streak
				continue;
			}
			REQUIRE(!looks.empty());
			const StreakLook &head = looks.front();
			CHECK(head.length == 15.0f);
			if (!upgraded)
			{
				// the plain arrow: one blended (not additive) 2 wide streak of EXArrowStreak01
				CHECK(looks.size() == 1);
				CHECK(head.texture == "EXArrowStreak01.tga");
				CHECK_FALSE(head.additive);
				CHECK(head.width == 2.0f);
			}
			else
			{
				// fire / ice / silverthorn: an ADDITIVE 3 wide streak whose texture's flame only shows added ONE / ONE (S-1002); Gondor's, Rohan's, Isengard's and
				// the silverthorn arrows add the long faint EXLightStreaks2 streak
				CHECK(head.additive);
				CHECK(head.width == 3.0f);
				CHECK((head.texture == "EXArrowStreakFire.tga" || head.texture == "EXArrowStreakIce.tga"));
				if (looks.size() > 1)
				{
					CHECK(looks[1].texture == "EXLightStreaks2.tga");
					CHECK(looks[1].length == 50.0f);
					CHECK(looks[1].additive);
				}
			}
		}
	}
}

TEST_CASE("proj2 binary: the streak shader presets - Additive DAT 0xD9B2E4 is SRCBLEND ONE / DSTBLEND ONE, else DAT 0xD9B2F0 SRC_ALPHA / INV_SRC_ALPHA (RW 0x4CFA9A, S-1002)")
{
	CHECK(W3DStreakRenderStopLine().rfind("[S-1002] ", 0) == 0);
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("proj2 streak shader binary facts (RW_GAME_DAT unset)");
		return;
	}
	// RW 0x4CFB99: cmp byte [edi + 0x10], bl (Additive); mov ecx, [0xD9B2E4]; ...; jne +; mov ecx, [0xD9B2F0]
	std::vector<std::uint8_t> code;
	REQUIRE(pe->read(0x4CFB99, 0x17, &code));
	const std::uint8_t want[] = { 0x38, 0x5F, 0x10, 0x51, 0x8B, 0x0D, 0xE4, 0xB2, 0xD9, 0x00 };
	for (size_t i = 0; i < sizeof(want); ++i)
	{
		CHECK(code[i] == want[i]);
	}
	CHECK(code[0x11] == 0x8B);
	CHECK(code[0x12] == 0x0D);
	CHECK(pe->u32At(0x4CFBAC) == 0xD9B2F0u);
	// ZH ShaderClass bits: DSTBLEND bits 5..7, SRCBLEND bits 14..15 (ZH dst: 1 ONE, 5 ONE_MINUS_SRC_ALPHA; src: 1 ONE, 2 SRC_ALPHA); depth write off, no culling
	const std::uint32_t add = pe->u32At(0xD9B2E4), mix = pe->u32At(0xD9B2F0);
	CHECK(add == 0x115833u);
	CHECK(mix == 0x1198B3u);
	CHECK(((add >> 5) & 7u) == 1u);
	CHECK(((add >> 14) & 3u) == 1u);
	CHECK(((mix >> 5) & 7u) == 5u);
	CHECK(((mix >> 14) & 3u) == 2u);
	CHECK(((add >> 3) & 1u) == 0u);
	CHECK(((add >> 19) & 1u) == 0u);
}

namespace
{
// the hidden sub object names of a horde's members' model draws, counted
std::map<std::string, int> memberHides(LiveGame &game, Object *horde)
{
	std::map<std::string, int> out;
	if (const ContainModuleInterface *c = horde->getContain())
	{
		for (const Object *m : *c->getContainedItemsList())
		{
			std::string key;
			if (const Drawable *d = game.drawables().findByObject(m->getID()))
			{
				for (const DrawEntry &e : d->entries())
				{
					if (e.draw)
					{
						for (const std::string &h : e.draw->frame().hiddenSubObjects)
						{
							key += h + " ";
						}
					}
				}
			}
			++out[key.empty() ? "(none)" : key];
		}
	}
	return out;
}
} // namespace

TEST_CASE("proj2 arrows: in a live game the archers' flaming tip sprites hide at creation (OnCreated: FireArowTip / ARROWFIRE) and show once the arrow upgrade is bought")
{
	if (!hudtest::haveWorld("proj2 arrows live"))
	{
		return;
	}
	SharedWorld &sh = shared();
	auto contextScope = sh.world->enterContext();
	MapObjectOptions options;
	std::string err;
	REQUIRE_MESSAGE(MapObjectGameData::load(*sh.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(*sh.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapCreationHooks::load(*sh.mount->fs, options.creationScripts, &err), err);
	ArchiveW3DFileSource source(*sh.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*sh.world, *sh.mount->fs, assets, options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 7;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *owner = nullptr;
	for (int i = 0; i < game.logic().players().getPlayerCount() && !owner; ++i)
	{
		Player *p = game.logic().players().getNthPlayer(i);
		if (p && p->getDefaultTeam() && p != game.logic().players().getNeutralPlayer())
		{
			owner = p;
		}
	}
	REQUIRE(owner);
	struct Case
	{
		const char *horde, *upgrade, *tip;
	};
	const Case cases[] = { { "GondorArcherHorde", "Upgrade_GondorFireArrows", "FIREAROWTIP" }, { "MordorArcherHorde", "Upgrade_MordorFireArrows", "ARROWFIRE" } };
	for (const Case &c : cases)
	{
		INFO(std::string(c.horde));
		const ThingTemplate *t = sh.world->things().findTemplate(c.horde);
		REQUIRE(t);
		Object *h = game.logic().newObject(t, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(h);
		game.logic().runLogicFrame();
		game.refreshClient(1000.0 / 30.0, 0.0);
		// the idle state's BeginScript of the Mordor archer (CurDrawableShowSubObject("arrownock"), a plain show) on every member: retail's apply (RW 0x4BA74F)
		// re-applies the permanent records after the plain ones, so the OnCreated hide of the flame under the nock bone holds
		if (const ContainModuleInterface *cm = h->getContain())
		{
			for (const Object *m : *cm->getContainedItemsList())
			{
				if (Drawable *d = game.drawables().findByObject(m->getID()))
				{
					d->showSubObject("arrownock", true, false);
				}
			}
		}
		const std::map<std::string, int> before = memberHides(game, h);
		owner->addUpgrade(Player::resolveUpgrade(c.upgrade, true), Player::UPGRADE_STATUS_COMPLETE);
		game.logic().runLogicFrame();
		game.refreshClient(1000.0 / 30.0, 0.0);
		const std::map<std::string, int> after = memberHides(game, h);
		std::string b, a;
		for (const auto &kv : before)
		{
			b += kv.first + "x" + std::to_string(kv.second) + " ";
		}
		for (const auto &kv : after)
		{
			a += kv.first + "x" + std::to_string(kv.second) + " ";
		}
		std::printf("  info: %s members hide before the upgrade: %s| after: %s\n", c.horde, b.c_str(), a.c_str());
		REQUIRE(!before.empty());
		for (const auto &kv : before)
		{
			CHECK_MESSAGE(kv.first.find(c.tip) != std::string::npos, kv.first);
		}
		for (const auto &kv : after)
		{
			CHECK_MESSAGE(kv.first.find(c.tip) == std::string::npos, kv.first);
		}
	}
}

// lane STEALTH-2 (owner report): the hero archers fire their retail projectile with the plain arrow look (weapon.ini: LegolasBow, HaldirBow, FaramirBow,
// ElvenThranduilBow -> GoodFactionArrow (Elrond fights with ElrondSword: his bow is the garrison weapon set); IsenguardLurtzBow -> EvilFactionArrow, the only evil hero with a bow)
TEST_CASE("proj2 arrows: the hero archers (Legolas, Haldir, Faramir, Thranduil, Lurtz) fire their retail arrow with the retail streak")
{
	if (!hudtest::haveWorld("proj2 hero arrows"))
	{
		return;
	}
	struct Hero
	{
		Faction fc;
		const char *toggle;
	};
	const Hero heroes[] = {
		{ { "FactionElves", "ElvenLegolas", "", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "" }, nullptr },
		{ { "FactionMen", "RohanLegolas", "", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "" }, nullptr },
		{ { "FactionElves", "ElvenHaldir", "", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "" }, "WEAPONSET_TOGGLE_1" },
		{ { "FactionMen", "GondorFaramir", "", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "" }, nullptr },
		{ { "FactionElves", "ElvenThranduil", "", "FactionMordor", "MordorFighterHorde", "GoodFactionArrow", "" }, nullptr },
		{ { "FactionIsengard", "IsengardLurtz", "", "FactionMen", "GondorFighterHorde", "EvilFactionArrow", "" }, nullptr },
	};
	for (const Hero &h : heroes)
	{
		INFO(std::string(h.fc.horde));
		const Volley r = runVolley(h.fc, false, 120, h.toggle);
		std::string got;
		for (const auto &kv : r.projectiles)
		{
			got += kv.first + " x" + std::to_string(kv.second) + " ";
		}
		std::printf("  info: %s: %s\n", h.fc.horde, got.c_str());
		CHECK_MESSAGE(r.projectiles.count(h.fc.normal) == 1, got);
		CHECK(r.projectiles.size() == 1);
		CHECK(r.drawableMade.count(h.fc.normal) == 1);
		const auto looks = r.looks.count(h.fc.normal) ? r.looks.at(h.fc.normal) : std::vector<StreakLook>{};
		REQUIRE(looks.size() == 1);
		CHECK(looks[0].texture == "EXArrowStreak01.tga");
		CHECK_FALSE(looks[0].additive);
		CHECK(looks[0].width == 2.0f);
		CHECK(looks[0].length == 15.0f);
	}
}
