// OpenBFME. LOCAL-30 tests (issue fork issue 246): the last damage record as retail:
// which hit replaces "who hit me last" (ActiveBody, RW 0x8C4294 .. 0x8C42F5).
//
// Retail RotWK rules:
// 1. RW 0x8C4294 .. 0x8C42A9: a record from the current frame (`now`) or previous frame (`now - 1`) is retained
//    unless incoming source exists and either previous source is gone or incoming source is INFANTRY, CAVALRY,
//    or one of RW 0x68DAE6's five significant-building kinds (FS_POWER, FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY, FS_CASH_PRODUCER).
//    The subtraction is unsigned: at frame 0, initial frame 0xFFFFFFFF equals `0 - 1u` (counts as previous frame).
// 2. RW 0x8C42F1 .. 0x8C4309: all parts of the record are replaced together (frame, damager, player mask, type, noEffect, m_lastAttackerTaken = false).
// 3. RW 0x8C430F .. 0x8C4341: attacker marking runs unconditionally using the record's source.
// 4. RW 0x8C425D .. 0x8C4276: scheduleRecovery runs before output amounts and record selection.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/StructureModules.h"

using namespace combattest;

namespace
{
const char kLocal30Objects[] =
	"Object TargetUnit\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object GenericMonster\n"
	"  KindOf = MONSTER\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object GenericMachine\n"
	"  KindOf = MACHINE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object CavalryAttacker\n"
	"  KindOf = CAVALRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object GenericBuilding\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object PowerBuilding\n"
	"  KindOf = STRUCTURE FS_POWER\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object FactoryBuilding\n"
	"  KindOf = STRUCTURE FS_FACTORY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object DefenseBuilding\n"
	"  KindOf = STRUCTURE FS_BASE_DEFENSE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object TechBuilding\n"
	"  KindOf = STRUCTURE FS_TECHNOLOGY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object CashBuilding\n"
	"  KindOf = STRUCTURE FS_CASH_PRODUCER\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object SymbioteHost\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_HostBody\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object SymbioteChild\n"
	"  KindOf = STRUCTURE\n"
	"  Body = SymbioticStructuresBody ModuleTag_ChildBody\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";

DamageInfo makeHit(const Object *source, float amount, int type, bool noEffect = false)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	if (source && source->getControllingPlayer())
	{
		d.m_input.m_sourcePlayerMask = 1u << source->getControllingPlayer()->getPlayerIndex();
	}
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	d.m_output.m_noEffect = noEffect;
	return d;
}
} // namespace

