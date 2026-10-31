// OpenBFME. GPL-3.0.
// See GodotInGameHud.h.

#include "Common/BfmeGame.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioLog.h"
#include "GodotDevice/GodotInGameHud.h"
#include "GameClient/GUI/AptScreens/AptImageMode.h"
#include "GodotDevice/GodotPackedTexture.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "Common/SpecialPower.h"
#include "GameClient/GameText.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/PlacementTextStyle.h"
#include "GameClient/GlobalLanguage.h"
#include "GameLogic/BeaconCommands.h"
#include "GameClient/GameTextTableSource.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/ShellServices.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "GameClient/Radar.h"
#include "GameClient/CursorFile.h"
#include "GodotDevice/GodotPlacementGhost.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameHud.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/LiveGame.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalCamera.h"
#include "GameClient/CameraAnimation.h"
#include "GameClient/LiveFX.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GodotDevice/GodotAptPlayer.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotHud6Draw.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GodotDevice/GodotMouse.h"

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
#include <godot_cpp/classes/input_event_mouse.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/decal.hpp>
#include <godot_cpp/classes/node3d.hpp>
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

} // namespace

struct HudDevice : public AptNativeHook
{
	std::map<int, Ref<ShaderMaterial>> imageModes; ///< community PR #37: the canvas materials of the Apt image modes (AptImageMode)
	Ref<RetailFileSystem> fs;
	GameWorld *world = nullptr;
	Camera3D *camera = nullptr;
	AptMenuPlayer *player = nullptr;
	int64_t menuKeyEvents = 0; // lane OPTS-6: the keys a menu over the game took
	std::unique_ptr<INIEnvironment> metaEnv;
	MetaMap meta;
	MouseSettings mouse;
	std::shared_ptr<GodotMouse> godotMouse;
	RecordingShellServices services;
	// lane CAM-1: the retail tactical camera; the Godot camera is set from it every frame (applyCamera)
	std::unique_ptr<TacticalCamera> cam;
	DisplayedCameraView shown; // picking and projection follow the displayed (interpolated) pose (TacticalCamera.h, lane BUILD-5 r3)
	// lane CAMANIM-1: the view's camera animation (W3DView mode 4) over the map's CameraAnimationList (built at the first request; the map is immutable)
	CameraAnimationList camAnimList;
	bool camAnimListBuilt = false;
	CameraAnimationPlayback camAnim;
	double clientClock = 0.0;
	double updateMs = 0.0, cameraMs = 0.0, cursorMs = 0.0; ///< SMOOTH-1: the parts of the last _process
	double viewClock = 0.0; ///< lane ANTICHEAT-1: seconds since the last view sample (GameWorld::net_submit_view, 2 a second)
	double hudMs = 0.0, iconsMs = 0.0, decalsMs = 0.0, radialMs = 0.0; ///< SELPERF-1: updateMs's parts (InGameHud::update, the icon ops, the selection decals, HUD-6)
	double placeMs = 0.0, ghostMs = 0.0; ///< lane FOGFX-1: the placement update and the ghost sync of the last frame
	bool edgeScroll = false;
	int edgeScrollMode = 0;   // lane OPTS-2: 0 retail (full screen only, or edge_scroll), 1 always, 2 never
	float uiScale = 1.0f;     // lane OPTS-2: the decorations' scale (health bars, veterancy marks, texts) about each object's ops
	float fogShift = 0.0f; ///< lane PLAY-1: the last w3d_fog_shift sent
	// lane SPELL-2 (review r2): the game text of a standalone HUD (data/lotr.str); an attached HUD uses its shell's. Before the HUD: it outlives it
	std::unique_ptr<GameTextSource> ownedText;
	std::unique_ptr<InGameHud> hud;
	std::unique_ptr<Hud6Draw> hud6; ///< lane HUD-6: the radial bubbles and the help box's content (destroyed before the HUD)
	int keyState = 0;
	ModifierTracker mods; // lane INPUT-1 r2: Ctrl / Shift / Alt per side
	int freeCameraToggles = 0; // lane INPUT-1: Ctrl+Z presses that toggled the free camera
	unsigned screenshotsDone = 0; // lane INPUT-1: TAKE_SCREENSHOT requests saved
	unsigned keyEvents = 0;       // lane INPUT-1: InputEventKey events handleInput saw
	int screenshotNumber = 0;
	String lastScreenshot;
	bool ready = false;
	bool modal = false;      ///< lane OPTS-6: a menu is the input's top over the game (InGameHud::setModal)
	unsigned modalEntries = 0; ///< lane OPTS-6: the menus that took the input over the game (get_state keys.modal_entries)
	unsigned heldButtons = 0; ///< lane OPTS-6: the mouse buttons the world saw go down (bit 1 left, 2 right, 3 middle), released when a menu opens
	bool ownsPlayer = false;
	std::uint64_t playerId = 0;
	Vector2 lastPointer;
	bool livePointerSeen = false;
	Vector2 livePointer; ///< lane BUILD-5 r3: the pointer as the OS reports it, also while the HUD's input waits for the logic worker (the ghost's pose: RotWK Mouse + 0x4F0C)
	int localPlayerIndex = -1;                   ///< SMOOTH-1: the local player's index (the radar's own blips, from the snapshot)
	std::vector<Ref<InputEvent>> pendingInput;   ///< SMOOTH-1: input that arrived while the logic worker ran a frame, replayed in order when it is idle
	unsigned long long deferredInput = 0;
	unsigned long long droppedInput = 0, focusResets = 0; ///< lane BACKLOG3: queued presses dropped at a focus loss, the focus losses
	bool holdInput = false;                               ///< lane BACKLOG3 (QA): hold_input

	std::unique_ptr<AptArchiveFileSource> source;
	std::unique_ptr<AptTextureStore> textures;
	MappedImageCollection images;
	std::unique_ptr<INIEnvironment> imageEnv;
	std::map<std::string, Ref<Image>> godotImages;
	std::map<std::string, Ref<ImageTexture>> godotTextures;
	std::map<std::string, Ref<ImageTexture>> grayTextures; ///< lane OWNERFIX-3: the greyscale copies of textures a disabled button draws
	Ref<ImageTexture> radarTexture;
	Ref<ImageTexture> radarShroud;                  ///< HUD-2: ScrollShroud with the shroud's alpha
	bool radarShroudTried = false;
	unsigned long long radarShroudVersion = ~0ull; ///< VIS-1: the shroud edges the picture was made with
	std::shared_ptr<const ShroudView> radarShroudView;
	std::uint64_t radarVersion = 0;
	int radarSize = 128;
	Ref<ImageTexture> radarOverlay;                ///< lane RADAR-1: W3DRadar's object overlay (+ 0x147C)
	std::vector<std::uint32_t> radarOverlayTexels;
	unsigned radarOverlayFrame = ~0u;
	Rect2 radarPicture;                            ///< lane RADAR-1: the picture rectangle RenderRadar drew (the view box's frame)
	Rect2 radarClip;                               ///< lane HUD-7: the RenderRadar clip rectangle of the last draw
	float radarViewBox[8] = {};                    ///< lane HUD-7: the view box corners RenderRadarViewBox drew last (window pixels)
	unsigned long long radarDraws = 0, radarViewBoxDraws = 0; ///< lane HUD-7
	std::vector<std::string> drawErrors;
	std::set<std::string> drawNotes; ///< lane HUD-4: the device's inference notes (S-1482: a .jpg + .png texture)
	unsigned drawnImages = 0, drawnTimers = 0, skippedImages = 0;
	std::string portraitDrawn; // lane UI-4: the mapped image the CommandUI.Portrait clip drew last ("" none)
	unsigned portraitDraws = 0;
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
	std::unique_ptr<PlacementGhostRenderer> ghost; ///< lane BUILD-5 r2: the placement ghost, drawn after the displayed camera (GodotPlacementGhost.h)

	void drawPlaceholder(const AptCanvasOp &op, RID item) override;
	Ref<Image> imageFor(const std::string &file);
	Ref<ImageTexture> textureFor(const std::string &file);
	Ref<ImageTexture> grayTextureFor(const std::string &file); // lane OWNERFIX-3
	// lane HUD-5: the drawable decorations (InGameHud::iconOps) drawn under the Palantir each frame
	RID iconItem;
	ObjectFilter veterancyFilter;
	bool haveVeterancyFilter = false;
	unsigned iconOpsDrawn = 0;
	bool iconsCovered = false; ///< lane SAVE-UI1: set_icons_covered
	// lane PLAY-3: the drag selection box drawn in the last frame, the box, the frames it was drawn in
	bool dragBoxShown = false;
	IconUIOp dragBox;
	unsigned long long dragBoxFrames = 0;
	void drawIconOps(RID parent);
	// lane UI-4: the selection markers (GameClient/SelectionDecals.h) as Godot Decal nodes under a Node3D beside the camera, one per decal, reused
	uint64_t decalRootId = 0;
	std::vector<uint64_t> decalIds;
	unsigned decalsDrawn = 0;
	// lane SELPERF-1: the SHADOW_MERGE_DECAL decals drawn by the terrain shader (godot/shaders/selection_decals.gdshaderinc) from the grid of this frame
	// (BuildMergeDecalGrid) in the global sel_data and their textures in the global sel_atlas; the atlas grows when a texture is new to it
	MergeDecalGrid selGrid;
	Ref<ImageTexture> selData, selAtlas;
	unsigned long long selVersion = ~0ull; // the markers' version the grid was built from (an unchanged frame builds and uploads nothing)
	MergeDecalAtlas selAtlasPack;
	int selMode = -1;                       // the mode last given to the shader
	Vector4 selGridSet, selGrid2Set;        // the grid parameters last given to it
	int selAlphaSet = -1;
	int64_t selUploads = 0;
	bool selAtlasFor(const std::vector<SelectionDecal> &list);
	void composeMergeReport(); // the CPU composition (ComposeMergeDecals) of the state report
	// a decal texture in a colour: the texel colour times the colour, the texel alpha (the colour's alpha is the decal's modulate)
	struct DecalTextures
	{
		Ref<ImageTexture> albedo;
	};
	std::map<std::string, DecalTextures> unlitDecals;
	const DecalTextures *unlitDecalFor(const std::string &file, uint32_t rgb);
	std::string mergeKey;
	MergedSelectionDecals merged;
	int64_t mergeComposes = 0; ///< lane SELPERF-1: how often the state report composed the merged image
	std::map<std::string, PackedByteArray> decalPixels; // RGBA8 of the decal textures, by name
	std::map<std::string, Vector2i> decalPixelSizes;
	DecalImage decalImageFor(const std::string &file);
	void drawSelectionDecals();
	void freeSelectionDecals();
	// SPELL-4: the radius cursor is composited by the terrain alongside selection decals.
	bool radiusCursorBound = false;
	bool radiusDecalDrawn = false;
	std::string radiusDecalError; ///< the texture of the cursor that could not be read (also in the HUD's draw errors)
	void drawRadiusCursor();
	// lane OWNERFIX-2: the resource claim feedback (InGameHud::resourceFeedback, GameClient/TerrainResourceFeedback.h): the selected resource building's
	// claim decals, the placement decal (its texture masked by the claimable vertices) and the placement's percentage text
	std::vector<uint64_t> claimDecalIds;
	uint64_t placeDecalId = 0;
	Ref<ImageTexture> placeDecalTexture;
	unsigned placeDecalVersion = ~0u;
	int claimDecalsDrawn = 0;
	bool placeDecalDrawn = false;
	bool placeTextDrawn = false;
	bool chatHistoryShown = false; ///< lane CHAT-1: the scroll-back replaces the message lines while the chat window is open
	int messageLinesDrawn = 0;     ///< lane CHAT-1: the lines the last draw put on screen
	Rect2 messageLinesRect;        ///< lane CHAT-1: their extent (window pixels)
	void drawMessageLines();
	Rect2 placeTextRect;
	std::string resourceDecalError;
	void drawResourceFeedback();
	void drawPlacementText(const Ref<Font> &font, float k);
	void applyCamera(const Vector2 &window, double alpha);
};

// SMOOTH-1 (S-810): a bound method that reads or changes the live game waits until the logic worker has finished its frame (the HUD sees a completed
// frame, never one being computed); the per-frame _process does not wait (InGameHud::update skips the frames the worker is busy in)
static void waitLogic(const HudDevice *d, const char *file = __builtin_FILE(), int line = __builtin_LINE());

// lane OWNERFIX-3: the texture's greyscale copy (the luminance 0.299 / 0.587 / 0.114 of each texel, its alpha kept: the radial bubbles' grey shader,
// GodotHud6Draw.cpp kGrayShader), made once per file
Ref<ImageTexture> HudDevice::grayTextureFor(const std::string &file)
{
	auto it = grayTextures.find(file);
	if (it != grayTextures.end())
	{
		return it->second;
	}
	Ref<ImageTexture> colour = textureFor(file);
	Ref<ImageTexture> out;
	if (colour.is_valid())
	{
		Ref<godot::Image> img = colour->get_image();
		if (img.is_valid() && !img->is_empty())
		{
			img = img->duplicate();
			if (img->is_compressed())
			{
				img->decompress();
			}
			img->convert(godot::Image::FORMAT_RGBA8);
			PackedByteArray px = img->get_data();
			uint8_t *p = px.ptrw();
			for (int64_t i = 0; i + 3 < px.size(); i += 4)
			{
				const uint8_t g = (uint8_t)std::lround(p[i] * 0.299f + p[i + 1] * 0.587f + p[i + 2] * 0.114f);
				p[i] = p[i + 1] = p[i + 2] = g;
			}
			out = ImageTexture::create_from_image(godot::Image::create_from_data(img->get_width(), img->get_height(), img->has_mipmaps(), godot::Image::FORMAT_RGBA8, px));
		}
	}
	grayTextures[file] = out;
	return out;
}

