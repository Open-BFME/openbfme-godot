// OpenBFME. GPL-3.0.
//
// W3DDebrisDraw (both editions): the flying debris piece's animation state machine, the shadow / shroud / transform setters and the
// module's snapshot block.
//
//   RW 0x004B135C doDrawModule, RW 0x004B1810 setModelName, RW 0x004B19A4 setAnimNames, RW 0x004B16E8 xfer, RW 0x004B12FE
//   setShadowsEnabled, RW 0x004B130F setFullyObscuredByShroud, RW 0x004B1339 reactToTransformChange.
//   BFME2 donor (Open-BFME-2 decompilation): doDrawModule 0x000B1A37, xfer 0x000B1DC3, setModelName 0x000B1EEB, setAnimNames 0x000B207F.

#include "doctest.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDebrisDraw.h"

TEST_CASE("W3DDebrisDraw: INITIAL -> FLYING -> FINAL, STOP freezes the final animation (RW 0x4B135C)")
{
	W3DDebrisDraw draw(nullptr, nullptr);

	// No model name yet: nothing is drawn and the machine does not advance.
	W3DDebrisDrawStep step = draw.advance(true, false);
	CHECK_FALSE(step.hasModel);
	CHECK(draw.frames() == 0);
	CHECK(draw.state() == 0);

	draw.setModelName("DebrisRock", 0xFFFFFFFF, 0);
	draw.setAnimNames("DebrisInitial", "DebrisFlying", "STOP");
	CHECK(draw.finalStop());
	CHECK(draw.state() == 0);
	CHECK(draw.frames() == 0);

	// INITIAL plays once; the first frame starts the animation.
	step = draw.advance(true, false);
	CHECK(step.hasModel);
	CHECK(step.anim == W3DDebrisDrawStep::ANIM_INITIAL);
	CHECK(step.animChanged);
	CHECK(step.mode == W3DDebrisDrawStep::MODE_ONCE);
	CHECK_FALSE(step.finalFX);
	CHECK(draw.frames() == 1);

	// The INITIAL animation completes -> FLYING (loops).
	step = draw.advance(true, true);
	CHECK(step.anim == W3DDebrisDrawStep::ANIM_FLYING);
	CHECK(step.mode == W3DDebrisDrawStep::MODE_LOOP);
	CHECK(draw.state() == 1);
	CHECK(draw.frames() == 2);

	// FLYING completes -> FINAL; the final FX fires once and STOP freezes the final animation (manual mode).
	step = draw.advance(true, true);
	CHECK(step.anim == W3DDebrisDrawStep::ANIM_FINAL);
	CHECK(step.finalFX);
	CHECK(step.mode == W3DDebrisDrawStep::MODE_MANUAL);
	CHECK(draw.state() == 2);
	CHECK(draw.frames() == 3);

	// FINAL does not advance further and the FX does not fire again.
	step = draw.advance(true, true);
	CHECK(step.anim == W3DDebrisDrawStep::ANIM_FINAL);
	CHECK_FALSE(step.finalFX);
	CHECK(draw.frames() == 4);
}

TEST_CASE("W3DDebrisDraw: a landed piece goes FINAL once past 3 frames (RW 0x4B135C)")
{
	W3DDebrisDraw draw(nullptr, nullptr);
	draw.setModelName("DebrisRock", 0, 0);
	draw.setAnimNames("DebrisInitial", "DebrisFlying", "DebrisFinal");

	// Above terrain with the animation never completing: the state stays INITIAL through 4 frames.
	for (int i = 0; i < 4; ++i)
	{
		W3DDebrisDrawStep s = draw.advance(true, false);
		CHECK(s.anim == W3DDebrisDrawStep::ANIM_INITIAL);
	}
	CHECK(draw.frames() == 4);
	CHECK(draw.state() == 0);

	// Landed (not above terrain) with more than 3 frames: straight to FINAL.
	W3DDebrisDrawStep s = draw.advance(false, false);
	CHECK(s.anim == W3DDebrisDrawStep::ANIM_FINAL);
	CHECK(s.finalFX);
	CHECK(draw.state() == 2);
}

TEST_CASE("W3DDebrisDraw: setModelName is a one-shot, the setters and the transform hook (RW 0x4B1810 / 0x4B12FE / 0x4B130F / 0x4B1339)")
{
	W3DDebrisDrawModuleData data;
	W3DDebrisDraw debris(nullptr, &data);

	// RW 0x4B1810: an empty name sets nothing; a second name does not replace the first.
	debris.setModelName("", 1, 0);
	CHECK(debris.modelName().empty());
	debris.setModelName("DebrisChunk", 0x112233, 1);
	debris.setModelName("OtherChunk", 0x445566, 2);
	CHECK(debris.modelName() == "DebrisChunk");
	CHECK(debris.modelColor() == 0x112233);
	CHECK(debris.shadowType() == 1);

	// RW 0x4B19A4: "STOP" as the final name copies the flying name into the final slot.
	debris.setAnimNames("Chunk_Init", "Chunk_Fly", "STOP");
	CHECK(debris.finalStop());
	CHECK(debris.animFinal() == "Chunk_Fly");
	debris.setAnimNames("Chunk_Init", "Chunk_Fly", "Chunk_End");
	CHECK_FALSE(debris.finalStop());
	CHECK(debris.animFinal() == "Chunk_End");

	debris.setShadowsEnabled(false);
	CHECK_FALSE(debris.shadowsEnabled());
	debris.setFullyObscuredByShroud(true);
	CHECK(debris.isFullyObscuredByShroud());

	// RW 0x4B1339: forwards the transform only when a model is set.
	debris.reactToTransformChange(nullptr, 0.0f);
	CHECK(debris.transformChangeCount() == 1u);
	W3DDebrisDraw empty(nullptr, &data);
	empty.reactToTransformChange(nullptr, 0.0f);
	CHECK(empty.transformChangeCount() == 0u);
}

TEST_CASE("W3DDebrisDraw: xfer restores the model, animations and state into a fresh module (RW 0x4B16E8)")
{
	W3DDebrisDrawModuleData data;
	W3DDebrisDraw debris(nullptr, &data);
	debris.setModelName("DebrisChunk", 0x112233, 1);
	debris.setAnimNames("Chunk_Init", "Chunk_Fly", "STOP");
	debris.advance(true, true);
	debris.advance(true, true);
	REQUIRE(debris.state() == 2);

	XferSave save;
	debris.xfer(save);

	W3DDebrisDraw loaded(nullptr, &data);
	CHECK(loaded.modelName().empty());
	XferLoad load(save.data().data(), save.data().size());
	loaded.xfer(load);
	CHECK(loaded.modelName() == "DebrisChunk");
	CHECK(loaded.modelColor() == 0x112233);
	CHECK(loaded.animInitial() == "Chunk_Init");
	CHECK(loaded.animFlying() == "Chunk_Fly");
	CHECK(loaded.animFinal() == "Chunk_Fly");
	CHECK(loaded.state() == 2);
	CHECK(loaded.frames() == 2);
	CHECK(loaded.finalStop());
}
