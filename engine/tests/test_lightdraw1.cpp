// OpenBFME unit tests, lane LIGHTDRAW-1: W3DLightDraw (RW 0x4CF515 defaults, RW 0xBE3638 fields, RW 0x4CF121 the light, RW 0x4CF2B5 the draw,
// RW 0x4CF29D setHidden), the drawables' light set and the four shader slots the pool hands out (inference S-113 / S-687).

#include "doctest.h"
#include "HudTestUtil.h"
#include "LogicTestUtil.h"
#include "RetailTestMount.h"

#include "Common/INI.h"
#include "Common/NumericState.h"
#include "GameClient/ClientEffects.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace logictest;

namespace
{
// the first values of WWMath::Random_Float on a fresh MSVCR71 generator (srand's default seed 1): rand() = 41, 18467, 6334, ...
float randomAt(int index)
{
	CrtRand r;
	float v = 0.0f;
	for (int i = 0; i <= index; ++i)
	{
		v = r.randomFloat();
	}
	return v;
}

W3DLightDrawModuleData torch()
{
	// GBCastleWallsA's ModuleTag_02 (campsandcastles.ini): Diffuse R:240 G:240 B:100, Radius 40, Intensity 1.2, FlickerAmplitude 0.5, FlickerFrequency 10
	W3DLightDrawModuleData d;
	d.m_diffuse = RGBColor{ 240.0f / 255.0f, 240.0f / 255.0f, 100.0f / 255.0f };
	d.m_radius = 40.0f;
	d.m_intensity = 1.2f;
	d.m_flickerAmplitude = 0.5f;
	d.m_flickerFrequency = 10.0f;
	d.m_attachToBone = "E_Fire_SM";
	return d;
}

W3DLightDrawSet::Source source(std::uint64_t key, const W3DLightDrawModuleData &d, float x, float y)
{
	W3DLightDrawSet::Source s;
	s.key = key;
	s.data = &d;
	s.position = Coord3D{ x, y, 0.0f };
	return s;
}
} // namespace

TEST_CASE("lightdraw1: module data defaults and field table (RW 0x4CF515, 0xBE3638)")
{
	W3DLightDrawModuleData d;
	CHECK(d.m_ambient.red == 0.0f);
	CHECK(d.m_diffuse.green == 0.0f);
	CHECK(d.m_specular.blue == 0.0f);
	CHECK(d.m_radius == 20.0f);   // RW 0xBDBC6C
	CHECK(d.m_intensity == 1.0f); // RW 0xBD1908
	CHECK(d.m_flickerAmplitude == 0.0f);
	CHECK(d.m_flickerFrequency == 0.0f);
	CHECK(d.m_pulseAmplitude == 0.0f);
	CHECK(d.m_pulseFrequency == 0.0f);
	CHECK(d.m_attachToBone.empty());
	const char *const names[] = { "Ambient", "Diffuse", "Specular", "Radius", "Intensity", "FlickerAmplitude", "FlickerFrequency", "PulseAmplitude",
		"PulseFrequency", "AttachToBoneInAnotherModule" };
	const FieldParse *t = W3DLightDrawTables::light();
	int n = 0;
	for (; t[n].token; ++n)
	{
		REQUIRE(n < 10); // tolerance: a loop guard (the table is read up to its terminator); the count is asserted exactly below
		CHECK(std::strcmp(t[n].token, names[n]) == 0);
	}
	CHECK(n == 10);
	// the class is a light draw now, with typed data
	REQUIRE(W3DDrawModules::find("W3DLightDraw") != nullptr);
	CHECK(W3DDrawModules::find("W3DLightDraw")->kind == W3D_DRAWKIND_LIGHT);
}

