// OpenBFME unit tests. GPL-3.0.
// PORT-61 (issue fork issue 171) tests: FlammableUpdate / FireFXList — the effects an object plays when it catches fire.
//
// RotWK 2.01 (game.dat):
//   * the field row `FireFXList` RW 0xC62EA0 of the FlammableUpdate table RW 0xC62E30: parser RW 0x890CD9, offset 0 (the parser appends to the
//     vector at data + 0x28, RW 0x890D82 / 0x890D85). The parser: `FX` with colon separators (RW 0x890D0A..0x890D16: stricmp against RW 0xBF3474 = "FX",
//     else INIException 3 "'fx' expected" RW 0xC11A64), the FXList through RW 0x73A302 ("None" stores NULL, any other name must be an FXList, else
//     INIException 3 RW 0xC24F34), then the optional keyword `BONE` (stricmp against RW 0xC11A5C, RW 0x890D58..0x890D68) and the bone name with the default
//     separators (RW 0x890D76). One entry is 8 bytes: the FXList at +0, the bone's AsciiString at +4 (RW 0x890CB9 steps the vector's end by 8; the
//     element's copy RW 0x88FEA8 constructs the string at entry + 4).
//   * the reader: FlammableUpdate::tryToIgnite RW 0x8901AB. After the aflame / burned / damage frames, the swap model conditions (RW 0x8902C3 / 0x8902E3)
//     and the drawable flush (RW 0x890303..0x89031A) come the list's first and last pointers (RW 0x89031F / 0x890322): empty takes RW 0x89043A. Every
//     entry is copied into a local pair (the FXList RW 0x890337, the bone RW 0x89033C) and played at once, in list order:
//       - a bone (RW 0x89034B tests the string's length word at +4) AND an object with a drawable (RW 0x890356, RW 0x70E013 -> RW 0x67449C at
//         RW 0x89031A never returns null in retail): the local matrix starts as the identity (RW 0x890366..0x8903B2, RW 0xBD1908 = 1.0), the
//         drawable's bone transform is asked for it (RW 0x8903CD -> RW 0x672B5B, the result is not tested) and FXList::doFXPos(fx, the matrix's
//         translation, the matrix, 0 speed, no secondary) runs (RW 0x890403 -> RW 0x494615, its five arguments at RW 0x8903DF .. 0x8903FF);
//       - otherwise FXList::doFXObj(fx, the object, no secondary) (RW 0x890416 -> RW 0x4B1B5A).
//     The loop ends when the entry pointer reaches the list's end (RW 0x890431) and the custom anim's time follows (RW 0x89043A).
//
// The port keeps the list in the module data (`FlammableUpdateModuleData::m_fireFXList`, entry {fxList, bone}) and hands each entry to the existing object
// FX path at the ignition — the FXEventLog event lane FX-2 with an OBJECT_FX carrying the bone, which LiveFX plays exactly as the branch above: a bone
// with a drawable takes the bone transform and doFXPos, no bone takes doFXObj (GameClient/LiveFX.cpp, FXEvent::OBJECT_FX). No second path is added.
//
// Expected values come from the data retail 1.9.8.2 gives these fields (its objects name `FX:FX_FireStartEnt`, `FX:FX_FireStartWoosh` and
// `FX:FX_ForgeChimneySmoke BONE:FireSmall01` — the shape of the synthetic entries below), from RW's parser and from RW's ignition above, never from the
// port's own code. Synthetic INI only: no ROTWK_INSTALL, no OPENBFME_ENNORATH_INSTALL.
#include "doctest.h"
#include "MoveTestUtil.h"

#include "GameClient/FXList.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Object/Object.h"

#include <string>
#include <vector>

namespace
{
const char kPort61Lists[] =
	"FXList FX_FireStartEnt\n"
	"End\n"
	"FXList FX_FireStartWoosh\n"
	"End\n"
	"FXList FX_ForgeChimneySmoke\n"
	"End\n";

// BurningWood: two FireFXList entries (one at a bone, one on the object), as an retail forge or a burning test object has them.
// BurningWoodNoBone: the same two lists, no bone on either entry (the neighbour: both play on the object).
// BurningWoodNone: an entry whose list is `None` (RW 0x73A302 stores NULL: nothing is played) beside a real one.
// BurningWoodEmpty: no FireFXList at all (nothing is requested at ignition).
// BurningWoodOneEntry: one entry only (the count of the calls is the list's length, RW 0x890431).
const char kPort61Objects[] =
	"Object BurningWood\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    FireFXList = FX:FX_FireStartEnt BONE:FireSmall01\n"
	"    FireFXList = FX:FX_FireStartWoosh\n"
	"  End\n"
	"End\n"
	"Object BurningWoodNoBone\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    FireFXList = FX:FX_FireStartEnt\n"
	"    FireFXList = FX:FX_FireStartWoosh\n"
	"  End\n"
	"End\n"
	"Object BurningWoodNone\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    FireFXList = FX:None\n"
	"    FireFXList = FX:FX_FireStartWoosh\n"
	"  End\n"
	"End\n"
	"Object BurningWoodEmpty\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"  End\n"
	"End\n"
	"Object BurningWoodOneEntry\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    InitialHealth = 1000\n"
	"  End\n"
	"  Behavior = FlammableUpdate ModuleTag_Flame\n"
	"    AflameDuration = 0\n"
	"    FlameDamageLimit = 1000000\n"
	"    FlameDamageExpiration = 10000\n"
	"    FireFXList = FX:FX_FireStartEnt BONE:FireSmall01\n"
	"  End\n"
	"End\n";

// The fires of the ignition, in the order the logic made them: `site` "FlammableUpdate FireFXList", the list and the bone the event carries.
struct FireWorld : movetest::MoveWorld
{
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;

