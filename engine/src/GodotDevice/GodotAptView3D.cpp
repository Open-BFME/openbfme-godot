// OpenBFME. GPL-3.0.
// See GodotDevice/GodotAptView3D.h.

#include "GodotDevice/GodotAptView3D.h"

#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"

#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world3d.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace godot
{

namespace
{
// out = A + D * M with A the picture over black, A + M the picture over white and D the screen behind the clip (see the header)
const char *kView3DShader = R"(shader_type canvas_item;
render_mode blend_disabled;
uniform sampler2D screen_tex : hint_screen_texture, filter_nearest;
uniform sampler2D over_black : filter_linear;
uniform sampler2D over_white : filter_linear;
void fragment() {
	vec3 d = texture(screen_tex, SCREEN_UV).rgb;
	vec3 a = texture(over_black, UV).rgb;
	vec3 m = texture(over_white, UV).rgb - a;
	COLOR = vec4(clamp(a + d * m, vec3(0.0), vec3(1.0)), 1.0);
}
)";

std::string varOf(const AptCanvasOp &op, const char *name, bool *has = nullptr)
{
	for (const auto &kv : op.nativeVars)
	{
		if (kv.first == name)
		{
			if (has)
			{
				*has = true;
			}
			return kv.second;
		}
	}
	if (has)
	{
		*has = false;
	}
	return std::string();
}

double nowMs()
{
	return (double)Time::get_singleton()->get_ticks_usec() / 1000.0;
}

Camera3D *makeCamera(SubViewport *view, const Color &clear)
{
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(clear);
	env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	Camera3D *cam = memnew(Camera3D);
	cam->set_environment(env);
	// RW 0xB544CC / 0xB544E4: clip planes 1 / 5000, horizontal field of view 0.8726646 rad (50 degrees), the vertical one from the aspect
	cam->set_keep_aspect_mode(Camera3D::KEEP_WIDTH);
	cam->set_fov(50.0f);
	cam->set_near(1.0f);
	cam->set_far(5000.0f);
	view->add_child(cam);
	cam->set_current(true);
	return cam;
}
} // namespace

AptView3DViewers::~AptView3DViewers()
{
	for (auto &kv : m_slots)
	{
		release(kv.second);
	}
}

void AptView3DViewers::setOwner(Node *owner, const Ref<RetailFileSystem> &fs)
{
	m_owner = owner;
	m_fs = fs;
}

void AptView3DViewers::release(Slot &slot)
{
	// the viewports own their children (cameras, the instancer). Godot frees a node's children when the node is deleted (NOTIFICATION_PREDELETE,
	// before the player's destructor runs): a viewport that is gone already is skipped
	for (ObjectID id : { slot.whiteId, slot.blackId })
	{
		SubViewport *v = Object::cast_to<SubViewport>(ObjectDB::get_instance(id));
		if (v)
		{
			if (v->get_parent())
			{
				v->get_parent()->remove_child(v);
			}
			memdelete(v);
		}
	}
	slot.black = slot.white = nullptr;
	slot.inst = nullptr;
}

bool AptView3DViewers::create(Slot &slot, const AptCanvasOp &op)
{
	slot.renderObject = op.renderObject;
	m_notes.insert("[S-1483] View3D " + op.renderObject + ": the frame command is the clip's _Frame, the clock is real time, the model is lit by the game's W3D light set, the "
		"frame buffer's per-blend clamp is applied once (composite A + D * M)");
	if (!m_owner || m_fs.is_null())
	{
		m_errors.insert("View3D " + op.renderObject + ": the player has no node or file system");
		return false;
	}
	if (m_shader.is_null())
	{
		m_shader.instantiate();
		m_shader->set_code(kView3DShader);
	}
	slot.black = memnew(SubViewport);
	slot.black->set_use_own_world_3d(true);
	slot.black->set_transparent_background(false);
	slot.black->set_size(Vector2i(4, 4));
	slot.blackId = slot.black->get_instance_id();
	m_owner->add_child(slot.black);
	slot.camBlack = makeCamera(slot.black, Color(0, 0, 0, 1));
	slot.white = memnew(SubViewport);
	slot.white->set_transparent_background(false);
	slot.white->set_size(Vector2i(4, 4));
	slot.whiteId = slot.white->get_instance_id();
	m_owner->add_child(slot.white);
	slot.white->set_world_3d(slot.black->find_world_3d()); // one scene, two clears
	slot.camWhite = makeCamera(slot.white, Color(1, 1, 1, 1));
	slot.inst = memnew(W3DInstancer);
	slot.black->add_child(slot.inst);
	const Dictionary setup = slot.inst->setup(m_fs);
	if (!bool(setup.get("ok", false)))
	{
		m_errors.insert("View3D " + op.renderObject + ": the W3D instancer did not set up");
		return false;
	}
	// RW 0xB54D33: the render object by name, at the identity
	slot.model = slot.inst->add_model(String(op.renderObject.c_str()));
	slot.instance = slot.model >= 0 ? slot.inst->add_instance(slot.model, Transform3D(), String(), 0.0, 1.0) : -1;
	if (slot.instance < 0)
	{
		m_errors.insert("View3D " + op.renderObject + ": the W3D model did not load");
		return false;
	}
	slot.cameraBone = slot.inst->get_bone_index_native(slot.model, "CAMERA");
	if (slot.cameraBone < 0)
	{
		// retail keeps the camera of RW 0xB5442B (turned about two axes by -pi and pi) when the model has no CAMERA bone; not ported
		m_errors.insert("View3D " + op.renderObject + ": the model has no CAMERA bone (the viewer's default camera is not ported) [S-1483]");
	}
	// RW 0xB54C1A: the animation "<name>.<name>" (none: the bind pose)
	slot.clip = op.renderObject + "." + op.renderObject;
	const Dictionary info = slot.inst->get_clip_info(slot.model, String(slot.clip.c_str()));
	if (bool(info.get("ok", false)))
	{
		slot.numFrames = (int)(int64_t)info.get("frames", 0);
		slot.frameRate = (float)(double)info.get("frame_rate", 30.0);
	}
	slot.material.instantiate();
	slot.material->set_shader(m_shader);
	slot.material->set_shader_parameter("over_black", slot.black->get_texture());
	slot.material->set_shader_parameter("over_white", slot.white->get_texture());
	return true;
}

