// OpenBFME. GPL-3.0.
// See GodotAptPlayer.h.

#include "GodotDevice/GodotAptPlayer.h"
#include "GodotDevice/GodotVideoStream.h"
#include "GameClient/CameraSettings.h"
#include "GameNetwork/Transport.h"
#include "GodotDevice/GodotPackedTexture.h"
#include "GodotDevice/GodotAptView3D.h"
#include "GameClient/OptionPreferences.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/FontSubstitution.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptScreens/AptTimeLine.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/UserDataFolder.h"
#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GodotDevice/GodotGameStart.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>

namespace godot
{

namespace
{

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string compact(const std::string &s)
{
	std::string out;
	for (char c : s)
	{
		if (c != ' ')
		{
			out += (char)std::tolower((unsigned char)c);
		}
	}
	return out;
}

// lane UI-2: a texture by UV corners in the port's top-row-first textures (a pair with lo > hi mirrors that axis): a quad with explicit UVs
void addTextureUV(RenderingServer *rs, RID item, const Rect2 &dest, const Ref<Texture2D> &texture, const float uvLo[2], const float uvHi[2], const Color &colour)
{
	PackedVector2Array points, uvs;
	PackedColorArray colours;
	const Vector2 p0 = dest.position, p1 = dest.position + dest.size;
	points.push_back(Vector2(p0.x, p0.y));
	points.push_back(Vector2(p1.x, p0.y));
	points.push_back(Vector2(p1.x, p1.y));
	points.push_back(Vector2(p0.x, p1.y));
	uvs.push_back(Vector2(uvLo[0], uvLo[1]));
	uvs.push_back(Vector2(uvHi[0], uvLo[1]));
	uvs.push_back(Vector2(uvHi[0], uvHi[1]));
	uvs.push_back(Vector2(uvLo[0], uvHi[1]));
	colours.push_back(colour);
	rs->canvas_item_add_polygon(item, points, colours, uvs, texture->get_rid());
}

// loadPackedTexture (lane UI-2) is in GodotPackedTexture.cpp (lane HUD-4: the HUD's radar loads ScrollShroud through it too)

static_assert(sizeof(Vector2) == 8, "PackedVector2Array is copied as float pairs");
static_assert(sizeof(Color) == 16, "PackedColorArray is copied as float quads");

// Godot key -> Windows virtual-key code (the codes the player's Key natives report; S-108 leaves the retail ids unverified).
int virtualKeyOf(Key key)
{
	const int k = (int)key;
	if ((k >= (int)Key::KEY_A && k <= (int)Key::KEY_Z) || (k >= (int)Key::KEY_0 && k <= (int)Key::KEY_9))
	{
		return k; // ASCII upper case and digits are the VK codes
	}
	switch (key)
	{
		case Key::KEY_BACKSPACE: return 0x08;
		case Key::KEY_TAB: return 0x09;
		case Key::KEY_ENTER:
		case Key::KEY_KP_ENTER: return 0x0D;
		case Key::KEY_SHIFT: return 0x10;
		case Key::KEY_CTRL: return 0x11;
		case Key::KEY_ALT: return 0x12;
		case Key::KEY_ESCAPE: return 0x1B;
		case Key::KEY_SPACE: return 0x20;
		case Key::KEY_PAGEUP: return 0x21;
		case Key::KEY_PAGEDOWN: return 0x22;
		case Key::KEY_END: return 0x23;
		case Key::KEY_HOME: return 0x24;
		case Key::KEY_LEFT: return 0x25;
		case Key::KEY_UP: return 0x26;
		case Key::KEY_RIGHT: return 0x27;
		case Key::KEY_DOWN: return 0x28;
		case Key::KEY_DELETE: return 0x2E;
		default: break;
	}
	if (k >= (int)Key::KEY_F1 && k <= (int)Key::KEY_F12)
	{
		return 0x70 + (k - (int)Key::KEY_F1);
	}
	return 0;
}

} // namespace

} // namespace godot

// ---------------------------------------------------------------------------------------------------------------------------------
// the host
// ---------------------------------------------------------------------------------------------------------------------------------
class AptGodotHost : public AptHost
{
public:
	struct Event
	{
		std::string kind, a, b;
		std::uint64_t frame = 0;
	};
	std::map<std::string, std::string> externs;
	std::set<std::string> componentMovies; // lower-cased
	std::vector<Event> events;
	std::vector<std::string> scriptErrors;
	std::uint64_t *frame = nullptr;
	std::uint32_t rng = 0x2545F491u;

	void add(const char *kind, const std::string &a, const std::string &b = std::string())
	{
		if (events.size() < 100000)
		{
			events.push_back({ kind, a, b, frame ? *frame : 0 });
		}
	}
	bool isComponentSymbol(const std::string &movie, const std::string &) override
	{
		return componentMovies.count(godot::lowerAscii(movie)) != 0;
	}
	void trace(const std::string &message) override { add("trace", message); }
	void fscommand(const std::string &command, const std::string &argument) override { add("fscommand", command, argument); }
	void loadMovie(const std::string &movieName, const std::string &target) override { add("load", movieName, target); }
	void getURL(const std::string &url, const std::string &target) override { add("url", url, target); }
	AptExternResult getExtern(const std::string &name, std::string &value) override
	{
		auto it = externs.find(name);
		if (it == externs.end())
		{
			add("extern_unknown", name); // a read nobody provides: the interpreter reports it too
			return AptExternResult::NoProvider;
		}
		value = it->second;
		add("extern_get", name, value);
		return AptExternResult::Value;
	}
	bool setExtern(const std::string &name, const std::string &value) override
	{
		add("extern_set", name, value);
		auto it = externs.find(name);
		if (it == externs.end())
		{
			return false;
		}
		it->second = value;
		return true;
	}
	std::uint32_t random() override
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng >> 8;
	}
	void scriptError(const std::string &message) override
	{
		if (scriptErrors.size() < 1000)
		{
			scriptErrors.push_back(message);
		}
		add("script_error", message);
	}
};

namespace godot
{


// ---------------------------------------------------------------------------------------------------------------------------------
// shell mode: the pieces the real shell is built from (lane START-1)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
std::u16string utf8ToU16(const std::string &s)
{
	std::u16string out;
	for (size_t i = 0; i < s.size();)
	{
		const unsigned char c = (unsigned char)s[i];
		std::uint32_t cp = c;
		int extra = 0;
		if (c >= 0xF0)
		{
			cp = c & 0x07;
			extra = 3;
		}
		else if (c >= 0xE0)
		{
			cp = c & 0x0F;
			extra = 2;
		}
		else if (c >= 0xC0)
		{
			cp = c & 0x1F;
			extra = 1;
		}
		++i;
		for (int k = 0; k < extra && i < s.size(); ++k, ++i)
		{
			cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
		}
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back((char16_t)(0xD800 + (cp >> 10)));
			out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
		}
		else
		{
			out.push_back((char16_t)cp);
		}
	}
	return out;
}

std::string u16ToUtf8(const std::u16string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t cp = s[i];
		if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size())
		{
			cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		if (cp < 0x80)
		{
			out.push_back((char)cp);
		}
		else if (cp < 0x800)
		{
			out.push_back((char)(0xC0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else if (cp < 0x10000)
		{
			out.push_back((char)(0xE0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (cp & 0x3F)));
		}
	}
	return out;
}

// The game text the screens read: lotr.str through the table the canvas uses, and a map's own map.str on request (ZH GameText::initMapStringFile).
class ShellGameText : public GameTextSource
{
public:
	ShellGameText(const GameTextTable &table, ArchiveFileSystem &fs) : m_table(table), m_fs(fs) {}
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		std::string text;
		if (!m_table.lookup(label, text))
		{
			return false;
		}
		out = utf8ToU16(text);
		return true;
	}
	bool fetchMapLabel(const std::string &file, const std::string &label, std::u16string &out) const override
	{
		auto it = m_maps.find(file);
		if (it == m_maps.end())
		{
			GameTextTable table;
			std::vector<std::uint8_t> bytes;
			std::string error;
			if (m_fs.readFile(file, bytes, &error))
			{
				table.parse(bytes, &error);
			}
			it = m_maps.emplace(file, std::move(table)).first;
		}
		std::string text;
		if (!it->second.lookup(label, text))
		{
			return false;
		}
		out = utf8ToU16(text);
		return true;
	}

private:
	const GameTextTable &m_table;
	ArchiveFileSystem &m_fs;
	mutable std::map<std::string, GameTextTable> m_maps;
};

// ShellServices of the device: every call is queued for the owner (take_events / signals); nothing is dropped.
class ShellDeviceServices : public ShellServices
{
public:
	struct Event
	{
		std::string kind, a, b;
	};
	std::vector<Event> events;
	std::vector<ShellRequest> requests;
	void playSound(const std::string &name) override { events.push_back({ "sound", name, "" }); }
	void setBackground(const std::string &mode) override { events.push_back({ "background", mode, "" }); }
	void setMouseVisible(bool visible) override { events.push_back({ "mouse_visible", visible ? "1" : "0", "" }); }
	void setCursorTooltip(const std::string &label) override { events.push_back({ "tooltip", label, "" }); }
	void request(const ShellRequest &r) override
	{
		requests.push_back(r);
		events.push_back({ "request", std::to_string((int)r.action), r.argument });
	}
	// lane UI-2: Options.ini entries the device acts on; "SoftParticles" is lane FX-3's project setting openbfme/rendering/soft_particles
	void applyOption(const std::string &key, const std::string &value) override
	{
		events.push_back({ "option", key, value });
		if (key == OptionPreferences::kSoftParticles)
		{
			std::string v = value;
			for (char &c : v)
			{
				c = (char)std::tolower((unsigned char)c);
			}
			ProjectSettings::get_singleton()->set_setting("openbfme/rendering/soft_particles", v == "yes");
		}
	}
};

class ShellNewGameSink : public NewGameSink
{
public:
	std::vector<NewGameMessage> messages;
	std::vector<std::pair<std::u16string, std::u16string>> refusals;
	std::uint32_t seed = 1;
	void queueNewGame(const NewGameMessage &message) override { messages.push_back(message); }
	void startRefused(const std::u16string &title, const std::u16string &text) override { refusals.emplace_back(title, text); }
	std::uint32_t newGameSeed() override { return seed; }
};

const char *shellActionName(ShellAction a)
{
	switch (a)
	{
		case ShellAction::ExitGame: return "ExitGame";
		case ShellAction::LoadGame: return "LoadGame";
		case ShellAction::LoadReplay: return "LoadReplay";
		case ShellAction::Lan: return "Lan";
		case ShellAction::Online: return "Online";
		case ShellAction::BattleSchool: return "BattleSchool";
		case ShellAction::LevelSelect: return "LevelSelect";
		case ShellAction::LoadCampaign: return "LoadCampaign";
		case ShellAction::ContinueCampaign: return "ContinueCampaign";
		case ShellAction::Expansion1Campaign: return "Expansion1Campaign";
		case ShellAction::BonusCampaign: return "BonusCampaign";
		case ShellAction::WarOfTheRing: return "WarOfTheRing";
		case ShellAction::CreateAHero: return "CreateAHero";
		case ShellAction::Tutorial: return "Tutorial";
		case ShellAction::Credits: return "Credits";
		case ShellAction::CreditsExit: return "CreditsExit";
		case ShellAction::StopGameMovie: return "StopGameMovie";
		case ShellAction::ResetResolution: return "ResetResolution";
		case ShellAction::ScoreScreenContinue: return "ScoreScreenContinue";
		case ShellAction::ScoreScreenSaveReplay: return "ScoreScreenSaveReplay";
		case ShellAction::LoadReplayFile: return "LoadReplayFile"; // lane MP-2
		case ShellAction::LanGameStart: return "LanGameStart";
		case ShellAction::QuitMenuReturn: return "QuitMenuReturn";
		case ShellAction::QuitMenuExit: return "QuitMenuExit";
		case ShellAction::QuitMenuRestart: return "QuitMenuRestart";
		case ShellAction::QuitMenuForfeit: return "QuitMenuForfeit";
		case ShellAction::QuitMenuOptions: return "QuitMenuOptions";
		case ShellAction::ToggleQuitMenu: return "ToggleQuitMenu";
		case ShellAction::PalantirObjectives: return "PalantirObjectives"; // lane PLAY-1
		case ShellAction::TributeReturnToGame: return "TributeReturnToGame";
		case ShellAction::CreateAHeroExit: return "CreateAHeroExit";
	}
	return "Unknown";
}

// Godot key -> DirectInput scan code (ZH KeyDefs.h, the codes the gadgets take); 0 when the gadgets have no use for the key.
int dikOf(Key key)
{
	switch (key)
	{
		case Key::KEY_ESCAPE: return ::KEY_ESC;
		case Key::KEY_BACKSPACE: return ::KEY_BACKSPACE;
		case Key::KEY_TAB: return ::KEY_TAB;
		case Key::KEY_ENTER: return ::KEY_ENTER;
		case Key::KEY_KP_ENTER: return ::KEY_KPENTER;
		case Key::KEY_HOME: return ::KEY_HOME;
		case Key::KEY_UP: return ::KEY_UP;
		case Key::KEY_PAGEUP: return ::KEY_PGUP;
		case Key::KEY_LEFT: return ::KEY_LEFT;
		case Key::KEY_RIGHT: return ::KEY_RIGHT;
		case Key::KEY_END: return ::KEY_END;
		case Key::KEY_DOWN: return ::KEY_DOWN;
		case Key::KEY_PAGEDOWN: return ::KEY_PGDN;
		case Key::KEY_DELETE: return ::KEY_DEL;
		default: return 0;
	}
}
} // namespace



// The gadget layer's font geometry (S-176): the device fonts the canvas draws with, measured at the stage's own pixel size.
class ShellFontMetrics : public FontMetricsSource
{
public:
	typedef std::function<Ref<Font>(const std::string &, float, float *)> FontFn;
	explicit ShellFontMetrics(FontFn fn) : m_fn(std::move(fn)) {}
	int fontHeight(const GameFont &font) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, &size);
		return f.is_valid() ? (int)std::ceil(f->get_height((int)std::lround(size))) : font.pointSize + 4;
	}
	int textWidth(const GameFont &font, const UnicodeString &text) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, &size);
		if (f.is_null())
		{
			return (int)text.size() * (font.pointSize * 6 / 10 + 1);
		}
		return (int)std::ceil(f->get_string_size(toGodot(u16ToUtf8(text)), HORIZONTAL_ALIGNMENT_LEFT, -1, (int)std::lround(size)).x);
	}
	int wrappedHeight(const GameFont &font, const UnicodeString &text, int wrapWidth) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, &size);
		if (f.is_null())
		{
			return font.pointSize + 4;
		}
		if (wrapWidth <= 0)
		{
			return fontHeight(font);
		}
		return (int)std::ceil(f->get_multiline_string_size(toGodot(u16ToUtf8(text)), HORIZONTAL_ALIGNMENT_LEFT, (float)wrapWidth, (int)std::lround(size)).y);
	}

private:
	FontFn m_fn;
};

// Everything shell mode owns, in construction order (destroyed in reverse: the Shell and the layer go before the manager).
struct ShellMode
{
	ShellDeviceServices services;
	std::unique_ptr<ShellGameText> text;
	WorldSkirmishSetupSource setup;
	ShellNewGameSink sink;
	LoadScreenInfo loadScreen;
	ScoreScreenData scoreScreen; // lane END-1
	MemorySkirmishProfiles profiles; // lane END-1: the profiles of the shell session (a second visit of Skirmish.apt finds them, as retail's saved profiles)
	std::unique_ptr<AptScreen> guiFX; // lane END-1: GuiFX.apt, loaded for the end screen (not on the shell stack)
	std::unique_ptr<AptDisconnectScreen> disconnect; // lane MP-2: DisconnectScreen.apt while the disconnect screen is shown
	Array disconnectActions;                         // lane MP-2: the buttons pressed, for take_disconnect_actions
	SaveLoadInfo saveLoad;                           // lane MP-2: what SaveLoad.apt lists
	QuitMenuContext quitMenuContext;    // lane END-2: the game QuitMenu.apt is opened over
	std::unique_ptr<AptQuitMenu> quitMenu; // lane END-2: QuitMenu.apt, an in-game overlay (RW 0x75E3B7), not on the shell stack
	ShellEnvironment environment;
	PlayerStatusInfo playerStatus; // lane HUD-5: the players screen's Status rows
	OptionPreferences options; // lane UI-2: the user data folder's Options.ini
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	std::unique_ptr<FontMetricsSource> metrics;
	std::string lastTop;
	std::vector<NewGameMessage> pendingNewGames;
	std::set<std::string> gadgetErrors; // textures a gadget image needed and could not load, unresolved image names
	std::set<std::string> deviceUnverified; // lane UI-2: inferences of the device the report lists (S-1482)
	CreateAHeroHeroList cahHeroes;      // lane CAH-1: the builder's and the lobby's heroes
	CreateAHeroScreenContext cahContext;
	std::mt19937 cahRandom{ 0x43414831u }; // lane CAH-1: the builder's client random (AppearanceRandom, RW 0x6D32E4): never logic state
	~ShellMode()
	{
		disconnect.reset();
		quitMenu.reset();
		guiFX.reset();
		shell.reset();
		layer.reset();
		wm.reset();
	}
};

// ---------------------------------------------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------------------------------------------
struct AptMenuPlayer::Impl
{
	Ref<RetailFileSystem> fs;
	AptView3DViewers view3d; // lane UI-2: the View3D render objects (the end screen's rings, the menu frame)
	std::unique_ptr<AptArchiveFileSource> source;
	AptGodotHost host;
	std::unique_ptr<Apt> apt;
	// lane HUD-1: an Apt player owned by a window manager the device layer runs (the in-game HUD's); null otherwise
	WindowManager *external = nullptr;
	AptNativeHook *nativeHook = nullptr;
	bool inputForwarded = false;
	std::unique_ptr<AptTextureStore> textures;
	GameTextTable strings;
	FontSubstitution fonts;
	std::map<std::string, Ref<ImageTexture>> godotTextures;
	std::map<std::string, Ref<FontFile>> fontByName; // lower-cased family name and file stem
	std::vector<std::string> fontNames;              // for the report
	std::set<std::string> fontFallbacks;
	std::vector<RID> items;
	AptCanvasList list;
	// lane PERF-1 r2: the list the canvas items show (valid until clearCanvas frees them) and, per native placeholder, where its item sits, so a frame
	// whose list is the same op for op redraws only the native components (their pictures follow the game, not the list)
	AptCanvasList drawnList;
	GadgetDrawList gadgets, drawnGadgets; // shell mode: this frame's gadget commands and the ones on screen
	bool drawnValid = false;
	// lane CAMP-2: what the canvas shows behind and under the movies (get_backdrop_state): the shell backdrop image drawCanvas put on screen ("" none) and
	// the visible commands of each level in the last list built (level_drawn; the front-end background's level is Background.apt)
	std::string drawnBackdrop;
	std::map<int, int64_t> levelCommands;
	std::vector<std::string> backgroundPaths; // the front-end background's visible commands (paths, alpha), for the reports
	// lane CAMP-2: the BinkMovie components on screen (the clip's path -> its movie stream), started when first drawn, advanced on the render clock
	struct MovieSlot
	{
		Ref<VP6MovieStream> stream;
		std::string title;
		double elapsedMs = 0.0;
		double durationMs = 0.0;
		double fps = 30.0;
		bool loop = false;
		bool drawn = false;
		std::string error;
	};
	std::map<std::string, MovieSlot> movies;
	uint64_t worldId = 0; // the GameWorld of boot_shell (its get_movie finds a Video's file)
	std::set<std::string> movieErrors;
	struct NativeSlot
	{
		size_t op;   ///< index in drawnList.ops
		size_t item; ///< index in items
		RID parent;
	};
	std::vector<NativeSlot> nativeSlots;
	std::uint64_t unchangedFrames = 0;
	AptStageMapping mapping;
	Vector2 lastWindow;
	bool lastFit = false;
	bool dirty = true;
	double carryMs = 0;
	std::uint64_t frames = 0, steps = 0, rebuilds = 0;
	std::uint64_t stepUs = 0, listUs = 0, canvasUs = 0, submitUs = 0;
	std::uint64_t textureLoads = 0;
	double textureLoadMs = 0;
	std::set<std::string> unverified, errors, missingLabels, fontSubstitutions, placeholders;
	std::unique_ptr<ShellMode> shell;  // shell mode (boot_shell): the Apt player is the window manager's
	Apt *A() const { return external ? &external->apt() : shell ? &shell->wm->apt() : apt.get(); }
	// the font a request (name, stage pixel size) draws with: fontsubstitution.ini, then the corpus fonts, else the engine default (reported)
	Ref<Font> fontFor(const std::string &name, float size, float *drawSize)
	{
		const FontRequestResult r = fonts.resolve(name, size);
		*drawSize = r.size;
		auto fit = fontByName.find(lowerAscii(r.name));
		if (fit == fontByName.end())
		{
			fit = fontByName.find(compact(r.name));
		}
		if (fit != fontByName.end())
		{
			return fit->second;
		}
		fontFallbacks.insert(r.name);
		return ThemeDB::get_singleton()->get_fallback_font();
	}

