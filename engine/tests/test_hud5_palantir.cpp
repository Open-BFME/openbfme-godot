// OpenBFME retail tests, lane HUD-5 (the owner's report: the HUD's spell book button showed 1 point while the powers screen said 0). GPL-3.0.
//
// RW 0x6D5C0F (Palantir::Impl::UpdatePlayerStats; BFME2 decomp Palantir.cpp, tier A): APT:PlayerRank, the badge on the Palantir's PlayerMagic button, is "%d" of
// Player + 0x24 = PlayerScience + 0x1C, the science purchase points; the powers screen's APT:SpellStoreSpellPoints is the same points minus the pending cost
// (RW 0x822843). The owner's "1" was lotr.str's default text of APT:PlayerRank, never replaced. Both readouts are checked against the logic's number, at the start,
// after a purchase, and for a point earned while the screen is open. The tests SKIP loudly without the installs.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/PalantirCommandUI.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/SpellBookUI.h"

#include <algorithm>
#include <string>

using namespace hudtest;

namespace
{
struct PointsRig
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text;
	std::unique_ptr<InGameHud> hud;
	PointsRig(SharedWorld &s, const char *faction) : rig(s, "map mp fall back 4p", faction)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~PointsRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	int logicPoints() { return rig.local->science().getSciencePurchasePoints(); }
	std::string badge()
	{
		const std::string *t = hud->windows().aptText("APT:PlayerRank");
		return t ? *t : std::string("<none>");
	}
	std::string storePoints()
	{
		const std::string *t = hud->windows().aptText("APT:SpellStoreSpellPoints");
		return t ? *t : std::string("<none>");
	}
	bool statsCalled(const std::string &line)
	{
		const std::vector<std::string> &c = hud->palantir()->playerStatsCalls();
		return std::find(c.begin(), c.end(), line) != c.end();
	}
};
} // namespace

TEST_CASE("hud5 power points retail: the Palantir badge (APT:PlayerRank) and the powers screen show the logic's purchase points, before and after a purchase")
{
	if (!haveWorld("hud5 power points"))
	{
		return;
	}
	SharedWorld &s = shared();
	PointsRig b(s, "FactionMordor");
	b.frames(30);
	REQUIRE(b.hud->palantir());
	// rank 1 grants Rank.ini's SciencePurchasePointsGranted (5); the badge says so (it said lotr.str's "1" before)
	CHECK(b.logicPoints() == 5);
	CHECK(b.badge() == "5");
	std::string error;
	REQUIRE_MESSAGE(b.hud->openSpellStore(&error), error);
	b.frames(40);
	AptSpellStore *st = b.hud->spellStore();
	REQUIRE(st);
	CHECK(b.storePoints() == "5");
	// a point earned while the screen is open reaches both readouts on the next updates (both read the player live)
	b.rig.local->science().addSciencePurchasePoints(1);
	b.frames(2);
	CHECK(b.logicPoints() == 6);
	CHECK(b.storePoints() == "6");
	CHECK(b.badge() == "6");
	// a pending purchase lowers the screen's figure only (the proxy, RW 0x822843); the badge is the player's until the purchase is made
	int first = -1;
	for (int i = 0; i < SpellStoreModel::MAX_BUTTONS && first < 0; ++i)
	{
		if (AptSpellStore::stateName(st->buttonState(i)) == std::string("_active") && st->model().buttons().size() > 0)
		{
			for (const SpellStoreModel::Button &btn : st->model().buttons())
			{
				if (btn.index == i && btn.cost == 5)
				{
					first = i;
				}
			}
		}
	}
	REQUIRE(first >= 0);
	REQUIRE(st->click(first));
	b.frames(3);
	CHECK(b.storePoints() == "1");
	CHECK(b.badge() == "6");
	// ACCEPT: the purchase message runs in the logic; then the badge follows
	b.hud->closeSpellStore();
	b.frames(15);
	CHECK(b.logicPoints() == 1);
	CHECK(b.badge() == "1");
}

TEST_CASE("hud5 power points retail: the progress ring into the next rank and the level-up effect (RW 0x6D5C76 .. 0x6D5CB2, RW 0x8002BD)")
{
	if (!haveWorld("hud5 power progress"))
	{
		return;
	}
	SharedWorld &s = shared();
	PointsRig b(s, "FactionMen");
	b.frames(10);
	PlayerScience &sc = b.rig.local->science();
	const int thisNeed = sc.getSkillPointsLevelDown();
	const int nextNeed = sc.getSkillPointsLevelUp();
	REQUIRE(nextNeed > thisNeed);
	// at the start the progress is 1 (the cache's start value: nothing is sent)
	CHECK_FALSE(b.statsCalled("SetPlayerMagicProgress(1)"));
	// half way to rank 2
	const int half = thisNeed + (nextNeed - thisNeed) / 2;
	sc.addSkillPoints((float)(half - thisNeed), false);
	b.frames(2);
	const int expected = (int)((float)half - (float)thisNeed) * 100 / (nextNeed - thisNeed);
	CHECK(b.statsCalled("SetPlayerMagicProgress(" + std::to_string(expected) + ")"));
	// rank 2: the level-up effect, and the badge shows the granted points
	const int before = b.logicPoints();
	sc.addSkillPoints((float)(nextNeed - half), false);
	b.frames(2);
	CHECK(sc.getRankLevel() == 2);
	CHECK(b.statsCalled("PlayPlayerLevelUpEffect()"));
	CHECK(b.logicPoints() > before);
	CHECK(b.badge() == std::to_string(b.logicPoints()));
}