void AptView3DViewers::beginBuild()
{
	for (auto &kv : m_slots)
	{
		kv.second.used = false;
	}
}

void AptView3DViewers::draw(RID item, const Rect2 &rect, const AptCanvasOp &op)
{
	if (op.renderObject.empty() || rect.size.x < 1.0f || rect.size.y < 1.0f)
	{
		return;
	}
	const std::string key = op.path + "|" + op.renderObject;
	auto it = m_slots.find(key);
	if (it == m_slots.end())
	{
		it = m_slots.emplace(key, Slot()).first;
		it->second.failed = !create(it->second, op);
	}
	Slot &slot = it->second;
	if (slot.failed)
	{
		return;
	}
	slot.used = true;
	// RW 0x8145DD: `_KeepAspectRatio` (true unless its text has an 'f') and `_AnimMode`
	bool hasKeep = false;
	const std::string keep = varOf(op, "_KeepAspectRatio", &hasKeep);
	slot.keepAspect = !hasKeep || AptView3DKeepAspect(keep);
	if (!slot.keepAspect)
	{
		m_errors.insert("View3D " + op.renderObject + ": _KeepAspectRatio false: the camera's own aspect is not ported, the clip's is used [S-1483]");
	}
	slot.modeName = varOf(op, "_AnimMode");
	const int mode = AptView3DAnimModeIndex(slot.modeName);
	const double now = nowMs();
	if (slot.numFrames > 0 && !slot.anim.started())
	{
		slot.anim.setAnimation(slot.numFrames, 1, mode, now); // RW 0xB54C1A -> 0xB5430E(1, mode)
	}
	// RW 0x813514: a changed "_frame=<n>" command -> setFrame(n, `_AnimMode`) (RW 0xB54BFC); the port's command is the clip's `_Frame` [S-1483]
	bool hasFrame = false;
	const std::string frame = varOf(op, "_Frame", &hasFrame);
	if (hasFrame && frame != slot.frameCommand && slot.numFrames > 0)
	{
		slot.frameCommand = frame;
		slot.anim.setAnimation(slot.numFrames, AptView3DFrameOf(frame), mode, now);
	}
	// RW 0xB5470F: the viewport is the clip's rectangle
	const Vector2i size((int)std::max(1.0f, std::round(rect.size.x)), (int)std::max(1.0f, std::round(rect.size.y)));
	if (slot.black->get_size() != size)
	{
		slot.black->set_size(size);
		slot.white->set_size(size);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_set_copy_to_backbuffer(item, true, rect);
	rs->canvas_item_set_material(item, slot.material->get_rid());
	rs->canvas_item_add_texture_rect(item, rect, slot.black->get_texture()->get_rid());
	pose(slot, now);
}

void AptView3DViewers::endBuild()
{
	for (auto &kv : m_slots)
	{
		Slot &slot = kv.second;
		if (!slot.black)
		{
			continue;
		}
		const SubViewport::UpdateMode mode = slot.used && !slot.failed ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED;
		slot.black->set_update_mode(mode);
		slot.white->set_update_mode(mode);
	}
}

void AptView3DViewers::pose(Slot &slot, double now)
{
	if (!slot.inst || slot.instance < 0)
	{
		return;
	}
	slot.anim.progress(now, slot.frameRate);
	if (slot.numFrames > 0)
	{
		slot.inst->set_instance_pose_native(slot.instance, slot.clip, (double)slot.anim.frame(), std::string(), 0.0, 0.0);
	}
	slot.inst->update_now();
	::Matrix3D m;
	if (slot.cameraBone >= 0 && slot.inst->get_bone_transform_native(slot.instance, slot.cameraBone, m))
	{
		// the bone's transform is the camera's (W3D cameras look down -Z with +Y up, as Godot's); Godot's frame is (x, z, -y) (HUD-2's globe camera)
		auto toGodot3 = [](float x, float y, float z) { return Vector3(x, z, -y); };
		const Basis basis(toGodot3(m.Row[0][0], m.Row[1][0], m.Row[2][0]), toGodot3(m.Row[0][1], m.Row[1][1], m.Row[2][1]), toGodot3(m.Row[0][2], m.Row[1][2], m.Row[2][2]));
		const Transform3D xf(basis, toGodot3(m.Row[0][3], m.Row[1][3], m.Row[2][3]));
		slot.camBlack->set_transform(xf);
		slot.camWhite->set_transform(xf);
	}
	GammaComposite::ensure(slot.camBlack);
	GammaComposite::ensure(slot.camWhite);
}

void AptView3DViewers::advance()
{
	const double now = nowMs();
	for (auto &kv : m_slots)
	{
		if (kv.second.used && !kv.second.failed)
		{
			pose(kv.second, now);
		}
	}
}

} // namespace godot
