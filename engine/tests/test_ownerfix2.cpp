// OpenBFME unit tests. GPL-3.0.
// lane OWNERFIX-2: the owner's playtest of 2026-10-17. The lobby handicap (RotWK GameSlot + 0x20: the combo RW 0x840B9D, the player's dict RW 0x627F8F,
// Player + 0x29C RW 0x6B09DE, the HandicapPercent<n> modifier of Object::initObject RW 0x6940DB) and the resource claim feedback (RW 0x75B561 / 0x75C02D /
// 0x75C1A9 and the claim decals of a selected resource building). Expected values come from the binary (byte checks), from the retail INI data (the
// HandicapPercent lists' HEALTH_MULT) or from a control object of the same template without the cause.

#include "doctest.h"

#include "PeImage.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/MpGameRules.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/TerrainResourceFeedback.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/TerrainResourceManager.h"
#include "GameNetwork/NetPacket.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using retailtest::PeImage;

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// Evendim, slot 0 an idle human (start 0), slot 1 an easy computer (start 1)
NewGameMessage evendim(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = -1;
	m.game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	return m;
}

struct Game
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void load(starttest::Shared &s, const NewGameMessage &m, Game &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
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

std::vector<std::string> listsOf(const Object &o)
{
	const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o.findModule("AttributeModifierPoolUpdate"));
	return pool ? pool->listNames() : std::vector<std::string>{};
}

bool hasList(const Object &o, const std::string &name)
{
	for (const std::string &n : listsOf(o))
	{
		if (n == name)
		{
			return true;
		}
	}
	return false;
}

Object *make(GameLogic &logic, const char *templateName, Player &owner, float x, float y)
{
	const ThingTemplate *tt = logic.things().findTemplate(templateName);
	REQUIRE_MESSAGE(tt != nullptr, templateName);
	Object *o = logic.newObject(tt, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o != nullptr, templateName);
	Coord3D p{ x, y, 0.0f };
	o->setPosition(&p);
	return o;
}
} // namespace

TEST_CASE("ownerfix2 binary facts: the Handicap combo (RW 0x840B9D), the player's handicap (RW 0x6B09DE) and the initObject modifier (RW 0x6940DB)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("ownerfix2 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// RW 0x840BB6 xor edi, edi (the first value 0); RW 0x840C22 sub edi, 5; RW 0x840C25 cmp edi, -0x5f; RW 0x840C28 jge: 0, -5 .. -95
	CHECK((pe->u32At(0x840BB6) & 0xFFFFu) == 0xFF33u);
	CHECK((pe->u32At(0x840C22) & 0xFFFFFFu) == 0x05EF83u);
	CHECK((pe->u32At(0x840C25) & 0xFFFFFFu) == 0xA1FF83u);
	CHECK((pe->u32At(0x840C28) & 0xFFu) == 0x7Du);
	// RW 0x840BE3 push 0xC53FB8: u"%d%%"
	CHECK(pe->u32At(0x840BE4) == 0xC53FB8u);
	CHECK(pe->u32At(0xC53FB8) == 0x00640025u); // '%' 'd'
	CHECK(pe->u32At(0xC53FBC) == 0x00250025u); // '%' '%'
	std::vector<int> retail;
	for (int v = 0; v >= -0x5F; v -= 5)
	{
		retail.push_back(v);
	}
	CHECK(retail.size() == 20);
	// the port's closed set is exactly the combo's values
	std::vector<int> port;
	for (int v = -200; v <= 200; ++v)
	{
		if (SkirmishGameSlot::handicapValid(v))
		{
			port.push_back(v);
		}
	}
	std::vector<int> retailSorted(retail.rbegin(), retail.rend());
	CHECK(port == retailSorted);
	// RW 0x6B09DE mov [esi + 0x29C], eax (Player::initFromDict stores playerHandicap); RW 0x6940DE mov eax, [eax + 0x29C]; RW 0x6940E4 neg eax;
	// RW 0x6940EC cmp ecx, 0x64; RW 0x6940F1 push 5 (the % 5 test)
	CHECK((pe->u32At(0x6B09DE) & 0xFFFFu) == 0x8689u);
	CHECK(pe->u32At(0x6B09E0) == 0x29Cu);
	CHECK((pe->u32At(0x6940DE) & 0xFFFFu) == 0x808Bu);
	CHECK(pe->u32At(0x6940E0) == 0x29Cu);
	CHECK((pe->u32At(0x6940E4) & 0xFFFFu) == 0xD8F7u);
	CHECK((pe->u32At(0x6940EC) & 0xFFFFFFu) == 0x64F983u);
	CHECK((pe->u32At(0x6940F1) & 0xFFFFu) == 0x056Au);
	// RW 0x694102 push 0xC121A4: "HandicapPercent%d"
	CHECK(pe->u32At(0x694103) == 0xC121A4u);
	CHECK(pe->cstring(0xC121A4) == "HandicapPercent%d");
	// RW 0x627F8F push [esi + 0x20] then the playerHandicap key holder (RW 0x627F92 mov ecx, 0xDA2F7C), CreateMPPlayers
	CHECK((pe->u32At(0x627F8F) & 0xFFFFFFu) == 0x2076FFu);
	CHECK(pe->u32At(0x627F93) == 0xDA2F7Cu);
}