Ref<Image> HudDevice::imageFor(const std::string &file)
{
	auto it = godotImages.find(file);
	if (it != godotImages.end())
	{
		return it->second;
	}
	Ref<Image> img;
	const AptTextureStore::Entry &e = textures->get(file);
	if (e.ok && !e.rgba.empty())
	{
		PackedByteArray px;
		px.resize((int64_t)e.rgba.size());
		memcpy(px.ptrw(), e.rgba.data(), e.rgba.size());
		img = Image::create_from_data(e.width, e.height, false, Image::FORMAT_RGBA8, px);
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
		const std::string packedStem = fs.is_valid() && fs->is_bfme1() ? "art/textures/" + lower :
			(lower.size() > 2 ? "art/compiledtextures/" + lower.substr(0, 2) + "/" + lower : std::string());
		img = !packedStem.empty() ? loadPackedTexture(*source, packedStem, &paired) : Ref<Image>();
		if (img.is_valid())
		{
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
	godotImages[file] = img;
	return img;
}

Ref<ImageTexture> HudDevice::textureFor(const std::string &file)
{
	auto it = godotTextures.find(file);
	if (it != godotTextures.end())
	{
		return it->second;
	}
	const Ref<Image> img = imageFor(file);
	Ref<ImageTexture> tex = img.is_valid() ? ImageTexture::create_from_image(img) : Ref<ImageTexture>();
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
	if (hud6 && hud6->handles(op.symbolName))
	{
		hud6->drawHelp(r, item); // lane HUD-6: the help box's content clip (`_type` = "<clip>_Content", RW 0x92E2AC)
		return;
	}
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
		const bool visionChanged = snap && snap->shroud && (!radarShroudView || radarShroudView->localPlayer != snap->shroud->localPlayer ||
			radarShroudView->visionPlayers != snap->shroud->visionPlayers);
		// lane RADAR-1: the object overlay (W3DRadar::draw RW 0x450108 .. 0x4501FA): rebuilt when the client frame % 6 == 0 (and once at the start, + 0x1464),
		// a 128 x 128 texture drawn over the picture's rectangle with UV (0,1)-(1,0) (texture row 0 at the bottom); RenderRadar's tint is -1 (RW 0x6D427C)
		radarPicture = sq;
		radarClip = r; // lane HUD-7
		++radarDraws;
		radar.setPicture(ul[0], ul[1], lr[0] - ul[0], lr[1] - ul[1]); // the events' pings (RW 0x44DE58, InGameHud::updateRadarEvents)
		const unsigned cf = radar.clientFrame();
		if (snap && (visionChanged || radarOverlay.is_null() || Radar::overlayDue(cf, radarOverlayFrame)))
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
		if (visionChanged || Radar::shroudVersion(shroudView) != radarShroudVersion || (radarShroud.is_null() && radarShroudTried == false))
		{
			radarShroudView = snap ? snap->shroud : nullptr;
			radarShroudVersion = Radar::shroudVersion(shroudView);
			radarShroudTried = true;
			radarShroud.unref();
			const std::vector<std::int16_t> alpha = shroudView ? radar.shroudAlpha(*shroudView) : std::vector<std::int16_t>();
			const ::Image *mapped = alpha.empty() ? nullptr : images.findImageByName("ScrollShroud");
			if (!alpha.empty() && !mapped && drawErrors.size() < 100)
			{
				drawErrors.push_back("radar: the mapped image ScrollShroud is unknown");
			}
			Ref<Image> scroll = mapped ? imageFor(mapped->filename) : Ref<Image>();
			if (scroll.is_valid())
			{
				// The radar changes CPU pixels, not rendered output; never read the uploaded texture back from the GPU.
				Ref<Image> src = scroll->duplicate();
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
		std::copy(corners, corners + 8, radarViewBox); // lane HUD-7: what the box was drawn with
		++radarViewBoxDraws;
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
		// BFME2 AptPlayer::FindRenderImage (0x223AC4, called by the image draw 0x412B94 with the clip's path and parameters): after the dynamic image map,
		// `_imageMap` names a MappedImageCollection image directly (static button / background layers are not entries of Palantir's dynamic map).
		// INFERENCE (S-5016): 0x223AC4's body is not decompiled in Open-BFME-2; the order (dynamic map first) was not read
		AptPalantir::NativeImage staticImage;
		const std::string imageMap = varOf(op, "_imageMap");
		if ((!img || img->image.empty()) && !imageMap.empty() && images.findImageByName(imageMap))
		{
			staticImage.image = imageMap;
			img = &staticImage;
		}
		// lane CAMPUI-1: an image record of the window manager (setAptImage: InGameNotificationBox's "_level<n>_Icon", RW 0x8EB81E -> RW 0x92B1F9), as the
		// player's own RenderImage path reads them outside a game (GodotAptPlayer: the score screen's icons)
		AptPalantir::NativeImage recordImage;
		if ((!img || img->image.empty()) && !imageMap.empty() && hud->palantir())
		{
			const std::string *record = hud->palantir()->windows().aptImage(imageMap);
			if (record && !record->empty())
			{
				recordImage.image = *record;
				img = &recordImage;
			}
		}
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
			if (hud->palantir() && op.path == hud->palantir()->portraitKey())
			{
				portraitDrawn.clear(); // lane UI-4
			}
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
		// lane OWNERFIX-3: a disabled button's image (`_mode` GRAYSCALE, RenderImageDisabled) is drawn in greyscale, as the radial bubbles' disabled
		// windows are (W3DGadgetPushButtonImageDrawOne RW 0x4A5112's grayscale mode); it was a 0.45 dimming before (an inference) and the owner saw
		// another player's disabled buttons in colour. INFERENCE (S-5040): RotWK's RenderImage callback was not read; the luminance weights are the
		// bubbles' shader's
		Ref<ImageTexture> tex = img->grayscale ? grayTextureFor(mapped->filename) : textureFor(mapped->filename);
		if (tex.is_null())
		{
			return;
		}
		const float tw = (float)tex->get_width(), th = (float)tex->get_height();
		const Rect2 src(mapped->uvLo[0] * tw, mapped->uvLo[1] * th, (mapped->uvHi[0] - mapped->uvLo[0]) * tw, (mapped->uvHi[1] - mapped->uvLo[1]) * th);
		Color mod = Color(1, 1, 1, 1);
		// lane PLAY-1: the clip's cumulative colour multiplies the image (SpellStore.apt's _disabled frame places the image at alpha 0.396: a locked power is
		// dimmed; drawn at full alpha every power looked buyable). INFERENCE: retail's RenderImage draw was not read; the add terms are not applied
		mod = Color(mod.r * op.placeholderColor[0], mod.g * op.placeholderColor[1], mod.b * op.placeholderColor[2], mod.a * op.placeholderColor[3]);
		// BFME2 AptImageModeParser (0x412AF3; GameClient/GUI/AptScreens/AptImageMode.h): the clip's `_mode`. Recruitment art uses transparent foreground
		// pixels whose RGB still holds the background, drawn SOLID. The per-mode render states are inferred (S-5016)
		const AptImageMode mode = parseAptImageMode(varOf(op, "_mode"));
		if (mode != APT_IMAGE_ALPHA)
		{
			Ref<ShaderMaterial> &material = imageModes[(int)mode];
			if (material.is_null())
			{
				Ref<Shader> shader;
				shader.instantiate();
				shader->set_code(String(aptImageModeShader(mode).c_str()));
				material.instantiate();
				material->set_shader(shader);
			}
			rs->canvas_item_set_material(item, material->get_rid());
		}
		else
		{
			rs->canvas_item_set_material(item, RID()); // an in-place redraw (redrawNativePlaceholders) keeps the item: no earlier mode's material
		}
		rs->canvas_item_add_texture_rect_region(item, r, tex->get_rid(), src, mod);
		++drawnImages;
		if (hud->palantir() && op.path == hud->palantir()->portraitKey())
		{
			portraitDrawn = img->image; // lane UI-4
			++portraitDraws;
		}
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
		// lane OWNERFIX-1 (the owner's report: health bars over the Powers screen): the decorations are in-game UI, drawn before the windows (ZH
		// W3DDisplay::draw: the views with their drawables' icons, then TheWindowManager->winRepaint(); RotWK's Apt screens are the window manager's).
		// In a game the HUD node comes after the shell's player in the scene (game.gd _install_hud), so a sibling-order draw index cannot put them under
		// the shell's movies: one z step below the shell's items does (as the HUD-6 radial): under the Palantir, SpellStore.apt and every other screen
		rs->canvas_item_set_z_index(iconItem, -1);
	}
	rs->canvas_item_clear(iconItem);
	if (hud->menuCoversStage())
	{
		iconOpsDrawn = 0;
		dragBoxShown = false;
		return; // lane OPTS-6: nothing of the world over a menu that covers the stage
	}
	{
		IconUISettings is = hud->iconSettings();
		is.zoom = cam ? cam->getZoom() : 1.0f;
		hud->setIconUISettings(is);
	}
	auto col = [](std::uint32_t argb) { return Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f); };
	Ref<Font> font = ThemeDB::get_singleton() ? ThemeDB::get_singleton()->get_fallback_font() : Ref<Font>();
	// lane OPTS-2 (not retail): the UI scale enlarges each object's decorations about the centre of their ops (retail draws them in screen pixels: at
	// 1.0 nothing moves)
	const float k = uiScale;
	std::map<::ObjectID, Rect2> groups;
	if (k != 1.0f)
	{
		for (const IconUIOp &op : hud->iconOps())
		{
			if (op.stageTextSize > 0.0f)
			{
				continue; // a money text is anchored to its own building and grows through its size (below), not about a group's centre
			}
			const Rect2 r(op.x, op.y, op.w, op.h);
			auto it = groups.find(op.object);
			if (it == groups.end())
			{
				groups.emplace(op.object, r);
			}
			else
			{
				it->second = it->second.merge(r);
			}
		}
	}
	const int fontSize = (int)std::lround(13.0f * k);
	const float line = k;
	iconOpsDrawn = 0;
	if (iconsCovered)
	{
		// lane SAVE-UI1: a menu over the game covers the drawables' decorations (retail draws them with the world, before the window manager's movies)
		return;
	}
	for (const IconUIOp &raw : hud->iconOps())
	{
		IconUIOp op = raw;
		if (k != 1.0f && raw.stageTextSize <= 0.0f)
		{
			const Vector2 c = groups[raw.object].get_center();
			op.x = c.x + (raw.x - c.x) * k;
			op.y = c.y + (raw.y - c.y) * k;
			op.w = raw.w * k;
			op.h = raw.h * k;
		}
		switch (op.kind)
		{
			case IconUIOp::FILL_RECT:
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y, op.w, op.h), col(op.color));
				break;
			case IconUIOp::OPEN_RECT: // Display +0xE0 (width 1): the four edges (lane OPTS-2: the UI scale's width)
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y, op.w, line), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y + op.h - line, op.w, line), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x, op.y + line, line, op.h - 2 * line), col(op.color));
				rs->canvas_item_add_rect(iconItem, Rect2(op.x + op.w - line, op.y + line, line, op.h - 2 * line), col(op.color));
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
				// a stage-sized text (the money texts) grows with the window as the HUD movies do: the window height over the 600 stage rows
				const float stageScale = op.stageTextSize > 0.0f ? std::max(1.0f, (float)hud->windowHeight() / 600.0f) : 1.0f;
				const int size_px = op.stageTextSize > 0.0f ? (int)std::lround(op.stageTextSize * stageScale * k) : fontSize;
				const float drop = op.stageTextSize > 0.0f ? std::max(1.0f, std::round(stageScale)) : 1.0f;
				const Vector2 size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px);
				const Vector2 at(op.x - std::floor(size.x / 2.0f), op.y + font->get_ascent(size_px)); // RW 0x677F1B: x - width / 2, the string's top at y
				font->draw_string(iconItem, at + Vector2(drop, drop), text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, col(op.dropColor));
				font->draw_string(iconItem, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, col(op.color));
				break;
			}
		}
		++iconOpsDrawn;
	}
	// lane PLAY-3: the drag selection box (InGameHud::selectionRegionOp, W3DInGameUI::drawSelectionRegion RW 0x48ECF4) over the decorations and under the
	// Palantir (ZH W3DDisplay::draw: the in-game UI, then the windows). Display +0xE0 (BFME2 decomp W3DDisplayDrawOpenRect.cpp) hands RectClass(x, y,
	// x + w, y + h) to Render2D Add_Outline; DONOR (WW3D render2d.cpp Add_Outline): four lines of the given width, (L + 1, B)-(L + 1, T + 1),
	// (L, T + 1)-(R - 1, T + 1), (R, T)-(R, B - 1), (R, B)-(L + 1, B). INFERENCE: Godot's line of width 2 covers the same pixels as Add_Line's quad
	IconUIOp box;
	dragBoxShown = hud->selectionRegionOp(box);
	if (dragBoxShown)
	{
		const float l = box.x, t = box.y, r = box.x + box.w, b = box.y + box.h;
		const Color c = col(box.color);
		rs->canvas_item_add_line(iconItem, Vector2(l + 1, b), Vector2(l + 1, t + 1), c, box.width);
		rs->canvas_item_add_line(iconItem, Vector2(l, t + 1), Vector2(r - 1, t + 1), c, box.width);
		rs->canvas_item_add_line(iconItem, Vector2(r, t), Vector2(r, b - 1), c, box.width);
		rs->canvas_item_add_line(iconItem, Vector2(r, b), Vector2(l + 1, b), c, box.width);
		dragBox = box;
		++dragBoxFrames;
	}
	drawPlacementText(font, k); // lane OWNERFIX-2
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
	shown.cam = cam.get();
	shown.present((int)window.x, (int)window.y, alpha);
	const Coord3D eyeS = shown.eye(), targetS = shown.PinholeView::position();
	const Vector3 eye = toGodotAxes(eyeS);
	const Vector3 target = toGodotAxes(targetS);
	// retail: horizontal field of view 50 degrees (View + 0x6C), near 10, far 1800 (RW 0x48B7B1); the view plane's height follows the viewport aspect.
	// lane UI-6: the Widescreen view's Wider widens the horizontal angle beyond 4:3 (TacticalCamera::setWidescreenView; Original is retail's 50 degrees)
	camera->set_keep_aspect_mode(Camera3D::KEEP_WIDTH);
	const float hfov = cam->widescreenView() ? cam->viewHorizontalFov() : cam->horizontalFov();
	camera->set_fov((real_t)(hfov * 180.0f / 3.14159265f));
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
	// lane CAMANIM-1: in mode 4 the camera build (RW 0x502858) takes the animation's transform and the view plane its FOV as the horizontal angle
	// (RW 0x48B919); the W3D camera looks down its -Z with Y up as Godot's does, so the matrix's columns map axis by axis (SAGE -> Godot)
	Matrix3D tm;
	float animFov = 0.0f;
	if (camAnim.transform(alpha, tm, animFov))
	{
		auto col = [&](int c) { return toGodotAxes(Coord3D{ tm.Row[0][c], tm.Row[1][c], tm.Row[2][c] }); };
		t.basis = Basis(col(0), col(1), col(2));
		t.origin = col(3);
		camera->set_fov((real_t)(animFov * 180.0f / 3.14159265f));
	}
	camera->set_global_transform(t);
}