TEST_CASE("lightdraw1: an INI body parses into the typed module data")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load(
		"Object Torch1\n"
		"  Draw = W3DLightDraw ModuleTag_Light\n"
		"    Ambient = R:0 G:0 B:51\n    Diffuse = R:255 G:100 B:0\n    Specular = R:1 G:2 B:3\n    Radius = 90\n    Intensity = 6\n"
		"    FlickerAmplitude = 0.5\n    FlickerFrequency = 10\n    PulseAmplitude = 2\n    PulseFrequency = 0.25\n"
		"    AttachToBoneInAnotherModule = Pike\n"
		"  End\n"
		"End\n").empty());
	const ThingTemplate *tt = lw.w.things.findTemplate("Torch1");
	REQUIRE(tt != nullptr);
	REQUIRE(tt->drawModules().nuggets().size() == 1);
	const W3DLightDrawModuleData *d = dynamic_cast<const W3DLightDrawModuleData *>(tt->drawModules().nuggets()[0].data.get());
	REQUIRE(d != nullptr);
	CHECK(d->m_ambient.red == 0.0f);
	CHECK(d->m_ambient.blue == NumericState::pc24Mul(51.0f, 1.0f / 255.0f));
	CHECK(d->m_diffuse.red == NumericState::pc24Mul(255.0f, 1.0f / 255.0f));
	CHECK(d->m_diffuse.green == NumericState::pc24Mul(100.0f, 1.0f / 255.0f));
	CHECK(d->m_specular.blue == NumericState::pc24Mul(3.0f, 1.0f / 255.0f));
	CHECK(d->m_radius == 90.0f);
	CHECK(d->m_intensity == 6.0f);
	CHECK(d->m_flickerAmplitude == 0.5f);
	CHECK(d->m_flickerFrequency == 10.0f);
	CHECK(d->m_pulseAmplitude == 2.0f);
	CHECK(d->m_pulseFrequency == 0.25f);
	CHECK(d->m_attachToBone == "Pike");
}

TEST_CASE("lightdraw1: the constructor's phase and light (RW 0x4CF121) and a steady light's draw (RW 0x4CF2B5)")
{
	CrtRand r;
	W3DLightDraw draw(r);
	CHECK(draw.phase() == (float)((double)randomAt(0) * 30.0)); // Random_Float * 30.0
	CHECK(draw.countdown() == 0.0f);
	CHECK(draw.step() == 0.0f);
	CHECK(draw.height() == 0.0f);
	W3DLightDrawModuleData d = torch();
	LightPulseFx light;
	light.intensity = 7.0f;
	W3DLightDraw::initLight(d, light);
	CHECK(light.enabled);
	CHECK(light.color.red == d.m_diffuse.red);
	CHECK(light.targetColor.blue == d.m_diffuse.blue);
	CHECK(light.farAttenStart == 1.0f);
	CHECK(light.farAttenEnd == 40.0f);
	CHECK(light.range() == 40.0f);
	CHECK(light.intensity == 1.0f);
	CHECK(light.increaseFrames == 0u);
	CHECK(light.decreaseFrames == 0u);
	CHECK(light.step()); // setFrameFade(0, 0): On_Frame_Update keeps it lit at scale 1
	CHECK(light.scale() == 1.0f);
	// no flicker, no pulse: the light's Intensity is the data's; the phase moves by 1 / 5 a draw
	d.m_flickerAmplitude = 0.0f;
	const float phase0 = draw.phase();
	CHECK(draw.doDrawModule(d, r) == 1.2f);
	CHECK(draw.phase() == phase0 + 0.2f);
	CHECK(draw.doDrawModule(d, r) == 1.2f);
	CHECK(draw.phase() == (phase0 + 0.2f) + 0.2f);
	// the intensity scales the light's colour (DONOR BFME2 Init_From_Point_Or_Spot_Light)
	light.intensity = 2.0f;
	CHECK(light.colorNow().red == d.m_diffuse.red * 2.0f);
}

