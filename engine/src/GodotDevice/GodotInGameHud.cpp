// OpenBFME. GPL-3.0.
// See GodotInGameHud.h.

#include "GodotDevice/GodotInGameHud.h"
#include "GodotDevice/GodotPackedTexture.h"
#include "Common/SpecialPower.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GameTextTableSource.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/ShellServices.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "GameClient/Radar.h"
#include "GameClient/CursorFile.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameHud.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalCamera.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GodotDevice/GodotAptPlayer.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DMaterial.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

// Godot key -> the DirectInput scan code of ZH KeyDefs (MetaEvent.h KeyCode); 0 for a key the CommandMap cannot name.
// lane INPUT-1: the scan codes are written ::KEY_*: inside namespace godot an unqualified KEY_A / KEY_1 / KEY_F1 / KEY_UP / KEY_SPACE is godot::Key's
// keycode (65, 49, ...), which no CommandMap record carries. That made every letter, digit, F, arrow, Space, Tab and Enter key reach the HUD with Godot's
// code: the control groups (Ctrl+1 / 1), S (STOP), X (SELECT_ALL), the camera keys and the rest never matched; only the modifier keys worked.
int dikOf(Key key)
{
	const int k = (int)key;
	static const int letters[26] = { ::KEY_A, ::KEY_B, ::KEY_C, ::KEY_D, ::KEY_E, ::KEY_F, ::KEY_G, ::KEY_H, ::KEY_I, ::KEY_J, ::KEY_K, ::KEY_L, ::KEY_M, ::KEY_N, ::KEY_O, ::KEY_P, ::KEY_Q, ::KEY_R, ::KEY_S, ::KEY_T, ::KEY_U, ::KEY_V, ::KEY_W, ::KEY_X, ::KEY_Y, ::KEY_Z };
	static const int digits[10] = { ::KEY_0, ::KEY_1, ::KEY_2, ::KEY_3, ::KEY_4, ::KEY_5, ::KEY_6, ::KEY_7, ::KEY_8, ::KEY_9 };
	if (k >= (int)Key::KEY_A && k <= (int)Key::KEY_Z)
	{
		return letters[k - (int)Key::KEY_A];
	}
	if (k >= (int)Key::KEY_0 && k <= (int)Key::KEY_9)
	{
		return digits[k - (int)Key::KEY_0];
	}
	if (k >= (int)Key::KEY_F1 && k <= (int)Key::KEY_F10)
	{
		return ::KEY_F1 + (k - (int)Key::KEY_F1);
	}
	switch (key)
	{
		case Key::KEY_F11: return ::KEY_F11;
		case Key::KEY_F12: return ::KEY_F12;
		case Key::KEY_ESCAPE: return ::KEY_ESC;
		case Key::KEY_BACKSPACE: return ::KEY_BACKSPACE;
		case Key::KEY_ENTER: return ::KEY_ENTER;
		case Key::KEY_KP_ENTER: return ::KEY_KPENTER; // DIK_NUMPADENTER: its own scan code (RotWK's CommandMap names no KEY_KPENTER)
		case Key::KEY_KP_DIVIDE: return ::KEY_KPSLASH;
		case Key::KEY_KP_MULTIPLY: return ::KEY_KPSTAR;
		case Key::KEY_KP_SUBTRACT: return ::KEY_KPMINUS;
		case Key::KEY_KP_ADD: return ::KEY_KPPLUS;
		case Key::KEY_KP_PERIOD: return ::KEY_KPDOT;
		case Key::KEY_SPACE: return ::KEY_SPACE;
		case Key::KEY_TAB: return ::KEY_TAB;
		case Key::KEY_UP: return ::KEY_UP;
		case Key::KEY_DOWN: return ::KEY_DOWN;
		case Key::KEY_LEFT: return ::KEY_LEFT;
		case Key::KEY_RIGHT: return ::KEY_RIGHT;
		case Key::KEY_HOME: return ::KEY_HOME;
		case Key::KEY_END: return ::KEY_END;
		case Key::KEY_PAGEUP: return ::KEY_PGUP;
		case Key::KEY_PAGEDOWN: return ::KEY_PGDN;
		case Key::KEY_INSERT: return ::KEY_INS;
		case Key::KEY_DELETE: return ::KEY_DEL;
		case Key::KEY_MINUS: return ::KEY_MINUS;
		case Key::KEY_EQUAL: return ::KEY_EQUAL;
		case Key::KEY_BRACKETLEFT: return ::KEY_LBRACKET;
		case Key::KEY_BRACKETRIGHT: return ::KEY_RBRACKET;
		case Key::KEY_SEMICOLON: return ::KEY_SEMICOLON;
		case Key::KEY_APOSTROPHE: return ::KEY_APOSTROPHE;
		case Key::KEY_QUOTELEFT: return ::KEY_TICK;
		case Key::KEY_BACKSLASH: return ::KEY_BACKSLASH;
		case Key::KEY_COMMA: return ::KEY_COMMA;
		case Key::KEY_PERIOD: return ::KEY_PERIOD;
		case Key::KEY_SLASH: return ::KEY_SLASH;
		case Key::KEY_SHIFT: return ::KEY_LSHIFT;
		case Key::KEY_CTRL: return ::KEY_LCTRL;
		case Key::KEY_ALT: return ::KEY_LALT;
		default: break;
	}
	if (k >= (int)Key::KEY_KP_0 && k <= (int)Key::KEY_KP_9)
	{
		static const int kp[10] = { ::KEY_KP0, ::KEY_KP1, ::KEY_KP2, ::KEY_KP3, ::KEY_KP4, ::KEY_KP5, ::KEY_KP6, ::KEY_KP7, ::KEY_KP8, ::KEY_KP9 };
		return kp[k - (int)Key::KEY_KP_0];
	}
	return 0;
}

// The view the HUD picks, projects and draws the radar box with: the pose the player SEES (the interpolated eye / target, the viewport and the field of view of the displayed
// camera); the camera's own movement (lookAt, the translator) stays on the current client pose.
class DisplayedView : public PinholeView
{
public:
	TacticalCamera *cam = nullptr;
	Coord3D position() const override { return cam->position(); }
	void lookAt(const Coord3D &world) override { cam->lookAt(world); }
};
} // namespace

struct HudDevice : public AptNativeHook
{
	Ref<RetailFileSystem> fs;
	GameWorld *world = nullptr;
	Camera3D *camera = nullptr;
	AptMenuPlayer *player = nullptr;
	std::unique_ptr<INIEnvironment> metaEnv;
	MetaMap meta;
	MouseSettings mouse;
	RecordingShellServices services;
	// lane CAM-1: the retail tactical camera; the Godot camera is set from it every frame (applyCamera)
	std::unique_ptr<TacticalCamera> cam;
	DisplayedView shown; // picking and projection follow the displayed (interpolated) pose
	double clientClock = 0.0;
	double updateMs = 0.0, cameraMs = 0.0, cursorMs = 0.0; ///< SMOOTH-1: the parts of the last _process
	bool edgeScroll = false;
	float fogShift = 0.0f; ///< lane PLAY-1: the last w3d_fog_shift sent
	// lane SPELL-2 (review r2): the game text of a standalone HUD (data/lotr.str); an attached HUD uses its shell's. Before the HUD: it outlives it
	std::unique_ptr<GameTextSource> ownedText;
	std::unique_ptr<InGameHud> hud;
	int keyState = 0;
	ModifierTracker mods; // lane INPUT-1 r2: Ctrl / Shift / Alt per side
	int freeCameraToggles = 0; // lane INPUT-1: Ctrl+Z presses that toggled the free camera
	unsigned screenshotsDone = 0; // lane INPUT-1: TAKE_SCREENSHOT requests saved
	unsigned keyEvents = 0;       // lane INPUT-1: InputEventKey events handleInput saw
	int screenshotNumber = 0;
	String lastScreenshot;
	bool ready = false;
	bool ownsPlayer = false;
	std::uint64_t playerId = 0;
	Vector2 lastPointer;
	int localPlayerIndex = -1;                   ///< SMOOTH-1: the local player's index (the radar's own blips, from the snapshot)
	std::vector<Ref<InputEvent>> pendingInput;   ///< SMOOTH-1: input that arrived while the logic worker ran a frame, replayed in order when it is idle
	unsigned long long deferredInput = 0;

	std::unique_ptr<AptArchiveFileSource> source;
	std::unique_ptr<AptTextureStore> textures;
	MappedImageCollection images;
	std::unique_ptr<INIEnvironment> imageEnv;
	std::map<std::string, Ref<ImageTexture>> godotTextures;
	Ref<ImageTexture> radarTexture;
	Ref<ImageTexture> radarShroud;                  ///< HUD-2: ScrollShroud with the shroud's alpha
	bool radarShroudTried = false;
	unsigned long long radarShroudVersion = ~0ull; ///< VIS-1: the shroud edges the picture was made with
	std::uint64_t radarVersion = 0;
	int radarSize = 128;
	Ref<ImageTexture> radarOverlay;                ///< lane RADAR-1: W3DRadar's object overlay (+ 0x147C)
	std::vector<std::uint32_t> radarOverlayTexels;
	unsigned radarOverlayFrame = ~0u;
	Rect2 radarPicture;                            ///< lane RADAR-1: the picture rectangle RenderRadar drew (the view box's frame)
	std::vector<std::string> drawErrors;
	std::set<std::string> drawNotes; ///< lane HUD-4: the device's inference notes (S-1482: a .jpg + .png texture)
	unsigned drawnImages = 0, drawnTimers = 0, skippedImages = 0;
	// lane HUD-2: the Palantir globe (AptPalantir::RenderGlobe, see drawGlobe)
	Node *owner = nullptr;
	struct GlobeLayer
	{
		SubViewport *view = nullptr;
		W3DInstancer *inst = nullptr;
	};
	GlobeLayer globeAdd, globeMul; ///< SPHERE01 alone over black (A), SPHERE02 alone over white (M)
	W3DInstancer *globe = nullptr; ///< globeAdd.inst once both layers loaded
	Ref<Shader> globeShader;
	Ref<ShaderMaterial> globeMaterial;
	// lane QA-1: the probe's own canvas items (globeProbe): freed with this node, before the globe material
	std::vector<RID> probeItems;
	bool globeTried = false;
	unsigned globeDraws = 0;
	bool ensureGlobe();
	bool makeGlobeLayer(GlobeLayer &layer, const char *name, const Color &clear, const char *hidden);
	void globeProbe();
	// the mouse cursors (Mouse.ini names, the install's loose Data\Cursors files)
	std::map<std::string, MouseCursorEntry> cursorTable;
	struct CursorSet
	{
		bool loaded = false;
		CursorImage image;
		std::vector<Ref<ImageTexture>> frames;
	};
	std::map<std::string, CursorSet> cursors;
	std::string cursorDir, cursorName;
	int cursorStep = 0;
	double cursorClock = 0.0;
	std::vector<std::string> cursorErrors;
	void updateCursor(double delta, const std::string &wanted);
	void loadCursorTable(ArchiveFileSystem &fs);

	void drawPlaceholder(const AptCanvasOp &op, RID item) override;
	Ref<ImageTexture> textureFor(const std::string &file);
	// lane HUD-5: the drawable decorations (InGameHud::iconOps) drawn under the Palantir each frame
	RID iconItem;
	ObjectFilter veterancyFilter;
	bool haveVeterancyFilter = false;
	unsigned iconOpsDrawn = 0;
	void drawIconOps(RID parent);
	void applyCamera(const Vector2 &window, double alpha);
};

// SMOOTH-1 (S-810): a bound method that reads or changes the live game waits until the logic worker has finished its frame (the HUD sees a completed
// frame, never one being computed); the per-frame _process does not wait (InGameHud::update skips the frames the worker is busy in)
static void waitLogic(const HudDevice *d);