static void waitLogic(const HudDevice *d, const char *file, int line)
{
	if (d && d->world && d->world->hud_game())
	{
		d->world->hud_game()->holdLogic(file, line); // lane RENDERWAIT-1: no wait inside the client window; elsewhere the caller names the wait in the wait log
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
	ClassDB::bind_method(D_METHOD("get_hud6_state"), &InGameHudNode::get_hud6_state); // lane HUD-6
	ClassDB::bind_method(D_METHOD("take_chat_requests"), &InGameHudNode::take_chat_requests); // lane CHAT-1
	ClassDB::bind_method(D_METHOD("chat_text", "label"), &InGameHudNode::chat_text);
	ClassDB::bind_method(D_METHOD("chat_format", "mode", "text"), &InGameHudNode::chat_format);
	ClassDB::bind_method(D_METHOD("add_chat_line", "text", "rgb"), &InGameHudNode::add_chat_line);
	ClassDB::bind_method(D_METHOD("set_chat_history_shown", "shown"), &InGameHudNode::set_chat_history_shown);
	ClassDB::bind_method(D_METHOD("message_lines_state"), &InGameHudNode::message_lines_state);
	ClassDB::bind_method(D_METHOD("set_network_game", "on"), &InGameHudNode::set_network_game);
	ClassDB::bind_method(D_METHOD("beacon_state"), &InGameHudNode::beacon_state);
	ClassDB::bind_method(D_METHOD("get_mapped_image", "name"), &InGameHudNode::get_mapped_image);
	ClassDB::bind_method(D_METHOD("get_spellbook_state"), &InGameHudNode::get_spellbook_state);
	ClassDB::bind_method(D_METHOD("get_icon_ui"), &InGameHudNode::get_icon_ui); // lane HUD-5
	ClassDB::bind_method(D_METHOD("set_icons_covered", "covered"), &InGameHudNode::set_icons_covered); // lane SAVE-UI1
	ClassDB::bind_method(D_METHOD("select_object", "id"), &InGameHudNode::select_object); // lane HUD-5
	ClassDB::bind_method(D_METHOD("select_object_with_message", "id"), &InGameHudNode::select_object_with_message); // lane BACKLOG2
	ClassDB::bind_method(D_METHOD("select_objects", "ids"), &InGameHudNode::select_objects); // lane SELPERF-1
	ClassDB::bind_method(D_METHOD("select_objects_with_message", "ids"), &InGameHudNode::select_objects_with_message);
	ClassDB::bind_method(D_METHOD("open_spell_store"), &InGameHudNode::open_spell_store);
	ClassDB::bind_method(D_METHOD("close_spell_store"), &InGameHudNode::close_spell_store);
	ClassDB::bind_method(D_METHOD("cancel_spell_store"), &InGameHudNode::cancel_spell_store);
	ClassDB::bind_method(D_METHOD("spell_store_click", "index"), &InGameHudNode::spell_store_click);
	ClassDB::bind_method(D_METHOD("press_spell_slot", "slot"), &InGameHudNode::press_spell_slot);
	ClassDB::bind_method(D_METHOD("get_frame_timings"), &InGameHudNode::get_frame_timings);
	ClassDB::bind_method(D_METHOD("get_placement"), &InGameHudNode::get_placement);
	ClassDB::bind_method(D_METHOD("take_misc_sounds"), &InGameHudNode::take_misc_sounds);
	ClassDB::bind_method(D_METHOD("get_cursor_image"), &InGameHudNode::get_cursor_image);
	ClassDB::bind_method(D_METHOD("get_placement_ghost_state"), &InGameHudNode::get_placement_ghost_state);
	ClassDB::bind_static_method("InGameHudNode", D_METHOD("image_mode_shader", "mode"), &InGameHudNode::image_mode_shader); // community PR #37
	ClassDB::bind_method(D_METHOD("get_move_hints"), &InGameHudNode::get_move_hints);
	ClassDB::bind_method(D_METHOD("press_command_button", "template_name"), &InGameHudNode::press_command_button);
	ClassDB::bind_method(D_METHOD("get_command_buttons"), &InGameHudNode::get_command_buttons);
	ClassDB::bind_method(D_METHOD("inject_mouse_move", "position"), &InGameHudNode::inject_mouse_move);
	ClassDB::bind_method(D_METHOD("inject_mouse_button", "button", "down", "position", "double_click"), &InGameHudNode::inject_mouse_button, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("inject_key", "dik_code", "down"), &InGameHudNode::inject_key);
	ClassDB::bind_method(D_METHOD("hold_input", "on"), &InGameHudNode::hold_input);                 // lane BACKLOG3 (QA)
	ClassDB::bind_method(D_METHOD("set_input_enabled", "on"), &InGameHudNode::set_input_enabled); // lane BACKLOG3 (QA)
	ClassDB::bind_method(D_METHOD("input_enabled"), &InGameHudNode::input_enabled); // lane BFME2CAMP1
	ClassDB::bind_method(D_METHOD("script_input", "enable"), &InGameHudNode::script_input); // lane OWNERFIX-1
	ClassDB::bind_method(D_METHOD("set_broadcast_clean", "on"), &InGameHudNode::set_broadcast_clean);
	ClassDB::bind_method(D_METHOD("inject_mouse_wheel", "notches", "position"), &InGameHudNode::inject_mouse_wheel);
	ClassDB::bind_method(D_METHOD("get_camera"), &InGameHudNode::get_camera);
	ClassDB::bind_method(D_METHOD("set_edge_scroll", "on"), &InGameHudNode::set_edge_scroll);
	ClassDB::bind_method(D_METHOD("set_free_camera", "on"), &InGameHudNode::set_free_camera);
	ClassDB::bind_method(D_METHOD("set_view_options", "options"), &InGameHudNode::set_view_options); // lane OPTS-2
	ClassDB::bind_method(D_METHOD("camera_look_at", "sage_xy"), &InGameHudNode::camera_look_at);
	ClassDB::bind_method(D_METHOD("add_player_message", "text"), &InGameHudNode::add_player_message); // lane CHEAT-1
	ClassDB::bind_method(D_METHOD("get_player_message_layout"), &InGameHudNode::get_player_message_layout);
	ClassDB::bind_method(D_METHOD("camera_set_height", "height_above_ground"), &InGameHudNode::camera_set_height);
	ClassDB::bind_method(D_METHOD("camera_reset", "sage_xy"), &InGameHudNode::camera_reset);                 // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("camera_set_zoom_limited", "on"), &InGameHudNode::camera_set_zoom_limited); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("camera_animation_request", "action", "name"), &InGameHudNode::camera_animation_request); // lane CAMANIM-1
	ClassDB::bind_method(D_METHOD("camera_animation_state"), &InGameHudNode::camera_animation_state);                  // lane CAMANIM-1
	ClassDB::bind_method(D_METHOD("camera_set_clip_depth_multiplier", "m"), &InGameHudNode::camera_set_clip_depth_multiplier); // lane MODCOMPAT-AI
	ClassDB::bind_method(D_METHOD("set_objectives_screen_open", "open"), &InGameHudNode::set_objectives_screen_open); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("flash_palantir_button", "slot", "seconds"), &InGameHudNode::flash_palantir_button); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("is_objectives_flag_flashing"), &InGameHudNode::is_objectives_flag_flashing); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("pixel_to_world", "pixel"), &InGameHudNode::pixel_to_world);
	ClassDB::bind_method(D_METHOD("ping_point", "pixel"), &InGameHudNode::ping_point); // the ping wheel
	ClassDB::bind_method(D_METHOD("world_to_pixel", "world_xy"), &InGameHudNode::world_to_pixel);
	ClassDB::bind_method(D_METHOD("pick_probe", "pixel", "id"), &InGameHudNode::pick_probe);
	ClassDB::bind_method(D_METHOD("pick_box_pixels", "id"), &InGameHudNode::pick_box_pixels);
	ClassDB::bind_method(D_METHOD("find_button_window", "path"), &InGameHudNode::find_button_window);
	ClassDB::bind_method(D_METHOD("get_radar_square"), &InGameHudNode::get_radar_square);
	ClassDB::bind_method(D_METHOD("world_to_radar_pixel", "world_xy"), &InGameHudNode::world_to_radar_pixel);
	ClassDB::bind_method(D_METHOD("create_radar_event", "world_xy", "type", "jump_to"), &InGameHudNode::create_radar_event, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_radar_state"), &InGameHudNode::get_radar_state); // lane HUD-7
	ClassDB::bind_method(D_METHOD("radar_probe", "world_xy"), &InGameHudNode::radar_probe); // lane HUD-7
	ClassDB::bind_method(D_METHOD("camera_set_angle_pitch", "angle", "pitch"), &InGameHudNode::camera_set_angle_pitch); // lane HUD-7 (a test hook)
	ClassDB::bind_method(D_METHOD("dump_tree", "max_depth"), &InGameHudNode::dump_tree, DEFVAL(4));
	ClassDB::bind_method(D_METHOD("invoke_at", "path", "function", "args"), &InGameHudNode::invoke_at, DEFVAL(PackedStringArray()));
}

void HudDevice::drawRadiusCursor()
{
	const RadiusCursorDecal &rc = hud->radiusCursor().decal();
	RenderingServer *rs = RenderingServer::get_singleton();
	radiusDecalDrawn = false;
	radiusDecalError.clear();
	if (!rs) return;
	// A zero size disables sampling, including cancellation and missing textures.
	rs->global_shader_parameter_set("radius_cursor_transform", Vector4());
	if (!rc.visible) return;
	const bool alphaStyle = (rc.style & 0x1420u) != 0;
	const DecalTextures *dt = unlitDecalFor(rc.texture, alphaStyle ? rc.rgb : (rc.packed & 0xFFFFFFu));
	if (!dt)
	{
		radiusDecalError = "radius cursor " + rc.type + ": no texture " + rc.texture;
		return;
	}
	// Composite the texture and opacity with the terrain, as the selection's
	// merge decals do. This also supports unshaded terrain, where a Godot
	// albedo-only Decal cannot supply a visible radius preview.
	// S-4640: this remains the port's terrain projection, not retail's stencil pass.
	rs->global_shader_parameter_set("radius_cursor_texture", dt->albedo->get_rid());
	rs->global_shader_parameter_set("radius_cursor_params", Vector4(alphaStyle ? rc.opacity / 255.0f : 1.0f, alphaStyle ? 1.0f : 0.0f, 0.0f, 0.0f));
	rs->global_shader_parameter_set("radius_cursor_transform", Vector4(rc.position.x, rc.position.y, rc.size, rc.angle));
	radiusCursorBound = true;
	radiusDecalDrawn = true;
}

void HudDevice::drawResourceFeedback()
{
	// lane OWNERFIX-2 (GameClient/TerrainResourceFeedback.h): drawn as the selection decals are (S-2522): a Godot Decal over the terrain layer. The claim
	// decals: TerrainResourceClaimDecal tinted 0x6464C8 at opacity 0x80 (type 0x20, SHADOW_ALPHA_DECAL); the placement decal: PlaceTerrainResourceClaimantDecal
	// tinted the band colour, its alpha the texture's times the claimable vertices' alpha (RW 0x75C1A9 with the opacity 0x80; INFERENCE S-5030: bilinear across
	// a terrain quad)
	Node3D *root = decalRootId ? Object::cast_to<Node3D>(ObjectDB::get_instance(godot::ObjectID(decalRootId))) : nullptr;
	claimDecalsDrawn = 0;
	placeDecalDrawn = false;
	if (!root)
	{
		return;
	}
	const TerrainResourceUiSettings &ui = hud->radiusCursorTemplates().resourceUi();
	const TerrainResourceFeedback &fb = hud->resourceFeedback();
	auto place = [](Decal *decal, const Coord3D &p, float size) {
		// SAGE (x, y, z up) -> Godot (x, z, -y); projected down over a box `size` wide and 200 units deep around the point
		decal->set_position(Vector3(p.x, p.z, -p.y));
		decal->set_rotation(Vector3(0.0f, 0.0f, 0.0f));
		decal->set_size(Vector3(size, 200.0f, size));
		decal->set_albedo_mix(1.0f);
		decal->set_cull_mask(1u << 19); // the terrain's layer (GodotMapTerrain)
		decal->set_upper_fade(0.0f);
		decal->set_lower_fade(0.0f);
	};
	const std::vector<ResourceClaimDecal> &claimed = fb.claimedDecals();
	const DecalTextures *claimTex = claimed.empty() ? nullptr : unlitDecalFor(ui.claimDecal, 0x6464C8u);
	if (!claimed.empty() && !claimTex)
	{
		resourceDecalError = "TerrainResourceClaimDecal: no texture '" + ui.claimDecal + "'";
	}
	for (size_t i = 0; claimTex && i < claimed.size(); ++i)
	{
		Decal *decal = i < claimDecalIds.size() ? Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(claimDecalIds[i]))) : nullptr;
		if (!decal)
		{
			decal = memnew(Decal);
			root->add_child(decal);
			if (i < claimDecalIds.size())
			{
				claimDecalIds[i] = decal->get_instance_id();
			}
			else
			{
				claimDecalIds.push_back(decal->get_instance_id());
			}
		}
		place(decal, claimed[i].position, claimed[i].size);
		decal->set_texture(Decal::TEXTURE_ALBEDO, claimTex->albedo);
		decal->set_modulate(Color(1, 1, 1, claimed[i].opacity / 255.0f));
		decal->set_visible(true);
		++claimDecalsDrawn;
	}
	for (size_t i = (size_t)claimDecalsDrawn; i < claimDecalIds.size(); ++i)
	{
		if (Decal *decal = Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(claimDecalIds[i]))))
		{
			decal->set_visible(false);
		}
	}
	const ResourcePlacementFeedback &rp = fb.placement();
	Decal *pd = placeDecalId ? Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(placeDecalId))) : nullptr;
	if (!rp.active || rp.maskW <= 0 || rp.maskH <= 0 || !(rp.decalSize > 0.0f))
	{
		if (pd)
		{
			pd->set_visible(false);
		}
		return;
	}
	if (rp.version != placeDecalVersion || placeDecalTexture.is_null())
	{
		const DecalImage img = decalImageFor(ui.placeDecal);
		if (img.width <= 0 || !img.rgba)
		{
			resourceDecalError = "PlaceTerrainResourceClaimantDecal: no texture '" + ui.placeDecal + "'";
			if (pd)
			{
				pd->set_visible(false);
			}
			return;
		}
		const int w = img.width, h = img.height;
		PackedByteArray px;
		px.resize((int64_t)w * h * 4);
		uint8_t *out = px.ptrw();
		const unsigned col[3] = { (rp.color >> 16) & 0xFFu, (rp.color >> 8) & 0xFFu, rp.color & 0xFFu };
		const float half = rp.decalSize * 0.5f;
		auto maskAt = [&](float vx, float vy) {
			// bilinear between the four vertices around (vx, vy), in vertex units relative to the mask's first vertex
			const float fx = std::clamp(vx, 0.0f, (float)(rp.maskW - 1)), fy = std::clamp(vy, 0.0f, (float)(rp.maskH - 1));
			const int x0 = std::min((int)fx, rp.maskW - 1), y0 = std::min((int)fy, rp.maskH - 1);
			const int x1 = std::min(x0 + 1, rp.maskW - 1), y1 = std::min(y0 + 1, rp.maskH - 1);
			const float tx = fx - (float)x0, ty = fy - (float)y0;
			auto a = [&](int x, int y) { return (float)rp.maskAlpha[(size_t)y * (size_t)rp.maskW + (size_t)x]; };
			return (a(x0, y0) * (1 - tx) + a(x1, y0) * tx) * (1 - ty) + (a(x0, y1) * (1 - tx) + a(x1, y1) * tx) * ty;
		};
		for (int j = 0; j < h; ++j)
		{
			// the texture's v runs along Godot +z = SAGE -y
			const float wy = rp.decalCenter.y + half - ((float)j + 0.5f) / (float)h * rp.decalSize;
			for (int i = 0; i < w; ++i)
			{
				const float wx = rp.decalCenter.x - half + ((float)i + 0.5f) / (float)w * rp.decalSize;
				const float m = maskAt(wx / TerrainResourceFeedback::kVertexSpacing - (float)rp.maskVx0, wy / TerrainResourceFeedback::kVertexSpacing - (float)rp.maskVy0);
				const size_t o = ((size_t)j * (size_t)w + (size_t)i) * 4;
				for (int c = 0; c < 3; ++c)
				{
					out[o + c] = (uint8_t)((img.rgba[o + c] * col[c] + 127) / 255u);
				}
				out[o + 3] = (uint8_t)std::lround(img.rgba[o + 3] * m / 255.0f);
			}
		}
		// lane FOGFX-1 (FB-0055): the decal's texture is updated in place (a new ImageTexture as the pointer crossed each claim cell made Godot pack its
		// decal atlas again)
		const Ref<Image> image = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, px);
		if (placeDecalTexture.is_valid() && placeDecalTexture->get_width() == w && placeDecalTexture->get_height() == h)
		{
			placeDecalTexture->update(image);
		}
		else
		{
			placeDecalTexture = ImageTexture::create_from_image(image);
		}
		placeDecalVersion = rp.version;
	}
	if (!pd)
	{
		pd = memnew(Decal);
		root->add_child(pd);
		placeDecalId = pd->get_instance_id();
	}
	place(pd, rp.decalCenter, rp.decalSize);
	pd->set_texture(Decal::TEXTURE_ALBEDO, placeDecalTexture);
	pd->set_modulate(Color(1, 1, 1, 1));
	pd->set_visible(true);
	placeDecalDrawn = true;
}

namespace
{
// UTF-16 (the engine's UnicodeString) to a Godot String: surrogate pairs joined, a lone surrogate shown as U+FFFD
String chatU16ToGodot(const std::u16string &u)
{
	String s;
	for (std::size_t i = 0; i < u.size(); ++i)
	{
		char32_t c = u[i];
		if (c >= 0xD800 && c <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00);
			++i;
		}
		else if (c >= 0xD800 && c <= 0xDFFF)
		{
			c = 0xFFFD;
		}
		else if (c < 32)
		{
			c = 0xFFFD; // a control character is not drawn as one (no line breaks, escapes or bells from another player's text)
		}
		s += String::chr(c);
	}
	return s;
}
std::u16string chatGodotToU16(const String &t)
{
	std::u16string u;
	for (int i = 0; i < t.length(); ++i)
	{
		const char32_t c = t[i];
		if (c > 0xFFFF)
		{
			u.push_back((char16_t)(0xD800 + ((c - 0x10000) >> 10)));
			u.push_back((char16_t)(0xDC00 + ((c - 0x10000) & 0x3FF)));
		}
		else
		{
			u.push_back((char16_t)c);
		}
	}
	return u;
}
} // namespace