	explicit FireWorld()
	{
		TheFXListStore = &fxLists;
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		std::string err = w.load(kPort61Lists, INI_LOAD_OVERWRITE, "port61-lists.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kPort61Objects, INI_LOAD_OVERWRITE, "port61.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~FireWorld()
	{
		logic->reset(); // the objects leave the world before the store they were parsed against goes away
		TheFXListStore = savedFx;
	}
	Object *spawnBurning(const char *name)
	{
		Object *o = spawn(name, 190.0f, 190.0f, 0.0f);
		REQUIRE(o);
		return o;
	}
	std::vector<FXEvent> fireEvents() const
	{
		std::vector<FXEvent> out;
		for (const FXEvent &e : logic->fxEvents().frameEvents()) // the events of the current logic frame
		{
			if (e.site && std::string(e.site) == "FlammableUpdate FireFXList")
			{
				out.push_back(e);
			}
		}
		return out;
	}
};

FlammableUpdate *flameOf(Object *o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (FlammableUpdate *f = dynamic_cast<FlammableUpdate *>(m.get()))
		{
			return f;
		}
	}
	return nullptr;
}

// the template's module data (a table of parsed fields: it outlives every object of the kind)
const FlammableUpdateModuleData *dataOf(Object *o)
{
	for (const ThingTemplate::Nugget &n : o->getTemplate()->behaviorModules().nuggets())
	{
		if (n.name == "FlammableUpdate")
		{
			return dynamic_cast<const FlammableUpdateModuleData *>(n.data.get());
		}
	}
	return nullptr;
}

int aflameStatus()
{
	static const int s = CombatNames::status("AFLAME"); // TheObjectStatusNames[10]
	return s;
}
} // namespace

TEST_CASE("PORT-61 the FireFXList parser stores an FXList and an optional bone per entry (RW 0x890CD9: `FX:<list> [BONE:<bone>]`, entry +0 the list, +4 the bone)")
{
	FireWorld w;
	const FlammableUpdateModuleData *d = dataOf(w.spawnBurning("BurningWood"));
	REQUIRE(d);
	REQUIRE(d->m_fireFXList.size() == 2); // RW 0x890D85: one entry per line, in file order
	CHECK(d->m_fireFXList[0].fxList == "FX_FireStartEnt"); // RW 0x890D43 -> RW 0x73A302: the name as the INI wrote it
	CHECK(d->m_fireFXList[0].bone == "FireSmall01");       // RW 0x890D58: the BONE keyword, then the bone
	CHECK(d->m_fireFXList[1].fxList == "FX_FireStartWoosh");
	CHECK(d->m_fireFXList[1].bone.empty()); // no BONE keyword: no bone, the entry plays on the object
	const FlammableUpdateModuleData *plain = dataOf(w.spawnBurning("BurningWoodNoBone"));
	REQUIRE(plain);
	REQUIRE(plain->m_fireFXList.size() == 2);
	CHECK(plain->m_fireFXList[0].bone.empty());
	CHECK(plain->m_fireFXList[1].bone.empty());
	const FlammableUpdateModuleData *none = dataOf(w.spawnBurning("BurningWoodNone"));
	REQUIRE(none);
	REQUIRE(none->m_fireFXList.size() == 2);
	CHECK(none->m_fireFXList[0].fxList.empty()); // RW 0x73A302: "None" stores no list
	CHECK(none->m_fireFXList[1].fxList == "FX_FireStartWoosh");
	const FlammableUpdateModuleData *empty = dataOf(w.spawnBurning("BurningWoodEmpty"));
	REQUIRE(empty);
	CHECK(empty->m_fireFXList.empty());
}

TEST_CASE("PORT-61 a synthetic burning object requests both its FireFXList effects at the ignition, at its bone and on the object (RW 0x89031F .. 0x890434)")
{
	FireWorld w;
	Object *wood = w.spawnBurning("BurningWood");
	FlammableUpdate *fl = flameOf(wood);
	REQUIRE(fl);
	w.frames(1);
	w.logic->fxEvents().beginFrame();
	fl->tryToIgnite(); // RW 0x8901AB; the object has no drawable in this world, so both entries take RW 0x890416's object path
	REQUIRE(wood->testStatus((unsigned)aflameStatus()));
	const std::vector<FXEvent> fires = w.fireEvents();
	REQUIRE(fires.size() == 2); // the list's length, in list order (RW 0x890322 / 0x890431)
	// entry 0: the list at its bone (RW 0x89034B: the bone is kept on the event, the client's branch RW 0x890356 asks for the drawable)
	CHECK(fires[0].kind == FXEvent::OBJECT_FX); // RW 0x4B1B5A / RW 0x494615: doFXObj or doFXPos at the bone, one event per entry
	CHECK(fires[0].fxList != nullptr);
	REQUIRE(fires[0].fxList);
	CHECK(*fires[0].fxList == "FX_FireStartEnt");
	REQUIRE(fires[0].bone);
	CHECK(*fires[0].bone == "FireSmall01");
	CHECK(fires[0].primary == wood->getID()); // RW 0x890356 / RW 0x89040D: the burning object itself
	CHECK(fires[0].frame == w.logic->getFrame());
	// entry 1: no bone: the object (RW 0x890416, doFXObj on the object)
	CHECK(fires[1].kind == FXEvent::OBJECT_FX);
	REQUIRE(fires[1].fxList);
	CHECK(*fires[1].fxList == "FX_FireStartWoosh");
	CHECK(fires[1].bone == nullptr); // no BONE keyword: RW 0x89034B's length test sends it to doFXObj
	CHECK(fires[1].primary == wood->getID());
}

TEST_CASE("PORT-61 without FireFXList nothing is requested at the ignition; a `None` entry plays nothing (RW 0x89031F / 0x890322 and RW 0x73A302)")
{
	{
		FireWorld w;
		Object *wood = w.spawnBurning("BurningWoodEmpty");
		FlammableUpdate *fl = flameOf(wood);
		REQUIRE(fl);
		w.frames(1);
		w.logic->fxEvents().beginFrame();
		fl->tryToIgnite();
		REQUIRE(wood->testStatus((unsigned)aflameStatus())); // the ignition itself ran
		CHECK(w.fireEvents().empty());                       // RW 0x890328: the empty vector goes straight to RW 0x89043A
	}
	{
		FireWorld w;
		Object *wood = w.spawnBurning("BurningWoodNone");
		FlammableUpdate *fl = flameOf(wood);
		REQUIRE(fl);
		w.frames(1);
		w.logic->fxEvents().beginFrame();
		fl->tryToIgnite();
		REQUIRE(wood->testStatus((unsigned)aflameStatus()));
		const std::vector<FXEvent> fires = w.fireEvents();
		REQUIRE(fires.size() == 1); // the "None" entry is dropped by the event log (RW 0x73A302 stores NULL: no call), the real one stays
		REQUIRE(fires[0].fxList);
		CHECK(*fires[0].fxList == "FX_FireStartWoosh");
	}
}

TEST_CASE("PORT-61 the list is played once per ignition, not once per frame of the burning (RW 0x8901AB: the loop is in the ignition, the update RW 0x890738 never plays it)")
{
	FireWorld w;
	Object *wood = w.spawnBurning("BurningWoodOneEntry");
	FlammableUpdate *fl = flameOf(wood);
	REQUIRE(fl);
	w.frames(1);
	w.logic->fxEvents().beginFrame();
	fl->tryToIgnite();
	REQUIRE(w.fireEvents().size() == 1);
	w.frames(20);
	w.logic->fxEvents().beginFrame();
	w.frames(20);
	CHECK(w.fireEvents().empty());                  // the burning goes on, the update makes no FX
	CHECK(wood->testStatus((unsigned)aflameStatus())); // AflameDuration = 0: the burning never ends in these frames
	// the two checks above are the whole account: the log held no event of the ignition after 40 more frames
	CHECK(w.fireEvents().empty());
}

TEST_CASE("PORT-61 a second tryToIgnite on a burning object is not an ignition and plays nothing (RW 0x8901BE: status must be NORMAL)")
{
	FireWorld w;
	Object *wood = w.spawnBurning("BurningWood");
	FlammableUpdate *fl = flameOf(wood);
	REQUIRE(fl);
	w.frames(1);
	w.logic->fxEvents().beginFrame();
	fl->tryToIgnite();
	REQUIRE(w.fireEvents().size() == 2);
	w.logic->fxEvents().beginFrame();
	fl->tryToIgnite(); // FS_AFLAME already: RW 0x8901BE returns at once
	CHECK(w.fireEvents().empty());
}
