// OpenBFME. GPL-3.0.
// ProjectileStreamUpdate tests (both editions; RW 0x00740AE5 / 0x007409F9 / 0x00740A31).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/ProjectileStreamUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kStreamIni[] =
	"Object TestStreamObj\n"
	"  KindOf = INERT IMMOBILE\n"
	"  Behavior = ProjectileStreamUpdate ModuleTag_Stream\n"
	"  End\n"
	"End\n"
	"Object TestStreamOwner\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object TestStreamProj\n"
	"  KindOf = PROJECTILE INERT\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 ProjectileStreamUpdate: tracks circular buffer, culls destroyed projectiles, self-destructs when owner gone, and round-trips xfer")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kStreamIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *streamObj = w.unit("TestStreamObj", 'A', 100.0f, 100.0f);
		Object *owner = w.unit("TestStreamOwner", 'A', 100.0f, 100.0f);
		Object *proj1 = w.unit("TestStreamProj", 'A', 120.0f, 100.0f);
		Object *proj2 = w.unit("TestStreamProj", 'A', 140.0f, 100.0f);
		REQUIRE(streamObj != nullptr);
		REQUIRE(owner != nullptr);
		REQUIRE(proj1 != nullptr);
		REQUIRE(proj2 != nullptr);

		auto *psu = dynamic_cast<ProjectileStreamUpdate *>(streamObj->findModule("ProjectileStreamUpdate"));
		REQUIRE(psu != nullptr);

		const ObjectID streamId = streamObj->getID();
		const Coord3D targetPos{ 200.0f, 100.0f, 0.0f };
		psu->addProjectile(owner->getID(), proj1->getID(), 0, &targetPos);
		psu->addProjectile(owner->getID(), proj2->getID(), 0, &targetPos);

		// xfer round-trip with active projectiles
		StateHasher h1;
		psu->crc(h1);
		XferSave save;
		psu->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		psu->xfer(load);
		StateHasher h2;
		psu->crc(h2);
		CHECK(h1.value() == h2.value());

		// While projectiles exist, stream stays alive even if owner is destroyed
		w.logic->destroyObject(owner);
		w.frames(1);
		CHECK(w.logic->findObjectByID(streamId) != nullptr);

		// Once all projectiles are gone and owner is gone, stream destroys itself
		w.logic->destroyObject(proj1);
		w.logic->destroyObject(proj2);
		w.frames(1);
		CHECK(w.logic->findObjectByID(streamId) == nullptr);
	}
}
