// OpenBFME. GPL-3.0. SwayClientUpdate (both editions): RW 0x008CD514..0x008CD9CD, DONOR BFME2 0x004C90E0..0x004C9593. Synthetic INI only; no retail files.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/NameKeyGenerator.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ClientBehaviorModules.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/SwayClientUpdate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DVegetationSway.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
const char *kSwaySyntheticIni =
	"Object SwayTestTree\n"
	"  Draw = W3DDefaultDraw ModuleTag_Draw\n"
	"  End\n"
	"  ClientUpdate = SwayClientUpdate ModuleTag_Sway\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"End\n";

SwayClientUpdate *findSway(const Drawable *d)
{
	if (!d)
	{
		return nullptr;
	}
	for (const auto &m : d->clientModules())
	{
		if (auto *s = dynamic_cast<SwayClientUpdate *>(m.get()))
		{
			return s;
		}
	}
	return nullptr;
}
} // namespace

// Expectations from RW 0x008CD514, RW 0x008CD51A, RW 0x008CD55F (DONOR BFME2 0x004C90E0 / 0x004C9125).
TEST_CASE("sway client update: getModuleName, getModuleNameKey, updateSway and zero randomness reset (RW 0x008CD514 / 0x008CD51A / 0x008CD55F)")
{
	CHECK(std::strcmp(SwayClientUpdate::getModuleName(), "SwayClientUpdate") == 0);
	NameKeyGenerator gen;
	gen.init();
	const NameKeyType k1 = SwayClientUpdate::getModuleNameKey(gen);
	const NameKeyType k2 = gen.nameToKey("SwayClientUpdate");
	CHECK(k1 != NAMEKEY_INVALID);
	CHECK(k1 == k2);

	combattest::CombatWorld cw;
	ClientBehaviorModules::registerAll(cw.w.modules);
	W3DDrawModules::registerTypedDrawModuleData(cw.w.modules);
	REQUIRE(cw.w.load(kSwaySyntheticIni).empty());

	ArchiveW3DFileSource source(cw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, (*cw.logic));
	ClientEventRecorder rec((*cw.logic));
	cw.logic->setClientHooks(&rec);

	const ThingTemplate *tt = cw.w.things.findTemplate("SwayTestTree");
	REQUIRE(tt != nullptr);
	Object *o = cw.logic->newObject(tt, cw.players.getNeutralPlayer()->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o != nullptr);
	const float objYaw = 0.5f;
	{ Coord3D p{ 100.0f, 100.0f, 0.0f }; o->setPosition(&p); }
	o->setOrientation(objYaw);
	dm.applyEvents(rec.take());

	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d != nullptr);
	d->setOrientation(objYaw);

	SwayClientUpdate *sway = findSway(d);
	REQUIRE(sway != nullptr);
	CHECK(!sway->isUnported());
	CHECK(sway->curVersion() == -1);
	CHECK(sway->isSwaying());

	W3DClientRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seed(42);

	W3DVegetationSway::Breeze b;
	b.direction = 1.25f;
	b.directionX = std::sin(b.direction);
	b.directionY = std::cos(b.direction);
	b.intensity = 0.08f;
	b.lean = 0.03f;
	b.randomness = 0.0f;
	b.breezePeriod = 50;
	b.breezeVersion = 3;

	sway->updateSway(b, rng, 6.0f);
	CHECK(sway->curValue() == 0.0f);
	CHECK(sway->curAngleLimit() == 0.08f);
	CHECK(sway->curDelta() == (1.0f / (6.0f * 50.0f)) * 6.2831854820251465f);
	CHECK(sway->leanAngle() == 0.03f);
	CHECK(sway->curVersion() == 3);
	CHECK(sway->directionSin() == doctest::Approx(std::sin(b.direction - objYaw)).epsilon(1e-4)); // tolerance: atan2/sin rounding from drawable basis
	CHECK(sway->directionCos() == doctest::Approx(std::cos(b.direction - objYaw)).epsilon(1e-4)); // tolerance: atan2/cos rounding from drawable basis

	// Non-zero randomness scales within [1 - r/2, 1 + r/2] (RW 0x008CD593..0x008CD637)
	b.randomness = 0.4f;
	b.breezeVersion = 4;
	sway->updateSway(b, rng, 6.0f);
	CHECK(sway->curVersion() == 4);
	CHECK(sway->curAngleLimit() >= 0.08f * 0.8f - 1e-6f); // tolerance: random draw in [1 - r/2, 1 + r/2]
	CHECK(sway->curAngleLimit() <= 0.08f * 1.2f + 1e-6f); // tolerance: random draw in [1 - r/2, 1 + r/2]
	CHECK(sway->leanAngle() >= 0.03f * 0.8f - 1e-6f); // tolerance: random draw in [1 - r/2, 1 + r/2]
	CHECK(sway->leanAngle() <= 0.03f * 1.2f + 1e-6f); // tolerance: random draw in [1 - r/2, 1 + r/2]

	cw.logic->setClientHooks(nullptr);
}

