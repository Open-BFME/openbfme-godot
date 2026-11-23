// PORT-45 (issue fork issue 149): ActiveBody's DamageCreationList — the objects a body creates when a hit carries one of its damage
// triggers. Expected values are read from the RotWK 2.01 binary, each check names its address (ports/PORT-45.md):
//   * parser RW 0x8C3F1F: one line = <OCL> <FXTrigger> [<death side>]; the OCL through RW 0x73A368 / RW 0x5EFFE7 (NULL for a
//     name the store lacks), the FXTrigger through scanIndexList over RW 0xDB2AF4, the optional death side over RW 0xDB2B04
//     (0 when absent, RW 0x8C3F68 / 0x8C3F7E); the 12-byte record goes to the data's vector (RW 0x8C3F85 / RW 0x6E0A42).
//   * reader RW 0x8C2F75 .. 0x8C2FB8 in doDamageFX (RW 0x8C2F02): a record whose +0x04 equals the hit's DamageInfo +0x2C
//     (input +0x28 = the weapon's FXTrigger), whose +0x08 is 0 and whose +0x00 is not NULL creates its ObjectCreationList on
//     this object (RW 0x5F0126). The loop is reached from every FX-block return (RW 0x8C2F75 after the `je`s).
// Data are synthetic; the object names are the port's test objects, the OCL / trigger / side names are the binary's.
// GPL-3.0.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectCreationList.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
// PORT-45 data. The OCLs are loaded after the store's block is registered (Port45World), the victims right after them, because a victim's
// line resolves its ObjectCreationList when it is parsed (RW 0x8C3F37 -> RW 0x73A368)
const char kPort45Ocls[] =
	"ObjectCreationList Port45Chunk\n"
	"  CreateObject\n"
	"    ObjectNames = Arrow\n" // the port's Arrow: an ActiveBody and a DestroyDie, no AI, no weapon
	"    Count = 2\n"
	"  End\n"
	"End\n"
	"ObjectCreationList Port45Other\n"
	"  CreateObject\n"
	"    ObjectNames = Arrow\n"
	"    Count = 1\n"
	"  End\n"
	"End\n";

// one line per trigger and side. A stone-thrower hit (FXTrigger CATAPULT_ROCK, index 1) makes the wall's first two; the trebuchet's
// (index 2) makes the third, whose death side is not 0 and which this reader leaves alone; the line whose OCL the store lacks makes nothing
const char kPort45Objects[] =
	"Object Port45Wall\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    DamageCreationList = Port45Chunk CATAPULT_ROCK\n"
	"    DamageCreationList = Port45Other NONE\n"
	"    DamageCreationList = Port45Chunk TREBUCHET_ROCK FRONT_DESTROYED\n"
	"    DamageCreationList = Port45Missing CATAPULT_ROCK\n"
	"  End\n"
	"End\n"
	// the same list on a plain ActiveBody: the loop belongs to the body, not to StructureBody
	"Object Port45Target\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    DamageCreationList = Port45Other CATAPULT_ROCK\n"
	"  End\n"
	"End\n";

// the world with an ObjectCreationList store. CombatWorld's constructor loads its objects before this one runs, so the store's block is
// registered for that load as well (the victim's lines resolve their OCLs at parse time, RW 0x8C3F37 -> RW 0x73A368)
struct Port45StoreBeforeLoad
{
	ObjectCreationListStore *savedStore = TheObjectCreationListStore;
	explicit Port45StoreBeforeLoad(ObjectCreationListStore &store, objtest::World &w)
	{
		TheObjectCreationListStore = &store;
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
	}
	~Port45StoreBeforeLoad() { TheObjectCreationListStore = savedStore; }
};

struct Port45World : CombatWorld
{
	ObjectCreationListStore store{ w.keys };
	Port45StoreBeforeLoad storeScope{ store, w };

	// CombatWorld loads its own objects/weapons/armour; the victims are loaded here, after the store's block is registered and after
	// the OCLs, because a victim's line resolves its ObjectCreationList at the moment it is parsed (RW 0x8C3F37 -> RW 0x73A368)
	explicit Port45World() : CombatWorld()
	{
		std::string err = w.load(kPort45Ocls, INI_LOAD_OVERWRITE, "port45-ocl.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kPort45Objects, INI_LOAD_OVERWRITE, "port45.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}

	// the objects of a template the world holds now (the OCL's creations of this frame included)
	int countOf(const std::string &templateName) const
	{
		int n = 0;
		for (const Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate() && o->getTemplate()->getName() == templateName)
			{
				++n;
			}
		}
		return n;
	}
};

// a hit of `amount` carrying `trigger` (the weapon's FXTrigger, DamageInfo input +0x28), from `source`
DamageInfo port45Hit(const Object *source, float amount, int trigger)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = DAMAGE_SIEGE; // the stone throwers' damage type
	d.m_input.m_fxTrigger = trigger;
	return d;
}
} // namespace