TEST_CASE("ownerfix2: the slots' handicaps travel in the game setup (LAN options, new game message, replay); a value the combo never offers is refused")
{
	SkirmishGameInfo g;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"A";
	g.slots[0].handicap = -50;
	g.slots[1].state = SLOT_EASY_AI;
	g.slots[1].handicap = -95;
	NetByteWriter w;
	NetPacket::writeGameInfo(w, g);
	{
		NetByteReader r(w.data().data(), w.data().size());
		SkirmishGameInfo back;
		std::string error;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error), error);
		CHECK(back.slots[0].handicap == -50);
		CHECK(back.slots[1].handicap == -95);
		CHECK(back.slots[2].handicap == 0);
		CHECK(back.slots[0] == g.slots[0]);
	}
	// a handicap outside the combo's values is refused by name, for each kind of bad value
	for (int bad : { -3, 5, -100, -96, 1000 })
	{
		SkirmishGameInfo b = g;
		b.slots[1].handicap = bad;
		NetByteWriter bw;
		NetPacket::writeGameInfo(bw, b);
		NetByteReader r(bw.data().data(), bw.data().size());
		SkirmishGameInfo back;
		std::string error;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &error));
		CHECK(error.find("handicap") != std::string::npos);
	}
	// truncated inside the handicaps: refused (never a partial read)
	{
		std::vector<std::uint8_t> bytes = w.data();
		bytes.resize(bytes.size() - 2);
		NetByteReader r(bytes.data(), bytes.size());
		SkirmishGameInfo back;
		std::string error;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &error));
	}
	// a LAN announcement that ends after the profile (review: the LAN protocol version moved with the layout, so a protocol 3 announcement carries them):
	// truncated, as a full GameInfo is
	{
		std::vector<std::uint8_t> bytes = w.data();
		bytes.resize(bytes.size() - 4 - 4 * LEGACY_MAX_SLOTS); // lane CHEAT-1: the Cheat Mode i32 after the handicaps goes too
		NetByteReader r(bytes.data(), bytes.size());
		SkirmishGameInfo back;
		std::string error;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &error, true));
		CHECK(error == "truncated GameInfo");
		NetByteReader r2(bytes.data(), bytes.size());
		CHECK_FALSE(NetPacket::readGameInfo(r2, back, &error));
	}
	// a format 6 replay / save (GameInfo without handicaps) still reads, every handicap 0; the same bytes read as format 7 are refused
	{
		std::vector<std::uint8_t> bytes = w.data();
		bytes.resize(bytes.size() - 4 - 4 * LEGACY_MAX_SLOTS); // lane CHEAT-1: the Cheat Mode i32 after the handicaps goes too
		NetByteReader r(bytes.data(), bytes.size());
		SkirmishGameInfo back;
		std::string error;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error, false, false), error);
		CHECK(r.atEnd());
		CHECK(back.slots[1].handicap == 0);
	}
	// the replay header carries them (format 7)
	{
		ReplayHeader h;
		h.game.game = g;
		h.game.mode = NewGameMode::Skirmish;
		const std::string path = "ownerfix2_handicap.rep";
		ReplayWriter rw;
		std::string error;
		REQUIRE_MESSAGE(rw.open(path, h, &error), error);
		rw.close(0, 0);
		ReplayFile f;
		REQUIRE_MESSAGE(ReplayFile::load(path, f, &error), error);
		CHECK(f.header.game.game.slots[0].handicap == -50);
		CHECK(f.header.game.game.slots[1].handicap == -95);
		std::remove(path.c_str());
	}
}