Ref<ImageTexture> HudDevice::textureFor(const std::string &file)
{
	auto it = godotTextures.find(file);
	if (it != godotTextures.end())
	{
		return it->second;
	}
	Ref<ImageTexture> tex;
	const AptTextureStore::Entry &e = textures->get(file);
	if (e.ok && !e.rgba.empty())
	{
		PackedByteArray px;
		px.resize((int64_t)e.rgba.size());
		memcpy(px.ptrw(), e.rgba.data(), e.rgba.size());
		tex = ImageTexture::create_from_image(Image::create_from_data(e.width, e.height, false, Image::FORMAT_RGBA8, px));
	}
	else
	{
		// the button images are packed: art\compiledtextures\<first two letters>\<stem> (the mapped image names the .tga it was made from). Lane HUD-4 (QA-1 U11):
		// the extensions in RotWK's order (RW 0x530D29, the loader the Apt player uses, GodotPackedTexture.cpp): .dds, else .jpg with its .png; the radar's
		// ScrollShroud.tga is packed as scrollshroud.jpg + .png
		std::string stem = file.substr(0, file.find_last_of('.'));
		std::string lower;
		for (char ch : stem)
		{
			lower += (char)std::tolower((unsigned char)ch);
		}
		bool paired = false;
		Ref<Image> img = lower.size() > 2 ? loadPackedTexture(*source, "art/compiledtextures/" + lower.substr(0, 2) + "/" + lower, &paired) : Ref<Image>();
		if (img.is_valid())
		{
			tex = ImageTexture::create_from_image(img);
			if (paired)
			{
				drawNotes.insert("[S-1482] texture " + file + ": the packed .jpg's colour with its .png as the alpha (the loader's merge of the two streams was not read)");
			}
		}
		else if (drawErrors.size() < 100)
		{
			drawErrors.push_back("texture " + file + ": " + e.error + "; no packed .dds / .jpg under art/compiledtextures");
		}
	}
	godotTextures[file] = tex;
	return tex;
}

namespace
{
Rect2 windowRect(const AptCanvasOp &op, Vector2 pts[4])
{
	const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
	const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
	float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
	for (int i = 0; i < 4; ++i)
	{
		const float sx = op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx;
		const float sy = op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty;
		pts[i] = Vector2(sx * op.scaleX + op.offsetX, sy * op.scaleY + op.offsetY);
		minx = std::min(minx, pts[i].x);
		maxx = std::max(maxx, pts[i].x);
		miny = std::min(miny, pts[i].y);
		maxy = std::max(maxy, pts[i].y);
	}
	return Rect2(minx, miny, maxx - minx, maxy - miny);
}
std::string varOf(const AptCanvasOp &op, const char *name)
{
	for (const auto &kv : op.nativeVars)
	{
		if (kv.first == name)
		{
			return kv.second;
		}
	}
	return std::string();
}
} // namespace

// lane HUD-2: the Palantir's globe. TARGET FACTS (RotWK game.dat, caveat S-001):
// - AptPalantir::RenderGlobe (callback RW 0x6D42EE) hands the clip's two corner points (+0.5, truncated) to the W3DPalantir (vtable RW 0xBE5748) +0x3C,
//   RW 0x504019, which draws only when its render object exists (+0x1C);
// - its set-up (RW 0x5044B4): the render object is the W3D model "palantir" (BFME2 W3D.big art\w3d\pa\palantir.w3d: HLod PALANTIR, the meshes SPHERE02
//   (material "black": emissive white, multiplicative shader src ZERO / dst SRC_COLOR, detail INVSCALE) and SPHERE01 ("Material #85": emissive 146, vertex
//   colours, additive ONE / ONE, detail SCALE), each with two LinearOffset stages of PalantirA.tga (UPerSec / VPerSec in the file)), in a SimpleSceneClass of
//   its own (vtable RW 0xBE56C8) with the ambient light (0, 0, 0) and no light, at the identity transform (RW 0x504563); a CameraClass with clip planes
//   0.1 / 5000 (RW 0x5337C0), the horizontal field of view 0.8726646 rad (50 degrees, the vertical one derived) and the aspect 1 (RW 0x533630);
// - each draw (RW 0x504019) sets the CAMERA's transform (+0x18, vtable +0x54): the identity rotated about X by -pi/2 (RW 0xBE43C0; MSVCR71 sin 0xA3CF90 and
//   cos 0xA3CF84) and moved 160 (RW 0xD9AEC8) along its new Z column: the camera stands at (0, 160, 0) looking down -Y with -Z up, at the domes (SPHERE01 /
//   SPHERE02 are caps on pivots near (0.7, -100, -0.4) turned to face +Y). Nothing turns; the motion is the stages' UV scrolling. The camera's viewport is
//   the clip rectangle over the display size (RW 0x47DFDB), the scene is rendered (RW 0x518000).
// - the scene is rendered straight into the movie's frame buffer (RW 0x518000; the custom scene at RW 0x504230 clears depth / stencil and keeps the colour),
//   the static sorter (RW 0x576240) draws sort level 2 before 1: SPHERE01 adds, then SPHERE02 multiplies; both blends ignore the texture alpha. Over a
//   frame buffer colour D the result is clamp(D + A) * M with A SPHERE01's colour (0 outside it) and M SPHERE02's (1 outside it), in gamma space.
// Device: each dome is rendered ALONE by the world renderer (W3D shaders and mappers) in a SubViewport of its own, SPHERE01 over black (A) and SPHERE02 over
// white (M): one layer over a constant background survives the shader's linear conversion exactly, so the two pictures hold retail's gamma values. A canvas
// shader with blending disabled then computes clamp(D + A) * M from the screen behind the clip (D, the movie's black CommandBackground disc: A * M).
// INFERENCE (stop S-762): the mapper clock is the renderer's, not WW3D's sync time; the pictures are 256 x 256 and stretched to the clip.
static const char *kGlobeShader = R"(shader_type canvas_item;
render_mode blend_disabled;
uniform sampler2D screen_tex : hint_screen_texture, filter_nearest;
uniform sampler2D add_tex : filter_linear;
uniform sampler2D mul_tex : filter_linear;
void fragment() {
	vec3 d = texture(screen_tex, SCREEN_UV).rgb;
	vec3 a = texture(add_tex, UV).rgb;
	vec3 m = texture(mul_tex, UV).rgb;
	COLOR = vec4(clamp(d + a, vec3(0.0), vec3(1.0)) * m, 1.0);
}
)";

bool HudDevice::makeGlobeLayer(GlobeLayer &layer, const char *name, const Color &clear, const char *hidden)
{
	layer.view = memnew(SubViewport);
	layer.view->set_name(name);
	layer.view->set_use_own_world_3d(true);
	layer.view->set_transparent_background(false);
	layer.view->set_size(Vector2i(256, 256));
	layer.view->set_update_mode(SubViewport::UPDATE_ALWAYS);
	owner->add_child(layer.view);
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(clear);
	env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	env->set_ambient_light_color(Color(0, 0, 0, 1)); // RW 0x5045B3: the scene's ambient (0, 0, 0)
	env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	Camera3D *cam = memnew(Camera3D);
	cam->set_environment(env);
	cam->set_keep_aspect_mode(Camera3D::KEEP_WIDTH);
	cam->set_fov(50.0f);
	cam->set_near(0.1f);
	cam->set_far(5000.0f);
	// RW 0x50402C .. 0x50415D: R = RotX(-pi/2) of the identity (columns (1, 0, 0), (0, c, s), (0, -s, c) with s = sin, c = cos of the float -pi/2), the
	// position R's Z column * 160. A W3D camera looks down its -Z with +Y up, as a Godot one; Godot's frame is (x, z, -y) = P, so the node's transform is P * C.
	const double a = (double)-1.5707963705062866f;
	const double s = std::sin(a), c = std::cos(a);
	const Vector3 colX(1, 0, 0), colY(0, (float)c, (float)s), colZ(0, (float)-s, (float)c); // W3D columns of R
	auto toGodot3 = [](const Vector3 &v) { return Vector3(v.x, v.z, -v.y); };
	cam->set_transform(Transform3D(Basis(toGodot3(colX), toGodot3(colY), toGodot3(colZ)), toGodot3(colZ * 160.0f)));
	layer.view->add_child(cam);
	cam->set_current(true);
	layer.inst = memnew(W3DInstancer);
	layer.view->add_child(layer.inst);
	const Dictionary setup = layer.inst->setup(fs);
	const int64_t model = bool(setup.get("ok", false)) ? layer.inst->add_model("palantir") : -1;
	const int64_t instance = model >= 0 ? layer.inst->add_instance(model, Transform3D(), String(), 0.0, 1.0) : -1; // RW 0x504563: the model at the identity
	PackedStringArray hide;
	hide.push_back(hidden);
	if (instance < 0 || !layer.inst->set_instance_hidden_subobjects(instance, hide))
	{
		drawErrors.push_back(std::string("palantir globe: the W3D model palantir did not load or has no ") + hidden);
		return false;
	}
	return true;
}

bool HudDevice::ensureGlobe()
{
	if (globeTried)
	{
		return globe != nullptr;
	}
	globeTried = true;
	if (!owner || fs.is_null())
	{
		drawErrors.push_back("palantir globe: no node or file system");
		return false;
	}
	if (!makeGlobeLayer(globeAdd, "PalantirGlobeAdd", Color(0, 0, 0, 1), "PALANTIR.SPHERE02") || !makeGlobeLayer(globeMul, "PalantirGlobeMul", Color(1, 1, 1, 1), "PALANTIR.SPHERE01"))
	{
		return false;
	}
	globeShader.instantiate();
	globeShader->set_code(kGlobeShader);
	globeMaterial.instantiate();
	globeMaterial->set_shader(globeShader);
	globeMaterial->set_shader_parameter("add_tex", globeAdd.view->get_texture());
	globeMaterial->set_shader_parameter("mul_tex", globeMul.view->get_texture());
	globe = globeAdd.inst;
	if (!OS::get_singleton()->get_environment("OPENBFME_GLOBE_PROBE").is_empty())
	{
		globeProbe();
	}
	return true;
}

