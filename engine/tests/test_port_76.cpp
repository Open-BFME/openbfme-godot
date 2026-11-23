// OpenBFME unit tests. GPL-3.0.
// PORT-76 (issue fork issue 208): SpawnBehavior/FadeInTime - a unit a spawner makes fades in. Synthetic world (no game files), expectations read from the RotWK
// binary (caveat S-001), each check names its address:
//   * RW 0x862C18 .. 0x862C43 (SpawnBehavior::createSpawn, RW 0x862B07): eax = FadeInTime (the module data's + 0x18C, field table RW 0xC06498),
//     `fild` it, `fmul` the float 0.03 at RW 0xD9F624 (milliseconds -> 30 Hz client frames), _ftol (RW 0xA3CFA4: truncation toward zero), then the
//     spawn's drawable (Object::getDrawable RW 0x70E013) takes Drawable::fadeIn(frames) (RW 0x670AA2).
//   * RW 0x670AA2 fadeIn(n): n == 0 -> opacity 1 and mode 0 (shown at once); else opacity 0, mode 1, counter 0, target n, the start = the client frame.
//   * RW 0x675AB7 .. 0x675BBF (Drawable::updateDrawable): every client frame of mode 1 the opacity is counter / target, the mode ends at 0 when the
//     counter reaches the target, a hidden drawable is shown, a shown one whose opacity * 255 < 1 is hidden.
// DONOR BFME2 SpawnBehavior_createSpawn.cpp:251 (`newSpawn->getDrawable()->fadeIn(md->m_fadeInTime * 0.03f)`, data constructor BFME2 0x2575C2) - the
// mechanism, confirmed instruction by instruction in RW above. The port hands the frames to the drawable through its existing FADE_IN client event (lane
// BUILD-4), the same call the wall spans (RW 0x7954A8), the castle unpack (RW 0x798AE4) and the porter (RW 0x88D6CE) use: no second fade path.

#include "doctest.h"
#include "EconTestUtil.h"

#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <list>
#include <string>
#include <utility>
#include <vector>

namespace
{
// the spawn: a drawn object, its drawable is what fades
const char kCreep[] =
	"Object Port76Creep\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n";

// a spawner with an exit (QueueProductionExitUpdate, the simplest exit module: its AI half is not executed, S-201) so createSpawn reaches its fade
std::string spawner(const char *name, const char *fadeLine)
{
	return std::string("Object ") + name + "\n" +
		"  KindOf = STRUCTURE SELECTABLE\n"
		"  Behavior = SpawnBehavior ModuleTag_Spawn\n"
		"    SpawnNumber = 1\n"
		"    SpawnReplaceDelay = 1000\n" // 5 logic frames (RW 0x862DC8: now + the delay)
		"    SpawnTemplateName = Port76Creep\n" +
		std::string(fadeLine) +
		"  End\n"
		"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
		"    UnitCreatePoint = X:20.0 Y:0.0 Z:0.0\n"
		"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000\n  End\n"
		"End\n";
}

struct Port76World : econtest::Fx
{
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	DrawableManager drawables;
	ClientEventRecorder events;

	explicit Port76World(const std::string &objects)
		: econtest::Fx(objects.c_str())
		, source(w.fx.fsys)
		, assets(source)
		, drawables(assets, *logic)
		, events(*logic)
	{
		W3DDrawModules::registerTypedDrawModuleData(w.modules);
		logic->setClientHooks(&events);
	}
	~Port76World()
	{
		logic->setClientHooks(nullptr); // before the recorder goes, even when a REQUIRE ends the test
		logic.reset();
	}
	// one logic frame, then the render side takes the frame's events (the FADE_IN among them); `last` keeps them for the checks
	std::vector<ClientEvent> last;
	void frame()
	{
		logic->runLogicFrame();
		last = events.take();
		drawables.applyEvents(last);
	}
	// one retail client frame (30 Hz) of every drawable's fade (RW 0x675AB7)
	void clientStep()
	{
		for (size_t id = 1; id < drawables.slotCount(); ++id)
		{
			if (Drawable *d = drawables.find((DrawableID)id))
			{
				d->updateFade(1000.0 / 30.0);
			}
		}
	}
	// the FADE_IN events of the last frame: (object, client frames)
	std::vector<std::pair<ObjectID, UnsignedInt>> fades()
	{
		std::vector<std::pair<ObjectID, UnsignedInt>> out;
		for (const ClientEvent &e : last)
		{
			if (e.kind == ClientEvent::FADE_IN)
			{
				out.push_back({ e.object, e.fadeFrames });
			}
		}
		return out;
	}
};

// the spawner's first update makes its spawn (the initial burst queues the frame 0 replacement, RW 0x863455 .. 0x863493); its FADE_IN is still recorded
Object *firstSpawn(Port76World &f, Object *lair, SpawnBehavior *&module, std::vector<std::pair<ObjectID, UnsignedInt>> &fades)
{
	module = dynamic_cast<SpawnBehavior *>(lair->findModule("SpawnBehavior"));
	REQUIRE(module);
	f.frame();
	const std::list<ObjectID> &ids = module->spawns();
	REQUIRE_MESSAGE(ids.size() == 1, "the spawner made one spawn");
	Object *spawn = f.logic->findObjectByID(ids.front());
	REQUIRE(spawn);
	fades = f.fades();
	return spawn;
}
} // namespace