TEST_CASE("ownerfix2 retail: a slot's handicap reaches its player (RW 0x6B09DE) and its new units and structures take HandicapPercent<n> with its HEALTH_MULT; the other player none")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m = evendim(*s, 41);
	m.game.slots[0].handicap = -50;
	{
	Game g;
	load(*s, m, g);
	GameLogic &logic = g.game->logic();
	Player *human = playerAtStart(logic, 0);
	Player *ai = playerAtStart(logic, 1);
	REQUIRE(human);
	REQUIRE(ai);
	CHECK(human->getHandicap() == -50);
	CHECK(ai->getHandicap() == 0);
	// the same infantry template for both players: the handicapped one has HandicapPercent50 and half the maximum health (attributemodifier.ini:
	// HandicapPercent50 Modifier = HEALTH_MULT 50%); the other one has no handicap list and the template's health
	Object *a = make(logic, "GondorFighter", *human, 1000.0f, 1000.0f);
	Object *b = make(logic, "GondorFighter", *ai, 1100.0f, 1000.0f);
	logic.runLogicFrame();
	CHECK(hasList(*a, "HandicapPercent50"));
	for (const std::string &n : listsOf(*b))
	{
		CHECK_FALSE(n.rfind("HandicapPercent", 0) == 0);
	}
	REQUIRE(a->getBodyModule());
	REQUIRE(b->getBodyModule());
	CHECK(a->getBodyModule()->getMaxHealth() == b->getBodyModule()->getMaxHealth() * 0.5f); // HEALTH_MULT 50%: an exact halving
	// a structure of the handicapped player too (STRUCTURE is in the KindOf test); its own castle was made at the start
	Object *farm = make(logic, "GondorFarm", *human, 1300.0f, 1300.0f);
	logic.runLogicFrame();
	CHECK(hasList(*farm, "HandicapPercent50"));
	}
	// a game without a handicap (the first game is gone: one live game per world, S-3808): no player has one, no object has the list
	NewGameMessage m0 = evendim(*s, 41);
	Game g0;
	load(*s, m0, g0);
	Player *h0 = playerAtStart(g0.game->logic(), 0);
	REQUIRE(h0);
	CHECK(h0->getHandicap() == 0);
	Object *c = make(g0.game->logic(), "GondorFighter", *h0, 1000.0f, 1000.0f);
	g0.game->logic().runLogicFrame();
	for (const std::string &n : listsOf(*c))
	{
		CHECK_FALSE(n.rfind("HandicapPercent", 0) == 0);
	}
}

TEST_CASE("ownerfix2: the claimable fraction's colour bands and the vertex weights are RotWK's (Impl::update, RW 0x75C1A9)")
{
	// Impl::update: > 0.75 green, >= 0.5 yellow, >= 0.25 red, else black
	CHECK(TerrainResourceClaimView::bandColor(1.0f) == 0xFF00FF00u);
	CHECK(TerrainResourceClaimView::bandColor(0.7501f) == 0xFF00FF00u);
	CHECK(TerrainResourceClaimView::bandColor(0.75f) == 0xFFFFFF00u);
	CHECK(TerrainResourceClaimView::bandColor(0.5f) == 0xFFFFFF00u);
	CHECK(TerrainResourceClaimView::bandColor(0.4999f) == 0xFFFF0000u);
	CHECK(TerrainResourceClaimView::bandColor(0.25f) == 0xFFFF0000u);
	CHECK(TerrainResourceClaimView::bandColor(0.2499f) == 0xFF000000u);
	CHECK(TerrainResourceClaimView::bandColor(0.0f) == 0xFF000000u);
}