TEST_CASE("PORT-45 parse: DamageCreationList keeps OCL, FXTrigger and death side as RW 0x8C3F1F stores them (12-byte record, RW 0x8C3F85)")
{
	Port45World w;
	const ActiveBodyModuleData *d = dynamic_cast<const ActiveBodyModuleData *>(w.w.get("Port45Wall")->behaviorModules().find("ModuleTag_Body")->data.get());
	REQUIRE(d);
	REQUIRE(d->m_damageCreationList.size() == 4);
	// the OCL resolves through the store (RW 0x73A368); the trigger and side are the index lists' values (RW 0xDB2AF4 / RW 0xDB2B04)
	CHECK(d->m_damageCreationList[0].m_oclName == "Port45Chunk");
	CHECK(d->m_damageCreationList[0].m_fxTrigger == 1); // CATAPULT_ROCK, RW 0xDB2AF8
	CHECK(d->m_damageCreationList[0].m_deathSide == 0); // the third token absent: 0 (RW 0x8C3F7E)
	CHECK(d->m_damageCreationList[1].m_fxTrigger == 0); // NONE, RW 0xDB2AF4
	CHECK(d->m_damageCreationList[2].m_fxTrigger == 2); // TREBUCHET_ROCK, RW 0xDB2AFC
	CHECK(d->m_damageCreationList[2].m_deathSide == 1); // FRONT_DESTROYED, RW 0xDB2B08
	// a name the store lacks is NULL in RotWK (RW 0x5EFFE7): an empty name, the line itself stays (RW 0x8C3F88)
	CHECK(d->m_damageCreationList[3].m_oclName.empty());
	CHECK(d->m_damageCreationList[3].m_fxTrigger == 1);
	// the same table on a plain ActiveBody
	const ActiveBodyModuleData *body = dynamic_cast<const ActiveBodyModuleData *>(w.w.get("Port45Target")->behaviorModules().find("ModuleTag_Body")->data.get());
	REQUIRE(body);
	REQUIRE(body->m_damageCreationList.size() == 1);
	CHECK(body->m_damageCreationList[0].m_oclName == "Port45Other");
}

TEST_CASE("PORT-45 runtime: a hit carrying CATAPULT_ROCK makes the body's DamageCreationList objects, another trigger makes none (RW 0x8C2F75)")
{
	Port45World w;
	Object *attacker = w.unit("Swordsman", 'A', 300, 300);
	Object *wall = w.unit("Port45Wall", 'B', 340, 300);
	Object *target = w.unit("Port45Target", 'B', 340, 340);
	w.frames(2);
	const int arrows = w.countOf("Arrow");
	// RW 0x8C2F88: the record's +0x04 against the hit's +0x2C. The stone thrower's trigger (1) matches the wall's CATAPULT_ROCK line
	// (Port45Chunk, 2 objects) and its Port45Missing line (whose OCL the store lacks: NULL, RW 0x8C2F97), and nothing else: the wall's
	// NONE line (trigger 0) and its TREBUCHET_ROCK line (trigger 2) do not match
	DamageInfo stone = port45Hit(attacker, 10.0f, 1);
	wall->attemptDamage(stone);
	CHECK(w.countOf("Arrow") == arrows + 2);
	// the same trigger on the plain ActiveBody: Port45Other's single object
	DamageInfo stoneOnTarget = port45Hit(attacker, 10.0f, 1);
	target->attemptDamage(stoneOnTarget);
	CHECK(w.countOf("Arrow") == arrows + 3);
	// RW 0x8C2F8B: a different trigger matches different records. Trigger 0 (a weapon with no FXTrigger) matches the wall's NONE line only:
	// one object, not the two of the CATAPULT_ROCK line
	DamageInfo none = port45Hit(attacker, 10.0f, 0);
	wall->attemptDamage(none);
	CHECK(w.countOf("Arrow") == arrows + 4);
	// RW 0x8C2F92: the record whose death side is 1 (FRONT_DESTROYED, the wall's TREBUCHET_ROCK line) is not created here
	DamageInfo trebuchet = port45Hit(attacker, 10.0f, 2);
	wall->attemptDamage(trebuchet);
	CHECK(w.countOf("Arrow") == arrows + 4);
}

TEST_CASE("PORT-45 runtime: the loop is not part of the FX block - a body without an armour set still creates its objects (RW 0x8C2F75)")
{
	Port45World w;
	Object *attacker = w.unit("Swordsman", 'A', 300, 300);
	Object *target = w.unit("Port45Target", 'B', 340, 300); // CYLINDER geometry, no ArmorSet: doDamageFX's FX branch has no DamageFX
	w.frames(2);
	const int arrows = w.countOf("Arrow");
	DamageInfo stone = port45Hit(attacker, 10.0f, 1);
	target->attemptDamage(stone);
	CHECK(w.countOf("Arrow") == arrows + 1); // Port45Other's single Arrow
	// the hit still went through the damage pipeline: the health moved by the full amount
	CHECK(target->getBodyModule()->getHealth() == 990.0f);
}
