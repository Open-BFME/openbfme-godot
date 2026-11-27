// OpenBFME unit tests. GPL-3.0.
// LaserUpdate: Gandalf's lightning beams as retail (client update that feeds the endpoints of W3DLaserDraw; both editions).
//
// Target facts verified from RotWK 2.01 game.dat (md5 daed3668006cd90f01c34e5a7da1901f) and donor BFME2 1.06:
// - Factory: RW 0x64D233 for module data (new 0x1C), RW 0x64D1FB for module (new 0x54).
// - Registry: "LaserUpdate", MODULETYPE_CLIENT_UPDATE = 2, interface mask 0x800.
// - ModuleData: ctor RW 0x764D9C / BFME2 0x363147, table RW 0xC2D890 / BFME2 0xBDD288 (5 fields).
// - Module: ctor RW 0x764B43, dtor RW 0x764BB9, name key "LaserUpdate" RW 0x764D57, decay RW 0x764C18,
//   xfer RW 0x764C4D, initLaser RW 0x7651B1, initFromDrawables RW 0x7654A8, clientUpdate RW 0x764E88.
// - W3DLaserDraw consumer: RW 0x4C8AA1..0x4C8B35 reads endpoints, width scalar, and clears dirty byte at RW 0x4C8AE2.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "W3dSynth.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ClientBehaviorModules.h"
#include "GameClient/Drawable.h"
#include "GameClient/LaserUpdate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.h"

#include <cmath>
#include <memory>
#include <string>

using namespace combattest;

namespace
{
// the drawable's LaserUpdate client module (the test's own look-up; RW 0x6747C2 walks the client modules the same way)
LaserUpdate *findLaserUpdate(const Drawable &d)
{
	for (const auto &m : d.clientModules())
	{
		if (auto *lu = dynamic_cast<LaserUpdate *>(m.get()))
		{
			return lu;
		}
	}
	return nullptr;
}
} // namespace

namespace
{
bool nearFloat(float a, float b, float eps = 1e-4f)
{
	return std::fabs(a - b) <= eps;
}

bool hasField(const MultiIniFieldParse &p, const char *name)
{
	for (int i = 0; i < p.getCount(); ++i)
	{
		const FieldParse *fp = p.getNthFieldParse(i);
		while (fp && fp->token)
		{
			if (std::strcmp(fp->token, name) == 0)
			{
				return true;
			}
			++fp;
		}
	}
	return false;
}
} // namespace

TEST_CASE("LaserUpdateModuleData defaults and FieldParse (RW 0xC2D890 / BFME2 0xBDD288)")
{
	LaserUpdateModuleData data;
	CHECK(data.m_muzzleParticleSystem.empty());
	CHECK(data.m_parentFireBoneName.empty());
	CHECK(data.m_parentFireBoneOnTurret == false);
	CHECK(data.m_targetParticleSystem.empty());
	CHECK(data.m_laserLifetime == 0.0f);

	MultiIniFieldParse p;
	LaserUpdateModuleData::buildFieldParse(p);
	CHECK(hasField(p, "MuzzleParticleSystem"));
	CHECK(hasField(p, "ParentFireBoneName"));
	CHECK(hasField(p, "ParentFireBoneOnTurret"));
	CHECK(hasField(p, "TargetParticleSystem"));
	CHECK(hasField(p, "LaserLifetime"));
}

