// OpenBFME unit tests. GPL-3.0.
// ObjectCreationUpgrade::update (RW 0x8B8464, loop RW 0x8B86DB .. 0x8B8708; DONOR BFME2 0x004B42DB
// ObjectCreationUpgrade.cpp:661-670): walk spawned object's behavior modules for SlavedUpdateInterface (slot 0x68)
// and call onEnslave(owner) (slot 4) so slaves with DieOnMastersDeath die when the creator falls.
// Synthetic INI only; exercises retail RotWK 2.01 / BFME2 1.06 code path (fork issue 804).

#include "doctest.h"

#include "EconTestUtil.h"

#include "Common/Upgrade.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SlaveWatcherBehavior.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Object/Object.h"

#include <string>

namespace
{
const char kSpikesIni[] =
	"Upgrade L120RazorSpines\n"
	"  Type = OBJECT\n"
	"End\n"
	"Upgrade L120FreeSpikes\n"
	"  Type = OBJECT\n"
	"End\n"
	"Object L120Spikes\n"
	"  KindOf = STRUCTURE IMMOBILE NOT_AUTOACQUIRABLE UNATTACKABLE\n"
	"  Body = HighlanderBody ModuleTag_Body\n"
	"    MaxHealth = 1.0\n"
	"  End\n"
	"  Behavior = SlavedUpdate ModuleTag_Slave\n"
	"    DieOnMastersDeath = Yes\n"
	"  End\n"
	"End\n"
	"Object L120FreeSpikesObj\n"
	"  KindOf = STRUCTURE IMMOBILE NOT_AUTOACQUIRABLE UNATTACKABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = SlavedUpdate ModuleTag_Slave\n"
	"    DieOnMastersDeath = No\n"
	"  End\n"
	"End\n"
	"Object L120Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000.0\n"
	"  End\n"
	"  Behavior = SlaveWatcherBehavior WatchTheDrake\n"
	"    LetSlaveLive = Yes\n"
	"  End\n"
	"  Behavior = ObjectCreationUpgrade CreateTheSpikes\n"
	"    TriggeredBy = L120RazorSpines\n"
	"    Delay = 0.0\n"
	"    ThingToSpawn = L120Spikes\n"
	"  End\n"
	"  Behavior = ObjectCreationUpgrade CreateFreeSpikes\n"
	"    TriggeredBy = L120FreeSpikes\n"
	"    Delay = 0.0\n"
	"    ThingToSpawn = L120FreeSpikesObj\n"
	"  End\n"
	"End\n";

Object *findFirstByTemplate(GameLogic &logic, const std::string &name)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() && o->getTemplate()->getName() == name)
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace

// Expectations from RotWK 2.01 RW 0x8B8464 (RW 0x8B86DB .. 0x8B8708 -> SlavedUpdateInterface::onEnslave RW 0x8A510B)
// and SlavedUpdate::update RW 0x8A5D00 (DieOnMastersDeath -> kill RW 0x8A5DA6); DONOR BFME2 0x004B42DB.
TEST_CASE("LOCAL-120 upstream: ObjectCreationUpgrade enslaves spawned SlavedUpdate objects so DieOnMastersDeath kills them on creator death (RW 0x8B86DB .. 0x8B8708)")
{
	econtest::Fx f(kSpikesIni);
	Object *fortress = f.makeAt("L120Fortress", "Alice", 100.0f, 100.0f);
	REQUIRE(fortress != nullptr);
	const ObjectID fortId = fortress->getID();

	const UpgradeTemplate *utSpikes = TheUpgradeCenter->findUpgrade("L120RazorSpines");
	const UpgradeTemplate *utFree = TheUpgradeCenter->findUpgrade("L120FreeSpikes");
	REQUIRE(utSpikes != nullptr);
	REQUIRE(utFree != nullptr);
	fortress->giveUpgrade(utSpikes);
	fortress->giveUpgrade(utFree);
	f.frames(3);

	Object *spikes = findFirstByTemplate(*f.logic, "L120Spikes");
	Object *freeSpikes = findFirstByTemplate(*f.logic, "L120FreeSpikesObj");
	REQUIRE(spikes != nullptr);
	REQUIRE(freeSpikes != nullptr);
	const ObjectID spikesId = spikes->getID();
	const ObjectID freeId = freeSpikes->getID();

	SlavedUpdateInterface *slavedSpikes = SlavedUpdateInterface::of(*spikes);
	SlavedUpdateInterface *slavedFree = SlavedUpdateInterface::of(*freeSpikes);
	REQUIRE(slavedSpikes != nullptr);
	REQUIRE(slavedFree != nullptr);
	CHECK(slavedSpikes->getSlaverID() == fortId);
	CHECK(slavedFree->getSlaverID() == fortId);

	fortress->kill(DAMAGE_UNRESISTABLE, 0);
	f.frames(10);

	Object *spikesAfter = f.logic->findObjectByID(spikesId);
	CHECK((spikesAfter == nullptr || spikesAfter->isEffectivelyDead()));

	Object *freeAfter = f.logic->findObjectByID(freeId);
	REQUIRE(freeAfter != nullptr);
	CHECK_FALSE(freeAfter->isEffectivelyDead());
}