	// string table, font substitution, the corpus fonts (boot and boot_shell); appends to `errors`
	void loadResources(Array &errorsOut)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!source->readFile("data/lotr.str", bytes, &error) || !strings.parse(bytes, &error))
		{
			errorsOut.push_back(toGodot("data/lotr.str: " + error));
		}
		if (!source->readFile("data/ini/fontsubstitution.ini", bytes, &error) || !fonts.parse(std::string(bytes.begin(), bytes.end()), &error))
		{
			errorsOut.push_back(toGodot("data/ini/fontsubstitution.ini: " + error));
		}
		std::vector<std::string> files;
		for (const char *pattern : { "*.ttf", "*.otf" })
		{
			FilenameList list;
			fs->archive_fs()->getFileListInDirectory(std::string(), std::string(), std::string(pattern), list, true);
			for (const std::string &p : list)
			{
				if (p.find('/') == std::string::npos && p.find('\\') == std::string::npos)
				{
					files.push_back(p);
				}
			}
		}
		std::sort(files.begin(), files.end());
		files.erase(std::unique(files.begin(), files.end()), files.end());
		for (const std::string &file : files)
		{
			if (!source->readFile(file, bytes, &error))
			{
				errorsOut.push_back(toGodot(file + ": " + error));
				continue;
			}
			PackedByteArray data;
			data.resize((int64_t)bytes.size());
			memcpy(data.ptrw(), bytes.data(), bytes.size());
			Ref<FontFile> font;
			font.instantiate();
			font->set_data(data);
			font->get_ascent(16); // forces the face to load, which gives the family name
			const std::string family = toNative(font->get_font_name());
			const std::string stem = lowerAscii(file.substr(0, file.find_last_of('.')));
			if (family.empty())
			{
				errorsOut.push_back(toGodot(file + ": the font has no family name (it is only reachable by its file name)"));
			}
			else
			{
				fontByName[lowerAscii(family)] = font;
			}
			fontByName[stem] = font;
			fontNames.push_back(file + " = " + (family.empty() ? "(no name)" : family));
		}
	}

	void noteList()
	{
		for (const std::string &s : list.unverified)
		{
			unverified.insert(s);
		}
		for (const std::string &s : list.errors)
		{
			if (errors.size() < 500)
			{
				errors.insert(s);
			}
		}
		for (const std::string &s : list.missingLabels)
		{
			missingLabels.insert(s);
		}
		for (const std::string &s : list.fontSubstitutions)
		{
			fontSubstitutions.insert(s);
		}
		for (const AptCanvasOp &op : list.ops)
		{
			if (op.kind == AptCanvasOp::Kind::Placeholder)
			{
				placeholders.insert(op.nativeTag ? "_type=" + op.symbolName + (op.renderObject.empty() ? std::string() : " _RenderObj=" + op.renderObject) + " @ " + op.path : op.symbolMovie + "." + op.symbolName);
			}
		}
	}
};

AptMenuPlayer::AptMenuPlayer() : m(std::make_unique<Impl>()) {}

AptMenuPlayer::~AptMenuPlayer()
{
	// lane CAH-2: the builder's view hook is static; its texture reference must not outlive the player (a quit with the builder up crashed at exit)
	set_create_a_hero_view_texture(Ref<Texture2D>());
	clearCanvas();
}

void AptMenuPlayer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("boot", "fs", "config"), &AptMenuPlayer::boot);
	ClassDB::bind_method(D_METHOD("is_booted"), &AptMenuPlayer::is_booted);
	ClassDB::bind_method(D_METHOD("tick", "delta"), &AptMenuPlayer::tick);
	ClassDB::bind_method(D_METHOD("render", "force"), &AptMenuPlayer::render, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_auto_process", "enabled"), &AptMenuPlayer::set_auto_process);
	ClassDB::bind_method(D_METHOD("get_auto_process"), &AptMenuPlayer::get_auto_process);
	ClassDB::bind_method(D_METHOD("set_fit", "fit"), &AptMenuPlayer::set_fit);
	ClassDB::bind_method(D_METHOD("get_fit"), &AptMenuPlayer::get_fit);
	ClassDB::bind_method(D_METHOD("set_show_placeholders", "show"), &AptMenuPlayer::set_show_placeholders);
	ClassDB::bind_method(D_METHOD("get_show_placeholders"), &AptMenuPlayer::get_show_placeholders);
	ClassDB::bind_method(D_METHOD("load_movie", "level", "movie"), &AptMenuPlayer::load_movie);
	ClassDB::bind_method(D_METHOD("unload_level", "level"), &AptMenuPlayer::unload_level);
	ClassDB::bind_method(D_METHOD("set_level_visible", "level", "visible"), &AptMenuPlayer::set_level_visible);
	ClassDB::bind_method(D_METHOD("has_level", "level"), &AptMenuPlayer::has_level);
	ClassDB::bind_method(D_METHOD("invoke", "level", "function", "args"), &AptMenuPlayer::invoke);
	ClassDB::bind_method(D_METHOD("set_extern_value", "name", "value"), &AptMenuPlayer::set_extern_value);
	ClassDB::bind_method(D_METHOD("dump_tree", "level", "max_depth"), &AptMenuPlayer::dump_tree);
	ClassDB::bind_method(D_METHOD("find_button", "level", "path"), &AptMenuPlayer::find_button);
	ClassDB::bind_method(D_METHOD("instance_info", "level", "path"), &AptMenuPlayer::instance_info);
	ClassDB::bind_method(D_METHOD("window_to_stage", "window_position"), &AptMenuPlayer::window_to_stage);
	ClassDB::bind_method(D_METHOD("stage_to_window", "stage_position"), &AptMenuPlayer::stage_to_window);
	ClassDB::bind_method(D_METHOD("post_mouse_move_stage", "stage"), &AptMenuPlayer::post_mouse_move_stage);
	ClassDB::bind_method(D_METHOD("post_mouse_button", "down"), &AptMenuPlayer::post_mouse_button);
	ClassDB::bind_method(D_METHOD("post_mouse_wheel", "delta"), &AptMenuPlayer::post_mouse_wheel);
	ClassDB::bind_method(D_METHOD("post_key", "vk_code", "down"), &AptMenuPlayer::post_key);
	ClassDB::bind_method(D_METHOD("boot_shell", "fs", "world", "config"), &AptMenuPlayer::boot_shell, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("is_shell_mode"), &AptMenuPlayer::is_shell_mode);
	ClassDB::bind_method(D_METHOD("shell_push", "filename"), &AptMenuPlayer::shell_push);
	ClassDB::bind_method(D_METHOD("shell_pop"), &AptMenuPlayer::shell_pop);
	ClassDB::bind_method(D_METHOD("shell_show_shell_map", "use"), &AptMenuPlayer::shell_show_shell_map); // lane FB7-1
	ClassDB::bind_method(D_METHOD("shell_hide_background"), &AptMenuPlayer::shell_hide_background); // lane FB7-1
	ClassDB::bind_method(D_METHOD("shell_screen_unavailable", "action"), &AptMenuPlayer::shell_screen_unavailable); // lane CAH-2
	ClassDB::bind_method(D_METHOD("get_backdrop_state"), &AptMenuPlayer::get_backdrop_state); // lane CAMP-2
	ClassDB::bind_method(D_METHOD("shell_levels"), &AptMenuPlayer::shell_levels);             // lane CAMP-2
	ClassDB::bind_method(D_METHOD("level_drawn", "level"), &AptMenuPlayer::level_drawn);      // lane CAMP-2
	ClassDB::bind_method(D_METHOD("get_movies"), &AptMenuPlayer::get_movies);                 // lane CAMP-2
	ClassDB::bind_method(D_METHOD("shell_stack"), &AptMenuPlayer::shell_stack);
	ClassDB::bind_method(D_METHOD("get_option", "key"), &AptMenuPlayer::get_option);
	ClassDB::bind_method(D_METHOD("set_player_status", "state"), &AptMenuPlayer::set_player_status); // lane HUD-5
	ClassDB::bind_method(D_METHOD("shell_top_level"), &AptMenuPlayer::shell_top_level);
	ClassDB::bind_method(D_METHOD("shell_invoke", "level", "function", "args"), &AptMenuPlayer::shell_invoke, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("take_new_game"), &AptMenuPlayer::take_new_game);
	ClassDB::bind_method(D_METHOD("set_load_screen", "info"), &AptMenuPlayer::set_load_screen);
	ClassDB::bind_method(D_METHOD("set_load_progress", "percent"), &AptMenuPlayer::set_load_progress);
	ClassDB::bind_method(D_METHOD("set_load_screen_from_game", "resolved_game"), &AptMenuPlayer::set_load_screen_from_game);
	ClassDB::bind_method(D_METHOD("end_game_screen", "request"), &AptMenuPlayer::end_game_screen); // lane END-1
	ClassDB::bind_method(D_METHOD("disconnect_screen", "state"), &AptMenuPlayer::disconnect_screen); // lane MP-2
	ClassDB::bind_method(D_METHOD("take_disconnect_actions"), &AptMenuPlayer::take_disconnect_actions);
	ClassDB::bind_method(D_METHOD("set_save_load", "info"), &AptMenuPlayer::set_save_load);
	ClassDB::bind_method(D_METHOD("set_score_screen_from_world", "world"), &AptMenuPlayer::set_score_screen_from_world);
	ClassDB::bind_method(D_METHOD("set_lan", "world"), &AptMenuPlayer::set_lan); // lane MP-2
	ClassDB::bind_method(D_METHOD("set_create_a_hero", "world", "profile_dir"), &AptMenuPlayer::set_create_a_hero); // lane CAH-1
	ClassDB::bind_method(D_METHOD("get_create_a_hero_view"), &AptMenuPlayer::get_create_a_hero_view);
	ClassDB::bind_method(D_METHOD("get_create_a_hero_heroes"), &AptMenuPlayer::get_create_a_hero_heroes);
	ClassDB::bind_method(D_METHOD("set_create_a_hero_view_texture", "texture"), &AptMenuPlayer::set_create_a_hero_view_texture);
	ClassDB::bind_method(D_METHOD("create_a_hero_save_folder", "rotwk_install"), &AptMenuPlayer::create_a_hero_save_folder); // lane CAH-2
	ClassDB::bind_method(D_METHOD("create_a_hero_type_name", "name"), &AptMenuPlayer::create_a_hero_type_name);
	ClassDB::bind_method(D_METHOD("shell_fscommand", "command", "argument"), &AptMenuPlayer::shell_fscommand);
	ClassDB::bind_method(D_METHOD("get_timeline_graph", "mode", "path"), &AptMenuPlayer::get_timeline_graph);
	ClassDB::bind_method(D_METHOD("get_member", "level", "path", "name"), &AptMenuPlayer::get_member);
	ClassDB::bind_method(D_METHOD("get_timeline_stats"), &AptMenuPlayer::get_timeline_stats); // lane END-2
	ClassDB::bind_method(D_METHOD("quit_menu", "request"), &AptMenuPlayer::quit_menu);
	ClassDB::bind_method(D_METHOD("list_buttons", "level"), &AptMenuPlayer::list_buttons);
	ClassDB::bind_method(D_METHOD("fetch_text", "label"), &AptMenuPlayer::fetch_text);
	ClassDB::bind_method(D_METHOD("take_load_progress_calls"), &AptMenuPlayer::take_load_progress_calls);
	ClassDB::bind_method(D_METHOD("lobby_apply", "spec"), &AptMenuPlayer::lobby_apply);
	ClassDB::bind_method(D_METHOD("lobby_gadget_rect", "name"), &AptMenuPlayer::lobby_gadget_rect);
	ClassDB::bind_method(D_METHOD("get_shell_report"), &AptMenuPlayer::get_shell_report);
	ADD_SIGNAL(MethodInfo("shell_service", PropertyInfo(Variant::STRING, "kind"), PropertyInfo(Variant::STRING, "a"), PropertyInfo(Variant::STRING, "b")));
	ADD_SIGNAL(MethodInfo("shell_request", PropertyInfo(Variant::STRING, "action"), PropertyInfo(Variant::STRING, "argument")));
	ADD_SIGNAL(MethodInfo("shell_screen", PropertyInfo(Variant::STRING, "filename")));
	ClassDB::bind_method(D_METHOD("take_events"), &AptMenuPlayer::take_events);
	ClassDB::bind_method(D_METHOD("get_stats"), &AptMenuPlayer::get_stats);
	ClassDB::bind_method(D_METHOD("reset_timing"), &AptMenuPlayer::reset_timing);
	ClassDB::bind_method(D_METHOD("get_report"), &AptMenuPlayer::get_report);
	ClassDB::bind_method(D_METHOD("get_stage_size"), &AptMenuPlayer::get_stage_size);
	ClassDB::bind_method(D_METHOD("describe_ops"), &AptMenuPlayer::describe_ops);
	ClassDB::bind_method(D_METHOD("add_test_texture", "name", "image"), &AptMenuPlayer::add_test_texture);
	ClassDB::bind_method(D_METHOD("draw_test_ops", "ops"), &AptMenuPlayer::draw_test_ops);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_process"), "set_auto_process", "get_auto_process");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fit"), "set_fit", "get_fit");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_placeholders"), "set_show_placeholders", "get_show_placeholders");
}

void AptMenuPlayer::set_auto_process(bool enabled)
{
	m_autoProcess = enabled;
	set_process(enabled && m_booted);
}

void AptMenuPlayer::set_fit(bool fit)
{
	m_fit = fit;
	m->dirty = true;
}

void AptMenuPlayer::set_show_placeholders(bool show)
{
	m_showPlaceholders = show;
	m->drawnValid = false; // lane PERF-1 r2: the same list draws differently
	m->dirty = true;
}

void AptMenuPlayer::_ready()
{
	set_process(m_autoProcess && m_booted);
}

void AptMenuPlayer::_process(double delta)
{
	if (!m_booted)
	{
		return;
	}
	tick(delta);
	render(false);
	m->view3d.advance(); // lane UI-2
	advanceMovies(delta * 1000.0); // lane CAMP-2
}