TEST_CASE("LaserUpdate INI parse and module template registration (GandalfLaserBeam, RW 0x64D1FB)")
{
	CombatWorld world("");
	W3DDrawModules::registerTypedDrawModuleData(world.w.modules);
	ClientBehaviorModules::registerAll(world.w.modules);

	const char *kGandalfLaserBeamIni =
		"Object GandalfLaserBeamTest\n"
		"  KindOf = IMMOBILE INERT DRAWABLE_ONLY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 1.0\n"
		"  GeometryHeight = 2.0\n"
		"  ClientUpdate = LaserUpdate ModuleTag_02\n"
		"    MuzzleParticleSystem = flareSpin2\n"
		"    ParentFireBoneName = Staff\n"
		"    ParentFireBoneOnTurret = No\n"
		"    TargetParticleSystem = GandalfBeamFlare4\n"
		"    LaserLifetime = 9000\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"End\n";

	const std::string err = world.w.load(kGandalfLaserBeamIni, INI_LOAD_OVERWRITE, "test_gandalf_laser.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	const ThingTemplate *tt = world.logic->things().findTemplate("GandalfLaserBeamTest");
	REQUIRE(tt != nullptr);

	const LaserUpdateModuleData *laserData = nullptr;
	for (const auto &n : tt->clientUpdateModules().nuggets())
	{
		if (n.name == "LaserUpdate")
		{
			laserData = dynamic_cast<const LaserUpdateModuleData *>(n.data.get());
		}
	}
	REQUIRE(laserData != nullptr);
	CHECK(laserData->m_muzzleParticleSystem == "flareSpin2");
	CHECK(laserData->m_parentFireBoneName == "Staff");
	CHECK(laserData->m_parentFireBoneOnTurret == false);
	CHECK(laserData->m_targetParticleSystem == "GandalfBeamFlare4");
	CHECK(nearFloat(laserData->m_laserLifetime, 9000.0f));
}

TEST_CASE("LaserUpdate construction and initial state (RW 0x764B43, RW 0xBD1908)")
{
	LaserUpdateModuleData data;
	LaserUpdate update(nullptr, &data);

	CHECK(update.data() == &data);
	CHECK(update.startPos().x == 0.0f);
	CHECK(update.startPos().y == 0.0f);
	CHECK(update.startPos().z == 0.0f);
	CHECK(update.endPos().x == 0.0f);
	CHECK(update.endPos().y == 0.0f);
	CHECK(update.endPos().z == 0.0f);
	CHECK(update.isDirty() == false);
	// RW 0xBD1908: currentWidthScalar initialized to 1.0f
	CHECK(nearFloat(update.currentWidthScalar(), 1.0f));
	CHECK(update.isWidening() == false);
	CHECK(update.isDecaying() == false);
	CHECK(update.expiryFrame() == 0);
	CHECK(update.parentID() == 0);
	CHECK(update.targetID() == 0);
}

TEST_CASE("LaserUpdate initLaser widening, decaying and lifetime calculation (RW 0x7651B1, RW 0xD9F624)")
{
	LaserUpdateModuleData data;
	data.m_laserLifetime = 500.0f; // 500 ms -> 0.03 * 500 = 15 frames (RW 0xD9F624)
	LaserUpdate update(nullptr, &data);

	const Coord3D start{ 10.0f, 20.0f, 30.0f };
	const Coord3D end{ 100.0f, 200.0f, 300.0f };

	// Widening: sizeDeltaFrames > 0
	update.initLaser(nullptr, &start, &end, 10, 50);
	CHECK(update.isWidening() == true);
	CHECK(update.isDecaying() == false);
	CHECK(update.widenStartFrame() == 50);
	CHECK(update.widenFinishFrame() == 60);
	CHECK(nearFloat(update.currentWidthScalar(), 0.0f));
	CHECK(update.expiryFrame() == 50 + 15);
	CHECK(update.isDirty() == true);
	CHECK(nearFloat(update.startPos().x, 10.0f));
	CHECK(nearFloat(update.endPos().x, 100.0f));

	// Decaying: sizeDeltaFrames < 0
	update.initLaser(nullptr, &start, &end, -20, 100);
	CHECK(update.isWidening() == false);
	CHECK(update.isDecaying() == true);
	CHECK(update.decayStartFrame() == 100);
	CHECK(update.decayFinishFrame() == 120);
	CHECK(nearFloat(update.currentWidthScalar(), 1.0f));
	CHECK(update.expiryFrame() == 100 + 15);
	CHECK(update.isDirty() == true);
}

TEST_CASE("LaserUpdate clientUpdate widening, decaying and expiry progression (RW 0x764E88)")
{
	LaserUpdateModuleData data;
	data.m_laserLifetime = 1000.0f; // 30 frames lifetime
	LaserUpdate update(nullptr, &data);

	const Coord3D start{ 0.0f, 0.0f, 0.0f };
	const Coord3D end{ 10.0f, 10.0f, 10.0f };

	// Test Widening: 10 frames ramp (frame 0 to 10)
	update.initLaser(nullptr, &start, &end, 10, 0);
	CHECK(nearFloat(update.currentWidthScalar(), 0.0f));

	// Frame 5: half-way -> 0.5f
	bool alive = update.clientUpdate(5);
	CHECK(alive == true);
	CHECK(nearFloat(update.currentWidthScalar(), 0.5f));
	CHECK(update.isWidening() == true);
	CHECK(update.isDirty() == true);

	// Frame 10: finish widening -> 1.0f, widening becomes false
	alive = update.clientUpdate(10);
	CHECK(alive == true);
	CHECK(nearFloat(update.currentWidthScalar(), 1.0f));
	CHECK(update.isWidening() == false);

	// Test Decaying: 20 frames decay (frame 10 to 30)
	update.decay(20, 10);
	CHECK(update.isDecaying() == true);
	CHECK(nearFloat(update.currentWidthScalar(), 1.0f));

	// Frame 20: half-way decay -> 0.5f
	alive = update.clientUpdate(20);
	CHECK(alive == true);
	CHECK(nearFloat(update.currentWidthScalar(), 0.5f));

	// Frame 30: fully decayed -> 0.0f, clientUpdate returns false (expired)
	alive = update.clientUpdate(30);
	CHECK(alive == false);
	CHECK(nearFloat(update.currentWidthScalar(), 0.0f));

	// Test Lifetime Expiry: lifetime is 30 frames, at frame 35 (> 30) -> expired
	update.initLaser(nullptr, &start, &end, 0, 0); // expiry = 30
	alive = update.clientUpdate(35);
	CHECK(alive == false);
}

TEST_CASE("LaserUpdate initFromDrawables and geometry height tracking (RW 0x7654A8, RW 0x765062, RW 0x76509D)")
{
	CombatWorld world("");
	W3DDrawModules::registerTypedDrawModuleData(world.w.modules);
	ClientBehaviorModules::registerAll(world.w.modules);

	const char *kUnitsIni =
		"Object CasterUnit\n"
		"  KindOf = INFANTRY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 10.0\n"
		"  GeometryHeight = 20.0\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"End\n"
		"Object TargetUnit\n"
		"  KindOf = INFANTRY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 15.0\n"
		"  GeometryHeight = 30.0\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 100\n"
		"  End\n"
		"End\n"
		"Object LaserBeamHolder\n"
		"  KindOf = IMMOBILE INERT DRAWABLE_ONLY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryHeight = 1.0\n"
		"  ClientUpdate = LaserUpdate ModuleTag_LU\n"
		"    LaserLifetime = 5000\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"End\n";

	const std::string err = world.w.load(kUnitsIni, INI_LOAD_OVERWRITE, "test_tracking.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	w3dsynth::MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpts;
	DrawServices drawServices(assets, world.w.modules, hostOpts);

	std::vector<std::string> problems;
	const ThingTemplate *ttCaster = world.logic->things().findTemplate("CasterUnit");
	const ThingTemplate *ttTarget = world.logic->things().findTemplate("TargetUnit");
	const ThingTemplate *ttLaser = world.logic->things().findTemplate("LaserBeamHolder");

	std::vector<ClientEvent::ClientModule> laserModules;
	for (const auto &n : ttLaser->clientUpdateModules().nuggets())
	{
		ClientEvent::ClientModule cm;
		cm.name = n.name;
		cm.data = n.data.get();
		cm.resolved = world.logic->modules().resolveClientModule(n.name, MODULETYPE_CLIENT_UPDATE);
		laserModules.push_back(std::move(cm));
	}

	Drawable caster(drawServices, ttCaster, 101, 1, {}, problems);
	Drawable target(drawServices, ttTarget, 102, 2, {}, problems);
	Drawable laser(drawServices, ttLaser, 103, 3, laserModules, problems);

	const Coord3D casterPos{ 0.0f, 0.0f, 0.0f };
	const Coord3D targetPos{ 100.0f, 0.0f, 0.0f };
	caster.setPosition(&casterPos);
	target.setPosition(&targetPos);

	LaserUpdate *lu = findLaserUpdate(laser);
	REQUIRE(lu != nullptr);

	// initFromDrawables
	lu->initFromDrawables(&caster, &target, 0, 0);

	CHECK(lu->parentID() == 101);
	CHECK(lu->targetID() == 102);
	// Parent geometry height is 20.0, fallback scalar is 0.5f (RW 0x765062) -> Z = 10.0f
	CHECK(nearFloat(lu->startPos().x, 0.0f));
	CHECK(nearFloat(lu->startPos().y, 0.0f));
	CHECK(nearFloat(lu->startPos().z, 10.0f));
	// Target geometry height is 30.0, fallback scalar is 0.83f (RW 0x76509D / RW 0xC2D8F0) -> Z = 24.9f
	CHECK(nearFloat(lu->endPos().x, 100.0f));
	CHECK(nearFloat(lu->endPos().y, 0.0f));
	CHECK(nearFloat(lu->endPos().z, 30.0f * 0.83f));

	// Midpoint set on laser drawable: ((0+100)/2, (0+0)/2, (10+24.9)/2) = (50.0, 0.0, 17.45)
	REQUIRE(laser.getPosition() != nullptr);
	CHECK(nearFloat(laser.getPosition()->x, 50.0f));
	CHECK(nearFloat(laser.getPosition()->y, 0.0f));
	CHECK(nearFloat(laser.getPosition()->z, (10.0f + 30.0f * 0.83f) * 0.5f));

	// Now move target to (200, 50, 10) and verify dynamic tracking in clientUpdate
	const Coord3D newTargetPos{ 200.0f, 50.0f, 10.0f };
	target.setPosition(&newTargetPos);

	auto lookup = [&](DrawableID id) -> Drawable * {
		if (id == 101) return &caster;
		if (id == 102) return &target;
		return nullptr;
	};

	lu->clientUpdate(1, lookup);
	CHECK(nearFloat(lu->endPos().x, 200.0f));
	CHECK(nearFloat(lu->endPos().y, 50.0f));
	CHECK(nearFloat(lu->endPos().z, 10.0f + 30.0f * 0.83f));
}

TEST_CASE("LaserUpdate integration with W3DLaserDrawState::build and dirty clear (RW 0x4C8A6C, RW 0x4C8AE2)")
{
	W3DLaserDrawModuleData drawData;
	drawData.m_segments = 1;
	drawData.m_numBeams = 1;
	drawData.m_innerBeamWidth = 10.0f;
	drawData.m_outerBeamWidth = 20.0f;

	LaserUpdateModuleData updateData;
	LaserUpdate update(nullptr, &updateData);

	const Coord3D start{ 0.0f, 0.0f, 0.0f };
	const Coord3D end{ 100.0f, 0.0f, 0.0f };
	update.initLaser(nullptr, &start, &end, 0, 0);
	CHECK(update.isDirty() == true);
	CHECK(nearFloat(update.currentWidthScalar(), 1.0f));

	W3DLaserDrawState state;
	state.reset(drawData);

	auto groundFn = [](float, float) { return 0.0f; };
	std::vector<W3DLaserBeam> beams = state.build(drawData, update, false, 1.0f, 0, groundFn);

	// Verified: beams were built with LaserUpdate endpoints and width
	REQUIRE(!beams.empty());
	CHECK(nearFloat(beams[0].points[0][0], 0.0f));
	CHECK(nearFloat(beams[0].points[1][0], 100.0f));
	// Width scale: template width outer is 20.0f * 0.5 = 10.0f
	CHECK(nearFloat(beams[0].width, 10.0f));

	// RW 0x4C8AE2: update.dirty must be cleared by build!
	CHECK(update.isDirty() == false);

	// A second build with neither dirty should return empty (RW 0x4C8AC8 / 0x4C8AD8)
	std::vector<W3DLaserBeam> secondBeams = state.build(drawData, update, false, 1.0f, 0, groundFn);
	CHECK(secondBeams.empty());
}

TEST_CASE("LaserUpdate xfer save/load roundtrip (RW 0x764C4D / BFME2 0x362FF8)")
{
	LaserUpdateModuleData data;
	LaserUpdate src(nullptr, &data);

	const Coord3D s{ 12.0f, 34.0f, 56.0f };
	const Coord3D e{ 78.0f, 90.0f, 12.0f };
	src.initLaser(nullptr, &s, &e, 15, 42);
	src.setDirty(true);

	XferSave saver;
	src.xfer(saver);

	LaserUpdate dst(nullptr, &data);
	XferLoad loader(saver.data());
	dst.xfer(loader);

	CHECK(nearFloat(dst.startPos().x, src.startPos().x));
	CHECK(nearFloat(dst.startPos().y, src.startPos().y));
	CHECK(nearFloat(dst.startPos().z, src.startPos().z));
	CHECK(nearFloat(dst.endPos().x, src.endPos().x));
	CHECK(nearFloat(dst.endPos().y, src.endPos().y));
	CHECK(nearFloat(dst.endPos().z, src.endPos().z));
	CHECK(dst.isDirty() == src.isDirty());
	CHECK(dst.isWidening() == src.isWidening());
	CHECK(dst.isDecaying() == src.isDecaying());
	CHECK(dst.widenStartFrame() == src.widenStartFrame());
	CHECK(dst.widenFinishFrame() == src.widenFinishFrame());
	CHECK(nearFloat(dst.currentWidthScalar(), src.currentWidthScalar()));
	CHECK(dst.expiryFrame() == src.expiryFrame());
}

TEST_CASE("Drawable::findLaserUpdate module lookup on live drawable (RW 0x6747C2)")
{
	CombatWorld world("");
	W3DDrawModules::registerTypedDrawModuleData(world.w.modules);
	ClientBehaviorModules::registerAll(world.w.modules);

	const char *kIni =
		"Object LaserObj\n"
		"  KindOf = IMMOBILE INERT DRAWABLE_ONLY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryHeight = 5.0\n"
		"  ClientUpdate = LaserUpdate ModuleTag_01\n"
		"    LaserLifetime = 1000\n"
		"  End\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"End\n"
		"Object PlainObj\n"
		"  KindOf = IMMOBILE INERT DRAWABLE_ONLY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryHeight = 5.0\n"
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"End\n";

	const std::string err = world.w.load(kIni, INI_LOAD_OVERWRITE, "test_lookup.ini");
	REQUIRE_MESSAGE(err.empty(), err);

	w3dsynth::MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpts;
	DrawServices drawServices(assets, world.w.modules, hostOpts);

	std::vector<std::string> problems;
	const ThingTemplate *ttLaser = world.logic->things().findTemplate("LaserObj");
	const ThingTemplate *ttPlain = world.logic->things().findTemplate("PlainObj");

	// Spawn client update modules
	std::vector<ClientEvent::ClientModule> laserModules;
	for (const auto &n : ttLaser->clientUpdateModules().nuggets())
	{
		ClientEvent::ClientModule cm;
		cm.name = n.name;
		cm.data = n.data.get();
		cm.resolved = world.logic->modules().resolveClientModule(n.name, MODULETYPE_CLIENT_UPDATE);
		laserModules.push_back(std::move(cm));
	}

	Drawable dLaser(drawServices, ttLaser, 201, 1, laserModules, problems);
	Drawable dPlain(drawServices, ttPlain, 202, 2, {}, problems);

	CHECK(findLaserUpdate(dLaser) != nullptr);
	CHECK(findLaserUpdate(dPlain) == nullptr);
	CHECK(nearFloat(dLaser.geometryHeight(), 5.0f));
}