TEST_CASE("lightdraw1: the flicker steps toward a random target over 5 / FlickerFrequency draws (RW 0x4CF3C9 .. 0x4CF42F)")
{
	// CampfireLight_Flash: Intensity 0.5, FlickerAmplitude 1.5, FlickerFrequency 0.5: a countdown of 10 draws per target
	W3DLightDrawModuleData d;
	d.m_intensity = 0.5f;
	d.m_flickerAmplitude = 1.5f;
	d.m_flickerFrequency = 0.5f;
	CrtRand r;
	W3DLightDraw draw(r); // draws Random_Float #0
	const double target = ((double)1.5f - (double)-1.5f) * (double)randomAt(1) + (double)-1.5f + (double)0.5f;
	const float step = (float)((target - 0.0) / (double)10.0f);
	float expected = 0.0f;
	for (int k = 1; k <= 10; ++k)
	{
		CAPTURE(k);
		expected = step + expected;
		CHECK(draw.doDrawModule(d, r) == expected);
		CHECK(draw.countdown() == 10.0f - (float)k);
		CHECK(draw.step() == step);
	}
	// the 11th draw: the countdown is 0, a new target from the height reached (Random_Float #2)
	const double target2 = ((double)1.5f - (double)-1.5f) * (double)randomAt(2) + (double)-1.5f + (double)0.5f;
	const float step2 = (float)((target2 - (double)expected) / (double)10.0f);
	CHECK(draw.doDrawModule(d, r) == step2 + expected);
	CHECK(draw.step() == step2);

	// the castle torches: FlickerFrequency 10 gives a countdown of 0.5, so every draw takes a new target and overshoots it (2 * target - height)
	const W3DLightDrawModuleData t = torch();
	CrtRand r2;
	W3DLightDraw wall(r2);
	const double t1 = ((double)0.5f - (double)-0.5f) * (double)randomAt(1) + (double)-0.5f + (double)1.2f;
	const float s1 = (float)(t1 / (double)0.5f);
	CHECK(wall.doDrawModule(t, r2) == s1);
	CHECK(wall.countdown() == -0.5f);
	const double t2 = ((double)0.5f - (double)-0.5f) * (double)randomAt(2) + (double)-0.5f + (double)1.2f;
	const float s2 = (float)((t2 - (double)s1) / (double)0.5f);
	CHECK(wall.doDrawModule(t, r2) == s2 + s1);
	CHECK(wall.countdown() == -0.5f);
}

TEST_CASE("lightdraw1: the pulse adds PulseAmplitude * sin(PulseFrequency * phase * 2 pi) (RW 0x4CF43E)")
{
	W3DLightDrawModuleData d;
	d.m_intensity = 3.0f;
	d.m_pulseAmplitude = 2.0f;
	d.m_pulseFrequency = 0.25f;
	CrtRand r;
	W3DLightDraw draw(r);
	const float phase = draw.phase() + 0.2f;
	const float x = 0.25f * phase * 6.2831855f;
	const float s = (float)std::sin((double)x);
	CHECK(draw.doDrawModule(d, r) == 2.0f * s + 3.0f);
}

