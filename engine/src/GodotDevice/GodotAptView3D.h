// OpenBFME. GPL-3.0.
//
// The Godot side of RotWK's Apt View3D render objects (lane UI-2, owner feedback F2 and F1): a clip tagged `_type = "View3D"` with a `_RenderObj` shows
// that W3D model, animated, through its own camera (GameClient/AptView3D.h has the retail facts: RW 0x8145DD, 0xB5442B, 0xB54D33, 0xB5470F). GuiFX.apt's
// end screen uses it for the victory / defeat rings (SFE_GoodVW / SFE_EvilVW) and the shell's MenuFrameAndBg for the menu frame and backdrop
// (SFE_MenuFrame).
//
// Retail renders the scene straight into the frame buffer over what the movie drew (RW 0x518000 with no clear), the model's surfaces blending
// with it (additive ONE / ONE and SRCALPHA / INVSRCALPHA in these models). Every such blend is affine in the colour below, so the device renders the
// model twice in one world, over black (A) and over white (A + M), and a canvas shader computes A + D * M from the screen behind the clip (D), as
// HUD-2's Palantir globe does. INFERENCE [S-1483]: the D3D frame buffer clamps after every blend, the composite once; the viewer's lighting is the
// game's global W3D light set (the device has no per-scene lights), not retail's ambient 0.5 scene.

#pragma once

#include "GameClient/AptCanvas.h"
#include "GameClient/AptView3D.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace godot
{

class RetailFileSystem;
class W3DInstancer;

class AptView3DViewers
{
public:
	AptView3DViewers() = default;
	~AptView3DViewers();
	AptView3DViewers(const AptView3DViewers &) = delete;
	AptView3DViewers &operator=(const AptView3DViewers &) = delete;

	void setOwner(Node *owner, const Ref<RetailFileSystem> &fs);
	// A canvas build starts: every viewer is marked unused (an unused one stops rendering at endBuild).
	void beginBuild();
	// The View3D placeholder `op` (nativeTag, symbolName "View3D") drawn into `item` over `rect` (window pixels). Creates the viewer on first use.
	void draw(RID item, const Rect2 &rect, const AptCanvasOp &op);
	void endBuild();
	// Per render frame: the animation clocks, the poses and the cameras.
	void advance();
	const std::set<std::string> &errors() const { return m_errors; }
	const std::set<std::string> &notes() const { return m_notes; }
	std::size_t viewerCount() const { return m_slots.size(); }

private:
	struct Slot
	{
		SubViewport *black = nullptr, *white = nullptr;
		ObjectID blackId, whiteId;
		Camera3D *camBlack = nullptr, *camWhite = nullptr;
		W3DInstancer *inst = nullptr;
		int64_t model = -1, instance = -1;
		int cameraBone = -1;
		std::string renderObject, clip;
		int numFrames = 0;
		float frameRate = 30.0f;
		AptView3DAnimation anim;
		std::string modeName, frameCommand;
		bool keepAspect = true;
		bool used = false, failed = false;
		Ref<ShaderMaterial> material;
	};
	bool create(Slot &slot, const AptCanvasOp &op);
	void pose(Slot &slot, double nowMs);
	void release(Slot &slot);

	Node *m_owner = nullptr;
	Ref<RetailFileSystem> m_fs;
	Ref<Shader> m_shader;
	std::map<std::string, Slot> m_slots; // by clip path and render object
	std::set<std::string> m_errors;
	std::set<std::string> m_notes;
};

} // namespace godot
