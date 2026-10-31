// OpenBFME unit tests, lane SIEGE-1: the Dwarven demolisher's deploy (DeployStyleAIUpdate + ToggleDeploySpecialAbilityUpdate, S-1984 / S-4945). GPL-3.0.
//
// The retail DwarvenDemolisher (UnpackTime / PackTime 2000 ms = 10 logic frames, MustDeployToAttack No, DeployedAttributeModifier
// DwarvenDemolisherDeployModifier: ARMOR 100%) on Helm's Deep, owned by the human. The frames are this port's deterministic results (a fixed seed): regression
// pins, not retail measurements. SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DeployStyleAIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"

#include <string>

using namespace siege1test;

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";

SpecialPowerModuleInterface *deployPower(Object &o)
{
	for (const auto &m : o.modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == "SpecialAbilityDwarvenDemolisherDeploy")
		{
			return sp;
		}
	}
	return nullptr;
}

ToggleDeploySpecialAbilityUpdate *toggleOf(Object &o)
{
	for (const auto &m : o.modules())
	{
		if (ToggleDeploySpecialAbilityUpdate *t = dynamic_cast<ToggleDeploySpecialAbilityUpdate *>(m.get()))
		{
			return t;
		}
	}
	return nullptr;
}

float armourBonus(const Object &o)
{
	float v = 0.0f;
	return o.attributeModifierSum(ATTRIBUTE_ARMOR, nullptr, v) ? v : 0.0f;
}

