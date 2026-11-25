// OpenBFME. GPL-3.0.
// StealthUpdate post-load disguise drawable restoration (editions: both RotWK 2.01 and BFME2 1.06).
// Sources:
//   - StealthUpdate::loadPostProcess: RW 0x00776E23 / DONOR BFME2 0x00374AE8 (StealthUpdateLoadPost.cpp)
//   - StealthUpdate::update disguise restore: RW 0x007779ED .. 0x00777A2F / DONOR BFME2 0x003756A8 (StealthUpdateLogic.cpp:1451)
//   - StealthUpdate::changeVisualDisguise clears +0x47: RW 0x007772FB / DONOR BFME2 StealthUpdateLogic.cpp:1216

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/GameEdition.h"
#include "Common/GameState.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
const char kStealthDisguiseLoadObjects[] =
	"Object TestSpyLoad\n KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\n"
	" Behavior = StealthUpdate StealthTag\n"
	"  StealthDelay = 0\n"
	"  DisguisesAsTeam = Yes\n"
	"  DisguiseTransitionTime = 200\n"
	"  StartsActive = Yes\n"
	" End\nEnd\n"
	"Object TestEnemyLoad\n KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\nEnd\n";

struct SpyClientHooksLoad final : ObjectClientHooks
{
	struct ReplaceCall
	{
		ObjectID id = INVALID_ID;
		const ThingTemplate *tmpl = nullptr;
		bool hasColor = false;
		std::uint32_t color = 0;
	};
	std::vector<ReplaceCall> replaces;

	void objectCreated(Object &obj) override { obj.friend_bindToClient(this); }
	void objectDestroyed(Object &) override {}
	void replaceDrawable(Object &obj, const ThingTemplate *tmpl, bool hasColor, std::uint32_t color) override
	{
		replaces.push_back({ obj.getID(), tmpl, hasColor, color });
	}
};

struct StealthLoadWorld : combattest::CombatWorld
{
	StealthLoadWorld()
	{
		const std::string err = w.load(kStealthDisguiseLoadObjects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
};
} // namespace

// RW 0x00776E23 / 0x007779ED / 0x007772FB (DONOR BFME2 0x00374AE8 / 0x003756A8):
// Loading a StealthUpdate with m_disguiseTemplate (+0x3C) != nullptr arms m_xferRestoreDisguise (+0x47),
// so the next StealthUpdate::update() calls swapDisguise() (replaceDrawable) once and clears +0x47.
// Loading an undisguised StealthUpdate (+0x3C == nullptr) does not arm +0x47 or call replaceDrawable.
TEST_CASE("stealth load: StealthUpdate restores disguise drawable once on first update after load (RW 0x00776E23)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));

		std::vector<std::uint8_t> disguisedBytes;
		std::vector<std::uint8_t> plainBytes;
		{
			StealthLoadWorld saved;
			saved.logic->setEdition(edition);
			Object *spy = saved.unit("TestSpyLoad", 'A', 300, 300);
			Object *plain = saved.unit("TestSpyLoad", 'A', 400, 300);
			Object *enemy = saved.unit("TestEnemyLoad", 'B', 500, 300);
			REQUIRE(spy != nullptr);
			REQUIRE(plain != nullptr);
			REQUIRE(enemy != nullptr);

			StealthUpdate *stSpy = StealthUpdate::of(*spy);
			StealthUpdate *stPlain = StealthUpdate::of(*plain);
			REQUIRE(stSpy != nullptr);
			REQUIRE(stPlain != nullptr);

			stSpy->disguiseAsObject(enemy);
			saved.frames(10);
			REQUIRE(stSpy->disguiseTemplate() == enemy->getTemplate());
			REQUIRE(stSpy->disguiseShown());
			REQUIRE(stPlain->disguiseTemplate() == nullptr);

			XferSave saveDisguised;
			stSpy->xfer(saveDisguised);
			disguisedBytes = saveDisguised.data();

			XferSave savePlain;
			stPlain->xfer(savePlain);
			plainBytes = savePlain.data();
		}

		StealthLoadWorld loaded;
		loaded.logic->setEdition(edition);
		SpyClientHooksLoad hooks;
		loaded.logic->setClientHooks(&hooks);

		Object *spyLoaded = loaded.unit("TestSpyLoad", 'A', 300, 300);
		Object *plainLoaded = loaded.unit("TestSpyLoad", 'A', 400, 300);
		Object *enemyRef = loaded.unit("TestEnemyLoad", 'B', 500, 300);
		REQUIRE(spyLoaded != nullptr);
		REQUIRE(plainLoaded != nullptr);
		REQUIRE(enemyRef != nullptr);

		StealthUpdate *stSpyLoaded = StealthUpdate::of(*spyLoaded);
		StealthUpdate *stPlainLoaded = StealthUpdate::of(*plainLoaded);
		REQUIRE(stSpyLoaded != nullptr);
		REQUIRE(stPlainLoaded != nullptr);

		loaded.frames(10);
		{
			XferLoad r1(disguisedBytes);
			stSpyLoaded->xfer(r1);
			XferLoad r2(plainBytes);
			stPlainLoaded->xfer(r2);
		}
		REQUIRE(stSpyLoaded->disguiseTemplate() == enemyRef->getTemplate());
		REQUIRE(stPlainLoaded->disguiseTemplate() == nullptr);
		CHECK(hooks.replaces.empty());

		stSpyLoaded->update();
		stPlainLoaded->update();
		REQUIRE(hooks.replaces.size() == 1);
		CHECK(hooks.replaces[0].id == spyLoaded->getID());
		CHECK(hooks.replaces[0].tmpl == enemyRef->getTemplate());
		CHECK(stSpyLoaded->disguiseShown());

		stSpyLoaded->update();
		stPlainLoaded->update();
		CHECK(hooks.replaces.size() == 1);
		loaded.logic->setClientHooks(nullptr);
	}
}