void HudDevice::drawMessageLines()
{
	// lane CHAT-1: InGameUI's message draw (RW 0x6A0BC0; GameClient/InGameUIMessages.h): MessagePosition scaled from 1024 x 768, the lines oldest first,
	// a negative x from the right edge, each line's own colour over a black drop of the same alpha (ZH drawUIMessages); the font is MessageFont at
	// adjustFontSize(MessagePointSize) (RW 0x69C4B3 -> RW 0x5E9BEF)
	messageLinesDrawn = 0;
	messageLinesRect = Rect2();
	if (!hud || !iconItem.is_valid())
	{
		return;
	}
	const InGameUIMessageSettings &ms = hud->messageLines().settings();
	const int width = hud->windowWidth(), height = hud->windowHeight();
	if (width <= 0 || height <= 0)
	{
		return;
	}
	const int px = std::max(1, GlobalLanguage::adjustFontSize(ms.pointSize, width));
	float drawSize = (float)px;
	Ref<Font> font = player ? player->font_for(ms.font, (float)px, &drawSize) : Ref<Font>();
	if (font.is_valid() && player)
	{
		font = player->font_cell(font, (int)std::lround(drawSize));
	}
	if (font.is_null())
	{
		font = ThemeDB::get_singleton() ? ThemeDB::get_singleton()->get_fallback_font() : Ref<Font>();
	}
	if (font.is_null())
	{
		return;
	}
	const int size = std::max(1, (int)std::lround(drawSize));
	// the lines to draw, oldest first: the six message lines, or (the chat window open) the scroll-back's last lines in full colour
	struct L
	{
		String text;
		std::uint32_t argb;
	};
	std::vector<L> lines;
	if (chatHistoryShown)
	{
		const auto &h = hud->chat().history();
		const std::size_t kShown = 10;
		for (std::size_t i = h.size() > kShown ? h.size() - kShown : 0; i < h.size(); ++i)
		{
			lines.push_back({ chatU16ToGodot(h[i].text), h[i].argb | 0xFF000000u });
		}
	}
	else
	{
		const auto &ml = hud->messageLines().lines();
		for (int i = InGameUIMessages::kMaxLines - 1; i >= 0; --i)
		{
			if (ml[(size_t)i].used)
			{
				lines.push_back({ chatU16ToGodot(ml[(size_t)i].text), ml[(size_t)i].argb });
			}
		}
	}
	const int x0 = InGameUIMessages::screenX(width, ms.positionX);
	int y = InGameUIMessages::screenY(height, ms.positionY);
	const float ascent = font->get_ascent(size);
	const int lineH = std::max(1, (int)std::ceil(font->get_height(size)));
	for (const L &l : lines)
	{
		const int w = (int)std::ceil(font->get_string_size(l.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x);
		const int x = x0 < 0 ? width + x0 - w : x0;
		const float a = (float)((l.argb >> 24) & 0xFF) / 255.0f;
		const Color c(((l.argb >> 16) & 0xFF) / 255.0f, ((l.argb >> 8) & 0xFF) / 255.0f, (l.argb & 0xFF) / 255.0f, a);
		const Vector2 at((float)x, (float)y + ascent);
		font->draw_string(iconItem, at + Vector2(1, 1), l.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(0, 0, 0, a));
		font->draw_string(iconItem, at, l.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, c);
		messageLinesRect = messageLinesDrawn ? messageLinesRect.merge(Rect2((float)x, (float)y, (float)w, (float)lineH)) : Rect2((float)x, (float)y, (float)w, (float)lineH);
		++messageLinesDrawn;
		y += lineH;
	}
}

void HudDevice::drawPlacementText(const Ref<Font> &font, float k)
{
	// lane OWNERFIX-2: Impl::update / updateStringPos (BFME2 decomp Rva004E65CFLeaf.cpp): the text centred on the pointer's x (x += width / -2), its top at
	// the pointer's y - 20 - its height, clamped into the view; the band colour with a black drop (setColor(colour, 0xFF000000)). INFERENCE (S-1951): the
	// fallback font at PlaceTerrainResourceClaimantFont's size (the face Albertus MT is not loaded)
	placeTextDrawn = false;
	const ResourcePlacementFeedback &rp = hud->resourceFeedback().placement();
	if (!rp.active || rp.text.empty() || font.is_null() || !iconItem.is_valid())
	{
		return;
	}
	const TerrainResourceUiSettings &ui = hud->radiusCursorTemplates().resourceUi();
	const PlacementTextStyle style = PlacementTextStyle::forViewport(ui.fontSize, hud->windowHeight(), k);
	const int size = style.fontSize;
	String text;
	for (char16_t c : rp.text)
	{
		text += String::chr((char32_t)c);
	}
	const Vector2 sz = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size);
	const int w = (int)sz.x, h = (int)std::ceil(sz.y);
	int x = rp.pointerX + w / -2;
	int y = rp.pointerY - style.pointerGap - h;
	x = std::max(0, std::min(x, hud->windowWidth() - w));
	y = std::max(0, std::min(y, hud->windowHeight() - h));
	auto col = [](std::uint32_t argb) { return Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f); };
	const Vector2 at((float)x, (float)y + font->get_ascent(size));
	font->draw_string(iconItem, at + Vector2(style.shadowOffset, style.shadowOffset), text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(0, 0, 0, 1));
	font->draw_string(iconItem, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, col(rp.color));
	placeTextRect = Rect2((float)x, (float)y, (float)w, (float)h);
	placeTextDrawn = true;
}

void HudDevice::drawSelectionDecals()
{
	Node3D *root = decalRootId ? Object::cast_to<Node3D>(ObjectDB::get_instance(godot::ObjectID(decalRootId))) : nullptr;
	if (!root)
	{
		Node *parent = camera ? camera->get_parent() : nullptr;
		if (!parent)
		{
			return;
		}
		root = memnew(Node3D);
		root->set_name("SelectionDecals");
		parent->add_child(root);
		decalRootId = root->get_instance_id();
		decalIds.clear();
	}
	const std::vector<SelectionDecal> &list = hud->selectionDecals();
	decalsDrawn = 0;
	for (size_t i = 0; i < list.size(); ++i)
	{
		const SelectionDecal &sd = list[i];
		if (sd.style & 0x1000u)
		{
			continue; // composed below
		}
		const DecalTextures *dt = unlitDecalFor(sd.texture, sd.color & 0xFFFFFFu);
		if (!dt)
		{
			continue; // the texture's error is in drawErrors (textureFor)
		}
		Decal *decal = nullptr;
		if (decalsDrawn < decalIds.size())
		{
			decal = Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(decalIds[decalsDrawn])));
		}
		if (!decal)
		{
			decal = memnew(Decal);
			root->add_child(decal);
			if (decalsDrawn < decalIds.size())
			{
				decalIds[decalsDrawn] = decal->get_instance_id();
			}
			else
			{
				decalIds.push_back(decal->get_instance_id());
			}
		}
		++decalsDrawn;
		// SAGE (x, y, z up) -> Godot (x, z, -y); the decal projects down its -Y over a box as wide as the decal and 60 units deep around the drawable
		decal->set_position(Vector3(sd.position.x, sd.position.z, -sd.position.y));
		decal->set_rotation(Vector3(0.0f, sd.angle, 0.0f));
		decal->set_size(Vector3(sd.size, 60.0f, sd.size));
		// RotWK's alpha shader (terrain * (1 - a) + texel * colour * a, a = texel alpha * the colour's alpha); the terrain is unshaded (terrain.gdshader), so the
		// decal's albedo reaches the screen as it is
		decal->set_texture(Decal::TEXTURE_ALBEDO, dt->albedo);
		decal->set_modulate(Color(1, 1, 1, ((sd.color >> 24) & 0xFF) / 255.0f));
		decal->set_albedo_mix(1.0f);
		decal->set_cull_mask(1u << 19); // the terrain's layer (GodotMapTerrain): the units stand on the marker, it is not painted on them
		decal->set_upper_fade(0.0f);
		decal->set_lower_fade(0.0f);
		decal->set_visible(true);
	}
	for (size_t i = decalsDrawn; i < decalIds.size(); ++i)
	{
		if (Decal *decal = Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(decalIds[i]))))
		{
			decal->set_visible(false);
		}
	}

	// lane SELPERF-1: the merge decals: this frame's grid to the terrain shader (RotWK's two stencil passes per pixel there); the composed image is only the
	// state report's (composeMergeReport)
	const SelectionDecalSettings &ds = hud->selectionDecalSettings();
	RenderingServer *rs = RenderingServer::get_singleton();
	std::string error;
	// the grid follows the markers: built and uploaded when they changed (hud->selectionDecalsVersion), else the shader keeps the last one
	const bool fresh = hud->selectionDecalsVersion() != selVersion || selMode < 0;
	selVersion = hud->selectionDecalsVersion();
	if (fresh && (!selAtlasFor(list) || !BuildMergeDecalGrid(list, [this](const std::string &f, MergeDecalAtlasRect &r) {
			// a missing texture is not in the atlas: BuildMergeDecalGrid fails with its name (no outline this frame)
			return selAtlasPack.rect(f, r);
		}, ds, selGrid, &error)))
	{
		if (!error.empty() && drawErrors.size() < 100)
		{
			drawErrors.push_back(error);
		}
		selGrid = MergeDecalGrid();
	}
	const int mode = selGrid.decals == 0 ? 0 : (selGrid.simple ? 2 : 1);
	if (mode != 0 && rs)
	{
		if (fresh)
		{
			// the texture's height is the grid's rows rounded up to a power of two: a marching army's grid changes its row count often, and a new
			// texture is a global parameter change every terrain material sees (rows past the grid's are never read)
			int height = 4;
			while (height < selGrid.rows)
			{
				height *= 2;
			}
			const int64_t bytes = (int64_t)selGrid.texels.size() * (int64_t)sizeof(float);
			PackedByteArray px;
			px.resize((int64_t)height * MergeDecalGrid::kWidth * 4 * (int64_t)sizeof(float));
			memcpy(px.ptrw(), selGrid.texels.data(), (size_t)bytes);
			Ref<Image> img = Image::create_from_data(MergeDecalGrid::kWidth, height, false, Image::FORMAT_RGBAF, px);
			if (selData.is_valid() && selData->get_height() == height)
			{
				selData->update(img); // the same texture: no material sees a change
			}
			else
			{
				selData = ImageTexture::create_from_image(img);
				rs->global_shader_parameter_set("sel_data", selData->get_rid()); // the RID: the server holds no reference of its own
			}
			++selUploads;
		}
		// only what changed (a set marks the server's global buffer for upload)
		const Vector4 grid(selGrid.minX, selGrid.minY, selGrid.cellSize, (float)selGrid.cellsX);
		const Vector4 grid2((float)selGrid.cellsY, (float)selGrid.cellOffset, (float)selGrid.indexOffset, (float)mode);
		if (mode != selMode || grid != selGridSet)
		{
			rs->global_shader_parameter_set("sel_grid", grid);
			selGridSet = grid;
		}
		if (mode != selMode || grid2 != selGrid2Set)
		{
			rs->global_shader_parameter_set("sel_grid2", grid2);
			selGrid2Set = grid2;
		}
		if (mode != selMode || selGrid.alphaRef != selAlphaSet)
		{
			rs->global_shader_parameter_set("sel_alpha_ref", (float)selGrid.alphaRef);
			selAlphaSet = selGrid.alphaRef;
		}
		decalsDrawn += (unsigned)selGrid.decals;
	}
	else if (mode != selMode && rs)
	{
		rs->global_shader_parameter_set("sel_grid2", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	}
	selMode = mode;
}

bool HudDevice::selAtlasFor(const std::vector<SelectionDecal> &list)
{
	// the atlas (MergeDecalAtlas: packed and committed whole); a change is one new texture for the shader
	std::vector<std::string> errors;
	const bool changed = selAtlasPack.update(list, hud->selectionDecalSettings(), [this](const std::string &f) { return decalImageFor(f); }, errors);
	for (const std::string &e : errors)
	{
		if (drawErrors.size() < 100)
		{
			drawErrors.push_back(e);
		}
	}
	if (!changed)
	{
		return true;
	}
	PackedByteArray px;
	px.resize((int64_t)selAtlasPack.pixels().size());
	memcpy(px.ptrw(), selAtlasPack.pixels().data(), selAtlasPack.pixels().size());
	Ref<ImageTexture> atlas = ImageTexture::create_from_image(Image::create_from_data(selAtlasPack.width(), selAtlasPack.height(), false, Image::FORMAT_RGBA8, px));
	if (atlas.is_null())
	{
		if (drawErrors.size() < 100)
		{
			drawErrors.push_back("the selection decal atlas texture could not be made");
		}
		selAtlasPack.clear(); // packed again next frame
		selAtlas.unref();
		selVersion = ~0ull;
		return false;
	}
	selAtlas = atlas;
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set("sel_atlas", selAtlas->get_rid());
	}
	return true;
}

void HudDevice::composeMergeReport()
{
	// what decides the composed image is each decal's place, size, textures, colour and whether its alpha passes the alpha test
	const std::vector<SelectionDecal> &list = hud->selectionDecals();
	const SelectionDecalSettings &ds = hud->selectionDecalSettings();
	std::string key = ds.useSimpleMergeDecals ? "s" : "m";
	for (const SelectionDecal &sd : list)
	{
		if (sd.style & 0x1000u)
		{
			char buf[160];
			const unsigned alpha = ds.useSimpleMergeDecals ? (sd.color >> 24) : ((sd.color >> 24) >= 0x60 ? 1u : 0u);
			snprintf(buf, sizeof(buf), "|%.1f,%.1f,%.1f,%.1f,%06x,%u,", sd.position.x, sd.position.y, sd.position.z, sd.size, sd.color & 0xFFFFFFu, alpha);
			key += buf + sd.texture + "," + sd.texture2;
		}
	}
	if (key == mergeKey)
	{
		return;
	}
	mergeKey = key;
	++mergeComposes;
	std::string error;
	// at most 4 texels a world unit (a 50-unit decal is 200 texels; RotWK draws it at the screen's resolution)
	if (!ComposeMergeDecals(list, [this](const std::string &f) { return decalImageFor(f); }, ds, merged, &error, 2048, 4.0f))
	{
		merged = MergedSelectionDecals();
	}
}