void AptMenuPlayer::advanceMovies(double deltaMs)
{
	// lane CAMP-2: each BinkMovie on screen shows the frame of its time (at the movie's rate); a looping one starts again at its end (a new stream: VP6
	// starts over at a key frame); one that left the screen is closed
	for (auto it = m->movies.begin(); it != m->movies.end();)
	{
		Impl::MovieSlot &s = it->second;
		if (!s.drawn)
		{
			it = m->movies.erase(it);
			continue;
		}
		if (s.stream.is_valid() && s.error.empty())
		{
			s.elapsedMs += deltaMs;
			if (s.elapsedMs >= s.durationMs && s.loop)
			{
				s.elapsedMs = std::fmod(s.elapsedMs, std::max(1.0, s.durationMs));
				s.stream->restart(); // the same texture: the canvas item keeps showing it
			}
			const int64_t target = std::min<int64_t>((int64_t)(s.elapsedMs * s.fps / 1000.0), s.stream->get_frame_count() - 1);
			if (target > s.stream->get_frame() && !s.stream->advance_to(target))
			{
				s.error = toNative(s.stream->get_error());
				m->movieErrors.insert("BinkMovie " + s.title + ": " + s.error);
				UtilityFunctions::printerr(toGodot("MOVIE ERROR BinkMovie " + s.title + ": " + s.error));
			}
		}
		++it;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// boot
// ---------------------------------------------------------------------------------------------------------------------------------
Dictionary AptMenuPlayer::boot(const Ref<RetailFileSystem> &fs, const Dictionary &config)
{
	Dictionary result;
	Array errors;
	m_booted = false;
	const std::uint64_t start = Time::get_singleton()->get_ticks_usec();
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("the retail archives are not mounted");
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	clearCanvas(); // review r2: a reboot recreates textures / fonts, so the retained canvas is stale
	m->fs = fs;
	m->view3d.setOwner(this, fs); // lane UI-2
	m->shell.reset();
	m->source = std::make_unique<AptArchiveFileSource>(*fs->archive_fs());
	m->apt.reset(); // before the host it refers to is reused
	m->host = AptGodotHost();
	m->host.frame = &m->frames;
	m->apt = std::make_unique<Apt>(*m->source, m->host);
	m->textures = std::make_unique<AptTextureStore>(*m->source);
	m->godotTextures.clear();
	m->strings = GameTextTable();
	m->fonts = FontSubstitution();
	m->fontByName.clear();
	m->fontNames.clear();
	m->frames = m->steps = m->rebuilds = 0;
	m->stepUs = m->listUs = m->canvasUs = m->submitUs = 0;

	// the engine side of the extern object and the native-component list come from the caller (retail facts live in the script)
	Dictionary externs = config.get("extern_values", Dictionary());
	Array keys = externs.keys();
	for (int64_t i = 0; i < keys.size(); ++i)
	{
		m->host.externs[toNative(String(keys[i]))] = toNative(String(externs[keys[i]]));
	}
	PackedStringArray components = config.get("component_movies", PackedStringArray());
	for (int64_t i = 0; i < components.size(); ++i)
	{
		m->host.componentMovies.insert(lowerAscii(toNative(components[i])));
	}

	m->loadResources(errors);

	// the movies
	Array levels = config.get("levels", Array());
	for (int64_t i = 0; i < levels.size(); ++i)
	{
		Array entry = levels[i];
		if (entry.size() != 2)
		{
			errors.push_back("levels[] entries are [level, movie]");
			continue;
		}
		std::string loadError;
		if (!m->A()->loadMovie((int)(int64_t)entry[0], toNative(String(entry[1])), &loadError))
		{
			errors.push_back(toGodot("loadMovie(" + std::to_string((int)(int64_t)entry[0]) + ", " + toNative(String(entry[1])) + "): " + loadError));
		}
	}
	m_booted = errors.is_empty();
	set_process(m_autoProcess && m_booted);
	m->dirty = true;
	result["ok"] = m_booted;
	result["errors"] = errors;
	result["load_ms"] = (double)(Time::get_singleton()->get_ticks_usec() - start) / 1000.0;
	result["strings"] = (int64_t)m->strings.size();
	result["duplicate_labels"] = (int64_t)m->strings.duplicateLabels().size();
	PackedStringArray fontNames;
	for (const std::string &n : m->fontNames)
	{
		fontNames.push_back(toGodot(n));
	}
	result["fonts"] = fontNames;
	return result;
}


// ---------------------------------------------------------------------------------------------------------------------------------
// shell mode (lane START-1)
// ---------------------------------------------------------------------------------------------------------------------------------
Dictionary AptMenuPlayer::boot_shell(const Ref<RetailFileSystem> &fs, Object *worldObject, const Dictionary &config)
{
	Dictionary result;
	Array errors;
	m_booted = false;
	const std::uint64_t start = Time::get_singleton()->get_ticks_usec();
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("the retail archives are not mounted");
	}
	else if (!world || !world->object_world() || !world->object_world()->loaded())
	{
		errors.push_back("boot_shell needs a GameWorld whose setup() succeeded on the same file system (the lobby lists the logic's PlayerTemplate store)");
	}
	if (!errors.is_empty())
	{
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	clearCanvas(); // review r2: a reboot recreates textures / fonts, so the retained canvas is stale
	m->fs = fs;
	m->worldId = world->get_instance_id(); // lane CAMP-2: the BinkMovie component's movies
	m->view3d.setOwner(this, fs); // lane UI-2
	m->shell.reset();
	m->apt.reset();
	m->source = std::make_unique<AptArchiveFileSource>(*fs->archive_fs());
	m->textures = std::make_unique<AptTextureStore>(*m->source);
	m->godotTextures.clear();
	m->strings = GameTextTable();
	m->fonts = FontSubstitution();
	m->fontByName.clear();
	m->fontNames.clear();
	m->frames = m->steps = m->rebuilds = 0;
	m->stepUs = m->listUs = m->canvasUs = m->submitUs = 0;
	m->loadResources(errors);

	auto sm = std::make_unique<ShellMode>();
	std::string error;
	sm->text = std::make_unique<ShellGameText>(m->strings, *fs->archive_fs());
	if (!sm->setup.load(*world->object_world(), world->logic_settings(), *fs->archive_fs(), &error))
	{
		errors.push_back(toGodot("the lobby's setup source: " + error));
	}
	if (!loadGadgetSkinData(*fs->archive_fs(), sm->skins, &error))
	{
		errors.push_back(toGodot("gadget skins: " + error));
	}
	std::int64_t seed = config.get("seed", (int64_t)0);
	if (seed == 0)
	{
		seed = (std::int64_t)(Time::get_singleton()->get_ticks_msec() & 0x7FFFFFFF); // ZH SkirmishGameInfo::setSeed(GetTickCount()) at lobby entry
	}
	sm->sink.seed = (std::uint32_t)seed;
	sm->environment.gameText = sm->text.get();
	sm->environment.skirmish = &sm->setup;
	sm->environment.newGame = &sm->sink;
	sm->environment.loadScreen = &sm->loadScreen;
	sm->environment.scoreScreen = &sm->scoreScreen; // lane END-1: TimeLine.apt
	sm->environment.saveLoad = &sm->saveLoad;       // lane MP-2: SaveLoad.apt
	sm->environment.profiles = &sm->profiles;
	sm->environment.services = &sm->services;
	// lane UI-2: Options.ini in the user data folder (OptionPreferences); a missing file is no entries (the first run). The soft particle setting
	// takes effect at once (FX-3's project setting).
	sm->environment.optionsFile = toNative(OS::get_singleton()->get_user_data_dir()) + "/Options.ini";
	sm->options.load(sm->environment.optionsFile);
	sm->environment.options = &sm->options;
	sm->services.applyOption(OptionPreferences::kSoftParticles, sm->options.softParticles() ? "yes" : "no");
	registerAptScreenFactories(sm->factories);
	Impl *impl = m.get();
	sm->metrics = std::make_unique<ShellFontMetrics>([impl](const std::string &name, float size, float *drawSize) { return impl->fontFor(name, size, drawSize); });
	if (errors.is_empty())
	{
		sm->wm = std::make_unique<WindowManager>(*m->source, sm->services);
		sm->layer = std::make_unique<AptGadgetLayer>(*sm->wm, *m->source, sm->skins);
		sm->layer->gadgets().setFontMetrics(sm->metrics.get());
		// lane UI-2: a WND's TEXT label is game text (lotr.str's "APT:Null" is the empty string: the Options check boxes' own labels draw nothing);
		// a label the table lacks is kept as the label and listed by the manager
		ShellGameText *gameText = sm->text.get();
		sm->layer->gadgets().setTextResolver([gameText](const std::string &label, UnicodeString &out) { return gameText && gameText->fetch(label, out); });
		sm->layer->registerComponents();
		sm->shell = std::make_unique<Shell>(*sm->wm, sm->factories, sm->services, sm->environment);
		sm->wm->setShell(sm->shell.get());
		// lane FB7-1: what the Options screen's InitGadgets reads besides Options.ini (AptSimpleScreens.h): the AudioSettings default volumes and the
		// machine's addresses and display modes (the device's, from the config), GameData's KeyboardDefaultScrollSpeedFactor (CameraSettings)
		{
			PackedFloat32Array volumes = config.get("default_volumes", PackedFloat32Array());
			if (volumes.size() == 5)
			{
				for (int i = 0; i < 5; ++i)
				{
					sm->environment.defaultVolumes[i] = volumes[i];
				}
				sm->environment.haveDefaultVolumes = true;
			}
			// else: the Options screen reports that it has no defaults when it needs them (viewers and tests boot without an audio manager)
			// this machine's addresses through the LAN transport (the one place that touches the network)
			for (std::uint32_t a : LocalIPv4Addresses())
			{
				sm->environment.localAddresses.emplace_back(std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 0xFF) + "." + std::to_string((a >> 8) & 0xFF) + "." +
						std::to_string(a & 0xFF), a);
			}
			Array modes = config.get("display_modes", Array());
			for (int i = 0; i < modes.size(); ++i)
			{
				const Vector2i m = modes[i];
				sm->environment.displayModes.emplace_back(m.x, m.y);
			}
			const Vector2i current = config.get("current_resolution", Vector2i(0, 0));
			sm->environment.currentResolution = { current.x, current.y };
			CameraSettings camera;
			std::string cameraError;
			if (CameraSettings::load(*fs->archive_fs(), camera, &cameraError))
			{
				sm->environment.keyboardDefaultScrollSpeedFactor = camera.keyboardDefaultScrollSpeedFactor;
				sm->environment.haveScrollDefault = true;
			}
			else
			{
				errors.push_back("boot_shell: GameData KeyboardDefaultScrollSpeedFactor: " + toGodot(cameraError));
			}
		}
		sm->wm->init();
		sm->wm->seedRandom((std::uint32_t)seed);
		// lane FB7-1: GameData ShellMapOn, then the engine start's showShellMap(1) (RW 0x5EAB8F)
		{
			std::vector<std::uint8_t> bytes;
			std::string readError, parseError;
			bool on = false;
			if (!fs->archive_fs()->readFile("data\\ini\\gamedata.ini", bytes, &readError))
			{
				errors.push_back(toGodot("boot_shell: data\\ini\\gamedata.ini: " + readError));
			}
			else if (!Shell::readShellMapOn(std::string(bytes.begin(), bytes.end()), on, &parseError))
			{
				errors.push_back(toGodot("boot_shell: " + parseError));
			}
			sm->environment.shellMapOn = on;
			sm->shell->showShellMap(true);
		}
		// lane FB7-1: GameClient::init ends by loading the background movie (RW 0x646771 -> RW 0x6224C5), the main menu's FadeInBackground shows it
		if (!sm->wm->loadBackground())
		{
			errors.push_back("Background.apt did not load (the front-end background)");
		}
	}
	m->shell = std::move(sm);
	m_booted = errors.is_empty();
	set_process(m_autoProcess && m_booted);
	m->dirty = true;
	result["ok"] = m_booted;
	result["errors"] = errors;
	result["load_ms"] = (double)(Time::get_singleton()->get_ticks_usec() - start) / 1000.0;
	result["strings"] = (int64_t)m->strings.size();
	result["seed"] = (int64_t)seed;
	PackedStringArray fontNames;
	for (const std::string &n : m->fontNames)
	{
		fontNames.push_back(toGodot(n));
	}
	result["fonts"] = fontNames;
	return result;
}

String AptMenuPlayer::shell_screen_unavailable(const String &action)
{
	if (!m->shell || !m->shell->shell)
	{
		return String();
	}
	AptMainMenu *menu = dynamic_cast<AptMainMenu *>(m->shell->shell->findScreenByFilename("MainMenu.apt"));
	if (!menu)
	{
		return String();
	}
	m->dirty = true;
	return toGodot(menu->screenUnavailable(toNative(action)));
}

void AptMenuPlayer::shell_hide_background()
{
	// lane FB7-1: a game's start hides the front-end background with its animation (Skirmish's start RW 0x9286D7, the main menu's RW 0x91B1D2:
	// RW 0x622C88(0))
	if (m->shell && m->shell->wm)
	{
		m->shell->wm->hideBackground(false);
	}
}

void AptMenuPlayer::shell_show_shell_map(bool use)
{
	// lane FB7-1: Shell::showShellMap (RW 0x75DE01): a game's start passes false (RW 0x601C62), the return to the shell true (RW 0x7792BC)
	if (m->shell && m->shell->shell)
	{
		m->shell->shell->showShellMap(use);
		m->dirty = true;
		m->drawnValid = false; // lane CAMP-2: the backdrop is not in the canvas list: an unchanged list must still be drawn again without it
	}
}

Dictionary AptMenuPlayer::get_backdrop_state() const
{
	// lane CAMP-2: what is on screen behind the shell's movies, read from the canvas as drawn (not from the shell's flags)
	Dictionary d;
	d["backdrop_image"] = toGodot(m->drawnBackdrop);
	const int background = m->shell && m->shell->wm ? m->shell->wm->backgroundLevel() : -1;
	d["background_level"] = (int64_t)background;
	const auto it = m->levelCommands.find(background);
	d["background_commands"] = it == m->levelCommands.end() ? (int64_t)0 : it->second;
	d["background_mode"] = (int64_t)(m->shell && m->shell->wm ? m->shell->wm->backgroundMode() : 0);
	PackedStringArray paths;
	for (const std::string &p : m->backgroundPaths)
	{
		paths.push_back(toGodot(p));
	}
	d["background_paths"] = paths;
	return d;
}

bool AptMenuPlayer::is_shell_mode() const
{
	return (bool)m->shell;
}

bool AptMenuPlayer::shell_push(const String &filename)
{
	if (!m->shell || !m->shell->shell)
	{
		return false;
	}
	const int before = m->shell->shell->screenCount();
	m->shell->shell->push(toNative(filename), false);
	m->dirty = true;
	return m->shell->shell->screenCount() > before;
}

void AptMenuPlayer::shell_pop()
{
	if (m->shell && m->shell->shell)
	{
		m->shell->shell->pop();
		m->dirty = true;
	}
}

PackedStringArray AptMenuPlayer::shell_stack() const
{
	PackedStringArray out;
	if (m->shell && m->shell->shell)
	{
		for (int i = 0; i < m->shell->shell->screenCount(); ++i)
		{
			out.push_back(toGodot(m->shell->shell->screenAt(i)->filename()));
		}
	}
	return out;
}

Variant AptMenuPlayer::get_option(const String &key) const
{
	const std::string k = key.utf8().get_data();
	if (!m->shell || !m->shell->options.has(k))
	{
		return Variant();
	}
	return toGodot(m->shell->options.get(k));
}

Dictionary AptMenuPlayer::set_player_status(const Dictionary &state)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	if (!(bool)state.get("ok", false))
	{
		r["error"] = "no game";
		return r;
	}
	NewGameMessage message;
	std::string error;
	if (!newGameFromDictionary(state.get("game", Dictionary()), message, &error))
	{
		r["error"] = toGodot(error);
		return r;
	}
	const Array orig = state.get("orig", Array());
	const Array slots = state.get("slots", Array());
	PlayerStatusInput in;
	in.game = &message.game;
	for (int i = 0; i < MAX_SLOTS && i < (int)orig.size(); ++i)
	{
		const Dictionary o = orig[i];
		message.game.slots[i].origPlayerTemplate = (int)(int64_t)o.get("template", (int64_t)-1);
		message.game.slots[i].origColor = (int)(int64_t)o.get("color", (int64_t)-1);
	}
	for (int i = 0; i < MAX_SLOTS && i < (int)slots.size(); ++i)
	{
		const Dictionary sd = slots[i];
		in.slots[i].hasPlayer = (bool)sd.get("has_player", false);
		in.slots[i].defeated = (bool)sd.get("defeated", false);
		in.slots[i].observer = (bool)sd.get("observer", false);
		in.slots[i].connected = (bool)sd.get("connected", true);
	}
	in.localSlot = (int)(int64_t)state.get("local_slot", (int64_t)-1);
	in.network = (bool)state.get("network", false);
	in.gameMode = (int)(int64_t)state.get("mode", (int64_t)2);
	in.showRandomPlayerTemplate = (bool)state.get("show_random_template", true);
	in.showRandomColor = (bool)state.get("show_random_color", true);
	m->shell->playerStatus = makePlayerStatusInfo(in, m->shell->setup, m->shell->text.get());
	m->shell->environment.playerStatus = &m->shell->playerStatus;
	Array rows;
	for (const PlayerStatusRow &row : m->shell->playerStatus.rows)
	{
		Array a;
		for (const std::string &f : row.fields)
		{
			a.push_back(toGodot(f));
		}
		a.push_back((int64_t)row.color);
		rows.push_back(a);
	}
	r["rows"] = rows;
	r["ok"] = true;
	return r;
}

PackedInt32Array AptMenuPlayer::shell_levels() const
{
	// lane CAMP-2: the Apt level of each screen of the shell's stack, bottom first (-1: not loaded yet)
	PackedInt32Array out;
	if (m->shell && m->shell->shell)
	{
		for (int i = 0; i < m->shell->shell->screenCount(); ++i)
		{
			out.push_back(m->shell->shell->screenAt(i)->level());
		}
	}
	return out;
}

bool AptMenuPlayer::level_drawn(int level) const
{
	// lane CAMP-2: the last render list drew something visible of `level`
	const auto it = m->levelCommands.find(level);
	return it != m->levelCommands.end() && it->second > 0;
}

int AptMenuPlayer::shell_top_level() const
{
	if (m->shell && m->shell->shell && m->shell->shell->top())
	{
		return m->shell->shell->top()->level();
	}
	return -1;
}

bool AptMenuPlayer::shell_fscommand(const String &command, const String &argument)
{
	if (!m->shell)
	{
		return false;
	}
	return m->shell->wm->invokeCallback(toNative(command), toNative(argument));
}

Dictionary AptMenuPlayer::shell_invoke(int level, const String &function, const PackedStringArray &args)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	std::vector<std::string> a;
	for (int64_t i = 0; i < args.size(); ++i)
	{
		a.push_back(toNative(args[i]));
	}
	std::string result, error;
	const std::string fn = toNative(function);
	const size_t colon = fn.find(':');
	// "Main.Axis:Function" calls the function on that clip below the level root
	r["ok"] = colon == std::string::npos ? m->shell->wm->invokeAS(level, fn, a, &result, &error)
										 : m->shell->wm->invokeASAt(level, fn.substr(0, colon), fn.substr(colon + 1), a, &result, &error);
	r["result"] = toGodot(result);
	r["error"] = toGodot(error);
	m->dirty = true;
	return r;
}

Dictionary AptMenuPlayer::take_new_game()
{
	Dictionary d;
	if (m->shell && !m->shell->sink.messages.empty())
	{
		const NewGameMessage message = m->shell->sink.messages.front();
		m->shell->sink.messages.erase(m->shell->sink.messages.begin());
		d = newGameToDictionary(message);
	}
	return d;
}

Dictionary AptMenuPlayer::set_load_screen_from_game(const Dictionary &resolved)
{
	Dictionary out;
	if (!m->shell)
	{
		return out;
	}
	NewGameMessage message;
	std::string error;
	if (!newGameFromDictionary(resolved, message, &error))
	{
		out["error"] = toGodot(error);
		return out;
	}
	m->shell->loadScreen = makeLoadScreenInfo(message.game, m->shell->setup, m->shell->text.get());
	const LoadScreenInfo &info = m->shell->loadScreen;
	Array cards;
	for (int i = 0; i < MAX_LOAD_SLOTS; ++i)
	{
		if (!info.cards[i].occupied)
		{
			continue;
		}
		Dictionary c;
		c["name"] = toGodot(loadScreenU16ToUtf8(info.cards[i].playerName));
		c["army"] = toGodot(loadScreenU16ToUtf8(info.cards[i].armyName));
		c["team"] = info.cards[i].teamNumber;
		c["color"] = (int64_t)info.cards[i].color;
		cards.push_back(c);
	}
	out["cards"] = cards;
	out["local_card"] = info.localCard;
	out["loading_type"] = info.gameLoadingType;
	return out;
}

Dictionary AptMenuPlayer::lobby_gadget_rect(const String &name)
{
	Dictionary out;
	out["found"] = false;
	if (!m->shell || !m->shell->shell)
	{
		return out;
	}
	AptSkirmish *screen = dynamic_cast<AptSkirmish *>(m->shell->shell->findScreenByFilename("Skirmish.apt"));
	if (!screen)
	{
		screen = dynamic_cast<AptLanLobby *>(m->shell->shell->findScreenByFilename("LanLobby.apt"));
	}
	if (!screen)
	{
		return out;
	}
	const std::string n = toNative(name);
	GameWindow *w = nullptr;
	if (n.rfind("spot/", 0) == 0)
	{
		const int i = std::atoi(n.c_str() + 5);
		const std::vector<GameWindow *> &spots = screen->mapStartSpotWindows();
		w = i >= 0 && i < (int)spots.size() ? spots[(std::size_t)i] : nullptr;
	}
	else
	{
		const bool list = n.rfind("list/", 0) == 0;
		const std::string rest = list ? n.substr(5) : n;
		const std::size_t slash = rest.find('/');
		if (slash != std::string::npos)
		{
			w = screen->slotGadget(std::atoi(rest.c_str()), rest.substr(slash + 1));
			if (w && list)
			{
				w = BitTest(w->winGetStyle(), GWS_COMBO_BOX) ? GadgetComboBoxGetListBox(w) : GadgetImageComboBoxGetListBox(w);
			}
		}
	}
	if (!w)
	{
		return out;
	}
	int x = 0, y = 0, ww = 0, hh = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&ww, &hh);
	out["found"] = true;
	out["x"] = x;
	out["y"] = y;
	out["w"] = ww;
	out["h"] = hh;
	return out;
}

