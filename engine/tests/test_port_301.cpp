// OpenBFME unit tests. PORT-301 (fork issue 693): ShareExperienceBehavior — the shared experience. Synthetic INI only; no ROTWK_INSTALL /
// OPENBFME_ENNORATH_INSTALL. GPL-3.0.
//
// What RotWK 2.01 does (every address read with tools/mod/rw.py from the owner's game.dat, md5 daed3668…):
//   * ShareExperienceBehavior::share (RW 0x8832F0, the class's own function, already cited in the port): the object shares the experience it just gained with
//     every alive ally in Radius that the module's ObjectFilter allows (the ObjectFilter = ANY +HERO of the fixture allows the HERO candidates);
//     value := fstp(value * Percentage) and then value := fstp(dropOff * value) — 100 * 0.5 * (1 - 50 / 100) = 25 for a candidate 50 away (RW 0x883453 ..
//     RW 0x88346C). The value is not restored between the candidates.
//   * The hero-experience-sharing switch, TheGameLogic + 0x9F: the GameLogic constructor sets the byte to 1 (RW 0x62D299: mov byte ptr [esi + 0x9F], 1;
//     TheGameLogic is the global 0x00DE412C, stored by that constructor at RW 0x62CD16), the script action 538 SET_HERO_EXPERIENCE_SHARING writes it
//     (RW 0x7CDF99 -> RW 0x7BD3D1: mov al, [esp + 4]; mov ecx, [0xDE412C]; mov byte ptr [ecx + 0x9F], al — the parameter's int (+ 8) != 0), and the share
//     routine reads it (RW 0x883428: cmp byte ptr [eax + 0x9F], 0) to skip a HERO candidate while it is clear (RW 0x88342F: jne; the candidate's KindOf
//     bit 26 at RW 0x883434: test byte ptr [eax + 0x113], 4). The retail's .text holds only five +0x9F accesses (RW 0x5F1563 is a Player byte, RW 0x62D299
//     the constructor, RW 0x7BB038 / RW 0x7BD3DB the writers, RW 0x883428 the reader), and GameLogic's xfer (RW 0x630D6F .. RW 0x630D78) reads none of them:
//     the byte is neither saved nor hashed.
//   * ShareExperienceBehavior::xfer (RW 0x8E3BB1): the version (RW 0x404F15 writes (1, 1)) and the base UpdateModule xfer (RW 0x850C60) and nothing else.
//
// The fixture's numbers are the mod's module: Percentage 0.5, DropOff 1.0, Radius 100 (the port's HERO-2 regression test uses the same block).

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <cstdint>
#include <vector>

namespace
{
// A HERO sharer and a plain INFANTRY sharer, both with the mod's ShareExperienceBehavior block; the receivers are HEROes of the same side
const char kPort301Objects[] =
	"Object Port301Hero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = ShareExperienceBehavior ShareTag\n  Radius = 100.0\n  DropOff = 1.0\n  Percentage = 0.5\n  ObjectFilter = ANY +HERO\n End\n"
	"End\n"
	"Object Port301Footman\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = ShareExperienceBehavior ShareTag\n  Radius = 100.0\n  DropOff = 1.0\n  Percentage = 0.5\n  ObjectFilter = ANY +HERO\n End\n"
	"End\n";

float experienceOf(Object *o)
{
	return o->getExperienceTracker()->getExperience();
}
} // namespace

// PORT-301: with TheGameLogic + 0x9F at its default of 1 (RW 0x62D299) a HERO's shared experience reaches an allied HERO: 100 * 0.5 * (1 - 50 / 100) = 25
// (RW 0x883453, RW 0x88346C, RW 0x88348F)
TEST_CASE("PORT-301: a hero shares its experience with an allied hero while TheGameLogic + 0x9F is set (RW 0x62D299, RW 0x883428)")
{
	combattest::CombatWorld f(kPort301Objects);
	Object *giver = f.unit("Port301Hero", 'A', 1000, 1000);
	Object *receiver = f.unit("Port301Hero", 'A', 1050, 1000); // 50 away: the drop off is 0.5
	REQUIRE(giver);
	REQUIRE(receiver);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);
	CHECK(giver->logic().getHeroExperienceSharing()); // the constructor's 1 (RW 0x62D299)
	const float before = experienceOf(receiver);
	s->share(100.0f);
	CHECK(experienceOf(receiver) - before == 25.0f);
	CHECK(s->shares() == 1);
}

// PORT-301: with TheGameLogic + 0x9F clear the share routine skips a HERO candidate (RW 0x883428: cmp byte ptr [eax + 0x9F], 0; RW 0x883434: the
// candidate's KindOf bit 26)
TEST_CASE("PORT-301: a hero shares no experience with another hero while TheGameLogic + 0x9F is clear (RW 0x883428)")
{
	combattest::CombatWorld f(kPort301Objects);
	Object *giver = f.unit("Port301Hero", 'A', 1000, 1000);
	Object *receiver = f.unit("Port301Hero", 'A', 1050, 1000);
	REQUIRE(giver);
	REQUIRE(receiver);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);
	giver->logic().setHeroExperienceSharing(false);
	const float before = experienceOf(receiver);
	s->share(100.0f);
	CHECK(experienceOf(receiver) == before);
	CHECK(s->shares() == 0);
}