// A documented probe (OPENBFME_GLOBE_PROBE=1, windowed): the scroll clock of both layers is frozen at 7.5 s and the composite is drawn twice more at window
// pixels (0, 40) and (300, 40), 256 x 256 at 1:1, over a black and over a (0.25, 0.5, 0.75) square; OPENBFME_GLOBE_DUMP=<prefix> saves A and M
// (<prefix>-add.png, <prefix>-mul.png). tools/render/globe_probe.py compares the screenshot with clamp(D + A) * M.
void HudDevice::globeProbe()
{
	for (GlobeLayer *l : { &globeAdd, &globeMul })
	{
		l->inst->set_playing(false);
		l->inst->set_global_time(7.5);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const Color backs[2] = { Color(0, 0, 0, 1), Color(0.25f, 0.5f, 0.75f, 1) };
	for (int i = 0; i < 2; ++i)
	{
		const Rect2 rect(Vector2(i == 0 ? 0.0f : 300.0f, 40.0f), Vector2(256, 256));
		RID back = rs->canvas_item_create();
		rs->canvas_item_set_parent(back, static_cast<Node2D *>(owner)->get_canvas_item());
		rs->canvas_item_set_z_index(back, 4000 + i * 2);
		rs->canvas_item_add_rect(back, rect, backs[i]);
		RID comp = rs->canvas_item_create();
		rs->canvas_item_set_parent(comp, static_cast<Node2D *>(owner)->get_canvas_item());
		rs->canvas_item_set_z_index(comp, 4001 + i * 2);
		rs->canvas_item_set_copy_to_backbuffer(comp, true, rect);
		rs->canvas_item_set_material(comp, globeMaterial->get_rid());
		probeItems.push_back(back);
		probeItems.push_back(comp);
		rs->canvas_item_add_texture_rect(comp, rect, globeAdd.view->get_texture()->get_rid());
	}
}

void HudDevice::drawPlaceholder(const AptCanvasOp &op, RID item)
{
	if (!hud)
	{
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	Vector2 pts[4];
	const Rect2 r = windowRect(op, pts);
	if (op.symbolName == "AptPalantir::RenderGlobe")
	{
		// lane HUD-2 (see ensureGlobe): the clip's rectangle, the colour transform of the clip is not applied (retail draws the scene directly)
		if (r.size.x < 1.0f || r.size.y < 1.0f || !ensureGlobe())
		{
			return;
		}
		rs->canvas_item_set_copy_to_backbuffer(item, true, r); // D: the frame buffer behind the clip
		rs->canvas_item_set_material(item, globeMaterial->get_rid());
		rs->canvas_item_add_texture_rect(item, r, globeAdd.view->get_texture()->get_rid());
		++globeDraws;
		if (globeDraws == 8)
		{
			// a diagnostic for the viewers: OPENBFME_GLOBE_DUMP=<prefix> saves the two layers once (<prefix>-add.png, <prefix>-mul.png)
			const String dump = OS::get_singleton()->get_environment("OPENBFME_GLOBE_DUMP");
			if (!dump.is_empty())
			{
				globeAdd.view->get_texture()->get_image()->save_png(dump + String("-add.png"));
				globeMul.view->get_texture()->get_image()->save_png(dump + String("-mul.png"));
			}
		}
		return;
	}
	if (op.symbolName == "AptPalantir::RenderRadar")
	{
		Radar &radar = hud->radar();
		if (!radar.ready() || !radar.drawn(false)) // lane RADAR-1: RW 0x44FE0B (Player::hasRadar not ported, S-2453)
		{
			return;
		}
		// lane HUD-2, W3DRadar::draw (RW 0x44FDF3, Radar.h): the picture fills the clip rectangle reduced to the map's aspect (RW 0x6D89F2); a map without art
		// gets the letterbox bands first; the objects, then the ScrollShroud shroud over the whole clip rectangle, then the view box
		if (radarTexture.is_null())
		{
			const bool art = radar.hasArt();
			const int w = art ? radar.artWidth() : radarSize, h = art ? radar.artHeight() : radarSize;
			const std::vector<std::uint8_t> &img = art ? radar.artImage() : radar.terrainImage(radarSize);
			PackedByteArray px;
			px.resize((int64_t)img.size());
			memcpy(px.ptrw(), img.data(), img.size());
			radarTexture = ImageTexture::create_from_image(Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, px));
		}
		int ul[2], lr[2];
		radar.drawRect((int)r.position.x, (int)r.position.y, (int)r.size.x, (int)r.size.y, ul, lr);
		const Rect2 sq((float)ul[0], (float)ul[1], (float)(lr[0] - ul[0]), (float)(lr[1] - ul[1]));
		if (!radar.hasArt())
		{
			// RW 0x44FE8C: the bands beside a non-square map (0xFF323232; the 0xFF000000 edge lines are not drawn)
			const Color band(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f);
			if (sq.position.x > r.position.x)
			{
				rs->canvas_item_add_rect(item, Rect2(r.position.x, r.position.y, sq.position.x - r.position.x, r.size.y), band);
				rs->canvas_item_add_rect(item, Rect2(sq.position.x + sq.size.x, r.position.y, r.position.x + r.size.x - sq.position.x - sq.size.x, r.size.y), band);
			}
			if (sq.position.y > r.position.y)
			{
				rs->canvas_item_add_rect(item, Rect2(r.position.x, r.position.y, r.size.x, sq.position.y - r.position.y), band);
				rs->canvas_item_add_rect(item, Rect2(r.position.x, sq.position.y + sq.size.y, r.size.x, r.position.y + r.size.y - sq.position.y - sq.size.y), band);
			}
		}
		rs->canvas_item_add_texture_rect(item, sq, radarTexture->get_rid());
		// SMOOTH-1 (S-810): this draw callback runs whenever the canvas draws, also while the logic worker runs a frame: the objects come from the
		// presented snapshot (the completed frame the drawables show), never from the live objects
		const std::shared_ptr<const LogicSnapshot> snap = world && world->hud_game() ? world->hud_game()->presentedSnapshot() : nullptr;
		// lane RADAR-1: the object overlay (W3DRadar::draw RW 0x450108 .. 0x4501FA): rebuilt when the client frame % 6 == 0 (and once at the start, + 0x1464),
		// a 128 x 128 texture drawn over the picture's rectangle with UV (0,1)-(1,0) (texture row 0 at the bottom); RenderRadar's tint is -1 (RW 0x6D427C)
		radarPicture = sq;
		radar.setPicture(ul[0], ul[1], lr[0] - ul[0], lr[1] - ul[1]); // the events' pings (RW 0x44DE58, InGameHud::updateRadarEvents)
		const unsigned cf = radar.clientFrame();
		if (snap && (radarOverlay.is_null() || Radar::overlayDue(cf, radarOverlayFrame)))
		{
			radarOverlayFrame = cf;
			radar.renderOverlay(*snap, cf, 0xFFFFFFFFu, radarOverlayTexels);
			const int n = Radar::kCells;
			PackedByteArray px;
			px.resize((int64_t)n * n * 4);
			uint8_t *w = px.ptrw();
			for (int y = 0; y < n; ++y)
			{
				const std::uint32_t *row = &radarOverlayTexels[(size_t)(n - 1 - y) * (size_t)n];
				for (int x = 0; x < n; ++x, w += 4)
				{
					w[0] = (uint8_t)(row[x] >> 16);
					w[1] = (uint8_t)(row[x] >> 8);
					w[2] = (uint8_t)row[x];
					w[3] = (uint8_t)(row[x] >> 24);
				}
			}
			Ref<::godot::Image> img = ::godot::Image::create_from_data(n, n, false, ::godot::Image::FORMAT_RGBA8, px);
			if (radarOverlay.is_null())
			{
				radarOverlay = ImageTexture::create_from_image(img);
			}
			else
			{
				radarOverlay->update(img);
			}
		}
		if (radarOverlay.is_valid())
		{
			rs->canvas_item_add_texture_rect(item, sq, radarOverlay->get_rid());
		}
		// the shroud: ScrollShroud's colours, the alpha the shroud cells wrote (Radar::shroudAlpha), rebuilt when the shroud's edges moved
		// SMOOTH-1: the shroud from the presented snapshot's view (no live ShroudManager read beside the worker)
		const ShroudView *shroudView = snap ? snap->shroud.get() : nullptr;
		if (Radar::shroudVersion(shroudView) != radarShroudVersion || (radarShroud.is_null() && radarShroudTried == false))
		{
			radarShroudVersion = Radar::shroudVersion(shroudView);
			radarShroudTried = true;
			radarShroud.unref();
			const std::vector<std::int16_t> alpha = shroudView ? radar.shroudAlpha(*shroudView) : std::vector<std::int16_t>();
			const ::Image *mapped = alpha.empty() ? nullptr : images.findImageByName("ScrollShroud");
			if (!alpha.empty() && !mapped && drawErrors.size() < 100)
			{
				drawErrors.push_back("radar: the mapped image ScrollShroud is unknown");
			}
			Ref<ImageTexture> scroll = mapped ? textureFor(mapped->filename) : Ref<ImageTexture>();
			if (scroll.is_valid())
			{
				Ref<Image> src = scroll->get_image();
				src->decompress();
				src->convert(Image::FORMAT_RGBA8);
				const int n = Radar::kShroudTexture;
				src->resize(n, n, Image::INTERPOLATE_NEAREST);
				src->flip_y(); // shown with UV (0,1)-(1,0) like every radar image (stop S-761)
				for (int y = 0; y < n; ++y)
				{
					for (int x = 0; x < n; ++x)
					{
						const std::int16_t a = alpha[(size_t)y * (size_t)n + (size_t)x];
						if (a >= 0)
						{
							Color c = src->get_pixel(x, y);
							c.a = (float)a / 255.0f;
							src->set_pixel(x, y, c);
						}
					}
				}
				radarShroud = ImageTexture::create_from_image(src);
			}
		}
		if (radarShroud.is_valid())
		{
			rs->canvas_item_add_texture_rect(item, r, radarShroud->get_rid());
		}
		return;
	}
	if (op.symbolName == "AptPalantir::RenderRadarViewBox")
	{
		// lane RADAR-1: the view box (RW 0x6D429D -> RW 0x50434F) inside the movie's mask (RadarPings.instance1): the corners (Radar::viewBoxCorners) of the picture
		// RenderRadar drew this frame, the band (RW 0x503C33) as two triangles per edge textured with the mapped image RadarViewBoxEdge, white (RW 0x50431F)
		Radar &radar = hud->radar();
		float corners[8];
		if (!radar.ready() || !radar.drawn(false) || radarPicture.size.x <= 0.0f ||
		    !radar.viewBoxCorners(radarPicture.position.x, radarPicture.position.y, (int)radarPicture.size.x, (int)radarPicture.size.y, corners))
		{
			return;
		}
		const ::Image *edge = images.findImageByName("RadarViewBoxEdge");
		Ref<ImageTexture> tex = edge ? textureFor(edge->filename) : Ref<ImageTexture>();
		if (tex.is_null())
		{
			if (drawErrors.size() < 100)
			{
				drawErrors.push_back("radar: the mapped image RadarViewBoxEdge or its texture is unknown");
			}
			return;
		}
		const float thickness = Radar::viewBoxThickness(edge->getImageWidth(), (unsigned)hud->windowWidth(), corners);
		float outer[8], inner[8];
		Radar::viewBoxBand(corners, thickness, outer, inner);
		const Vector2 uvLL(edge->uvLo[0], edge->uvLo[1]), uvHL(edge->uvHi[0], edge->uvLo[1]), uvHH(edge->uvHi[0], edge->uvHi[1]), uvLH(edge->uvLo[0], edge->uvHi[1]);
		PackedVector2Array pts, uvs;
		PackedColorArray cols;
		PackedInt32Array idx;
		for (int i = 0; i < 4; ++i)
		{
			const int n = (i + 1) % 4;
			const Vector2 a(inner[i * 2], inner[i * 2 + 1]), b(outer[i * 2], outer[i * 2 + 1]), c(outer[n * 2], outer[n * 2 + 1]), d(inner[n * 2], inner[n * 2 + 1]);
			const Vector2 tri[6] = { a, b, c, a, c, d };
			const Vector2 tuv[6] = { uvLL, uvHL, uvHH, uvLL, uvHH, uvLH };
			for (int k = 0; k < 6; ++k)
			{
				idx.push_back((int32_t)pts.size());
				pts.push_back(tri[k]);
				uvs.push_back(tuv[k]);
				cols.push_back(Color(1, 1, 1, 1));
			}
		}
		rs->canvas_item_add_triangle_array(item, idx, pts, cols, uvs, PackedInt32Array(), PackedFloat32Array(), tex->get_rid());
		return;
	}
	if (op.symbolName == "RenderImage" || op.symbolName == "RenderImageDisabled")
	{
		// lane HUD-2: a RenderImage clip without `_imageMap` (the Palantir portrait, CommandUI.Portrait) finds its image by its own path
		const AptPalantir::NativeImage *img = hud->palantir() ? hud->palantir()->imageForClip(op.path, varOf(op, "_imageMap")) : nullptr;
		// lane SPELL-2: the spell store's buttons ("SpellStore/Buttons/Spell%d", RW 0x822B23); RenderImageDisabled draws the same image grey
		AptPalantir::NativeImage storeImage;
		if ((!img || img->image.empty()) && hud->spellStore())
		{
			storeImage.image = hud->storeImageForClip(op.path);
			img = storeImage.image.empty() ? img : &storeImage;
		}
		AptPalantir::NativeImage greyed;
		if (img && op.symbolName == "RenderImageDisabled")
		{
			greyed = *img;
			greyed.grayscale = true;
			img = &greyed;
		}
		if (!img || img->image.empty())
		{
			++skippedImages;
			return;
		}
		const ::Image *mapped = images.findImageByName(img->image);
		if (!mapped)
		{
			if (drawErrors.size() < 100)
			{
				drawErrors.push_back("mapped image " + img->image + " is unknown");
			}
			return;
		}
		Ref<ImageTexture> tex = textureFor(mapped->filename);
		if (tex.is_null())
		{
			return;
		}
		const float tw = (float)tex->get_width(), th = (float)tex->get_height();
		const Rect2 src(mapped->uvLo[0] * tw, mapped->uvLo[1] * th, (mapped->uvHi[0] - mapped->uvLo[0]) * tw, (mapped->uvHi[1] - mapped->uvLo[1]) * th);
		Color mod = img->grayscale ? Color(0.45f, 0.45f, 0.45f, 1.0f) : Color(1, 1, 1, 1);
		// lane PLAY-1: the clip's cumulative colour multiplies the image (SpellStore.apt's _disabled frame places the image at alpha 0.396: a locked power is
		// dimmed; drawn at full alpha every power looked buyable). INFERENCE: retail's RenderImage draw was not read; the add terms are not applied
		mod = Color(mod.r * op.placeholderColor[0], mod.g * op.placeholderColor[1], mod.b * op.placeholderColor[2], mod.a * op.placeholderColor[3]);
		rs->canvas_item_add_texture_rect_region(item, r, tex->get_rid(), src, mod);
		++drawnImages;
		return;
	}
	if (op.symbolName == "TimerOverlay")
	{
		const float t = hud->palantir() ? hud->palantir()->timerFor(varOf(op, "_timerId")) : -1.0f;
		if (t < 0.0f || t >= 1.0f)
		{
			return;
		}
		// the part of the button still to build is darkened as a pie from the top, clockwise (ZH GadgetButtonDrawInverseClock)
		const Vector2 c = r.get_center();
		const float rad = std::max(r.size.x, r.size.y) * 0.5f;
		PackedVector2Array poly;
		PackedColorArray cols;
		poly.push_back(c);
		const int steps = 24;
		for (int i = 0; i <= steps; ++i)
		{
			const float a = -1.5707963f + 6.2831853f * (t + (1.0f - t) * (float)i / (float)steps);
			poly.push_back(c + Vector2(std::cos(a), std::sin(a)) * rad);
		}
		for (int64_t i = 0; i < poly.size(); ++i)
		{
			cols.push_back(Color(0, 0, 0, 0.55f));
		}
		rs->canvas_item_add_polygon(item, poly, cols);
		return;
	}
}

// lane HUD-5: the ops of InGameHud::iconOps (GameClient/DrawableIconUI.h) into one canvas item under the Apt player's (drawn first among the node's children).
// INFERENCE (S-1951): the construction text uses Godot's fallback font at 13 pixels (the in-game UI's drawable caption font is not read)
void HudDevice::drawIconOps(RID parent)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs || !hud)
	{
		return;
	}
	if (!iconItem.is_valid())
	{
		iconItem = rs->canvas_item_create();
		rs->canvas_item_set_parent(iconItem, parent);
		rs->canvas_item_set_draw_index(iconItem, -1000);
	}
	rs->canvas_item_clear(iconItem);
	{
		IconUISettings is = hud->iconSettings();
		is.zoom = cam ? cam->getZoom() : 1.0f;
		hud->setIconUISettings(is);
	}
	auto col = [](std::uint32_t argb) { return Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f); };
	Ref<Font> font = ThemeDB::get_singleton() ? ThemeDB::get_singleton()->get_fallback_font() : Ref<Font>();
	const int fontSize = 13;
	iconOpsDrawn = 0;
	for (const IconUIOp &op : hud->iconOps())
	{
		switch (op.kind)
		{
			case IconUIOp::FILL_RECT:
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y, op.w, op.h), col(op.color));
				break;
			case IconUIOp::OPEN_RECT: // Display +0xE0 (width 1): the four edges
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y, op.w, 1), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y + op.h - 1, op.w, 1), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y + 1, 1, op.h - 2), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x + op.w - 1, op.y + 1, 1, op.h - 2), col(op.color));
				break;
			case IconUIOp::IMAGE:
			{
				const ::Image *mapped = images.findImageByName(op.image);
				Ref<ImageTexture> tex = mapped ? textureFor(mapped->filename) : Ref<ImageTexture>();
				if (tex.is_null())
				{
					continue;
				}
				const float tw = (float)tex->get_width(), th = (float)tex->get_height();
				const Rect2 src(mapped->uvLo[0] * tw, mapped->uvLo[1] * th, (mapped->uvHi[0] - mapped->uvLo[0]) * tw, (mapped->uvHi[1] - mapped->uvLo[1]) * th);
				rs->canvas_item_add_texture_rect_region(iconItem, Rect2(op.x, op.y, op.w, op.h), tex->get_rid(), src, col(op.color));
				break;
			}
			case IconUIOp::TEXT:
			{
				if (font.is_null())
				{
					continue;
				}
				const String text = String::utf8(op.text.c_str());
				const Vector2 size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, fontSize);
				const Vector2 at(op.x - std::floor(size.x / 2.0f), op.y + font->get_ascent(fontSize)); // RW 0x677F1B: x - width / 2, the string's top at y
				font->draw_string(iconItem, at + Vector2(1, 1), text, HORIZONTAL_ALIGNMENT_LEFT, -1, fontSize, col(op.dropColor));
				font->draw_string(iconItem, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, fontSize, col(op.color));
				break;
			}
		}
		++iconOpsDrawn;
	}
}

