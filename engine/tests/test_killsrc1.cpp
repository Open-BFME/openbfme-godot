// OpenBFME. KILLSRC-1 tests: Object::kill as RotWK's RW 0x698EC3 (BFME2 decomp ObjectKill.cpp:61): a killing hit with no source, the caller's damage type and
// the body's maximum health, through Object::attemptDamage (RW 0x698E7D). So a kill credits nobody: no "lost" from scoreTheKill, no attacked-by-self entry, and
// the die modules see source 0. Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/Combat/CombatNames.h"
#include "Common/ScoreKeeper.h"
#include "GameLogic/Module/ExtraDieModules.h"

using namespace combattest;

namespace
{
const char kKillObjects[] =
	"Object KillProbeTower\n"
	"  KindOf = STRUCTURE SELECTABLE SCORE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 250\n"
	"  End\n"
	"  Behavior = CreateObjectDie ModuleTag_Die\n"
	"  End\n"
	"End\n"
	"Object KillProbeTimed\n"
	"  KindOf = STRUCTURE SELECTABLE SCORE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 80\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 2000\n"
	"    MaxLifetime = 2000\n"
	"  End\n"
	"  Behavior = CreateObjectDie ModuleTag_Die\n"
	"  End\n"
	"End\n";

// CreateObjectDie that records the death event it gets (the port's CreateObjectDie does not read the source itself: S-980)
struct DieRecord
{
	int calls = 0;
	ObjectID source = 0xFFFFFFFFu;
	int damageType = -1;
	int deathType = -1;
	float amount = -1.0f;
};
DieRecord g_die;

struct ProbeCreateObjectDie : CreateObjectDie
{
	using CreateObjectDie::CreateObjectDie;
	void onDie(const DieModuleInterface::Event &event) override
	{
		++g_die.calls;
		g_die.source = event.sourceId;
		g_die.damageType = event.damageType;
		g_die.deathType = event.deathType;
		g_die.amount = event.damageAmount;
		CreateObjectDie::onDie(event);
	}
};

struct KillWorld : CombatWorld
{
	KillWorld()
	{
		w.modules.bindModuleProc("CreateObjectDie", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			return std::make_unique<ProbeCreateObjectDie>(thing, dynamic_cast<const CreateObjectDieModuleData *>(data));
		});
		const std::string err = w.load(kKillObjects, INI_LOAD_OVERWRITE, "killsrc1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		g_die = DieRecord{};
	}
};
} // namespace

TEST_CASE("killsrc1: Object::kill is RW 0x698EC3: no source, the caller's damage and death types, the maximum health as the amount; the die modules see source 0")
{
	KillWorld w;
	Object *tower = w.unit("KillProbeTower", 'B', 400, 300);
	REQUIRE(tower);
	Player *bob = w.playerOf('B');
	REQUIRE(bob);
	const int lostBefore = bob->getScoreKeeper().structuresLost();
	ObjectID traced = 0xFFFFFFFFu;
	w.combat().setDamageTrace([&](const Object &victim, std::uint32_t source, float, float) {
		if (&victim == tower)
		{
			traced = source;
		}
	});
	tower->kill(DAMAGE_UNRESISTABLE, DEATH_LASERED);
	w.combat().setDamageTrace(nullptr);
	CHECK(tower->isEffectivelyDead());
	CHECK(traced == INVALID_ID);
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(tower->getBodyModule());
	REQUIRE(body);
	CHECK(body->lastDamager() == INVALID_ID);
	CHECK(g_die.calls == 1);
	CHECK(g_die.source == INVALID_ID);
	CHECK(g_die.damageType == DAMAGE_UNRESISTABLE);
	CHECK(g_die.deathType == DEATH_LASERED);
	CHECK(g_die.amount == 250.0f); // RW 0x698EC3 + 0x20: the body's getMaxHealth (vslot 0x1C)
	// with no killer, ActiveBody runs no scoreTheKill: the owner counts no loss and is not marked as attacked by itself (RW 0x8C430F needs a source object)
	CHECK(bob->getScoreKeeper().structuresLost() == lostBefore);
	CHECK_FALSE(bob->getAttackedBy(bob->getPlayerIndex()));
}

TEST_CASE("killsrc1: a LifetimeUpdate expiry (RW 0x7A802D kill(8, DeathType)) counts no loss and no attacked-by entry for its own player")
{
	KillWorld w;
	Player *bob = w.playerOf('B');
	REQUIRE(bob);
	const int lostBefore = bob->getScoreKeeper().structuresLost();
	Object *timed = w.unit("KillProbeTimed", 'B', 400, 300);
	REQUIRE(timed);
	const ObjectID id = timed->getID();
	const int frames = w.runUntil([&] {
		const Object *o = w.byId(id);
		return !o || o->isEffectivelyDead();
	}, 40);
	CHECK(frames == 10); // MinLifetime = MaxLifetime = 2000 ms = 10 frames
	CHECK(g_die.calls == 1);
	CHECK(g_die.source == INVALID_ID);
	CHECK(g_die.deathType == DEATH_NORMAL);
	CHECK(bob->getScoreKeeper().structuresLost() == lostBefore);
	CHECK_FALSE(bob->getAttackedBy(bob->getPlayerIndex()));
}

TEST_CASE("killsrc1: a kill made while the victim runs its AI waits a frame like any hit (RW 0x698EC3 goes through attemptDamage RW 0x698E7D)")
{
	KillWorld w;
	Object *d = w.unit("Dummy", 'B', 400, 300);
	REQUIRE(d);
	const unsigned updatingAI = (unsigned)CombatNames::statuses().updatingAI;
	d->setStatus(updatingAI, true);
	d->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	d->setStatus(updatingAI, false);
	CHECK_FALSE(d->isEffectivelyDead());
	CHECK(d->pendingDamageCount() == 1);
	const ObjectID id = d->getID();
	const int frames = w.runUntil([&] {
		const Object *o = w.byId(id);
		return !o || o->isEffectivelyDead();
	}, 10);
	CHECK(frames == 2); // the pending delay 1.0 counts down a frame at a time and applies once below 0 (RW 0x697EB6)
}
