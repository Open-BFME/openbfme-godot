// OpenBFME unit tests. GPL-3.0.
// Lane UI-2 (owner feedback F2: "the victory / defeat menu doesn't have the ring animation or the evenstar / gondor victory circle"): GuiFX.apt's
// GoodMovie / EvilMovie clips are View3D render objects (`_RenderObj` SFE_GoodVW / SFE_EvilVW, BFME2 art\w3d\sf) and the shell's menu frame is
// SFE_MenuFrame. RotWK's viewer (RW 0xB54D33) loads the model, takes its CAMERA bone as the camera, makes lights of LIGHT_<n> bones and plays the
// animation "<name>.<name>" (RW 0xB54C1A). SKIP loudly without the retail installs.

#include "doctest.h"
#include "AptRetail.h"

#include "GameClient/AptView3D.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"

#include <cstdio>
#include <string>

TEST_CASE("ui2 retail: the end screen's and menu frame's W3D models load with their CAMERA bone and the <name>.<name> animation")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	ArchiveW3DFileSource source(mount.fs);
	WW3DAssetManager assets(source);
	for (const char *name : { "SFE_GoodVW", "SFE_EvilVW", "SFE_MenuFrame" })
	{
		INFO(name);
		std::string err;
		const RenderObjPrototype *proto = assets.Create_Render_Obj(name, &err);
		REQUIRE_MESSAGE(proto, err);
		REQUIRE(proto->Tree != nullptr);
		std::string pivots;
		for (int i = 0; i < proto->Tree->Num_Pivots(); ++i)
		{
			pivots += proto->Tree->Get_Pivot(i).Name + " ";
		}
		std::string subs;
		for (const RenderSubObject &s : proto->SubObjects)
		{
			subs += s.Name + "(" + std::to_string((int)s.Type) + "@" + std::to_string(s.BoneIndex);
			if (s.Mesh)
			{
				for (const W3dShaderStruct &sh : s.Mesh->Shaders)
				{
					subs += " src" + std::to_string(sh.SrcBlend) + "/dst" + std::to_string(sh.DestBlend) + "/dm" + std::to_string(sh.DepthMask) + "/at" + std::to_string(sh.AlphaTest);
				}
				subs += " fx" + std::to_string(s.Mesh->ShaderMaterials.size());
			}
			subs += ") ";
		}
		const HAnimClass *anim = assets.Get_HAnim(std::string(name) + "." + name, &err);
		printf("  info: %s pivots [%s] subs [%s] anim %s frames %d rate %.1f\n", name, pivots.c_str(), subs.c_str(), anim ? anim->Get_Name().c_str() : err.c_str(),
			anim ? anim->Get_Num_Frames() : 0, anim ? anim->Get_Frame_Rate() : 0.0f);
		// RW 0xB54D33: the bone named CAMERA (compared case-insensitively) is the viewer's camera; it is pivot 1 of all three
		CHECK(proto->Tree->Get_Pivot(1).Name == "CAMERA");
		REQUIRE(anim);
		CHECK(anim->Get_Frame_Rate() == doctest::Approx(30.0f));
	}
}

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include "Libraries/Source/Apt/AptRenderList.h"