TEST_CASE("lightdraw1: the set makes a light per module, follows the drawable, switches it off while hidden and releases it (RW 0x4CF29D / 0x4CF5B6)")
{
	LightPulsePool pool;
	W3DLightDrawSet set;
	W3DLightDrawModuleData d = torch();
	d.m_flickerAmplitude = 0.0f;
	std::vector<W3DLightDrawSet::Source> frame = { source((1ull << 8) | 0, d, 100.0f, 200.0f), source((2ull << 8) | 1, d, 300.0f, 50.0f) };
	set.update(frame, pool);
	REQUIRE(set.size() == 2);
	CHECK(pool.drawLightCount() == 2);
	const size_t h1 = set.lightHandle((1ull << 8) | 0);
	const size_t h2 = set.lightHandle((2ull << 8) | 1);
	REQUIRE(h1 != (size_t)-1);
	REQUIRE(h2 != (size_t)-1);
	CHECK(pool.drawLights()[h1].enabled);
	CHECK(pool.drawLights()[h1].position.x == 100.0f);
	CHECK(pool.drawLights()[h1].position.y == 200.0f);
	CHECK(pool.drawLights()[h1].intensity == 1.2f);
	CHECK(pool.drawLights()[h1].range() == 40.0f);
	// the modules were made in the given order: the first took Random_Float #0, the second #1
	CHECK(set.find((1ull << 8) | 0)->phase() == (float)((double)randomAt(0) * 30.0) + 0.2f);
	CHECK(set.find((2ull << 8) | 1)->phase() == (float)((double)randomAt(1) * 30.0) + 0.2f);
	CHECK(pool.enabledCount() == 0); // the pulses' count does not see the draw lights

	// the drawable moves: the light follows (the draw's translation)
	frame[0].position = Coord3D{ 110.0f, 210.0f, 5.0f };
	set.update(frame, pool);
	CHECK(pool.drawLights()[h1].position.x == 110.0f);
	CHECK(pool.drawLights()[h1].position.z == 5.0f);
	const float phaseDrawn = set.find((1ull << 8) | 0)->phase();

	// under the shroud the draw is not reached: the light stays lit where it was, the phase stands
	frame[0].drawn = false;
	frame[0].position = Coord3D{ 500.0f, 500.0f, 0.0f };
	set.update(frame, pool);
	CHECK(pool.drawLights()[h1].enabled);
	CHECK(pool.drawLights()[h1].position.x == 110.0f);
	CHECK(set.find((1ull << 8) | 0)->phase() == phaseDrawn);

	// hidden: setHidden(true) switches the light off
	frame[0].drawn = true;
	frame[0].hidden = true;
	set.update(frame, pool);
	CHECK_FALSE(pool.drawLights()[h1].enabled);
	CHECK(set.find((1ull << 8) | 0)->phase() == phaseDrawn);
	frame[0].hidden = false;
	set.update(frame, pool);
	CHECK(pool.drawLights()[h1].enabled);
	CHECK(pool.drawLights()[h1].position.x == 500.0f);

	// a pulse never takes a draw light, even a disabled one
	frame[0].hidden = true;
	set.update(frame, pool);
	CHECK(pool.add(Coord3D{ 0, 0, 0 }, RGBColor{ 1, 1, 1 }, 50.0f, 0, 10));
	CHECK(pool.lights().size() == 1);
	CHECK(pool.drawLightCount() == 2);

	// the drawable is gone: its light is disabled and its slot freed (and reused by the next module)
	frame.erase(frame.begin());
	set.update(frame, pool);
	CHECK(set.size() == 1);
	CHECK(pool.drawLightCount() == 1);
	CHECK_FALSE(pool.drawLights()[h1].enabled);
	frame.push_back(source((3ull << 8) | 0, d, 0.0f, 0.0f));
	set.update(frame, pool);
	CHECK(set.lightHandle((3ull << 8) | 0) == h1);
	CHECK(pool.drawLights()[h1].enabled);

	// a cleared pool (the game ended): the set starts again
	pool.clear();
	CHECK(pool.drawLightCount() == 0);
	set.update(frame, pool);
	CHECK(set.size() == 2);
	CHECK(pool.drawLightCount() == 2);
}

