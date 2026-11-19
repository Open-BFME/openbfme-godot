// OpenBFME. PORT-9 (issue #62): TransportContain/CollidePickup - OpenContainModuleData + 0x81, row RW 0xC5A020 of RW's OpenContain field table RW 0xC59F30,
// parser RW 0x42E558 (INI::parseBool) and the constructor's default 1 at RW 0x867ECF.
//
// RotWK 2.01 installs the OpenContain collide interface (subobject at module + 0x24, vtable RW 0xC5D650, answered by BehaviorModuleInterface slot +0x04,
// getCollide RW 0x64F3EA) in OpenContain (RW 0x867FC1), TransportContain (RW 0x86B6CF), HordeTransportContain (RW 0x87A2CF), SiegeEngineContain (RW 0x87F525)
// and HordeSiegeEngineContain (RW 0x88051C) - every class that derives from OpenContain here. When two objects overlap, RW's contact pass calls
// Object::onCollide (RW 0x68EEC3, Collidable vtable RW 0xC12368 slot +0x24), which calls each collide module's slot +0x00 (RW 0x68EF1E); the port's
// AIWorld::processCollisions calls ObjectCollide::onCollide, which does the same through CollideModuleInterface::onCollide.
//
// OpenContain::onCollide (RW 0x8659D8 .. 0x865A51):
//   * RW 0x8659E0: !other -> return;
//   * RW 0x8659EB: container->isKindOf(SHIP) (template + 0x11F bit 0x80) -> return;
//   * RW 0x8659F4 .. 0x865A08: other's AI (Object + 0x260) must exist and its vslot 0x1FC (RW 0x664D12) must name this container;
//   * RW 0x865A11 .. 0x865A16: module vslot 0x4C (tryToEvacuate, RW 0x867C40) must return true;
//   * RW 0x865A23 .. 0x865A2B: contain vslot 0x98 isValidContainerFor(other, true, false) must return true;
//   * RW 0x865A2D .. 0x865A33: other->getContainedBy() (Object + 0x27C) != container;
//   * RW 0x865A38 .. 0x865A3F: CollidePickup (+ 0x81) != 0 -> RW 0x865A48 contain vslot 0x9C addToContain(other); 0 returns without adding.
//
// Synthetic data (combattest::CombatWorld), no retail files, no Ennorath files. GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <cmath>
#include <string>

using namespace combattest;