// lane HUD-5 (the owner asked for a hero's level / experience bar): retail's Palantir shows the selected unit's rank interface (RW 0x9305CE): ShowRankInterface,
// APT:HeroRank = APT:RankLabel with the rank (RW 0x92FACB) and the bar SetRankProgressBar(1 + trunc(100 x progress)) into the next level (RW 0x9D2437, RW 0x9304DB)
TEST_CASE("hud5 rank interface retail: a selected hero shows its rank and the experience bar into the next level; a building shows none")
{
	if (!haveWorld("hud5 rank interface"))
	{
		return;
	}
	SharedWorld &s = shared();
	PointsRig b(s, "FactionMen");
	b.frames(5);
	std::string error;
	Object *hero = b.rig.game->createObject("GondorBoromir", b.rig.local->getPlayerIndex(), Coord3D{ 1200.0f, 1200.0f, b.rig.game->logic().getGroundHeight(1200.0f, 1200.0f) }, 0.0f, &error);
	REQUIRE_MESSAGE(hero, error);
	b.frames(3);
	ExperienceTracker *xp = hero->getExperienceTracker();
	REQUIRE(xp);
	REQUIRE(xp->isTrainable());
	b.hud->input().ui().deselectAll(true);
	b.hud->input().ui().selectObject(hero->getID());
	b.frames(5);
	// a fresh tracker has no current level (+0x08 empty) until its experience reaches the first one: RW 0x9D2437 answers false, nothing is shown
	MESSAGE("fresh hero: level '" << xp->getLevelName() << "', interface " << b.hud->palantir()->rankInterfaceShown());
	xp->addExperiencePoints(1.0f, false, false, false);
	b.frames(5);
	REQUIRE(!xp->getLevelName().empty());
	const auto worldContext = s.world->enterContext(); // the level store of this world (TheExperienceLevelSystem is per thread)
	REQUIRE(TheExperienceLevelSystem);
	const ExperienceLevelTemplate *lvl = TheExperienceLevelSystem->findLevel(xp->getLevelName());
	MESSAGE("level " << xp->getLevelName() << " found " << (lvl != nullptr) << " rank " << (lvl ? lvl->m_rank : -1) << " trainable " << xp->isTrainable()
					 << " next " << (xp->upcomingLevel() ? xp->upcomingLevel()->m_name : std::string("-")));
	const PalantirCommandUI::RankInfo r0 = PalantirCommandUI::rankInfo(b.hud->input().context(), *hero);
	MESSAGE("hero rank " << r0.rank << " progress " << r0.progress << " type " << r0.type);
	CHECK(r0.type == 0);
	CHECK(r0.rank == xp->getRank());
	AptPalantir *pal = b.hud->palantir();
	REQUIRE(pal);
	CHECK(pal->rankInterfaceShown());
	auto called = [&](const std::string &line) {
		const std::vector<std::string> &c = pal->rankCalls();
		return std::find(c.begin(), c.end(), line) != c.end();
	};
	CHECK(called("ShowRankInterface()"));
	const std::string *text = b.hud->windows().aptText("APT:HeroRank");
	REQUIRE(text);
	MESSAGE("APT:HeroRank = " << *text);
	CHECK(text->find(std::to_string(xp->getRank())) != std::string::npos);
	CHECK(text->find("MISSING") == std::string::npos);
	// half way to the next level: the bar shows 1 + trunc(100 x progress)
	int nextRank = 0;
	const int need = xp->experienceForNextLevel(&nextRank);
	REQUIRE(need > 0);
	xp->addExperiencePoints((float)(need / 2), false, false, false);
	b.frames(3);
	const PalantirCommandUI::RankInfo r1 = PalantirCommandUI::rankInfo(b.hud->input().context(), *hero);
	CHECK(r1.progress > 0.0f);
	CHECK(r1.progress < 1.0f);
	CHECK(called("ShowRankProgress()"));
	CHECK(called("SetRankProgressBar(" + std::to_string(std::min(100, std::max(1, 1 + (int)(r1.progress * 100.0f)))) + ")"));
	// the next level: the text follows
	xp->gainLevels(1, false);
	b.frames(3);
	text = b.hud->windows().aptText("APT:HeroRank");
	REQUIRE(text);
	CHECK(text->find(std::to_string(xp->getRank())) != std::string::npos);
	// the last level: no next level, the bar goes (HideRankProgress)
	xp->gainLevels(20, false);
	b.frames(3);
	CHECK(PalantirCommandUI::rankInfo(b.hud->input().context(), *hero).progress == -1.0f);
	CHECK(called("HideRankProgress()"));
	// a hero and a farm selected together: their rank infos differ (RW 0x92F5D5), nothing is shown (RW 0x93069D)
	Object *farm = b.rig.game->createObject("GondorFarm", b.rig.local->getPlayerIndex(), Coord3D{ 1500.0f, 1200.0f, b.rig.game->logic().getGroundHeight(1500.0f, 1200.0f) }, 0.0f, &error);
	REQUIRE_MESSAGE(farm, error);
	b.frames(2);
	b.hud->input().ui().deselectAll(true);
	b.hud->input().ui().selectObject(hero->getID());
	b.hud->input().ui().selectObject(farm->getID());
	b.frames(5);
	CHECK(PalantirCommandUI::rankInfoFor(b.hud->input().context(), INVALID_ID, { hero->getID(), farm->getID() }).type == 2);
	CHECK_FALSE(pal->rankInterfaceShown());
	CHECK(called("HideRankInterface()"));
}