TEST_CASE("lightdraw1: the four shader slots take the lights in view nearest to the camera, pulses first on a tie (inference S-687)")
{
	LightPulsePool pool;
	// pulses at x = 100 and 400; draw lights at x = 50, 200, 300, 600
	REQUIRE(pool.add(Coord3D{ 100, 0, 0 }, RGBColor{ 1, 0, 0 }, 50.0f, 0, 30));
	REQUIRE(pool.add(Coord3D{ 400, 0, 0 }, RGBColor{ 1, 0, 0 }, 50.0f, 0, 30));
	const float xs[] = { 50.0f, 200.0f, 300.0f, 600.0f };
	for (float x : xs)
	{
		const size_t h = pool.acquireDrawLight();
		W3DLightDraw::initLight(torch(), pool.drawLight(h));
		pool.drawLight(h).position = Coord3D{ x, 0, 0 };
	}
	const Coord3D eye{ 0, 0, 0 };
	auto all = [](const Coord3D &, float) { return true; };
	std::vector<const LightPulseFx *> s = pool.select(eye, 4, all);
	REQUIRE(s.size() == 4);
	CHECK(s[0]->position.x == 50.0f);
	CHECK(s[1]->position.x == 100.0f);
	CHECK(s[2]->position.x == 200.0f);
	CHECK(s[3]->position.x == 300.0f);
	// out of view (the sphere does not reach the frustum): not given a slot
	auto right = [](const Coord3D &p, float range) { return p.x + range >= 250.0f; };
	s = pool.select(eye, 4, right);
	REQUIRE(s.size() == 3);
	CHECK(s[0]->position.x == 300.0f);
	CHECK(s[1]->position.x == 400.0f);
	CHECK(s[2]->position.x == 600.0f);
	// disabled lights take no slot
	pool.drawLight(0).enabled = false;
	s = pool.select(eye, 4, all);
	REQUIRE(s.size() == 4);
	CHECK(s[0]->position.x == 100.0f);
	// a tie: the pulse first
	pool.drawLight(1).position = Coord3D{ 100, 0, 0 };
	s = pool.select(eye, 2, all);
	REQUIRE(s.size() == 2);
	CHECK(s[0] == &pool.lights()[0]);
	CHECK(s[1] == &pool.drawLights()[1]);
	CHECK(W3DLightDrawStopLine().rfind("[S-113] W3DLightDraw", 0) == 0);
}

TEST_CASE("lightdraw1: the drawables' W3DLightDraw entries reach the set (a synthetic template)")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load(
		"Object Lamp1\n"
		"  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
		"  Draw = W3DLightDraw ModuleTag_Light\n    Diffuse = R:255 G:128 B:0\n    Radius = 65\n    Intensity = 7\n  End\n"
		"End\n").empty());
	ArchiveW3DFileSource src(lw.w.fx.fsys);
	WW3DAssetManager assets(src);
	DrawableManager dm(assets, *lw.logic);
	ClientEventRecorder rec(*lw.logic);
	lw.logic->setClientHooks(&rec);
	Object *o = lw.make("Lamp1");
	dm.applyEvents(rec.take());
	const Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d);
	REQUIRE(d->entries().size() == 2);
	CHECK(d->entries()[1].kind == W3D_DRAWKIND_LIGHT);
	LightPulsePool pool;
	W3DLightDrawSet set;
	set.update(dm, nullptr, false, pool);
	REQUIRE(set.size() == 1);
	const std::uint64_t key = ((std::uint64_t)d->getID() << 8) | 1;
	const size_t h = set.lightHandle(key);
	REQUIRE(h != (size_t)-1);
	CHECK(pool.drawLights()[h].enabled);
	CHECK(pool.drawLights()[h].range() == 65.0f);
	CHECK(pool.drawLights()[h].intensity == 7.0f);
	CHECK(pool.drawLights()[h].position.x == d->getPosition()->x);
	CHECK(pool.drawLights()[h].position.y == d->getPosition()->y);
	set.update(dm, nullptr, false, pool); // a known slot is not scanned twice
	CHECK(set.size() == 1);
	CHECK(pool.drawLightCount() == 1);
	lw.logic->destroyObject(o);
	lw.logic->processDestroyList();
	dm.applyEvents(rec.take());
	REQUIRE(dm.findByObject(o->getID()) == nullptr);
	set.update(dm, nullptr, false, pool);
	CHECK(set.size() == 0);
	CHECK(pool.drawLightCount() == 0);
	lw.logic->reset();
	lw.logic->setClientHooks(nullptr);
}

