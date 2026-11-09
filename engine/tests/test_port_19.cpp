// PORT-19: RotWK's RespawnUpdate asks the producer's exit interface for its natural rally point at revive time.
// The result is not used here; actual exit placement is the separate slot-2 call when the spawn animation ends.

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/QueueProductionExitUpdate.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>
#include <utility>

namespace
{
const char kPort19Objects[] =
	"Object PORT19Producer\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"  End\n"
	"End\n"
	"Object PORT19NoExitProducer\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"End\n"
	"Object PORT19Hero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 2000\n"
	"    InitialSpawnAnim = EMOTION_CELEBRATING\n"
	"    InitialSpawnAnimationTime = 1000\n"
	"    RespawnRules = AutoSpawn:No Cost:1000 Time:60000 Health:100%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n";

struct ExitQueryTrace
{
	unsigned calls = 0;
	bool offset = false;
	Coord3D incoming;
};

class ProbedQueueProductionExitUpdate final : public QueueProductionExitUpdate
{
public:
	ProbedQueueProductionExitUpdate(Thing *thing, const QueueProductionExitUpdateModuleData *data, std::shared_ptr<ExitQueryTrace> trace)
		: QueueProductionExitUpdate(thing, data)
		, m_trace(std::move(trace))
	{
	}

	bool getNaturalRallyPoint(Coord3D *out, bool offset) const override
	{
		++m_trace->calls;
		m_trace->incoming = *out;
		m_trace->offset = offset;
		return QueueProductionExitUpdate::getNaturalRallyPoint(out, offset);
	}

private:
	std::shared_ptr<ExitQueryTrace> m_trace;
};

struct Port19Fixture : prodtest::ProdWorld
{
	std::shared_ptr<ExitQueryTrace> trace = std::make_shared<ExitQueryTrace>();
	Object *producer = nullptr;
	Object *hero = nullptr;

	Port19Fixture()
	{
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.7f;
		logic->settings().unitReallyDamagedThreshold = 0.35f;
		const std::shared_ptr<ExitQueryTrace> savedTrace = trace;
		w.modules.bindModuleProc("QueueProductionExitUpdate", MODULETYPE_BEHAVIOR,
			[savedTrace](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) {
				return std::unique_ptr<Module>(new ProbedQueueProductionExitUpdate(thing, static_cast<const QueueProductionExitUpdateModuleData *>(data), savedTrace));
			});
		load(kPort19Objects);
		producer = make("PORT19Producer", teamOf("Alice"));
		hero = make("PORT19Hero", teamOf("Alice"));
		REQUIRE(producer != nullptr);
		REQUIRE(hero != nullptr);
	}

	RespawnUpdate *respawn() { return dynamic_cast<RespawnUpdate *>(hero->findModule("RespawnUpdate")); }
};
} // namespace

TEST_CASE("PORT-19 RespawnUpdate queries the producer exit's natural rally point with the copied producer position and offset (RW 0x8B38B0-0x8B38E7)")
{
	Port19Fixture f;
	const Coord3D producerPosition = { 123.0f, 456.0f, 7.0f };
	f.producer->setPosition(&producerPosition);
	RespawnUpdate *respawn = f.respawn();
	REQUIRE(respawn != nullptr);
	respawn->setInitialSpawn(false);

	respawn->onRevived(f.producer);

	CHECK(f.trace->calls == 1); // RW 0x68BB14 then vslot +0x24, with no null-interface path taken
	CHECK(f.trace->incoming.x == 123.0f);
	CHECK(f.trace->incoming.y == 456.0f);
	CHECK(f.trace->incoming.z == 7.0f);
	CHECK(f.trace->offset); // the pushed argument at RW 0x8B38DF is 1
	CHECK(respawn->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(f.hero->getPosition()->x == 0.0f); // the query's output is unused; placement is the later slot-2 exit call
}

TEST_CASE("PORT-19 RespawnUpdate without a producer preserves the port's existing revive behavior")
{
	Port19Fixture f;
	RespawnUpdate *respawn = f.respawn();
	REQUIRE(respawn != nullptr);
	respawn->setInitialSpawn(false);

	respawn->onRevived(nullptr);

	CHECK(f.trace->calls == 0); // adjacent null-interface behavior: the producer-dependent lookup/call is skipped
	CHECK(respawn->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(respawn->healthFraction() == 1.0f); // synthetic RespawnRules Health:100%; RW 0x8B39DA
	CHECK(f.hero->getBodyModule()->getHealth() == 500.0f);
}

TEST_CASE("PORT-19 RespawnUpdate skips the query when a producer has no exit interface (RW 0x68BB14 / 0x8B38D9-0x8B38EA)")
{
	Port19Fixture f;
	Object *producerWithoutExit = f.make("PORT19NoExitProducer", f.teamOf("Alice"));
	REQUIRE(producerWithoutExit != nullptr);
	RespawnUpdate *respawn = f.respawn();
	REQUIRE(respawn != nullptr);
	respawn->setInitialSpawn(false);

	respawn->onRevived(producerWithoutExit);

	CHECK(f.trace->calls == 0); // RW 0x68BB14 returns null; the branch skips the slot-9 call
	CHECK(respawn->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(respawn->healthFraction() == 1.0f); // RW 0x8B39DA: synthetic RespawnRules Health:100%
}