// RW 0x862C18 .. 0x862C43: 1000 ms -> ftol(1000 * 0.03) = 30 client frames, and RW 0x675AB7 steps the opacity up over exactly them
TEST_CASE("PORT-76 SpawnBehavior: a spawn's drawable fades in over FadeInTime (RW 0x862C18 .. 0x862C43 -> Drawable::fadeIn RW 0x670AA2)")
{
	Port76World f(std::string(kCreep) + spawner("Port76Lair1000", "    FadeInTime = 1000\n"));
	Object *lair = f.makeAt("Port76Lair1000", "Alice", 200.0f, 200.0f);
	SpawnBehavior *module = nullptr;
	std::vector<std::pair<ObjectID, UnsignedInt>> fades;
	Object *spawn = firstSpawn(f, lair, module, fades);
	CHECK(module->data()->m_fadeInTime == 1000u); // the field table RW 0xC06498 read it into the data's + 0x18C

	// the simulation asked its client for a fade of ftol(1000 * [RW 0xD9F624] = 0.03f) = 30 client frames (RW 0xA3CFA4 truncates)
	REQUIRE(fades.size() == 1);
	CHECK(fades[0].first == spawn->getID());
	CHECK(fades[0].second == (UnsignedInt)30);

	// the drawable the FADE_IN reached (RW 0x70E013) fades as RW 0x670AA2 / 0x675AB7 say: from 0, up over 30 client frames
	Drawable *d = f.drawables.findByObject(spawn->getID());
	REQUIRE(d);
	CHECK(d->fade().mode == 1);
	CHECK(d->fade().target == 30.0);
	CHECK(d->fade().counter == 0.0);
	CHECK(d->fade().opacity == 0.0f);
	CHECK(d->drawOpacity() == 0.0f);  // opacity * 255 < 1: not drawn while it fades in (RW 0x675B7A)
	CHECK(d->fade().hidden == false); // the fade hides nothing here (that is the projectile's mode 5, RW 0x67309D)
	f.clientStep();                   // client frame 1 of 30
	CHECK(d->drawOpacity() == (float)(1.0 / 30.0)); // RW 0x675B3E: the float of counter / target, exactly (counter 1 after one client frame)
	for (int i = 2; i <= 15; ++i)
	{
		f.clientStep();
	}
	CHECK(d->fade().counter == 15.0);
	CHECK(d->drawOpacity() == 0.5f); // half way: 15 / 30 is exact in binary (RW 0x675B3E)
	for (int i = 16; i <= 29; ++i)
	{
		f.clientStep();
	}
	CHECK(d->drawOpacity() == (float)(29.0 / 30.0)); // the float of counter / target, exactly (counter 29 of the target 30)
	CHECK(d->fade().mode == 1);
	f.clientStep(); // client frame 30: the counter reaches the target, the mode ends (RW 0x675B5C)
	CHECK(d->fade().mode == 0);
	CHECK(d->drawOpacity() == 1.0f);
}