TEST_CASE("lightdraw1 retail: the W3DLightDraw templates parse into the typed module data")
{
	if (!hudtest::haveWorld("lightdraw1 retail templates"))
	{
		return;
	}
	const RetailObjectWorld &world = *hudtest::shared().world;
	size_t withLight = 0, typed = 0;
	for (const ThingTemplate *tt : world.things().templates())
	{
		for (const ThingTemplate::Nugget &n : tt->drawModules().nuggets())
		{
			if (n.name == "W3DLightDraw")
			{
				++withLight;
				typed += dynamic_cast<const W3DLightDrawModuleData *>(n.data.get()) ? 1 : 0;
			}
		}
	}
	MESSAGE("templates with a W3DLightDraw module: " << withLight);
	CHECK(withLight == 37); // the 37 Draw = W3DLightDraw blocks of RotWK's ini.big (docs/module-coverage.md)
	CHECK(typed == withLight);
	const ThingTemplate *wall = world.things().findTemplate("GBCastleWallsA");
	REQUIRE(wall != nullptr);
	const W3DLightDrawModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : wall->drawModules().nuggets())
	{
		if (n.name == "W3DLightDraw")
		{
			d = dynamic_cast<const W3DLightDrawModuleData *>(n.data.get());
		}
	}
	REQUIRE(d != nullptr);
	// campsandcastles.ini GBCastleWallsA ModuleTag_02
	CHECK(d->m_diffuse.red == NumericState::pc24Mul(240.0f, 1.0f / 255.0f));
	CHECK(d->m_diffuse.blue == NumericState::pc24Mul(100.0f, 1.0f / 255.0f));
	CHECK(d->m_ambient.red == 0.0f);
	CHECK(d->m_radius == 40.0f);
	CHECK(d->m_intensity == 1.2f);
	CHECK(d->m_flickerAmplitude == 0.5f);
	CHECK(d->m_flickerFrequency == 10.0f);
	CHECK(d->m_attachToBone == "E_Fire_SM");
}

TEST_CASE("lightdraw1 retail: an orc scout's torch is a light at its drawable, flickering with nothing and lit with Intensity 6")
{
	if (!hudtest::haveWorld("lightdraw1 retail scout"))
	{
		return;
	}
	hudtest::Rig r(hudtest::shared());
	Object *o = r.make("MordorOrcScout_Unit", 600.0f, 600.0f);
	r.game->advance(0.25);
	const Drawable *d = r.game->drawables().findByObject(o->getID());
	REQUIRE(d != nullptr);
	size_t lightEntry = (size_t)-1;
	for (size_t k = 0; k < d->entries().size(); ++k)
	{
		if (d->entries()[k].kind == W3D_DRAWKIND_LIGHT)
		{
			lightEntry = k;
		}
	}
	REQUIRE(lightEntry != (size_t)-1);
	LightPulsePool pool;
	W3DLightDrawSet set;
	const std::shared_ptr<const LogicSnapshot> presented = r.game->presentedSnapshot();
	set.update(r.game->drawables(), presented.get(), false, pool);
	const size_t h = set.lightHandle(((std::uint64_t)d->getID() << 8) | lightEntry);
	REQUIRE(h != (size_t)-1);
	const LightPulseFx &l = pool.drawLights()[h];
	CHECK(l.enabled);
	// mordororcscout_unit.ini ModuleTag_02: Diffuse R:255 G:100 B:0, Radius 90, Intensity 6, no flicker
	CHECK(l.range() == 90.0f);
	CHECK(l.intensity == 6.0f);
	CHECK(l.color.green == NumericState::pc24Mul(100.0f, 1.0f / 255.0f));
	CHECK(l.position.x == d->getPosition()->x);
	CHECK(l.position.y == d->getPosition()->y);
	CHECK(l.position.z == d->getPosition()->z);
}