Dictionary AptMenuPlayer::lobby_apply(const Dictionary &spec)
{
	Dictionary out;
	Array errors;
	out["errors"] = errors;
	out["ok"] = false;
	if (!m->shell || !m->shell->shell)
	{
		errors.push_back("not in shell mode");
		return out;
	}
	ShellMode &sm = *m->shell;
	AptSkirmish *screen = dynamic_cast<AptSkirmish *>(sm.shell->findScreenByFilename("Skirmish.apt"));
	AptLanLobby *lan = dynamic_cast<AptLanLobby *>(sm.shell->findScreenByFilename("LanLobby.apt")); // lane MP-2: the same MpGameSetup movie
	screen = screen ? screen : lan;
	if (lan && spec.has("join_row"))
	{
		// LanLobby::CustomGamesList: the row's selection, then the Join button (AptLanLobby::OnJoinGameBttn)
		GameWindow *list = lan->gamesList();
		if (!list)
		{
			errors.push_back("the LAN lobby has no game list");
			return out;
		}
		GadgetListBoxSetSelected(list, (int)(int64_t)spec["join_row"]);
		sm.wm->invokeCallback("AptLanLobby::OnJoinGameBttn", "");
		out["ok"] = errors.is_empty();
		return out;
	}
	if (!screen || !screen->setup())
	{
		errors.push_back("the Skirmish screen is not up");
		return out;
	}
	if (spec.has("profile"))
	{
		if (GameWindow *entry = screen->profileEntryWindow())
		{
			GadgetTextEntrySetText(entry, utf8ToU16(toNative(String(spec["profile"]))));
			sm.wm->invokeCallback("AptSkirmish::OnAddProfileAccept", "");
			// the click on the popup's Select button also runs the movie's own close (the C++ click-through tests); the hook does the same
			std::string closeError;
			if (!sm.wm->invokeASAt(screen->level(), "ProfilePopup", "ClosePop", {}, nullptr, &closeError))
			{
				errors.push_back(toGodot("ProfilePopup.ClosePop: " + closeError));
			}
		}
		else
		{
			errors.push_back("the add-profile popup is not open");
		}
	}
	auto itemRow = [](GameWindow *combo, std::intptr_t data) {
		for (int r = 0; r < GadgetComboBoxGetLength(combo); ++r)
		{
			if ((std::intptr_t)GadgetComboBoxGetItemData(combo, r) == data)
			{
				return r;
			}
		}
		return -1;
	};
	if (spec.has("map"))
	{
		const std::string key = toNative(String(spec["map"]));
		const std::vector<std::string> keys = screen->mapListKeys();
		int row = -1;
		for (size_t i = 0; i < keys.size(); ++i)
		{
			row = keys[i] == key ? (int)i : row;
		}
		if (row < 0 || !screen->mapListWindow())
		{
			errors.push_back(toGodot("the lobby does not list the map " + key));
		}
		else
		{
			GadgetListBoxSetSelected(screen->mapListWindow(), row);
			if (screen->setup()->info().mapName != key)
			{
				errors.push_back(toGodot("selecting row " + std::to_string(row) + " did not choose " + key));
			}
		}
	}
	const Array slots = spec.get("slots", Array());
	for (int64_t i = 0; i < slots.size(); ++i)
	{
		const Dictionary sd = slots[i];
		const int slot = (int)(int64_t)sd["slot"];
		if (sd.has("state"))
		{
			GameWindow *combo = screen->slotGadget(slot, "Player");
			const int row = combo ? itemRow(combo, (std::intptr_t)(int64_t)sd["state"]) : -1;
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no Player entry for state " + std::to_string((int)(int64_t)sd["state"])));
			}
			else
			{
				GadgetComboBoxSetSelectedPos(combo, row, false);
			}
		}
		if (sd.has("faction"))
		{
			const std::string name = toNative(String(sd["faction"]));
			int index = -1;
			for (int f = 0; f < (int)sm.setup.factions().size(); ++f)
			{
				index = sm.setup.factions()[(size_t)f].templateName == name ? f : index;
			}
			GameWindow *combo = screen->slotGadget(slot, "PlayerTemplate");
			const int row = (combo && index >= 0) ? itemRow(combo, (std::intptr_t)index) : -1;
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no faction entry for " + name));
			}
			else
			{
				GadgetComboBoxSetSelectedPos(combo, row, false);
			}
		}
		if (sd.has("color"))
		{
			GameWindow *box = screen->slotGadget(slot, "Color");
			int row = -1;
			for (int r = 0; box && r < GadgetImageComboBoxGetLength(box); ++r)
			{
				row = (std::intptr_t)GadgetImageComboBoxGetItemData(box, r) == (std::intptr_t)(int64_t)sd["color"] ? r : row;
			}
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no colour entry " + std::to_string((int)(int64_t)sd["color"])));
			}
			else
			{
				GadgetImageComboBoxSetSelectedPos(box, row);
			}
		}
		if (sd.has("team"))
		{
			GameWindow *combo = screen->slotGadget(slot, "Team");
			const int row = combo ? itemRow(combo, (std::intptr_t)(int64_t)sd["team"]) : -1;
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no team entry " + std::to_string((int)(int64_t)sd["team"])));
			}
			else
			{
				GadgetComboBoxSetSelectedPos(combo, row, false);
			}
		}
		if (sd.has("hero"))
		{
			// lane CAH-1: the Hero combo (RW 0x842C93): a hero's unique id, "-" (none) or "random"
			const std::string want = toNative(String(sd["hero"]));
			const int index = sm.cahHeroes.findByUniqueID(want);
			const bool known = want == "-" || want == "random" || index >= 0;
			const std::intptr_t data = want == "-" ? -1 : want == "random" ? -2 : (std::intptr_t)index;
			GameWindow *combo = screen->slotGadget(slot, "Hero");
			const int row = (combo && known) ? itemRow(combo, data) : -1;
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no hero entry for " + want));
			}
			else
			{
				GadgetComboBoxSetSelectedPos(combo, row, false);
			}
		}
	}
	NewGameMessage current;
	current.game = screen->setup()->info();
	out["game"] = newGameToDictionary(current);
	out["ok"] = errors.is_empty();
	m->dirty = true;
	return out;
}

Array AptMenuPlayer::take_load_progress_calls()
{
	Array out;
	if (!m->shell || !m->shell->shell)
	{
		return out;
	}
	if (AptLoadScreen *load = dynamic_cast<AptLoadScreen *>(m->shell->shell->findScreenByFilename("LoadScreen.apt")))
	{
		for (const auto &p : load->progressCalls())
		{
			Array e;
			e.push_back(p.first);
			e.push_back(p.second);
			out.push_back(e);
		}
	}
	return out;
}

void AptMenuPlayer::set_load_screen(const Dictionary &info)
{
	if (m->shell)
	{
		loadScreenFromDictionary(info, m->shell->loadScreen);
	}
}

bool AptMenuPlayer::set_load_progress(double percent)
{
	if (!m->shell || !m->shell->shell)
	{
		return false;
	}
	AptScreen *screen = m->shell->shell->findScreenByFilename("LoadScreen.apt");
	AptLoadScreen *load = dynamic_cast<AptLoadScreen *>(screen);
	if (!load)
	{
		return false;
	}
	m->dirty = true;
	return load->setLocalProgress((int)percent);
}

Dictionary AptMenuPlayer::get_shell_report() const
{
	Dictionary r;
	if (!m->shell)
	{
		return r;
	}
	ShellMode &sm = *m->shell;
	Dictionary notes;
	std::map<std::string, int> counts;
	PackedStringArray unported;
	for (const WindowManagerNote &n : sm.wm->notes())
	{
		counts[n.kind] += 1;
		unported.push_back(toGodot(n.kind + ": " + n.detail));
	}
	for (const auto &kv : counts)
	{
		notes[toGodot(kv.first)] = kv.second;
	}
	r["notes"] = notes;
	r["note_lines"] = unported;
	// the calls without a function the movies made (stop S-380: retail skips them silently; the player keeps a note per call)
	PackedStringArray calls;
	for (const AptNote &n : sm.wm->apt().notes())
	{
		if (n.kind == "call-without-function")
		{
			calls.push_back(toGodot(n.detail));
		}
	}
	r["calls_without_function"] = calls;
	PackedStringArray errors;
	for (const std::string &e : sm.wm->errors())
	{
		errors.push_back(toGodot("window manager: " + e));
	}
	for (const std::string &e : sm.shell->errors())
	{
		errors.push_back(toGodot("shell: " + e));
	}
	for (const std::string &e : sm.layer->errors())
	{
		errors.push_back(toGodot("gadgets: " + e));
	}
	for (const std::string &e : sm.gadgetErrors)
	{
		errors.push_back(toGodot("gadget draw: " + e));
	}
	r["errors"] = errors;
	PackedStringArray traces; // the movies' ActionTrace output, the last 60 lines (diagnostics)
	const std::vector<std::string> &all = sm.wm->traces();
	for (size_t i = all.size() > 60 ? all.size() - 60 : 0; i < all.size(); ++i)
	{
		traces.push_back(toGodot(all[i]));
	}
	r["traces"] = traces;
	PackedStringArray layerNotes;
	for (const std::string &e : sm.layer->notes())
	{
		layerNotes.push_back(toGodot(e));
	}
	r["gadget_notes"] = layerNotes;
	PackedStringArray deviceUnverified; // lane UI-2: the device's own inferences (S-1482)
	for (const std::string &e : sm.deviceUnverified)
	{
		deviceUnverified.push_back(toGodot(e));
	}
	r["unverified_device"] = deviceUnverified;
	PackedStringArray refusals;
	for (const auto &p : sm.sink.refusals)
	{
		refusals.push_back(toGodot(u16ToUtf8(p.first) + ": " + u16ToUtf8(p.second)));
	}
	r["start_refusals"] = refusals;
	return r;
}