void HudDevice::loadCursorTable(ArchiveFileSystem &fs)
{
	std::string dir;
	if (OS *os = OS::get_singleton())
	{
		const String env = os->get_environment("ROTWK_INSTALL");
		if (!env.is_empty())
		{
			dir = env.utf8().get_data();
		}
	}
	if (dir.empty() && FileAccess::file_exists("user://install-paths.cfg"))
	{
		const String cfg = FileAccess::get_file_as_string("user://install-paths.cfg");
		for (const String &line : cfg.split("\n"))
		{
			if (line.begins_with("ROTWK_INSTALL="))
			{
				dir = line.substr(14).strip_edges().utf8().get_data();
			}
		}
	}
	cursorDir = dir.empty() ? std::string() : dir + "/data/cursors";
	std::vector<std::uint8_t> ini;
	std::string error;
	if (!fs.readFile("data/ini/mouse.ini", ini, &error) || !ParseMouseCursors(std::string(ini.begin(), ini.end()), cursorTable, &error))
	{
		cursorErrors.push_back("mouse.ini: " + error);
	}
}

void HudDevice::updateCursor(double delta, const std::string &wanted)
{
	if (cursorDir.empty())
	{
		return;
	}
	auto it = cursorTable.find(wanted);
	if (it == cursorTable.end() || it->second.image.empty())
	{
		return;
	}
	CursorSet &set = cursors[wanted];
	if (!set.loaded)
	{
		set.loaded = true;
		std::string path;
		for (const char *ext : { "", ".ani", ".cur" })
		{
			path = FindFileNoCase(cursorDir, it->second.image + ext);
			if (!path.empty())
			{
				break;
			}
		}
		std::string error;
		std::vector<std::uint8_t> bytes;
		if (path.empty())
		{
			cursorErrors.push_back("cursor " + wanted + ": no file " + it->second.image);
		}
		else
		{
			Ref<FileAccess> f = FileAccess::open(String::utf8(path.c_str()), FileAccess::READ);
			if (f.is_null())
			{
				cursorErrors.push_back("cursor " + wanted + ": cannot read " + path);
			}
			else
			{
				const PackedByteArray all = f->get_buffer((int64_t)f->get_length());
				bytes.assign(all.ptr(), all.ptr() + all.size());
				if (!DecodeCursorFile(bytes, set.image, &error))
				{
					cursorErrors.push_back("cursor " + wanted + ": " + error);
				}
				else
				{
					for (const std::vector<std::uint8_t> &frame : set.image.frames)
					{
						PackedByteArray px;
						px.resize((int64_t)frame.size());
						memcpy(px.ptrw(), frame.data(), frame.size());
						set.frames.push_back(ImageTexture::create_from_image(Image::create_from_data(set.image.width, set.image.height, false, Image::FORMAT_RGBA8, px)));
					}
				}
			}
		}
	}
	if (set.frames.empty())
	{
		return;
	}
	if (wanted != cursorName)
	{
		cursorName = wanted;
		cursorStep = 0;
		cursorClock = 0.0;
	}
	else
	{
		cursorClock += delta;
		const double step = (double)set.image.jiffies[(size_t)cursorStep % set.image.jiffies.size()] / 60.0;
		if (set.image.sequence.size() <= 1 || cursorClock < step)
		{
			return;
		}
		cursorClock = 0.0;
		cursorStep = (cursorStep + 1) % (int)set.image.sequence.size();
	}
	const int frame = set.image.sequence[(size_t)cursorStep % set.image.sequence.size()];
	const int hx = it->second.hotX >= 0 ? it->second.hotX : set.image.hotX, hy = it->second.hotY >= 0 ? it->second.hotY : set.image.hotY;
	Input::get_singleton()->set_custom_mouse_cursor(set.frames[(size_t)frame], Input::CURSOR_ARROW, Vector2((float)hx, (float)hy));
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y)
static Vector3 toGodotAxes(const Coord3D &c)
{
	return Vector3(c.x, c.z, -c.y);
}

void HudDevice::applyCamera(const Vector2 &window, double alpha)
{
	if (!camera || !cam)
	{
		return;
	}
	cam->setViewport((int)window.x, (int)window.y);
	const float a = (float)alpha;
	auto mix = [a](const Coord3D &p, const Coord3D &q) { return Coord3D{ p.x + (q.x - p.x) * a, p.y + (q.y - p.y) * a, p.z + (q.z - p.z) * a }; };
	const Coord3D eyeS = mix(cam->previousEye(), cam->eye()), targetS = mix(cam->previousTarget(), cam->target());
	shown.cam = cam.get();
	shown.setScreen((int)window.x, (int)window.y);
	shown.setFov(cam->verticalFov());
	shown.set(eyeS, targetS);
	const Vector3 eye = toGodotAxes(eyeS);
	const Vector3 target = toGodotAxes(targetS);
	// retail: horizontal field of view 50 degrees (View + 0x6C), near 10, far 1800 (RW 0x48B7B1); the view plane's height follows the viewport aspect
	camera->set_keep_aspect_mode(Camera3D::KEEP_WIDTH);
	camera->set_fov((real_t)(cam->horizontalFov() * 180.0f / 3.14159265f));
	camera->set_near((real_t)cam->nearPlane());
	camera->set_far((real_t)cam->farPlane());
	// lane PLAY-1: the free camera's fog shift (0 at retail heights), sent only when it changes
	const float shift = cam->fogShift();
	if (shift != fogShift)
	{
		fogShift = shift;
		W3D_Set_Fog_Shift(shift);
	}
	Transform3D t;
	t.origin = eye;
	t.basis = Basis::looking_at((target - eye).normalized(), Vector3(0, 1, 0));
	camera->set_global_transform(t);
}

static void waitLogic(const HudDevice *d)
{
	if (d && d->world && d->world->hud_game())
	{
		d->world->hud_game()->waitIdle();
	}
}

void InGameHudNode::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs", "world", "camera", "local_player", "options"), &InGameHudNode::setup, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("attach", "fs", "world", "camera", "player", "local_player", "options"), &InGameHudNode::attach, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("is_ready"), &InGameHudNode::is_ready);
	ClassDB::bind_method(D_METHOD("get_selection"), &InGameHudNode::get_selection);
	ClassDB::bind_method(D_METHOD("get_logic_selection"), &InGameHudNode::get_logic_selection);
	ClassDB::bind_method(D_METHOD("get_message_log"), &InGameHudNode::get_message_log);
	ClassDB::bind_method(D_METHOD("get_state"), &InGameHudNode::get_state);
	ClassDB::bind_method(D_METHOD("get_command_map"), &InGameHudNode::get_command_map);
	ClassDB::bind_method(D_METHOD("get_report"), &InGameHudNode::get_report);
	ClassDB::bind_method(D_METHOD("get_mapped_image", "name"), &InGameHudNode::get_mapped_image);
	ClassDB::bind_method(D_METHOD("get_spellbook_state"), &InGameHudNode::get_spellbook_state);
	ClassDB::bind_method(D_METHOD("get_icon_ui"), &InGameHudNode::get_icon_ui); // lane HUD-5
	ClassDB::bind_method(D_METHOD("select_object", "id"), &InGameHudNode::select_object); // lane HUD-5
	ClassDB::bind_method(D_METHOD("open_spell_store"), &InGameHudNode::open_spell_store);
	ClassDB::bind_method(D_METHOD("close_spell_store"), &InGameHudNode::close_spell_store);
	ClassDB::bind_method(D_METHOD("spell_store_click", "index"), &InGameHudNode::spell_store_click);
	ClassDB::bind_method(D_METHOD("press_spell_slot", "slot"), &InGameHudNode::press_spell_slot);
	ClassDB::bind_method(D_METHOD("get_frame_timings"), &InGameHudNode::get_frame_timings);
	ClassDB::bind_method(D_METHOD("get_placement"), &InGameHudNode::get_placement);
	ClassDB::bind_method(D_METHOD("get_move_hints"), &InGameHudNode::get_move_hints);
	ClassDB::bind_method(D_METHOD("press_command_button", "template_name"), &InGameHudNode::press_command_button);
	ClassDB::bind_method(D_METHOD("get_command_buttons"), &InGameHudNode::get_command_buttons);
	ClassDB::bind_method(D_METHOD("inject_mouse_move", "position"), &InGameHudNode::inject_mouse_move);
	ClassDB::bind_method(D_METHOD("inject_mouse_button", "button", "down", "position", "double_click"), &InGameHudNode::inject_mouse_button, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("inject_key", "dik_code", "down"), &InGameHudNode::inject_key);
	ClassDB::bind_method(D_METHOD("inject_mouse_wheel", "notches", "position"), &InGameHudNode::inject_mouse_wheel);
	ClassDB::bind_method(D_METHOD("get_camera"), &InGameHudNode::get_camera);
	ClassDB::bind_method(D_METHOD("set_edge_scroll", "on"), &InGameHudNode::set_edge_scroll);
	ClassDB::bind_method(D_METHOD("set_free_camera", "on"), &InGameHudNode::set_free_camera);
	ClassDB::bind_method(D_METHOD("camera_look_at", "sage_xy"), &InGameHudNode::camera_look_at);
	ClassDB::bind_method(D_METHOD("camera_set_height", "height_above_ground"), &InGameHudNode::camera_set_height);
	ClassDB::bind_method(D_METHOD("pixel_to_world", "pixel"), &InGameHudNode::pixel_to_world);
	ClassDB::bind_method(D_METHOD("world_to_pixel", "world_xy"), &InGameHudNode::world_to_pixel);
	ClassDB::bind_method(D_METHOD("pick_probe", "pixel", "id"), &InGameHudNode::pick_probe);
	ClassDB::bind_method(D_METHOD("find_button_window", "path"), &InGameHudNode::find_button_window);
	ClassDB::bind_method(D_METHOD("get_radar_square"), &InGameHudNode::get_radar_square);
	ClassDB::bind_method(D_METHOD("world_to_radar_pixel", "world_xy"), &InGameHudNode::world_to_radar_pixel);
	ClassDB::bind_method(D_METHOD("create_radar_event", "world_xy", "type"), &InGameHudNode::create_radar_event);
	ClassDB::bind_method(D_METHOD("dump_tree", "max_depth"), &InGameHudNode::dump_tree, DEFVAL(4));
	ClassDB::bind_method(D_METHOD("invoke_at", "path", "function", "args"), &InGameHudNode::invoke_at, DEFVAL(PackedStringArray()));
}

