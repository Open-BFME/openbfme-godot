// OpenBFME. COMBAT-2 tests: every field the structure lane added to the simulation is part of the deterministic world hash. Each case changes ONE field of an otherwise identical world
// and requires the hash to change (a mutation test, like test_combat_hash.cpp): a field the hash misses would let two peers of a lockstep game desync silently. Synthetic data.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/VictoryConditions.h"

using namespace combattest;

namespace
{
const char kHashObjects[] =
	"Armor HArmor\n"
	"  Armor = DEFAULT 100%\n"
	"End\n"
	"Object HKeep\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    MaxHealthDamaged = 600\n"
	"    MaxHealthReallyDamaged = 300\n"
	"  End\n"
	"  Behavior = StructureCollapseUpdate ModuleTag_Collapse\n"
	"    MinBurstDelay = 250\n"
	"    MaxBurstDelay = 800\n"
	"    CollapseDamping = 0.5\n"
	"    MaxShudder = 0.6\n"
	"    BigBurstFrequency = 4\n"
	"    DestroyObjectWhenDone = Yes\n"
	"    CollapseHeight = 155\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_Castle\n"
	"  End\n"
	"End\n"
	"Object HTroll\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = DelayedDeathBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    DelayedDeathTime = 1000\n"
	"  End\n"
	"  Behavior = LifetimeUpdate ModuleTag_Life\n"
	"    MinLifetime = 600000\n"
	"    MaxLifetime = 600000\n"
	"  End\n"
	"End\n"
	"Object HProxy\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = SymbioticStructuresBody ModuleTag_Body\n"
	"    Symbiote = HKeep\n"
	"  End\n"
	"End\n"
	"Object HProp\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = InactiveBody ModuleTag_Body\n"
	"  End\n"
	"End\n";

struct HashWorld : CombatWorld
{
	Object *keep = nullptr, *troll = nullptr, *proxy = nullptr, *prop = nullptr, *attacker = nullptr;
	HashWorld()
		: CombatWorld(kHashObjects)
	{
		combat().setAutoAcquireEnabled(false);
		attacker = unit("Swordsman", 'A', 200, 300);
		keep = unit("HKeep", 'B', 400, 300);
		troll = unit("HTroll", 'B', 440, 300);
		proxy = unit("HProxy", 'B', 480, 300);
		prop = unit("HProp", 'B', 520, 300);
		frames(3);
	}
};

#define MUTATE(NAME, STATEMENT)                                                                                                                           \
	do                                                                                                                                                    \
	{                                                                                                                                                     \
		HashWorld WORLD;                                                                                                                                  \
		const std::uint32_t before = WORLD.hash();                                                                                                        \
		{                                                                                                                                                 \
			HashWorld &w = WORLD;                                                                                                                         \
			(void)w;                                                                                                                                      \
			STATEMENT;                                                                                                                                    \
		}                                                                                                                                                 \
		CHECK_MESSAGE(WORLD.hash() != before, NAME);                                                                                                      \
	} while (0)

DamageInfo killShot(const Object *by)
{
	DamageInfo d;
	d.m_input.m_sourceID = by->getID();
	d.m_input.m_amount = 1.0e9f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	return d;
}
} // namespace

TEST_CASE("structure hash: two identical worlds have the same hash")
{
	HashWorld a, b;
	CHECK(a.hash() == b.hash());
}

TEST_CASE("structure hash: the body fields - the constructor id, the delayed death flags, the symbiote, the dead InactiveBody")
{
	MUTATE("StructureBody constructor object id", dynamic_cast<StructureBody *>(w.keep->getBodyModule())->setConstructorObject(w.attacker));
	MUTATE("DelayedDeathBody checked", dynamic_cast<DelayedDeathBody *>(w.troll->getBodyModule())->setChecked(true));
	MUTATE("DelayedDeathBody started (the delay begins)", { DamageInfo d = killShot(w.attacker); w.troll->attemptDamage(d); });
	MUTATE("SymbioticStructuresBody symbiote", dynamic_cast<SymbioticStructuresBody *>(w.proxy->getBodyModule())->setSymbiote(w.keep->getID()));
	MUTATE("InactiveBody died", { DamageInfo d = killShot(w.attacker); w.prop->getBodyModule()->attemptDamage(d); });
}

TEST_CASE("structure hash: the damage state, the collapse and the lifetime")
{
	MUTATE("damage state DAMAGED", { DamageInfo d; d.m_input.m_amount = 500.0f; d.m_input.m_damageType = DAMAGE_UNRESISTABLE; w.keep->attemptDamage(d); });
	MUTATE("the collapse begins", { DamageInfo d = killShot(w.attacker); w.keep->attemptDamage(d); });
	{
		HashWorld a, b;
		DamageInfo d = killShot(a.attacker);
		a.keep->attemptDamage(d);
		DamageInfo e = killShot(b.attacker);
		b.keep->attemptDamage(e);
		a.frames(3);
		b.frames(4);
		CHECK(a.hash() != b.hash()); // the fall (height, velocity, position, frames) moves the hash on
	}
	MUTATE("the lifetime range", dynamic_cast<LifetimeUpdate *>(w.troll->findModule("LifetimeUpdate"))->setLifetimeRange(7, 7));
	MUTATE("the castle member hit", { DamageInfo d; d.m_input.m_amount = 100.0f; d.m_input.m_damageType = DAMAGE_UNRESISTABLE; w.keep->attemptDamage(d); });
}

TEST_CASE("structure hash: the settings the structure lane reads and the player's defeat frame")
{
	MUTATE("Gravity", w.logic->settings().gravity = -9.0f);
	MUTATE("structureRulesLoaded", w.logic->settings().structureRulesLoaded = !w.logic->settings().structureRulesLoaded);
	MUTATE("DefaultStructureRubbleHeight", w.logic->settings().defaultStructureRubbleHeight = 3.0f);
	MUTATE("victoryRulesLoaded", w.logic->settings().victoryRulesLoaded = !w.logic->settings().victoryRulesLoaded);
	MUTATE("SecondsBeforeBaseCheckActive", w.logic->settings().secondsBeforeBaseCheckActive = 9.0f);
	MUTATE("the victory structure filter", w.logic->settings().victoryStructureFilter = std::make_shared<const ObjectFilter>(ObjectFilter::all(KindOfMaskType{})));
	MUTATE("the victory unit filter", w.logic->settings().victoryUnitFilter = std::make_shared<const ObjectFilter>(ObjectFilter::all(KindOfMaskType{})));
	MUTATE("Player defeat frame", w.playerOf('B')->setDefeatFrame(77));
	MUTATE("Player defeated flag", w.playerOf('B')->setDefeated(true));
}

TEST_CASE("structure hash: the combat counters of the structure lane")
{
	{
		DamageInfo d;
		d.m_input.m_amount = 1.0e9f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		HashWorld a, b;
		a.keep->attemptDamage(d); // the collapse starts: rubbleEntered, collapsesBegun, effects
		CHECK(a.combat().counters().rubbleEntered == 1);
		CHECK(a.combat().counters().collapsesBegun == 1);
		CHECK(a.hash() != b.hash());
	}
}