namespace
{
const char kPort9Objects[] =
	"Object Port9Rider\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 25\n"
	"  End\n"
	"End\n"
	"Object Port9StealthRider\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE STEALTH_GARRISON\n"
	"  VisionRange = 200\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 200\n"
	"    InnateStealth = Yes\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 25\n"
	"  End\n"
	"End\n"
	"Object Port9StealthNoAI\n"
	"  TransportSlotCount = 1\n"
	"  KindOf = INFANTRY SELECTABLE STEALTH_GARRISON\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = StealthUpdate ModuleTag_Stealth\n"
	"    StealthDelay = 200\n"
	"    InnateStealth = Yes\n"
	"  End\n"
	"End\n";

std::string makeTransportIni(const char *name, const char *containClass, const char *extraContainFields, const char *kindOf = "VEHICLE SELECTABLE TRANSPORT")
{
	return std::string("Object ") + name + "\n"
		"  KindOf = " + kindOf + "\n"
		"  VisionRange = 200\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 25\n"
		"  GeometryMinorRadius = 25\n"
		"  GeometryHeight = 30\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = PlainArmor\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 500\n"
		"  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"    AutoAcquireEnemiesWhenIdle = No\n"
		"  End\n"
		"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
		"  End\n"
		"  Behavior = " + containClass + " ModuleTag_Contain\n"
		"    Slots = 2\n"
		"    PassengerFilter = ANY +INFANTRY\n"
		"    AllowAlliesInside = Yes\n"
		"    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n" +
		extraContainFields +
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 20\n"
		"  End\n"
		"End\n";
}

const std::string kAllPort9Objects = std::string(kPort9Objects) +
	makeTransportIni("Port9TransportYes", "TransportContain", "") +
	makeTransportIni("Port9TransportNo", "TransportContain", "    CollidePickup = No\n") +
	makeTransportIni("Port9HordeTransportYes", "HordeTransportContain", "") +
	makeTransportIni("Port9HordeTransportNo", "HordeTransportContain", "    CollidePickup = No\n") +
	makeTransportIni("Port9SiegeTransportYes", "SiegeEngineContain", "") +
	makeTransportIni("Port9SiegeTransportNo", "SiegeEngineContain", "    CollidePickup = No\n") +
	makeTransportIni("Port9ShipTransport", "TransportContain", "", "VEHICLE SELECTABLE TRANSPORT SHIP") +
	makeTransportIni("Port9OneSlotTransport", "TransportContain", "    Slots = 1\n") +
	makeTransportIni("Port9SharedTransport", "TransportContain", "    AllowEnemiesInside = Yes\n    CollidePickup = Yes\n");

// CombatWorld's own load runs in its constructor, before StealthUpdate can be registered, so the PORT-9 objects come in a second load
struct Port9World : CombatWorld
{
	Port9World()
		: CombatWorld("")
	{
		InvisibilityModules::registerAll(w.modules); // StealthUpdate (RW 0x7767A9), not one of CombatWorld's families
		const std::string err = w.load(kAllPort9Objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
};

float dist2D(const Object &a, const Object &b)
{
	const float dx = a.getPosition()->x - b.getPosition()->x;
	const float dy = a.getPosition()->y - b.getPosition()->y;
	return std::sqrt(dx * dx + dy * dy);
}

struct BoardTrace
{
	int firstContactFrame = -1;
	int boardedFrame = -1;
	float distAtFirstContact = -1.0f;
	bool insideOnFirstContact = false;
};

// Spawns `transportTemplate` at (405, 405) (radius 25) and Port9Rider at (465, 405) (radius 5, so the contact radius is 30), orders the rider into the
// transport with aiEnterObject (AI command 0x17 -> AIEnterState, state 15) and steps frame by frame
BoardTrace runBoardingTrace(const char *transportTemplate)
{
	Port9World w;
	Object *transport = w.unit(transportTemplate, 'A', 405.0f, 405.0f);
	Object *rider = w.unit("Port9Rider", 'A', 465.0f, 405.0f);
	REQUIRE(transport != nullptr);
	REQUIRE(rider != nullptr);
	REQUIRE(transport->getContain() != nullptr);
	const float contactRadius = 25.0f + 5.0f; // the transport's and the rider's radii, which AIWorld::processCollisions adds
	REQUIRE(dist2D(*rider, *transport) == 60.0f); // spawned outside the 30 contact radius

	rider->getAIUpdateInterface()->aiEnterObject(transport, CMD_FROM_PLAYER);
	REQUIRE(rider->getAIUpdateInterface()->stateMachine().goalObject() == transport);

	BoardTrace tr;
	for (int f = 1; f <= 40; ++f)
	{
		w.logic->runLogicFrame();
		const float d = dist2D(*rider, *transport);
		const bool inside = rider->getContainedBy() == transport;
		if (tr.firstContactFrame < 0 && (inside || d < contactRadius))
		{
			tr.firstContactFrame = f;
			tr.distAtFirstContact = d;
			tr.insideOnFirstContact = inside;
		}
		if (tr.boardedFrame < 0 && inside)
		{
			tr.boardedFrame = f;
			break;
		}
	}
	return tr;
}
} // namespace

// RW 0x8659D8 .. 0x865A49: a unit ordered to enter (AIEnterState, 15) is picked up on the frame its contact is processed when CollidePickup is Yes (the
// default, RW 0x867ECF), and keeps walking to the entry point when it is No, where AIEnterState::update (RW 0x756184 .. 0x7561EF) puts it in several
// frames later. `addToContain` (RW 0x865A48) is the same call the walk ends with, so the difference is exactly the frames in between.
TEST_CASE("PORT-9 CollidePickup Yes vs No: a unit in AIEnterState is picked up on the contact frame when Yes, and walks to the entry point when No (RW 0x8659D8)")
{
	const struct Case
	{
		const char *label;
		const char *yesTemplate;
		const char *noTemplate;
	} cases[] = {
		{ "TransportContain (RW 0x86B6CF)", "Port9TransportYes", "Port9TransportNo" },
		{ "HordeTransportContain (RW 0x87A2CF)", "Port9HordeTransportYes", "Port9HordeTransportNo" },
		{ "SiegeEngineContain (RW 0x87F525)", "Port9SiegeTransportYes", "Port9SiegeTransportNo" },
	};

	for (const Case &c : cases)
	{
		SUBCASE(c.label)
		{
			// aiEnterObject (AI command 0x17, RW 0x664579) puts the rider in AIEnterState (15), which RW 0x664D12 counts as entering
			const BoardTrace yes = runBoardingTrace(c.yesTemplate);
			const BoardTrace no = runBoardingTrace(c.noTemplate);
			INFO(c.label, ": yes contact=", yes.firstContactFrame, " boarded=", yes.boardedFrame, " d=", yes.distAtFirstContact,
				" / no contact=", no.firstContactFrame, " boarded=", no.boardedFrame, " d=", no.distAtFirstContact);

			// both templates move identically up to the first contact with the 30-unit combined radius
			REQUIRE(yes.firstContactFrame == 10); // the rider walks 60 -> under 30 in ten frames
			REQUIRE(no.firstContactFrame == yes.firstContactFrame);

			// CollidePickup Yes: the contact pass runs on the positions at the start of the frame, so the pickup lands one frame after the distance fell
			CHECK_FALSE(yes.insideOnFirstContact);
			CHECK(yes.boardedFrame == 11);

			// CollidePickup No: RW 0x865A3F returns on contact; the rider walks inside the footprint and enters at the entry position frames later
			CHECK_FALSE(no.insideOnFirstContact);
			CHECK(no.distAtFirstContact == 25.0f);
			CHECK(no.boardedFrame == 20);
			CHECK(no.boardedFrame - yes.boardedFrame == 9); // the pickup is what CollidePickup buys: 20 - 11
		}
	}
}


// RW 0x8659E2 .. 0x865A36, the gates: no enter order, another container as the enter target, a SHIP container and a full container all leave the
// touching unit outside
TEST_CASE("PORT-9 collide gates: no enter order, another transport, a SHIP or a full transport never pick a unit up on contact (RW 0x8659E2)")
{
	SUBCASE("touching without an enter order (RW 0x865A00 .. 0x865A08: enterTargetOf != container)")
	{
		Port9World w;
		Object *transport = w.unit("Port9TransportYes", 'A', 405.0f, 405.0f);
		Object *walker = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		const Coord3D dest{ 355.0f, 405.0f, 0.0f };
		walker->getAIUpdateInterface()->aiMoveToPosition(dest, CMD_FROM_PLAYER);

		int contactFrames = 0;
		for (int f = 0; f < 30; ++f)
		{
			w.logic->runLogicFrame();
			if (dist2D(*walker, *transport) < 30.0f)
			{
				++contactFrames;
			}
			CHECK(walker->getContainedBy() == nullptr);
			CHECK(transport->getContain()->getContainCount() == 0u);
		}
		INFO("contact frames: ", contactFrames);
		CHECK(contactFrames == 6); // the walker starts inside the 30-unit radius and leaves it on the sixth frame
	}

	SUBCASE("ordered into transport B while touching transport A (RW 0x865A06: enterTargetOf == B != A)")
	{
		Port9World w;
		Object *transportA = w.unit("Port9TransportYes", 'A', 405.0f, 405.0f);
		Object *transportB = w.unit("Port9TransportYes", 'A', 305.0f, 405.0f);
		Object *rider = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		REQUIRE(dist2D(*rider, *transportA) == 20.0f); // inside A's contact radius

		rider->getAIUpdateInterface()->aiEnterObject(transportB, CMD_FROM_PLAYER);
		REQUIRE(rider->getAIUpdateInterface()->stateMachine().goalObject() == transportB);

		w.logic->runLogicFrame(); // overlaps A while entering B: A must not pick it up
		CHECK(rider->getContainedBy() == nullptr);
		CHECK(transportA->getContain()->getContainCount() == 0u);

		for (int f = 0; f < 40 && rider->getContainedBy() == nullptr; ++f)
		{
			w.logic->runLogicFrame();
		}
		CHECK(rider->getContainedBy() == transportB); // B takes it on contact
		CHECK(transportA->getContain()->getContainCount() == 0u);
		CHECK(transportB->getContain()->getContainCount() == 1u);
	}

	SUBCASE("state 56 is not an enter target for a container that is not a SHIP (RW 0x664D47 .. 0x664D56)")
	{
		Port9World w;
		Object *transport = w.unit("Port9TransportYes", 'A', 405.0f, 405.0f);
		Object *rider = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		REQUIRE(rider->getAIUpdateInterface()->aiEnter(transport, CMD_FROM_PLAYER)); // AI command 0x42, RW 0x66D85F: state 56
		REQUIRE(rider->getAIUpdateInterface()->currentStateId() == (unsigned)AI_MOVE_TO_POSITION_AND_ENTER);
		CHECK(rider->getAIUpdateInterface()->enterTarget() == nullptr); // RW 0x664D47: the SHIP test fails here
		ObjectCollide::onCollide(*transport, *rider);
		CHECK(rider->getContainedBy() == nullptr);
		CHECK(transport->getContain()->getContainCount() == 0u);
	}

	SUBCASE("a SHIP container never picks up on contact (RW 0x8659EB)")
	{
		const BoardTrace ship = runBoardingTrace("Port9ShipTransport");
		INFO("ship contact=", ship.firstContactFrame, " boarded=", ship.boardedFrame);
		REQUIRE(ship.firstContactFrame == 10);
		CHECK_FALSE(ship.insideOnFirstContact);
		CHECK(ship.boardedFrame == 20); // it boards through AIEnterState, as the CollidePickup No case does
	}

	SUBCASE("a full container refuses the pickup (RW 0x865A23: isValidContainerFor(other, true, false))")
	{
		Port9World w;
		Object *transport = w.unit("Port9OneSlotTransport", 'A', 405.0f, 405.0f);
		Object *first = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		Object *second = w.unit("Port9Rider", 'A', 405.0f, 425.0f);
		transport->getContain()->addToContain(first);
		REQUIRE(transport->getContain()->getContainCount() == 1u);

		ObjectCollide::onCollide(*transport, *second);
		CHECK(second->getContainedBy() == nullptr);
		CHECK(transport->getContain()->getContainCount() == 1u);
	}
}

// RW 0x867C40 (module vslot 0x4C): the enemy STEALTH_GARRISON occupants are revealed and ordered out before the entering object is added
TEST_CASE("PORT-9 tryToEvacuate (vslot 0x4C, RW 0x867C40): an enemy STEALTH_GARRISON rider is revealed and ordered out when a unit collides to enter")
{
	SUBCASE("a rider with an AIUpdateInterface is marked detected and leaves via aiExit(container, CMD_FROM_AI) (RW 0x867CAC .. 0x867CE0)")
	{
		Port9World w;
		Object *transport = w.unit("Port9SharedTransport", 'A', 405.0f, 405.0f);
		Object *enemyStealth = w.unit("Port9StealthRider", 'B', 405.0f, 405.0f);
		Object *entering = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		transport->getContain()->addToContain(enemyStealth);
		REQUIRE(enemyStealth->getContainedBy() == transport);
		CHECK_FALSE(enemyStealth->testStatus((unsigned)CombatNames::status("DETECTED")));
		const size_t entriesBefore = w.logic->invisibility().entryCount();

		entering->getAIUpdateInterface()->aiEnterObject(transport, CMD_FROM_PLAYER);
		const unsigned markFrame = w.logic->getFrame();
		w.logic->runLogicFrame();

		CHECK(entering->getContainedBy() == transport); // picked up on the same contact pass
		CHECK(enemyStealth->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(w.logic->invisibility().entryCount() == entriesBefore + 1u);
		StealthUpdate *su = StealthUpdate::of(*enemyStealth);
		REQUIRE(su != nullptr);
		// the collision runs in frame markFrame + 1; StealthDelay 200 ms is one frame (ceil(ms * 0.005)); markAsDetected(0, ...) uses that delay
		INFO("markFrame=", markFrame, " expires=", su->detectionExpiresFrame());
		CHECK(su->detectionExpiresFrame() == markFrame + 2u);

		for (int f = 0; f < 10 && enemyStealth->getContainedBy() == transport; ++f)
		{
			w.logic->runLogicFrame();
		}
		CHECK(enemyStealth->getContainedBy() == nullptr);
	}

	SUBCASE("a rider without an AIUpdateInterface is revealed and removed (RW 0x867CE2 .. 0x867CEA, RW 0x865EF5 .. 0x865F22)")
	{
		Port9World w;
		Object *transport = w.unit("Port9SharedTransport", 'A', 405.0f, 405.0f);
		Object *enemyNoAI = w.unit("Port9StealthNoAI", 'B', 405.0f, 405.0f);
		Object *entering = w.unit("Port9Rider", 'A', 425.0f, 405.0f);
		transport->getContain()->addToContain(enemyNoAI);
		REQUIRE(enemyNoAI->getContainedBy() == transport);
		const size_t entriesBefore = w.logic->invisibility().entryCount();

		entering->getAIUpdateInterface()->aiEnterObject(transport, CMD_FROM_PLAYER);
		const unsigned markFrame = w.logic->getFrame();
		w.logic->runLogicFrame();
		CHECK(entering->getContainedBy() == transport);
		CHECK(enemyNoAI->getContainedBy() == nullptr);
		CHECK(enemyNoAI->testStatus((unsigned)CombatNames::status("DETECTED")));
		CHECK(w.logic->invisibility().entryCount() == entriesBefore + 1u);
		StealthUpdate *su = StealthUpdate::of(*enemyNoAI);
		REQUIRE(su != nullptr);
		CHECK(su->detectionExpiresFrame() == markFrame + 2u);
	}
}