// RW 0x670AA2 with 0: opacity 1, mode 0 - the spawn is there at once. FadeInTime 1 (a value of the mod's data) truncates to 0 frames: the same.
TEST_CASE("PORT-76 SpawnBehavior: FadeInTime 0, 1 or absent shows the spawn at once (RW 0x670AA2, the truncation RW 0xA3CFA4)")
{
	struct Case
	{
		const char *name, *line;
		UnsignedInt frames;
	};
	for (const Case c : { Case{ "Port76LairZero", "    FadeInTime = 0\n", 0u }, Case{ "Port76LairOne", "    FadeInTime = 1\n", 0u },
						 Case{ "Port76LairNone", "", 0u } })
	{
		CAPTURE(c.name);
		Port76World f(std::string(kCreep) + spawner(c.name, c.line));
		Object *lair = f.makeAt(c.name, "Alice", 200.0f, 200.0f);
		SpawnBehavior *module = nullptr;
		std::vector<std::pair<ObjectID, UnsignedInt>> fades;
		Object *spawn = firstSpawn(f, lair, module, fades);
		REQUIRE(fades.size() == 1); // retail calls fadeIn always: 0 is a call that shows the drawable at once, not a missing call
		CHECK(fades[0].first == spawn->getID());
		CHECK(fades[0].second == c.frames); // ftol(1 * 0.03f) = 0 as well: RW 0xA3CFA4 truncates toward zero
		Drawable *d = f.drawables.findByObject(spawn->getID());
		REQUIRE(d);
		CHECK(d->fade().mode == 0);      // RW 0x670AA2: no fade at all
		CHECK(d->fade().opacity == 1.0f);
		CHECK(d->drawOpacity() == 1.0f); // fully visible from the first client frame
		f.clientStep();
		CHECK(d->drawOpacity() == 1.0f);
	}
}

// the neighbouring behaviour the fade must not move: the spawn itself (its producer, its place, the replacement frame it took) and the replacement after
// its death are exactly what createSpawn made before (RW 0x862B07, RW 0x862DAB)
TEST_CASE("PORT-76 SpawnBehavior: the fade does not move the spawn itself (RW 0x862B07: the exit, the producer, the list, the replacement)")
{
	Port76World f(std::string(kCreep) + spawner("Port76LairPlain", "    FadeInTime = 1000\n"));
	Object *lair = f.makeAt("Port76LairPlain", "Alice", 200.0f, 200.0f);
	SpawnBehavior *module = nullptr;
	std::vector<std::pair<ObjectID, UnsignedInt>> fades;
	Object *spawn = firstSpawn(f, lair, module, fades);
	REQUIRE(fades.size() == 1);
	CHECK(spawn->getProducerID() == lair->getID());            // RW 0x862C5F: setProducer
	CHECK(module->replacementFrames().empty());                // the due frame the spawn took off the list (RW 0x8634F0)
	CHECK(module->spawnCount() == 1);                          // RW 0x862CE4: from -1 to 1
	// tolerance: the exit's create point goes through the spawner's orientation matrix (RW 0x8A3DF4 .. 0x8A3E85), a float product chain
	CHECK(spawn->getPosition()->x == doctest::Approx(220.0f).epsilon(1e-6)); // the exit's UnitCreatePoint, relative to the spawner (RW 0x8A3DD5)
	// tolerance: the same product chain for the y of the create point
	CHECK(spawn->getPosition()->y == doctest::Approx(200.0f).epsilon(1e-6));
	// its death queues the replacement (RW 0x862DAB) and the spawner makes the next spawn, which fades in the same way
	const ObjectID first = spawn->getID();
	spawn->kill(DAMAGE_UNRESISTABLE, 0); // Object::onDie (RW 0x699082 -> RW 0x6938BD) tells the producer's spawn interface
	CHECK(module->spawns().empty());
	CHECK(module->spawnCount() == 0);
	REQUIRE(module->replacementFrames().size() == 1);
	CHECK(module->replacementFrames().front() == f.logic->getFrame() + 5); // SpawnReplaceDelay 1000 ms = 5 logic frames
	Object *second = nullptr;
	for (int i = 0; i < 20 && !second; ++i)
	{
		f.frame();
		if (!module->spawns().empty())
		{
			second = f.logic->findObjectByID(module->spawns().front());
		}
	}
	REQUIRE(second);
	CHECK(second->getID() != first);
	CHECK(module->spawnCount() == 1);
	Drawable *d = f.drawables.findByObject(second->getID());
	REQUIRE(d);
	CHECK(d->fade().mode == 1);   // the replacement fades in like the first one
	CHECK(d->fade().target == 30.0);
}