// PORT-301: the skip needs a HERO source as well: a plain INFANTRY sharer still shares with the HERO receiver while TheGameLogic + 0x9F is clear
// (RW 0x88342F: jne 0x88343D — the source's own KindOf bit 26 is tested first, at RW 0x88331F)
TEST_CASE("PORT-301: a plain sharer still shares with a hero while TheGameLogic + 0x9F is clear (RW 0x883428)")
{
	combattest::CombatWorld f(kPort301Objects);
	Object *giver = f.unit("Port301Footman", 'A', 1000, 1000);
	Object *receiver = f.unit("Port301Hero", 'A', 1050, 1000);
	REQUIRE(giver);
	REQUIRE(receiver);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);
	giver->logic().setHeroExperienceSharing(false);
	const float before = experienceOf(receiver);
	s->share(100.0f);
	CHECK(experienceOf(receiver) - before == 25.0f);
	CHECK(s->shares() == 1);
}

// PORT-301: the script action 538 SET_HERO_EXPERIENCE_SHARING writes TheGameLogic + 0x9F (RW 0x7CDF99 -> RW 0x7BD3D1: the parameter's int (+ 8) != 0)
TEST_CASE("PORT-301: the script action SET_HERO_EXPERIENCE_SHARING (538) writes TheGameLogic + 0x9F (RW 0x7CDF99, RW 0x7BD3D1)")
{
	using namespace scripttest;
	ScriptGame g;
	g.lists[1].items.push_back(item(script("Port301NoShare", { { cond("CONDITION_TRUE", {}) } },
		{ act("SET_HERO_EXPERIENCE_SHARING", { ip(8, 0) }) }, {}, true)));
	g.start();
	g.frame();
	CHECK(g.engine().stats().unportedActions.empty());
	CHECK(g.lw.logic->getHeroExperienceSharing() == false);

	ScriptGame g2;
	g2.lists[1].items.push_back(item(script("Port301Share", { { cond("CONDITION_TRUE", {}) } },
		{ act("SET_HERO_EXPERIENCE_SHARING", { ip(8, 1) }) }, {}, true)));
	g2.start();
	g2.frame();
	CHECK(g2.engine().stats().unportedActions.empty());
	CHECK(g2.lw.logic->getHeroExperienceSharing() == true);
}

// PORT-301: the behaviour's snapshot holds the version and the base xfer and nothing else (RW 0x8E3BB1): the share counter is the port's own diagnostics and
// does not change the saved bytes
TEST_CASE("PORT-301: the snapshot of a ShareExperienceBehavior holds the version and the base xfer only (RW 0x8E3BB1)")
{
	combattest::CombatWorld f(kPort301Objects);
	Object *giver = f.unit("Port301Hero", 'A', 1000, 1000);
	Object *receiver = f.unit("Port301Hero", 'A', 1050, 1000);
	REQUIRE(giver);
	REQUIRE(receiver);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);

	XferSave save;
	s->xfer(save);
	// the behaviour's own version byte, the base xfer's four version bytes and the u32 of the next call frame: 9 bytes in all
	REQUIRE(save.data().size() == 9);

	s->share(100.0f); // the counter grows (RW 0x88348F)
	CHECK(s->shares() == 1);
	XferSave save2;
	s->xfer(save2);
	CHECK(save2.data().size() == 9); // the counter is not in the snapshot: the bytes do not depend on it

	// the saved bytes are the version and the base xfer: they load back into the module without a complaint
	XferLoad load(save2.data());
	s->xfer(load);
	CHECK(load.position() == 9); // the load read exactly the saved bytes
	CHECK(s->shares() == 1); // the counter is not restored: it is not saved
}

// PORT-301: a snapshot written by the port's own version 1 (the version byte 1 and the share counter after the base xfer) still loads: the counter is read and
// dropped (SAVES.md section 5, the versioned blocks: the current version rose to 2 and the old layout has its read branch)
TEST_CASE("PORT-301: a version 1 snapshot of a ShareExperienceBehavior still loads (SAVES.md section 5)")
{
	combattest::CombatWorld f(kPort301Objects);
	Object *giver = f.unit("Port301Hero", 'A', 1000, 1000);
	REQUIRE(giver);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);

	XferSave save;
	s->xfer(save);
	std::vector<std::uint8_t> legacy = save.data();
	// the behaviour's own version byte, the base xfer's four version bytes and the u32 of the next call frame: 9 bytes in all
	REQUIRE(legacy.size() == 9);
	REQUIRE(legacy[0] == 2); // the port's version 2
	legacy[0] = 1; // the port's version 1 wrote its own version byte
	for (int i = 0; i < 8; ++i)
	{
		legacy.push_back(0); // the u64 share counter the version 1 wrote after the base xfer
	}
	REQUIRE(legacy.size() == 17); // the version 1 layout is the version 2 one plus the counter

	XferLoad load(legacy);
	s->xfer(load);
	CHECK(load.position() == 17); // the load read the counter too: the version 1 layout is fully consumed
	CHECK(s->shares() == 0); // the counter was read and dropped: it is diagnostics, not state
}