// Expectations from RW 0x008CD675 (DONOR BFME2 0x004C923B).
TEST_CASE("sway client update: clientUpdate visibility gate, breezeVersion update, instanceMatrix rotation, and burned/dead stop (RW 0x008CD675)")
{
	combattest::CombatWorld cw;
	ClientBehaviorModules::registerAll(cw.w.modules);
	W3DDrawModules::registerTypedDrawModuleData(cw.w.modules);
	REQUIRE(cw.w.load(kSwaySyntheticIni).empty());

	ArchiveW3DFileSource source(cw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, (*cw.logic));
	ClientEventRecorder rec((*cw.logic));
	cw.logic->setClientHooks(&rec);

	const ThingTemplate *tt = cw.w.things.findTemplate("SwayTestTree");
	REQUIRE(tt != nullptr);
	Object *o = cw.logic->newObject(tt, cw.players.getNeutralPlayer()->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o != nullptr);
	{ Coord3D p{ 50.0f, 50.0f, 0.0f }; o->setPosition(&p); }
	dm.applyEvents(rec.take());

	Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d != nullptr);
	SwayClientUpdate *sway = findSway(d);
	REQUIRE(sway != nullptr);

	W3DClientRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seed(7);

	W3DVegetationSway::Breeze b;
	b.direction = 0.7f;
	b.directionX = std::sin(b.direction);
	b.directionY = std::cos(b.direction);
	b.intensity = 0.1f;
	b.lean = 0.05f;
	b.randomness = 0.0f;
	b.breezePeriod = 10;
	b.breezeVersion = 1;

	// Version changed (-1 -> 1): updates even when visible == false (RW 0x008CD688..0x008CD69E)
	sway->clientUpdate(b, rng, /*visible=*/false, /*burnedOrDead=*/false, 6.0f);
	CHECK(sway->curVersion() == 1);
	CHECK(d->hasInstanceMatrix());
	const float angleAfterFirst = sway->curAngle();
	CHECK(angleAfterFirst == doctest::Approx(std::sin(sway->curDelta()) * 0.1f + 0.05f)); // tolerance: sin float rounding

	// Same version and visible == false: skips update (RW 0x008CD6A5..0x008CD6B4)
	const float valBeforeHidden = sway->curValue();
	sway->clientUpdate(b, rng, /*visible=*/false, /*burnedOrDead=*/false, 6.0f);
	CHECK(sway->curValue() == valBeforeHidden);
	CHECK(sway->curAngle() == angleAfterFirst);

	// Same version and visible == true: advances phase and rotates instanceMatrix
	sway->clientUpdate(b, rng, /*visible=*/true, /*burnedOrDead=*/false, 6.0f);
	CHECK(sway->curValue() == valBeforeHidden + sway->curDelta());
	CHECK(sway->curAngle() == doctest::Approx(std::sin(sway->curValue()) * 0.1f + 0.05f)); // tolerance: sin float rounding
	// the drawable's transform reflects the tilted instance matrix
	REQUIRE(!d->entries().empty());
	Matrix3D xf;
	REQUIRE(d->instanceMatrixTransform(d->entries()[0], xf));
	CHECK(std::fabs(xf.Row[0][2]) + std::fabs(xf.Row[1][2]) > 1e-4f); // tolerance: non-zero tilt from pre-rotate X/Y

	// Drawable::updateSway steps every SwayClientUpdate module of the drawable with the drawable's own client random (RW 0x008CD675)
	const float valBeforeDrawable = sway->curValue();
	d->updateSway(b, /*visible=*/true, /*burnedOrDead=*/false);
	CHECK(sway->curValue() != valBeforeDrawable);

	// Burned or dead stops sway (RW 0x008CD917..0x008CD937)
	sway->clientUpdate(b, rng, /*visible=*/true, /*burnedOrDead=*/true, 6.0f);
	CHECK(!sway->isSwaying());
	const float stoppedVal = sway->curValue();
	sway->clientUpdate(b, rng, /*visible=*/true, /*burnedOrDead=*/false, 6.0f);
	CHECK(sway->curValue() == stoppedVal);

	cw.logic->setClientHooks(nullptr);
}

// Expectations from RW 0x008CD93F and RW 0x008CD9CD (DONOR BFME2 0x004C9505 / 0x004C9593).
TEST_CASE("sway client update: xfer round-trip, XFER_CRC early-out, and loadPostProcess (RW 0x008CD93F / 0x008CD9CD)")
{
	SwayClientUpdateModuleData data;
	SwayClientUpdate src(nullptr, &data);

	W3DClientRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seed(11);
	W3DVegetationSway::Breeze b;
	b.direction = 1.0f;
	b.intensity = 0.06f;
	b.lean = 0.02f;
	b.randomness = 0.0f;
	b.breezePeriod = 20;
	b.breezeVersion = 5;
	src.updateSway(b, rng, 6.0f);

	// XFER_CRC early-out (RW 0x008CD94D..0x008CD956)
	XferCRC crcXfer;
	const std::uint32_t crcBefore = crcXfer.getCRC();
	src.xfer(crcXfer);
	CHECK(crcXfer.getCRC() == crcBefore);

	// Save -> Load round-trip (RW 0x008CD958..0x008CD9C5)
	XferSave save;
	src.xfer(save);
	REQUIRE(!save.data().empty());

	SwayClientUpdate dst(nullptr, &data);
	XferLoad load(save.data());
	dst.xfer(load);
	CHECK(dst.curValue() == src.curValue());
	CHECK(dst.curAngle() == src.curAngle());
	CHECK(dst.curDelta() == src.curDelta());
	CHECK(dst.curAngleLimit() == src.curAngleLimit());
	CHECK(dst.leanAngle() == src.leanAngle());
	CHECK(dst.curVersion() == src.curVersion());
	CHECK(dst.isSwaying() == src.isSwaying());
	CHECK(dst.directionSin() == src.directionSin());
	CHECK(dst.directionCos() == src.directionCos());

	// loadPostProcess re-runs updateSway (RW 0x008CD9CD -> jmp 0x008CD55F)
	b.breezeVersion = 9;
	b.intensity = 0.12f;
	dst.loadPostProcess(b, rng, 6.0f);
	CHECK(dst.curVersion() == 9);
	CHECK(dst.curAngleLimit() == 0.12f);
}
