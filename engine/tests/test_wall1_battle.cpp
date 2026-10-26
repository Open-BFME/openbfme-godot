// OpenBFME unit tests, lane WALL-1: the assault on Helm's Deep, piece by piece (Siege1TestUtil.h's fortress game). GPL-3.0.
//
// The culvert as the film blows it, with retail's units and data: Isengard's mine carrier (IsengardExplosiveMine) ordered at the culvert drops its charge
// (IsengardExplosiveMineDroppingWeapon's SpawnAndFadeNugget, RW 0x911C46: an IsengardDeployedExplosiveMine where it stands, the carrier fades), the
// berserker lights his torch (SpecialAbilityBerserkerLightTorch: WEAPONSET_TOGGLE_1) and the charge's own Detonate button goes off (a script cannot order the
// torch onto its own side's charge: no forced script attack), whose BURNED death fires IsengardExplosiveMineExplosionWeapon (50000% against HelmsDeepCulvert):
// the culvert falls. The buttons go through the ported command button script actions (SCRIPT-4's, S-1190). SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"

#include <cstdio>

using namespace siege1test;

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";

Object *firstOf(FortressGame &g, const char *templ)
{
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == templ && !o->isEffectivelyDead())
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("wall1 battle retail: the mine carrier drops its charge at Helm's Deep's culvert, the berserker lights his torch, the charge goes off and the culvert falls (S-1190)")
{
	OPENBFME_REQUIRE_START(s);
	FortressGame g(*s, kHelmsDeep);
	g.frames(2);
	Object *culvert = g.named("Culvert");
	REQUIRE(culvert);
	CHECK(culvert->getTemplate()->getName() == "HelmsDeepCulvert");
	Player *me = g.human();
	REQUIRE(me);
	std::string error;
	// where the Battle of Helm's Deep scenario stands them: before the culvert, out of bowshot
	Object *carrier = g.game->createObject("IsengardExplosiveMine", me->getPlayerIndex(), Coord3D{ 1660.0f, 2440.0f, 0.0f }, 1.5708f, &error);
	REQUIRE_MESSAGE(carrier, error);
	Object *berserker = g.game->createObject("IsengardBeserker", me->getPlayerIndex(), Coord3D{ 1710.0f, 2440.0f, 0.0f }, 1.5708f, &error);
	REQUIRE_MESSAGE(berserker, error);
	g.frames(5);
	const ObjectID carrierId = carrier->getID();
	REQUIRE(carrier->getAIUpdateInterface());
	CHECK(carrier->getAIUpdateInterface()->aiAttackObject(culvert, CMD_FROM_SCRIPT));
	int dropped = -1;
	Object *charge = nullptr;
	for (int f = 0; f < 200 && !charge; ++f)
	{
		g.frames(1);
		charge = firstOf(g, "IsengardDeployedExplosiveMine");
		dropped = charge ? f + 1 : -1;
	}
	REQUIRE(charge);
	std::printf("  info: the charge dropped at frame %d at (%.1f %.1f %.1f)\n", dropped, (double)charge->getPosition()->x, (double)charge->getPosition()->y,
				(double)charge->getPosition()->z);
	const Object *c0 = g.game->logic().findObjectByID(carrierId);
	CHECK((!c0 || c0->isEffectivelyDead())); // the carrier fades (DEATH_FADED)
	CHECK(charge->getTeam() == berserker->getTeam());
	CHECK(dropped == 61);
	// the scenario's scripts: the charge named (as SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER names it), the berserker's torch lit
	// (NAMED_USE_COMMANDBUTTON_ABILITY Command_SpecialAbilityLightTorch: SPECIAL_POWER SpecialAbilityBerserkerLightTorch -> WEAPONSET_TOGGLE_1), then the
	// charge's own Detonate button (Command_IsengardMineDetonate: SpecialAbilityDetonate, IsegardDetonateMine's SUICIDE BURNED damage)
	ScriptEngine &e = g.game->logic().scriptEngine();
	berserker->setName("HD_Berserker");
	e.nameInCache("HD_Bomb", *charge);
	auto use = [&](const char *unit, const char *button) {
		ScriptActionRec a;
		a.internalName = "NAMED_USE_COMMANDBUTTON_ABILITY";
		a.type = a.resolved = ScriptTemplates::findAction(a.internalName);
		ScriptParameter p0, p1;
		p0.type = 14;
		p0.stringValue = unit;
		p1.type = 34;
		p1.stringValue = button;
		a.params = { p0, p1 };
		e.executeAction(a);
	};
	use("HD_Berserker", "Command_SpecialAbilityLightTorch");
	g.frames(30); // ToggleOnSleepFrames 25
	CHECK(((berserker->getWeaponSetFlags()[0] >> 24) & 1u) == 1u); // WEAPONSET_TOGGLE_1, weapon set flag 24
	use("HD_Bomb", "Command_IsengardMineDetonate");
	// WeaponFireSpecialAbilityUpdate: UnpackTime 20000 (100 frames), then IsegardDetonateMine from the charge (it has no weapon set: the module's own host);
	// its BURNED death fires IsengardExplosiveMineExplosionWeapon
	const ObjectID chargeId = charge->getID();
	unsigned shots = 0;
	int fell = -1;
	for (int f = 0; f < 300 && fell < 0; ++f)
	{
		g.frames(1);
		if (const Object *ch = g.game->logic().findObjectByID(chargeId))
		{
			for (const auto &m : ch->modules())
			{
				if (const auto *wf = dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(m.get()))
				{
					shots = wf->shotsFired();
				}
			}
		}
		const Object *cv = g.named("Culvert");
		if (!cv || cv->isEffectivelyDead() || healthOf(cv) <= 0.0f)
		{
			fell = f + 1;
		}
	}
	std::printf("  info: the culvert fell %d frames after the Detonate\n", fell);
	CHECK(shots == 1);
	CHECK(fell == 103);
	// SCRIPT-4's command button actions (WALL-1's S-4841 closed into S-1190 on merge queue q26): RW 0x696FD2 has no case for ATTACK_MOVE (10) without a
	// position, so Command_AttackMove does nothing, as retail, and notes no unported case
	auto noted = [&]() {
		for (const auto &n : e.stats().notes)
		{
			if (n.first.rfind("[S-1190]", 0) == 0)
			{
				return true;
			}
		}
		return false;
	};
	CHECK_FALSE(noted());
	use("HD_Berserker", "Command_AttackMove");
	CHECK_FALSE(noted());
}