TEST_CASE("stealth load: saving mid-transition does not flip m_disguised early or drift state hashes")
{
	const char *kSlowDisguiseIni =
		"Object TestSlowSpyLoad\n KindOf = INFANTRY SELECTABLE\n"
		" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
		" Behavior = AIUpdateInterface AITag\n End\n"
		" Behavior = StealthUpdate StealthTag\n"
		"  StealthDelay = 0\n"
		"  DisguisesAsTeam = Yes\n"
		"  DisguiseTransitionTime = 2000\n"
		"  StartsActive = Yes\n"
		" End\nEnd\n"
		"Object TestSlowEnemyLoad\n KindOf = INFANTRY SELECTABLE\n"
		" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
		" Behavior = AIUpdateInterface AITag\n End\nEnd\n";

	combattest::CombatWorld uninterrupted;
	REQUIRE(uninterrupted.w.load(kSlowDisguiseIni).empty());
	Object *spyA = uninterrupted.unit("TestSlowSpyLoad", 'A', 300, 300);
	Object *enemyA = uninterrupted.unit("TestSlowEnemyLoad", 'B', 500, 300);
	REQUIRE(spyA != nullptr);
	REQUIRE(enemyA != nullptr);
	const ObjectID spyId = spyA->getID();
	StealthUpdate *stA = StealthUpdate::of(*spyA);
	REQUIRE(stA != nullptr);

	stA->disguiseAsObject(enemyA);
	uninterrupted.frames(1);
	REQUIRE(stA->disguiseTemplate() == enemyA->getTemplate());
	REQUIRE_FALSE(stA->disguiseShown()); // still in the first half of the 2-second transition

	std::vector<std::uint8_t> snapshot;
	std::string error;
	REQUIRE_MESSAGE(uninterrupted.logic->gameState().saveSnapshot(uninterrupted.logic->stateHashSectionNames(), snapshot, &error), error);

	// Continue uninterrupted for 1 frame mid-transition
	uninterrupted.frames(1);
	REQUIRE_FALSE(stA->disguiseShown());
	const std::uint32_t hashMid = uninterrupted.hash();

	// Load into a second world and advance 1 frame: post-load restore must not flip m_disguised early or drift state hashes
	combattest::CombatWorld loaded;
	REQUIRE(loaded.w.load(kSlowDisguiseIni).empty());
	SpyClientHooksLoad hooks;
	loaded.logic->setClientHooks(&hooks);
	REQUIRE_MESSAGE(loaded.logic->gameState().loadSnapshot(loaded.logic->stateHashSectionNames(), snapshot, &error), error);
	Object *spyB = loaded.byId(spyId);
	REQUIRE(spyB != nullptr);
	StealthUpdate *stB = StealthUpdate::of(*spyB);
	REQUIRE(stB != nullptr);

	loaded.frames(1);
	CHECK_FALSE(stB->disguiseShown());
	CHECK(hooks.replaces.empty());
	CHECK(loaded.hash() == hashMid);
	loaded.logic->setClientHooks(nullptr);
}
