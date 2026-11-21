// PORT-249 (fork issue 577): OCLSpecialPower's own answer for the special power interface's slot 0x48 — the class's location requirements. Synthetic INI,
// no ROTWK_INSTALL / OPENBFME_ENNORATH_INSTALL. The expectations are the RotWK 2.01 binary's rules, read linearly with tools/mod/rw.py dis on game.dat:
//   * RW 0x8C744B is the class's slot 0x48 (interface table RW 0xC73890; the base is RW 0x897E01). It first asks the object's disabled mask: `mov ecx,
//     [esi - 8]` (the object), `add ecx, 0x1C8`, `call 0x9325B4` — RW 0x9325B4 tests the one word's bits — and a set bit answers no (RW 0x8C7460).
//   * then, with the list RW 0x8C7393, the location and the object present (RW 0x8C746D .. 0x8C747E), RW 0x8C72C3 forwards the (object, location) pair to
//     RW 0x5EFB95, which folds an AND over the list's nuggets (`call dword ptr [eax + 0x14]`, the nugget vtable RW 0xBF72F8 slot 5 = RW 0x5F08C2); a nugget
//     that answers no answers no (RW 0x8C748D). Only after that does the base RW 0x897E01 run (RW 0x8C7492).
//   * RW 0x5F08C2's ported part: `cmp dword ptr [esi + 0x28], 1` (Count) unequal answers yes (RW 0x5F08D5); the byte + 0xAB (SpreadFormation) set answers
//     yes (RW 0x5F08DB); an empty ObjectNames vector answers no (RW 0x5F08EB); a first name that is no template answers no (RW 0x5F08FF .. 0x5F0916).
// GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/INI.h"
#include "Common/SpecialPower.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectTypes.h"

#include <string>

namespace
{
// the shape of an retail OCL special power (data/ini/specialpower.ini: SpecialPower with LIMIT_DISTANCE, and object/…/*.ini: OCLSpecialPower with an
// OCL and CREATE_AT_LOCATION). Four powers on one caster, one per rule of RW 0x8C744B / RW 0x5F08C2 that the tests read.
const char kObjects[] =
	"SpecialPower Port249Power\n"
	"  Enum = SPECIAL_SPELL_BOOK_BOMBARD\n"
	"  ReloadTime = 1000\n"
	"  Flags = LIMIT_DISTANCE\n"
	"  MaxCastRange = 9999\n"
	"End\n"
	"SpecialPower Port249PowerEmpty\n"
	"  Enum = SPECIAL_SPELL_BOOK_BOMBARD\n"
	"  ReloadTime = 1000\n"
	"  Flags = LIMIT_DISTANCE\n"
	"  MaxCastRange = 9999\n"
	"End\n"
	"SpecialPower Port249PowerCount2\n"
	"  Enum = SPECIAL_SPELL_BOOK_BOMBARD\n"
	"  ReloadTime = 1000\n"
	"  Flags = LIMIT_DISTANCE\n"
	"  MaxCastRange = 9999\n"
	"End\n"
	"SpecialPower Port249PowerShort\n"
	"  Enum = SPECIAL_SPELL_BOOK_BOMBARD\n"
	"  ReloadTime = 1000\n"
	"  Flags = LIMIT_DISTANCE\n"
	"  MaxCastRange = 50\n"
	"End\n"
	"ObjectCreationList Port249OCL\n"
	"  CreateObject\n"
	"    ObjectNames = Port249Chest\n"
	"    Count = 1\n"
	"  End\n"
	"End\n"
	"ObjectCreationList Port249OCLEmpty\n"
	"  CreateObject\n"
	"    Count = 1\n"
	"  End\n"
	"End\n"
	"ObjectCreationList Port249OCLCount2\n"
	"  CreateObject\n"
	"    Count = 2\n"
	"  End\n"
	"End\n"
	"Object Port249Caster\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_OCL\n"
	"    SpecialPowerTemplate = Port249Power\n"
	"    OCL = Port249OCL\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_OCLEmpty\n"
	"    SpecialPowerTemplate = Port249PowerEmpty\n"
	"    OCL = Port249OCLEmpty\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_OCLCount2\n"
	"    SpecialPowerTemplate = Port249PowerCount2\n"
	"    OCL = Port249OCLCount2\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_OCLShort\n"
	"    SpecialPowerTemplate = Port249PowerShort\n"
	"    OCL = Port249OCL\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"  End\n"
	"End\n"
	"Object Port249Chest\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct OclFx : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	ObjectCreationListStore ocls{ w.keys };
	ObjectCreationListStore *savedOcl = TheObjectCreationListStore;
	Object *caster = nullptr;