void AptMenuPlayer::pumpShellEvents()
{
	if (!m->shell)
	{
		return;
	}
	ShellMode &sm = *m->shell;
	// lane PLAY-1: the events are taken out before they are emitted: a handler that drives the shell (game.gd opens the quit menu on ToggleQuitMenu) pumps
	// again, and the list still held the event being handled, so one press of the Palantir's options button reached the game twice (the owner's log)
	std::vector<ShellDeviceServices::Event> events;
	events.swap(sm.services.events);
	for (const ShellDeviceServices::Event &e : events)
	{
		if (e.kind == "request")
		{
			emit_signal("shell_request", toGodot(shellActionName((ShellAction)std::atoi(e.a.c_str()))), toGodot(e.b));
		}
		else
		{
			emit_signal("shell_service", toGodot(e.kind), toGodot(e.a), toGodot(e.b));
		}
	}
	std::string top = sm.shell && sm.shell->top() ? sm.shell->top()->filename() : std::string();
	if (top != sm.lastTop)
	{
		sm.lastTop = top;
		emit_signal("shell_screen", toGodot(top));
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------------------------------------------------------------
int AptMenuPlayer::tick(double delta)
{
	if (!m_booted)
	{
		return 0;
	}
	if (m->external)
	{
		// lane HUD-1: the window manager that owns the Apt player is stepped by its owner (InGameHud::update); the picture changes every frame
		m->dirty = true;
		m->frames += 1;
		return 0;
	}
	// the player takes whole milliseconds; the rest carries.  A long stall is not replayed (250 ms cap, viewer pacing: S-138).
	m->carryMs += std::min(delta, 0.25) * 1000.0;
	const int ms = (int)m->carryMs;
	m->carryMs -= ms;
	const std::uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	int stepped = 0;
	if (m->shell)
	{
		// WindowManager::update (retail 0x0046E850): pending pop / loads, focus, the movies, the gadget layer, the screens' updates, the tooltip.  Gadgets change
		// with input between movie steps, so the canvas is rebuilt on every tick in shell mode.
		m->shell->wm->update(ms);
		stepped = 1;
	}
	else
	{
		stepped = m->A()->update(ms);
	}
	m->stepUs += Time::get_singleton()->get_ticks_usec() - t0;
	m->frames += 1;
	m->steps += (std::uint64_t)stepped;
	if (stepped > 0)
	{
		m->dirty = true;
	}
	pumpShellEvents();
	return stepped;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------------------------------------------------------------
Vector2 AptMenuPlayer::windowSize() const
{
	Viewport *vp = get_viewport();
	return vp ? vp->get_visible_rect().get_size() : Vector2(1024, 768);
}

Vector2 AptMenuPlayer::get_stage_size() const
{
	return Vector2(m->mapping.stageW, m->mapping.stageH);
}

void AptMenuPlayer::clearCanvas()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs)
	{
		return;
	}
	// children first: a freed parent does not free its children
	for (size_t i = m->items.size(); i-- > 0;)
	{
		rs->free_rid(m->items[i]);
	}
	m->items.clear();
	m->drawnValid = false;
	m->nativeSlots.clear();
	m->drawnBackdrop.clear(); // lane CAMP-2
}

void AptMenuPlayer::render(bool force)
{
	if (!m_booted)
	{
		return;
	}
	const Vector2 window = windowSize();
	if (window != m->lastWindow || m_fit != m->lastFit)
	{
		m->dirty = true;
		m->lastWindow = window;
		m->lastFit = m_fit;
	}
	if (!force && !m->dirty)
	{
		return;
	}
	m->dirty = false;
	// the stage is the lowest loaded level's movie size
	const std::vector<int> levels = m->A()->loadedLevels();
	if (levels.empty())
	{
		clearCanvas();
		return;
	}
	if (AptSpriteInst *root = m->A()->level(levels.front()))
	{
		if (root->timelineFile)
		{
			m->mapping.stageW = (float)root->timelineFile->width;
			m->mapping.stageH = (float)root->timelineFile->height;
		}
	}
	m->mapping.windowW = window.x;
	m->mapping.windowH = window.y;
	m->mapping.mode = m_fit ? AptStageMapping::Mode::Fit : AptStageMapping::Mode::Stretch;

	Time *time = Time::get_singleton();
	std::uint64_t t0 = time->get_ticks_usec();
	AptRenderList rl;
	m->A()->buildRenderList(rl);
	std::uint64_t t1 = time->get_ticks_usec();
	// lane CAMP-2: each level's visible commands (a shape, text or native component whose cumulative alpha is not zero)
	m->levelCommands.clear();
	m->backgroundPaths.clear();
	const int backgroundLevel = m->shell && m->shell->wm ? m->shell->wm->backgroundLevel() : -1;
	for (const AptRenderCommand &c : rl.commands)
	{
		if ((c.kind == AptRenderCommand::Kind::Shape || c.kind == AptRenderCommand::Kind::Text || c.kind == AptRenderCommand::Kind::Placeholder)
			&& c.color.mul[3] > 0.0f)
		{
			m->levelCommands[c.level] += 1;
			if (c.level == backgroundLevel && m->backgroundPaths.size() < 8)
			{
				m->backgroundPaths.push_back(c.path + " alpha " + std::to_string(c.color.mul[3]));
			}
		}
	}
	AptCanvasInputs in;
	in.mapping = m->mapping;
	in.textures = m->textures.get();
	in.text = &m->strings;
	in.fonts = &m->fonts;
	AptTextRecordLookup recordLookup;
	if (m->shell || m->external)
	{
		WindowManager *wm = m->external ? m->external : m->shell->wm.get();
		recordLookup = [wm](const std::string &name, std::string &out) { return wm->aptTextShown(name, out); };
		in.textRecords = &recordLookup;
	}
	BuildAptCanvas(rl, in, m->list);
	std::uint64_t t2 = time->get_ticks_usec();
	const bool haveGadgets = buildGadgets(m->gadgets);
	if (m->drawnValid && sameCanvas(m->list, m->drawnList) && (!haveGadgets || sameGadgets(m->gadgets, m->drawnGadgets)))
	{
		// lane PERF-1 r2: the HUD's canvas list (and in shell mode its gadget list) is the same as the one on screen in most render frames; its items
		// stay, only the native components are drawn again, each into a new item at its place (what a full redraw does for them)
		redrawNativePlaceholders();
		m->unchangedFrames += 1;
	}
	else
	{
		drawCanvas(m->list);
		drawGadgets(m->gadgets);
		m->drawnList = m->list;
		m->drawnGadgets = m->gadgets;
		m->drawnValid = true;
	}
	std::uint64_t t3 = time->get_ticks_usec();
	m->listUs += t1 - t0;
	m->canvasUs += t2 - t1;
	m->submitUs += t3 - t2;
	m->rebuilds += 1;
	m->noteList();
}

bool AptMenuPlayer::sameCanvas(const AptCanvasList &a, const AptCanvasList &b)
{
	// every field drawCanvas reads, compared bit for bit (floats too: the same bits draw the same)
	auto sameFloats = [](const float *x, const float *y, size_t n) { return n == 0 || std::memcmp(x, y, n * sizeof(float)) == 0; };
	auto sameVec = [&](const std::vector<float> &x, const std::vector<float> &y) { return x.size() == y.size() && sameFloats(x.data(), y.data(), x.size()); };
	if (a.ops.size() != b.ops.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.ops.size(); ++i)
	{
		const AptCanvasOp &x = a.ops[i], &y = b.ops[i];
		const float mx[6] = { x.matrix.a, x.matrix.b, x.matrix.c, x.matrix.d, x.matrix.tx, x.matrix.ty };
		const float my[6] = { y.matrix.a, y.matrix.b, y.matrix.c, y.matrix.d, y.matrix.tx, y.matrix.ty };
		const float tx[6] = { x.fontHeight, x.drawSize, x.scaleX, x.scaleY, x.offsetX, x.offsetY };
		const float ty[6] = { y.fontHeight, y.drawSize, y.scaleX, y.scaleY, y.offsetX, y.offsetY };
		if (x.kind != y.kind || x.maskShape != y.maskShape || x.bold != y.bold || x.alignment != y.alignment || x.wordWrap != y.wordWrap || x.multiline != y.multiline ||
			x.readOnly != y.readOnly || x.dropShadow != y.dropShadow || x.nativeTag != y.nativeTag || !sameFloats(x.shadowColor, y.shadowColor, 4) || !sameFloats(x.textColor, y.textColor, 4) ||
			!sameFloats(x.bounds, y.bounds, 4) || !sameFloats(mx, my, 6) || !sameFloats(tx, ty, 6) || !sameVec(x.positions, y.positions) || !sameVec(x.colors, y.colors) ||
			!sameVec(x.uvs, y.uvs) || x.texture != y.texture || x.text != y.text || x.fontName != y.fontName || x.drawFont != y.drawFont || x.path != y.path ||
			x.symbolMovie != y.symbolMovie || x.symbolName != y.symbolName || x.renderObject != y.renderObject || x.nativeVars != y.nativeVars)
		{
			return false;
		}
	}
	return true;
}

void AptMenuPlayer::redrawNativePlaceholders()
{
	if (!m->nativeHook)
	{
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	for (const Impl::NativeSlot &slot : m->nativeSlots)
	{
		RID &item = m->items[slot.item];
		rs->free_rid(item);
		item = rs->canvas_item_create();
		rs->canvas_item_set_parent(item, slot.parent);
		rs->canvas_item_set_draw_index(item, static_cast<int>(slot.item));
		rs->canvas_item_set_default_texture_filter(item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(item, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		m->nativeHook->drawPlaceholder(m->drawnList.ops[slot.op], item);
	}
}

void AptMenuPlayer::drawCanvas(const AptCanvasList &list)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	clearCanvas();
	for (auto &kv : m->movies)
	{
		kv.second.drawn = false; // lane CAMP-2: a BinkMovie this list does not draw is closed (advanceMovies)
	}
	const RID root = get_canvas_item();
	std::vector<RID> parents{ root };
	RID current;

	auto newItem = [&](RID parent) {
		RID r = rs->canvas_item_create();
		rs->canvas_item_set_parent(r, parent);
		// Godot sorts the children of a parent by their draw index, which is 0 for every new item, with an unstable sort: siblings
		// need increasing indices to keep the render list's order (review finding; masks and material changes make many siblings)
		rs->canvas_item_set_draw_index(r, static_cast<int>(m->items.size()));
		rs->canvas_item_set_default_texture_filter(r, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(r, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		m->items.push_back(r);
		return r;
	};
	auto itemFor = [&]() {
		// one item until a mask bracket opens or closes (the colour transform is in the vertex colours: no per-op material)
		if (!current.is_valid())
		{
			current = newItem(parents.back());
		}
		return current;
	};
	// lane FB7-1: the display's backdrop behind every Apt level (Shell::update RW 0x75E1D3 -> RW 0x65C42C slot 0): the mapped image ShellMapLowLOD
	// (HandCreatedMappedImages.ini: InstallLoad.tga, 1024 x 768 of a 1024 x 1024 texture). INFERENCE (S-1912): it covers the stage as the movies do
	// (the display's draw of the slot was not read)
	m->drawnBackdrop.clear(); // lane CAMP-2
	if (m->shell && m->shell->shell && !m->shell->shell->backdropImage().empty())
	{
		ShellMode &sm = *m->shell;
		const std::string &name = sm.shell->backdropImage();
		const ::Image *img = sm.skins.images.findImageByName(name);
		if (!img)
		{
			sm.gadgetErrors.insert("backdrop image '" + name + "' is not in the mapped image collection");
		}
		else
		{
			auto it = m->godotTextures.find(img->filename);
			if (it == m->godotTextures.end())
			{
				const AptTextureStore::Entry &entry = m->textures->get(img->filename);
				if (entry.ok && !entry.rgba.empty())
				{
					PackedByteArray px;
					px.resize((int64_t)entry.rgba.size());
					memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
					Ref<godot::Image> image = godot::Image::create_from_data(entry.width, entry.height, false, godot::Image::FORMAT_RGBA8, px);
					it = m->godotTextures.emplace(img->filename, ImageTexture::create_from_image(image)).first;
					m->textures->releasePixels(img->filename);
					m->textureLoads += 1;
				}
				else
				{
					// the W3D loader's packed file for the requested name (as the gadget images, drawGadgets)
					std::string stem = img->filename.substr(0, img->filename.find_last_of('.'));
					for (char &ch : stem)
					{
						ch = (char)std::tolower((unsigned char)ch);
					}
					bool paired = false;
					Ref<godot::Image> packed = stem.size() > 2 ? loadPackedTexture(*m->source, "art/compiledtextures/" + stem.substr(0, 2) + "/" + stem, &paired) : Ref<godot::Image>();
					if (packed.is_valid())
					{
						it = m->godotTextures.emplace(img->filename, ImageTexture::create_from_image(packed)).first;
						m->textureLoads += 1;
					}
					else
					{
						sm.gadgetErrors.insert("backdrop texture '" + img->filename + "': " + entry.error);
					}
				}
			}
			if (it != m->godotTextures.end())
			{
				float x0, y0, x1, y1;
				m->mapping.stageToWindow(0.0f, 0.0f, x0, y0);
				m->mapping.stageToWindow((float)img->imageSize.x, (float)img->imageSize.y, x1, y1);
				addTextureUV(rs, newItem(root), Rect2(Vector2(x0, y0), Vector2(x1 - x0, y1 - y0)), it->second, img->uvLo, img->uvHi, Color(1, 1, 1, 1));
				m->drawnBackdrop = name; // lane CAMP-2
			}
		}
	}
	// a diagnostic for artifact hunts (lane HUD-3): OPENBFME_APT_SKIP=<substring>[,<substring>...] leaves out the mesh ops whose instance path contains one
	static const std::vector<std::string> skipPaths = [] {
		std::vector<std::string> v;
		const std::string s = OS::get_singleton() ? std::string(OS::get_singleton()->get_environment("OPENBFME_APT_SKIP").utf8().get_data()) : std::string();
		size_t a = 0;
		while (a < s.size())
		{
			const size_t b = s.find(',', a);
			v.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
			a = b == std::string::npos ? s.size() : b + 1;
		}
		return v;
	}();

	m->nativeSlots.clear();
	m->view3d.beginBuild(); // lane UI-2
	for (size_t opIndex = 0; opIndex < list.ops.size(); ++opIndex)
	{
		const AptCanvasOp &op = list.ops[opIndex];
		if (op.kind == AptCanvasOp::Kind::Mesh && !skipPaths.empty()
			&& std::any_of(skipPaths.begin(), skipPaths.end(), [&](const std::string &s) { return !s.empty() && op.path.find(s) != std::string::npos; }))
		{
			continue;
		}
		switch (op.kind)
		{
			case AptCanvasOp::Kind::MaskBegin:
			{
				RID mask = newItem(parents.back());
				// the item's own drawing is the mask; its children are clipped by it and it is not drawn itself.  (S-135)
				rs->canvas_item_set_canvas_group_mode(mask, RenderingServer::CANVAS_GROUP_MODE_CLIP_ONLY, 5.0f, false, 0.0f, false);
				parents.push_back(mask);
				current = mask;
				break;
			}
			case AptCanvasOp::Kind::MaskContent:
				current = RID();
				break;
			case AptCanvasOp::Kind::MaskEnd:
				if (parents.size() > 1)
				{
					parents.pop_back();
				}
				current = RID();
				break;
			case AptCanvasOp::Kind::Mesh:
			{
				RID item = itemFor();
				const int64_t n = (int64_t)(op.positions.size() / 2);
				PackedVector2Array pts;
				pts.resize(n);
				memcpy(pts.ptrw(), op.positions.data(), (size_t)n * sizeof(float) * 2);
				PackedColorArray cols;
				cols.resize(n);
				memcpy(cols.ptrw(), op.colors.data(), (size_t)n * sizeof(float) * 4);
				PackedVector2Array uvs;
				if (!op.uvs.empty())
				{
					uvs.resize(n);
					memcpy(uvs.ptrw(), op.uvs.data(), (size_t)n * sizeof(float) * 2);
				}
				PackedInt32Array idx;
				idx.resize(n);
				int32_t *ip = idx.ptrw();
				for (int64_t i = 0; i < n; ++i)
				{
					ip[i] = (int32_t)i;
				}
				RID tex;
				if (!op.texture.empty())
				{
					auto it = m->godotTextures.find(op.texture);
					if (it == m->godotTextures.end())
					{
						Time *time = Time::get_singleton();
						const std::uint64_t t0 = time->get_ticks_usec();
						const AptTextureStore::Entry &entry = m->textures->get(op.texture);
						if (!entry.ok || entry.rgba.empty())
						{
							continue; // BuildAptCanvas only keeps textures that loaded; an emptied one is a bug, shown by the missing draw
						}
						PackedByteArray px;
						px.resize((int64_t)entry.rgba.size());
						memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
						Ref<Image> image = Image::create_from_data(entry.width, entry.height, false, Image::FORMAT_RGBA8, px);
						Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
						it = m->godotTextures.emplace(op.texture, texture).first;
						m->textures->releasePixels(op.texture);
						m->textureLoads += 1;
						m->textureLoadMs += (double)(time->get_ticks_usec() - t0) / 1000.0;
					}
					tex = it->second->get_rid();
				}
				rs->canvas_item_add_triangle_array(item, idx, pts, cols, uvs, PackedInt32Array(), PackedFloat32Array(), tex);
				break;
			}
			case AptCanvasOp::Kind::Placeholder:
			{
				if (op.nativeTag && op.symbolName == "View3D" && !op.renderObject.empty())
				{
					// lane UI-2: RotWK's View3D render component (RW 0x814DDC): the `_RenderObj` W3D model through its CAMERA bone over the clip
					float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
					const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
					const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
					for (int i = 0; i < 4; ++i)
					{
						const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
						const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
						minx = std::min(minx, sx);
						maxx = std::max(maxx, sx);
						miny = std::min(miny, sy);
						maxy = std::max(maxy, sy);
					}
					RID item = newItem(parents.back());
					current = RID();
					m->view3d.draw(item, Rect2(minx, miny, maxx - minx, maxy - miny), op);
					break;
				}
				if (op.nativeTag && op.symbolName == "BinkMovie")
				{
					// lane CAMP-2: the BinkMovie render component (registered with the other gadget components, BFME2 decomp Rva00411B52 for RW
					// 0x81487B; RotWK's registration RW 0x814EFA was not read): the clip's script names the Video (`_MovieName`, e.g. MainMenu's
					// movieCredits), `_Loop` and `_UseAlpha`; its picture fills the clip's bounds. INFERENCE (stop S-2342, unverified: the credits page is not ported): the movie starts when the clip is
					// first drawn and loops when `_Loop` is true; `_UseAlpha` = false draws the alpha stream opaque; `_Init` / BinkMovieInit and the
					// movie-complete callback (OnlineHome's MovieComplete) are not ported
					std::string title, loop, useAlpha;
					for (const auto &kv : op.nativeVars)
					{
						if (kv.first == "_MovieName")
						{
							title = kv.second;
						}
						else if (kv.first == "_Loop")
						{
							loop = kv.second;
						}
						else if (kv.first == "_UseAlpha")
						{
							useAlpha = kv.second;
						}
					}
					Impl::MovieSlot &slot = m->movies[op.path];
					slot.drawn = true;
					if (slot.stream.is_null() && slot.error.empty())
					{
						slot.title = title;
						slot.loop = loop == "true" || loop == "1";
						GameWorld *world = Object::cast_to<GameWorld>(ObjectDB::get_instance(m->worldId));
						Dictionary info = world && !title.empty() ? world->get_movie(toGodot(title)) : Dictionary();
						if (!(bool)info.get("ok", false))
						{
							slot.error = title.empty() ? std::string("the clip names no _MovieName") : toNative(String(info.get("error", "no GameWorld")));
						}
						else
						{
							slot.stream.instantiate();
							Dictionary opened = slot.stream->open(String(info.get("full_path", "")));
							if (!(bool)opened.get("ok", false))
							{
								slot.error = toNative(String(opened.get("error", "?")));
								slot.stream.unref();
							}
							else
							{
								slot.durationMs = (double)opened.get("duration_ms", 0.0);
								slot.fps = (double)opened.get("fps", 30.0);
								slot.stream->set_opaque(!(useAlpha == "true" || useAlpha == "1"));
								slot.stream->advance_to(0);
							}
						}
						if (!slot.error.empty())
						{
							m->movieErrors.insert("BinkMovie " + title + ": " + slot.error);
							UtilityFunctions::printerr(toGodot("MOVIE ERROR BinkMovie " + title + ": " + slot.error));
						}
					}
					if (slot.stream.is_valid())
					{
						float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
						const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
						const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
						for (int i = 0; i < 4; ++i)
						{
							const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
							const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
							minx = std::min(minx, sx);
							maxx = std::max(maxx, sx);
							miny = std::min(miny, sy);
							maxy = std::max(maxy, sy);
						}
						RID item = newItem(parents.back());
						current = RID();
						rs->canvas_item_add_texture_rect(item, Rect2(minx, miny, maxx - minx, maxy - miny), slot.stream->get_texture()->get_rid());
					}
					break;
				}
				if (m->nativeHook && op.nativeTag && m->nativeHook->handlesPlaceholder(op))
				{
					// lane HUD-1: the clip is a native render component (the radar, a command button image, a timer): its owner draws into a canvas item of its own
					m->nativeSlots.push_back(Impl::NativeSlot{ opIndex, m->items.size(), parents.back() });
					RID item = newItem(parents.back());
					current = RID();
					m->nativeHook->drawPlaceholder(op, item);
					break;
				}
				if (m->shell && op.nativeTag)
				{
					// lane END-2: a RenderImage clip whose `_imageMap` names an image record of the window manager (the score screen's faction icons,
					// RW 0x92632E): the mapped image fills the clip's bounds
					const std::string *record = nullptr;
					bool mapped = false;
					for (const auto &kv : op.nativeVars)
					{
						if (kv.first == "_imageMap")
						{
							record = m->shell->wm->aptImage(kv.second);
							mapped = true;
						}
					}
					if (!mapped && op.symbolName == "ColorPicker")
					{
						// lane CAH-2: the ColorPicker component draws the mapped image its `_imageName` names over the clip (RW 0xB552BF -> RW 0x44CF58)
						for (const auto &kv : op.nativeVars)
						{
							if (kv.first == "_imageName")
							{
								record = &kv.second;
								mapped = true;
							}
						}
					}
					if (!mapped && op.symbolName == "RenderImage")
					{
						// lane FB7-1: a RenderImage clip without `_imageMap` takes the record of its instance name (AptMainMenu's ctor binds "Image" to
						// LogoWithShadow, RW 0x91CA5C, and MainMenu's `Image` clip has no `_imageMap`)
						const size_t dot = op.path.find_last_of('.');
						record = m->shell->wm->aptImage(dot == std::string::npos ? op.path : op.path.substr(dot + 1));
					}
					// lane UI-2: an engine render callback named by the clip's tag (AptMapPreview::Picture, RW 0x9757C0) draws its picture over the clip's
					// rectangle in opaque white (RW 0x44CF58 with 0xFFFFFFFF): a mapped image, or an image the engine made from a texture file (UV 0..1)
					const WindowManager::RenderPicture *picture = m->shell->wm->renderPicture(op.symbolName);
					if (picture && !picture->mappedImage.empty())
					{
						record = &picture->mappedImage;
					}
					else if (picture && !picture->file.empty())
					{
						auto it = m->godotTextures.find("file:" + picture->file);
						if (it == m->godotTextures.end())
						{
							const AptTextureStore::Entry &entry = m->textures->getFile(picture->file);
							if (entry.ok && !entry.rgba.empty())
							{
								PackedByteArray px;
								px.resize((int64_t)entry.rgba.size());
								memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
								Ref<godot::Image> image = godot::Image::create_from_data(entry.width, entry.height, false, godot::Image::FORMAT_RGBA8, px);
								it = m->godotTextures.emplace("file:" + picture->file, ImageTexture::create_from_image(image)).first;
								m->textureLoads += 1;
							}
							else
							{
								m->shell->gadgetErrors.insert("picture '" + picture->file + "' of " + op.symbolName + ": " + entry.error);
							}
						}
						if (it != m->godotTextures.end())
						{
							float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
							const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
							const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
							for (int i = 0; i < 4; ++i)
							{
								const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
								const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
								minx = std::min(minx, sx);
								maxx = std::max(maxx, sx);
								miny = std::min(miny, sy);
								maxy = std::max(maxy, sy);
							}
							RID item = newItem(parents.back());
							current = RID();
							addTextureUV(rs, item, Rect2(minx, miny, maxx - minx, maxy - miny), it->second, picture->uvLo, picture->uvHi, Color(1, 1, 1, 1));
						}
						break;
					}
					const ::Image *img = record ? m->shell->skins.images.findImageByName(*record) : nullptr;
					if (record && !img)
					{
						m->shell->gadgetErrors.insert("image record '" + *record + "' names no mapped image");
					}
					Ref<ImageTexture> tex;
					if (img)
					{
						auto it = m->godotTextures.find(img->filename);
						if (it == m->godotTextures.end())
						{
							const AptTextureStore::Entry &entry = m->textures->get(img->filename);
							if (entry.ok && !entry.rgba.empty())
							{
								PackedByteArray px;
								px.resize((int64_t)entry.rgba.size());
								memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
								Ref<godot::Image> image = godot::Image::create_from_data(entry.width, entry.height, false, godot::Image::FORMAT_RGBA8, px);
								it = m->godotTextures.emplace(img->filename, ImageTexture::create_from_image(image)).first;
								m->textures->releasePixels(img->filename);
								m->textureLoads += 1;
							}
							else
							{
								// lane CAH-1: the packed DDS of the mapped image's texture (art\compiledtextures\<first two letters>\<stem>.dds, as the HUD's
								// button images: the Create-a-Hero class / type / power icons)
								std::string stem = img->filename.substr(0, img->filename.find_last_of('.'));
								for (char &ch : stem)
								{
									ch = (char)std::tolower((unsigned char)ch);
								}
								std::vector<std::uint8_t> dds;
								std::string ddsError;
								Ref<godot::Image> image;
								if (stem.size() > 2 && m->fs.is_valid() && m->fs->archive_fs() &&
									m->fs->archive_fs()->readFile("art/compiledtextures/" + stem.substr(0, 2) + "/" + stem + ".dds", dds, &ddsError))
								{
									PackedByteArray data;
									data.resize((int64_t)dds.size());
									memcpy(data.ptrw(), dds.data(), dds.size());
									image.instantiate();
									if (image->load_dds_from_buffer(data) != OK)
									{
										image.unref();
									}
								}
								if (image.is_valid())
								{
									it = m->godotTextures.emplace(img->filename, ImageTexture::create_from_image(image)).first;
									m->textureLoads += 1;
								}
								else
								{
									m->shell->gadgetErrors.insert("texture '" + img->filename + "' of image '" + *record + "': " + entry.error);
								}
							}
						}
						if (it != m->godotTextures.end())
						{
							tex = it->second;
						}
					}
					if (tex.is_valid())
					{
						float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
						const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
						const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
						for (int i = 0; i < 4; ++i)
						{
							const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
							const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
							minx = std::min(minx, sx);
							maxx = std::max(maxx, sx);
							miny = std::min(miny, sy);
							maxy = std::max(maxy, sy);
						}
						const Vector2 size = tex->get_size();
						const Rect2 src(img->uvLo[0] * size.x, img->uvLo[1] * size.y, (img->uvHi[0] - img->uvLo[0]) * size.x, (img->uvHi[1] - img->uvLo[1]) * size.y);
						RID item = newItem(parents.back());
						current = RID();
						rs->canvas_item_add_texture_rect_region(item, Rect2(minx, miny, maxx - minx, maxy - miny), tex->get_rid(), src, Color(1, 1, 1, 1));
						break;
					}
				}
				if (!m_showPlaceholders)
				{
					break;
				}
				RID item = itemFor();
				PackedVector2Array pts;
				const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
				const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
				for (int i = 0; i < 4; ++i)
				{
					const float sx = op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx;
					const float sy = op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty;
					pts.push_back(Vector2(sx * op.scaleX + op.offsetX, sy * op.scaleY + op.offsetY));
				}
				PackedColorArray cols;
				for (int i = 0; i < 4; ++i)
				{
					cols.push_back(Color(1.0f, 0.2f, 0.9f, 0.25f));
				}
				rs->canvas_item_add_polygon(item, pts, cols);
				break;
			}
			case AptCanvasOp::Kind::Text:
			{
				RID item = itemFor();
				const float det = op.matrix.a * op.matrix.d - op.matrix.b * op.matrix.c;
				const float sdet = std::sqrt(std::fabs(det));
				if (sdet <= 0.0f)
				{
					break;
				}
				// the font follows the device scale by min(scaleX, scaleY) (donor AptDisplayStringAllocation_ctor.cpp:105-110)
				const float k = std::min(op.scaleX, op.scaleY);
				const int px = std::max(1, (int)std::lround(op.drawSize * k * sdet));
				Ref<Font> font;
				auto fit = m->fontByName.find(lowerAscii(op.drawFont));
				if (fit == m->fontByName.end())
				{
					fit = m->fontByName.find(compact(op.drawFont));
				}
				if (fit != m->fontByName.end())
				{
					font = fit->second;
				}
				else
				{
					// a system font the corpus does not ship (Times New Roman ...): the engine default font, reported (S-132)
					font = ThemeDB::get_singleton()->get_fallback_font();
					m->fontFallbacks.insert(op.drawFont);
				}
				if (font.is_null())
				{
					break;
				}
				HorizontalAlignment align = HORIZONTAL_ALIGNMENT_LEFT;
				switch (op.alignment)
				{
					case 1: align = HORIZONTAL_ALIGNMENT_RIGHT; break;
					case 2: align = HORIZONTAL_ALIGNMENT_CENTER; break;
					case 3: align = HORIZONTAL_ALIGNMENT_FILL; break;
					default: break;
				}
				// RW 0x4A8F95: the text and its drop shadow carry RotWK's vertex colours (AptCanvas.h AptRetailVertexColour; the colour transform's
				// additive term is inside them)
				const Color colour(op.textColor[0], op.textColor[1], op.textColor[2], op.textColor[3]);
				const Color shadow(op.shadowColor[0], op.shadowColor[1], op.shadowColor[2], op.shadowColor[3]);
				const String text = toGodot(op.text);
				// RW 0x4A8F95 (lane FB7-1): every field is placed as RotWK's Apt display string draws it (AptCanvas.h PlaceAptText): the box from the two
				// corners through the matrix, the string aligned in it, centred vertically unless the field is multiline AND word-wrapping, squeezed when
				// wider, at whole pixels, unrotated. The ctor (RW 0x4AA369) does not look at the read-only flag: a multiline field without word wrap (the
				// main menu's buttons) is one centred line, not a top-aligned block (S-1910)
				auto toWindow = [&](float lx, float ly) {
					const float px2 = op.matrix.a * lx + op.matrix.c * ly + op.matrix.tx;
					const float py2 = op.matrix.b * lx + op.matrix.d * ly + op.matrix.ty;
					return Vector2(px2 * op.scaleX + op.offsetX, py2 * op.scaleY + op.offsetY);
				};
				const Vector2 c0 = toWindow(op.bounds[0], op.bounds[1]), c1 = toWindow(op.bounds[2], op.bounds[3]);
				// RW 0x4AA4F0 ..: only the word-wrap flag makes the display string wrap (to the box width, centred lines for alignment 2); a '\n' in the
				// text still breaks the line
				const bool wrapped = op.wordWrap;
				const bool lines = wrapped || op.text.find('\n') != std::string::npos;
				const float boxWidth = std::fabs(c1.x - c0.x);
				const Vector2 size = lines ? font->get_multiline_string_size(text, align, wrapped ? boxWidth : -1.0f, px) : font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, px);
				const AptTextPlacement place = PlaceAptText(c0.x, c0.y, c1.x, c1.y, wrapped ? boxWidth : size.x, size.y, op.alignment, op.multiline, op.wordWrap);
				rs->canvas_item_add_set_transform(item, Transform2D(Vector2(place.squeezeX, 0), Vector2(0, 1), Vector2(place.x, place.y)));
				const Vector2 base(0, font->get_ascent(px));
				auto drawText = [&](const Vector2 &at, const Color &c) {
					if (lines)
					{
						font->draw_multiline_string(item, at, text, wrapped ? align : HORIZONTAL_ALIGNMENT_LEFT, wrapped ? boxWidth : -1.0f, px, -1, c);
					}
					else
					{
						font->draw_string(item, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, c);
					}
				};
				if (op.dropShadow)
				{
					drawText(base + Vector2(1, 1) * k, shadow); // offset: S-133
				}
				drawText(base, colour);
				rs->canvas_item_add_set_transform(item, Transform2D());
				break;
			}
		}
	}
	m->view3d.endBuild(); // lane UI-2: viewers no op drew stop rendering
}


// The native gadgets (lobby list boxes, combo boxes, text entries ...) after the movies: GadgetDrawList commands in stage pixels (GadgetDrawList.h),
// translated through the stage mapping to canvas items under the player's own item list (cleared with the movies' items by clearCanvas).
bool AptMenuPlayer::buildGadgets(GadgetDrawList &commands)
{
	commands.clear();
	if (!m->shell || !m->shell->layer)
	{
		return false;
	}
	m->shell->layer->buildDrawList(commands);
	return true;
}

bool AptMenuPlayer::sameGadgets(const GadgetDrawList &a, const GadgetDrawList &b)
{
	if (a.commands.size() != b.commands.size() || a.unresolvedImages != b.unresolvedImages)
	{
		return false;
	}
	for (size_t i = 0; i < a.commands.size(); ++i)
	{
		const GadgetDrawCommand &x = a.commands[i], &y = b.commands[i];
		if (x.kind != y.kind || x.x0 != y.x0 || x.y0 != y.y0 || x.x1 != y.x1 || x.y1 != y.y1 || x.color != y.color || x.dropColor != y.dropColor ||
			x.lineWidth != y.lineWidth || x.wrapWidth != y.wrapWidth || x.wrapCentered != y.wrapCentered || x.image != y.image || x.text != y.text ||
			x.window != y.window || x.font.name != y.font.name || x.font.pointSize != y.font.pointSize || x.font.bold != y.font.bold || x.font.height != y.font.height)
		{
			return false;
		}
	}
	return true;
}

void AptMenuPlayer::drawGadgets(const GadgetDrawList &commands)
{
	if (!m->shell || !m->shell->layer)
	{
		return;
	}
	ShellMode &sm = *m->shell;
	for (const std::string &u : commands.unresolvedImages)
	{
		sm.gadgetErrors.insert("image '" + u + "' is not in the mapped image collection");
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const AptStageMapping &map = m->mapping;
	const float k = map.uniformScale();
	std::vector<RID> parents{ get_canvas_item() };
	bool clipOpen = false; // a ClipBegin without its ClipEnd yet
	RID current;
	auto newItem = [&](RID parent) {
		RID r = rs->canvas_item_create();
		rs->canvas_item_set_parent(r, parent);
		rs->canvas_item_set_draw_index(r, static_cast<int>(m->items.size()));
		rs->canvas_item_set_default_texture_filter(r, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(r, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		m->items.push_back(r);
		return r;
	};
	auto item = [&]() {
		if (!current.is_valid())
		{
			current = newItem(parents.back());
		}
		return current;
	};
	auto toWindow = [&](float x, float y) {
		float wx, wy;
		map.stageToWindow(x, y, wx, wy);
		return Vector2(wx, wy);
	};
	auto colour = [](std::uint32_t argb) { return Color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f); };

	for (const GadgetDrawCommand &c : commands.commands)
	{
		switch (c.kind)
		{
			case GadgetDrawCommand::Kind::Image:
			{
				const ::Image *img = sm.skins.images.findImageByName(c.image);
				if (!img)
				{
					break; // reported through unresolvedImages
				}
				// lane UI-2: an image the engine made from a file (IMAGE_STATUS_RAW_TEXTURE: the lobby's map preview, RW 0x70292F) names the file's path
				const bool rawFile = (img->status & IMAGE_STATUS_RAW_TEXTURE) != 0;
				const std::string key = rawFile ? "file:" + img->filename : img->filename;
				auto it = m->godotTextures.find(key);
				if (it == m->godotTextures.end())
				{
					const AptTextureStore::Entry &entry = rawFile ? m->textures->getFile(img->filename) : m->textures->get(img->filename);
					if ((!entry.ok || entry.rgba.empty()) && !rawFile)
					{
						// lane UI-2: a mapped image naming a .tga that the install packs as DDS (ScrollShroud.tga -> art\compiledtextures\sc\scrollshroud.dds),
						// as the HUD device loads it (GodotInGameHud.cpp textureFor): the W3D loader takes the packed file for the requested name
						std::string stem = img->filename.substr(0, img->filename.find_last_of('.'));
						for (char &ch : stem)
						{
							ch = (char)std::tolower((unsigned char)ch);
						}
						bool paired = false;
						Ref<godot::Image> packed = stem.size() > 2 ? loadPackedTexture(*m->source, "art/compiledtextures/" + stem.substr(0, 2) + "/" + stem, &paired) : Ref<godot::Image>();
						if (paired)
						{
							sm.deviceUnverified.insert("[S-1482] texture " + img->filename + ": the packed .jpg's colour with its .png as the alpha (the loader's merge of the two streams was not read)");
						}
						if (packed.is_valid())
						{
							it = m->godotTextures.emplace(key, ImageTexture::create_from_image(packed)).first;
							m->textureLoads += 1;
						}
					}
					if (it == m->godotTextures.end() && (!entry.ok || entry.rgba.empty()))
					{
						sm.gadgetErrors.insert("texture '" + img->filename + "' of image '" + c.image + "': " + entry.error);
						break;
					}
					if (it == m->godotTextures.end())
					{
						PackedByteArray px;
						px.resize((int64_t)entry.rgba.size());
						memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
						Ref<godot::Image> image = godot::Image::create_from_data(entry.width, entry.height, false, godot::Image::FORMAT_RGBA8, px);
						Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
						it = m->godotTextures.emplace(key, texture).first;
						if (!rawFile)
						{
							m->textures->releasePixels(img->filename);
						}
						m->textureLoads += 1;
					}
				}
				const Vector2 a = toWindow((float)c.x0, (float)c.y0), b = toWindow((float)c.x1, (float)c.y1);
				addTextureUV(rs, item(), Rect2(a, b - a), it->second, img->uvLo, img->uvHi, colour(c.color));
				break;
			}
			case GadgetDrawCommand::Kind::FillRect:
			{
				// lane UI-2: corners in either order (W3DDrawMapPreview passes a -1 wide bar when the picture spans the window, RW 0x49DA94; retail's 2D
				// quad is not culled, so it covers the pixel column on the other side)
				const Vector2 a = toWindow((float)c.x0, (float)c.y0), b = toWindow((float)c.x1, (float)c.y1);
				rs->canvas_item_add_rect(item(), Rect2(a, b - a).abs(), colour(c.color));
				break;
			}
			case GadgetDrawCommand::Kind::OpenRect:
			{
				// lane PLAY-1: corners in either order (a negative rectangle made Godot's canvas cull report "Rect2 size is negative" when it merged the
				// item's bounds: 124 errors in the owner's session)
				const Vector2 p0 = toWindow((float)c.x0, (float)c.y0), p1 = toWindow((float)c.x1, (float)c.y1);
				const Vector2 a(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), b(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
				const float w = std::max(1.0f, (float)c.lineWidth * k);
				const Color col = colour(c.color);
				RID it = item();
				rs->canvas_item_add_rect(it, Rect2(a.x, a.y, b.x - a.x, w), col);
				rs->canvas_item_add_rect(it, Rect2(a.x, b.y - w, b.x - a.x, w), col);
				rs->canvas_item_add_rect(it, Rect2(a.x, a.y, w, b.y - a.y), col);
				rs->canvas_item_add_rect(it, Rect2(b.x - w, a.y, w, b.y - a.y), col);
				break;
			}
			case GadgetDrawCommand::Kind::Line:
				rs->canvas_item_add_line(item(), toWindow((float)c.x0, (float)c.y0), toWindow((float)c.x1, (float)c.y1), colour(c.color), std::max(1.0f, (float)c.lineWidth * k));
				break;
			case GadgetDrawCommand::Kind::Text:
			{
				float drawSize = 0;
				Ref<Font> font = m->fontFor(c.font.name, (float)c.font.pointSize, &drawSize);
				if (font.is_null() || c.text.empty())
				{
					break;
				}
				const int px = std::max(1, (int)std::lround(drawSize * k));
				const Vector2 at = toWindow((float)c.x0, (float)c.y0);
				const Vector2 base = at + Vector2(0, font->get_ascent(px));
				const String text = toGodot(u16ToUtf8(c.text));
				const float width = c.wrapWidth > 0 ? (float)c.wrapWidth * map.scaleX() : -1.0f;
				RID it = item();
				auto drawText = [&](const Vector2 &p, const Color &col) {
					if (c.wrapWidth > 0)
					{
						font->draw_multiline_string(it, p, text, c.wrapCentered ? HORIZONTAL_ALIGNMENT_CENTER : HORIZONTAL_ALIGNMENT_LEFT, width, px, -1, col);
					}
					else
					{
						font->draw_string(it, p, text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, col);
					}
				};
				if ((c.dropColor >> 24) != 0)
				{
					drawText(base + Vector2(1, 1) * k, colour(c.dropColor));
				}
				drawText(base, colour(c.color));
				break;
			}
			case GadgetDrawCommand::Kind::ClipBegin:
			{
				// ZH setClipRegion REPLACES the region (a list box sets one per cell before it draws it): a clip still open is closed first, else the
				// cells' regions would intersect (lane END-2: the score screen's StatsList drew its first cell only)
				if (clipOpen && parents.size() > 1)
				{
					parents.pop_back();
				}
				clipOpen = true;
				const Vector2 a = toWindow((float)c.x0, (float)c.y0), b = toWindow((float)c.x1, (float)c.y1);
				RID clip = newItem(parents.back());
				// lane PLAY-1: a region whose far corner lies before its near one clips everything (an empty scissor; INFERENCE: retail's clip of an inverted
				// region was not read); a negative custom rect made Godot's canvas cull report "Rect2 size is negative" on every merge
				rs->canvas_item_set_custom_rect(clip, true, Rect2(a, Vector2(std::max(0.0f, b.x - a.x), std::max(0.0f, b.y - a.y))));
				rs->canvas_item_set_clip(clip, true);
				parents.push_back(clip);
				current = RID();
				break;
			}
			case GadgetDrawCommand::Kind::ClipEnd:
				if (clipOpen && parents.size() > 1)
				{
					parents.pop_back();
				}
				clipOpen = false;
				current = RID();
				break;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// levels
// ---------------------------------------------------------------------------------------------------------------------------------
Dictionary AptMenuPlayer::load_movie(int level, const String &movie)
{
	Dictionary r;
	std::string error;
	r["ok"] = m_booted && m->A()->loadMovie(level, toNative(movie), &error);
	r["error"] = toGodot(error);
	m->dirty = true;
	return r;
}

bool AptMenuPlayer::unload_level(int level)
{
	m->dirty = true;
	return m_booted && m->A()->unloadLevel(level);
}

void AptMenuPlayer::set_level_visible(int level, bool visible)
{
	if (!m_booted)
	{
		return;
	}
	if (AptSpriteInst *root = m->A()->level(level))
	{
		root->visible = visible;
		m->dirty = true;
	}
}

bool AptMenuPlayer::has_level(int level) const
{
	return m_booted && m->A()->level(level) != nullptr;
}

Dictionary AptMenuPlayer::invoke(int level, const String &function, const PackedStringArray &args)
{
	Dictionary r;
	std::string result, error;
	std::vector<std::string> a;
	for (int64_t i = 0; i < args.size(); ++i)
	{
		a.push_back(toNative(args[i]));
	}
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	const bool ok = root && m->A()->invoke(root, toNative(function), a, &result, &error);
	if (!root)
	{
		error = "no movie on level " + std::to_string(level);
	}
	r["ok"] = ok;
	r["result"] = toGodot(result);
	r["error"] = toGodot(error);
	m->dirty = true;
	return r;
}

void AptMenuPlayer::set_extern_value(const String &name, const String &value)
{
	m->host.externs[toNative(name)] = toNative(value);
}

String AptMenuPlayer::dump_tree(int level, int max_depth)
{
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	return root ? toGodot(m->A()->dumpTree(root, max_depth)) : String();
}

namespace
{
AptButtonInst *firstButton(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *s = c->asSprite())
	{
		for (AptCharacterInst *k : s->children())
		{
			if (AptButtonInst *b = firstButton(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}
} // namespace

Dictionary AptMenuPlayer::find_button(int level, const String &path)
{
	Dictionary r;
	r["found"] = false;
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	if (!root)
	{
		return r;
	}
	AptCharacterInst *inst = m->A()->resolvePath(root, toNative(path));
	AptButtonInst *b = firstButton(inst);
	float x0, y0, x1, y1;
	if (!b || !b->contentBounds(x0, y0, x1, y1))
	{
		return r;
	}
	// lane PLAY-1: the button's hit area is its mesh placed by each Hit record's matrix (AptButtonInst::hitTest); the file's button bounds are the mesh's own
	// (the Palantir's PlayerMagic / Objectives buttons place a -20..20 mesh at 0..100): aim at the centre of the Hit records' area
	if (const AptButtonInfo *info = b->info())
	{
		bool any = false;
		float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
		for (const AptButtonRecord &rec : info->records)
		{
			if (!(rec.stateMask & 8))
			{
				continue;
			}
			const float cxs[4] = { x0, x1, x1, x0 }, cys[4] = { y0, y0, y1, y1 };
			for (int k = 0; k < 4; ++k)
			{
				const float px = rec.matrix[0] * cxs[k] + rec.matrix[2] * cys[k] + rec.translation[0];
				const float py = rec.matrix[1] * cxs[k] + rec.matrix[3] * cys[k] + rec.translation[1];
				hx0 = any ? std::min(hx0, px) : px;
				hy0 = any ? std::min(hy0, py) : py;
				hx1 = any ? std::max(hx1, px) : px;
				hy1 = any ? std::max(hy1, py) : py;
				any = true;
			}
		}
		if (any)
		{
			x0 = hx0;
			y0 = hy0;
			x1 = hx1;
			y1 = hy1;
		}
	}
	float x, y;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	// lane PLAY-1: the centre of the content bounds need not be inside the button's hit shape (the Palantir's PlayerMagic: its centre lies on PalantirBack); then
	// the first point of a grid over the bounds that the input routes to this button is the one to press (a harness clicks there, as a player would)
	if (!b->hitTest(x, y) || m->A()->input().hitTestButtons(x, y) != b)
	{
		// the hit point nearest the centre (a rollover may scale the clip: a point at the shape's edge would leave it again)
		bool found = false;
		float best = 0.0f, cx = x, cy = y;
		for (int gy = 1; gy < 16; ++gy)
		{
			for (int gx = 1; gx < 16; ++gx)
			{
				float px, py;
				b->globalMatrix().apply(x0 + (x1 - x0) * (float)gx / 16.0f, y0 + (y1 - y0) * (float)gy / 16.0f, px, py);
				const float d = (px - cx) * (px - cx) + (py - cy) * (py - cy);
				if ((!found || d < best) && b->hitTest(px, py) && m->A()->input().hitTestButtons(px, py) == b)
				{
					x = px;
					y = py;
					best = d;
					found = true;
				}
			}
		}
		r["moved_to_hit"] = found;
	}
	r["found"] = true;
	r["x"] = x;
	r["y"] = y;
	r["path"] = toGodot(b->targetPath());
	r["enabled"] = b->enabled;
	// the button the input would hit at that point (another one may lie over it)
	AptButtonInst *hit = m->A()->input().hitTestButtons(x, y);
	r["hit"] = hit ? toGodot(hit->targetPath()) : String();
	r["self_hit"] = b->hitTest(x, y);
	String hidden; // the first instance up the chain the input's alpha test (RW 0xAE0BF0) refuses
	for (const AptCharacterInst *c = b; c && hidden.is_empty(); c = c->parent())
	{
		if (!c->visible || c->color.mul[3] < 0.5f)
		{
			hidden = toGodot(c->targetPath()) + String(c->visible ? " alpha " : " hidden ") + String::num(c->color.mul[3]);
		}
	}
	r["hidden_by"] = hidden;
	return r;
}

namespace
{
void collectButtons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
{
	if (!c)
	{
		return;
	}
	if (AptButtonInst *b = c->asButton())
	{
		out.push_back(b);
	}
	if (AptSpriteInst *s = c->asSprite())
	{
		for (AptCharacterInst *k : s->children())
		{
			collectButtons(k, out);
		}
	}
}
} // namespace

// every button of a level with its target path, its centre in stage space and whether the input would take it there (diagnostics and scripted runs)
Array AptMenuPlayer::list_buttons(int level)
{
	Array out;
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	std::vector<AptButtonInst *> buttons;
	collectButtons(root, buttons);
	for (AptButtonInst *b : buttons)
	{
		float x0, y0, x1, y1;
		if (!b->contentBounds(x0, y0, x1, y1))
		{
			continue;
		}
		float x, y;
		b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
		Dictionary d;
		d["path"] = toGodot(b->targetPath());
		d["x"] = x;
		d["y"] = y;
		AptButtonInst *hit = m->A()->input().hitTestButtons(x, y);
		d["hittable"] = hit == b;
		out.push_back(d);
	}
	return out;
}

Dictionary AptMenuPlayer::instance_info(int level, const String &path)
{
	Dictionary r;
	r["found"] = false;
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	if (!root)
	{
		return r;
	}
	AptCharacterInst *inst = m->A()->resolvePath(root, toNative(path));
	if (!inst)
	{
		return r;
	}
	r["found"] = true;
	r["type"] = (int)inst->type();
	r["depth"] = inst->depth();
	float x, y;
	inst->globalMatrix().apply(0, 0, x, y);
	r["x"] = x;
	r["y"] = y;
	r["visible"] = inst->visible;
	// lane END-1: the content bounds in stage space (the parent's global matrix over the corners of contentBounds)
	float bx0, by0, bx1, by1;
	if (inst->contentBounds(bx0, by0, bx1, by1))
	{
		float ax, ay, cx, cy;
		if (inst->parent())
		{
			inst->parent()->globalMatrix().apply(bx0, by0, ax, ay);
			inst->parent()->globalMatrix().apply(bx1, by1, cx, cy);
		}
		else
		{
			ax = bx0, ay = by0, cx = bx1, cy = by1;
		}
		r["bounds"] = Rect2(std::min(ax, cx), std::min(ay, cy), std::abs(cx - ax), std::abs(cy - ay));
	}
	if (AptSpriteInst *s = inst->asSprite())
	{
		r["frame"] = s->frame;
		r["total_frames"] = s->totalFrames();
		r["playing"] = s->playing;
	}
	return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------------------------------------------
Vector2 AptMenuPlayer::window_to_stage(const Vector2 &w) const
{
	const Vector2 window = windowSize(); // the mapping follows the window between renders (events may come first)
	m->mapping.windowW = window.x;
	m->mapping.windowH = window.y;
	float x, y;
	m->mapping.windowToStage(w.x, w.y, x, y);
	return Vector2(x, y);
}

Vector2 AptMenuPlayer::stage_to_window(const Vector2 &s) const
{
	const Vector2 window = windowSize();
	m->mapping.windowW = window.x;
	m->mapping.windowH = window.y;
	float x, y;
	m->mapping.stageToWindow(s.x, s.y, x, y);
	return Vector2(x, y);
}

void AptMenuPlayer::post_mouse_move_stage(const Vector2 &stage)
{
	if (m_booted)
	{
		m->A()->setMousePosition(stage.x, stage.y);
		if (m->shell)
		{
			m->shell->wm->postMouseMove(stage.x, stage.y); // the gadgets first, then the movies (WindowManager.h, input routing)
		}
		else
		{
			m->A()->input().postMouseMove(stage.x, stage.y);
		}
	}
}

void AptMenuPlayer::post_mouse_button(bool down)
{
	if (m_booted)
	{
		if (m->shell)
		{
			m->shell->wm->postMouseButton(down);
		}
		else
		{
			m->A()->input().postMouseButton(down);
		}
	}
}

void AptMenuPlayer::post_mouse_wheel(int delta)
{
	if (m_booted)
	{
		if (m->shell)
		{
			m->shell->wm->postMouseWheel(delta);
		}
		else
		{
			m->A()->input().postMouseWheel(delta);
		}
	}
}

void AptMenuPlayer::post_key(int vk, bool down)
{
	if (m_booted)
	{
		m->A()->input().postKey(vk, down);
	}
}

void AptMenuPlayer::_input(const Ref<InputEvent> &event)
{
	if (!m_booted || event.is_null() || m->external || m->inputForwarded)
	{
		return;
	}
	if (const InputEventMouseMotion *mm = Object::cast_to<InputEventMouseMotion>(event.ptr()))
	{
		post_mouse_move_stage(window_to_stage(mm->get_position()));
	}
	else if (const InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(event.ptr()))
	{
		post_mouse_move_stage(window_to_stage(mb->get_position()));
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT)
		{
			post_mouse_button(mb->is_pressed());
		}
		else if (mb->is_pressed() && mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP)
		{
			post_mouse_wheel(1);
		}
		else if (mb->is_pressed() && mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN)
		{
			post_mouse_wheel(-1);
		}
	}
	else if (const InputEventKey *key = Object::cast_to<InputEventKey>(event.ptr()))
	{
		if (key->is_echo())
		{
			return;
		}
		const int vk = virtualKeyOf(key->get_keycode());
		if (vk != 0)
		{
			post_key(vk, key->is_pressed());
		}
		if (m->shell)
		{
			// the gadgets take DirectInput scan codes (arrows, backspace, delete ...) and, separately, characters (printable keys; Enter is the
			// character '\r', RotWK-style GWM_IME_CHAR path of GadgetTextEntry)
			if (const int dik = dikOf(key->get_keycode()))
			{
				if (dik != ::KEY_ENTER && dik != ::KEY_KPENTER)
				{
					m->shell->wm->postGadgetKey(dik, key->is_pressed());
				}
			}
			if (key->is_pressed())
			{
				const Key code = key->get_keycode();
				if (code == Key::KEY_ENTER || code == Key::KEY_KP_ENTER)
				{
					m->shell->wm->postTextInput(u'\r');
				}
				else if (key->get_unicode() >= 32 && key->get_unicode() != 127)
				{
					m->shell->wm->postTextInput((char16_t)key->get_unicode());
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// observation
// ---------------------------------------------------------------------------------------------------------------------------------
Array AptMenuPlayer::take_events()
{
	Array out;
	for (const AptGodotHost::Event &e : m->host.events)
	{
		Dictionary d;
		d["kind"] = toGodot(e.kind);
		d["a"] = toGodot(e.a);
		d["b"] = toGodot(e.b);
		d["frame"] = (int64_t)e.frame;
		out.push_back(d);
	}
	m->host.events.clear();
	return out;
}

void AptMenuPlayer::reset_timing()
{
	m->frames = m->steps = m->rebuilds = 0;
	m->stepUs = m->listUs = m->canvasUs = m->submitUs = 0;
}

Dictionary AptMenuPlayer::get_stats() const
{
	Dictionary s;
	s["frames"] = (int64_t)m->frames;
	s["steps"] = (int64_t)m->steps;
	s["rebuilds"] = (int64_t)m->rebuilds;
	s["unchanged_frames"] = (int64_t)m->unchangedFrames; // lane PERF-1 r2: rebuilds whose list was the one on screen (only the native components redrawn)
	s["last_ops"] = (int64_t)m->list.ops.size();
	s["last_triangles"] = (int64_t)m->list.triangles;
	s["last_text_ops"] = (int64_t)m->list.count(AptCanvasOp::Kind::Text);
	s["last_mask_ops"] = (int64_t)m->list.count(AptCanvasOp::Kind::MaskBegin);
	s["canvas_items"] = (int64_t)m->items.size();
	// microseconds per call of each stage (steps_us per tick, the rest per rebuild)
	s["steps_us"] = m->frames ? (double)m->stepUs / (double)m->frames : 0.0;
	s["list_us"] = m->rebuilds ? (double)m->listUs / (double)m->rebuilds : 0.0;
	s["canvas_us"] = m->rebuilds ? (double)m->canvasUs / (double)m->rebuilds : 0.0;
	s["submit_us"] = m->rebuilds ? (double)m->submitUs / (double)m->rebuilds : 0.0;
	s["texture_loads"] = (int64_t)m->textureLoads;
	s["texture_load_ms"] = m->textureLoadMs;
	return s;
}

void AptMenuPlayer::add_test_texture(const String &name, const Ref<Image> &image)
{
	m->godotTextures[toNative(name)] = ImageTexture::create_from_image(image);
}

void AptMenuPlayer::draw_test_ops(const Array &ops)
{
	AptCanvasList list;
	for (int64_t i = 0; i < ops.size(); ++i)
	{
		Dictionary d = ops[i];
		const std::string kind = toNative(String(d.get("kind", "mesh")));
		AptCanvasOp op;
		if (kind == "mask_begin")
		{
			op.kind = AptCanvasOp::Kind::MaskBegin;
		}
		else if (kind == "mask_content")
		{
			op.kind = AptCanvasOp::Kind::MaskContent;
		}
		else if (kind == "mask_end")
		{
			op.kind = AptCanvasOp::Kind::MaskEnd;
		}
		else
		{
			op.kind = AptCanvasOp::Kind::Mesh;
			const Rect2 r = d.get("rect", Rect2());
			const Color c = d.get("color", Color(1, 1, 1, 1));
			op.texture = toNative(String(d.get("texture", "")));
			op.maskShape = d.get("mask_shape", false);
			const float x0 = r.position.x, y0 = r.position.y, x1 = r.position.x + r.size.x, y1 = r.position.y + r.size.y;
			const float px[6][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y0 }, { x1, y1 }, { x0, y1 } };
			const float uv[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
			for (int v = 0; v < 6; ++v)
			{
				op.positions.push_back(px[v][0]);
				op.positions.push_back(px[v][1]);
				op.colors.insert(op.colors.end(), { c.r, c.g, c.b, c.a });
				if (!op.texture.empty())
				{
					op.uvs.push_back(uv[v][0]);
					op.uvs.push_back(uv[v][1]);
				}
			}
		}
		list.ops.push_back(op);
	}
	drawCanvas(list);
}

PackedStringArray AptMenuPlayer::describe_ops() const
{
	PackedStringArray out;
	static const char *kinds[] = { "mesh", "text", "placeholder", "mask-begin", "mask-content", "mask-end" };
	for (const AptCanvasOp &op : m->list.ops)
	{
		std::string line = std::string(kinds[(int)op.kind]) + " " + op.path;
		if (op.kind == AptCanvasOp::Kind::Mesh)
		{
			float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
			for (size_t i = 0; i + 1 < op.positions.size(); i += 2)
			{
				x0 = std::min(x0, op.positions[i]);
				x1 = std::max(x1, op.positions[i]);
				y0 = std::min(y0, op.positions[i + 1]);
				y1 = std::max(y1, op.positions[i + 1]);
			}
			char buf[200];
			snprintf(buf, sizeof buf, " tex=%s tris=%zu rgba=(%.2f %.2f %.2f %.2f) mask=%d box=(%.0f %.0f %.0f %.0f)", op.texture.c_str(), op.triangleCount(),
				op.colors[0], op.colors[1], op.colors[2], op.colors[3], op.maskShape ? 1 : 0, x0, y0, x1, y1);
			line += buf;
		}
		else if (op.kind == AptCanvasOp::Kind::Text)
		{
			char buf[120];
			snprintf(buf, sizeof buf, " rgba=(%.2f %.2f %.2f %.2f)", op.textColor[0], op.textColor[1], op.textColor[2], op.textColor[3]);
			line += " '" + op.text + "' " + op.drawFont + " " + std::to_string((int)op.drawSize) + buf;
			line += std::string(" readOnly=") + (op.readOnly ? "1" : "0") + " multiline=" + (op.multiline ? "1" : "0") + " wordWrap=" + (op.wordWrap ? "1" : "0") +
				" align=" + std::to_string(op.alignment) + " bounds=(" + std::to_string((int)op.bounds[0]) + " " + std::to_string((int)op.bounds[1]) + " " +
				std::to_string((int)op.bounds[2]) + " " + std::to_string((int)op.bounds[3]) + ")";
		}
		else if (op.kind == AptCanvasOp::Kind::Placeholder)
		{
			line += " " + op.symbolMovie + "." + op.symbolName;
		}
		out.push_back(toGodot(line));
	}
	return out;
}

// lane HUD-1: the Apt player of a window manager the device layer runs (the in-game HUD)
void AptMenuPlayer::attach_window_manager(WindowManager *wm)
{
	m->external = wm;
	m->dirty = true;
}

WindowManager *AptMenuPlayer::window_manager() const
{
	return m->external ? m->external : (m->shell ? m->shell->wm.get() : nullptr);
}

Shell *AptMenuPlayer::shell_object() const
{
	return m->shell ? m->shell->shell.get() : nullptr;
}

void AptMenuPlayer::set_input_forwarded(bool forwarded)
{
	m->inputForwarded = forwarded;
}

void AptMenuPlayer::set_native_hook(AptNativeHook *hook)
{
	if (m->nativeHook && m->nativeHook != hook)
	{
		// lane QA-1: the items the old hook drew into may carry its resources (the HUD's globe material): they go now, before their owner frees them, and the
		// next frame draws the canvas again (else leaving a game freed the material under live items: "Parameter material is null")
		clearCanvas();
		m->nativeSlots.clear();
	}
	m->nativeHook = hook;
	m->drawnValid = false; // lane PERF-1 r2: the native placeholders are drawn by another owner
	m->dirty = true;
}

Array AptMenuPlayer::get_movies() const
{
	Array out;
	for (const auto &kv : m->movies)
	{
		Dictionary d;
		d["path"] = toGodot(kv.first);
		d["title"] = toGodot(kv.second.title);
		d["frame"] = kv.second.stream.is_valid() ? kv.second.stream->get_frame() : (int64_t)-1;
		d["frames"] = kv.second.stream.is_valid() ? kv.second.stream->get_frame_count() : (int64_t)0;
		d["loop"] = kv.second.loop;
		d["error"] = toGodot(kv.second.error);
		out.push_back(d);
	}
	return out;
}

Dictionary AptMenuPlayer::get_report() const
{
	Dictionary r;
	auto toArray = [](const std::set<std::string> &s) {
		PackedStringArray a;
		for (const std::string &v : s)
		{
			a.push_back(toGodot(v));
		}
		return a;
	};
	r["unverified"] = toArray(m->unverified);
	r["errors"] = toArray(m->errors);
	r["missing_labels"] = toArray(m->missingLabels);
	r["font_substitutions"] = toArray(m->fontSubstitutions);
	r["font_fallbacks"] = toArray(m->fontFallbacks);
	r["placeholders"] = toArray(m->placeholders);
	r["view3d_errors"] = toArray(m->view3d.errors()); // lane UI-2
	r["view3d_viewers"] = (int64_t)m->view3d.viewerCount();
	r["view3d_notes"] = toArray(m->view3d.notes());
	r["movie_errors"] = toArray(m->movieErrors); // lane CAMP-2: BinkMovie components that could not play
	Dictionary notes;
	std::map<std::string, int> counts;
	if (m->A())
	{
		for (const AptNote &n : m->A()->notes())
		{
			counts[n.kind] += 1;
		}
	}
	for (const auto &kv : counts)
	{
		notes[toGodot(kv.first)] = kv.second;
	}
	r["notes"] = notes;
	PackedStringArray scriptErrors;
	for (const std::string &e : m->host.scriptErrors)
	{
		scriptErrors.push_back(toGodot(e));
	}
	r["script_errors"] = scriptErrors;
	// the input bridge's unverified rules (S-137): key codes are Windows virtual keys, the wheel delta is +-1, the pointer is never captured
	PackedStringArray input;
	input.push_back("key-codes");
	input.push_back("mouse-wheel");
	input.push_back("click-through");
	r["unverified_input"] = input;
	return r;
}

// ---- lane END-1 ----------------------------------------------------------------------------------------------------------------------------------------

Dictionary AptMenuPlayer::end_game_screen(const Dictionary &request)
{
	Dictionary out;
	out["ok"] = false;
	if (!m->shell || !m->shell->shell)
	{
		out["error"] = "not in shell mode";
		return out;
	}
	ShellMode &sm = *m->shell;
	if (!sm.guiFX)
	{
		// BFME1 AptGuiFXRegisterCallbacks: GuiFX.apt is loaded by the window manager, outside the shell's stack (RotWK calls its ShowEndGame on _level13: S-1060)
		sm.guiFX = std::make_unique<AptGuiFXScreen>(*sm.wm, *sm.shell);
	}
	const int level = sm.guiFX->level();
	out["level"] = level;
	if (level < 0)
	{
		out["error"] = "GuiFX.apt could not be loaded";
		return out;
	}
	const std::string kind = toNative(String(request.get("kind", "")));
	std::string error;
	bool ok = false;
	for (int i = 0; i < 30 && (kind == "show_end_game" || kind == "hide_end_game") && !sm.wm->isAptWindowLoaded(level); ++i)
	{
		tick(1.0 / 30.0); // the window manager loads a movie over its first updates
	}
	if (kind == "show_end_game")
	{
		// RW 0x808E5B: the text ":VictoryDefeat" = TheGameText->fetch(label)
		bool exists = false;
		const std::u16string text = fetchOrMissing(sm.text.get(), toNative(String(request.get("text", ""))), &exists);
		sm.wm->setAptText(":VictoryDefeat", loadScreenU16ToUtf8(text));
		out["text_found"] = exists;
		out["text"] = toGodot(loadScreenU16ToUtf8(text));
		ok = sm.wm->invokeAS(level, "ShowEndGame", { (bool)request.get("evil", false) ? "0" : "1" /* RW 0x808E5B: DAT 0xBD5D8C "0" for the evil side, 0xBD5D90 "1" otherwise */, toNative(String(request.get("sound", ""))), toNative(String(request.get("cheer", ""))) }, nullptr, &error);
	}
	else if (kind == "hide_end_game")
	{
		ok = sm.wm->invokeAS(level, "HideEndGame", {}, nullptr, &error);
	}
	else if (kind == "preload")
	{
		ok = true; // the movie is loading (it is ready after the next ticks)
	}
	else
	{
		error = "not a movie request: " + kind;
	}
	m->dirty = true;
	out["ok"] = ok;
	out["error"] = toGodot(error);
	if (kind == "show_end_game")
	{
		out["flag"] = (bool)request.get("evil", false) ? "0" : "1"; // the ShowEndGame argument (RW 0x808E5B)
	}
	return out;
}

Dictionary AptMenuPlayer::disconnect_screen(const Dictionary &state)
{
	Dictionary out;
	out["ok"] = false;
	if (!m->shell || !m->shell->shell)
	{
		out["error"] = "not in shell mode";
		return out;
	}
	ShellMode &sm = *m->shell;
	if (!(bool)state.get("visible", false))
	{
		// RW 0x918F97: the menu is released when the screen goes
		sm.disconnect.reset();
		m->dirty = true;
		out["ok"] = true;
		out["level"] = -1;
		return out;
	}
	if (!sm.disconnect)
	{
		// RW 0x9190F0: the window manager loads DisconnectScreen.apt (the first free level, S-1123)
		sm.disconnect = std::make_unique<AptDisconnectScreen>(*sm.wm, *sm.shell);
		ShellMode *smp = &sm;
		sm.disconnect->onKick = [smp](int row) {
			Dictionary a;
			a["kind"] = "kick";
			a["row"] = row;
			smp->disconnectActions.push_back(a);
		};
		sm.disconnect->onQuit = [smp]() {
			Dictionary a;
			a["kind"] = "quit";
			smp->disconnectActions.push_back(a);
		};
	}
	std::array<AptDisconnectScreen::Row, 7> rows{};
	const Array src = state.get("rows", Array());
	for (int i = 0; i < 7 && i < src.size(); ++i)
	{
		const Dictionary d = src[i];
		AptDisconnectScreen::Row &r = rows[(size_t)i];
		r.used = (bool)d.get("used", false);
		r.nameUtf8 = toNative(String(d.get("name", "")));
		r.votes = (int)d.get("votes", 0);
		r.barPercent = (int)d.get("bar", 100);
		r.kickShown = (bool)d.get("kick", false);
	}
	const bool loaded = sm.disconnect->apply(rows);
	m->dirty = true;
	out["ok"] = sm.disconnect->level() >= 0;
	out["level"] = sm.disconnect->level();
	out["loaded"] = loaded;
	PackedStringArray calls;
	for (const std::string &c : sm.disconnect->calls())
	{
		calls.push_back(toGodot(c));
	}
	out["calls"] = calls;
	if (sm.disconnect->level() < 0)
	{
		out["error"] = "DisconnectScreen.apt could not be loaded";
	}
	return out;
}

void AptMenuPlayer::set_save_load(const Dictionary &info)
{
	if (!m->shell)
	{
		return;
	}
	SaveLoadInfo &sl = m->shell->saveLoad;
	sl = SaveLoadInfo();
	sl.mode = (int)info.get("mode", 2);
	sl.flags = (unsigned)(int)info.get("flags", 4);
	sl.lastReplayNameUtf8 = toNative(String(info.get("last_replay", "")));
	const Array rows = info.get("replays", Array());
	for (int i = 0; i < rows.size(); ++i)
	{
		const Dictionary d = rows[i];
		SaveLoadInfo::Replay r;
		r.path = toNative(String(d.get("path", "")));
		r.fileNameUtf8 = toNative(String(d.get("name", "")));
		r.mapUtf8 = toNative(String(d.get("map", "")));
		r.dateUtf8 = toNative(String(d.get("date", "")));
		r.timeUtf8 = toNative(String(d.get("time", "")));
		r.compatible = (bool)d.get("compatible", true);
		sl.replays.push_back(r);
	}
}

Array AptMenuPlayer::take_disconnect_actions()
{
	Array a;
	if (m->shell)
	{
		a = m->shell->disconnectActions;
		m->shell->disconnectActions = Array();
	}
	return a;
}

bool AptMenuPlayer::set_score_screen_from_world(Object *worldObject)
{
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (!world || !m->shell)
	{
		return false;
	}
	m->shell->scoreScreen = world->scoreScreenData();
	return m->shell->scoreScreen.valid;
}

// ---- lane CAH-1: the Create-a-Hero builder --------------------------------------------------------------------------------------------------------------
namespace
{
// draws the builder's 3D view (a texture: the device's viewport of the map mode) into the CreateAHero::DrawMapComponent clip (RW 0x91A3A9)
class CreateAHeroViewHook : public AptNativeHook
{
public:
	Ref<Texture2D> texture;
	bool handlesPlaceholder(const AptCanvasOp &op) const override { return op.symbolName == "CreateAHero::DrawMapComponent"; }
	void drawPlaceholder(const AptCanvasOp &op, RID item) override
	{
		if (texture.is_null())
		{
			return;
		}
		const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
		const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
		float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
		for (int i = 0; i < 4; ++i)
		{
			const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
			const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
			minx = std::min(minx, sx);
			maxx = std::max(maxx, sx);
			miny = std::min(miny, sy);
			maxy = std::max(maxy, sy);
		}
		lastRect = Rect2(minx, miny, maxx - minx, maxy - miny);
		if (lastRect.size.x >= 1.0f && lastRect.size.y >= 1.0f)
		{
			RenderingServer::get_singleton()->canvas_item_add_texture_rect(item, lastRect, texture->get_rid());
			++draws;
		}
	}
	Rect2 lastRect;
	std::uint64_t draws = 0;
};
CreateAHeroViewHook g_cahViewHook; // one per process: the shell player is the only one that shows the builder
} // namespace

Dictionary AptMenuPlayer::set_create_a_hero(Object *worldObject, const String &profileDir)
{
	Dictionary out;
	Array errors;
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (!m->shell || !world || !world->object_world() || m->fs.is_null() || !m->fs->archive_fs())
	{
		errors.append(String("set_create_a_hero needs the shell mode and a GameWorld whose setup ran on the same file system"));
		out["ok"] = false;
		out["errors"] = errors;
		return out;
	}
	ShellMode &sm = *m->shell;
	std::vector<std::string> listErrors;
	sm.cahHeroes.load(m->fs->archive_fs(), profileDir.utf8().get_data(), &listErrors);
	for (const std::string &e : listErrors)
	{
		errors.append(toGodot(e));
	}
	sm.cahContext.system = &world->object_world()->createAHeroSystem();
	sm.cahContext.heroes = &sm.cahHeroes;
	ShellMode *smp = &sm;
	sm.cahContext.random = [smp](int lo, int hi) { return hi <= lo ? lo : lo + (int)(smp->cahRandom() % (std::uint32_t)(hi - lo + 1)); };
	sm.environment.createAHero = &sm.cahContext;
	out["ok"] = errors.is_empty();
	out["heroes"] = (int64_t)sm.cahHeroes.size();
	out["errors"] = errors;
	return out;
}

Dictionary AptMenuPlayer::get_create_a_hero_view() const
{
	Dictionary out;
	out["up"] = false;
	AptCreateAHero *screen = m->shell && m->shell->shell ? dynamic_cast<AptCreateAHero *>(m->shell->shell->findScreenByFilename("CreateAHero.apt")) : nullptr;
	if (!screen)
	{
		return out;
	}
	out["up"] = true;
	const char page = screen->currentPage();
	out["page"] = page ? String(std::string(1, page).c_str()) : String();
	out["revision"] = (int64_t)screen->displayRevision();
	PackedByteArray record;
	if (const CreateAHeroHero *h = screen->displayedHero())
	{
		const std::vector<std::uint8_t> bytes = h->save();
		record.resize((int64_t)bytes.size());
		if (!bytes.empty())
		{
			memcpy(record.ptrw(), bytes.data(), bytes.size());
		}
		out["class"] = (int64_t)h->classIndex;
		out["subclass"] = (int64_t)h->subClassIndex;
	}
	out["record"] = record;
	out["rotate_left"] = screen->rotateLeft();
	out["rotate_right"] = screen->rotateRight();
	out["zoom_in"] = screen->zoomIn();
	out["zoom_out"] = screen->zoomOut();
	out["available_power"] = toGodot(screen->firstAvailablePower());
	out["powers_chosen"] = (int64_t)screen->powers().chosenCount();
	out["selected_hero"] = (int64_t)screen->selectedHero();
	out["last_message"] = toGodot(screen->lastMessage());
		out["view_draws"] = (int64_t)g_cahViewHook.draws;
	out["view_rect"] = g_cahViewHook.lastRect;
	return out;
}

Array AptMenuPlayer::get_create_a_hero_heroes() const
{
	Array out;
	if (!m->shell)
	{
		return out;
	}
	for (const CreateAHeroListEntry &e : m->shell->cahHeroes.entries())
	{
		Dictionary d;
		d["name"] = toGodot(u16ToUtf8(e.hero.name));
		d["unique_id"] = toGodot(e.hero.uniqueID);
		d["system"] = e.system;
		d["class"] = (int64_t)e.hero.classIndex;
		d["subclass"] = (int64_t)e.hero.subClassIndex;
		const std::vector<std::uint8_t> bytes = e.hero.save();
		PackedByteArray record;
		record.resize((int64_t)bytes.size());
		if (!bytes.empty())
		{
			memcpy(record.ptrw(), bytes.data(), bytes.size());
		}
		d["record"] = record;
		out.append(d);
	}
	return out;
}

bool AptMenuPlayer::create_a_hero_type_name(const String &name)
{
	AptCreateAHero *screen = m->shell && m->shell->shell ? dynamic_cast<AptCreateAHero *>(m->shell->shell->findScreenByFilename("CreateAHero.apt")) : nullptr;
	if (!screen)
	{
		return false;
	}
	std::u16string u;
	const CharString utf = name.utf8();
	for (const char *p = utf.get_data(); *p; ++p) // ASCII names (the automation's)
	{
		u.push_back((char16_t)(unsigned char)*p);
	}
	m->dirty = true;
	return screen->typeName(u);
}

void AptMenuPlayer::set_create_a_hero_view_texture(const Ref<Texture2D> &texture)
{
	g_cahViewHook.texture = texture;
	if (texture.is_valid())
	{
		if (!m->nativeHook)
		{
			m->nativeHook = &g_cahViewHook;
		}
	}
	else if (m->nativeHook == &g_cahViewHook)
	{
		m->nativeHook = nullptr;
	}
}

Dictionary AptMenuPlayer::create_a_hero_save_folder(const String &rotwkInstall) const
{
	Dictionary out;
	out["ok"] = false;
	std::vector<std::uint8_t> bytes;
	const std::string path = toNative(rotwkInstall) + "/gi.dat";
	{
		std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
		if (!in)
		{
			out["error"] = toGodot("cannot read " + path + " (RW 0xAAA6C0 reads the install's gi.dat for the user data folder's name)");
			return out;
		}
		bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	std::string error;
	const std::string leaf = UserDataFolder::leafName(bytes, &error);
	if (leaf.empty())
	{
		out["error"] = toGodot(error);
		return out;
	}
	const std::string userData = UserDataFolder::userDataFolder(toNative(OS::get_singleton()->get_data_dir()), leaf, '/');
	out["ok"] = true;
	out["leaf"] = toGodot(leaf);
	out["user_data"] = toGodot(userData);
	out["folder"] = toGodot(UserDataFolder::heroSaveFolder(userData, '/'));
	return out;
}

bool AptMenuPlayer::set_lan(Object *worldObject)
{
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (!m->shell)
	{
		return false;
	}
	m->shell->environment.lan = world ? world->lan() : nullptr;
	return m->shell->environment.lan != nullptr;
}

Dictionary AptMenuPlayer::get_member(int level, const String &path, const String &name)
{
	Dictionary r;
	r["found"] = false;
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	AptCharacterInst *inst = root ? (path.is_empty() ? root : m->A()->resolvePath(root, toNative(path))) : nullptr;
	AptValue v;
	if (inst && inst->getMember(toNative(name), v))
	{
		r["found"] = true;
		r["value"] = toGodot(v.toString());
	}
	return r;
}

Dictionary AptMenuPlayer::get_timeline_graph(const String &modeName, const String &path)
{
	Dictionary out;
	out["ok"] = false;
	if (!m->shell || !m->shell->shell)
	{
		return out;
	}
	AptTimeLine *screen = dynamic_cast<AptTimeLine *>(m->shell->shell->findScreenByFilename("TimeLine.apt"));
	if (!screen || !screen->data())
	{
		return out;
	}
	std::string name = toNative(modeName);
	if (name.empty() && screen->level() >= 0)
	{
		const Dictionary member = get_member(screen->level(), path, "_graphMode"); // RW 0x925808: the component's _graphMode (default "FinalScore")
		name = (bool)member["found"] ? toNative(String(member["value"])) : std::string("FinalScore");
	}
	const int mode = ScoreScreenData::graphMode(name);
	const ScoreScreenData::Axis &axis = screen->graph(mode);
	out["ok"] = true;
	out["mode"] = toGodot(name);
	out["level"] = screen->level();
	Dictionary ad;
	ad["step"] = axis.step;
	ad["top"] = axis.top;
	ad["samples"] = axis.samples;
	ad["max_value"] = axis.maxValue;
	PackedStringArray yl, xl;
	for (const std::string &l : axis.yLabels)
	{
		yl.push_back(toGodot(l));
	}
	for (const std::string &l : axis.xLabels)
	{
		xl.push_back(toGodot(l));
	}
	ad["y_labels"] = yl;
	ad["x_labels"] = xl;
	ad["total_time"] = toGodot(axis.totalTime);
	out["axis"] = ad;
	Array lines;
	const ScoreScreenData &d = *screen->data();
	const float sx = axis.samples > 1 ? 1.0f / (float)(axis.samples - 1) : 1.0f;
	for (size_t i = 0; i < d.entries.size(); ++i)
	{
		const ScoreScreenData::Entry &e = d.entries[i];
		Dictionary ld;
		ld["color"] = (int64_t)e.color;
		ld["result"] = e.result;
		ld["local"] = e.local;
		ld["name"] = String::utf8(e.name.c_str());
		ld["focus"] = screen->graphFocus((int)i);
		PackedVector2Array pts, marks;
		for (size_t f = 0; f < e.perFrame.size(); ++f)
		{
			pts.push_back(Vector2((float)f * sx, ScoreScreenData::graphValue(e.perFrame[f], mode) / axis.top));
		}
		for (int mark : e.fortressMarks)
		{
			const float v = mark >= 0 && (size_t)mark < e.perFrame.size() ? ScoreScreenData::graphValue(e.perFrame[(size_t)mark], mode) : 0.0f;
			marks.push_back(Vector2((float)mark * sx, v / axis.top));
		}
		ld["points"] = pts;
		ld["marks"] = marks;
		lines.push_back(ld);
	}
	out["lines"] = lines;
	return out;
}

// lane END-2: the statistics page of the score screen: the rows the data gives, the shown entries, and what the StatsList gadget lists
Dictionary AptMenuPlayer::get_timeline_stats()
{
	Dictionary out;
	out["ok"] = false;
	AptTimeLine *screen = m->shell && m->shell->shell ? dynamic_cast<AptTimeLine *>(m->shell->shell->findScreenByFilename("TimeLine.apt")) : nullptr;
	if (!screen)
	{
		return out;
	}
	out["ok"] = true;
	Array rows;
	for (const ScoreScreenData::StatRow &row : screen->statRows())
	{
		Dictionary rd;
		rd["label"] = String::utf8(row.label.c_str());
		rd["shown"] = row.shown;
		rd["best"] = row.best;
		PackedStringArray texts;
		PackedFloat32Array values;
		for (const ScoreScreenData::StatCell &c : row.values)
		{
			texts.push_back(String::utf8(c.text.c_str()));
			values.push_back(c.value);
		}
		rd["texts"] = texts;
		rd["values"] = values;
		rows.push_back(rd);
	}
	out["rows"] = rows;
	PackedInt32Array shown;
	for (int i : screen->shownColumns())
	{
		shown.push_back(i);
	}
	out["shown"] = shown;
	out["fills"] = screen->statsListFills();
	Array listed;
	if (GameWindow *list = screen->statsList())
	{
		const int n = GadgetListBoxGetNumEntries(list), cols = GadgetListBoxGetNumColumns(list);
		out["columns"] = cols;
		int w = 0, h = 0;
		list->winGetSize(&w, &h);
		out["size"] = Vector2i(w, h);
		PackedInt32Array widths, heights;
		for (int c = 0; c < cols; ++c)
		{
			widths.push_back(GadgetListBoxGetColumnWidth(list, c));
		}
		if (const ListboxData *ld = static_cast<const ListboxData *>(list->winGetUserData()))
		{
			for (const ListEntryRow &row : ld->listData)
			{
				heights.push_back(row.height);
			}
			out["list_length"] = ld->listLength;
			out["display_height"] = ld->displayHeight;
		}
		out["column_widths"] = widths;
		out["row_heights"] = heights;
		for (int r = 0; r < n; ++r)
		{
			Array cells;
			for (int c = 0; c < cols; ++c)
			{
				::Color color = 0;
				const UnicodeString t = GadgetListBoxGetTextAndColor(list, &color, r, c);
				Dictionary cd;
				cd["text"] = toGodot(loadScreenU16ToUtf8(t));
				cd["color"] = (int64_t)color;
				cells.push_back(cd);
			}
			listed.push_back(cells);
		}
	}
	out["listed"] = listed;
	return out;
}

// lane END-2: QuitMenu.apt over the live game. request.kind: "open" (request.context: GameWorld.get_quit_menu_context()), "close", "state"
Dictionary AptMenuPlayer::quit_menu(const Dictionary &request)
{
	Dictionary out;
	out["ok"] = false;
	if (!m->shell || !m->shell->shell)
	{
		out["error"] = "not in shell mode";
		return out;
	}
	ShellMode &sm = *m->shell;
	const std::string kind = toNative(String(request.get("kind", "")));
	if (kind == "open")
	{
		if (sm.quitMenu)
		{
			out["error"] = "the quit menu is open";
			return out;
		}
		const Dictionary c = request.get("context", Dictionary());
		QuitMenuContext &q = sm.quitMenuContext;
		q = QuitMenuContext();
		q.inGame = (bool)c.get("in_game", false);
		q.gameMode = (int)(int64_t)c.get("mode", 2);
		q.gameKind = (int)(int64_t)c.get("kind", 3);
		q.replay = (bool)c.get("replay", false);
		q.localPlayerDefeated = (bool)c.get("local_defeated", false);
		q.alliedVictory = (bool)c.get("allied_victory", false);
		sm.environment.quitMenu = &q;
		sm.quitMenu = std::make_unique<AptQuitMenu>(*sm.wm, *sm.shell, sm.environment);
		if (sm.quitMenu->level() < 0)
		{
			sm.quitMenu.reset();
			sm.environment.quitMenu = nullptr;
			out["error"] = "QuitMenu.apt could not be loaded";
			return out;
		}
		for (int i = 0; i < 30 && !sm.wm->isAptWindowLoaded(sm.quitMenu->level()); ++i)
		{
			tick(1.0 / 30.0); // the window manager loads a movie over its first updates
		}
		out["ok"] = true;
	}
	else if (kind == "close")
	{
		out["ok"] = sm.quitMenu != nullptr;
		sm.quitMenu.reset(); // the dtor sets APT:Pause back (RW 0x921A9B)
		sm.environment.quitMenu = nullptr;
	}
	else if (kind == "state")
	{
		out["ok"] = true;
	}
	else
	{
		out["error"] = "not a quit menu request: " + String(kind.c_str());
		return out;
	}
	out["open"] = sm.quitMenu != nullptr;
	if (sm.quitMenu)
	{
		out["level"] = sm.quitMenu->level();
		out["popup_type"] = toGodot(sm.quitMenu->restartPopupType());
		out["tooltip"] = toGodot(sm.quitMenu->restartTooltip());
		PackedStringArray disabled, requests;
		for (const std::string &b : sm.quitMenu->disabledButtons())
		{
			disabled.push_back(toGodot(b));
		}
		for (const std::string &b : sm.quitMenu->requests())
		{
			requests.push_back(toGodot(b));
		}
		out["disabled"] = disabled;
		out["requests"] = requests;
		out["initialized"] = sm.quitMenu->commandCalls("AptQuitMenu::OnInitialized");
		const std::string *t = sm.wm->aptText("APT:RestartOrSurrender");
		out["restart_text"] = t ? toGodot(*t) : String();
	}
	m->dirty = true;
	return out;
}

Dictionary AptMenuPlayer::fetch_text(const String &label)
{
	Dictionary r;
	bool exists = false;
	const std::u16string text = fetchOrMissing(m->shell ? static_cast<const GameTextSource *>(m->shell->text.get()) : nullptr, toNative(label), &exists);
	r["found"] = exists;
	r["text"] = toGodot(loadScreenU16ToUtf8(text));
	return r;
}

} // namespace godot