InGameHudNode::InGameHudNode() : d(std::make_unique<HudDevice>()) {}

InGameHudNode::~InGameHudNode()
{
	if (d->fogShift != 0.0f)
	{
		W3D_Set_Fog_Shift(0.0f); // lane PLAY-1: the free camera's fog shift goes with the game
	}
	// the player may be gone before this node (the game scene frees its nodes in tree order)
	const bool playerAlive = d->playerId != 0 && ObjectDB::get_instance(ObjectID(d->playerId)) != nullptr;
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		for (const RID &item : d->probeItems)
		{
			rs->free_rid(item); // lane QA-1: the globe probe's items go before the globe material
		}
		if (d->iconItem.is_valid())
		{
			rs->free_rid(d->iconItem); // lane HUD-5
			d->iconItem = RID();
		}
	}
	d->probeItems.clear();
	if (playerAlive)
	{
		d->player->set_native_hook(nullptr);
		d->player->set_input_forwarded(false);
		if (d->ownsPlayer)
		{
			d->player->attach_window_manager(nullptr);
		}
	}
	d->hud.reset();
}

bool InGameHudNode::is_ready() const
{
	return d->ready;
}

Dictionary InGameHudNode::setup(const Ref<RetailFileSystem> &fs, GameWorld *world, Camera3D *camera, const String &local_player, const Dictionary &options)
{
	return attach(fs, world, camera, nullptr, local_player, options);
}

