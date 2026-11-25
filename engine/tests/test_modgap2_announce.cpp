// Both editions: RW 0x888439 / 0x888453 and BFME2 0x8851AD / 0x8851C7.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AnnounceBirthAndDeathBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

namespace
{
const char kAnnounceObjects[] =
	"Object TestHeroAnnounce\n"
	" KindOf = HERO INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n"
	"  MaxHealth = 100\n"
	"  InitialHealth = 100\n"
	" End\n"
	" Behavior = AIUpdateInterface AI\n"
	" End\n"
	" Behavior = AnnounceBirthAndDeathBehavior AnnounceTag\n"
	" End\n"
	"End\n"
	"Object TestGruntAnnounce\n"
	" KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n"
	"  MaxHealth = 50\n"
	" End\n"
	" Behavior = AIUpdateInterface AI\n"
	" End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 announce: birth and death announcements report stop S-6012 in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		movetest::MoveWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string error = w.w.load(kAnnounceObjects);
		REQUIRE_MESSAGE(error.empty(), error);
		w.buildMap();

		Player *alice = w.players.findPlayerWithName("Alice");
		Player *bob = w.players.findPlayerWithName("Bob");
		REQUIRE(alice);
		REQUIRE(bob);

		// Spawn hero: onCreate() is triggered during spawning lifecycle
		Object *hero = w.spawn("TestHeroAnnounce", 300, 300, 0, w.teamOf("Alice"));
		REQUIRE(hero);

		auto *announce = dynamic_cast<AnnounceBirthAndDeathBehavior *>(hero->findModule("AnnounceBirthAndDeathBehavior"));
		REQUIRE(announce);

		// Birth announcement verified
		CHECK(announce->birthAnnouncementCount() == 1);
		CHECK(announce->lastNotificationType() == AnnounceBirthAndDeathBehavior::kHeroInitialSpawnType);

		// Build complete lifecycle
		CHECK(announce->shouldAttemptOnBuildComplete());
		announce->onBuildComplete();
		CHECK(!announce->shouldAttemptOnBuildComplete());

		// Stop S-6012 was recorded
		bool stopFound = false;
		for (const auto &line : w.logic->report().stops)
		{
			if (line == AnnounceBirthAndDeathBehavior::stopLine())
			{
				stopFound = true;
				break;
			}
		}
		CHECK(stopFound);

		// Spawn a grunt (non-hero)
		Object *grunt = w.spawn("TestGruntAnnounce", 350, 300, 0, w.teamOf("Alice"));
		REQUIRE(grunt);

		// Verify shouldAnnounceHeroEvent gate (RW 0x6A03BA / BFME2 0x69F965)
		CHECK(AnnounceBirthAndDeathBehavior::shouldAnnounceHeroEvent(*hero, alice) == true);
		CHECK(AnnounceBirthAndDeathBehavior::shouldAnnounceHeroEvent(*hero, bob) == false);
		CHECK(AnnounceBirthAndDeathBehavior::shouldAnnounceHeroEvent(*hero, nullptr) == true);
		CHECK(AnnounceBirthAndDeathBehavior::shouldAnnounceHeroEvent(*grunt, alice) == false);

		// Kill the hero
		DamageInfo dmg;
		dmg.m_input.m_amount = 200.0f;
		dmg.m_input.m_damageType = DAMAGE_SLASH;
		dmg.m_input.m_deathType = DEATH_NORMAL;
		hero->attemptDamage(dmg);

		CHECK(announce->deathAnnouncementCount() == 1);
		CHECK(announce->lastNotificationType() == AnnounceBirthAndDeathBehavior::kHeroDeathType);
	}
}

TEST_CASE("modgap2 announce: snapshot xfer preserves counts and last notification type")
{
	movetest::MoveWorld w;
	w.logic->setEdition(GameEdition::RotWK201);
	const std::string error = w.w.load(kAnnounceObjects);
	REQUIRE_MESSAGE(error.empty(), error);
	w.buildMap();

	Object *hero = w.spawn("TestHeroAnnounce", 300, 300, 0, w.teamOf("Alice"));
	REQUIRE(hero);
	auto *announce = dynamic_cast<AnnounceBirthAndDeathBehavior *>(hero->findModule("AnnounceBirthAndDeathBehavior"));
	REQUIRE(announce);

	announce->onBuildComplete();
	CHECK(!announce->shouldAttemptOnBuildComplete());

	// Save
	XferSave saved;
	announce->xfer(saved);
	REQUIRE(!saved.data().empty());

	// Simulate dying
	DieModuleInterface::Event dieEvt;
	announce->onDie(dieEvt);
	CHECK(announce->deathAnnouncementCount() == 1);
	CHECK(announce->lastNotificationType() == AnnounceBirthAndDeathBehavior::kHeroDeathType);

	// Restore
	XferLoad loader(saved.data());
	announce->xfer(loader);
	CHECK(!announce->shouldAttemptOnBuildComplete());
	CHECK(announce->birthAnnouncementCount() == 1);
	CHECK(announce->deathAnnouncementCount() == 0);
	CHECK(announce->lastNotificationType() == AnnounceBirthAndDeathBehavior::kHeroInitialSpawnType);
}