bool reported(GameLogic &logic, const char *id)
{
	for (const std::string &l : logic.report().stops)
	{
		if (l.rfind(id, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

struct DemoRig
{
	FortressGame g;
	Object *demo = nullptr;
	DeployStyleAIUpdate *ai = nullptr;
	SpecialPowerModuleInterface *power = nullptr;
	ToggleDeploySpecialAbilityUpdate *toggle = nullptr;
	explicit DemoRig(starttest::Shared &s) : g(s, kHelmsDeep)
	{
		g.frames(2);
		Player *me = g.human();
		REQUIRE(me);
		const Waypoint *w = g.game->logic().terrain()->findWaypointByName("Player_2_Start");
		REQUIRE(w);
		std::string error;
		demo = g.game->createObject("DwarvenDemolisher", me->getPlayerIndex(), Coord3D{ w->location.x + 150.0f, w->location.y, 0.0f }, 0.0f, &error);
		REQUIRE_MESSAGE(demo, error);
		g.frames(5);
		ai = DeployStyleAIUpdate::of(*demo);
		power = deployPower(*demo);
		toggle = toggleOf(*demo);
		REQUIRE(ai);
		REQUIRE(power);
		REQUIRE(toggle);
	}
	// runs frames until the AI is in `state`; the frames it took, -1 when it did not get there within `limit`
	int until(int state, int limit)
	{
		for (int f = 1; f <= limit; ++f)
		{
			g.frames(1);
			if (ai->state() == state)
			{
				return f;
			}
		}
		return -1;
	}
};
} // namespace

TEST_CASE("siege1 demolisher retail: the deploy button unpacks the Dwarven demolisher (UNPACKING, 10 frames) into DEPLOYED with ARMOR +100%; pressed again it packs")
{
	OPENBFME_REQUIRE_START(s);
	DemoRig r(*s);
	const int unpacking = CombatNames::modelCondition("UNPACKING");
	const int packing = CombatNames::modelCondition("PACKING");
	const int deployed = CombatNames::modelCondition("DEPLOYED");
	CHECK(r.ai->state() == DeployStyleAIUpdate::READY_TO_MOVE);
	CHECK(armourBonus(*r.demo) == 0.0f);
	CHECK(reported(r.g.game->logic(), "[S-4945]"));
	CHECK(r.power->isReady());

	r.power->doSpecialPower(0);
	const int toUnpack = r.until(DeployStyleAIUpdate::DEPLOY, 20);
	MESSAGE("unpacking after " << toUnpack << " frames");
	CHECK(toUnpack == 2);
	CHECK(r.toggle->deploys() == 1u);
	CHECK(r.demo->testModelCondition(unpacking));
	CHECK_FALSE(r.demo->testModelCondition(deployed));
	const int toDeployed = r.until(DeployStyleAIUpdate::READY_TO_ATTACK, 40);
	MESSAGE("deployed after " << toDeployed << " more frames");
	CHECK(toDeployed == 10); // UnpackTime 2000 ms
	CHECK(r.demo->testModelCondition(deployed));
	CHECK_FALSE(r.demo->testModelCondition(unpacking));
	CHECK(armourBonus(*r.demo) == 1.0f); // DwarvenDemolisherDeployModifier: ARMOR 100%
	r.g.frames(10);
	CHECK(r.ai->state() == DeployStyleAIUpdate::READY_TO_ATTACK); // a deployed demolisher stays deployed

	REQUIRE(r.power->isReady());
	r.power->doSpecialPower(0);
	const int toPack = r.until(DeployStyleAIUpdate::UNDEPLOY, 20);
	MESSAGE("packing after " << toPack << " frames");
	CHECK(toPack == 2);
	CHECK(r.toggle->undeploys() == 1u);
	CHECK(r.demo->testModelCondition(packing));
	CHECK_FALSE(r.demo->testModelCondition(deployed));
	CHECK(armourBonus(*r.demo) == 0.0f);
	CHECK_FALSE(r.toggle->allowsSpecialPowerUse()); // RW 0x8B1C5F: not while PACKING
	const int toReady = r.until(DeployStyleAIUpdate::READY_TO_MOVE, 40);
	CHECK(toReady == 10); // PackTime 2000 ms
	CHECK_FALSE(r.demo->testModelCondition(packing));
}

TEST_CASE("siege1 demolisher retail: a move order to a deployed demolisher packs it first; the kept order runs once it is packed (RW 0x891F9B)")
{
	OPENBFME_REQUIRE_START(s);
	DemoRig r(*s);
	r.power->doSpecialPower(0);
	REQUIRE(r.until(DeployStyleAIUpdate::READY_TO_ATTACK, 40) > 0); // tolerance: the deploy happened within the limit (-1: never)
	const Coord3D start = *r.demo->getPosition();
	const Coord3D goal{ start.x + 200.0f, start.y, start.z };
	AIUpdateInterface *ai = r.demo->getAIUpdateInterface();
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	const int toPack = r.until(DeployStyleAIUpdate::UNDEPLOY, 20);
	MESSAGE("packing after " << toPack << " frames");
	CHECK(toPack == 1); // the path asked for (AI + 0x3B1) packs the deployed unit
	CHECK(r.ai->hasPendingCommand());
	CHECK_FALSE(r.ai->isIdle());
	const int toReady = r.until(DeployStyleAIUpdate::READY_TO_MOVE, 40);
	CHECK(toReady == 10);
	CHECK(dist2(*r.demo->getPosition(), start) == 0.0f); // it did not move while it packed
	CHECK(r.ai->replays() == 1u);
	CHECK_FALSE(r.ai->hasPendingCommand());
	r.g.frames(100);
	const float moved = dist2(*r.demo->getPosition(), start);
	MESSAGE("moved " << moved);
	CHECK(dist2(*r.demo->getPosition(), goal) < 30.0f); // tolerance: the move's arrival slop around the goal
	CHECK(r.ai->state() == DeployStyleAIUpdate::READY_TO_MOVE);
}

TEST_CASE("siege1 demolisher retail: a move order while it unpacks waits and runs after the demolisher packs again")
{
	OPENBFME_REQUIRE_START(s);
	DemoRig r(*s);
	r.power->doSpecialPower(0);
	REQUIRE(r.until(DeployStyleAIUpdate::DEPLOY, 20) > 0); // tolerance: the unpack started within the limit (-1: never)
	const Coord3D start = *r.demo->getPosition();
	const Coord3D goal{ start.x + 200.0f, start.y, start.z };
	r.demo->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	CHECK(r.ai->hasPendingCommand()); // RW 0x892120: inside the unpacking the command waits
	const int toDeployed = r.until(DeployStyleAIUpdate::READY_TO_ATTACK, 40);
	const int toPack = r.until(DeployStyleAIUpdate::UNDEPLOY, 20);
	const int toReady = r.until(DeployStyleAIUpdate::READY_TO_MOVE, 40);
	MESSAGE("deployed " << toDeployed << ", packing " << toPack << ", ready " << toReady << ", replays " << r.ai->replays());
	CHECK(toDeployed == 10);
	CHECK(toPack == 1); // the kept move ran once deployed (RW 0x8921E3 state 2), its path packs the unit
	CHECK(toReady == 10);
	CHECK(r.ai->replays() == 2u); // once deployed (state 2's replay), once packed (state 0's)
	r.g.frames(100);
	CHECK(dist2(*r.demo->getPosition(), goal) < 30.0f); // tolerance: the move's arrival slop around the goal
}