const HudDevice::DecalTextures *HudDevice::unlitDecalFor(const std::string &file, uint32_t rgb)
{
	char key[16];
	snprintf(key, sizeof(key), "|%06x", rgb);
	auto it = unlitDecals.find(file + key);
	if (it != unlitDecals.end())
	{
		return &it->second;
	}
	const DecalImage img = decalImageFor(file);
	if (img.width <= 0 || !img.rgba)
	{
		return nullptr;
	}
	const size_t n = (size_t)img.width * (size_t)img.height * 4;
	PackedByteArray albedo;
	albedo.resize((int64_t)n);
	uint8_t *pa = albedo.ptrw();
	const unsigned col[3] = { (rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu };
	for (size_t i = 0; i < n; i += 4)
	{
		for (int c = 0; c < 3; ++c)
		{
			pa[i + c] = (uint8_t)((img.rgba[i + c] * col[c] + 127) / 255u);
		}
		pa[i + 3] = img.rgba[i + 3];
	}
	DecalTextures t;
	t.albedo = ImageTexture::create_from_image(Image::create_from_data(img.width, img.height, false, Image::FORMAT_RGBA8, albedo));
	return &unlitDecals.emplace(file + key, t).first->second;
}

DecalImage HudDevice::decalImageFor(const std::string &file)
{
	auto it = decalPixels.find(file);
	if (it == decalPixels.end())
	{
		PackedByteArray px;
		Vector2i size(0, 0);
		Ref<Image> img = imageFor(file);
		if (img.is_valid())
		{
			img = img->duplicate();
			if (img->is_compressed())
			{
				img->decompress();
			}
			img->clear_mipmaps();
			img->convert(Image::FORMAT_RGBA8);
			px = img->get_data();
			size = Vector2i(img->get_width(), img->get_height());
		}
		it = decalPixels.emplace(file, px).first;
		decalPixelSizes[file] = size;
	}
	const Vector2i size = decalPixelSizes[file];
	DecalImage out;
	if (size.x > 0 && it->second.size() == (int64_t)size.x * size.y * 4)
	{
		out.width = size.x;
		out.height = size.y;
		out.rgba = it->second.ptr();
	}
	return out;
}

void HudDevice::freeSelectionDecals()
{
	if (Node3D *root = decalRootId ? Object::cast_to<Node3D>(ObjectDB::get_instance(godot::ObjectID(decalRootId))) : nullptr)
	{
		root->queue_free();
	}
	decalRootId = 0;
	decalIds.clear();
	if (radiusCursorBound)
	{
		if (RenderingServer *rs = RenderingServer::get_singleton())
		{
			rs->global_shader_parameter_set("radius_cursor_transform", Vector4());
			rs->global_shader_parameter_set("radius_cursor_texture", RID());
		}
		radiusCursorBound = false;
	}
	radiusDecalDrawn = false;
	mergeKey.clear();
	// lane SELPERF-1: the terrain shader draws no merge decals once the HUD is gone
	if (selMode > 0 || selData.is_valid() || selAtlas.is_valid())
	{
		if (RenderingServer *rs = RenderingServer::get_singleton())
		{
			rs->global_shader_parameter_set("sel_grid2", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
			rs->global_shader_parameter_set("sel_data", RID()); // the textures go with the HUD
			rs->global_shader_parameter_set("sel_atlas", RID());
		}
	}
	selData.unref();
	selAtlas.unref();
	selAtlasPack.clear();
	selMode = -1;
	selVersion = ~0ull;
}

InGameHudNode::InGameHudNode() : d(std::make_unique<HudDevice>()) {}

InGameHudNode::~InGameHudNode()
{
	d->freeSelectionDecals(); // lane UI-4
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
	d->ghost.reset(); // lane BUILD-5 r2: its drawables use the game's draw services: before the HUD and the game
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
	// BFME1 ships only Data\\INI\\CommandMap.ini (no root CommandMap.ini override; docs/BFME1.md)
	const bool bfme1Keys = d->metaEnv->fileSystem && isBfme1Data(*d->metaEnv->fileSystem);
	if (!d->meta.load(*d->metaEnv, "Data\\INI\\CommandMap.ini", &error) || (!bfme1Keys && !d->meta.load(*d->metaEnv, "CommandMap.ini", &error)))
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
	SelectionDecalSettings decalSettings; // lane UI-4
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
			d->camAnim.stop(); // lane CAMANIM-1: a new map: its own animation list (built at the first request)
			d->camAnimListBuilt = false;
			d->cam->setViewport((int)window.x, (int)window.y);
			const LoadedMap &lm = game.map();
			d->cam->startMap(game.logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, Coord3D{ s.x, -s.z, 0.0f });
			// SMOOTH-1 (S-810): the lock / follow target from the presented snapshot (the camera runs every render frame, also while the worker runs)
			LiveGame *g = &game;
			d->cam->setObjectSource([g](::ObjectID id, Coord3D &pos) { return g->presentedObjectPosition(id, pos); });
			d->edgeScroll = (bool)options.get("edge_scroll", false);
			// lane OPTS-2: Options.ini ScrollFactor over GameData's RMB scroll factors (RW 0x641E0C; the text, or null for the default)
			{
				OptionPreferences scroll;
				const Variant sf = options.get("scroll_factor_option", Variant());
				if (sf.get_type() != Variant::NIL)
				{
					scroll.set("ScrollFactor", std::string(String(sf).utf8().get_data()));
				}
				d->cam->setScrollFactor(scroll.getScrollFactor(gd.keyboardDefaultScrollSpeedFactor));
			}
			d->cam->setFreeCamera((bool)options.get("free_camera", false)); // lane PLAY-1: the owner's option (not retail)
			d->cam->setZoomOutFactor((float)(double)options.get("zoom_out_factor", 1.0)); // lane ZOOM-1: the owner's Max zoom-out (not retail; 1 = retail)
			d->cam->setWidescreenView((bool)options.get("widescreen_wider", false)); // lane UI-6: the Widescreen view (Wider: not retail; false = retail)
		}
		// lane HUD-5: GameData's ShowObjectHealth / VeterancyPipDrawObjectFilter for the drawable decorations
		d->veterancyFilter = gd.veterancyPipFilter;
		d->haveVeterancyFilter = gd.haveVeterancyPipFilter;
		showObjectHealth = gd.showObjectHealth;
		// lane UI-4: the selection marker's GameData fields (SelectionDecals.h)
		decalSettings.showSelectedUnitMarker = gd.showSelectedUnitMarker;
		decalSettings.useSimpleHordeDecals = gd.useSimpleHordeDecals;
		decalSettings.useSimpleMergeDecals = gd.useSimpleMergeDecals;
		decalSettings.opacityOfSimpleMergeDecals = gd.opacityOfSimpleMergeDecals;
		// RW 0x601C62: the static LOD level overrides ShowSelectedUnitMarker / UseSimpleMergeDecals with its DecalLOD. INFERENCE (S-2522): the level is the
		// one the renderer draws at, UltraHigh (no level is applied to the renderer, S-2481); GameLOD.ini comes with the shell
		const GameLODManager *lod = shellPlayer && shellPlayer->shell_object() ? shellPlayer->shell_object()->environment().gameLOD : nullptr;
		const int ultraHigh = GameLODManager::findLevel("UltraHigh");
		if (!lod || !lod->hasLevel(ultraHigh))
		{
			d->drawNotes.insert("[S-2522] selection markers: no GameLOD.ini StaticGameLOD UltraHigh (no shell): GameData's ShowSelectedUnitMarker / UseSimpleMergeDecals");
		}
		else
		{
			ApplyDecalLOD(decalSettings, lod->level(ultraHigh).decalLOD);
		}
		if (!errors.is_empty())
		{
			result["ok"] = false;
			result["errors"] = errors;
			return result;
		}
	}
	d->applyCamera(window, 1.0);
	// RW 0x48CEA3 updates the view immediately, even when an Apt command runs after this node's camera frame.
	d->cam->setJumpHandler([this]() {
		const Vector2 size = get_viewport()->get_visible_rect().get_size();
		d->applyCamera(size, 1.0);
	});
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
		if (!text->table.loadRetail(afs, &textError))
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
	cfg.endGameTimer = [world]() { return world->hud_end_game_timer(); };
	d->hud = std::make_unique<InGameHud>(cfg);
	d->hud->input().attachCamera(*d->cam);
	d->hud->setWindowSize((int)window.x, (int)window.y);
	d->hud->input().commandTranslator().setUseAlternateMouse((bool)options.get("alternate_mouse", false));
	// lane BUILD-5 r2: the ghost's step runs after the displayed camera (InGameHudNode::_process), drawn by the HUD's own renderer
	d->hud->input().setPlacementDeferred(true);
	d->ghost = std::make_unique<PlacementGhostRenderer>(world, fs);
	d->ghost->setFeedbackSources(&d->images, gameText); // community PR #37: the farm placement decal and label
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
	if (shellPlayer)
	{
		d->godotMouse = shellPlayer->godot_mouse();
		if (!d->godotMouse)
		{
			errors.push_back("shell player has no native cursor adapter");
		}
	}
	else
	{
		d->godotMouse = std::make_shared<GodotMouse>();
		d->godotMouse->loadCursorTable(afs, fs->install_root());
	}
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
	{
		// lane HUD-6: the bubbles' images and the help's fonts
		HudDevice *dev = d.get();
		d->hud6 = std::make_unique<Hud6Draw>(*d->hud, d->images, [dev](const std::string &file) { return dev->textureFor(file); }, d->player);
		d->hud->setMappedImages(&d->images);
		d->hud->setFontMetrics(d->hud6->metrics());
	}
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
	d->hud->setSelectionDecalSettings(decalSettings); // lane UI-4
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
	{
		// the money texts (GameClient/FloatingText.h)
		Array texts;
		for (const FloatingTextList::Record &r : d->hud->floatingText().records())
		{
			Dictionary t;
			t["text"] = String::utf8(r.text.c_str());
			t["x"] = r.pos.x;
			t["y"] = r.pos.y;
			t["z"] = r.pos.z;
			t["alpha"] = (int64_t)(r.color >> 24);
			t["color"] = (int64_t)(r.color & 0xFFFFFFu);
			t["frames"] = (int64_t)r.frameCount;
			texts.push_back(t);
		}
		s["floating_texts"] = texts;
		s["floating_texts_added"] = (int64_t)d->hud->floatingText().added();
	}
	const InGameUI &ui = d->hud->input().ui();
	s["cursor"] = toGodot(ui.cursor());
	Dictionary rally;
	rally["drawable"] = (int64_t)d->hud->rallyPointDrawableId();
	if (d->world && d->world->hud_game())
	{
		if (const Drawable *marker = d->world->hud_game()->drawables().find(d->hud->rallyPointDrawableId()))
		{
			rally["template"] = toGodot(marker->getTemplate()->getName());
			rally["x"] = marker->getPosition()->x;
			rally["y"] = marker->getPosition()->y;
			rally["z"] = marker->getPosition()->z;
			rally["angle"] = marker->getOrientation();
			rally["color"] = (int64_t)marker->getHouseColor();
			rally["animated"] = marker->hasAnimatedEntry();
		}
	}
	s["rally_marker"] = rally;
	s["screenshot"] = d->lastScreenshot; // lane INPUT-1: the last TAKE_SCREENSHOT file
	s["spell_store_open"] = d->hud->spellStore() != nullptr; // lane INPUT-1: the SPELL_STORE key
	// lane UI-4: the Palantir portrait: the image the selection asks for (PalantirCommandUI) and the one the CommandUI.Portrait clip drew
	s["portrait"] = toGodot(d->hud->palantir() ? d->hud->palantir()->portraitShown() : std::string());
	s["portrait_drawn"] = toGodot(d->portraitDrawn);
	s["portrait_draws"] = (int64_t)d->portraitDraws;
	// lane AUTOCAST-1: the auto casts so far (AutoAbilityBehavior::stats, process-wide) and the right presses that toggled one
	s["auto_ability_casts"] = (int64_t)AutoAbilityBehavior::stats().casts;
	s["auto_ability_toggles"] = (int64_t)d->hud->controlBar().autoAbilityToggles();
	s["auto_ability_overlay_errors"] = (int64_t)(d->hud->palantir() ? d->hud->palantir()->autoAbilityOverlayErrors() : 0u); // r2: the proof fails on any
	{
		// lane UI-4: the selection markers of the last update and how many decals the device showed
		Array decals;
		for (const SelectionDecal &sd : d->hud->selectionDecals())
		{
			Dictionary e;
			e["object"] = (int64_t)sd.object;
			e["size"] = sd.size;
			e["texture"] = toGodot(sd.texture);
			e["color"] = (int64_t)sd.color;
			e["x"] = sd.position.x;
			e["y"] = sd.position.y;
			decals.push_back(e);
		}
		s["selection_decals"] = decals;
		s["selection_decals_drawn"] = (int64_t)d->decalsDrawn;
		d->composeMergeReport(); // lane SELPERF-1: composed here only (the terrain shader draws the merge decals)
		Dictionary m;
		m["decals"] = (int64_t)d->merged.decals;
		m["width"] = (int64_t)d->merged.width;
		m["height"] = (int64_t)d->merged.height;
		m["simple"] = d->hud->selectionDecalSettings().useSimpleMergeDecals;
		m["composes"] = d->mergeComposes; // lane SELPERF-1
		m["gpu_decals"] = (int64_t)d->selGrid.decals;
		m["gpu_cells"] = (int64_t)d->selGrid.cellsX * d->selGrid.cellsY;
		m["gpu_max_per_cell"] = (int64_t)d->selGrid.maxPerCell;
		m["gpu_uploads"] = d->selUploads;
		m["gpu_mode"] = (int64_t)d->selMode;
		m["show"] = d->hud->selectionDecalSettings().showSelectedUnitMarker;
		int64_t opaque = 0;
		for (size_t i = 3; i < d->merged.rgba.size(); i += 4)
		{
			opaque += d->merged.rgba[i] != 0 ? 1 : 0;
		}
		m["opaque_texels"] = opaque;
		Array errs;
		for (const std::string &e : d->drawErrors)
		{
			if (e.find("selection decal") != std::string::npos)
			{
				errs.push_back(toGodot(e));
			}
		}
		m["errors"] = errs;
		s["selection_merge"] = m;
	}
	// lane UI-4: the hero bar (InGameHeroSelect): its movie path, shown, the select-all button and the slots as the movie was last told
	if (InGameHeroSelect *hs = d->hud->heroSelect())
	{
		Dictionary h;
		h["attached"] = hs->attached();
		h["name"] = toGodot(hs->name());
		h["shown"] = hs->shown();
		h["select_all"] = hs->selectAllShown();
		Array slots;
		for (int i = 0; i < InGameHeroSelect::kSlots; ++i)
		{
			const InGameHeroSelect::Slot &sl = hs->slot(i);
			Dictionary e;
			e["hero"] = (int64_t)sl.hero;
			e["builder"] = sl.builder;
			e["image"] = toGodot(sl.image);
			e["rank"] = (int64_t)sl.rank;
			e["rank_progress"] = (int64_t)sl.rankProgress;
			e["health"] = (int64_t)sl.health;
			e["selected"] = sl.selected;
			slots.push_back(e);
		}
		h["slots"] = slots;
		Array heroes;
		for (const InGameHeroSelect::Hero &e : hs->heroes())
		{
			heroes.push_back((int64_t)e.id);
		}
		h["heroes"] = heroes;
		Array builders;
		for (const InGameHeroSelect::Builder &e : hs->builders())
		{
			builders.push_back((int64_t)e.id);
		}
		h["builders"] = builders;
		s["hero_select"] = h;
	}
	s["frame_selection_changed"] = (int64_t)ui.getFrameSelectionChanged();
	{
		// lane PLAY-3: the drag selection box as the last render frame drew it (RW 0x48ECF4), the last box drawn and the frames it was drawn in
		Dictionary box;
		box["shown"] = d->dragBoxShown;
		box["selecting"] = ui.isSelecting();
		box["rect"] = Rect2(d->dragBox.x, d->dragBox.y, d->dragBox.w, d->dragBox.h);
		box["width"] = d->dragBox.width;
		box["color"] = (int64_t)d->dragBox.color;
		box["frames"] = (int64_t)d->dragBoxFrames;
		s["drag_box"] = box;
	}
	{
		// lane INPUT-1: the keyboard path, for a diagnosis (raw keys reaching the MetaEventTranslator, the meta messages they made, the modifier state)
		const MetaEventTranslator &mt = d->hud->input().metaTranslator();
		Dictionary keys;
		keys["raw"] = (int64_t)mt.rawKeys();
		keys["metas"] = (int64_t)mt.metasMade();
		keys["last_meta"] = toGodot(mt.lastMeta());
		keys["key_state"] = d->keyState;
		keys["key_events"] = (int64_t)d->keyEvents;
		keys["menu_key_events"] = d->menuKeyEvents; // lane OPTS-6
		keys["modal"] = d->modal;
		keys["modal_entries"] = (int64_t)d->modalEntries;
		keys["deferred"] = (int64_t)d->deferredInput;
		keys["pending"] = (int64_t)d->pendingInput.size(); // lane BACKLOG3
		keys["dropped"] = (int64_t)d->droppedInput;
		keys["focus_resets"] = (int64_t)d->focusResets;
		keys["input_enabled"] = ui.getInputEnabled();
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
	Array displayedMessages;
	for (const auto &entry : d->hud->playerMessages().entries())
	{
		Dictionary message;
		message["text"] = String::utf16(reinterpret_cast<const char16_t *>(entry.text.data()), (int64_t)entry.text.size());
		message["color"] = (int64_t)entry.color;
		displayedMessages.push_back(message);
	}
	s["player_messages"] = displayedMessages;
	{
		ControlBar &cb = d->hud->controlBar();
		Dictionary bar;
		bar["source"] = (int64_t)cb.sourceObject();
		bar["command_set"] = toGodot(cb.commandSetName());
		bar["foreign_view"] = cb.foreignView(); // lane OWNERFIX-3: another player's set, every button disabled (RW 0x94397F)
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

bool InGameHudNode::select_object_with_message(int64_t id)
{
	if (!select_object(id))
	{
		return false;
	}
	ClientMessage &m = d->hud->input().stream().append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
	m.appendBoolean(true);
	m.appendObjectID((::ObjectID)id);
	return true;
}

// Formation integration test hook: keep presentation and authoritative selection together.
int64_t InGameHudNode::select_objects_with_message(const PackedInt64Array &ids)
{
 const auto count=select_objects(ids);
 if(!d->hud)return count;
 ClientMessage &message=d->hud->input().stream().append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
 message.appendBoolean(true);
 for(auto id: d->hud->input().ui().selected())message.appendObjectID(id);
 return count;
}

// lane SELPERF-1: the selection benchmark (play1_player.gd selperf) selects exactly N objects; how many ended up selected
int64_t InGameHudNode::select_objects(const PackedInt64Array &ids)
{
	waitLogic(d.get());
	if (!d->hud)
	{
		return 0;
	}
	UtilityFunctions::print("GAME TEST HOOK: select ", ids.size(), " objects");
	InGameUI &ui = d->hud->input().ui();
	ui.deselectAll(true);
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		ui.selectObject((::ObjectID)ids[i]);
	}
	return (int64_t)ui.selected().size();
}

void InGameHudNode::set_icons_covered(bool covered)
{
	// lane SAVE-UI1: the health bars and the other drawable decorations (HUD-5's icon layer, a canvas item of this node drawn after the shell's movies)
	// are under a menu over the game, as retail draws them with the world before the window manager (ZH W3DDisplay::draw)
	d->iconsCovered = covered;
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
	Array texts, images, notes, barObjects, fills;
	for (const IconUIOp &op : d->hud->iconOps())
	{
		if (op.kind == IconUIOp::OPEN_RECT && op.color == 0x7F000000u) // the outer frame of a health bar
		{
			++bars;
			barObjects.push_back((int64_t)op.object); // lane ZOOM-1: whose bars
		}
		if (op.kind == IconUIOp::FILL_RECT) // lane OWNERFIX-1: the filled rectangles (the bars' colours) where they are drawn, for the pixel tests
		{
			Dictionary f;
			f["rect"] = Rect2(op.x, op.y, op.w, op.h);
			f["color"] = Color(((op.color >> 16) & 0xFF) / 255.0f, ((op.color >> 8) & 0xFF) / 255.0f, (op.color & 0xFF) / 255.0f, ((op.color >> 24) & 0xFF) / 255.0f);
			f["object"] = (int64_t)op.object;
			fills.push_back(f);
		}
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
	r["bar_objects"] = barObjects;
	r["fills"] = fills;
	r["ui_scale"] = d->uiScale; // the fills are the ops before the OPTS-2 scale (1.0: as drawn)
	r["texts"] = texts;
	r["images"] = images;
	r["drawn"] = (int64_t)d->iconOpsDrawn;
	r["covered"] = d->iconsCovered; // lane SAVE-UI1
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
	r["palantir_level"] = p->level(); // lane HUDREDRAW-1: the Palantir's Apt level (the shell player's get_stats levels)
	r["shown"] = p->spellBookShown();
	Array slots;
	for (int i = 0; i < AptPalantir::kSpellSlots; ++i)
	{
		slots.push_back(String(AptPalantir::spellStateName(p->spellSlotState(i))));
	}
	r["slots"] = slots;
	Array slotButtons; // lane SPELL-3: the spell book command set index of each slot (a harness names the slot's power with it)
	for (int i = 0; i < AptPalantir::kSpellSlots; ++i)
	{
		slotButtons.push_back((int64_t)p->spellSlotButton(i));
	}
	r["slot_buttons"] = slotButtons;
	const InGameSpellBookModel &bar = d->hud->spellBar();
	r["targeting"] = bar.targeting();
	r["radius"] = bar.targetButton() ? bar.targetButton()->radius : 0.0f;
	{
		// lane SPELL-4: the aiming marker (InGameHud::radiusCursor) and what the device drew of it
		const RadiusCursorDecal &rc = d->hud->radiusCursor().decal();
		Dictionary c;
		c["active"] = d->hud->radiusCursor().active();
		c["type"] = toGodot(rc.type);
		c["visible"] = rc.visible;
		c["texture"] = toGodot(rc.texture);
		c["style"] = (int64_t)rc.style;
		c["size"] = rc.size;
		c["x"] = rc.position.x;
		c["y"] = rc.position.y;
		c["z"] = rc.position.z;
		c["rgb"] = (int64_t)rc.rgb;
		c["opacity"] = (int64_t)rc.opacity;
		c["valid"] = rc.valid;
		c["drawn"] = d->radiusDecalDrawn;
		c["error"] = toGodot(d->radiusDecalError);
		const InGameUI &ui = d->hud->input().ui();
		c["pointer"] = toGodot(ui.cursor());
		r["radius_cursor"] = c;
	}
	{
		// lane OWNERFIX-2: the resource claim feedback and what the device drew of it
		const TerrainResourceFeedback &fb = d->hud->resourceFeedback();
		const ResourcePlacementFeedback &rp = fb.placement();
		Dictionary f;
		f["placement_active"] = rp.active;
		f["template"] = toGodot(rp.templateName);
		f["fraction"] = rp.fraction;
		f["percent"] = (int64_t)rp.percent;
		String text;
		for (char16_t ch : rp.text)
		{
			text += String::chr((char32_t)ch);
		}
		f["text"] = text;
		f["color"] = (int64_t)rp.color;
		f["decal_size"] = rp.decalSize;
		f["decal_x"] = rp.decalCenter.x;
		f["decal_y"] = rp.decalCenter.y;
		int64_t visible = 0;
		for (std::uint8_t a : rp.maskAlpha)
		{
			visible += a > 0 ? 1 : 0;
		}
		f["mask_vertices"] = (int64_t)rp.maskAlpha.size();
		f["mask_visible"] = visible;
		f["place_decal_drawn"] = d->placeDecalDrawn;
		f["text_drawn"] = d->placeTextDrawn;
		f["text_rect"] = d->placeTextRect;
		Array claims;
		for (const ResourceClaimDecal &cd : fb.claimedDecals())
		{
			Dictionary e;
			e["id"] = (int64_t)cd.id;
			e["x"] = cd.position.x;
			e["y"] = cd.position.y;
			e["size"] = cd.size;
			e["color"] = (int64_t)cd.color;
			e["opacity"] = (int64_t)cd.opacity;
			claims.push_back(e);
		}
		f["claim_decals"] = claims;
		f["claim_decals_drawn"] = (int64_t)d->claimDecalsDrawn;
		f["error"] = toGodot(d->resourceDecalError);
		r["resource_feedback"] = f;
	}
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
		store["spell_clicks"] = (int64_t)st->commandCalls("AptSpellStore::OnBttnSpell"); // lane SPELL-3: the movie's OnBttnSpell calls
		store["spell_arg"] = toGodot(st->lastSpellArgument());
		// lane SPELL-3: the movies' trace lines since the store opened (SetSpellButtonState, the connector set-up), the last 80
		Array traces;
		const std::vector<std::string> &all = d->hud->windows().traces();
		for (size_t i = all.size() > 80 ? all.size() - 80 : 0; i < all.size(); ++i)
		{
			traces.push_back(toGodot(all[i]));
		}
		store["traces"] = traces;
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

void InGameHudNode::cancel_spell_store()
{
	if (d->hud)
	{
		const auto worldContext = d->hud->enterContext();
		d->hud->cancelSpellStore();
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

Array InGameHudNode::take_chat_requests()
{
	Array out;
	if (d->hud)
	{
		for (bool everyone : d->hud->takeChatRequests())
		{
			out.push_back(everyone);
		}
	}
	return out;
}

String InGameHudNode::chat_text(const String &label) const
{
	std::u16string text;
	if (d->hud && d->hud->gameText() && d->hud->gameText()->fetch(std::string(label.utf8().get_data()), text))
	{
		return chatU16ToGodot(text);
	}
	return String();
}

String InGameHudNode::chat_format(int64_t mode, const String &text) const
{
	// RW 0x8ECF3E: UnicodeString::format(TheGameText->fetch(label), text); a missing label leaves the text as it is (reported by the caller's chat_text)
	std::u16string format;
	const char *label = InGameChat::formatLabel((InGameChatMode)std::max<int64_t>(0, std::min<int64_t>(4, mode)));
	if (!d->hud || !d->hud->gameText() || !d->hud->gameText()->fetch(label, format))
	{
		return text;
	}
	return chatU16ToGodot(InGameChat::formatLine(format, chatGodotToU16(text)));
}

void InGameHudNode::add_chat_line(const String &text, int64_t rgb)
{
	if (!d->hud)
	{
		return;
	}
	const std::u16string u = chatGodotToU16(text);
	if (rgb >= 0)
	{
		const std::uint32_t c = (std::uint32_t)rgb & 0xFFFFFFu;
		d->hud->addMessageText(u, &c);
		d->hud->chat().addHistory(u, c | 0xFF000000u, d->hud->messageLines().lines()[0].timestamp);
	}
	else
	{
		d->hud->addMessageText(u, nullptr);
		d->hud->chat().addHistory(u, d->hud->messageLines().lines()[0].argb, d->hud->messageLines().lines()[0].timestamp);
	}
}

void InGameHudNode::set_chat_history_shown(bool shown)
{
	d->chatHistoryShown = shown;
}

void InGameHudNode::set_network_game(bool on)
{
	if (d->hud)
	{
		d->hud->input().ui().setNetworkGame(on);
	}
}

Dictionary InGameHudNode::beacon_state() const
{
	Dictionary r;
	if (!d->hud)
	{
		return r;
	}
	const InGameHud::BeaconStats &b = d->hud->beaconStats();
	r["placed"] = (int64_t)b.placed;
	r["too_many"] = (int64_t)b.tooMany;
	r["failed"] = (int64_t)b.failed;
	r["hidden"] = (int64_t)b.hidden;
	r["pulses"] = (int64_t)b.pulses;
	Array ids, hidden;
	for (::ObjectID id : d->hud->beacons())
	{
		ids.push_back((int64_t)id);
	}
	for (::ObjectID id : d->hud->input().ui().clientHidden())
	{
		hidden.push_back((int64_t)id);
	}
	r["beacons"] = ids;
	r["client_hidden"] = hidden;
	const CommandButton *gc = d->hud->input().ui().getGUICommand();
	r["gui_command"] = gc ? toGodot(gc->m_name) : String();
	r["network_game"] = d->hud->input().ui().networkGame();
	PackedStringArray stops;
	for (const std::string &l : BeaconCommands::stopLines())
	{
		stops.push_back(toGodot(l));
	}
	r["stops"] = stops;
	return r;
}

Dictionary InGameHudNode::message_lines_state() const
{
	Dictionary r;
	r["ok"] = d->hud != nullptr;
	if (!d->hud)
	{
		return r;
	}
	Array lines;
	for (int i = InGameUIMessages::kMaxLines - 1; i >= 0; --i)
	{
		const InGameUIMessages::Line &l = d->hud->messageLines().lines()[(size_t)i];
		if (l.used)
		{
			Dictionary x;
			x["text"] = chatU16ToGodot(l.text);
			x["argb"] = (int64_t)l.argb;
			x["frame"] = (int64_t)l.timestamp;
			lines.push_back(x);
		}
	}
	r["lines"] = lines; // oldest first, as drawn
	Array history;
	for (const InGameChat::HistoryLine &h : d->hud->chat().history())
	{
		history.push_back(chatU16ToGodot(h.text));
	}
	r["history"] = history;
	r["history_shown"] = d->chatHistoryShown;
	r["drawn"] = d->messageLinesDrawn;
	r["rect"] = d->messageLinesRect;
	r["gui_messages_shown"] = (int64_t)d->hud->guiMessagesShown();
	PackedStringArray missing;
	for (const std::string &m : d->hud->missingMessageLabels())
	{
		missing.push_back(toGodot(m));
	}
	r["missing_labels"] = missing;
	const InGameUIMessageSettings &ms = d->hud->messageLines().settings();
	r["font"] = toGodot(ms.font);
	r["point_size"] = ms.pointSize;
	r["settings_loaded"] = ms.loaded;
	PackedStringArray stops;
	for (const std::string &l : InGameChat::stopLines())
	{
		stops.push_back(toGodot(l));
	}
	r["stops"] = stops;
	return r;
}

Dictionary InGameHudNode::get_hud6_state() const
{
	waitLogic(d.get());
	return d->hud6 ? d->hud6->state() : Dictionary();
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
		if (d->godotMouse)
		{
			for (const std::string &e : d->godotMouse->cursorErrors())
			{
				ce.push_back(toGodot(e));
			}
		}
		r["cursor_errors"] = ce;
		const Dictionary cursor = d->godotMouse ? d->godotMouse->getCursorImage(false) : Dictionary();
		r["cursor"] = cursor.get("name", String());
		r["cursor_direction"] = cursor.get("direction", 0);
		r["cursor_hidden"] = cursor.get("hidden", false);
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
	syncModal(); // lane OPTS-6: before the queued input is replayed and before the camera's frame
	if (!d->holdInput && !d->pendingInput.empty() && d->world && d->world->hud_game() && d->world->hud_game()->logicHeld())
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
	// A HUD without the shell owns this service queue; a shared shell drains its own. RW 0x815772: PlaySound queues a 2D UI event.
	std::vector<std::string> sounds;
	sounds.swap(d->services.sounds);
	for (const std::string &name : sounds)
	{
		AudioLog::Scope sound("HUD movie");
		AudioApi::playUiSound(name);
	}
	const uint64_t ta = Time::get_singleton()->get_ticks_usec();
	d->drawIconOps(get_canvas_item()); // lane HUD-5
	const uint64_t tb = Time::get_singleton()->get_ticks_usec();
	d->drawSelectionDecals();          // lane UI-4
	d->drawRadiusCursor();             // lane SPELL-4
	d->drawResourceFeedback();         // lane OWNERFIX-2
	d->drawMessageLines();             // lane CHAT-1
	const uint64_t tc = Time::get_singleton()->get_ticks_usec();
	if (d->hud6)
	{
		d->hud6->drawRadial(get_canvas_item()); // lane HUD-6
		d->hud6->drawMessages(get_canvas_item());
		if (d->fs.is_valid() && d->fs->is_bfme1())
		{
			const Vector2 size = get_viewport_rect().size;
			const float width = std::min(420.0f, (float)size.x * 0.4f);
			const Rect2 rect(size.x - width - 24.0f, std::max(24.0f, (float)size.y - 370.0f), width, 300.0f);
			if (d->hud->helpBox().isShown())
				RenderingServer::get_singleton()->canvas_item_add_rect(get_canvas_item(), rect.grow(10), Color(0.06f, 0.05f, 0.03f, 0.95f));
			d->hud6->drawHelp(rect, get_canvas_item());
		}

	}
	const uint64_t t1 = Time::get_singleton()->get_ticks_usec();
	d->hudMs = (double)(ta - t0) / 1000.0;
	d->iconsMs = (double)(tb - ta) / 1000.0;
	d->decalsMs = (double)(tc - tb) / 1000.0;
	d->radialMs = (double)(t1 - tc) / 1000.0;
	// the camera runs 30 client frames a second (retail FramesPerSecondLimit 30, S-456) and is drawn interpolated between two of them
	if (LookAtTranslator *la = d->hud->input().lookAt())
	{
		DisplayServer *ds = DisplayServer::get_singleton();
		const DisplayServer::WindowMode wm = ds ? ds->window_get_mode() : DisplayServer::WINDOW_MODE_WINDOWED;
		const bool retail = d->edgeScroll || wm == DisplayServer::WINDOW_MODE_FULLSCREEN || wm == DisplayServer::WINDOW_MODE_EXCLUSIVE_FULLSCREEN;
		la->setEdgeScrollEnabled(d->edgeScrollMode == 1 ? true : (d->edgeScrollMode == 2 ? false : retail)); // lane OPTS-2: the option
	}
	// lane FX-4: the effects' ViewShake calls shake the camera (W3DView::shake RW 0x486D86); the next client frame's update moves the view (RW 0x48C0E8)
	if (d->cam && d->world)
	{
		if (LiveFX *fx = d->world->live_fx())
		{
			for (const LiveFX::ViewShake &v : fx->takeViewShakes())
			{
				d->cam->shake(v.epicenter, v.type, v.angle);
			}
		}
	}
	const double step = 1.0 / 30.0;
	d->clientClock += delta;
	int guard = 0;
	while (d->clientClock >= step && guard < 8)
	{
		d->hud->input().cameraFrame((unsigned)Time::get_singleton()->get_ticks_msec(), d->world && d->world->is_paused());
		d->camAnim.clientFrame(); // lane CAMANIM-1: the view update's mode-4 step (RW 0x48AA25), one client frame
		d->clientClock -= step;
		++guard;
	}
	if (guard >= 8)
	{
		d->clientClock = 0.0; // a stall: no catching up
	}
	d->applyCamera(window, d->clientClock / step);
	// lane BUILD-5 r2: the placement update (RW 0x6A2AE5) projects the pointer through the camera of the frame drawn, then the ghost's drawables are drawn
	// (behind the same logic gate as the HUD's update: a frame the logic still runs keeps last frame's ghost)
	// lane BUILD-5 r3: every render frame (review r2 #2): the pose follows the live pointer through the displayed camera also while the logic worker runs; the
	// builder lookup, the legality and new drawables wait for an idle frame (they read the live logic)
	if (d->ghost && d->world && d->world->hud_game())
	{
		const bool idle = d->world->hud_game()->logicHeld(); // RENDERWAIT-1: the client window (or an idle worker)
		if (d->livePointerSeen)
		{
			d->hud->input().placeTranslator().setLivePointer({ (int)d->livePointer.x, (int)d->livePointer.y });
		}
		const uint64_t tp = Time::get_singleton()->get_ticks_usec();
		d->hud->input().placementUpdate(idle);
		const uint64_t tg = Time::get_singleton()->get_ticks_usec();
		d->ghost->sync(*d->world->hud_game(), d->hud->input().ui(), delta, PlaceEventTranslator::ghostOptions(), idle);
		d->placeMs = (double)(tg - tp) / 1000.0; // lane FOGFX-1 (FB-0055): the placement's legality and the ghost, apart
		d->ghostMs = (double)(Time::get_singleton()->get_ticks_usec() - tg) / 1000.0;
	}
	const uint64_t t2 = Time::get_singleton()->get_ticks_usec();
	if (d->player && !d->ownsPlayer)
	{
		d->godotMouse = d->player->godot_mouse();
	}
	{
		// lane BUILD-5: while scrolling the cursor is InGameUI's Scroll whatever is under the pointer (createMouseoverHint's window test RW 0x69F637 is skipped
		// while scrolling); its direction follows the scroll amount
		const InGameUI &ui = d->hud->input().ui();
		const bool overGui = !ui.isScrolling() && d->hud->isOverGui((int)d->lastPointer.x, (int)d->lastPointer.y);
		// lane OWNERFIX-1: DISABLE_INPUT's Mouse::setVisibility(false) (RW 0x5EDD0F): no pointer over the world; a menu over the game (Esc's quit menu, which
		// still opens: CMSG_META_OPTIONS) shows the arrow. INFERENCE (S-4964): retail's pointer over a menu opened during a cutscene was not read
		const bool hidden = d->hud->scriptMouseHidden() && !overGui;
		if (d->godotMouse)
		{
			d->godotMouse->updateCursor(delta, hidden ? std::string() : (overGui ? std::string(MouseCursorName::Arrow) : ui.cursor()), ui.scrollAmountX(), ui.scrollAmountY(), ui.isScrolling());
		}
	}
	// lane ANTICHEAT-1 (owner 2026-10-21): twice a second, the player's view (the camera's look-at point, the ground under the pointer) for the relay's
	// log of an online game (never sent to another player, never simulation input; GameWorld::net_submit_view decides whether a game takes it). The
	// pointer's ground point is read like every pick, only while the logic is held (else this sample is skipped)
	d->viewClock += delta;
	if (d->viewClock >= 0.5 && d->cam && d->world && d->world->hud_game())
	{
		d->viewClock = 0.0;
		LiveGame *game = d->world->hud_game();
		Coord3D ground{ 0.0f, 0.0f, 0.0f };
		const bool overWorld = game->logicHeld() && !d->hud->isOverGui((int)d->lastPointer.x, (int)d->lastPointer.y) &&
			d->shown.screenToTerrain({ (int)d->lastPointer.x, (int)d->lastPointer.y }, game->logic(), ground);
		d->world->net_submit_view(d->cam->position().x, d->cam->position().y, ground.x, ground.y, overWorld);
	}
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
	t["hud_logic_ms"] = d->hudMs;      // lane SELPERF-1: hud_update_ms's parts
	t["hud_icons_ms"] = d->iconsMs;
	t["hud_decals_ms"] = d->decalsMs;
	t["hud_radial_ms"] = d->radialMs;
	t["hud_place_ms"] = d->placeMs; // lane FOGFX-1: hud_camera_ms's parts: the placement update and the ghost's sync
	t["hud_ghost_ms"] = d->ghostMs;
	t["hud_ghost_icons_ms"] = d->ghost ? d->ghost->lastIconsMs() : 0.0;
	t["hud_ghost_draw_ms"] = d->ghost ? d->ghost->lastDrawMs() : 0.0;
	if (d->ghost)
	{
		const char *names[5] = { "hud_ghost_inst_ms", "hud_ghost_update_ms", "hud_ghost_tint_ms", "hud_ghost_bib_ms", "hud_ghost_shadow_ms" };
		for (int k = 0; k < 5; ++k)
		{
			t[names[k]] = d->ghost->lastDrawParts()[k];
		}
	}
	if (d->hud)
	{
		// lane SELPERF-1: InGameHud::update's parts
		const InGameHud::UpdateParts &p = d->hud->updateParts();
		t["hud_input_ms"] = p.input;
		t["hud_bar_ms"] = p.bar;
		t["hud_windows_ms"] = p.windows;
		t["hud_palantir_ms"] = p.palantir;
		t["hud_heroes_ms"] = p.heroes;
		t["hud_iconui_ms"] = p.icons;
		t["hud_seldecals_ms"] = p.decals;
		t["hud_radar_ms"] = p.radar;
	}
	t["icon_ops"] = (int64_t)d->iconOpsDrawn; // the drawable decorations drawn (health bars of the selection, ...)
	t["sel_uploads"] = d->selUploads; // the merge decal grids uploaded so far
	t["sel_decals"] = (int64_t)(d->hud ? d->hud->selectionDecals().size() : 0); // this frame's selection markers, those the terrain shader draws, its grid
	t["sel_merge_decals"] = (int64_t)d->selGrid.decals;
	t["sel_mode"] = (int64_t)d->selMode;
	t["sel_cells"] = (int64_t)d->selGrid.cellsX * d->selGrid.cellsY;
	t["sel_max_per_cell"] = (int64_t)d->selGrid.maxPerCell;
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
		if (!ui.moveHintDrawn(i)) // RW 0x48EDED's test
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
	// lane BUILD-5: the rotation anchor (W3DInGameUI::drawPlaceAngle RW 0x48EAD8 draws W_Arrow on the ghost while anchored) and the client frame (the code 9 pulse)
	p["anchored"] = ui.isPlacementAnchored();
	p["client_frame"] = (int64_t)ui.clientFrame();
	return p;
}

String InGameHudNode::image_mode_shader(const String &mode)
{
	return String(aptImageModeShader(parseAptImageMode(mode.utf8().get_data())).c_str());
}

Dictionary InGameHudNode::get_placement_ghost_state() const
{
	return d->ghost ? d->ghost->state() : Dictionary();
}

Dictionary InGameHudNode::get_cursor_image() const
{
	return d->godotMouse ? d->godotMouse->getCursorImage() : Dictionary();
}

PackedStringArray InGameHudNode::take_misc_sounds()
{
	PackedStringArray out;
	if (!d->hud)
	{
		return out;
	}
	for (const std::string &f : d->hud->input().ui().takeMiscSounds())
	{
		out.push_back(toGodot(f));
	}
	return out;
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
				// lane BACKLOG2: a button without a template (the camps' Command_UnpackCamp, CASTLE_UNPACK) is found by its own name
				if (b.button && ((b.button->getThingTemplate() && b.button->getThingTemplate()->getName() == want) || b.button->m_name == want))
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
			e["production_count"] = b.productionCount; // lane OWNERFIX-3: the count the window shows (RW 0x729D9A)
			e["auto_ability"] = b.autoAbility; // lane AUTOCAST-1: the logic's auto button is this one
			e["glow"] = d->hud->palantir() && d->hud->palantir()->autoAbilityOverlayShown(b.slot, arc); // the content's SetAutoAbilityOverlayState("_show") went out
			// r2: the casts of the button's AutoAbilityBehavior (its power's, else the first: the glow's module), the last one's power and target
			::Object *source = d->hud->controlBar().sourceObject() != INVALID_ID && d->world && d->world->hud_game() ? d->world->hud_game()->logic().findObjectByID(d->hud->controlBar().sourceObject()) : nullptr;
			const AutoAbilityBehavior *aa = source ? AutoAbilityBehavior::forButton(*source, *b.button) : nullptr;
			e["auto_casts"] = (int64_t)(aa ? aa->castRecord().casts : 0ull);
			e["auto_cast_power"] = aa ? toGodot(aa->castRecord().power) : String();
			e["auto_cast_target"] = (int64_t)(aa ? aa->castRecord().target : INVALID_ID);
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
		d->livePointer = p;
		d->livePointerSeen = true;
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
	if (!d->modal && button >= 1 && button <= 3)
	{
		d->heldButtons = down ? (d->heldButtons | (1u << button)) : (d->heldButtons & ~(1u << button)); // lane OPTS-6
	}
	d->hud->mouseButton(b, down, (int)p.x, (int)p.y, d->keyState, (int)Time::get_singleton()->get_ticks_msec(), double_click);
}

void InGameHudNode::syncModal()
{
	// lane OPTS-6 (the owner / Sol: hold Right, open the pause menu, release Right under it: the camera kept moving): when a menu becomes the input's top
	// the world's held input is cancelled: the buttons it saw down are let go (the release the menu would otherwise swallow), the modifiers released
	// (paired with their presses), no key counts as down and no scroll / rotate / zoom goes on (HudInput::cancelHeld; retail ends a held scroll when the
	// input is disabled, RW 0x83B4A5). While it is up the world sees no key and no pointer (InGameHud::setModal). INFERENCE (S-5032): retail's pause
	// menu does not cancel it the same way (its keys are the movie's own); ZH's Keyboard::resetKeys on a lost focus is the precedent
	const bool modal = d->player && !d->ownsPlayer && d->player->modal_menu();
	if (!d->ready || modal == d->modal)
	{
		return;
	}
	if (modal)
	{
		for (int button = 1; button <= 3; ++button)
		{
			if (d->heldButtons & (1u << button))
			{
				inject_mouse_button(button, false, d->lastPointer, false);
			}
		}
		d->heldButtons = 0;
		for (const ModifierTracker::Transition &t : d->mods.sync(false, false, false))
		{
			keyEvent(t.key, false, 0);
		}
		++d->modalEntries;
	}
	d->modal = modal;
	d->hud->setModal(modal);
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

// lane BACKLOG3 (S-454): a key held when the window loses the focus never sends its release here. ZH's Keyboard::resetKeys runs then; the LookAt translator's
// tick (RW 0x83BAF8) drops the scroll / rotate / zoom keys the keyboard no longer has down, so the camera stops. Client state only (HudInput's key table).
// Review r1 (Sol): input that waited for the logic worker (pendingInput) is replayed after this reset, so a press queued before the focus went would
// set its key again with no release to follow: the queued PRESSES (keys and mouse buttons) are dropped here. Queued releases and pointer moves stay: they
// only end what the HUD already holds
void InGameHudNode::_notification(int what)
{
	if ((what == NOTIFICATION_WM_WINDOW_FOCUS_OUT || what == NOTIFICATION_APPLICATION_FOCUS_OUT) && d && d->ready && d->hud)
	{
		std::vector<Ref<InputEvent>> kept;
		for (const Ref<InputEvent> &e : d->pendingInput)
		{
			const InputEventKey *k = Object::cast_to<InputEventKey>(e.ptr());
			const InputEventMouseButton *b = Object::cast_to<InputEventMouseButton>(e.ptr());
			if ((k && k->is_pressed()) || (b && b->is_pressed()))
			{
				++d->droppedInput;
				continue;
			}
			kept.push_back(e);
		}
		d->pendingInput.swap(kept);
		d->hud->input().resetKeys();
		++d->focusResets;
	}
}

// lane BACKLOG3 (QA, review r1): _input queues every event as it does while the logic worker runs a frame, until released (the focus test's deferred presses)
void InGameHudNode::hold_input(bool on)
{
	d->holdInput = on;
}

// lane BACKLOG3 (QA): InGameUI's input flag, as the scripts' DISABLE_INPUT / ENABLE_INPUT set it in retail (InGameUI + 0x810, RW 0x69B6A8)
void InGameHudNode::set_broadcast_clean(bool on)
{
	if (d->hud && (!on || (d->world && d->world->hud_game() && d->world->hud_game()->canObserve())))
	{
		d->hud->setBroadcastClean(on);
	}
}

bool InGameHudNode::input_enabled() const
{
	return d->ready && d->hud && d->hud->input().ui().getInputEnabled();
}

void InGameHudNode::set_input_enabled(bool on)
{
	if (d->ready && d->hud)
	{
		d->hud->input().ui().setInputEnabled(on);
	}
}

// lane OWNERFIX-1: the map scripts' DISABLE_INPUT / ENABLE_INPUT (RW 0x7BC586 / 0x7BC5E9): the input flag, the selection, the pointer
void InGameHudNode::script_input(bool enable)
{
	if (d->ready && d->hud)
	{
		d->hud->scriptInput(enable);
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
	if (const InputEventMouse *m = Object::cast_to<InputEventMouse>(event.ptr()))
	{
		d->livePointerSeen = true;
		d->livePointer = m->get_position(); // lane BUILD-5 r3: presentation only (the placement ghost's pose); the event itself may still wait
	}
	LiveGame *game = d->world ? d->world->hud_game() : nullptr;
	if (d->holdInput || (game && (!game->logicHeld() || !d->pendingInput.empty())))
	{
		d->pendingInput.push_back(event);
		++d->deferredInput;
		return;
	}
	handleInput(event);
}

void InGameHudNode::handleInput(const Ref<InputEvent> &event)
{
	syncModal(); // lane OPTS-6
	if (LookAtTranslator *la = d->hud->input().lookAt())
	{
		la->setTime((unsigned)Time::get_singleton()->get_ticks_msec());
	}
	if (Object::cast_to<InputEventMouse>(event.ptr()) && d->player && !d->ownsPlayer)
	{
		d->player->menu_pointer_moved(); // lane OPTS-6: the mouse takes over from the keyboard focus
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
		// lane OPTS-6: while a menu has the input over the game (the quit menu, Options, the save / load screen, the tribute screen) its keys are the
		// menu's (the navigation, the movie, its gadgets: AptMenuPlayer::menu_input), never the game's hot keys or camera keys. Esc stays the game
		// scene's (game.gd closes the menu on top)
		// lane CHAT-1: while the chat window is open its keys are the window's and its entry's (AptMenuPlayer::menu_input), Esc included; the pointer
		// stays the world's (the window takes no mouse in retail)
		if (!d->modal && d->player && !d->ownsPlayer && d->player->ingame_chat_has_keys())
		{
			++d->menuKeyEvents;
			d->player->menu_input(event);
			get_viewport()->set_input_as_handled();
			return;
		}
		if (d->modal)
		{
			if (k->get_keycode() == Key::KEY_ESCAPE)
			{
				// lane SAVE-UI1: the top screen's own Escape (SaveLoad's RW 0x816627) takes it; else game.gd's (the quit menu's toggle, Options' pop)
				if (!k->is_echo() && d->player->menu_escape(k->is_pressed()))
				{
					++d->menuKeyEvents;
					get_viewport()->set_input_as_handled();
				}
			}
			else
			{
				++d->menuKeyEvents;
				d->player->menu_input(event);
			}
			return;
		}
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

void InGameHudNode::camera_reset(const Vector2 &sage_xy)
{
	// lane CAMPUI-1: client state, no wait for the logic worker (camera_look_at). RESET_CAMERA's TacticalCamera::resetCamera at the point: the map's
	// heights and the default zoom / pitch (RW 0x48D26B)
	if (d->cam)
	{
		const Coord3D at{ sage_xy.x, sage_xy.y, 0.0f };
		d->cam->resetCamera(&at);
		d->cam->snapInterpolation();
	}
}

bool InGameHudNode::camera_animation_request(const String &action, const String &name)
{
	// lane CAMANIM-1: client state (the map's records are immutable after the load), no wait for the logic worker
	const std::string a = action.utf8().get_data();
	if (a != "MOVE_CAMERA_BY_ANIMATION" && !CameraAnimationPlayback::cancelsAnimation(a))
	{
		return false;
	}
	if (!d->camAnimListBuilt)
	{
		LiveGame *game = d->world ? d->world->hud_game() : nullptr;
		if (!game)
		{
			UtilityFunctions::printerr("CAMANIM ", action, ": no game (the map's camera animations are unknown)");
			return false;
		}
		std::vector<std::string> errors;
		d->camAnimList.build(game->map().chunks.cameraAnimations, errors);
		d->camAnimListBuilt = true;
		for (const std::string &e : errors)
		{
			UtilityFunctions::printerr("CAMANIM ", String::utf8(e.c_str()));
			d->drawNotes.insert("[CAMANIM-1] " + e);
		}
	}
	const std::string n = name.utf8().get_data();
	const bool changed = d->camAnim.applyRequest(d->camAnimList, a, n);
	if (a == "MOVE_CAMERA_BY_ANIMATION")
	{
		if (changed)
		{
			UtilityFunctions::print("CAMANIM start '", name, "' (", (int64_t)d->camAnimList.find(n)->numFrames(), " frames)");
		}
		else
		{
			// RW 0x7BBB9D: no animation of that name, the view is not called
			UtilityFunctions::print("CAMANIM '", name, "' is not in the map's CameraAnimationList (", (int64_t)d->camAnimList.size(), " animations): no camera change");
		}
	}
	else if (changed)
	{
		UtilityFunctions::print("CAMANIM ", action, " ends the camera animation");
	}
	return changed;
}

Dictionary InGameHudNode::camera_animation_state() const
{
	Dictionary r;
	r["active"] = d->camAnim.active();
	r["name"] = String::utf8(d->camAnim.name().c_str());
	r["time"] = d->camAnim.time();
	r["animations"] = (int64_t)d->camAnimList.size();
	return r;
}

void InGameHudNode::set_objectives_screen_open(bool open)
{
	if (d->ready && d->hud)
	{
		d->hud->setObjectivesScreenOpen(open);
	}
}

void InGameHudNode::flash_palantir_button(int slot, int seconds)
{
	if (d->ready && d->hud)
	{
		d->hud->flashPalantirButton(slot, seconds);
	}
}

bool InGameHudNode::is_objectives_flag_flashing() const
{
	return d->ready && d->hud && d->hud->palantirObjectivesFlashing();
}

void InGameHudNode::camera_set_zoom_limited(bool on)
{
	if (d->cam)
	{
		d->cam->setZoomLimited(on); // lane CAMPUI-1: RW 0x443C81 (the letterbox lifts the zoom limit while it is on)
	}
}

void InGameHudNode::camera_set_clip_depth_multiplier(double m)
{
	if (d->cam)
	{
		d->cam->setClipDepthMultiplier((float)m); // RW 0x7CAFA5 case 0x14E: GlobalData + 0x950, read by setCameraTransform (RW 0x48B7B1)
	}
}

void InGameHudNode::set_edge_scroll(bool on)
{
	d->edgeScroll = on;
}

Dictionary InGameHudNode::set_view_options(const Dictionary &o)
{
	// lane OPTS-2: the options screen's camera and interface settings, applied live (client only)
	Dictionary r;
	if (o.has("scroll_factor_option") && d->cam)
	{
		OptionPreferences scroll;
		const Variant sf = o["scroll_factor_option"];
		if (sf.get_type() != Variant::NIL)
		{
			scroll.set("ScrollFactor", std::string(String(sf).utf8().get_data()));
		}
		d->cam->setScrollFactor(scroll.getScrollFactor(d->cam->gameData().keyboardDefaultScrollSpeedFactor));
	}
	if (o.has("edge_scroll_mode"))
	{
		const String m = o["edge_scroll_mode"];
		d->edgeScrollMode = m == "always" ? 1 : (m == "off" ? 2 : 0);
	}
	if (d->hud)
	{
		if (LookAtTranslator *la = d->hud->input().lookAt())
		{
			if (o.has("invert_drag_scroll"))
			{
				la->setInvertDragScroll((bool)o["invert_drag_scroll"]);
			}
			if (o.has("rotate_speed_scale"))
			{
				la->setRotateSpeedScale((float)(double)o["rotate_speed_scale"]);
			}
		}
		if (o.has("alternate_mouse"))
		{
			d->hud->input().commandTranslator().setUseAlternateMouse((bool)o["alternate_mouse"]);
		}
		IconUISettings is = d->hud->iconSettings();
		if (o.has("all_health_bars"))
		{
			is.allHealthBars = (bool)o["all_health_bars"];
		}
		if (o.has("health_bar_mode"))
		{
			const String m = o["health_bar_mode"];
			is.healthBarMode = m == "always" ? IconUISettings::HEALTH_BARS_ALWAYS : (m == "damaged" ? IconUISettings::HEALTH_BARS_DAMAGED : IconUISettings::HEALTH_BARS_RETAIL);
		}
		d->hud->setIconUISettings(is);
	}
	if (o.has("ui_scale"))
	{
		const double k = o["ui_scale"];
		d->uiScale = k >= 0.5 && k <= 4.0 ? (float)k : 1.0f;
	}
	if (o.has("free_camera") && d->cam)
	{
		d->cam->setFreeCamera((bool)o["free_camera"]);
	}
	if (o.has("zoom_out_factor") && d->cam)
	{
		d->cam->setZoomOutFactor((float)(double)o["zoom_out_factor"]); // lane ZOOM-1
	}
	if (o.has("widescreen_wider") && d->cam)
	{
		d->cam->setWidescreenView((bool)o["widescreen_wider"]); // lane UI-6
	}
	r["ok"] = d->hud != nullptr;
	r["edge_scroll_mode"] = d->edgeScrollMode;
	r["scroll_factor"] = d->cam ? d->cam->gameData().horizontalScrollSpeedFactor : 0.0f;
	r["ui_scale"] = d->uiScale;
	return r;
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
	out["zoom_out_factor"] = c.zoomOutFactor(); // lane ZOOM-1
	out["widescreen_wider"] = c.widescreenView(); // lane UI-6: the Widescreen view
	out["horizontal_fov"] = c.viewHorizontalFov(); // lane UI-6: the drawn view's angles (radians)
	out["vertical_fov"] = c.verticalFov();
	out["fog_shift"] = c.fogShift();
	out["far"] = c.farPlane();
	out["eye"] = toGodotAxes(c.eye());
	out["target"] = toGodotAxes(c.target());
	out["displayed_target"] = toGodotAxes(d->shown.PinholeView::position());
	out["displayed_eye"] = toGodotAxes(d->shown.eye());
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

Vector2 InGameHudNode::ping_point(const Vector2 &pixel) const
{
	// the ping wheel (not retail): the map point a ping placed at this window pixel marks. Over the minimap: the point the radar shows there (the mapping of
	// a radar click, InGameHud::radarClick); over any other part of the HUD (the Palantir, the control bar, the radial bubbles: InGameHud::isOverGui): none,
	// as the terrain behind the HUD is not what the player points at; elsewhere the terrain under the pixel (pixel_to_world). (-1e9, -1e9) for none
	waitLogic(d.get());
	if (!d->hud)
	{
		return Vector2(-1e9f, -1e9f);
	}
	bool hasPoint = false;
	float wx = 0.0f, wy = 0.0f;
	if (d->hud->radarPoint((int)pixel.x, (int)pixel.y, hasPoint, wx, wy))
	{
		return hasPoint ? Vector2(wx, wy) : Vector2(-1e9f, -1e9f);
	}
	if (d->hud->isOverGui((int)pixel.x, (int)pixel.y))
	{
		return Vector2(-1e9f, -1e9f);
	}
	return pixel_to_world(pixel);
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

// lane SELECT-1: the scripted player's click targets (scripts/select1_player.gd). Client state only: nothing reaches the simulation.
PackedVector2Array InGameHudNode::pick_box_pixels(int64_t id) const
{
	waitLogic(d.get());
	PackedVector2Array out;
	if (!d->hud || !d->world || !d->world->hud_game())
	{
		return out;
	}
	const HudContext &ctx = d->hud->input().context();
	const Drawable *drawable = d->world->hud_game()->drawables().findByObject((::ObjectID)id);
	if (!drawable)
	{
		return out;
	}
	for (const DrawEntry &e : drawable->entries())
	{
		const RenderObjPrototype *proto = e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.moduleHidden && !e.conditionHidden ? e.draw->currentModel() : nullptr;
		if (!proto)
		{
			continue;
		}
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			if (sub.Type != RenderSubObject::SUB_BOX || !sub.Box || sub.Aggregate || (sub.Box->Attributes & 1u) == 0)
			{
				continue;
			}
			const HTreeClass *tree = proto->Tree ? proto->Tree : &proto->DefaultTree;
			HTreePose pose;
			tree->Base_Pose(Matrix3D(), pose);
			const Matrix3D pivot = sub.BoneIndex >= 0 && sub.BoneIndex < (int)pose.Transform.size() ? pose.Transform[(size_t)sub.BoneIndex] : Matrix3D();
			const float c[3] = { sub.Box->Center.X, sub.Box->Center.Y, sub.Box->Center.Z };
			const float ex[3] = { sub.Box->Extent.X, sub.Box->Extent.Y, sub.Box->Extent.Z };
			const float *basis = drawable->getBasis();
			const float scale = drawable->getInstanceScale();
			const Coord3D at = drawable->entryPosition(e);
			for (int k = 0; k < 7; ++k)
			{
				float p[3] = { c[0], c[1], c[2] };
				if (k > 0)
				{
					p[(k - 1) / 2] += ((k - 1) % 2 == 0 ? -0.8f : 0.8f) * ex[(k - 1) / 2];
				}
				float m[3];
				for (int r = 0; r < 3; ++r)
				{
					m[r] = (pivot.Row[r][0] * p[0] + pivot.Row[r][1] * p[1] + pivot.Row[r][2] * p[2] + pivot.Row[r][3]) * scale;
				}
				const Coord3D w{ at.x + basis[0] * m[0] + basis[1] * m[1] + basis[2] * m[2], at.y + basis[3] * m[0] + basis[4] * m[1] + basis[5] * m[2],
					at.z + basis[6] * m[0] + basis[7] * m[1] + basis[8] * m[2] };
				ICoord2D px;
				if (ctx.view.worldToScreen(w, px))
				{
					out.push_back(Vector2((float)px.x, (float)px.y));
				}
			}
			return out; // RW 0x59C680: the first one answers
		}
	}
	return out;
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

bool InGameHudNode::create_radar_event(const Vector2 &world_xy, int type, bool jump_to)
{
	// lane RADAR-1 (QA): an event the way a script's radar event action makes one (createEvent, 4 s); false without a radar. `jump_to` (the ping wheel):
	// also TheEva's event position, as the script action adds it after createEvent (RW 0x7BC221 -> RW 0x5DD1F3, Radar::noteEvaPosition), so Space
	// (VIEW_LAST_RADAR_EVENT) looks at it. Client state only (the radar), as the script's event is
	if (!d->hud || !d->hud->radar().ready())
	{
		return false;
	}
	const Coord3D at{ (float)world_xy.x, (float)world_xy.y, 0.0f };
	d->hud->radar().createEvent(at, type, 4.0f);
	if (jump_to)
	{
		d->hud->radar().noteEvaPosition(at);
	}
	return true;
}

Dictionary InGameHudNode::get_radar_state() const
{
	// lane HUD-7: what the radar's draw callbacks drew last (the picture, the clip, the view box) beside the rectangle the clicks map through (radarSquare)
	waitLogic(d.get());
	Dictionary out;
	out["ready"] = d->hud && d->hud->radar().ready();
	out["draws"] = (int64_t)d->radarDraws;
	out["view_box_draws"] = (int64_t)d->radarViewBoxDraws;
	out["picture"] = d->radarPicture;
	out["clip"] = d->radarClip;
	PackedVector2Array box;
	for (int i = 0; i < 4; ++i)
	{
		box.push_back(Vector2(d->radarViewBox[i * 2], d->radarViewBox[i * 2 + 1]));
	}
	out["view_box"] = box;
	float sq[4];
	out["square"] = d->hud && d->hud->radarSquare(sq) ? Rect2(sq[0], sq[1], sq[2] - sq[0], sq[3] - sq[1]) : Rect2();
	if (d->hud && d->hud->radar().ready())
	{
		out["extent"] = Vector2(d->hud->radar().extentWidth(), d->hud->radar().extentHeight());
	}
	// lane DEVPORT-8: the live radar events (Radar::radarEvent: type, world position, created / dies in client frames, whether its first draw played)
	Array events;
	if (d->hud)
	{
		for (int i = 0; i < Radar::kMaxEvents; ++i)
		{
			const Radar::RadarEventRecord &e = d->hud->radar().radarEvent(i);
			if (!e.active)
			{
				continue;
			}
			Dictionary ev;
			ev["type"] = e.type;
			ev["world"] = Vector2(e.worldLoc.x, e.worldLoc.y);
			ev["created"] = (int64_t)e.createFrame;
			ev["dies"] = (int64_t)e.dieFrame;
			ev["sound_played"] = e.soundPlayed;
			events.push_back(ev);
		}
	}
	out["events"] = events;
	return out;
}

Dictionary InGameHudNode::radar_probe(const Vector2 &world) const
{
	// lane HUD-7: the overlay cell an object at SAGE (x, y) is drawn in (Radar::renderObjects: (int)(x * 128 / width), (int)(y * 128 / height)), the strongest
	// overlay alpha of its 3 x 3 cells (a 2 x 2 dot is drawn left and below of its sample), and that cell's centre in the drawn picture (row 0 of the overlay at
	// the bottom)
	waitLogic(d.get());
	Dictionary out;
	out["ok"] = false;
	if (!d->hud || !d->hud->radar().ready() || d->radarPicture.size.x <= 0.0f)
	{
		return out;
	}
	const Radar &radar = d->hud->radar();
	const int n = Radar::kCells;
	const int cx = (int)(128.0f / radar.extentWidth() * world.x), cy = (int)(world.y * (128.0f / radar.extentHeight()));
	int alpha = 0;
	for (int y = cy - 1; y <= cy + 1; ++y)
	{
		for (int x = cx - 1; x <= cx + 1; ++x)
		{
			if (x >= 0 && y >= 0 && x < n && y < n && d->radarOverlayTexels.size() == (size_t)n * (size_t)n)
			{
				alpha = std::max(alpha, (int)(d->radarOverlayTexels[(size_t)y * (size_t)n + (size_t)x] >> 24));
			}
		}
	}
	const Rect2 &p = d->radarPicture;
	out["ok"] = true;
	out["cell"] = Vector2i(cx, cy);
	out["alpha"] = alpha;
	out["drawn_pixel"] = Vector2(p.position.x + ((float)cx + 0.5f) * p.size.x / (float)n, p.position.y + ((float)(n - 1 - cy) + 0.5f) * p.size.y / (float)n);
	return out;
}

void InGameHudNode::camera_set_angle_pitch(float angle, float pitch)
{
	// lane HUD-7 (a test hook, logged by the caller): TacticalCamera::setAngle (RW 0x48CA7F) and setPitch (RW 0x65E8D0), as the rotate keys and the tilt do
	if (d->cam)
	{
		d->cam->setAngle(angle);
		d->cam->setPitch(pitch);
		d->cam->snapInterpolation();
	}
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

// lane CHEAT-1 ------------------------------------------------------------------------------------------------------------------------------------------

void InGameHudNode::add_player_message(const String &text)
{
	if (d->hud)
	{
		const Char16String u = text.utf16();
		d->hud->addPlayerMessageText(std::u16string(reinterpret_cast<const char16_t *>(u.get_data()), (std::size_t)u.length()));
	}
}

Dictionary InGameHudNode::get_player_message_layout() const
{
	Dictionary out;
	if (d->hud)
	{
		const PlayerMessageSettings &s = d->hud->playerMessages().settings;
		out["x"] = s.position.x;
		out["y"] = s.position.y;
		out["point_size"] = s.pointSize;
	}
	return out;
}

} // namespace godot