TEST_CASE("LOCAL-30 frame-0: sourceless hit retains initial -1 record via unsigned wrap (RW 0x8C4294 .. 0x8C42CB)")
{
	CombatWorld w(kLocal30Objects);
	REQUIRE(w.logic->getFrame() == 0);
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	REQUIRE(target != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	CHECK(body->lastDamageFrame() == 0xFFFFFFFFu);
	CHECK(body->lastDamager() == INVALID_ID);
	int type = -1;
	REQUIRE(body->lastDamageRecordType(type));
	CHECK(type == DAMAGE_UNDEFINED);

	// At frame 0: unsigned 0 - 1u == 0xFFFFFFFF == initial m_lastDamageFrame.
	// Sourceless hit: incomingSource is null -> retain old record!
	DamageInfo sourceless = makeHit(nullptr, 20.0f, DAMAGE_MAGIC);
	target->attemptDamage(sourceless);

	CHECK(body->lastDamageFrame() == 0xFFFFFFFFu);
	CHECK(body->lastDamager() == INVALID_ID);
	REQUIRE(body->lastDamageRecordType(type));
	CHECK(type == DAMAGE_UNDEFINED);

	// Now hit with an existing infantry source at frame 0:
	// incomingSource exists, previousSource is null -> record IS replaced.
	Object *swordsman = w.unit("Swordsman", 'B', 210, 200);
	REQUIRE(swordsman != nullptr);
	DamageInfo infHit = makeHit(swordsman, 30.0f, DAMAGE_SLASH);
	target->attemptDamage(infHit);

	CHECK(body->lastDamageFrame() == 0);
	CHECK(body->lastDamager() == swordsman->getID());
	REQUIRE(body->lastDamageRecordType(type));
	CHECK(type == DAMAGE_SLASH);
}

TEST_CASE("LOCAL-30 same frame: infantry then sourceless hit retains infantry; reverse replaces with infantry (RW 0x8C4294 .. 0x8C42F5)")
{
	CombatWorld w(kLocal30Objects);
	w.frames(5); // Advance to frame 5
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	Object *infantry = w.unit("Swordsman", 'B', 210, 200);
	REQUIRE(target != nullptr);
	REQUIRE(infantry != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	// Case 1: Infantry hits first, then sourceless hit in the same frame.
	DamageInfo hit1 = makeHit(infantry, 25.0f, DAMAGE_SLASH);
	target->attemptDamage(hit1);
	CHECK(body->lastDamageFrame() == 5);
	CHECK(body->lastDamager() == infantry->getID());

	DamageInfo hit2 = makeHit(nullptr, 15.0f, DAMAGE_PIERCE);
	target->attemptDamage(hit2);
	// In the same frame, previousSource (infantry) exists, incomingSource is null -> record RETAINED!
	CHECK(body->lastDamageFrame() == 5);
	CHECK(body->lastDamager() == infantry->getID());
	int type = -1;
	REQUIRE(body->lastDamageRecordType(type));
	CHECK(type == DAMAGE_SLASH);

	// Case 2: Target 2 hit by sourceless first at frame 6, then infantry in the same frame.
	w.frames(1); // frame 6
	Object *target2 = w.unit("TargetUnit", 'A', 300, 200);
	REQUIRE(target2 != nullptr);
	ActiveBody *body2 = dynamic_cast<ActiveBody *>(target2->getBodyModule());
	REQUIRE(body2 != nullptr);

	// First hit: sourceless at frame 6 (record was -1, which is != 6 and != 5, so replaced).
	DamageInfo hit3 = makeHit(nullptr, 10.0f, DAMAGE_PIERCE);
	target2->attemptDamage(hit3);
	CHECK(body2->lastDamageFrame() == 6);
	CHECK(body2->lastDamager() == 0);

	// Second hit in same frame: infantry.
	// previousSource is null, incomingSource exists -> record REPLACED with infantry!
	DamageInfo hit4 = makeHit(infantry, 20.0f, DAMAGE_SLASH);
	target2->attemptDamage(hit4);
	CHECK(body2->lastDamageFrame() == 6);
	CHECK(body2->lastDamager() == infantry->getID());
	REQUIRE(body2->lastDamageRecordType(type));
	CHECK(type == DAMAGE_SLASH);
}

TEST_CASE("LOCAL-30 same frame: generic attacker does not replace previous source, but CAVALRY and 5 building kinds do (RW 0x68DAE6)")
{
	CombatWorld w(kLocal30Objects);
	w.frames(10);
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	Object *monster = w.unit("GenericMonster", 'B', 210, 200);
	Object *machine = w.unit("GenericMachine", 'B', 220, 200);
	Object *genericBuilding = w.unit("GenericBuilding", 'B', 230, 200);
	Object *cavalry = w.unit("CavalryAttacker", 'B', 240, 200);
	REQUIRE(target != nullptr);
	REQUIRE(monster != nullptr);
	REQUIRE(machine != nullptr);
	REQUIRE(genericBuilding != nullptr);
	REQUIRE(cavalry != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	// Hit by monster
	DamageInfo h1 = makeHit(monster, 20.0f, DAMAGE_CRUSH);
	target->attemptDamage(h1);
	CHECK(body->lastDamager() == monster->getID());

	// Hit in same frame by machine: both sources exist, machine is MACHINE (not INFANTRY/CAVALRY/faction structure).
	// Record is RETAINED (monster remains damager)!
	DamageInfo h2 = makeHit(machine, 20.0f, DAMAGE_SIEGE);
	target->attemptDamage(h2);
	CHECK(body->lastDamager() == monster->getID());

	// Hit in same frame by generic structure: not one of the 5 faction structure kinds.
	// Record is RETAINED!
	DamageInfo h3 = makeHit(genericBuilding, 20.0f, DAMAGE_STRUCTURAL);
	target->attemptDamage(h3);
	CHECK(body->lastDamager() == monster->getID());

	// Hit in same frame by CAVALRY: KindOf CAVALRY (RW 0x8C42D4) -> REPLACES record!
	DamageInfo h4 = makeHit(cavalry, 30.0f, DAMAGE_CRUSH);
	target->attemptDamage(h4);
	CHECK(body->lastDamager() == cavalry->getID());

	// Now test all 5 significant building kinds (RW 0x68DAE6 Object::isFactionStructure):
	// FS_POWER, FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY, FS_CASH_PRODUCER
	const char *buildingTemplates[] = {
		"PowerBuilding",
		"FactoryBuilding",
		"DefenseBuilding",
		"TechBuilding",
		"CashBuilding"
	};
	for (const char *tmpl : buildingTemplates)
	{
		Object *bld = w.unit(tmpl, 'B', 250, 200);
		REQUIRE(bld != nullptr);
		// bld hits target in the same frame -> because it is a faction structure, it REPLACES the record!
		DamageInfo bHit = makeHit(bld, 15.0f, DAMAGE_MAGIC);
		target->attemptDamage(bHit);
		CHECK(body->lastDamager() == bld->getID());
	}
}

TEST_CASE("LOCAL-30 frame timing: record from 1 frame ago is retained against sourceless; record from 2 frames ago is replaced")
{
	CombatWorld w(kLocal30Objects);
	w.frames(10);
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	Object *infantry = w.unit("Swordsman", 'B', 210, 200);
	REQUIRE(target != nullptr);
	REQUIRE(infantry != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	// Hit at frame 10 by infantry
	DamageInfo h1 = makeHit(infantry, 20.0f, DAMAGE_SLASH);
	target->attemptDamage(h1);
	CHECK(body->lastDamageFrame() == 10);
	CHECK(body->lastDamager() == infantry->getID());

	// Advance 1 frame to frame 11 (previous frame):
	w.frames(1);
	REQUIRE(w.logic->getFrame() == 11);
	// Sourceless hit at frame 11 (now - 1 == 10 == m_lastDamageFrame):
	// Record is RETAINED!
	DamageInfo h2 = makeHit(nullptr, 10.0f, DAMAGE_PIERCE);
	target->attemptDamage(h2);
	CHECK(body->lastDamageFrame() == 10);
	CHECK(body->lastDamager() == infantry->getID());

	// Advance another frame to frame 12 (now - 1 == 11 != 10):
	w.frames(1);
	REQUIRE(w.logic->getFrame() == 12);
	// Sourceless hit at frame 12 (record from 2 frames ago):
	// Older than 1 frame -> record is REPLACED!
	DamageInfo h3 = makeHit(nullptr, 15.0f, DAMAGE_PIERCE);
	target->attemptDamage(h3);
	CHECK(body->lastDamageFrame() == 12);
	CHECK(body->lastDamager() == 0);
}

TEST_CASE("LOCAL-30 retaliation (LOCAL-20 neighbor): takeLastAttacker returns infantry when sourceless hit arrived in same frame")
{
	CombatWorld w(kLocal30Objects);
	w.frames(5);
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	Object *infantry = w.unit("Swordsman", 'B', 210, 200);
	REQUIRE(target != nullptr);
	REQUIRE(infantry != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	// Infantry hits first, then sourceless hit in same frame
	DamageInfo h1 = makeHit(infantry, 30.0f, DAMAGE_SLASH);
	target->attemptDamage(h1);
	DamageInfo h2 = makeHit(nullptr, 10.0f, DAMAGE_PIERCE);
	target->attemptDamage(h2);

	// Body's takeLastAttacker (RW 0x8C3A33) returns infantry, not INVALID_ID or 0
	ObjectID attacker = body->takeLastAttacker();
	CHECK(attacker == infantry->getID());
	// Second call returns INVALID_ID because m_lastAttackerTaken was set
	CHECK(body->takeLastAttacker() == INVALID_ID);
}

TEST_CASE("LOCAL-30 attacker marking (CAMP-1 neighbor, RW 0x8C430F .. 0x8C4341): setAttackedBy runs with record source")
{
	CombatWorld w(kLocal30Objects);
	w.frames(20);
	Object *target = w.unit("TargetUnit", 'A', 200, 200);
	Object *infantry = w.unit("Swordsman", 'B', 210, 200);
	REQUIRE(target != nullptr);
	REQUIRE(infantry != nullptr);

	Player *playerA = target->getControllingPlayer();
	Player *playerB = infantry->getControllingPlayer();
	REQUIRE(playerA != nullptr);
	REQUIRE(playerB != nullptr);

	// First hit by infantry at frame 20
	DamageInfo h1 = makeHit(infantry, 25.0f, DAMAGE_SLASH);
	target->attemptDamage(h1);
	CHECK(playerA->getAttackedBy(playerB->getPlayerIndex()));
	CHECK(playerA->attackedFrame() == 20);

	// Sourceless hit in the SAME frame (frame 20):
	// Does not replace the infantry record, and marks the player of the record's source (player B).
	DamageInfo hSame = makeHit(nullptr, 10.0f, DAMAGE_PIERCE);
	target->attemptDamage(hSame);
	CHECK(playerA->getAttackedBy(playerB->getPlayerIndex()));
	CHECK(playerA->attackedFrame() == 20);

	// Advance 2 frames to frame 22
	w.frames(2);
	// Sourceless hit at frame 22 (record replaces with damager 0):
	DamageInfo h2 = makeHit(nullptr, 10.0f, DAMAGE_PIERCE);
	target->attemptDamage(h2);
	// Because damager is 0 (no source), setAttackedBy was not called for frame 22.
	CHECK(playerA->attackedFrame() == 20);
}

TEST_CASE("LOCAL-30 SymbioticStructuresBody forwards damage record queries (RW 0x8C4FD7, 0x8C5100)")
{
	CombatWorld w(kLocal30Objects);
	w.frames(15);
	Object *host = w.unit("SymbioteHost", 'A', 200, 200);
	Object *child = w.unit("SymbioteChild", 'A', 200, 200);
	Object *attacker = w.unit("Swordsman", 'B', 220, 200);
	REQUIRE(host != nullptr);
	REQUIRE(child != nullptr);
	REQUIRE(attacker != nullptr);

	SymbioticStructuresBody *childBody = dynamic_cast<SymbioticStructuresBody *>(child->getBodyModule());
	REQUIRE(childBody != nullptr);
	childBody->setSymbiote(host->getID());

	// Hit host with an infantry hit with noEffect = true
	DamageInfo h = makeHit(attacker, 40.0f, DAMAGE_SLASH, true);
	host->attemptDamage(h);

	// Child forwards to host:
	CHECK(childBody->lastDamageFrame() == 15);
	int type = -1;
	REQUIRE(childBody->lastDamageRecordType(type));
	CHECK(type == DAMAGE_SLASH);
	bool noEffect = false;
	REQUIRE(childBody->lastDamageRecordNoEffect(noEffect));
	CHECK(noEffect);
}