TEST_CASE("ownerfix2 retail: a farm's placement shows its claimable share (RW 0x75B561) and the selected farm its claim decals; overlapping farms share the ground")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m = evendim(*s, 43);
	Game g;
	load(*s, m, g);
	GameLogic &logic = g.game->logic();
	Player *human = playerAtStart(logic, 0);
	Player *ai = playerAtStart(logic, 1);
	REQUIRE(human);
	REQUIRE(ai);
	REQUIRE(logic.shroud());
	const int me = human->getPlayerIndex();
	logic.shroud()->revealMapForPlayerPermanently(me); // every cell CLEAR for the local player: the shroud clauses are tested apart below
	logic.runLogicFrame();
	const TerrainResourceManager &grid = logic.economy().resources();
	REQUIRE(grid.hasGrid());
	const ThingTemplate *farmT = logic.things().findTemplate("GondorFarm");
	REQUIRE(farmT);
	const TerrainResourceBehaviorModuleData *data = TerrainResourceClaimView::resourceDataOf(*farmT);
	REQUIRE(data);
	const float radius = data->m_radius;
	REQUIRE(radius == 300.0f); // farm.ini: Radius = GONDOR_FARM_MONEY_RANGE (300)
	const int r = (int)std::ceil(radius / grid.cellSize());
	// a block of free cells (2r + 3 square) that no claim reaches: its centre's disc is whole
	auto freeBlock = [&](int skipX, int skipY, int &ox, int &oy) {
		const int n = 2 * r + 3;
		for (int y = 1; y + n < grid.height(); y += 2)
		{
			for (int x = 1; x + n < grid.width(); x += 2)
			{
				if (std::abs(x - skipX) < 3 * n && std::abs(y - skipY) < 3 * n)
				{
					continue;
				}
				bool ok = true;
				for (int j = 0; j < n && ok; ++j)
				{
					for (int i = 0; i < n && ok; ++i)
					{
						const TerrainResourceManager::Cell *c = grid.cellAt(x + i, y + j);
						ok = c && c->state == TerrainResourceManager::CELL_FREE;
					}
				}
				if (ok)
				{
					ox = x + n / 2;
					oy = y + n / 2;
					return true;
				}
			}
		}
		return false;
	};
	int cx = 0, cy = 0;
	REQUIRE(freeBlock(-10000, -10000, cx, cy));
	const float px = (float)cx * grid.cellSize() + grid.cellSize() * 0.5f, py = (float)cy * grid.cellSize() + grid.cellSize() * 0.5f;
	// free ground: the whole disc is claimable
	CHECK(TerrainResourceClaimView::claimableFraction(logic, px, py, radius, data->m_highPriority, me) == 1.0f);
	TerrainResourceFeedback fb;
	fb.updatePlacement(logic, me, farmT, Coord3D{ px, py, 0.0f }, u"%d%%");
	CHECK(fb.placement().active);
	CHECK(fb.placement().percent == 100);
	CHECK(fb.placement().text == u"100%");
	CHECK(fb.placement().color == 0xFF00FF00u);
	CHECK(fb.placement().decalSize == radius * 2.0f);
	CHECK(std::fmod(fb.placement().decalCenter.x, grid.cellSize()) == 0.0f);
	CHECK(fb.placement().decalOpacity == 0x80);
	// every vertex of free ground has the full opacity (weight 12)
	for (std::uint8_t a : fb.placement().maskAlpha)
	{
		CHECK(a == 0x80);
	}
	// the local player's finished farm there: its disc is owned by a visible object of the player: nothing claimable at the same spot, half way it shares
	Object *farm = make(logic, "GondorFarm", *human, px, py);
	farm->friend_onBuildComplete();
	logic.runLogicFrame();
	CHECK(TerrainResourceClaimView::claimableFraction(logic, px, py, radius, data->m_highPriority, me) == 0.0f);
	fb.updatePlacement(logic, me, farmT, Coord3D{ px, py, 0.0f }, u"%d%%");
	CHECK(fb.placement().percent == 0);
	CHECK(fb.placement().color == 0xFF000000u);
	for (std::uint8_t a : fb.placement().maskAlpha)
	{
		(void)a; // vertices outside the farm's disc stay visible; the centre is checked below
	}
	{
		// the centre vertex of the farm: its own cell is owned: alpha 0
		const int vx = (int)(px / TerrainResourceFeedback::kVertexSpacing), vy = (int)(py / TerrainResourceFeedback::kVertexSpacing);
		CHECK((TerrainResourceClaimView::vertexColor(logic, 0x80FFFFFFu, vx, vy, me) >> 24) == 0u);
	}
	const float half = TerrainResourceClaimView::claimableFraction(logic, px + radius, py, radius, data->m_highPriority, me);
	CHECK(half > 0.25f); // tolerance: a disc half over the farm's own ground shares it; the exact share is the raster's (checked cell by cell below)
	CHECK(half < 0.75f); // tolerance: as above
	// the vertex weights along the farm's edge are the table of RW 0x75C1A9 (an independent reading of the 13 cells)
	{
		auto ok = [&](int x, int y) { return TerrainResourceClaimView::claimable(logic, x, y, 0u, INVALID_ID, false, me); };
		int checked = 0, partial = 0;
		const int vy = (int)(py / TerrainResourceFeedback::kVertexSpacing);
		for (int vx = (int)((px - 2 * radius) / 10.0f); vx < (int)((px + 2 * radius) / 10.0f); ++vx)
		{
			const std::uint32_t got = TerrainResourceClaimView::vertexColor(logic, 0xC0123456u, vx, vy, me);
			int w = 0;
			if (ok(vx / 2, vy / 2))
			{
				w = 12;
				const int d[12][3] = { { 1, 0, 4 }, { 0, 1, 4 }, { -1, 0, 4 }, { 0, -1, 4 }, { 1, 1, 2 }, { -1, -1, 2 }, { -1, 1, 3 }, { 1, -1, 3 }, { 2, 0, 1 }, { -2, 0, 1 },
					{ 0, 2, 1 }, { 0, -2, 1 } };
				for (const auto &e : d)
				{
					w -= ok((vx + e[0]) / 2, (vy + e[1]) / 2) ? 0 : e[2];
				}
			}
			const std::uint32_t want = w <= 0 ? 0u : (0x00123456u | ((std::uint32_t)(int)(192.0f * ((float)w / 12.0f)) << 24));
			CHECK(got == want);
			++checked;
			partial += (w > 0 && w < 12) ? 1 : 0;
		}
		CHECK(checked > 0); // tolerance: the scan ran (its count is the map geometry's)
		CHECK(partial > 0); // tolerance: the edge was crossed (some vertex had a partial weight)
	}
	// the farm earns less with an overlapping second farm (the simulation half): the first farm's share drops below 1
	// the selected farm's claim decals: the local player's visible claimants (the farm; every building's dead spot is Visible = No), not the enemy's
	Object *enemyFarm = nullptr;
	{
		int ex = 0, ey = 0;
		REQUIRE(freeBlock(cx, cy, ex, ey));
		enemyFarm = make(logic, "MordorSlaughterHouse", *ai, (float)ex * grid.cellSize() + 10.0f, (float)ey * grid.cellSize() + 10.0f);
		enemyFarm->friend_onBuildComplete();
		logic.runLogicFrame();
	}
	fb.updateSelection(logic, me, 1);
	REQUIRE(fb.claimedDecals().size() >= 1); // tolerance: the castle pieces of a Men start may hold Visible claims too; every decal is checked below
	bool sawFarm = false;
	for (const ResourceClaimDecal &d : fb.claimedDecals())
	{
		const Object *o = logic.findObjectByID(d.id);
		REQUIRE(o);
		CHECK(o->getControllingPlayer() == human);
		CHECK(d.id != enemyFarm->getID());
		CHECK(d.color == 0xFF6464C8u);
		CHECK(d.opacity == 0x80);
		if (d.id == farm->getID())
		{
			sawFarm = true;
			CHECK(d.size == radius * 2.0f);
			CHECK(d.position.x == px);
		}
	}
	CHECK(sawFarm);
	fb.updateSelection(logic, me, 0);
	CHECK(fb.claimedDecals().empty());
	// a template without a TerrainResourceBehavior (a barracks), or none: no placement feedback
	fb.updatePlacement(logic, me, logic.things().findTemplate("GondorBarracks"), Coord3D{ px, py, 0.0f }, u"%d%%");
	CHECK_FALSE(fb.placement().active);
	fb.updatePlacement(logic, me, nullptr, Coord3D{ px, py, 0.0f }, u"%d%%");
	CHECK_FALSE(fb.placement().active);
	// the shroud clauses (RW 0x75C02D): the enemy's farm owned ground is claimable for a player who sees it fogged (the start's revealMapForPlayer state),
	// not once it is clear
	const float efx = enemyFarm->getPosition()->x, efy = enemyFarm->getPosition()->y;
	CHECK(TerrainResourceClaimView::claimableFraction(logic, efx, efy, radius, data->m_highPriority, me) == 0.0f);
	logic.shroud()->undoRevealMapForPlayerPermanently(me);
	logic.shroud()->revealMapForPlayer(me);
	logic.runLogicFrame();
	const ShroudManager &sh = *logic.shroud();
	if (sh.getStatusAt(me, efx, efy) == CELLSHROUD_FOGGED)
	{
		CHECK(TerrainResourceClaimView::claimableFraction(logic, efx, efy, radius, data->m_highPriority, me) == 1.0f);
	}
	else
	{
		MESSAGE("the enemy farm is in the local player's sight (status " << (int)sh.getStatusAt(me, efx, efy) << "): the fog clause is not reached here");
	}
}