Dictionary InGameHudNode::attach(const Ref<RetailFileSystem> &fs, GameWorld *world, Camera3D *camera, AptMenuPlayer *shellPlayer, const String &local_player, const Dictionary &options)
{
	Dictionary result;
	Array errors;
	d->ready = false;
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("the retail archives are not mounted");
	}
	else if (!world || !world->hud_game() || !world->hud_world())
	{
		errors.push_back("setup needs a GameWorld with a loaded map");
	}
	else if (!camera)
	{
		errors.push_back("setup needs the game camera");
	}
	if (!errors.is_empty())
	{
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	d->fs = fs;
	d->owner = this;
	d->world = world;
	d->camera = camera;
	LiveGame &game = *world->hud_game();
	Player *local = game.players().findPlayerWithName(local_player.utf8().get_data());
	if (!local)
	{
		errors.push_back("no player named " + local_player);
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	game.players().setLocalPlayer(local);
	d->localPlayerIndex = local->getPlayerIndex();
	std::string error;
	ArchiveFileSystem &afs = *fs->archive_fs();
	if (!MouseSettings::load(afs, d->mouse, &error))
	{
		errors.push_back(toGodot(error));
	}
	d->metaEnv = std::make_unique<INIEnvironment>();
	d->metaEnv->fileSystem = &afs;
	d->meta.registerBlocks(*d->metaEnv);
	if (!d->meta.load(*d->metaEnv, "Data\\INI\\CommandMap.ini", &error) || !d->meta.load(*d->metaEnv, "CommandMap.ini", &error))
	{
		errors.push_back(toGodot(error));
	}
	if (!errors.is_empty())
	{
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	bool showObjectHealth = false; // lane HUD-5
	// lane CAM-1: the retail tactical camera over the map's terrain, looking at the start position (options.camera_start, Godot axes)
	Viewport *vp = get_viewport();
	const Vector2 window = vp ? vp->get_visible_rect().get_size() : Vector2(1024, 768);
	{
		CameraSettings gd;
		if (!CameraSettings::load(afs, gd, &error))
		{
			errors.push_back(toGodot(error));
		}
		else if (!game.map().hasHeightMap)
		{
			errors.push_back("the map has no height map: the camera cannot start");
		}
		else if (!options.has("camera_start"))
		{
			errors.push_back("setup needs options.camera_start (the Vector3 the camera looks at, Godot axes)");
		}
		else
		{
			const Vector3 s = options["camera_start"];
			d->cam = std::make_unique<TacticalCamera>(gd);
			d->cam->setViewport((int)window.x, (int)window.y);
			const LoadedMap &lm = game.map();
			d->cam->startMap(game.logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, Coord3D{ s.x, -s.z, 0.0f });
			// SMOOTH-1 (S-810): the lock / follow target from the presented snapshot (the camera runs every render frame, also while the worker runs)
			LiveGame *g = &game;
			d->cam->setObjectSource([g](::ObjectID id, Coord3D &pos) { return g->presentedObjectPosition(id, pos); });
			d->edgeScroll = (bool)options.get("edge_scroll", false);
			d->cam->setFreeCamera((bool)options.get("free_camera", false)); // lane PLAY-1: the owner's option (not retail)
		}
		// lane HUD-5: GameData's ShowObjectHealth / VeterancyPipDrawObjectFilter for the drawable decorations
		d->veterancyFilter = gd.veterancyPipFilter;
		d->haveVeterancyFilter = gd.haveVeterancyPipFilter;
		showObjectHealth = gd.showObjectHealth;
		if (!errors.is_empty())
		{
			result["ok"] = false;
			result["errors"] = errors;
			return result;
		}
	}
	d->applyCamera(window, 1.0);
	// lane SPELL-2 (review r2): the game text the HUD's screens read (the spell store's help): the shell's when attached, else data/lotr.str
	GameTextSource *gameText = nullptr;
	if (shellPlayer && shellPlayer->shell_object())
	{
		gameText = shellPlayer->shell_object()->environment().gameText;
	}
	else
	{
		auto text = std::make_unique<GameTextTableSource>();
		std::vector<std::uint8_t> bytes;
		std::string textError;
		if (!afs.readFile("data/lotr.str", bytes, &textError) || !text->table.parse(bytes, &textError))
		{
			errors.push_back(toGodot("data/lotr.str: " + textError));
		}
		else
		{
			d->ownedText = std::move(text);
			gameText = d->ownedText.get();
		}
	}
	InGameHud::Config cfg{ game, *world->hud_world(), afs, d->services, d->shown, d->mouse, d->meta, gameText, shellPlayer ? shellPlayer->window_manager() : nullptr, shellPlayer ? shellPlayer->shell_object() : nullptr };
	d->hud = std::make_unique<InGameHud>(cfg);
	d->hud->input().attachCamera(*d->cam);
	d->hud->setWindowSize((int)window.x, (int)window.y);
	d->hud->input().commandTranslator().setUseAlternateMouse((bool)options.get("alternate_mouse", false));
	{
		// lane INPUT-1 r2: RotWK's keyboard setup (RW 0x63F024) sets OurLanguage 2 for a German keyboard layout (de-DE / CH / AT / LU / LI): the CommandMap's
		// Y and Z follow the keys' labels. options.german_keyboard (true / false) overrides the layout the display server reports
		bool german = false;
		if (options.has("german_keyboard"))
		{
			german = (bool)options["german_keyboard"];
		}
		else if (DisplayServer *ds = DisplayServer::get_singleton())
		{
			german = ds->keyboard_get_layout_language(ds->keyboard_get_current_layout()).to_lower().begins_with("de");
		}
		d->hud->input().metaTranslator().setGermanKeyboard(german);
	}
	if (!d->hud->boot(&error))
	{
		errors.push_back(toGodot(error));
	}
	for (const std::string &e : d->hud->errors())
	{
		errors.push_back(toGodot(e));
	}
	// the mapped images (button images, portraits) and the textures they name
	d->source = std::make_unique<AptArchiveFileSource>(afs);
	d->textures = std::make_unique<AptTextureStore>(*d->source);
	d->loadCursorTable(afs);
	d->imageEnv = std::make_unique<INIEnvironment>();
	d->imageEnv->fileSystem = &afs;
	d->images.registerBlocks(d->imageEnv->blocks);
	try
	{
		INI ini(*d->imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
	}
	catch (const std::exception &e)
	{
		errors.push_back(toGodot(std::string("mapped images: ") + e.what()));
	}
	// the player that draws the HUD's movie
	if (shellPlayer)
	{
		d->player = shellPlayer;
		d->ownsPlayer = false;
		d->playerId = shellPlayer->get_instance_id(); // the game scene's shell-mode player: it steps and draws its window manager, the HUD takes the input
		d->player->set_input_forwarded(true);
	}
	else
	{
		if (!d->player)
		{
			d->player = memnew(AptMenuPlayer);
			add_child(d->player);
			d->ownsPlayer = true;
			d->playerId = d->player->get_instance_id();
		}
		Dictionary bootConfig;
		bootConfig["levels"] = Array();
		Dictionary boot = d->player->boot(fs, bootConfig);
		Array bootErrors = boot.get("errors", Array());
		for (int64_t i = 0; i < bootErrors.size(); ++i)
		{
			errors.push_back(bootErrors[i]);
		}
		d->player->set_show_placeholders((bool)options.get("show_placeholders", false));
		d->player->attach_window_manager(&d->hud->windows());
	}
	d->player->set_native_hook(d.get());
	// lane HUD-5: the drawable decorations' settings (the zoom follows the camera every frame)
	{
		IconUISettings is;
		is.showObjectHealth = showObjectHealth;
		is.allHealthBars = (bool)options.get("all_health_bars", false);
		is.veterancyFilter = d->haveVeterancyFilter ? &d->veterancyFilter : nullptr;
		is.images = &d->images;
		is.text = gameText;
		is.zoom = d->cam ? d->cam->getZoom() : 1.0f;
		d->hud->setIconUISettings(is);
	}
	d->ready = errors.is_empty();
	set_process(true);
	// the camera is set before the world's own _process (the streak ribbons face the camera transform of the same frame)
	set_process_priority(-100);
	set_process_input(true);
	result["ok"] = d->ready;
	result["errors"] = errors;
	return result;
}

Array InGameHudNode::get_selection() const
{
	waitLogic(d.get());
	Array out;
	if (d->hud)
	{
		for (std::uint32_t id : d->hud->input().ui().selected())
		{
			out.push_back((int64_t)id);
		}
	}
	return out;
}

Array InGameHudNode::get_logic_selection() const
{
	waitLogic(d.get());
	Array out;
	if (d->world && d->world->hud_game())
	{
		if (Player *p = d->world->hud_game()->players().getLocalPlayer())
		{
			for (std::uint32_t id : p->selection())
			{
				out.push_back((int64_t)id);
			}
		}
	}
	return out;
}

PackedStringArray InGameHudNode::get_message_log() const
{
	waitLogic(d.get());
	PackedStringArray out;
	if (d->hud)
	{
		for (const std::string &l : d->hud->input().messageLog())
		{
			out.push_back(toGodot(l));
		}
	}
	return out;
}

Array InGameHudNode::get_command_map() const
{
	Array out;
	for (const MetaMapRec &r : d->meta.records())
	{
		Dictionary rec;
		rec["name"] = toGodot(ClientMessageMetaName(r.meta));
		rec["key"] = toGodot(MetaMap::keyName(r.key));
		rec["transition"] = r.transition;
		rec["ctrl"] = (r.modState & MOD_CTRL) != 0;
		rec["shift"] = (r.modState & MOD_SHIFT) != 0;
		rec["alt"] = (r.modState & MOD_ALT) != 0;
		rec["game"] = (r.usableIn & COMMANDUSABLE_GAME) != 0;
		out.push_back(rec);
	}
	return out;
}

Dictionary InGameHudNode::get_state() const
{
	waitLogic(d.get());
	Dictionary s;
	if (!d->hud)
	{
		return s;
	}
	const InGameUI &ui = d->hud->input().ui();
	s["cursor"] = toGodot(ui.cursor());
	s["screenshot"] = d->lastScreenshot; // lane INPUT-1: the last TAKE_SCREENSHOT file
	s["spell_store_open"] = d->hud->spellStore() != nullptr; // lane INPUT-1: the SPELL_STORE key
	s["frame_selection_changed"] = (int64_t)ui.getFrameSelectionChanged();
	{
		// lane INPUT-1: the keyboard path, for a diagnosis (raw keys reaching the MetaEventTranslator, the meta messages they made, the modifier state)
		const MetaEventTranslator &mt = d->hud->input().metaTranslator();
		Dictionary keys;
		keys["raw"] = (int64_t)mt.rawKeys();
		keys["metas"] = (int64_t)mt.metasMade();
		keys["last_meta"] = toGodot(mt.lastMeta());
		keys["key_state"] = d->keyState;
		keys["key_events"] = (int64_t)d->keyEvents;
		keys["deferred"] = (int64_t)d->deferredInput;
		keys["map_records"] = (int64_t)d->meta.records().size();
		keys["german"] = mt.germanKeyboard();
		PackedStringArray recent;
		for (const std::string &m : mt.recentMetas())
		{
			recent.push_back(toGodot(m));
		}
		keys["recent"] = recent;
		s["keys"] = keys;
		// the command bar hotkeys the HotKeyTranslator holds (ControlBar::setControlCommand RW 0x71CF3E -> RW 0x71D139) and what its presses did
		Array hotkeys;
		for (const HotKeyTranslator::Entry &e : d->hud->input().hotKeyTranslator().entries())
		{
			Dictionary h;
			h["key"] = String::chr((char32_t)e.key);
			h["slot"] = e.slot;
			h["palantir"] = e.inPalantir;
			h["enabled"] = e.availability == HotKeyTranslator::Availability::Enabled;
			hotkeys.push_back(h);
		}
		s["hotkeys"] = hotkeys;
		Dictionary outcomes;
		for (const auto &kv : d->hud->input().hotKeyTranslator().outcomes())
		{
			outcomes[toGodot(kv.first)] = (int64_t)kv.second;
		}
		s["hotkey_outcomes"] = outcomes;
	}
	s["selecting"] = ui.isSelecting();
	s["gui_command"] = ui.getGUICommand() != nullptr;
	s["over_gui"] = d->hud->isOverGui((int)d->lastPointer.x, (int)d->lastPointer.y); // lane PLAY-1: the pointer is over the HUD movie / the radar
	s["pointer"] = d->lastPointer;
	Dictionary outcomes;
	for (const auto &kv : d->hud->input().commandTranslator().clickOutcomes())
	{
		outcomes[toGodot(kv.first)] = (int64_t)kv.second;
	}
	s["click_outcomes"] = outcomes;
	s["selected"] = get_selection();
	s["logic_selected"] = get_logic_selection();
	Array msgs;
	for (const std::string &m : ui.messages())
	{
		msgs.push_back(toGodot(m));
	}
	s["messages"] = msgs;
	{
		ControlBar &cb = d->hud->controlBar();
		Dictionary bar;
		bar["source"] = (int64_t)cb.sourceObject();
		bar["command_set"] = toGodot(cb.commandSetName());
		bar["money"] = (int64_t)cb.money();
		bar["command_points"] = toGodot(std::to_string(cb.commandPointsUsed()) + "/" + std::to_string(cb.commandPointsLimit()));
		Array pal, side, queue;
		for (const ControlBarButton &b : cb.palantirButtons())
		{
			pal.push_back(toGodot(std::to_string(b.slot) + " " + b.button->m_name + " state " + std::to_string((int)b.state) + " image " + b.image + " cost " + std::to_string(b.cost) + " queued " + std::to_string(b.queued)));
		}
		for (const ControlBarButton &b : cb.sideButtons())
		{
			side.push_back(toGodot(std::to_string(b.slot) + " " + b.button->m_name + " state " + std::to_string((int)b.state)));
		}
		for (const ControlBarQueueEntry &q : cb.queue())
		{
			queue.push_back(toGodot(q.templateName + " " + std::to_string((int)q.percent) + "%"));
		}
		bar["palantir_buttons"] = pal;
		bar["side_buttons"] = side;
		bar["queue"] = queue;
		s["control_bar"] = bar;
	}
	if (AptPalantir *p = d->hud->palantir())
	{
		Dictionary pal;
		pal["initialized"] = p->initialized();
		Array frames;
		for (const AptPalantir::ButtonFrame &f : p->buttonFrames())
		{
			frames.push_back(toGodot(f.index + " " + f.path));
		}
		pal["button_frames"] = frames;
		// lane PLAY-1: the movie's last commands (which of the Palantir's buttons reached the engine)
		Array cmds;
		const std::vector<std::string> &log = p->commandLog();
		for (size_t i = log.size() > 12 ? log.size() - 12 : 0; i < log.size(); ++i)
		{
			cmds.push_back(toGodot(log[i]));
		}
		pal["last_commands"] = cmds;
		s["palantir"] = pal;
	}
	return s;
}

// ---- lane SPELL-2: the spell book's state for harnesses and the targeting ring --------------------------------------------------------------
bool InGameHudNode::select_object(int64_t id)
{
	waitLogic(d.get());
	if (!d->hud)
	{
		return false;
	}
	UtilityFunctions::print("GAME TEST HOOK: select object ", id);
	InGameUI &ui = d->hud->input().ui();
	ui.deselectAll(true);
	ui.selectObject((::ObjectID)id);
	return ui.isSelected((::ObjectID)id);
}

Dictionary InGameHudNode::get_icon_ui() const
{
	Dictionary r;
	r["ops"] = 0;
	if (!d->hud)
	{
		return r;
	}
	int bars = 0;
	Array texts, images, notes;
	for (const IconUIOp &op : d->hud->iconOps())
	{
		bars += op.kind == IconUIOp::OPEN_RECT && op.color == 0x7F000000u ? 1 : 0; // the outer frame of a health bar
		if (op.kind == IconUIOp::TEXT)
		{
			texts.push_back(String::utf8(op.text.c_str()));
		}
		else if (op.kind == IconUIOp::IMAGE)
		{
			images.push_back(String(op.image.c_str()));
		}
	}
	for (const std::string &n : d->hud->iconUI().notes())
	{
		notes.push_back(String::utf8(n.c_str()));
	}
	r["ops"] = (int64_t)d->hud->iconOps().size();
	r["health_bars"] = bars;
	r["texts"] = texts;
	r["images"] = images;
	r["drawn"] = (int64_t)d->iconOpsDrawn;
	r["notes"] = notes;
	return r;
}

Dictionary InGameHudNode::get_spellbook_state()
{
	Dictionary r;
	r["ok"] = false;
	if (!d->hud || !d->hud->palantir())
	{
		return r;
	}
	const auto worldContext = d->hud->enterContext();
	AptPalantir *p = d->hud->palantir();
	r["ok"] = true;
	r["path"] = String(p->spellBookPath().c_str());
	r["shown"] = p->spellBookShown();
	Array slots;
	for (int i = 0; i < AptPalantir::kSpellSlots; ++i)
	{
		slots.push_back(String(AptPalantir::spellStateName(p->spellSlotState(i))));
	}
	r["slots"] = slots;
	const InGameSpellBookModel &bar = d->hud->spellBar();
	r["targeting"] = bar.targeting();
	r["radius"] = bar.targetButton() ? bar.targetButton()->radius : 0.0f;
	r["target_power"] = String(bar.targetButton() && bar.targetButton()->power ? bar.targetButton()->power->getName().c_str() : "");
	Dictionary store;
	AptSpellStore *st = d->hud->spellStore();
	store["open"] = st != nullptr;
	store["requests"] = (int64_t)d->hud->spellStoreRequests(); // lane PLAY-1
	store["error"] = toGodot(d->hud->spellStoreError());
	if (st)
	{
		store["initialized"] = st->initialized();
		store["layout"] = st->layout();
		store["points"] = st->model().points();
		store["set"] = String(st->model().commandSetName().c_str());
		Array states, sciences;
		for (int i = 0; i < SpellStoreModel::MAX_BUTTONS; ++i)
		{
			states.push_back(String(AptSpellStore::stateName(st->buttonState(i))));
		}
		for (const SpellStoreModel::Button &b : st->model().buttons())
		{
			sciences.push_back(String(b.scienceName.c_str()));
		}
		store["states"] = states;
		store["sciences"] = sciences;
		store["level"] = st->level();
	}
	r["store"] = store;
	return r;
}

bool InGameHudNode::open_spell_store()
{
	if (!d->hud)
	{
		return false;
	}
	const auto worldContext = d->hud->enterContext();
	return d->hud->openSpellStore();
}

void InGameHudNode::close_spell_store()
{
	if (d->hud)
	{
		const auto worldContext = d->hud->enterContext();
		d->hud->closeSpellStore();
	}
}

bool InGameHudNode::spell_store_click(int64_t index)
{
	if (!d->hud || !d->hud->spellStore())
	{
		return false;
	}
	const auto worldContext = d->hud->enterContext();
	return d->hud->spellStore()->click((int)index);
}

// what the movie's OnAptInGameSpellBookButtonPressed(slot) does (a harness without a mouse on the button)
bool InGameHudNode::press_spell_slot(int64_t slot)
{
	if (!d->hud || !d->hud->palantir())
	{
		return false;
	}
	const auto worldContext = d->hud->enterContext();
	return d->hud->palantir()->pressSpellSlot((int)slot);
}

Dictionary InGameHudNode::get_mapped_image(const String &name)
{
	Dictionary r;
	r["found"] = false;
	const ::Image *mapped = d->images.findImageByName(name.utf8().get_data());
	if (!mapped)
	{
		return r;
	}
	Ref<ImageTexture> tex = d->textureFor(mapped->filename);
	if (tex.is_null())
	{
		return r;
	}
	const float tw = (float)tex->get_width(), th = (float)tex->get_height();
	r["found"] = true;
	r["texture"] = tex;
	r["region"] = Rect2(mapped->uvLo[0] * tw, mapped->uvLo[1] * th, (mapped->uvHi[0] - mapped->uvLo[0]) * tw, (mapped->uvHi[1] - mapped->uvLo[1]) * th);
	return r;
}

Dictionary InGameHudNode::get_report() const
{
	waitLogic(d.get());
	Dictionary r;
	Array stops, errors, notes;
	if (d->hud)
	{
		for (const std::string &l : d->hud->stops())
		{
			stops.push_back(toGodot(l));
		}
		for (const std::string &e : d->hud->errors())
		{
			errors.push_back(toGodot(e));
		}
		for (const std::string &e : d->hud->windows().errors())
		{
			errors.push_back(toGodot(e));
		}
		if (d->hud->palantir())
		{
			for (const std::string &e : d->hud->palantir()->callErrors())
			{
				errors.push_back(toGodot("palantir call: " + e));
			}
			for (const auto &kv : d->hud->palantir()->images())
			{
				notes.push_back(toGodot("image " + kv.first + " = " + kv.second.image));
			}
		}
		for (const std::string &e : d->drawErrors)
		{
			errors.push_back(toGodot("draw: " + e));
		}
		for (const std::string &n : d->drawNotes)
		{
			stops.push_back(toGodot(n));
		}
		r["globe_ready"] = d->globe != nullptr; // lane HUD-2: the palantir model loaded into its viewport
		r["globe_draws"] = (int64_t)d->globeDraws;
		for (const WindowManagerNote &n : d->hud->windows().notes())
		{
			notes.push_back(toGodot(n.kind + " | " + n.detail));
		}
	}
	{
		Array ce;
		for (const std::string &e : d->cursorErrors)
		{
			ce.push_back(toGodot(e));
		}
		r["cursor_errors"] = ce;
		r["cursor"] = toGodot(d->cursorName);
	}
	r["draw_counts"] = toGodot("images " + std::to_string(d->drawnImages) + " skipped " + std::to_string(d->skippedImages));
	if (d->hud)
	{
		// lane RADAR-1: the radar's events and the Palantir's ping calls (Radar.h)
		int events = 0;
		for (int i = 0; i < Radar::kMaxEvents; ++i)
		{
			events += d->hud->radar().radarEvent(i).type != Radar::RADAR_EVENT_INVALID;
		}
		const unsigned calls = d->hud->palantir() ? d->hud->palantir()->radarPingCalls() : 0u;
		r["radar"] = toGodot("events " + std::to_string(events) + " pings " + std::to_string(d->hud->radar().pings().size()) + " ping calls " + std::to_string(calls) + " attack checks " +
		                     std::to_string(d->hud->radar().attackStats()[0]) + " hit objects " + std::to_string(d->hud->radar().attackStats()[1]) + " ours " +
		                     std::to_string(d->hud->radar().attackStats()[2]));
		if (d->hud->palantir())
		{
			for (const std::string &e : d->hud->palantir()->callErrors())
			{
				errors.push_back(toGodot("palantir call: " + e));
			}
		}
	}
	r["stops"] = stops;
	r["errors"] = errors;
	r["notes"] = notes;
	if (d->hud)
	{
		// lane QA-1: presses of buttons whose command is not ported (ControlBar::pressButton counts them): a button the player clicks and nothing happens
		Dictionary unported;
		for (const auto &kv : d->hud->controlBar().unportedPresses())
		{
			unported[toGodot(kv.first)] = (int64_t)kv.second;
		}
		r["unported_presses"] = unported;
	}
	if (d->player)
	{
		r["player"] = d->player->get_report();
	}
	return r;
}

void InGameHudNode::_process(double delta)
{
	if (!d->ready)
	{
		return;
	}
	Viewport *vp = get_viewport();
	const Vector2 window = vp ? vp->get_visible_rect().get_size() : Vector2(1024, 768);
	d->hud->setWindowSize((int)window.x, (int)window.y);
	if (!d->pendingInput.empty() && d->world && d->world->hud_game() && d->world->hud_game()->logicIdle())
	{
		std::vector<Ref<InputEvent>> events;
		events.swap(d->pendingInput);
		for (const Ref<InputEvent> &e : events)
		{
			handleInput(e); // the input that waited for the worker, in arrival order
		}
	}
	// lane INPUT-1: TAKE_SCREENSHOT (F12). RotWK's W3DDisplay::takeScreenShot (RW 0x4474A0) writes sshot%.4d.bmp into the user data folder, the first number
	// whose file does not exist (a counter from 0 per run); here the same name as PNG (a presentation format choice) in the user data folder
	while (d->screenshotsDone < d->hud->input().ui().screenshotRequests() && vp)
	{
		++d->screenshotsDone;
		const bool headless = DisplayServer::get_singleton() && DisplayServer::get_singleton()->get_name() == String("headless");
		Ref<Image> image = (!headless && vp->get_texture().is_valid()) ? vp->get_texture()->get_image() : Ref<Image>(); // a headless run draws nothing
		const String dir = OS::get_singleton()->get_user_data_dir();
		String path;
		do
		{
			path = dir.path_join(vformat("sshot%04d.png", d->screenshotNumber++));
		} while (FileAccess::file_exists(path));
		const Error err = image.is_valid() ? image->save_png(path) : ERR_UNAVAILABLE;
		d->lastScreenshot = err == OK ? path : String("failed: ") + path;
		UtilityFunctions::print("HUD screenshot ", d->lastScreenshot);
	}
	const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	d->hud->update(delta);
	d->drawIconOps(get_canvas_item()); // lane HUD-5
	const uint64_t t1 = Time::get_singleton()->get_ticks_usec();
	// the camera runs 30 client frames a second (retail FramesPerSecondLimit 30, S-456) and is drawn interpolated between two of them
	if (LookAtTranslator *la = d->hud->input().lookAt())
	{
		DisplayServer *ds = DisplayServer::get_singleton();
		const DisplayServer::WindowMode wm = ds ? ds->window_get_mode() : DisplayServer::WINDOW_MODE_WINDOWED;
		la->setEdgeScrollEnabled(d->edgeScroll || wm == DisplayServer::WINDOW_MODE_FULLSCREEN || wm == DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN);
	}
	const double step = 1.0 / 30.0;
	d->clientClock += delta;
	int guard = 0;
	while (d->clientClock >= step && guard < 8)
	{
		d->hud->input().cameraFrame((unsigned)Time::get_singleton()->get_ticks_msec(), d->world && d->world->is_paused());
		d->clientClock -= step;
		++guard;
	}
	if (guard >= 8)
	{
		d->clientClock = 0.0; // a stall: no catching up
	}
	d->applyCamera(window, d->clientClock / step);
	const uint64_t t2 = Time::get_singleton()->get_ticks_usec();
	d->updateCursor(delta, d->hud->isOverGui((int)d->lastPointer.x, (int)d->lastPointer.y) ? std::string(MouseCursorName::Arrow) : d->hud->input().ui().cursor());
	const uint64_t t3 = Time::get_singleton()->get_ticks_usec();
	d->updateMs = (double)(t1 - t0) / 1000.0;
	d->cameraMs = (double)(t2 - t1) / 1000.0;
	d->cursorMs = (double)(t3 - t2) / 1000.0;
}

Dictionary InGameHudNode::get_frame_timings() const
{
	Dictionary t;
	t["hud_update_ms"] = d->updateMs;
	t["hud_camera_ms"] = d->cameraMs;
	t["hud_cursor_ms"] = d->cursorMs;
	return t;
}

Dictionary InGameHudNode::get_move_hints() const
{
	// client state the HUD changes on the main thread only (HudInput::update, cameraFrame): no wait for the logic worker (game.gd asks every render frame)
	Dictionary out;
	Array hints;
	out["model"] = String();
	out["hints"] = hints;
	if (!d->hud || !d->cam)
	{
		return out;
	}
	const InGameUI &ui = d->hud->input().ui();
	out["model"] = toGodot(d->cam->gameData().moveHintName);
	out["frame"] = (int64_t)ui.clientFrame();
	out["made"] = (int64_t)ui.moveHintsMade();
	for (int i = 0; i < InGameUI::MAX_MOVE_HINTS; ++i)
	{
		const InGameUI::MoveHint &h = ui.moveHints()[i];
		if (h.frame == 0 || ui.clientFrame() - h.frame > InGameUI::MOVE_HINT_FRAMES)
		{
			continue;
		}
		Dictionary e;
		e["slot"] = i;
		e["frame"] = (int64_t)h.frame;
		e["age"] = (int64_t)(ui.clientFrame() - h.frame);
		e["position"] = toGodotAxes(h.pos);
		e["x"] = h.pos.x;
		e["y"] = h.pos.y;
		e["z"] = h.pos.z;
		hints.push_back(e);
	}
	return out;
}

Dictionary InGameHudNode::get_placement() const
{
	waitLogic(d.get());
	Dictionary p;
	p["placing"] = false;
	if (!d->hud)
	{
		return p;
	}
	const InGameUI &ui = d->hud->input().ui();
	p["placing"] = ui.isPlacing();
	p["template"] = toGodot(ui.placeBuildTemplate());
	p["source"] = (int64_t)ui.placeBuildSource();
	p["has_ghost"] = ui.placeHasGhost();
	p["x"] = ui.placeLocation().x;
	p["y"] = ui.placeLocation().y;
	p["z"] = ui.placeLocation().z;
	p["angle"] = ui.placeAngle();
	p["legal"] = ui.placeLegalCode();
	return p;
}

bool InGameHudNode::press_command_button(const String &template_name)
{
	waitLogic(d.get());
	if (!d->ready)
	{
		return false;
	}
	const auto worldContext = d->hud->enterContext(); // the command store of this game's world
	ControlBar &cb = d->hud->controlBar();
	const std::string want = template_name.utf8().get_data();
	auto find = [&]() -> const ControlBarButton * {
		for (const std::vector<ControlBarButton> *list : { &cb.palantirButtons(), &cb.sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button && b.button->getThingTemplate() && b.button->getThingTemplate()->getName() == want)
				{
					return &b;
				}
			}
		}
		return nullptr;
	};
	for (int tries = 0; tries < 6; ++tries)
	{
		if (const ControlBarButton *b = find())
		{
			return cb.pressButton(b->slot, b->inPalantir);
		}
		const ControlBarButton *push = nullptr;
		for (const std::vector<ControlBarButton> *list : { &cb.palantirButtons(), &cb.sideButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (!push && b.button && b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
				{
					push = &b;
				}
			}
		}
		if (!push)
		{
			return false;
		}
		cb.pressButton(push->slot, push->inPalantir);
		cb.update();
	}
	return false;
}

Array InGameHudNode::get_command_buttons() const
{
	waitLogic(d.get());
	Array out;
	if (!d->ready)
	{
		return out;
	}
	ControlBar &cb = d->hud->controlBar();
	for (const bool arc : { true, false })
	{
		const std::vector<ControlBarButton> &list = arc ? cb.palantirButtons() : cb.sideButtons();
		for (size_t i = 0; i < list.size(); ++i)
		{
			const ControlBarButton &b = list[i];
			if (!b.button)
			{
				continue;
			}
			Dictionary e;
			e["slot"] = b.slot;
			e["in_palantir"] = b.inPalantir;
			e["position"] = (int64_t)(b.position >= 0 ? b.position : (int)i); // lane HUD-4: the arc window / the side bar position
			e["name"] = toGodot(b.button->m_name);
			e["command"] = String(GUICommandName(b.button->m_command));
			e["template"] = b.button->getThingTemplate() ? toGodot(b.button->getThingTemplate()->getName()) : String();
			e["upgrade"] = toGodot(b.button->m_upgradeName);
			e["power"] = toGodot(b.button->m_specialPowerName);
			e["state"] = (int64_t)b.state;
			e["cost"] = b.cost;
			e["queued"] = b.queued;
			// AptPalantir::syncFrames: the arc's position i is the clip CommandButtons.<i>, the side bar's SideCommandBar.ButtonSet.Button<i>.Button
			const int64_t pos = b.position >= 0 ? b.position : (int64_t)i;
			e["frame"] = arc ? String("CommandButtons.") + String::num_int64(pos) : String("SideCommandBar.ButtonSet.Button") + String::num_int64(pos) + String(".Button");
			out.push_back(e);
		}
	}
	return out;
}

void InGameHudNode::inject_mouse_move(const Vector2 &p)
{
	waitLogic(d.get());
	if (d->ready)
	{
		d->lastPointer = p;
		d->hud->mouseMove((int)p.x, (int)p.y, d->keyState);
	}
}

void InGameHudNode::inject_mouse_button(int button, bool down, const Vector2 &p, bool double_click)
{
	waitLogic(d.get());
	if (!d->ready)
	{
		return;
	}
	// 1 left, 2 right, 3 middle (Godot's MouseButton)
	const HudInput::Button b = button == 1 ? HudInput::Button::Left : button == 2 ? HudInput::Button::Right : HudInput::Button::Middle;
	d->hud->mouseButton(b, down, (int)p.x, (int)p.y, d->keyState, (int)Time::get_singleton()->get_ticks_msec(), double_click);
}

void InGameHudNode::inject_mouse_wheel(int notches, const Vector2 &p)
{
	waitLogic(d.get());
	if (d->ready)
	{
		d->hud->mouseWheel(notches, (int)p.x, (int)p.y);
	}
}

void InGameHudNode::inject_key(int dik, bool down)
{
	keyEvent(dik, down, 0);
}

void InGameHudNode::keyEvent(int dik, bool down, char32_t character)
{
	waitLogic(d.get());
	if (!d->ready)
	{
		return;
	}
	d->mods.key(dik, down); // lane INPUT-1 r2: a bit per side (KEY_STATE_LCONTROL / _RCONTROL ...)
	d->keyState = d->mods.state();
	d->hud->key(dik, (down ? KEY_STATE_DOWN : KEY_STATE_UP) | d->keyState, character);
}

void InGameHudNode::syncModifiers(const InputEventWithModifiers *e)
{
	// lane INPUT-1: the modifier state follows the event's own flags (keys, buttons and pointer motion). Tracked from the modifier keys' presses alone, a
	// release the window never saw (Alt+Tab, a modifier let go over another window) left Ctrl / Shift / Alt held for good: every digit then made or added
	// a control group instead of selecting it. The sides are tracked apart (review r1: letting go of left Ctrl with right Ctrl held keeps Ctrl down);
	// the HUD sees each correction as the side key's transition, the change retail's MetaEventTranslator works on (KEY_NONE records)
	for (const ModifierTracker::Transition &t : d->mods.sync(e->is_ctrl_pressed(), e->is_shift_pressed(), e->is_alt_pressed()))
	{
		keyEvent(t.key, t.down, 0);
	}
}

void InGameHudNode::_input(const Ref<InputEvent> &event)
{
	if (!d->ready || event.is_null())
	{
		return;
	}
	// SMOOTH-1 (S-810): the HUD's input reads the live game (picking, selection, the control bar). While the logic worker runs a frame the event waits,
	// in order, for the next render frame the worker is idle in (InGameHudNode::_process replays it), instead of blocking the render on the frame
	LiveGame *game = d->world ? d->world->hud_game() : nullptr;
	if (game && (!game->logicIdle() || !d->pendingInput.empty()))
	{
		d->pendingInput.push_back(event);
		++d->deferredInput;
		return;
	}
	handleInput(event);
}

void InGameHudNode::handleInput(const Ref<InputEvent> &event)
{
	if (LookAtTranslator *la = d->hud->input().lookAt())
	{
		la->setTime((unsigned)Time::get_singleton()->get_ticks_msec());
	}
	if (const InputEventMouseMotion *mm = Object::cast_to<InputEventMouseMotion>(event.ptr()))
	{
		syncModifiers(mm); // lane INPUT-1 r2: the pointer's events carry the modifier flags too
		inject_mouse_move(mm->get_position());
	}
	else if (const InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(event.ptr()))
	{
		syncModifiers(mb);
		const int idx = (int)mb->get_button_index();
		if (idx == 1 || idx == 2 || idx == 3)
		{
			inject_mouse_button(idx, mb->is_pressed(), mb->get_position(), mb->is_double_click());
		}
		else if ((idx == 4 || idx == 5) && mb->is_pressed())
		{
			d->hud->mouseWheel(idx == 4 ? 1 : -1, (int)mb->get_position().x, (int)mb->get_position().y);
		}
	}
	else if (const InputEventKey *k = Object::cast_to<InputEventKey>(event.ptr()))
	{
		++d->keyEvents;
		// lane INPUT-1: RotWK reads DirectInput scan codes, the key's position (the CommandMap's KEY_1 is the key left of 2 on any layout): the
		// physical key decides, the layout's keycode only when the event has none (a synthetic event)
		const Key code = k->get_physical_keycode() != Key::KEY_NONE ? k->get_physical_keycode() : k->get_keycode();
		int dik = dikOf(code);
		if (k->get_location() == KeyLocation::KEY_LOCATION_RIGHT)
		{
			// lane INPUT-1 r2: the right modifier keys have their own scan codes (DIK_RCONTROL 0x9D, DIK_RSHIFT 0x36, DIK_RMENU 0xB8)
			dik = dik == ::KEY_LCTRL ? ::KEY_RCTRL : dik == ::KEY_LSHIFT ? ::KEY_RSHIFT : dik == ::KEY_LALT ? ::KEY_RALT : dik;
		}
		if (!ModifierTracker::isModifierKey(dik))
		{
			syncModifiers(k);
		}
		if (dik != 0 && !k->is_echo())
		{
			if (dik == ::KEY_Z && k->is_pressed() && (d->keyState & KEY_STATE_CONTROL) && !(d->keyState & ~KEY_STATE_CONTROL) && !d->meta.isBound(::KEY_Z, MOD_CTRL) && d->cam)
			{
				// lane INPUT-1: Ctrl+Z toggles the free camera (OpenBFME's presentation option, not retail; the owner's request). No RotWK CommandMap
				// record takes Ctrl+Z (Z alone is TOGGLE_PLANNING_MODE); a CommandMap that binds it (a mod's) keeps it and the toggle is off
				d->cam->setFreeCamera(!d->cam->freeCamera());
				++d->freeCameraToggles;
			}
			// lane INPUT-1: the layout's character of the key (its unshifted label) for the command button hotkeys (RotWK RW 0x63F14D translates the scan
			// code with the keyboard layout); none for the special keys
			const int64_t label = (int64_t)k->get_key_label();
			const char32_t character = (label > 0x20 && label < 0x10000) ? (char32_t)label : 0;
			keyEvent(dik, k->is_pressed(), character);
		}
		else if (dik != 0 && k->is_echo())
		{
			d->hud->key(dik, KEY_STATE_DOWN | KEY_STATE_AUTOREPEAT | d->keyState);
		}
	}
}

void InGameHudNode::camera_look_at(const Vector2 &sage_xy)
{
	// lane PERF-3: no wait for the logic worker: the camera is client state whose terrain heights are the immutable height field, as its per-frame update
	// and get_camera read it (the wait held a script that moves the camera every render frame, e.g. a benchmark's sweep, until each logic frame finished)
	if (d->cam)
	{
		d->cam->lookAt(Coord3D{ sage_xy.x, sage_xy.y, 0.0f });
		d->cam->snapInterpolation(); // lane MOVE-2 r2: a script's pose is drawn at once (no 30 Hz steps when it is set every render frame)
	}
}

void InGameHudNode::camera_set_height(float height_above_ground)
{
	// lane PERF-3: client state, no wait for the logic worker (camera_look_at)
	if (d->cam)
	{
		d->cam->setHeightAboveGround(height_above_ground);
		d->cam->snapInterpolation(); // lane MOVE-2 r2
	}
}

void InGameHudNode::set_edge_scroll(bool on)
{
	d->edgeScroll = on;
}

void InGameHudNode::set_free_camera(bool on)
{
	if (d->cam)
	{
		d->cam->setFreeCamera(on);
	}
}

Dictionary InGameHudNode::get_camera() const
{
	// the camera is client state (its terrain heights are the immutable terrain): no wait for the logic worker (game.gd's listener asks every frame)
	Dictionary out;
	if (!d->cam)
	{
		return out;
	}
	const TacticalCamera &c = *d->cam;
	out["position"] = Vector2(c.position().x, c.position().y);
	out["angle"] = c.getAngle();
	out["pitch"] = c.getPitch();
	out["zoom"] = c.getZoom();
	out["height_above_ground"] = c.getHeightAboveGround();
	out["ground_level"] = c.getGroundLevel();
	out["terrain_height"] = c.terrainHeightUnderCamera();
	out["min_height"] = c.minHeight();
	out["max_height"] = c.maxHeight();
	out["free_camera"] = c.freeCamera(); // lane PLAY-1
	out["free_camera_toggles"] = d->freeCameraToggles; // lane INPUT-1: Ctrl+Z
	out["zoom_out_limit"] = c.zoomOutLimit();
	out["fog_shift"] = c.fogShift();
	out["far"] = c.farPlane();
	out["eye"] = toGodotAxes(c.eye());
	out["target"] = toGodotAxes(c.target());
	out["offset"] = Vector3(c.cameraOffset().x, c.cameraOffset().y, c.cameraOffset().z);
	out["constraint_valid"] = c.constraintValid();
	float lim[4];
	c.constraint(lim);
	out["constraint"] = Vector4(lim[0], lim[1], lim[2], lim[3]);
	out["frame"] = (int64_t)c.frame();
	if (const LookAtTranslator *la = d->hud ? d->hud->input().lookAt() : nullptr)
	{
		out["scrolling"] = la->isScrolling();
		out["scroll_type"] = la->scrollType();
		out["rotating"] = la->isRotating();
	}
	Array overridden;
	for (const std::string &k : c.mapValues().overridden)
	{
		overridden.push_back(toGodot(k));
	}
	out["map_overrides"] = overridden;
	out["height_field_ready"] = c.heightField().ready();
	return out;
}

Vector2 InGameHudNode::pixel_to_world(const Vector2 &pixel) const
{
	waitLogic(d.get());
	Coord3D out;
	if (d->cam && d->world && d->world->hud_game() && d->shown.screenToTerrain({ (int)pixel.x, (int)pixel.y }, d->world->hud_game()->logic(), out))
	{
		return Vector2(out.x, out.y);
	}
	return Vector2(-1e9f, -1e9f);
}

Vector2 InGameHudNode::world_to_pixel(const Vector2 &w) const
{
	waitLogic(d.get());
	ICoord2D p;
	if (d->cam && d->world && d->world->hud_game() && d->shown.worldToScreen({ w.x, w.y, d->world->hud_game()->logic().getGroundHeight(w.x, w.y) }, p))
	{
		return Vector2(p.x, p.y);
	}
	return Vector2(-1e9f, -1e9f);
}

// lane IDLE-1: what the pointer meets at a pixel, for the QA harness's verdict on a failed click (qa_player.gd _select). The HUD's own pick
// (HudObjects::pickObject, the nearest drawn model on the ray) and the same ray against object `id` alone: a click whose ray meets the object's model while the
// pick answers another object met that one nearer (the object is drawn behind it); a ray that misses the object's model aimed beside it.
// Client state only: nothing reaches the simulation.
Dictionary InGameHudNode::pick_probe(const Vector2 &pixel, int64_t id) const
{
	waitLogic(d.get());
	Dictionary r;
	r["ok"] = false;
	if (!d->hud || !d->world || !d->world->hud_game())
	{
		return r;
	}
	const HudContext &ctx = d->hud->input().context();
	const ICoord2D px{ (int)pixel.x, (int)pixel.y };
	Coord3D origin, dir;
	if (!ctx.view.screenToRay(px, origin, dir))
	{
		return r;
	}
	r["ok"] = true;
	const ::Object *picked = HudObjects::pickForSelection(ctx, px); // lane HUD-5: what a selection click takes (RW 0x485CB8)
	r["picked"] = picked ? (int64_t)picked->getID() : (int64_t)0;
	const ::Object *target = d->world->hud_game()->logic().findObjectByID((::ObjectID)id);
	String verdict = "gone";
	float t = 0.0f;
	if (target)
	{
		const DrawablePick::Result res = ctx.pickRay ? ctx.pickRay(*target, origin, dir, &t) : DrawablePick::Result::Unknown;
		verdict = res == DrawablePick::Result::Hit ? "hit" : res == DrawablePick::Result::Miss ? "miss" : res == DrawablePick::Result::NotDrawn ? "not_drawn" : "unknown";
		r["selectable"] = HudObjects::isSelectable(*target);
	}
	r["target"] = verdict;
	r["target_t"] = t;
	// lane HUD-5: every hit of the pick's cast, near to far: "<id> <template> t=<t> type=0x<collision type>"
	Array hits;
	for (const HudObjects::PickHit &h : HudObjects::pickHits(ctx, px, HudObjects::pickTypesForContext(ctx)))
	{
		char b[64];
		std::snprintf(b, sizeof(b), " t=%.1f type=0x%X", h.t, h.type);
		hits.push_back(String::num_int64((int64_t)h.obj->getID()) + " " + String(h.obj->getTemplate()->getName().c_str()) + String(b));
	}
	r["hits"] = hits;
	return r;
}

Dictionary InGameHudNode::invoke_at(const String &path, const String &function, const PackedStringArray &args)
{
	waitLogic(d.get());
	Dictionary r;
	if (!d->hud || !d->hud->palantir())
	{
		r["ok"] = false;
		r["error"] = "no HUD";
		return r;
	}
	std::vector<std::string> a;
	for (int64_t i = 0; i < args.size(); ++i)
	{
		a.push_back(args[i].utf8().get_data());
	}
	std::string result, error;
	const bool ok = d->hud->windows().invokeASAt(d->hud->palantir()->level(), path.utf8().get_data(), function.utf8().get_data(), a, &result, &error);
	r["ok"] = ok;
	r["result"] = toGodot(result);
	r["error"] = toGodot(error);
	return r;
}

Vector2 InGameHudNode::world_to_radar_pixel(const Vector2 &world) const
{
	// lane PLAY-1: the window pixel of the radar that shows SAGE (x, y) (the inverse of InGameHud::radarClick's mapping, size 128); (-1, -1) without a radar
	waitLogic(d.get());
	float sq[4];
	if (!d->hud || !d->hud->radarSquare(sq) || !d->hud->radar().ready())
	{
		return Vector2(-1, -1);
	}
	float rx = 0.0f, ry = 0.0f;
	d->hud->radar().worldToRadar(world.x, world.y, 128, rx, ry);
	return Vector2(sq[0] + rx * (sq[2] - sq[0]) / 128.0f, sq[1] + ry * (sq[3] - sq[1]) / 128.0f);
}

bool InGameHudNode::create_radar_event(const Vector2 &world_xy, int type)
{
	// lane RADAR-1 (QA): an event the way a script's radar event action makes one (createEvent, 4 s); false without a radar
	if (!d->hud || !d->hud->radar().ready())
	{
		return false;
	}
	d->hud->radar().createEvent(Coord3D{ (float)world_xy.x, (float)world_xy.y, 0.0f }, type, 4.0f);
	return true;
}

Rect2 InGameHudNode::get_radar_square() const
{
	waitLogic(d.get());
	float sq[4];
	if (d->hud && d->hud->radarSquare(sq))
	{
		return Rect2(sq[0], sq[1], sq[2] - sq[0], sq[3] - sq[1]);
	}
	return Rect2();
}

String InGameHudNode::dump_tree(int max_depth) const
{
	if (!d->player || !d->hud || !d->hud->palantir())
	{
		return String();
	}
	return d->player->dump_tree(d->hud->palantir()->level(), max_depth);
}

Dictionary InGameHudNode::find_button_window(const String &path) const
{
	waitLogic(d.get());
	Dictionary r;
	r["found"] = false;
	if (!d->player)
	{
		return r;
	}
	Dictionary b = d->player->find_button(d->hud && d->hud->palantir() ? d->hud->palantir()->level() : 1, path);
	if (!(bool)b.get("found", false))
	{
		return r;
	}
	const Vector2 w = d->player->stage_to_window(Vector2((float)(double)b.get("x", 0.0), (float)(double)b.get("y", 0.0)));
	r["found"] = true;
	r["x"] = (double)w.x;
	r["y"] = (double)w.y;
	return r;
}

} // namespace godot
