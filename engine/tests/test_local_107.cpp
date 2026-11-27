// OpenBFME. GPL-3.0.
// LOCAL-107 (fork issue 785): AIGateUpdate destructor runtime polygon trigger cleanup (editions: both RotWK 2.01 and BFME2 1.06).
// Sources:
//   - AIGateUpdate::~AIGateUpdate: RW 0x008B490F (scalar deleting dtor RW 0x008B4A33, PolygonTrigger::getPolygonTriggerByID RW 0x006E4A7F,
//     PolygonTrigger::removePolygonTrigger RW 0x6E4ABE, object trigger cleanup RW 0x6263AF); DONOR BFME2 AIGateUpdate.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/GameEdition.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>

namespace
{
const char kGateObjects107[] =
	"SkirmishAIData\n"
	"End\n"
	"Locomotor TestLoco107\n"
	"  Surfaces = GROUND\n"
	"  Speed = 40\n"
	"  Acceleration = 100\n"
	"  Braking = 100\n"
	"End\n"
	"Object TestGate107\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = GateOpenAndCloseBehavior GateTag\n"
	"    ResetTimeInMilliseconds = 100\n"
	"    OpenByDefault = No\n"
	"    PercentOpenForPathing = 50\n"
	"  End\n"
	"  Behavior = AIGateUpdate AIGateTag\n"
	"    TriggerWidthX = 80.0\n"
	"    TriggerWidthY = 80.0\n"
	"  End\n"
	"End\n"
	"Object TestUnit107\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = TestLoco107\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"End\n";

struct GateWorld107 : combattest::CombatWorld
{
	SkirmishAIStore aiStore;
	PlayerTemplate aiTemplate;

	GateWorld107() : aiStore(w.keys)
	{
		GateModules::registerAll(w.modules);
		aiStore.registerBlocks(w.fx.env.blocks);
		const std::string err = w.load(kGateObjects107);
		REQUIRE_MESSAGE(err.empty(), err);
		aiTemplate.m_playableSide = true;
		logic->skirmishAI().setStore(&aiStore);
	}
};

bool hasTriggerEntry(const Object &obj, int triggerIdx)
{
	for (int i = 0; i < obj.triggerCount(); ++i)
	{
		if (obj.triggerEntry(i).trigger == triggerIdx && obj.triggerEntry(i).inside)
		{
			return true;
		}
	}
	return false;
}
} // namespace

// RW 0x008B490F (~AIGateUpdate), RW 0x008B4A33 (scalar deleting dtor), RW 0x006E4A7F (PolygonTrigger::getPolygonTriggerByID):
// destroying a gate with an active AIGateUpdate removes its runtime polygon trigger from ScriptEngine (RW 0x6E4ABE)
// and clears the trigger entry from every object that was inside it (RW 0x6263AF).
TEST_CASE("LOCAL-107 AIGateUpdate: destroying a gate removes its runtime polygon trigger and clears unit entries (RW 0x008B490F)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		GateWorld107 world;
		world.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *gate = world.unit("TestGate107", 'A', 300.0f, 300.0f);
		Object *ally = world.unit("TestUnit107", 'A', 305.0f, 300.0f);
		REQUIRE(gate != nullptr);
		REQUIRE(ally != nullptr);

		Player *owner = gate->getControllingPlayer();
		REQUIRE(owner != nullptr);
		owner->init(&world.aiTemplate, 0);
		owner->setPlayerType(PLAYER_COMPUTER, true);
		world.logic->skirmishAI().newGame(true);
		REQUIRE(world.logic->skirmishAI().findAI(owner->getPlayerIndex()) != nullptr);

		auto *aiGate = dynamic_cast<AIGateUpdate *>(gate->findModule("AIGateUpdate"));
		REQUIRE(aiGate != nullptr);
		REQUIRE(GateOpenAndCloseBehavior::findGate(*gate) != nullptr);

		world.frames(1);
		const int trigIdx = aiGate->triggerIndex();
		REQUIRE(trigIdx == 0);
		Coord3D pos = *ally->getPosition();
		pos.x += 3.0f;
		ally->setPosition(&pos);
		world.frames(2);
		CHECK(aiGate->alliesInside() == 1);
		CHECK(world.logic->scriptEngine().triggerAreaAt((size_t)trigIdx).points.size() == 4);
		CHECK(hasTriggerEntry(*ally, trigIdx));

		world.logic->destroyObject(gate);
		world.frames(1);

		CHECK(world.logic->scriptEngine().triggerAreaAt((size_t)trigIdx).points.empty());
		CHECK_FALSE(hasTriggerEntry(*ally, trigIdx));
	}
}

// Neighbour test: a human player's gate never creates a runtime trigger (RW 0x8B4D34 -> RW 0x6A950B) and destroys cleanly.
TEST_CASE("LOCAL-107 neighbour: human gate destroys cleanly without runtime trigger")
{
	GateWorld107 world;
	Object *gate = world.unit("TestGate107", 'A', 300.0f, 300.0f);
	REQUIRE(gate != nullptr);
	auto *aiGate = dynamic_cast<AIGateUpdate *>(gate->findModule("AIGateUpdate"));
	REQUIRE(aiGate != nullptr);
	world.frames(2);
	CHECK(aiGate->triggerIndex() == -1);
	world.logic->destroyObject(gate);
	world.frames(1);
}