TEST_CASE("ui2 retail: GuiFX.apt's ShowEndGame shows the View3D clip GoodW3D: SFE_GoodVW played to frame 90, aspect kept")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager wm(source, services);
	Shell shell(wm, factories, services, environment);
	wm.init();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			wm.update(33);
		}
	};
	tick(2);
	AptMessageBox box(wm, shell);
	const int level = box.level();
	REQUIRE(level >= 0);
	for (int i = 0; i < 30 && !wm.isAptWindowLoaded(level); ++i)
	{
		tick(1);
	}
	std::string error;
	REQUIRE_MESSAGE(wm.invokeAS(level, "ShowEndGame", { "1", "", "" }, nullptr, &error), error); // RW 0x808E5B: "1" for a good side
	tick(15);
	// GoodMovie.Good3d.GoodW3D: the View3D clip of the good side's rings (EvilMovie's EvilW3D is the evil side's)
	AptCharacterInst *w3d = wm.apt().resolvePath(wm.apt().level(level), "GoodMovie.Good3d.GoodW3D");
	REQUIRE(w3d != nullptr);
	auto member = [&](const char *name) {
		AptValue v;
		return w3d->getMember(name, v) ? v.toString() : std::string("<none>");
	};
	CHECK(member("_type") == "View3D");
	CHECK(member("_RenderObj") == "SFE_GoodVW");
	CHECK(member("_AnimMode") == "PLAY_TO_FRAME");
	CHECK(member("_KeepAspectRatio") == "true");
	CHECK(member("_Frame") == "90"); // the last frame of SFE_GOODVW.SFE_GOODVW (91 frames)
	// the render list hands the viewer settings to the device
	AptRenderList rl;
	wm.apt().buildRenderList(rl);
	bool found = false;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Placeholder && c.nativeTag && c.symbolName == "View3D" && c.renderObject == "SFE_GoodVW")
		{
			found = true;
			bool mode = false, frame = false, keep = false;
			for (const auto &kv : c.nativeVars)
			{
				mode = mode || (kv.first == "_AnimMode" && kv.second == "PLAY_TO_FRAME");
				frame = frame || (kv.first == "_Frame" && kv.second == "90");
				keep = keep || (kv.first == "_KeepAspectRatio" && kv.second == "true");
			}
			CHECK(mode);
			CHECK(frame);
			CHECK(keep);
		}
	}
	CHECK(found);
}

TEST_CASE("ui2 View3D animation: the mode names (RW 0xDC3BC0), PLAY_TO_FRAME stops at its target, ONCE at the last frame, LOOP wraps (BFME Set_Animation / Compute_Current_Frame)")
{
	CHECK(AptView3DAnimModeIndex("MANUAL") == APT_VIEW3D_MANUAL);
	CHECK(AptView3DAnimModeIndex("play_to_frame") == APT_VIEW3D_PLAY_TO_FRAME);
	CHECK(AptView3DAnimModeIndex("ONCE_BACKWARDS") == APT_VIEW3D_ONCE_BACKWARDS);
	CHECK(AptView3DAnimModeIndex("bogus") == 0);
	CHECK(AptView3DKeepAspect("true"));
	CHECK_FALSE(AptView3DKeepAspect("false"));
	CHECK(AptView3DFrameOf("90") == 90);
	// the end screen: created with Set_Animation(1.0, PLAY_TO_FRAME) at t = 0, then the clip's _Frame 90 at t = 100 ms
	AptView3DAnimation a;
	CHECK(a.setAnimation(91, 1, APT_VIEW3D_PLAY_TO_FRAME, 0.0));
	CHECK_FALSE(a.setAnimation(91, 1, APT_VIEW3D_PLAY_TO_FRAME, 10.0)); // the same request is skipped (RW 0xB5432F)
	a.progress(100.0, 30.0f);
	CHECK(a.frame() == doctest::Approx(1.0f)); // the frame stops at the first target
	CHECK(a.setAnimation(91, 90, APT_VIEW3D_PLAY_TO_FRAME, 100.0));
	a.progress(1100.0, 30.0f); // one second at 30 frames per second
	CHECK(a.frame() == doctest::Approx(31.0f));
	a.progress(5100.0, 30.0f);
	CHECK(a.frame() == doctest::Approx(90.0f)); // held at the target
	// ONCE stops at the last frame; LOOP wraps by (frames - 1)
	AptView3DAnimation once;
	once.setAnimation(10, 0, APT_VIEW3D_ONCE, 0.0);
	once.progress(1000.0, 30.0f);
	CHECK(once.frame() == doctest::Approx(9.0f));
	AptView3DAnimation loop;
	loop.setAnimation(10, 0, APT_VIEW3D_LOOP, 0.0);
	loop.progress(100.0, 30.0f);
	CHECK(loop.frame() == doctest::Approx(3.0f));
	loop.progress(400.0, 30.0f);
	CHECK(loop.frame() == doctest::Approx(3.0f)); // 12 - 9
}