#include "GameClient/DrawableIconUI.h"
#include "GameClient/FloatingText.h"

#include "GameClient/CommandButtonHelp.h"

TEST_CASE("floating text: numeric settings accept INI math and report missing or malformed values")
{
	const std::string ini = "InGameUI\n FloatingTextTimeOut = #MULTIPLY( 500 2 )\n FloatingTextMoveUpSpeed = #MULTIPLY( 15 2 )\n FloatingTextVanishRate = #ADD( 1 2 )\nEnd\n";
	unsigned timeout = 0;
	float speed = 0, vanish = 0;
	std::string error;
	REQUIRE(loadInGameUiUnsigned(nullptr, &ini, "FloatingTextTimeOut", timeout, &error));
	REQUIRE(loadInGameUiReal(nullptr, &ini, "FloatingTextMoveUpSpeed", speed, &error));
	REQUIRE(loadInGameUiReal(nullptr, &ini, "FloatingTextVanishRate", vanish, &error));
	CHECK(timeout == 1000);
	CHECK(speed == 30.0f);
	CHECK(vanish == 3.0f);
	const std::string missing = "InGameUI\nEnd\n";
	CHECK_FALSE(loadInGameUiReal(nullptr, &missing, "FloatingTextVanishRate", vanish, &error));
	CHECK(error.find("FloatingTextVanishRate") != std::string::npos);
	const std::string malformed = "InGameUI\n FloatingTextVanishRate = bad\nEnd\n";
	CHECK_FALSE(loadInGameUiReal(nullptr, &malformed, "FloatingTextVanishRate", vanish, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("floating text: unavailable settings disable creation and discard existing labels")
{
	FloatingTextList list;
	for (unsigned f = 0; f < 100; ++f) list.add("+25", Coord3D{}, 0xE6FFFFFFu, f);
	CHECK(list.records().empty());
	CHECK(list.added() == 0);
	FloatingTextSettings settings;
	settings.vanishRate = 3.0f;
	list.setSettings(settings);
	list.add("+25", Coord3D{}, 0xE6FFFFFFu, 100);
	REQUIRE(list.records().size() == 1);
	list.disable();
	for (unsigned f = 101; f < 200; ++f) list.add("+25", Coord3D{}, 0xE6FFFFFFu, f);
	CHECK(list.records().empty());
	CHECK(list.added() == 1);
	list.setSettings(settings);
	list.add("+25", Coord3D{}, 0xE6FFFFFFu, 200);
	CHECK(list.records().size() == 1);
}

TEST_CASE("floating text: GUI:AddCash records hold for FloatingTextTimeOut, rise at FloatingTextMoveUpSpeed and fade at FloatingTextVanishRate")
{
	FloatingTextSettings s;
	s.timeOutMs = 1000; // RotWK InGameUI.ini
	s.moveUpSpeed = 30.0f;
	s.vanishRate = 3.0f;
	FloatingTextList list;
	list.setSettings(s);
	list.update(100); // the HUD steps the list every frame from the game's start; the first call sets the clock
	list.add("+10", Coord3D{ 100, 200, 30 }, 0xE6112233u, 100);
	REQUIRE(list.records().size() == 1);
	CHECK(list.records().front().frameTimeOut == 130); // 1000 ms * 0.03 client frames
	auto project = [](const Coord3D &p, float &x, float &y) { x = p.x; y = p.y; return true; };
	auto visible = [](const Coord3D &) { return true; };
	for (unsigned f = 101; f <= 130; ++f)
	{
		list.update(f);
		list.update(f); // a second call in the same client frame does nothing
	}
	REQUIRE(list.records().size() == 1);
	CHECK(list.records().front().frameCount == 30);
	CHECK(list.records().front().color == 0xE6112233u); // no fade until the time out
	std::vector<IconUIOp> ops;
	list.ops(project, visible, ops);
	REQUIRE(ops.size() == 1);
	CHECK(ops[0].kind == IconUIOp::TEXT);
	CHECK(ops[0].text == "+10");
	CHECK(ops[0].x == 100.0f);
	CHECK(ops[0].y == 170.0f); // 200 - 30 frames * 30 / 30
	CHECK(ops[0].dropColor == 0xE6000000u);
	// past the time out the alpha drops by (int)(3 / 30 * frames past it), until the record goes
	unsigned f = 131;
	for (; f < 400 && !list.records().empty(); ++f)
	{
		list.update(f);
	}
	CHECK(list.records().empty());
	CHECK(f == 203); // alpha 230 loses (int)(0.1 * frames past 130) a frame: 0 after the update of frame 202
	ops.clear();
	list.add("+5", Coord3D{}, 0xE6FFFFFFu, f);
	list.ops(project, [](const Coord3D &) { return false; }, ops);
	CHECK(ops.empty()); // a cell that is not CLEAR for the viewer shows nothing
}

TEST_CASE("floating text: a render frame spanning several client frames steps every one of them (rise and fade follow the client clock)")
{
	FloatingTextSettings s;
	s.timeOutMs = 1000;
	s.moveUpSpeed = 30.0f;
	s.vanishRate = 3.0f;
	FloatingTextList each, jumps;
	each.setSettings(s);
	jumps.setSettings(s);
	each.update(100);
	jumps.update(100);
	each.add("+10", Coord3D{}, 0xE6112233u, 100);
	jumps.add("+10", Coord3D{}, 0xE6112233u, 100);
	// a second text added mid-way (frame 151) steps only on the frames after it, also inside a jump
	for (unsigned f = 101; f <= 260; ++f)
	{
		each.update(f);
		if (f == 151)
		{
			each.add("+5", Coord3D{}, 0xE6445566u, 151);
		}
		if (f % 3 == 0)
		{
			jumps.update(f); // a render frame every third client frame
		}
		if (f == 151)
		{
			jumps.update(151);
			jumps.add("+5", Coord3D{}, 0xE6445566u, 151);
		}
		if (f % 3 == 0 || f == 151)
		{
			REQUIRE(each.records().size() == jumps.records().size());
			auto a = each.records().begin();
			auto b = jumps.records().begin();
			for (; a != each.records().end(); ++a, ++b)
			{
				CHECK(a->frameCount == b->frameCount);
				CHECK(a->color == b->color);
			}
		}
	}
	CHECK(each.records().empty()); // both texts faded: the first after frame 202, the second after frame 253
	CHECK(jumps.records().empty());
}

TEST_CASE("floating text: the runtime stop names the port's inferences (docs/STOPS.md S-283)")
{
	const std::vector<std::string> stops = FloatingTextList::acceptanceStops();
	REQUIRE(stops.size() == 1);
	CHECK(stops[0].rfind("[S-283] floating text:", 0) == 0);
	for (const char *inference : { "FloatingTextTimeOut is milliseconds", "30 client frames a second", "caption font" })
	{
		CHECK_MESSAGE(stops[0].find(inference) != std::string::npos, inference);
	}
}