	OclFx()
	{
		TheSpecialPowerStore = &powers;
		TheObjectCreationListStore = &ocls;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(kObjects, INI_LOAD_OVERWRITE, "port249.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~OclFx()
	{
		TheSpecialPowerStore = savedPowers;
		TheObjectCreationListStore = savedOcl;
	}

	void scene()
	{
		caster = spawn("Port249Caster", 100.0f, 100.0f, 0.0f, teamOf("Bob"));
		REQUIRE(caster);
	}

	const SpecialPowerTemplate *power(const char *name)
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(name);
		REQUIRE(t);
		return t;
	}

	// the module whose data names `oclName`
	OCLSpecialPower *moduleOf(const std::string &oclName)
	{
		for (const std::unique_ptr<BehaviorModule> &m : caster->modules())
		{
			if (OCLSpecialPower *o = dynamic_cast<OCLSpecialPower *>(m.get()))
			{
				if (o->data() && o->data()->m_ocl == oclName)
				{
					return o;
				}
			}
		}
		return nullptr;
	}

	// the module that answers for `powerName` (two modules may share one OCL list)
	OCLSpecialPower *moduleFor(const char *powerName)
	{
		const SpecialPowerTemplate *t = power(powerName);
		for (const std::unique_ptr<BehaviorModule> &m : caster->modules())
		{
			if (OCLSpecialPower *o = dynamic_cast<OCLSpecialPower *>(m.get()))
			{
				if (o->data() && o->getSpecialPowerTemplate() == t)
				{
					return o;
				}
			}
		}
		return nullptr;
	}

	bool allowed(const char *powerName, const Coord3D &loc)
	{
		return ActionManager::canDoSpecialPowerAtLocation(*caster, loc, power(powerName), 0);
	}
};
} // namespace

TEST_CASE("PORT-249: an OCL special power answers no at a location while the caster's disabled mask has a bit (RW 0x8C744B, RW 0x9325B4)")
{
	OclFx f;
	f.scene();
	const Coord3D spot{ 400.0f, 400.0f, 0.0f };

	// RW 0x8C745E: no disabled bit, the list's nuggets accept and the base passes
	CHECK(f.allowed("Port249Power", spot));

	// RW 0x8C7457 -> RW 0x9325B4 -> RW 0x8C7460: one bit of Object + 0x1C8 answers no before the list and the base are asked
	f.caster->setDisabled(DISABLED_HELD, f.logic->getFrame() + 100);
	CHECK(f.caster->getDisabledMask() == (DisabledMaskType)(1u << DISABLED_HELD));
	CHECK_FALSE(f.allowed("Port249Power", spot));

	// RW 0x692443: the bit cleared, the answer comes back
	f.caster->clearDisabled(DISABLED_HELD);
	CHECK(f.caster->getDisabledMask() == 0);
	CHECK(f.allowed("Port249Power", spot));
}

TEST_CASE("PORT-249: the list's nuggets are folded with an AND and RW 0x5F08C2's Count and ObjectNames rules answer (RW 0x8C72C3, RW 0x5EFB95)")
{
	OclFx f;
	f.scene();
	const Coord3D spot{ 400.0f, 400.0f, 0.0f };

	// RW 0x5F08EB: a CreateObject nugget without ObjectNames answers no, so the whole list does (RW 0x8C748D)
	OCLSpecialPower *empty = f.moduleOf("Port249OCLEmpty");
	REQUIRE(empty);
	CHECK_FALSE(f.allowed("Port249PowerEmpty", spot));
	CHECK(empty->unportedPlacementTests() == 0); // it stopped before the placement branch

	// RW 0x5F08D5: a Count other than 1 answers yes before the names are read, so the same nameless list passes
	CHECK(f.allowed("Port249PowerCount2", spot));

	// RW 0x5F091B is reached for the one-name list and its placement branch is not ported: counted, the nugget answers yes
	OCLSpecialPower *one = f.moduleFor("Port249Power");
	REQUIRE(one);
	CHECK(f.allowed("Port249Power", spot));
	CHECK(one->unportedPlacementTests() == 1);
}

TEST_CASE("PORT-249: the base location requirements still run after the class's own (RW 0x897E01, RW 0x82D3A3)")
{
	OclFx f;
	f.scene();

	// RW 0x82D3A3: LIMIT_DISTANCE refuses a location beyond MaxCastRange^2 (the other three powers' 9999 keep the far spot legal)
	CHECK(f.allowed("Port249Power", Coord3D{ 400.0f, 400.0f, 0.0f }));
	CHECK_FALSE(f.allowed("Port249PowerShort", Coord3D{ 400.0f, 400.0f, 0.0f }));
	CHECK(f.allowed("Port249PowerShort", Coord3D{ 120.0f, 120.0f, 0.0f }));
}
