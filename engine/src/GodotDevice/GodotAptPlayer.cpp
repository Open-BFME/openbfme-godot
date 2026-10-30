// OpenBFME. GPL-3.0.
// See GodotAptPlayer.h.

#include "GodotDevice/GodotAptPlayer.h"
#include "Common/PathIO.h"
#include <godot_cpp/classes/input.hpp>
#include "GodotDevice/GodotVideoStream.h"
#include "GameClient/GUI/PlatformIcons.h"
#include "GameClient/CameraSettings.h"
#include "GameNetwork/Transport.h"
#include "GodotDevice/GodotPackedTexture.h"
#include "GodotDevice/GodotAptView3D.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/INIException.h"
#include "GameClient/GameLODManager.h"
#include "GameClient/MessageStream/CommandMapList.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/GlobalLanguage.h"
#include "Common/INI.h"
#include "Common/INIException.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/AptScreens/AptImageMode.h"
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include "GameClient/FontSubstitution.h"
#include "GameClient/GdiFontMetrics.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/AptScreens/AptTimeLine.h"
#include "GameClient/GUI/CursorTooltip.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/AptScreens/AptInGameChat.h"
#include "GameClient/GUI/AptScreens/AptSaveLoad.h"
#include "GameClient/GUI/SaveLoadInfo.h"
#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/UserDataFolder.h"
#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/MenuNavigator.h"
#include "GameClient/GUI/AptScreens/AptCampaignMenu.h"
#include "Common/AsciiString.h"
#include "GameClient/GUI/AptScreens/AptSkirmish.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/AptScreens/AptObjectivesMenu.h"
#include "GameClient/GUI/AptScreens/InGameNotificationBox.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/Shell/ShellLayering.h"
#include "GameClient/GUI/StillImageLoadScreen.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioLog.h"
#include "Common/Audio/GameAudio.h"
#include "GodotDevice/GodotGameStart.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotMouse.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFileSystem.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptOnlineHub.h"
#include "GameClient/GameRecords.h"
#include "GameNetwork/PlatformApiClient.h"
#include "GameClient/GUI/AptScreens/AptMessenger.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/ProfileSwitch.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
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
#include <tuple>
#include <type_traits>
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
	// lane ONLINEMENU-1: a quad without area draws nothing, and Godot's canvas refuses it ("Invalid polygon data, triangulation failed", every frame:
	// 1104 lines in the friends video's log once the FRIENDS page's entry had the focus)
	if (!(std::fabs(dest.size.x) > 0.0f && std::fabs(dest.size.y) > 0.0f))
	{
		return;
	}
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

static_assert(sizeof(Vector2) == 8 && std::is_trivially_copyable_v<Vector2>, "PackedVector2Array is copied as float pairs");
static_assert(sizeof(Color) == 16 && std::is_trivially_copyable_v<Color>, "PackedColorArray is copied as float quads");

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
		if (r.action == ShellAction::OpenProjectWebsite)
		{
			Ref<Resource> opener = ResourceLoader::get_singleton()->load("res://scripts/external_open.gd");
			const Error error = opener.is_valid() ? (Error)(int)opener->call("open_url", "https://openbfme.com") : ERR_UNAVAILABLE;
			events.push_back({ "project_website", "https://openbfme.com", std::to_string((int)error) });
			if (error != OK) { UtilityFunctions::push_error("Could not open https://openbfme.com: ", (int)error); }
			return;
		}
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
		// lane OPTS-4: a volume of the Options screen (RW 0x91F90D / 0x91EC52 / 0x91F4D1 / 0x91FC9C: TheAudio vslot 0xE8(type, value * 0.01)) is set in
		// the installed audio manager at once, so the sounds playing follow while a slider moves
		static const char *const kVolumeKeys[5] = { "SFXVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume" }; // RW 0xDA2298, by type
		for (int t = 0; t < 5; ++t)
		{
			if (key == kVolumeKeys[t])
			{
				if (AudioManager *audio = AudioApi::current())
				{
					AudioLog::Scope tag("options " + key);
					audio->setSliderVolume(t, (float)std::atof(value.c_str()) * 0.01f);
				}
				else
				{
					problems.insert("Options: no audio manager is installed, the volume " + key + " = " + value + " was not applied");
				}
			}
		}
	}
	// lane OPTS-4: the Options screen's volume samples through the installed audio manager (TheAudio); without one the request is reported
	std::set<std::string> problems;
	std::uint32_t addMiscAudioEvent(const std::string &field, int viewType) override
	{
		AudioManager *audio = AudioApi::current();
		if (!audio)
		{
			problems.insert("Options: no audio manager is installed, the sample " + field + " was not played");
			return 0;
		}
		const std::string &name = audio->miscAudio(field);
		if (name.empty())
		{
			return 0; // the slot is NoSound
		}
		AudioLog::Scope tag("options " + field);
		const AudioHandle handle = audio->addAudioEvent(AudioEventRTS(name, viewType));
		events.push_back({ "volume_sample", field, std::to_string((unsigned)handle) });
		return handle >= AHSV_FirstHandle ? (std::uint32_t)handle : 0;
	}
	void removeAudioEvent(std::uint32_t handle) override
	{
		AudioManager *audio = AudioApi::current();
		if (audio && handle >= AHSV_FirstHandle)
		{
			audio->removeAudioEvent((AudioHandle)handle);
		}
	}
	bool isCurrentlyPlaying(std::uint32_t handle) override
	{
		AudioManager *audio = AudioApi::current();
		return audio && handle >= AHSV_FirstHandle && audio->isCurrentlyPlaying((AudioHandle)handle);
	}
	// lane OPTS-5: TheDisplay's gamma (the brightness slider while it moves, Reset, Cancel): the device's brightness pass (game_options.gd set_gamma)
	void setDisplayGamma(float gamma) override
	{
		char buf[32];
		std::snprintf(buf, sizeof buf, "%.9g", (double)gamma);
		events.push_back({ "gamma", buf, "" });
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
		case ShellAction::ObjectivesReturnToGame: return "ObjectivesReturnToGame"; // lane CAMPUI-1
		case ShellAction::CreateAHeroExit: return "CreateAHeroExit";
		case ShellAction::OnlineGameStart: return "OnlineGameStart"; // lane ONLINE-4
		case ShellAction::SaveGameFile: return "SaveGameFile"; // lane SAVE-2
		case ShellAction::LoadGameFile: return "LoadGameFile";
		case ShellAction::DeleteGameFile: return "DeleteGameFile";
		case ShellAction::QuitMenuSave: return "QuitMenuSave";
		case ShellAction::QuitMenuLoad: return "QuitMenuLoad";
		case ShellAction::OpenProjectWebsite: return "OpenProjectWebsite";
		case ShellAction::CampaignMenuMainMenu: return "CampaignMenuMainMenu"; // lane CAMPSAVE-1
		case ShellAction::CampaignMenuSave: return "CampaignMenuSave";
		case ShellAction::CampaignMenuLoad: return "CampaignMenuLoad";
		case ShellAction::CampaignMenuLastMission: return "CampaignMenuLastMission";
		case ShellAction::CampaignMenuNextMission: return "CampaignMenuNextMission";
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
	typedef std::function<Ref<Font>(const std::string &, float, bool, float *)> FontFn; // name, point size, bold (GameFont::bold), the draw size
	explicit ShellFontMetrics(FontFn fn) : m_fn(std::move(fn)) {}
	int fontHeight(const GameFont &font) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, font.bold, &size);
		return f.is_valid() ? (int)std::ceil(f->get_height((int)std::lround(size))) : font.pointSize + 4;
	}
	int textWidth(const GameFont &font, const UnicodeString &text) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, font.bold, &size);
		if (f.is_null())
		{
			return (int)text.size() * (font.pointSize * 6 / 10 + 1);
		}
		return (int)std::ceil(f->get_string_size(toGodot(u16ToUtf8(text)), HORIZONTAL_ALIGNMENT_LEFT, -1, (int)std::lround(size)).x);
	}
	int wrappedHeight(const GameFont &font, const UnicodeString &text, int wrapWidth) override
	{
		float size = 0;
		Ref<Font> f = m_fn(font.name, (float)font.pointSize, font.bold, &size);
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
	SkirmishHonorsGame skirmishHonors; // lane OWNERFIX-3: the skirmish game the score screen records the battle honors of
	std::unique_ptr<SkirmishPreferences> profiles; // lane MENU-1: Skirmish.ini, the profiles a player made (GUI/Skirmish/SkirmishPreferences.h)
	std::unique_ptr<AptScreen> guiFX; // lane END-1: GuiFX.apt, loaded for the end screen (not on the shell stack)
	std::unique_ptr<ShellCursorTooltip> cursorTooltip; // lane HOVER-1: the menus' tool tips (GuiFX.apt's ToolTip, CursorTooltip.h)
	std::unique_ptr<AptDisconnectScreen> disconnect; // lane MP-2: DisconnectScreen.apt while the disconnect screen is shown
	Array disconnectActions;                         // lane MP-2: the buttons pressed, for take_disconnect_actions
	InGameChat chatModel;                            // lane CHAT-1: the chat window's draft between opens
	std::unique_ptr<AptInGameChat> chat;             // lane CHAT-1: InGameChat.apt while the chat window is up
	Array chatSends;                                 // lane CHAT-1: the lines Send took, for ingame_chat_update
	SaveLoadInfo saveLoad;                           // lane MP-2: what SaveLoad.apt lists
	QuitMenuContext quitMenuContext;    // lane END-2: the game QuitMenu.apt is opened over
	CampaignMenuContext campaignMenu;   // lane CAMPSAVE-1: what CampaignMenu.apt shows
	std::unique_ptr<AptQuitMenu> quitMenu; // lane END-2: QuitMenu.apt, an in-game overlay (RW 0x75E3B7), not on the shell stack
	std::unique_ptr<InGameNotificationBoxMovieClip> notificationBox; // lane CAMPUI-1: InGameNotificationBox.apt (InGameUI + 0x9CC), a game's level
	NotificationBoxSettings notificationSettings;                     // lane CAMPUI-1: Data\INI\InGameNotificationBox.ini
	bool notificationSettingsLoaded = false;
	ShellEnvironment environment;
	PlayerStatusInfo playerStatus; // lane HUD-5: the players screen's Status rows
	ObjectivesScreenInfo objectivesScreen; // lane CAMPUI-1: what Objectives.apt lists
	OptionPreferences options; // lane UI-2: the user data folder's Options.ini
	GlobalLanguage language;   // lane UI-4: language.ini's Language block (the credits roll's fonts)
	GameLODManager gameLOD;    // lane PLAY-2: GameLOD.ini's StaticGameLOD presets (the Options screen's advanced page); lane OPTS-2: the level in effect
	std::string detailStop;    // lane OPTS-2: GameLODManager::init's stop (S-2481), empty when Options.ini named the level
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::unique_ptr<WindowManager> wm;
	std::unique_ptr<AptGadgetLayer> layer;
	std::unique_ptr<Shell> shell;
	std::unique_ptr<MenuNavigator> nav; // lane OPTS-6: keyboard / controller navigation of the menus
	std::set<int> navSwallowed;         // lane OPTS-6: the keys whose press the navigation took (their release is its too)
	std::unique_ptr<FontMetricsSource> metrics;
	std::string lastTop;
	std::vector<NewGameMessage> pendingNewGames;
	std::set<std::string> gadgetErrors; // textures a gadget image needed and could not load, unresolved image names
	std::set<std::string> deviceUnverified; // lane UI-2: inferences of the device the report lists (S-1482)
	CreateAHeroHeroList cahHeroes;      // lane CAH-1: the builder's and the lobby's heroes
	CreateAHeroScreenContext cahContext;
	std::mt19937 cahRandom{ 0x43414831u }; // lane CAH-1: the builder's client random (AppearanceRandom, RW 0x6D32E4): never logic state
	// lane ONLINE-4: the lobby client OnlineShell.apt drives (the shell's screens keep pointers to both: the shell goes first)
	std::unique_ptr<OnlineLobbyClient> online;
	ProfileSwitch profileSwitch; // lane UBERJOIN-1: the lobby screens' performance-mode switch (the world's), set by set_lan / set_online
	OnlineShellContext onlineCtx;
	// lane ONLINEUX-1: the started games the player left in this run (kept across set_online: Log Out and back in still offers none of them)
	std::shared_ptr<std::set<std::string>> onlineLeftGames = std::make_shared<std::set<std::string>>();
	std::string onlineUrl;
	// lane ONLINEHUB-1: the platform API's client and the recorded games (the screens keep pointers to both: the shell goes first)
	std::unique_ptr<PlatformApiClient> platform;
	std::vector<GameRecord> records;
	OnlineHubContext hubCtx;
	std::string platformUrl;
	~ShellMode()
	{
		nav.reset();
		disconnect.reset();
		chat.reset(); // lane CHAT-1
		if (shell)
		{
			shell->clearOverlay(); // lane OPTS-6
		}
		quitMenu.reset();
		notificationBox.reset(); // lane CAMPUI-1
		cursorTooltip.reset();
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
	int mainMenuGroup = -1, mainMenuItem = -1;
	PackedStringArray mainMenuGroups, mainMenuEntries;
	String mainMenuFocus;
	bool cahGuardShown = false;   ///< lane CAHPIC-1: the last picture showed Create-a-Hero's RenderPictureGuard clip
	bool cahMapShown = false;     ///< lane CAHPIC-1: ... and its DrawMapComponent clip (the 3D view)
	bool cahPicturePending = false; ///< lane CAHPIC-1: a picture is taken from the next frame drawn
	std::u16string cahPictureName;  ///< lane CAHPIC-1: its file name (no extension)
	Ref<RetailFileSystem> fs;
	Ref<RetailFileSystem> presentationFs;
	AptView3DViewers view3d; // lane UI-2: the View3D render objects (the end screen's rings, the menu frame)
	std::unique_ptr<AptArchiveFileSource> source;
	std::unique_ptr<AptArchiveFileSource> originalSource;
	AptGodotHost host;
	std::unique_ptr<Apt> apt;
	// lane HUD-1: an Apt player owned by a window manager the device layer runs (the in-game HUD's); null otherwise
	WindowManager *external = nullptr;
	AptNativeHook *nativeHook = nullptr;
	bool inputForwarded = false;
	std::shared_ptr<GodotMouse> godotMouse;
	bool openbfmeAvailable = false; // lane OPTS-3: set_openbfme_available
	std::unique_ptr<AptTextureStore> textures;
	std::unique_ptr<AptTextureStore> originalTextures;
	std::set<std::string> presentationImageNames;
	std::unique_ptr<VideoPlayer> presentationVideos;
	GameTextTable strings;
	FontSubstitution fonts;
	std::map<std::string, Ref<ImageTexture>> godotTextures;
	std::map<int, Ref<ShaderMaterial>> imageModes;
	AptImageReplacements imageReplacements;
	std::map<std::string, Ref<Texture2D>> brandTextures;
	std::map<std::string, Ref<FontFile>> fontByName; // lower-cased family name and file stem
	std::map<const Font *, GdiFaceMetrics> gdiFaces;  // lane UI-6: each corpus font's GDI metrics (GameClient/GdiFontMetrics.h)
	std::map<const Font *, const Font *> boldBase;    // lane UI-6: a synthetic bold variation -> its font file
	std::map<std::tuple<const Font *, bool, int>, Ref<FontVariation>> cellFonts; // lane UI-6: (font file, bold, pixel size) -> the GDI cell variation
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
		size_t op;   ///< index in the segment's ops (drawnList.ops[segment.opBegin + op])
		size_t item; ///< index in the segment's items
		RID parent;
	};
	// lane HUDREDRAW-1: the drawn list's segments (AptCanvasSegments: an op, or a mask bracket), each under a canvas item of its own (a child of the player's
	// item after the backdrop, in list order): a frame whose list changed in some segments draws those again and keeps the others' items
	struct Segment
	{
		int firstLevel = 0, lastLevel = 0;
		size_t opBegin = 0, opEnd = 0; ///< the range in drawnList.ops
		RID parent;                    ///< a child of the player's item (draw index 1 + its place: the backdrop is 0)
		std::vector<RID> items;        ///< the items drawn under parent, in creation order (their draw index)
		std::vector<NativeSlot> nativeSlots;
		std::vector<NativeSlot> callbackSlots; // lane UI-4: the clips an engine render callback draws (WindowManager::renderCallback)
	};
	std::vector<Segment> segments;
	struct LevelCounts
	{
		std::string movie;
		std::uint64_t redrawnFrames = 0, keptFrames = 0; ///< rebuilds that drew some of the level's segments again / kept all of them
		std::uint64_t segmentsRedrawn = 0;               ///< its segments drawn again, summed over the rebuilds
		std::uint64_t segments = 0;                      ///< in the last rebuild
	};
	std::map<int, LevelCounts> levelCounts; // by the segment's first level
	std::uint64_t partialFrames = 0;        // rebuilds that drew some segments again and kept the others
	// lane MENU-1: the canvas item at each gadget component's placeholder depth (drawCanvas): drawGadgets puts the window's commands under it, so the
	// movie's later content (a popup box) covers the gadget as in retail, which draws the window inside the movie's render (RW 0x814BEC -> 0x6C1B52)
	std::map<const GameWindow *, RID> gadgetAnchors;
	int unanchoredGadgets = 0; // top-level gadget windows drawn after the movies (no placeholder this frame, an open drop-down list, engine gadgets)
	// lane MENU-1: the picture loading screen (GameClient/GUI/StillImageLoadScreen.h) while it is up
	bool stillLoadOn = false;
	StillImageLoadScreenLayout stillLoad;
	std::uint64_t stillLoadStartUs = 0;
	double stillProgress = -1.0;   ///< lane SAVE-4e: an old save's catch-up (set_still_load_progress); < 0: none
	std::string stillProgressText; ///< lane SAVE-4e
	std::uint64_t unchangedFrames = 0;
	std::vector<RID> itemPool; // lane SELPERF-1: the last list's canvas items, reused by the next full redraw (freed after it)
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
	// the Godot texture of a gadget mapped image's file, loaded as the gadget layer draws it (drawGadgets); null when it cannot be loaded (the reason goes to
	// gadgetErrors). Lane OPTS-1: also the OpenBFME options screen's skin (shell_mapped_image)
	Ref<ImageTexture> imageTexture(ShellMode &sm, const ::Image *img, const std::string &imageName)
	{
		// lane UI-2: an image the engine made from a file (IMAGE_STATUS_RAW_TEXTURE: the lobby's map preview, RW 0x70292F) names the file's path
		const bool rawFile = (img->status & IMAGE_STATUS_RAW_TEXTURE) != 0;
		const bool originalImage = presentationFs.is_valid() && presentationFs->is_shell_source() && originalTextures &&
			presentationImageNames.find(lowerAscii(imageName)) == presentationImageNames.end();
		const std::string key = (originalImage ? "original:" : "") + (rawFile ? "file:" + img->filename : img->filename);
		auto it = godotTextures.find(key);
		if (it == godotTextures.end() && (img->status & IMAGE_STATUS_ENGINE_RESOURCE))
		{
			// lane PLATFORMICON-1: the game's own artwork (the platform icons, godot/assets/platform_icons), a resource of the Godot project, not of the
			// retail archives. A missing or unreadable one is reported through gadgetErrors, never drawn as something else
			Ref<Texture2D> res = ResourceLoader::get_singleton()->load(toGodot(img->filename));
			Ref<godot::Image> pixels = res.is_valid() ? res->get_image() : Ref<godot::Image>();
			if (pixels.is_null() || pixels->is_empty())
			{
				sm.gadgetErrors.insert("resource '" + img->filename + "' of image '" + imageName + "' cannot be loaded");
				return Ref<ImageTexture>();
			}
			it = godotTextures.emplace(key, ImageTexture::create_from_image(pixels)).first;
			textureLoads += 1;
		}
		if (it == godotTextures.end())
		{
			AptTextureStore *store = textures.get();
			AptFileSource *textureSource = source.get();
			if (originalImage)
			{
				store = originalTextures.get();
				textureSource = originalSource.get();
			}
			const AptTextureStore::Entry &entry = rawFile ? store->getFile(img->filename) : store->get(img->filename);
			if ((!entry.ok || entry.rgba.empty()) && !rawFile)
			{
				// lane UI-2: a mapped image naming a .tga that the install packs as DDS (ScrollShroud.tga -> art\compiledtextures\sc\scrollshroud.dds),
				// as the HUD device loads it (GodotInGameHud.cpp textureFor): the W3D loader takes the packed file for the requested name
				bool paired = false;
				Ref<godot::Image> packed = loadPackedTexture(*textureSource, store->packedTextureStem(img->filename), &paired);
				if (paired)
				{
					sm.deviceUnverified.insert("[S-1482] texture " + img->filename + ": the packed .jpg's colour with its .png as the alpha (the loader's merge of the two streams was not read)");
				}
				if (packed.is_valid())
				{
					it = godotTextures.emplace(key, ImageTexture::create_from_image(packed)).first;
					textureLoads += 1;
				}
			}
			if (it == godotTextures.end() && (!entry.ok || entry.rgba.empty()))
			{
				sm.gadgetErrors.insert("texture '" + img->filename + "' of image '" + imageName + "': " + entry.error);
				return Ref<ImageTexture>();
			}
			if (it == godotTextures.end())
			{
				PackedByteArray px;
				px.resize((int64_t)entry.rgba.size());
				memcpy(px.ptrw(), entry.rgba.data(), entry.rgba.size());
				Ref<godot::Image> image = godot::Image::create_from_data(entry.width, entry.height, false, godot::Image::FORMAT_RGBA8, px);
				Ref<ImageTexture> texture = ImageTexture::create_from_image(image);
				it = godotTextures.emplace(key, texture).first;
				if (!rawFile)
				{
					store->releasePixels(img->filename);
				}
				textureLoads += 1;
			}
		}
		return it->second;
	}

	// the font a request (name, stage pixel size) draws with: fontsubstitution.ini, then the corpus fonts, else the engine default (reported)
	Ref<Font> fontFor(const std::string &name, float size, float *drawSize, bool bold = false)
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
			return styled(fit->second, bold);
		}
		fontFallbacks.insert(r.name);
		return styled(ThemeDB::get_singleton()->get_fallback_font(), bold);
	}

	// lane UI-4 (Sol r1): a GameFont's bold. RotWK makes its fonts with GDI (the W3D font library's CreateFont, FW_BOLD for a bold GameFont); the archives
	// carry only the regular faces (AlbertusMT.otf, OmniaLTStd.ttf), so GDI synthesises the bold: thicker strokes and one pixel more a glyph.
	// INFERENCE (S-2520): Godot's embolden 1.0 for the strokes, glyph spacing + 1 for the advance (GDI's simulated-bold overhang)
	static constexpr float kSyntheticBoldEmbolden = 1.0f;
	std::map<const Font *, Ref<FontVariation>> boldFonts;
	std::uint64_t callbackTextDraws[2] = { 0, 0 }; // the render callbacks' texts drawn regular / bold (font_metrics reports them)
	// lane UI-4: the texts drawn outside the canvas ops (the gadgets' and the render callbacks'), by source and text, for checks (drawn_texts)
	struct DrawnText
	{
		std::string source;
		String text;
		Color color;
		Rect2 window;
	};
	std::map<std::string, DrawnText> drawnTexts;
	void noteDrawnText(const char *source, const String &text, const Color &color, const Rect2 &window)
	{
		if (drawnTexts.size() < 4000)
		{
			drawnTexts[std::string(source) + "|" + toNative(text)] = DrawnText{ source, text, color, window };
		}
	}
	Ref<Font> styled(const Ref<Font> &base, bool bold)
	{
		if (!bold || base.is_null())
		{
			return base;
		}
		auto it = boldFonts.find(base.ptr());
		if (it == boldFonts.end())
		{
			Ref<FontVariation> v;
			v.instantiate();
			v->set_base_font(base);
			v->set_variation_embolden(kSyntheticBoldEmbolden);
			v->set_spacing(TextServer::SPACING_GLYPH, 1);
			it = boldFonts.emplace(base.ptr(), v).first;
			boldBase[v.ptr()] = base.ptr();
		}
		return it->second;
	}

	// Lane UI-6 (GitGut's report on preview.6: the main menu's button texts sat a few % too high): `font` (a corpus font or its synthetic bold) at
	// `px` pixels with RotWK's GDI text cell (GameClient/GdiFontMetrics.h, RW 0x553FD0 / 0x556579): the ascent is the cell's baseline (external
	// leading / 2 + tmAscent, both reduced by the supersampling) and the height the char height, so every string placed by its ascent and height (the
	// Apt fields, the gadgets, the HUD texts) and every line of a multi-line string sits where RotWK puts it. Godot's own ascent / descent are the
	// hhea ones: for Omnia LT Std (hhea 580 / 420, win 648 / 176, line gap 200) the baseline was 0.156 em higher than RotWK's. A font without
	// GDI metrics (the engine fallback font, already reported) is returned as it is.
	Ref<Font> cellFont(const Ref<Font> &font, int px)
	{
		if (font.is_null() || px <= 0)
		{
			return font;
		}
		const Font *file = font.ptr();
		bool bold = false;
		auto b = boldBase.find(file);
		if (b != boldBase.end())
		{
			file = b->second;
			bold = true;
		}
		auto face = gdiFaces.find(file);
		if (face == gdiFaces.end())
		{
			return font;
		}
		const auto key = std::make_tuple(file, bold, px);
		auto it = cellFonts.find(key);
		if (it != cellFonts.end())
		{
			return it->second;
		}
		const GdiTextCell cell = GdiTextCellAt(face->second, px); // the font library's default supersampling, 4 (RW 0x6186E0)
		const int baseline = (int)std::lround(cell.baseline);
		Ref<FontVariation> v;
		v.instantiate();
		v->set_base_font(Ref<Font>(const_cast<Font *>(file)));
		if (bold)
		{
			v->set_variation_embolden(kSyntheticBoldEmbolden);
			v->set_spacing(TextServer::SPACING_GLYPH, 1);
		}
		// Godot's ascent and descent at px are whole pixels (FreeType's scaled metrics); the spacing makes them the cell's
		const int ascent = (int)std::lround(file->get_ascent(px)), descent = (int)std::lround(file->get_descent(px));
		v->set_spacing(TextServer::SPACING_TOP, baseline - ascent);
		v->set_spacing(TextServer::SPACING_BOTTOM, (cell.height - baseline) - descent);
		cellFonts.emplace(key, v);
		return v;
	}

	// string table, font substitution, the corpus fonts (boot and boot_shell); appends to `errors`
	void loadResources(Array &errorsOut)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!strings.loadRetail(*fs->archive_fs(), &error))
		{
			errorsOut.push_back(toGodot("presentation data/lotr.str: " + error));
		}
		if (presentationFs.is_valid() && presentationFs->archive_fs() != fs->archive_fs())
		{
			bytes.clear();
			error.clear();
			// a non-English install has the compiled lotr.csf instead (GameTextTable::loadRetail)
			// retail's order (RotWK 2.01 RW 0x6E7E3D -> RW 0x6E7D9E): data\lotr.str, else Lotr.csf
			const bool csf = !originalSource->fileExists("data/lotr.str") && originalSource->fileExists("lotr.csf");
			if (csf ? (!originalSource->readFile("lotr.csf", bytes, &error) || !strings.parseCSF(bytes.data(), bytes.size(), &error))
					: (!originalSource->readFile("data/lotr.str", bytes, &error) || !strings.parse(bytes, &error)))
			{
				errorsOut.push_back(toGodot("original data/lotr.str: " + error));
			}
		}
		imageReplacements.clear();
		brandTextures.clear();
		Ref<Resource> brand = ResourceLoader::get_singleton()->load("res://assets/brand/online_branding.tres");
		if (brand.is_null() || !brand->has_meta("images") || !brand->has_meta("strings"))
		{
			errorsOut.push_back("Missing or invalid online branding resource");
		}
		else
		{
			const Array images = brand->get_meta("images");
			for (int i = 0; i < images.size(); ++i)
			{
				const Dictionary entry = images[i];
				const Ref<Texture2D> texture = entry.get("texture", Variant());
				const String movie = entry.get("movie", "");
				const int id = entry.get("image", -1);
				if (texture.is_null() || texture->get_width() <= 0 || texture->get_height() <= 0 || movie.is_empty() || id < 0)
				{
					errorsOut.push_back("Invalid online branding image entry");
					continue;
				}
				const std::string key = "brand:" + toNative(movie.to_lower()) + ":" + std::to_string(id);
				imageReplacements[{ toNative(movie.to_lower()), id }] = { key, (float)texture->get_width(), (float)texture->get_height() };
				brandTextures[key] = texture;
			}
			const Dictionary labels = brand->get_meta("strings");
			const Array keys = labels.keys();
			for (int i = 0; i < keys.size(); ++i)
			{
				strings.setOverride(toNative(String(keys[i])), toNative(String(labels[keys[i]])));
			}
		}
		if (!source->readFile("data/ini/fontsubstitution.ini", bytes, &error) || !fonts.parse(std::string(bytes.begin(), bytes.end()), &error))
		{
			errorsOut.push_back(toGodot("data/ini/fontsubstitution.ini: " + error));
		}
		std::vector<std::string> files;
		for (const char *pattern : { "*.ttf", "*.otf" })
		{
			FilenameList list;
			(presentationFs.is_valid() ? presentationFs->archive_fs() : fs->archive_fs())->getFileListInDirectory(std::string(), std::string(), std::string(pattern), list, true);
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
			GdiFaceMetrics gdi;
			std::string gdiError;
			if (ReadGdiFaceMetrics(bytes.data(), bytes.size(), gdi, &gdiError))
			{
				gdiFaces[font.ptr()] = gdi;
			}
			else
			{
				errorsOut.push_back(toGodot(file + ": no GDI text metrics (" + gdiError + "); its texts are placed by the font's own line metrics (S-3700)"));
			}
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

	void collectPresentationImageNames(ArchiveFileSystem &archive, Array &errorsOut)
	{
		presentationImageNames.clear();
		INIEnvironment env;
		env.fileSystem = &archive;
		env.blocks.registerBlock("MappedImage", [this](INI *ini) {
			const std::string name = ini->getNextToken();
			presentationImageNames.insert(lowerAscii(name));
			::Image image;
			ini->initFromINI(&image, ::Image::fieldParseTable());
		});
		try
		{
			INI ini(env);
			ini.loadDirectory("data\\ini\\mappedimages", true, INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			errorsOut.push_back(toGodot("presentation mapped images: " + std::string(e.what())));
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
	ClassDB::bind_method(D_METHOD("component_rect", "instance_path"), &AptMenuPlayer::component_rect);
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
	ClassDB::bind_method(D_METHOD("get_layering_report"), &AptMenuPlayer::get_layering_report); // lane MENU-1
	ClassDB::bind_method(D_METHOD("choose_load_screen", "spec"), &AptMenuPlayer::choose_load_screen); // lane MENU-1
	ClassDB::bind_method(D_METHOD("show_still_load_screen", "picture"), &AptMenuPlayer::show_still_load_screen); // lane MENU-1
	ClassDB::bind_method(D_METHOD("hide_still_load_screen"), &AptMenuPlayer::hide_still_load_screen); // lane MENU-1
	ClassDB::bind_method(D_METHOD("set_still_load_progress", "percent", "text"), &AptMenuPlayer::set_still_load_progress); // lane SAVE-4e
	ClassDB::bind_method(D_METHOD("get_still_load_screen"), &AptMenuPlayer::get_still_load_screen); // lane MENU-1
	ClassDB::bind_method(D_METHOD("shell_levels"), &AptMenuPlayer::shell_levels);             // lane CAMP-2
	ClassDB::bind_method(D_METHOD("level_drawn", "level"), &AptMenuPlayer::level_drawn);      // lane CAMP-2
	ClassDB::bind_method(D_METHOD("get_movies"), &AptMenuPlayer::get_movies);                 // lane CAMP-2
	ClassDB::bind_method(D_METHOD("shell_stack"), &AptMenuPlayer::shell_stack);
	ClassDB::bind_method(D_METHOD("get_option", "key"), &AptMenuPlayer::get_option);
	ClassDB::bind_method(D_METHOD("get_options"), &AptMenuPlayer::get_options);                  // lane OPTS-2
	ClassDB::bind_method(D_METHOD("set_option", "key", "value"), &AptMenuPlayer::set_option);    // lane OPTS-2
	ClassDB::bind_method(D_METHOD("save_options"), &AptMenuPlayer::save_options);               // lane OPTS-2
	ClassDB::bind_method(D_METHOD("get_detail_state"), &AptMenuPlayer::get_detail_state);       // lane OPTS-2
	ClassDB::bind_method(D_METHOD("get_command_map"), &AptMenuPlayer::get_command_map);         // lane OPTS-2
	ClassDB::bind_method(D_METHOD("options_page"), &AptMenuPlayer::options_page);               // lane OPTS-2
	ClassDB::bind_method(D_METHOD("set_openbfme_available", "available"), &AptMenuPlayer::set_openbfme_available); // lane OPTS-3
	ClassDB::bind_method(D_METHOD("openbfme_page_open"), &AptMenuPlayer::openbfme_page_open);
	ClassDB::bind_method(D_METHOD("openbfme_page_close"), &AptMenuPlayer::openbfme_page_close);
	ClassDB::bind_method(D_METHOD("openbfme_page_show", "spec"), &AptMenuPlayer::openbfme_page_show);
	ClassDB::bind_method(D_METHOD("openbfme_page_set_row", "row"), &AptMenuPlayer::openbfme_page_set_row);
	ClassDB::bind_method(D_METHOD("openbfme_page_events"), &AptMenuPlayer::openbfme_page_events);
	ClassDB::bind_method(D_METHOD("openbfme_page_state"), &AptMenuPlayer::openbfme_page_state);
	ClassDB::bind_method(D_METHOD("openbfme_page_choose", "key", "value"), &AptMenuPlayer::openbfme_page_choose);
	ClassDB::bind_method(D_METHOD("options_retail_rows"), &AptMenuPlayer::options_retail_rows); // lane OPTS-5
	ClassDB::bind_method(D_METHOD("openbfme_menu_finish", "accept"), &AptMenuPlayer::openbfme_menu_finish);
	ClassDB::bind_method(D_METHOD("openbfme_menu_reset"), &AptMenuPlayer::openbfme_menu_reset);
	ClassDB::bind_method(D_METHOD("font_metrics", "name", "point_size", "bold", "text"), &AptMenuPlayer::font_metrics); // lane UI-4
	ClassDB::bind_method(D_METHOD("drawn_texts", "clear"), &AptMenuPlayer::drawn_texts); // lane UI-4
	ClassDB::bind_method(D_METHOD("gadget_draw_list"), &AptMenuPlayer::gadget_draw_list); // lane OPTS-4
	ClassDB::bind_method(D_METHOD("set_tribute_world", "world"), &AptMenuPlayer::set_tribute_world);
	ClassDB::bind_method(D_METHOD("set_player_status", "state"), &AptMenuPlayer::set_player_status); // lane HUD-5
	ClassDB::bind_method(D_METHOD("set_objectives_screen", "state"), &AptMenuPlayer::set_objectives_screen); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("notification_box", "request"), &AptMenuPlayer::notification_box); // lane CAMPUI-1
	ClassDB::bind_method(D_METHOD("shell_top_level"), &AptMenuPlayer::shell_top_level);
	ClassDB::bind_method(D_METHOD("shell_invoke", "level", "function", "args"), &AptMenuPlayer::shell_invoke, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("take_new_game"), &AptMenuPlayer::take_new_game);
	ClassDB::bind_method(D_METHOD("set_load_screen", "info"), &AptMenuPlayer::set_load_screen);
	ClassDB::bind_method(D_METHOD("set_load_progress", "percent"), &AptMenuPlayer::set_load_progress);
	ClassDB::bind_method(D_METHOD("set_load_screen_from_game", "resolved_game"), &AptMenuPlayer::set_load_screen_from_game);
	ClassDB::bind_method(D_METHOD("end_game_screen", "request"), &AptMenuPlayer::end_game_screen); // lane END-1
	ClassDB::bind_method(D_METHOD("disconnect_screen", "state"), &AptMenuPlayer::disconnect_screen); // lane MP-2
	ClassDB::bind_method(D_METHOD("take_disconnect_actions"), &AptMenuPlayer::take_disconnect_actions);
	ClassDB::bind_method(D_METHOD("ingame_chat_open", "mode", "receivers"), &AptMenuPlayer::ingame_chat_open); // lane CHAT-1
	ClassDB::bind_method(D_METHOD("ingame_chat_update"), &AptMenuPlayer::ingame_chat_update);
	ClassDB::bind_method(D_METHOD("ingame_chat_close"), &AptMenuPlayer::ingame_chat_close);
	ClassDB::bind_method(D_METHOD("ingame_chat_has_keys"), &AptMenuPlayer::ingame_chat_has_keys);
	ClassDB::bind_method(D_METHOD("ingame_chat_type", "text"), &AptMenuPlayer::ingame_chat_type);
	ClassDB::bind_method(D_METHOD("ingame_chat_command", "command"), &AptMenuPlayer::ingame_chat_command);
	ClassDB::bind_method(D_METHOD("disconnect_screen_chat", "line"), &AptMenuPlayer::disconnect_screen_chat);
	ClassDB::bind_method(D_METHOD("disconnect_screen_type", "text"), &AptMenuPlayer::disconnect_screen_type);
	ClassDB::bind_method(D_METHOD("set_save_load", "info"), &AptMenuPlayer::set_save_load);
	ClassDB::bind_method(D_METHOD("set_campaign_menu", "content"), &AptMenuPlayer::set_campaign_menu); // lane CAMPSAVE-1
	ClassDB::bind_method(D_METHOD("unlock_bonus_campaign"), &AptMenuPlayer::unlock_bonus_campaign);
	ClassDB::bind_method(D_METHOD("is_bonus_campaign_unlocked"), &AptMenuPlayer::is_bonus_campaign_unlocked);
	ClassDB::bind_method(D_METHOD("set_save_folder", "folder"), &AptMenuPlayer::set_save_folder);
	ClassDB::bind_method(D_METHOD("save_load_saved", "ok", "error"), &AptMenuPlayer::save_load_saved); // lane SAVE-2
	ClassDB::bind_method(D_METHOD("save_load_refused", "name", "error"), &AptMenuPlayer::save_load_refused);
	ClassDB::bind_method(D_METHOD("set_score_screen_from_world", "world"), &AptMenuPlayer::set_score_screen_from_world);
	ClassDB::bind_method(D_METHOD("set_lan", "world"), &AptMenuPlayer::set_lan); // lane MP-2
	ClassDB::bind_method(D_METHOD("set_online", "world", "options"), &AptMenuPlayer::set_online); // lane ONLINE-4
	ClassDB::bind_method(D_METHOD("online_take_requests"), &AptMenuPlayer::online_take_requests);
	ClassDB::bind_method(D_METHOD("online_deliver", "id", "status", "body", "error"), &AptMenuPlayer::online_deliver);
	ClassDB::bind_method(D_METHOD("online_status"), &AptMenuPlayer::online_status);
	ClassDB::bind_method(D_METHOD("online_apply", "spec"), &AptMenuPlayer::online_apply);
	ClassDB::bind_method(D_METHOD("online_social_status"), &AptMenuPlayer::online_social_status);
	ClassDB::bind_method(D_METHOD("online_social_events"), &AptMenuPlayer::online_social_events);
	ClassDB::bind_method(D_METHOD("online_social_apply", "spec"), &AptMenuPlayer::online_social_apply);
	ClassDB::bind_method(D_METHOD("online_messenger_sync", "spec"), &AptMenuPlayer::online_messenger_sync);
	ClassDB::bind_method(D_METHOD("online_ask", "key", "title", "text"), &AptMenuPlayer::online_ask);
	ClassDB::bind_method(D_METHOD("online_answers"), &AptMenuPlayer::online_answers);
	ClassDB::bind_method(D_METHOD("online_asking", "key"), &AptMenuPlayer::online_asking);
	ClassDB::bind_method(D_METHOD("quit_menu_cheat_button", "label"), &AptMenuPlayer::quit_menu_cheat_button); // lane CHEAT-1
	ClassDB::bind_method(D_METHOD("quit_menu_cheat_page_show", "spec"), &AptMenuPlayer::quit_menu_cheat_page_show);
	ClassDB::bind_method(D_METHOD("quit_menu_cheat_page_close"), &AptMenuPlayer::quit_menu_cheat_page_close);
	ClassDB::bind_method(D_METHOD("quit_menu_cheat_events"), &AptMenuPlayer::quit_menu_cheat_events);
	ClassDB::bind_method(D_METHOD("quit_menu_cheat_state"), &AptMenuPlayer::quit_menu_cheat_state);
	ClassDB::bind_method(D_METHOD("online_cancel_ask", "key"), &AptMenuPlayer::online_cancel_ask);
	ClassDB::bind_method(D_METHOD("online_start_failed", "error"), &AptMenuPlayer::online_start_failed); // Sol r1
	ClassDB::bind_method(D_METHOD("online_game_failed", "text"), &AptMenuPlayer::online_game_failed); // lane ONLINEUX-1
	ClassDB::bind_method(D_METHOD("online_set_counts", "counts"), &AptMenuPlayer::online_set_counts); // lane ONLINEUX-1
	ClassDB::bind_static_method("AptMenuPlayer", D_METHOD("online_redact", "text"), &AptMenuPlayer::online_redact);
	ClassDB::bind_static_method("AptMenuPlayer", D_METHOD("default_options_path"), &AptMenuPlayer::default_options_path); // lane DEVPORT-8
	ClassDB::bind_static_method("AptMenuPlayer", D_METHOD("read_options_file", "path"), &AptMenuPlayer::read_options_file);
	// lane ONLINEHUB-1
	ClassDB::bind_method(D_METHOD("set_platform", "options"), &AptMenuPlayer::set_platform);
	ClassDB::bind_method(D_METHOD("set_share_results", "share"), &AptMenuPlayer::set_share_results);
	ClassDB::bind_method(D_METHOD("platform_take_requests"), &AptMenuPlayer::platform_take_requests);
	ClassDB::bind_method(D_METHOD("platform_deliver", "id", "status", "body", "error"), &AptMenuPlayer::platform_deliver);
	ClassDB::bind_method(D_METHOD("platform_events"), &AptMenuPlayer::platform_events);
	ClassDB::bind_method(D_METHOD("set_game_records", "text"), &AptMenuPlayer::set_game_records);
	ClassDB::bind_method(D_METHOD("record_game", "json"), &AptMenuPlayer::record_game);
	ClassDB::bind_method(D_METHOD("platform_status"), &AptMenuPlayer::platform_status);
	ClassDB::bind_method(D_METHOD("platform_apply", "spec"), &AptMenuPlayer::platform_apply);
	ClassDB::bind_method(D_METHOD("set_create_a_hero", "world", "profile_dir"), &AptMenuPlayer::set_create_a_hero); // lane CAH-1
	ClassDB::bind_method(D_METHOD("get_create_a_hero_view"), &AptMenuPlayer::get_create_a_hero_view);
	ClassDB::bind_method(D_METHOD("get_create_a_hero_heroes"), &AptMenuPlayer::get_create_a_hero_heroes);
	ClassDB::bind_method(D_METHOD("set_create_a_hero_portrait", "image"), &AptMenuPlayer::set_create_a_hero_portrait);
	ClassDB::bind_method(D_METHOD("set_create_a_hero_view_texture", "texture"), &AptMenuPlayer::set_create_a_hero_view_texture);
	ClassDB::bind_method(D_METHOD("create_a_hero_save_folder", "rotwk_install"), &AptMenuPlayer::create_a_hero_save_folder); // lane CAH-2
	ClassDB::bind_method(D_METHOD("create_a_hero_type_name", "name"), &AptMenuPlayer::create_a_hero_type_name);
	ClassDB::bind_method(D_METHOD("shell_fscommand", "command", "argument"), &AptMenuPlayer::shell_fscommand);
	ClassDB::bind_method(D_METHOD("get_timeline_graph", "mode", "path"), &AptMenuPlayer::get_timeline_graph);
	ClassDB::bind_method(D_METHOD("get_member", "level", "path", "name"), &AptMenuPlayer::get_member);
	ClassDB::bind_method(D_METHOD("set_clip_property", "level", "path", "name", "value"), &AptMenuPlayer::set_clip_property); // lane DEVPORT-8
	ClassDB::bind_method(D_METHOD("get_timeline_stats"), &AptMenuPlayer::get_timeline_stats); // lane END-2
	ClassDB::bind_method(D_METHOD("quit_menu", "request"), &AptMenuPlayer::quit_menu);
	ClassDB::bind_method(D_METHOD("get_cursor_tooltip"), &AptMenuPlayer::get_cursor_tooltip); // lane HOVER-1
	ClassDB::bind_method(D_METHOD("list_buttons", "level"), &AptMenuPlayer::list_buttons);
	ClassDB::bind_method(D_METHOD("menu_controller_section", "delta"), &AptMenuPlayer::menu_controller_section);
	ClassDB::bind_method(D_METHOD("menu_input", "event"), &AptMenuPlayer::menu_input); // lane OPTS-6
	ClassDB::bind_method(D_METHOD("open_options", "network_enabled", "allow_resolution", "allow_advanced", "advanced_only"), &AptMenuPlayer::open_options);
	ClassDB::bind_method(D_METHOD("menu_has_input"), &AptMenuPlayer::menu_has_input);
	ClassDB::bind_method(D_METHOD("modal_menu"), &AptMenuPlayer::modal_menu);       // lane SAVE-UI1 / OPTS-6
	ClassDB::bind_method(D_METHOD("menu_escape", "pressed"), &AptMenuPlayer::menu_escape);
	ClassDB::bind_method(D_METHOD("menu_pointer_moved"), &AptMenuPlayer::menu_pointer_moved);
	ClassDB::bind_method(D_METHOD("menu_nav_state"), &AptMenuPlayer::menu_nav_state);
	ClassDB::bind_method(D_METHOD("hit_test_stage", "stage"), &AptMenuPlayer::hit_test_stage);
	ClassDB::bind_method(D_METHOD("gadget_pick", "stage"), &AptMenuPlayer::gadget_pick);
	ClassDB::bind_method(D_METHOD("list_gadgets"), &AptMenuPlayer::list_gadgets);   // lane MENUWORK-1
	ClassDB::bind_method(D_METHOD("take_commands"), &AptMenuPlayer::take_commands); // lane MENUWORK-1
	ClassDB::bind_method(D_METHOD("apt_text", "name"), &AptMenuPlayer::apt_text);   // lane MENUWORK-1
	ClassDB::bind_method(D_METHOD("screen_state"), &AptMenuPlayer::screen_state); // lane MENUWORK-1
	ClassDB::bind_method(D_METHOD("fetch_text", "label"), &AptMenuPlayer::fetch_text);
	ClassDB::bind_method(D_METHOD("shell_mapped_image", "name"), &AptMenuPlayer::shell_mapped_image);
	ClassDB::bind_method(D_METHOD("shell_font", "name", "size"), &AptMenuPlayer::shell_font);
	ClassDB::bind_method(D_METHOD("take_load_progress_calls"), &AptMenuPlayer::take_load_progress_calls);
	ClassDB::bind_method(D_METHOD("lobby_apply", "spec"), &AptMenuPlayer::lobby_apply);
	ClassDB::bind_method(D_METHOD("lobby_state"), &AptMenuPlayer::lobby_state); // lane UI-6
	ClassDB::bind_method(D_METHOD("lobby_gadget_rect", "name"), &AptMenuPlayer::lobby_gadget_rect);
	ClassDB::bind_method(D_METHOD("get_shell_report"), &AptMenuPlayer::get_shell_report);
	ClassDB::bind_method(D_METHOD("shell_drawn_sources"), &AptMenuPlayer::shell_drawn_sources); // lane SHELLFIX-1
	ADD_SIGNAL(MethodInfo("shell_service", PropertyInfo(Variant::STRING, "kind"), PropertyInfo(Variant::STRING, "a"), PropertyInfo(Variant::STRING, "b")));
	ADD_SIGNAL(MethodInfo("shell_request", PropertyInfo(Variant::STRING, "action"), PropertyInfo(Variant::STRING, "argument")));
	ADD_SIGNAL(MethodInfo("shell_screen", PropertyInfo(Variant::STRING, "filename")));
	ClassDB::bind_method(D_METHOD("take_events"), &AptMenuPlayer::take_events);
	ClassDB::bind_method(D_METHOD("get_stats"), &AptMenuPlayer::get_stats);
	ClassDB::bind_method(D_METHOD("reset_timing"), &AptMenuPlayer::reset_timing);
	ClassDB::bind_method(D_METHOD("get_report"), &AptMenuPlayer::get_report);
	ClassDB::bind_method(D_METHOD("get_cursor_image"), &AptMenuPlayer::get_cursor_image);
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
	if (m->shell && m->shell->shell)
	{
		// lane OPTS-3: the Options screen on top shows the OPENBFME button while the device offers the page
		if (AptOptionsScreen *o = dynamic_cast<AptOptionsScreen *>(m->shell->shell->top()))
		{
			o->setExtensionAvailable(m->openbfmeAvailable);
		}
		m->shell->environment.openbfmeOptionsMenu = m->openbfmeAvailable; // lane OPTS-5: the next Options screen opens as the OpenBFME menu
	}
	tick(delta);
	if (m->shell && m->shell->shell && m->godotMouse && !m->inputForwarded)
	{
		m->godotMouse->updateCursor(delta, "Arrow");
	}
	// lane JOINVER-1: a build refusal's DETAILS copied its text: onto the system clipboard (the engine has none of its own)
	if (m->shell && m->shell->shell)
	{
		if (AptOnlineShell *os = dynamic_cast<AptOnlineShell *>(m->shell->shell->findScreenByFilename("OnlineShell.apt")))
		{
			const std::string copied = os->takeClipboard();
			if (!copied.empty() && DisplayServer::get_singleton())
			{
				DisplayServer::get_singleton()->clipboard_set(String::utf8(copied.c_str()));
			}
		}
	}
	render(false);
	m->view3d.advance(); // lane UI-2
	advanceMovies(delta * 1000.0); // lane CAMP-2
	takeCreateAHeroPicture(); // lane CAHPIC-1
}

void AptMenuPlayer::takeCreateAHeroPicture()
{
	// lane CAHPIC-1: Create-a-Hero's TAKE PICTURE (AptCreateAHero.h). RotWK draws the 3D view, then (OnTakePicture having armed it) saves the window's
	// picture (TheDisplay->takeScreenShot, RW 0x4474A0: a 24-bit .bmp named for the hero in the pictures folder, RW 0x6427E6: My Pictures + the game text
	// APPDATA:PictureFolder). Here the picture is the window's frame the armed draw produced, read on the next frame (the viewport's texture is the frame
	// last drawn), saved as PNG (the presentation format the F12 screenshot already uses, GodotInGameHud) in the system's pictures folder + the same
	// subfolder; the box names the folder + the file name without its extension, as retail's does
	AptCreateAHero *cah = m->shell && m->shell->shell ? dynamic_cast<AptCreateAHero *>(m->shell->shell->findScreenByFilename("CreateAHero.apt")) : nullptr;
	if (!cah)
	{
		m->cahPicturePending = false;
		return;
	}
	if (m->cahPicturePending)
	{
		m->cahPicturePending = false;
		std::string error;
		String shown;
		const String base = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_PICTURES);
		const bool headless = DisplayServer::get_singleton() && DisplayServer::get_singleton()->get_name() == String("headless");
		Viewport *vp = get_viewport();
		Ref<Image> image = (!headless && vp && vp->get_texture().is_valid()) ? vp->get_texture()->get_image() : Ref<Image>();
		std::u16string sub;
		if (!m->shell->environment.gameText || !m->shell->environment.gameText->fetch("APPDATA:PictureFolder", sub))
		{
			error = "the game text APPDATA:PictureFolder is missing";
		}
		else if (base.is_empty())
		{
			error = "the system has no pictures folder";
		}
		else if (image.is_null())
		{
			error = "no picture was drawn (a run without a window)";
		}
		else
		{
			const String folder = base.path_join(toGodot(loadScreenU16ToUtf8(sub)));
			const String name = toGodot(loadScreenU16ToUtf8(m->cahPictureName));
			if (DirAccess::make_dir_recursive_absolute(folder) != OK && !DirAccess::dir_exists_absolute(folder))
			{
				error = "the folder " + toNative(folder) + " cannot be made";
			}
			else
			{
				const String file = folder.path_join(name + String(".png"));
				const Error err = image->save_png(file);
				if (err != OK)
				{
					error = "saving " + toNative(file) + " failed (" + toNative(UtilityFunctions::error_string(err)) + ")";
				}
				else
				{
					shown = folder.path_join(name);
					UtilityFunctions::print("CAH picture saved: ", file);
				}
			}
		}
		const Char16String wide = shown.utf16();
		cah->pictureTaken(std::u16string(wide.get_data(), (size_t)wide.length()), error);
		m->dirty = true;
		return;
	}
	if (m->cahGuardShown)
	{
		cah->pictureGuardDrawn(); // RW 0x919DA2: one per frame the guard clip is drawn
	}
	if (cah->takePicturePending() && m->cahMapShown)
	{
		const Dictionary now = Time::get_singleton()->get_datetime_dict_from_system(false); // GetLocalTime
		AptCreateAHero::PictureTime t;
		t.year = (int)now["year"];
		t.month = (int)now["month"];
		t.day = (int)now["day"];
		t.hour = (int)now["hour"];
		t.minute = (int)now["minute"];
		t.second = (int)now["second"];
		m->cahPictureName = cah->beginPicture(t);
		m->cahPicturePending = true;
	}
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
		if (it->first.rfind("loadscreen:", 0) == 0)
		{
			++it; // lane MENU-1: the loading screen's ring follows the wall clock (drawStillLoadScreen): the load runs synchronously
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
	m->godotMouse.reset();
	const std::uint64_t start = Time::get_singleton()->get_ticks_usec();
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("the retail archives are not mounted");
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	clearCanvas(); // review r2: a reboot recreates textures / fonts, so the retained canvas is stale
	m->movies.clear();
	m->movieErrors.clear();
	m->fs = fs;
	m->presentationFs = fs;
	m->view3d.setOwner(this, fs); // lane UI-2
	m->shell.reset();
	m->source = std::make_unique<AptArchiveFileSource>(*fs->archive_fs());
	m->apt.reset(); // before the host it refers to is reused
	m->host = AptGodotHost();
	m->host.frame = &m->frames;
	m->apt = std::make_unique<Apt>(*m->source, m->host);
	m->textures = std::make_unique<AptTextureStore>(*m->source);
	m->originalSource.reset();
	m->originalTextures.reset();
	m->presentationImageNames.clear();
	m->presentationVideos.reset();
	m->godotTextures.clear();
	m->strings = GameTextTable();
	m->fonts = FontSubstitution();
	m->fontByName.clear();
	m->fontNames.clear();
	m->gdiFaces.clear();
	m->boldBase.clear();
	m->cellFonts.clear();
	m->boldFonts.clear();
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
	m->godotMouse.reset();
	const std::uint64_t start = Time::get_singleton()->get_ticks_usec();
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	Ref<RetailFileSystem> presentationFs = config.get("presentation_fs", Ref<RetailFileSystem>());
	const bool suppliedPresentationFs = presentationFs.is_valid();
	if (presentationFs.is_null())
	{
		presentationFs = fs;
	}
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("the retail archives are not mounted");
	}
	else if (fs->is_shell_source())
	{
		errors.push_back("boot_shell requires the normal mounted game file system as its world source");
	}
	else if (suppliedPresentationFs && (!presentationFs->is_mounted() || !presentationFs->is_shell_source()))
	{
		errors.push_back("presentation_fs must be a mounted BFME2 shell source");
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
	m->movies.clear();
	m->movieErrors.clear();
	m->shell.reset();
	m->apt.reset();
	m->source.reset();
	m->originalSource.reset();
	m->textures.reset();
	m->originalTextures.reset();
	m->presentationVideos.reset();
	m->list = AptCanvasList();
	m->drawnList = AptCanvasList();
	m->gadgets = GadgetDrawList();
	m->drawnGadgets = GadgetDrawList();
	m->drawnTexts.clear();
	m->levelCommands.clear();
	m->backgroundPaths.clear();
	m->drawnBackdrop.clear();
	m->fs = fs;
	m->presentationFs = presentationFs;
	m->godotMouse = std::make_shared<GodotMouse>();
	m->godotMouse->loadCursorTable(*fs->archive_fs(), fs->install_root());
	m->godotMouse->updateCursor(0.0, "Arrow");
	for (const std::string &e : m->godotMouse->cursorErrors())
	{
		errors.push_back(toGodot("boot_shell: " + e));
	}
	m->worldId = world->get_instance_id(); // lane CAMP-2: the BinkMovie component's movies
	m->view3d.setOwner(this, presentationFs); // lane UI-2: shell decoration follows the presentation source; gameplay View3D stays on the world path
	m->source = std::make_unique<AptArchiveFileSource>(*presentationFs->archive_fs());
	m->originalSource = std::make_unique<AptArchiveFileSource>(*fs->archive_fs());
	m->textures = std::make_unique<AptTextureStore>(*m->source);
	m->originalTextures = std::make_unique<AptTextureStore>(*m->originalSource);
	m->presentationImageNames.clear();
	m->godotTextures.clear();
	m->strings = GameTextTable();
	m->fonts = FontSubstitution();
	m->fontByName.clear();
	m->fontNames.clear();
	m->gdiFaces.clear();
	m->boldBase.clear();
	m->cellFonts.clear();
	m->boldFonts.clear();
	m->frames = m->steps = m->rebuilds = 0;
	m->stepUs = m->listUs = m->canvasUs = m->submitUs = 0;
	m->loadResources(errors);
	if (presentationFs->is_shell_source())
	{
		m->collectPresentationImageNames(*presentationFs->archive_fs(), errors);
		m->presentationVideos = std::make_unique<VideoPlayer>();
		INIEnvironment videoEnv;
		videoEnv.fileSystem = presentationFs->archive_fs();
		m->presentationVideos->registerBlock(videoEnv.blocks);
		try
		{
			INI videoIni(videoEnv);
			for (const std::string &file : VideoPlayer::loadOrder())
			{
				videoIni.load(file, INI_LOAD_OVERWRITE);
			}
		}
		catch (const INIException &e)
		{
			errors.push_back(toGodot("presentation Video.ini: " + std::string(e.what())));
		}
	}

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
	if (presentationFs->archive_fs() != fs->archive_fs() && !loadGadgetSkinData(*presentationFs->archive_fs(), sm->skins, &error))
	{
		errors.push_back(toGodot("presentation gadget skins: " + error));
	}
	registerPlatformIcons(sm->skins.images); // lane PLATFORMICON-1: our own icons beside the retail mapped images
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
	sm->environment.skirmishHonors = &sm->skirmishHonors; // lane OWNERFIX-3
	sm->environment.saveLoad = &sm->saveLoad;       // lane MP-2: SaveLoad.apt
	// lane MENU-1: Skirmish.ini beside Options.ini (config "skirmish_ini" overrides the file: the scripted runs use a file of their own)
	{
		std::string skirmishIni = toNative(OS::get_singleton()->get_user_data_dir()) + "/Skirmish.ini";
		if (config.has("skirmish_ini") && !String(config["skirmish_ini"]).is_empty())
		{
			skirmishIni = toNative(ProjectSettings::get_singleton()->globalize_path(String(config["skirmish_ini"]))); // a user:// path is the user data folder's
		}
		sm->profiles = std::make_unique<SkirmishPreferences>(skirmishIni);
		for (const std::string &e : sm->profiles->errors())
		{
			errors.push_back(toGodot(e));
		}
		sm->environment.profiles = sm->profiles.get();
		sm->deviceUnverified.insert(SkirmishPreferences::unverified());
	}
	sm->environment.services = &sm->services;
	// lane UI-2: Options.ini in the user data folder (OptionPreferences); a missing file is no entries (the first run). The soft particle setting
	// takes effect at once (FX-3's project setting).
	sm->environment.optionsFile = toNative(default_options_path());
	if (config.has("options_file"))
	{
		sm->environment.optionsFile = toNative(ProjectSettings::get_singleton()->globalize_path(String(config["options_file"]))); // lane OPTS-2: a check's own file (game.gd --options-file), never the player's
	}
	sm->options.load(sm->environment.optionsFile);
	for (const std::string &key : sm->options.migrateLegacyKeys())
	{
		UtilityFunctions::print("SHELL Options.ini: ", toGodot(key), " is now ", toGodot(std::string(OptionPreferences::kSoftParticles)), " (lane OPTS-2: OpenBFME keys carry the prefix)");
	}
	sm->environment.options = &sm->options;
	// lane CAMPSAVE-1: GlobalData::applyOptionPreferences (RW 0x641DED -> RW 0x6E5E16): TheGlobalData + 0xAF5 = Options.ini's BCU compared with "true" by
	// _strcmpi (a missing key: false)
	sm->environment.bonusCampaignUnlocked = sm->options.has("BCU") && AsciiStringUtil::compareNoCase(sm->options.get("BCU"), "true") == 0;
	sm->environment.campaignMenu = &sm->campaignMenu;
	// lane OPTS-4: the saved volumes are in effect from the start, as ZH's AudioManager::init takes them from OptionPreferences (DONOR; RotWK's reader
	// is RW 0x6E5FB3, its caller at start was not read: INFERENCE, S-4510). Before, the AudioSettings defaults played until the Options screen's
	// ACCEPT or CANCEL, and CANCEL changed the volume. Without an installed audio manager (a shell viewer) nothing is applied, and the report says so
	{
		static const char *const kVolumeKeys[5] = { "SFXVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume" };
		for (const char *key : kVolumeKeys)
		{
			if (!sm->options.has(key))
			{
				continue;
			}
			if (AudioApi::current())
			{
				sm->services.applyOption(key, sm->options.get(key));
			}
			else
			{
				sm->deviceUnverified.insert("[S-4510] Options.ini's volumes are not applied at start: no audio manager is installed");
			}
		}
	}
	// lane PLAY-2: Data\INI\GameLOD.ini (RW 0x7F995C): StaticGameLOD for the Options screen's advanced page; its other blocks are read past
	{
		INIEnvironment env;
		env.fileSystem = fs->archive_fs();
		INIBlockRecorder recorder;
		RegisterRecordingBlockStubs(env.blocks, recorder, { "StaticGameLOD" }, StubExtent::Lenient);
		sm->gameLOD.registerBlocks(env.blocks);
		try
		{
			INI ini(env);
			for (const std::string &file : GameLODManager::loadOrder())
			{
				ini.load(file, INI_LOAD_OVERWRITE);
			}
			sm->environment.gameLOD = &sm->gameLOD;
			// lane OPTS-2: RW 0x602165 / RW 0x4465D8: the level of Options.ini (or the detection's, S-2481) is applied at start; the device reads the
			// state through get_detail_state
			sm->gameLOD.init(sm->options, &sm->detailStop);
			if (!sm->detailStop.empty())
			{
				UtilityFunctions::print("SHELL detail: ", toGodot(sm->detailStop));
			}
		}
		catch (const INIException &e)
		{
			errors.push_back(toGodot(std::string("GameLOD.ini: ") + e.what()));
		}
	}
	sm->services.applyOption(OptionPreferences::kSoftParticles, sm->options.softParticles() ? "yes" : "no");
	// lane UI-4: the main menu's credits roll reads Data\INI\Credits.ini from the archives and its fonts from language.ini (GlobalLanguage::init,
	// RW 0x5E9D3C); a language.ini that does not parse is a boot error
	sm->environment.fileSystem = presentationFs->archive_fs();
	{
		INIEnvironment env;
		env.fileSystem = presentationFs->archive_fs();
		sm->language.registerBlocks(env.blocks);
		try
		{
			INI ini(env);
			ini.load(GlobalLanguage::fileName(), INI_LOAD_OVERWRITE);
			sm->environment.language = &sm->language;
		}
		catch (const INIException &e)
		{
			errors.push_back(toGodot(std::string(GlobalLanguage::fileName()) + ": " + e.what()));
		}
	}
	registerAptScreenFactories(sm->factories);
	Impl *impl = m.get();
	sm->metrics = std::make_unique<ShellFontMetrics>([impl](const std::string &name, float size, bool bold, float *drawSize) {
		Ref<Font> f = impl->fontFor(name, size, drawSize, bold);
		return impl->cellFont(f, std::max(1, (int)std::lround(*drawSize))); // lane UI-6: GDI cell heights, as the gadgets' draw
	});
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
		sm->nav = std::make_unique<MenuNavigator>(*sm->wm); // lane OPTS-6
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
			sm->environment.resolutionChangeApplies = false; // lane OPTS-2 [S-3401]
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
			else if (!fs->is_bfme1() && !Shell::readShellMapOn(std::string(bytes.begin(), bytes.end()), on, &parseError))
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
		// lane HOVER-1: the menus' tool tips: AptButtonTooltipMap.ini's names, Mouse.ini / Language.ini's tool tip font and delay, GuiFX.apt (the window
		// manager's own movie at its level 11, BFME1 AptGuiFXRegisterCallbacks) whose ToolTip clip shows them; a missing file or field is a boot error
		if (!fs->is_bfme1())
		{
			AptButtonTooltipMap names;
			CursorTooltipSettings tipSettings;
			std::string tipError;
			if (!AptButtonTooltipMap::load(*presentationFs->archive_fs(), names, &tipError) ||
					!CursorTooltipSettings::load(*presentationFs->archive_fs(), tipSettings, &tipError))
			{
				errors.push_back(toGodot("boot_shell: tool tips: " + tipError));
			}
			else
			{
				sm->wm->setTooltipNameMap(names.buttonMap);
				if (!sm->guiFX)
				{
					sm->guiFX = std::make_unique<AptGuiFXScreen>(*sm->wm, *sm->shell);
				}
				sm->cursorTooltip = std::make_unique<ShellCursorTooltip>(*sm->wm, sm->text.get(), tipSettings);
				for (const std::string &l : CursorTooltip::acceptanceStops())
				{
					sm->deviceUnverified.insert(l); // [S-4610] in the shell report
				}
			}
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

Dictionary AptMenuPlayer::get_layering_report()
{
	Dictionary d;
	if (!m_booted || !m->shell || !m->shell->shell)
	{
		return d;
	}
	AptRenderList rl;
	m->A()->buildRenderList(rl);
	const ShellLayeringReport r = checkShellLayering(*m->shell->wm, *m->shell->shell, rl);
	auto strings = [](const std::vector<std::string> &v) {
		PackedStringArray a;
		for (const std::string &s : v)
		{
			a.push_back(toGodot(s));
		}
		return a;
	};
	d["top"] = toGodot(r.topScreen);
	d["screens"] = strings(r.visibleScreens);
	d["movies"] = strings(r.visibleMovies);
	d["overprints"] = strings(r.overprints);
	d["shown_gadgets"] = r.shownGadgets;
	d["enabled_gadgets"] = r.enabledGadgets;
	d["input_blocked"] = r.componentInputBlocked;
	d["unanchored_gadgets"] = m->unanchoredGadgets;
	d["problems"] = strings(r.problems);
	return d;
}

Dictionary AptMenuPlayer::choose_load_screen(const Dictionary &spec)
{
	// lane MENU-1: RW 0x626A87 (GameClient/GUI/StillImageLoadScreen.h); the film still's draw is the client's (Godot's generator, S-3505)
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	rng->randomize();
	const LoadScreenChoice c = chooseLoadScreen((int)(int64_t)spec.get("game_mode", 2), (int)(int64_t)spec.get("screen_kind", 3), (bool)spec.get("loading_save", false),
		toNative(String(spec.get("picture", ""))), [&](int lo, int hi) { return (int)rng->randi_range(lo, hi); });
	Dictionary d;
	d["kind"] = c.kind == LoadScreenKind::StillImage ? "still" : (c.kind == LoadScreenKind::Players ? "players" : "other");
	d["picture"] = toGodot(c.picture);
	d["error"] = toGodot(c.error);
	return d;
}

Dictionary AptMenuPlayer::show_still_load_screen(const String &picture)
{
	Dictionary d;
	d["ok"] = false;
	if (!m->shell)
	{
		d["error"] = "the shell is not booted";
		return d;
	}
	std::string error;
	if (!layoutStillImageLoadScreen(m->shell->skins.images, toNative(picture), m->stillLoad, &error))
	{
		d["error"] = toGodot(error);
		m->stillLoadOn = false;
		return d;
	}
	m->shell->deviceUnverified.insert(m->stillLoad.unverified);
	m->stillLoadOn = true;
	m->stillLoadStartUs = Time::get_singleton()->get_ticks_usec();
	m->stillProgress = -1.0; // lane SAVE-4e: a new screen has no catch-up line
	m->stillProgressText.clear();
	m->dirty = true;
	m->drawnValid = false;
	d["ok"] = true;
	return d;
}

void AptMenuPlayer::set_still_load_progress(double percent, const String &text)
{
	m->stillProgress = percent < 0.0 ? -1.0 : std::min(100.0, percent);
	m->stillProgressText = percent < 0.0 ? std::string() : toNative(text);
	m->dirty = true;
	m->drawnValid = false;
}

void AptMenuPlayer::hide_still_load_screen()
{
	if (m->stillLoadOn)
	{
		m->stillLoadOn = false;
		m->dirty = true;
		m->drawnValid = false;
	}
}

Dictionary AptMenuPlayer::get_still_load_screen() const
{
	Dictionary d;
	d["on"] = m->stillLoadOn;
	if (m->stillLoadOn)
	{
		Array slots;
		for (const StillImageSlot &sl : m->stillLoad.slots)
		{
			if (!sl.on)
			{
				continue;
			}
			Dictionary e;
			e["image"] = toGodot(sl.image);
			e["rect"] = Rect2(sl.x0, sl.y0, sl.x1 - sl.x0, sl.y1 - sl.y0);
			slots.push_back(e);
		}
		d["slots"] = slots;
		d["movie"] = toGodot(m->stillLoad.movie);
		d["text_label"] = toGodot(m->stillLoad.textLabel);
		d["progress"] = m->stillProgress; // lane SAVE-4e
		d["progress_text"] = toGodot(m->stillProgressText);
		auto it = m->movies.find("loadscreen:" + m->stillLoad.movie);
		d["movie_playing"] = it != m->movies.end() && it->second.stream.is_valid() && it->second.error.empty();
		d["movie_error"] = it != m->movies.end() ? toGodot(it->second.error) : String();
	}
	return d;
}

Dictionary AptMenuPlayer::get_backdrop_state() const
{
	// lane CAMP-2: what is on screen behind the shell's movies, read from the canvas as drawn (not from the shell's flags)
	Dictionary d;
	d["backdrop_image"] = toGodot(m->drawnBackdrop);
	d["model_animation_frames"] = m->view3d.animationFrames();
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

bool AptMenuPlayer::open_options(bool network_enabled, bool allow_resolution, bool allow_advanced, bool advanced_only)
{
	if (!m->shell || !m->shell->shell)
	{
		return false;
	}
	AptOptionsScreen::open(*m->shell->shell, network_enabled, allow_resolution, allow_advanced, advanced_only);
	m->dirty = true;
	return m->shell->shell->findScreenByFilename("Options.apt") != nullptr;
}

void AptMenuPlayer::shell_pop()
{
	if (m->shell && m->shell->shell)
	{
		m->shell->shell->pop();
		m->dirty = true;
	}
}

Dictionary AptMenuPlayer::shell_drawn_sources() const
{
	Dictionary r;
	if (!m->shell || !m->shell->shell || m->presentationFs.is_null() || !m->presentationFs->archive_fs())
	{
		r["error"] = "the shell is not booted";
		return r;
	}
	ArchiveFileSystem &fs = *m->presentationFs->archive_fs();
	r["style"] = m->presentationFs->is_shell_source() ? "bfme2-106" : "rotwk-201";
	r["view3d_style"] = m->view3d.sourceStyle();
	Dictionary movies;
	for (int i = 0; i < m->shell->shell->screenCount(); ++i)
	{
		const std::string name = m->shell->shell->screenAt(i)->filename();
		movies[toGodot(name)] = toGodot(fs.getArchiveFilenameForFile(name)); // AptArchiveFileSource reads the movie at the archives' root
	}
	r["movies"] = movies;
	Dictionary textures;
	for (const AptCanvasOp &op : m->drawnList.ops)
	{
		if (op.kind == AptCanvasOp::Kind::Mesh && !op.texture.empty() && m->brandTextures.find(op.texture) == m->brandTextures.end())
		{
			textures[toGodot(op.texture)] = toGodot(fs.getArchiveFilenameForFile("art/textures/" + op.texture)); // AptTextureStore::get
		}
	}
	r["textures"] = textures;
	PackedStringArray original;
	for (const auto &kv : m->godotTextures)
	{
		// a picture the engine makes from a file ("file:", the lobby's map pictures) is game content in either style
		if (kv.first.rfind("original:", 0) == 0 && kv.first.rfind("original:file:", 0) != 0)
		{
			original.push_back(toGodot(kv.first.substr(9)));
		}
	}
	r["original_images"] = original;
	return r;
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

void AptMenuPlayer::set_tribute_world(Object *world)
{
	if (!m->shell)
	{
		return;
	}
	GameWorld *w = Object::cast_to<GameWorld>(world);
	m->shell->environment.tribute = w ? w->tribute_source() : nullptr;
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

Dictionary AptMenuPlayer::get_options() const
{
	Dictionary out;
	if (m->shell)
	{
		for (const auto &kv : m->shell->options.values())
		{
			out[toGodot(kv.first)] = toGodot(kv.second);
		}
	}
	return out;
}

// lane UI-4: the texts the gadgets and the render callbacks drew since the last clear: [{ source, text, color, window }]
Array AptMenuPlayer::drawn_texts(bool clear)
{
	Array out;
	for (const auto &kv : m->drawnTexts)
	{
		Dictionary e;
		e["source"] = toGodot(kv.second.source);
		e["text"] = kv.second.text;
		e["color"] = kv.second.color;
		e["window"] = kv.second.window;
		out.push_back(e);
	}
	if (clear)
	{
		m->drawnTexts.clear();
	}
	return out;
}

Array AptMenuPlayer::gadget_draw_list()
{
	Array out;
	if (!m->shell || !m->shell->layer)
	{
		return out;
	}
	static const char *const kinds[] = { "image", "fill", "rect", "line", "text", "clip", "clip_end" };
	const AptStageMapping &map = m->mapping;
	auto box = [&map](float x0, float y0, float x1, float y1) {
		float ax, ay, bx, by;
		map.stageToWindow(x0, y0, ax, ay);
		map.stageToWindow(x1, y1, bx, by);
		return Rect2(Vector2(ax, ay), Vector2(bx - ax, by - ay));
	};
	GameWindowManager &gwm = m->shell->layer->gadgets();
	const GameWindow *lone = m->shell->layer->openDropDown();
	std::map<const GameWindow *, int> roots;
	for (const GadgetDrawCommand &c : m->gadgets.commands)
	{
		Dictionary e;
		e["kind"] = kinds[(int)c.kind];
		e["window"] = toGodot(c.window);
		e["rect"] = box((float)c.x0, (float)c.y0, (float)c.x1, (float)c.y1);
		e["image"] = toGodot(c.image);
		e["text"] = toGodot(u16ToUtf8(c.text));
		e["lone"] = c.root != nullptr && c.root == lone;
		int index = -1;
		if (c.root && gwm.winIsAlive(const_cast<GameWindow *>(c.root)))
		{
			index = roots.emplace(c.root, (int)roots.size()).first->second;
			int x = 0, y = 0, w = 0, h = 0;
			const_cast<GameWindow *>(c.root)->winGetScreenPosition(&x, &y);
			const_cast<GameWindow *>(c.root)->winGetSize(&w, &h);
			e["root_rect"] = box((float)x, (float)y, (float)(x + w), (float)(y + h));
			e["root_stage_rect"] = Rect2((float)x, (float)y, (float)w, (float)h);
			e["root_name"] = toGodot(c.root->aptInstanceName().empty() ? c.root->name() : c.root->aptInstanceName());
		}
		e["root"] = index;
		out.push_back(e);
	}
	return out;
}

bool AptMenuPlayer::set_option(const String &key, const Variant &value)
{
	if (!m->shell)
	{
		return false;
	}
	const std::string k = key.utf8().get_data();
	const std::string v = value.get_type() == Variant::NIL ? std::string() : std::string(String(value).utf8().get_data());
	// ZH UserPreferences::load keeps no key or value that is empty or holds a line break: such an entry would not read back
	if (k.empty() || k.find_first_of("=\r\n") != std::string::npos || v.find_first_of("\r\n") != std::string::npos)
	{
		return false;
	}
	if (v.empty())
	{
		m->shell->options.erase(k);
	}
	else
	{
		m->shell->options.set(k, v);
	}
	return true;
}

Dictionary AptMenuPlayer::save_options()
{
	Dictionary r;
	std::string error;
	const bool ok = m->shell && !m->shell->environment.optionsFile.empty() && m->shell->options.save(m->shell->environment.optionsFile, &error);
	r["ok"] = ok;
	r["error"] = toGodot(ok ? std::string() : (m->shell ? (error.empty() ? std::string("Options.ini: no file name") : error) : std::string("no shell")));
	r["path"] = m->shell ? toGodot(m->shell->environment.optionsFile) : String();
	return r;
}

// ---- lane OPTS-3: the OpenBFME page ----------------------------------------------------------------------------------------------------------------
namespace
{
AptOptionsScreen *optionsTop(Shell *shell)
{
	return shell ? dynamic_cast<AptOptionsScreen *>(shell->top()) : nullptr;
}

std::u16string u16Of(const Variant &v)
{
	return utf8ToU16(toNative(String(v)));
}

OpenBFMEPageRow pageRowOf(const Dictionary &d)
{
	OpenBFMEPageRow r;
	const String kind = d.get("kind", "text");
	r.kind = kind == "section" ? OpenBFMEPageRow::Kind::Section
		: kind == "choice"     ? OpenBFMEPageRow::Kind::Choice
		: kind == "check"      ? OpenBFMEPageRow::Kind::Check
		: kind == "slider"     ? OpenBFMEPageRow::Kind::Slider
		: kind == "entry"      ? OpenBFMEPageRow::Kind::Entry
		: kind == "button"     ? OpenBFMEPageRow::Kind::Button // lane CHEAT-1
							   : OpenBFMEPageRow::Kind::Text;
	r.key = toNative(String(d.get("key", "")));
	r.label = u16Of(d.get("label", ""));
	r.help = u16Of(d.get("help", ""));
	const Array items = d.get("items", Array());
	for (int64_t i = 0; i < items.size(); ++i)
	{
		const Variant it = items[i];
		if (it.get_type() == Variant::DICTIONARY)
		{
			const Dictionary id = it;
			r.items.push_back({ u16Of(id.get("text", "")), (bool)id.get("enabled", true) });
		}
		else
		{
			r.items.push_back({ u16Of(it), true });
		}
	}
	r.value = (int)d.get("value", 0);
	r.minValue = (int)d.get("min", 0);
	r.maxValue = (int)d.get("max", 100);
	r.valueText = u16Of(d.get("value_text", ""));
	r.enabled = (bool)d.get("enabled", true);
	r.column = (int)d.get("column", 0); // lane OPTS-5
	return r;
}

// A page spec from the device's Dictionary for the Options screen's OpenBFME page.
OpenBFMEPageSpec pageSpecOf(const Dictionary &spec)
{
	OpenBFMEPageSpec s;
	s.subtitle = u16Of(spec.get("subtitle", ""));
	s.tabsHeader = u16Of(spec.get("tabs_header", ""));
	const Array tabs = spec.get("tabs", Array());
	for (int64_t i = 0; i < tabs.size() && i < 16; ++i)
	{
		s.tabs.push_back(u16Of(tabs[i]));
	}
	s.tab = (int)spec.get("tab", 0);
	s.panelHeader = u16Of(spec.get("panel_header", ""));
	s.note = u16Of(spec.get("note", ""));
	const Array rows = spec.get("rows", Array());
	for (int64_t i = 0; i < rows.size() && i < 64; ++i)
	{
		s.rows.push_back(pageRowOf(rows[i]));
	}
	if (spec.has("list"))
	{
		s.hasList = true;
		const Array list = spec["list"];
		for (int64_t i = 0; i < list.size() && i < 500; ++i)
		{
			const Array pair = list[i];
			s.list.emplace_back(u16Of(pair.size() > 0 ? pair[0] : Variant("")), u16Of(pair.size() > 1 ? pair[1] : Variant("")));
		}
	}
	s.listFromRow = (int)spec.get("list_from_row", -1);
	s.listActivates = (bool)spec.get("list_activates", false);
	return s;
}

Array pageEventsOf(const std::vector<OpenBFMEPageEvent> &events)
{
	Array out;
	static const char *kinds[] = { "value", "tab", "cancel", "reset", "accept", "entry", "release", "submit", "list_select", "list_activate", "press" };
	for (const OpenBFMEPageEvent &e : events)
	{
		Dictionary d;
		d["kind"] = kinds[(int)e.kind];
		d["key"] = toGodot(e.key);
		d["value"] = e.value;
		d["text"] = toGodot(u16ToUtf8(e.text));
		out.push_back(d);
	}
	return out;
}

// lane OPTS-5: a row as GDScript describes them (pageRowOf's input)
Dictionary pageRowDict(const OpenBFMEPageRow &r)
{
	static const char *const kinds[] = { "section", "choice", "check", "slider", "text", "entry", "button" };
	Dictionary d;
	d["kind"] = kinds[(int)r.kind];
	d["key"] = toGodot(r.key);
	d["label"] = toGodot(u16ToUtf8(r.label));
	d["help"] = toGodot(u16ToUtf8(r.help));
	Array items;
	for (const OpenBFMEPageItem &it : r.items)
	{
		Dictionary id;
		id["text"] = toGodot(u16ToUtf8(it.text));
		id["enabled"] = it.enabled;
		items.push_back(id);
	}
	d["items"] = items;
	d["value"] = r.value;
	d["min"] = r.minValue;
	d["max"] = r.maxValue;
	d["value_text"] = toGodot(u16ToUtf8(r.valueText));
	d["enabled"] = r.enabled;
	d["column"] = r.column;
	return d;
}

Rect2 stageRect(const std::array<int, 4> &r)
{
	return Rect2((real_t)r[0], (real_t)r[1], (real_t)(r[2] - r[0]), (real_t)(r[3] - r[1]));
}
} // namespace

void AptMenuPlayer::set_openbfme_available(bool available)
{
	m->openbfmeAvailable = available;
	if (m->shell)
	{
		m->shell->environment.openbfmeOptionsMenu = available; // lane OPTS-5
	}
	if (AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr))
	{
		o->setExtensionAvailable(available);
	}
}

Dictionary AptMenuPlayer::openbfme_page_open()
{
	Dictionary r;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	std::string error;
	const bool ok = o && o->openExtension(&error);
	r["ok"] = ok;
	r["error"] = toGodot(o ? error : std::string("Options.apt is not the top screen"));
	return r;
}

void AptMenuPlayer::openbfme_page_close()
{
	if (AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr))
	{
		o->closeExtension();
	}
}

Dictionary AptMenuPlayer::openbfme_page_show(const Dictionary &spec)
{
	Dictionary r;
	r["ok"] = false;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	if (!o || !o->extensionOpen() || !o->extensionPage())
	{
		r["error"] = "the OpenBFME page is not open";
		return r;
	}
	OpenBFMEPageSpec s;
	s.subtitle = u16Of(spec.get("subtitle", ""));
	s.tabsHeader = u16Of(spec.get("tabs_header", ""));
	const Array tabs = spec.get("tabs", Array());
	for (int64_t i = 0; i < tabs.size(); ++i)
	{
		s.tabs.push_back(u16Of(tabs[i]));
	}
	s.tab = (int)spec.get("tab", 0);
	s.panelHeader = u16Of(spec.get("panel_header", ""));
	s.note = u16Of(spec.get("note", ""));
	const Array rows = spec.get("rows", Array());
	for (int64_t i = 0; i < rows.size(); ++i)
	{
		s.rows.push_back(pageRowOf(rows[i]));
	}
	if (spec.has("list"))
	{
		s.hasList = true;
		const Array list = spec["list"];
		for (int64_t i = 0; i < list.size(); ++i)
		{
			const Array pair = list[i];
			s.list.emplace_back(u16Of(pair.size() > 0 ? pair[0] : Variant("")), u16Of(pair.size() > 1 ? pair[1] : Variant("")));
		}
	}
	std::string error;
	const bool ok = o->extensionPage()->show(s, &error);
	r["ok"] = ok;
	r["error"] = toGodot(error);
	if (!ok)
	{
		UtilityFunctions::printerr(toGodot("SHELL OpenBFME page: " + error));
	}
	return r;
}

bool AptMenuPlayer::openbfme_page_set_row(const Dictionary &row)
{
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	return o && o->extensionOpen() && o->extensionPage() && o->extensionPage()->setRow(pageRowOf(row));
}

Array AptMenuPlayer::openbfme_page_events()
{
	Array out;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	if (!o)
	{
		return out;
	}
	return pageEventsOf(o->takeExtensionEvents());
}

Dictionary AptMenuPlayer::openbfme_page_state() const
{
	Dictionary d;
	d["available"] = m->openbfmeAvailable;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	d["open"] = o && o->extensionOpen();
	d["menu"] = o && o->menuMode(); // lane OPTS-5
	d["page_state"] = o ? o->pageState() : 0;
	if (!o)
	{
		return d;
	}
	float box[4];
	if (o->entryBox(box))
	{
		d["entry"] = Rect2(box[0], box[1], box[2] - box[0], box[3] - box[1]);
	}
	Dictionary buttons;
	static const char *names[3] = { "cancel", "reset", "accept" };
	for (int i = 0; i < 3; ++i)
	{
		AptButtonInst *b = o->barButton(i); // lane SHELLFIX-1: the button the page takes the clicks of, in either style's movie
		if (b && WindowManager::stageBounds(*b, box))
		{
			buttons[names[i]] = Rect2(box[0], box[1], box[2] - box[0], box[3] - box[1]);
		}
	}
	d["buttons"] = buttons;
	OpenBFMEOptionsPage *page = o->extensionPage();
	if (!o->extensionOpen() || !page || !page->shown())
	{
		return d;
	}
	d["tab"] = page->spec().tab;
	Array tabs;
	for (std::size_t i = 0; i < page->spec().tabs.size(); ++i)
	{
		std::array<int, 4> r{};
		page->rect("tab:" + std::to_string(i), r);
		tabs.push_back(stageRect(r));
	}
	d["tabs"] = tabs;
	Dictionary rows;
	for (const OpenBFMEPageRow &row : page->spec().rows)
	{
		if (row.key.empty())
		{
			continue;
		}
		Dictionary rd;
		std::array<int, 4> r{};
		if (page->rect(row.key, r))
		{
			rd["rect"] = stageRect(r);
		}
		rd["value"] = row.value;
		rd["enabled"] = row.enabled;
		rd["items"] = (int)row.items.size();
		static const char *const kKinds[] = { "section", "choice", "check", "slider", "text", "entry" };
		rd["kind"] = kKinds[(int)row.kind]; // lane OPTS-6
		rd["min"] = row.minValue;
		rd["max"] = row.maxValue;
		// lane OPTS-5: the label's text and room, the help line and the value text (the checks measure the label, read the help)
		rd["label"] = toGodot(u16ToUtf8(row.label));
		rd["help"] = toGodot(u16ToUtf8(row.help));
		rd["value_text"] = toGodot(u16ToUtf8(row.valueText));
		if (page->labelBox(row.key, r))
		{
			rd["label_rect"] = stageRect(r);
		}
		rows[toGodot(row.key)] = rd;
	}
	d["rows"] = rows;
	std::array<int, 4> lr{};
	if (page->rect("list", lr))
	{
		d["list"] = stageRect(lr);
	}
	d["help"] = toGodot(u16ToUtf8(page->helpText()));
	d["help_top"] = page->helpTop();
	return d;
}

Dictionary AptMenuPlayer::openbfme_page_choose(const String &key, int value)
{
	Dictionary r;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	std::string error = "the OpenBFME page is not open";
	const bool ok = o && o->extensionOpen() && o->extensionPage() && o->extensionPage()->choose(toNative(key), value, &error);
	r["ok"] = ok;
	r["error"] = ok ? String() : toGodot(error);
	return r;
}

Dictionary AptMenuPlayer::options_retail_rows() const
{
	Dictionary out;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	if (!o)
	{
		return out;
	}
	std::map<std::string, std::string> helpLabels;
	for (const AptOptionsScreen::RetailRow &r : AptOptionsScreen::retailRowTable())
	{
		helpLabels[r.key] = r.helpLabel;
	}
	for (const OpenBFMEPageRow &row : o->retailRows())
	{
		Dictionary d = pageRowDict(row);
		d["help_label"] = toGodot(helpLabels[row.key]);
		out[toGodot(row.key)] = d;
	}
	return out;
}

Dictionary AptMenuPlayer::openbfme_menu_finish(bool accept)
{
	Dictionary r;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	const bool ok = o && o->menuMode() && o->extensionOpen();
	r["ok"] = ok;
	r["error"] = ok ? String() : String(o ? "the OpenBFME menu is not open" : "Options.apt is not the top screen");
	if (ok)
	{
		o->finishMenu(accept);
	}
	return r;
}

Dictionary AptMenuPlayer::openbfme_menu_reset()
{
	Dictionary r;
	AptOptionsScreen *o = optionsTop(m->shell ? m->shell->shell.get() : nullptr);
	const bool ok = o && o->menuMode() && o->extensionOpen();
	r["ok"] = ok;
	r["error"] = ok ? String() : String(o ? "the OpenBFME menu is not open" : "Options.apt is not the top screen");
	if (ok)
	{
		o->resetMenu();
	}
	return r;
}

int AptMenuPlayer::options_page() const
{
	const AptOptionsScreen *o = m->shell && m->shell->shell ? dynamic_cast<const AptOptionsScreen *>(m->shell->shell->top()) : nullptr;
	return o ? o->pageState() : 0;
}

Dictionary AptMenuPlayer::get_detail_state() const
{
	Dictionary d;
	if (!m->shell || !m->shell->environment.gameLOD)
	{
		d["ok"] = false;
		d["error"] = "no GameLOD.ini (the shell did not boot or the file did not parse)";
		return d;
	}
	const StaticLODState &s = m->shell->gameLOD.state();
	d["ok"] = s.level != -1;
	d["level"] = s.level;
	d["level_name"] = s.level >= 0 ? String(GameLODManager::levelName(s.level)) : String();
	d["stop"] = toGodot(m->shell->detailStop);
	d["unapplied"] = toGodot(GameLODManager::unappliedEffects()); // [S-3402]
	d["model_lod"] = s.modelLOD;
	d["effects_lod"] = s.effectsLOD;
	d["shadow_lod"] = s.shadowLOD;
	d["terrain_lod"] = s.terrainLOD;
	d["water_lod"] = s.waterLOD;
	d["animation_detail"] = s.animationDetail;
	d["min_particle_priority"] = s.minParticlePriority;
	d["min_particle_skip_priority"] = s.minParticleSkipPriority;
	d["decals"] = s.decals;
	d["decals_reduced"] = s.decalsReduced;
	d["all_health_bars"] = s.allHealthBars;
	d["use_heat_effects"] = s.useHeatEffects;
	d["max_particle_count"] = s.maxParticleCount;
	d["use_shadow_volumes"] = s.useShadowVolumes;
	d["use_shadow_decals"] = s.useShadowDecals;
	d["use_shadow_mapping"] = s.useShadowMapping;
	d["texture_reduction"] = s.textureReduction;
	d["shader_lod"] = s.shaderLOD;
	d["shader_material_replacement"] = s.shaderMaterialReplacement;
	d["use_terrain_normal_map"] = s.useTerrainNormalMap;
	d["use_distance_dependant_terrain_textures"] = s.useDistanceDependantTerrainTextures;
	d["high_water"] = s.highWater;
	d["show_soft_water_edge"] = s.showSoftWaterEdge;
	d["max_tank_track_edges"] = s.maxTankTrackEdges;
	d["lowest_model_lod"] = s.lowestModelLOD;
	d["show_props"] = s.showProps;
	// RW 0x641E8B: the display gamma of Brightness; RW 0x6E59A9: the scroll factor (missing: the caller's GameData default, so only the text here)
	d["display_gamma"] = OptionPreferences::displayGamma((int)m->shell->options.getBrightness(), 1.0f);
	// the Scroll Speed slider's position as InitGadgets sets it (RW 0x9209D8: getScrollFactor * 50, truncated), the missing key's default included
	d["scroll_slider"] = (int64_t)(m->shell->options.getScrollFactor(m->shell->environment.keyboardDefaultScrollSpeedFactor) * 50.0f);
	return d;
}

// lane UI-4: a GameFont's metrics as the shell's gadgets and render callbacks see them (ShellFontMetrics), and whether its bold is applied
Dictionary AptMenuPlayer::font_metrics(const String &name, int point_size, bool bold, const String &text) const
{
	Impl *impl = m.get();
	ShellFontMetrics metrics([impl](const std::string &n, float size, bool b, float *drawSize) { return impl->fontFor(n, size, drawSize, b); });
	GameFont font;
	font.name = toNative(name);
	font.pointSize = point_size;
	font.bold = bold;
	const std::string utf8 = toNative(text);
	Dictionary r;
	r["height"] = metrics.fontHeight(font);
	r["width"] = metrics.textWidth(font, utf8ToU16(utf8));
	float drawSize = 0;
	Ref<Font> f = impl->fontFor(font.name, (float)point_size, &drawSize, bold);
	Ref<FontVariation> v = f;
	r["embolden"] = v.is_valid() ? v->get_variation_embolden() : 0.0;
	r["glyph_spacing"] = v.is_valid() ? v->get_spacing(TextServer::SPACING_GLYPH) : 0;
	r["callback_texts_regular"] = (int64_t)impl->callbackTextDraws[0];
	r["callback_texts_bold"] = (int64_t)impl->callbackTextDraws[1];
	return r;
}

// lane CAMPUI-1: the in-game notification box (GUI/AptScreens/InGameNotificationBox.h). Requests (kind): "begin" (the game's box: the movie in a level,
// the INI read once; RW 0x6A6AAD creates it with InGameUI), "end", "show" { type, label, seconds, override (bool), override_image, location (0 main,
// 1 spellStore, 2 objectives), opaque } (RW 0x7C5507 -> RW 0x69BFAE -> Open RW 0x8EB66D), "update" { now_ms, letterboxed } (RW 0x8EB9B4: answers the
// open audio of a notice that opened, "" none), "enable" { on } (RW 0x8EB2BA), "close" (DoClose RW 0x8EB2D8); every answer has ok, error, state,
// active (the shown notice's title / message, UTF-8), level
Dictionary AptMenuPlayer::notification_box(const Dictionary &request)
{
	Dictionary out;
	out["ok"] = false;
	if (!m->shell)
	{
		out["error"] = "not in shell mode";
		return out;
	}
	ShellMode &sm = *m->shell;
	const std::string kind = toNative(String(request.get("kind", "")));
	auto u8 = [](const std::u16string &t) { return u16ToUtf8(t); };
	if (kind == "begin")
	{
		if (!sm.notificationSettingsLoaded)
		{
			std::string error;
			if (!sm.environment.fileSystem || !NotificationBoxSettings::load(*sm.environment.fileSystem, sm.notificationSettings, &error))
			{
				out["error"] = toGodot(sm.environment.fileSystem ? error : std::string("no file system"));
				return out;
			}
			sm.notificationSettingsLoaded = true;
		}
		if (!sm.notificationBox)
		{
			sm.notificationBox = std::make_unique<InGameNotificationBoxMovieClip>(*sm.wm, *sm.shell, *sm.metrics);
			if (sm.notificationBox->level() < 0)
			{
				sm.notificationBox.reset();
				out["error"] = "InGameNotificationBox.apt could not be loaded";
				return out;
			}
			for (int i = 0; i < 30 && !sm.wm->isAptWindowLoaded(sm.notificationBox->level()); ++i)
			{
				tick(1.0 / 30.0); // the window manager loads a movie over its first updates
			}
			sm.notificationBox->setEnabled(true); // the game starts with its control bar shown (ShowControlBar RW 0x8046A1 enables the box)
		}
	}
	else if (kind == "end")
	{
		sm.notificationBox.reset();
		out["ok"] = true;
		return out;
	}
	if (!sm.notificationBox)
	{
		out["error"] = "no notification box (begin first)";
		return out;
	}
	InGameNotificationBoxMovieClip &box = *sm.notificationBox;
	if (kind == "show")
	{
		InGameNotificationBoxMovieClip::Notice n;
		if (!InGameNotificationBoxMovieClip::makeNotice(sm.notificationSettings, sm.environment.gameText, toNative(String(request.get("type", ""))),
				toNative(String(request.get("label", ""))), (int)(int64_t)request.get("seconds", (int64_t)0), (bool)request.get("override", false),
				toNative(String(request.get("override_image", ""))), n))
		{
			out["error"] = "unknown notification type '" + String(request.get("type", "")) + "' (RW 0x621E3A: nothing is shown)";
			out["state"] = box.state();
			return out;
		}
		n.location = (int)(int64_t)request.get("location", (int64_t)0);
		n.opaque = (bool)request.get("opaque", false);
		box.open(n);
	}
	else if (kind == "update")
	{
		out["audio"] = toGodot(box.update((unsigned)(int64_t)request.get("now_ms", (int64_t)0), (bool)request.get("letterboxed", false)));
	}
	else if (kind == "enable")
	{
		box.setEnabled((bool)request.get("on", true));
	}
	else if (kind == "close")
	{
		box.doClose();
	}
	else if (kind != "begin" && kind != "state")
	{
		out["error"] = "not a notification box request: " + String(kind.c_str());
		return out;
	}
	out["ok"] = true;
	out["state"] = box.state();
	out["level"] = box.level();
	out["width"] = box.messageWidth();
	if (box.hasActive())
	{
		out["title"] = toGodot(u8(box.active().title));
		out["message"] = toGodot(u8(box.active().message));
		out["icon"] = toGodot(box.active().icon);
	}
	PackedStringArray errors;
	for (const std::string &e : box.callErrors())
	{
		errors.push_back(toGodot(e));
	}
	out["call_errors"] = errors;
	return out;
}

Dictionary AptMenuPlayer::set_objectives_screen(const Dictionary &state)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	ObjectivesScreenInfo info;
	info.gameMode = (int)(int64_t)state.get("game_mode", (int64_t)0);
	info.tutorialInput = (bool)state.get("tutorial_input", false);
	const Array list = state.get("objectives", Array());
	for (int64_t i = 0; i < list.size(); ++i)
	{
		const Dictionary o = list[i];
		MissionObjective mo;
		mo.label = toNative(String(o.get("label", String())));
		mo.bonus = (bool)o.get("bonus", false);
		mo.shown = (bool)o.get("shown", true);
		mo.completed = (bool)o.get("completed", false);
		info.objectives.push_back(mo);
	}
	m->shell->objectivesScreen = info;
	m->shell->environment.objectivesScreen = &m->shell->objectivesScreen;
	Array rows;
	for (int row = 0; row < AptObjectivesMenu::kRows; ++row)
	{
		const std::string text = AptObjectivesMenu::objectiveText(info, row);
		if (text.empty())
		{
			break;
		}
		rows.push_back(toGodot(text + " " + AptObjectivesMenu::objectiveStatus(info, row)));
	}
	r["ok"] = true;
	r["rows"] = rows; // "$<label> <completed><bonus>" per row, as the providers answer
	return r;
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
	// lane PLATFORMICON-1: each slot's system (0 .. 3, display data); a value outside the set is no icon
	const Array platforms = state.get("platforms", Array());
	for (int i = 0; i < MAX_SLOTS && i < (int)platforms.size(); ++i)
	{
		const int64_t v = platforms[i];
		platformFromByte((std::uint8_t)(v >= 0 && v <= 3 ? v : 0), in.platforms[i]);
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
	ShellMode *sm = m->shell.get();
	LoadScreenInfo next = makeLoadScreenInfo(message.game, m->shell->setup, m->shell->text.get(),
		[sm](const std::string &name) { return sm->skins.images.findImageByName(name) != nullptr; });
	if (!next.error.empty())
	{
		out["error"] = toGodot(next.error);
		return out;
	}
	m->shell->loadScreen = std::move(next);
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
	if (info.mapStartCount > LEGACY_MAX_SLOTS) out["map_start_count"] = info.mapStartCount;
	out["rules_summary"] = toGodot(loadScreenU16ToUtf8(info.rulesSummary)); // lane RULES-1
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
	if (n == "profile-entry")
	{
		w = screen->profileEntryWindow(); // lane MENU-1: the add-profile box's name entry (Skirmish::CreatePersonaEntry)
	}
	else if (n == "cheat-mode")
	{
		w = screen->cheatModeWindow(); // lane CHEAT-1: the Rules tab's Cheat Mode check box (placed once the tab is shown)
	}
	else if (n.rfind("spot/", 0) == 0)
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

Dictionary AptMenuPlayer::lobby_state()
{
	Dictionary out;
	out["found"] = false;
	if (!m->shell || !m->shell->shell)
	{
		return out;
	}
	AptSkirmish *screen = dynamic_cast<AptSkirmish *>(m->shell->shell->findScreenByFilename("Skirmish.apt"));
	if (!screen || !screen->setup())
	{
		return out;
	}
	const SkirmishGameInfo &g = screen->setup()->info();
	out["found"] = true;
	out["map"] = toGodot(g.mapName);
	PackedStringArray mapKeys;
	for (const auto &key : screen->mapListKeys()) mapKeys.push_back(toGodot(key));
	out["map_keys"] = mapKeys;
	Array rules;
	for (std::int32_t r : g.rules)
	{
		rules.push_back((int64_t)r);
	}
	out["rules"] = rules;
	out["cheat_mode"] = (int64_t)g.cheatMode; // lane CHEAT-1: -1 off, else the cheat seat's slot
	Array slots;
	for (int i = 0; i < g.serializedSlotCount(); ++i)
	{
		const SkirmishGameSlot &s = g.slots[i];
		Dictionary d;
		d["state"] = (int64_t)s.state;
		d["template"] = (int64_t)s.playerTemplate;
		d["color"] = (int64_t)s.color;
		d["team"] = (int64_t)s.teamNumber;
		d["start"] = (int64_t)s.startPos;
		d["handicap"] = (int64_t)s.handicap; // lane OWNERFIX-2
		d["name"] = s.isOccupied() ? toGodot(u16ToUtf8(s.name)) : String();
		slots.push_back(d);
	}
	out["slots"] = slots;
	out["restored"] = toGodot(screen->restoredGame());
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
	if (!screen)
	{
		screen = dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt")); // lane ONLINE-4: the staging room's MpGameSetup
	}
	if (lan && spec.has("box"))
	{
		// lane UBERJOIN-1: a button of the LAN lobby's message box (the performance mode box: Yes / No), as the button runs it
		if (AptMessageBox *box = lan->messageBox())
		{
			box->hide(true);
			sm.wm->invokeCallback("_level" + std::to_string(box->level()) + ".MessageBox_OnButton" + toNative(String(spec["box"])), "");
			out["ok"] = true;
		}
		else
		{
			errors.push_back("no message box");
		}
		return out;
	}
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
	if (!lan && spec.has("box"))
	{
		// lane OWNERFIX-2: a button of the Rules tab's warning box (RW 0x8421E0 kind 3: MessageBoxOk RULE:CommandPointFactor / APT:CommandPointsTooHigh),
		// as the button's own command runs it (RW 0x953141: the type becomes none and the callback runs)
		AptMessageBox *box = screen->ruleBox();
		if (box && box->type() != AptMessageBox::TYPE_NONE)
		{
			sm.wm->invokeCallback("_level" + std::to_string(box->level()) + ".MessageBox_OnButton" + toNative(String(spec["box"])), "");
			out["ok"] = true;
			out["box_type_after"] = (int64_t)box->type();
		}
		else
		{
			errors.push_back("no rule warning box is up");
		}
		return out;
	}
	if (spec.has("profile"))
	{
		if (GameWindow *entry = screen->profileEntryWindow())
		{
			GadgetTextEntrySetText(entry, utf8ToU16(toNative(String(spec["profile"]))));
			// BFME1's persona popup accepts with Skirmish::PersonaAccept and closes with the root's PopUpControl("close") (docs/BFME1.md)
			const bool bfme1 = m->fs->is_bfme1();
			sm.wm->invokeCallback(bfme1 ? "AptSkirmish::Skirmish::PersonaAccept" : "AptSkirmish::OnAddProfileAccept", "");
			// the click on the popup's Select button also runs the movie's own close (the C++ click-through tests); the hook does the same
			std::string closeError;
			if (!(bfme1 ? sm.wm->invokeASAt(screen->level(), "", "PopUpControl", { "close" }, nullptr, &closeError) :
				sm.wm->invokeASAt(screen->level(), "ProfilePopup", "ClosePop", {}, nullptr, &closeError)))
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
	if (spec.has("legendary_heroes"))
	{
		// lane RULES-1: the Rules tab's Legendary Heroes combo (placed once the movie shows the tab): its entry, so the screen's own handler runs
		GameWindow *combo = screen->legendaryHeroesWindow();
		const int row = combo ? itemRow(combo, (std::intptr_t)(int64_t)spec["legendary_heroes"]) : -1;
		if (row < 0)
		{
			errors.push_back(combo ? "the Legendary Heroes combo has no such factor" : "the Rules tab's Legendary Heroes row is not placed (open the tab first)");
		}
		else
		{
			GadgetComboBoxSetSelectedPos(combo, row, false);
		}
	}
	if (spec.has("rule_combo"))
	{
		// lane OWNERFIX-2: a Rules tab combo row ([row, value]: RuleComboBox_<row>, e.g. row 1 = Command Points in the skirmish mode), its entry whose item
		// data is the value, so the panel's own message handler stores it (RW 0x986776); the tab must have been opened (the rows are placed then)
		const Array rc = spec["rule_combo"];
		GameWindow *combo = rc.size() == 2 ? screen->ruleComboWindow((int)(int64_t)rc[0]) : nullptr;
		const int row = combo ? itemRow(combo, (std::intptr_t)(int64_t)rc[1]) : -1;
		if (row < 0)
		{
			errors.push_back(combo ? "the rule combo has no such value" : "no such rule combo row (open the Rules tab first)");
		}
		else
		{
			GadgetComboBoxSetSelectedPos(combo, row, false);
		}
	}
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
		if (sd.has("handicap"))
		{
			// lane OWNERFIX-2: the Handicap combo's entry whose item data is the value (0, -5 .. -95), so the screen's own handler runs (RW 0x840485)
			GameWindow *combo = screen->slotGadget(slot, "Handicap");
			const int row = combo ? itemRow(combo, (std::intptr_t)(int64_t)sd["handicap"]) : -1;
			if (row < 0)
			{
				errors.push_back(toGodot("slot " + std::to_string(slot) + ": no handicap entry " + std::to_string((int)(int64_t)sd["handicap"])));
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

Dictionary AptMenuPlayer::set_load_screen(const Dictionary &info)
{
	Dictionary out;
	if (!m->shell)
	{
		out["error"] = "shell is not initialized";
		return out;
	}
	std::string error;
	const bool ok = loadScreenFromDictionary(info, m->shell->loadScreen, &error);
	out["ok"] = ok;
	if (!ok) out["error"] = toGodot(error);
	return out;
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
	for (const std::string &e : sm.services.problems)
	{
		errors.push_back(toGodot("services: " + e)); // lane OPTS-4: a volume or a sample the Options screen asked for without an audio manager
	}
	if (sm.profiles)
	{
		for (const std::string &e : sm.profiles->errors())
		{
			errors.push_back(toGodot("profiles: " + e)); // lane MENU-1: Skirmish.ini that could not be read or written
		}
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
	r["presentation_style"] = m->presentationFs.is_valid() && m->presentationFs->is_shell_source() ? "bfme2-106" : "rotwk-201";
	r["presentation_root"] = m->presentationFs.is_valid() ? m->presentationFs->install_root() : String();
	PackedStringArray presentationArchives;
	if (m->presentationFs.is_valid())
	{
		for (const MountedArchive &a : m->presentationFs->mountedArchives())
		{
			presentationArchives.push_back(toGodot(a.canonicalPath));
		}
	}
	r["presentation_archives"] = presentationArchives;
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
		if (m->shell->cursorTooltip && m->shell->metrics)
		{
			// lane HOVER-1: Mouse::drawTooltip once a frame (RW 0x5EE7A8) at the pointer's stage position
			const std::size_t before = m->shell->cursorTooltip->calls().size();
			const Vector2 window = windowSize();
			m->shell->cursorTooltip->update((std::uint32_t)Time::get_singleton()->get_ticks_msec(), window.x, window.y, *m->shell->metrics);
			if (m->shell->cursorTooltip->calls().size() != before)
			{
				m->dirty = true;
			}
		}
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
	for (const Impl::Segment &g : m->segments)
	{
		for (size_t i = g.items.size(); i-- > 0;)
		{
			rs->free_rid(g.items[i]);
		}
		rs->free_rid(g.parent);
	}
	m->segments.clear();
	for (size_t i = m->items.size(); i-- > 0;)
	{
		rs->free_rid(m->items[i]);
	}
	m->items.clear();
	m->drawnValid = false;
	m->gadgetAnchors.clear(); // lane MENU-1
	m->drawnBackdrop.clear(); // lane CAMP-2
	freeItemPool();
}

// lane SELPERF-1: a full redraw (the list changed: most HUD frames, a selection's command bar adds its items) reuses the last list's canvas items
// instead of freeing and making each again: they go to the pool, takeItem resets one (its draw commands, and every property the canvas, the gadgets
// and the native hooks set: the mask's canvas group, clip, custom rect, material, back-buffer copy) and gives it its new parent and index
void AptMenuPlayer::recycleCanvas()
{
	for (const Impl::Segment &g : m->segments)
	{
		m->itemPool.insert(m->itemPool.end(), g.items.begin(), g.items.end());
		m->itemPool.push_back(g.parent);
	}
	m->segments.clear();
	m->itemPool.insert(m->itemPool.end(), m->items.begin(), m->items.end());
	m->items.clear();
	m->drawnValid = false;
	m->drawnBackdrop.clear(); // lane CAMP-2
}

RID AptMenuPlayer::takeItem(RID parent, int drawIndex)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RID r;
	if (!m->itemPool.empty())
	{
		r = m->itemPool.back();
		m->itemPool.pop_back();
		rs->canvas_item_clear(r);
		rs->canvas_item_set_canvas_group_mode(r, RenderingServer::CANVAS_GROUP_MODE_DISABLED, 5.0f, false, 0.0f, false);
		rs->canvas_item_set_clip(r, false);
		rs->canvas_item_set_custom_rect(r, false, Rect2());
		rs->canvas_item_set_material(r, RID());
		rs->canvas_item_set_copy_to_backbuffer(r, false, Rect2());
	}
	else
	{
		r = rs->canvas_item_create();
		rs->canvas_item_set_default_texture_filter(r, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(r, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}
	rs->canvas_item_set_parent(r, parent);
	rs->canvas_item_set_draw_index(r, drawIndex);
	return r;
}

void AptMenuPlayer::freeItemPool()
{
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		for (size_t i = m->itemPool.size(); i-- > 0;)
		{
			rs->free_rid(m->itemPool[i]);
		}
	}
	m->itemPool.clear();
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
		m->drawnValid = false;
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
	// lane CAHPIC-1: Create-a-Hero's render callbacks in this picture: the guard clip (RW 0x919DA2 counts its draws) and the 3D view (RW 0x91A3A9 saves the
	// picture after drawing it); _process counts a frame for each frame the picture shows them (the canvas is rebuilt only when it changes)
	m->cahGuardShown = false;
	m->cahMapShown = false;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Placeholder && c.nativeTag && c.color.mul[3] > 0.0f)
		{
			m->cahGuardShown = m->cahGuardShown || c.symbolName == "CreateAHero::RenderPictureGuard";
			m->cahMapShown = m->cahMapShown || c.symbolName == "CreateAHero::DrawMapComponent";
		}
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
	in.imageReplacements = &m->imageReplacements;
	in.text = &m->strings;
	in.fonts = &m->fonts;
	AptTextRecordLookup recordLookup;
	if (m->shell || m->external)
	{
		WindowManager *wm = m->external ? m->external : m->shell->wm.get();
		recordLookup = [wm](const std::string &name, std::string &out) { return wm->aptTextShown(name, out); };
		in.textRecords = &recordLookup;
		in.textBadges = &wm->textBadges(); // lane PLATFORMICON-1: a field with an icon at its left leaves it room
	}
	BuildAptCanvas(rl, in, m->list);
	std::uint64_t t2 = time->get_ticks_usec();
	const bool haveGadgets = buildGadgets(m->gadgets);
	if (haveGadgets && m->shell && m->shell->wm)
	{
		appendTextBadges(rl, m->shell->wm->textBadges(), m->gadgets); // lane PLATFORMICON-1: the icons beside the movie rows' names
	}
	// lane HUDREDRAW-1: the list's segments against the ones on screen (AptCanvasPlanRedraw): with the same gadgets on screen, the segments whose ops changed
	// are drawn again, the others keep their items
	const std::vector<AptCanvasSegment> segments = AptCanvasSegments(m->list);
	AptCanvasRedrawPlan plan;
	bool sameShape = !m->stillLoadOn && m->drawnValid && (!haveGadgets || sameGadgets(m->gadgets, m->drawnGadgets));
	if (sameShape)
	{
		std::vector<AptCanvasSegment> drawn;
		drawn.reserve(m->segments.size());
		for (const Impl::Segment &g : m->segments)
		{
			drawn.push_back(AptCanvasSegment{ g.firstLevel, g.lastLevel, g.opBegin, g.opEnd });
		}
		plan = AptCanvasPlanRedraw(m->list, segments, m->drawnList, drawn);
		sameShape = !plan.full;
	}
	const bool unchanged = sameShape && segments.size() == m->segments.size() &&
		std::all_of(plan.same.begin(), plan.same.end(), [](bool same) { return same; });
	if (unchanged)
	{
		// lane PERF-1 r2: the HUD's canvas list (and in shell mode its gadget list) is the same as the one on screen in most render frames; its items
		// stay, only the native components are drawn again, each into a new item at its place (what a full redraw does for them)
		for (size_t i = 0; i < m->segments.size(); ++i)
		{
			redrawNativePlaceholders(i);
			redrawCallbackPlaceholders(i); // lane UI-4
		}
		countLevels(plan);
		m->unchangedFrames += 1;
	}
	else if (sameShape && (!haveGadgets || m->gadgets.commands.empty()))
	{
		// lane HUDREDRAW-1: some segments changed (a movie looping a state: the spell store's buyable powers, the Palantir's powers button). A gadget window is
		// drawn under its placeholder's item (drawGadgets): with gadgets on screen a redrawn segment could take their anchor away, so that case draws
		// everything again (below)
		redrawSegments(segments, plan);
		countLevels(plan);
		m->partialFrames += 1;
	}
	else
	{
		drawCanvas(m->list);
		drawGadgets(m->gadgets);
		drawStillLoadScreen(); // lane MENU-1
		freeItemPool(); // the last list's items this one did not reuse
		m->drawnList = m->list;
		m->drawnGadgets = m->gadgets;
		m->drawnValid = true;
		AptCanvasRedrawPlan all;
		all.old.assign(m->segments.size(), -1);
		all.same.assign(m->segments.size(), false);
		countLevels(all);
	}
	std::uint64_t t3 = time->get_ticks_usec();
	m->listUs += t1 - t0;
	m->canvasUs += t2 - t1;
	m->submitUs += t3 - t2;
	m->rebuilds += 1;
	m->noteList();
}

void AptMenuPlayer::drawRenderCallback(const AptCanvasOp &op, const RID &item)
{
	// lane UI-4: the callback (AptMainMenu::RenderCredits, RW 0x91B1B8 -> the credits roll's draw RW 0x9C6765) is given the clip's rectangle in stage
	// units; its texts are drawn with the canvas fonts at the stage-to-window mapping of the op, each with its drop colour one unit down-right
	const WindowManager::RenderCallback *callback = m->shell ? m->shell->wm->renderCallback(op.symbolName) : nullptr;
	if (!callback)
	{
		return;
	}
	float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
	const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
	const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
	for (int i = 0; i < 4; ++i)
	{
		const float sx = op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx;
		const float sy = op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty;
		minx = std::min(minx, sx);
		maxx = std::max(maxx, sx);
		miny = std::min(miny, sy);
		maxy = std::max(maxy, sy);
	}
	GadgetDrawList texts;
	(*callback)(minx, miny, maxx - minx, maxy - miny, texts);
	const float k = std::min(op.scaleX, op.scaleY);
	auto colourOf = [](std::uint32_t v) { return Color(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, ((v >> 24) & 0xFF) / 255.0f); };
	for (const GadgetDrawCommand &c : texts.commands)
	{
		if (c.kind != GadgetDrawCommand::Kind::Text || c.text.empty() || (c.color >> 24) == 0)
		{
			continue;
		}
		float drawSize = 0;
		Ref<Font> font = m->fontFor(c.font.name, (float)c.font.pointSize, &drawSize, c.font.bold); // CreditsMinorTitleFont is bold
		if (font.is_null())
		{
			continue;
		}
		++m->callbackTextDraws[c.font.bold ? 1 : 0];
		const int px = std::max(1, (int)std::lround(drawSize * k));
		font = m->cellFont(font, px); // lane UI-6: the string's top at y, its baseline where RotWK's GDI cell has it
		const Vector2 at((float)c.x0 * op.scaleX + op.offsetX, (float)c.y0 * op.scaleY + op.offsetY);
		const Vector2 base = at + Vector2(0, font->get_ascent(px));
		const String text = toGodot(u16ToUtf8(c.text));
		if ((c.dropColor >> 24) != 0)
		{
			font->draw_string(item, base + Vector2(1, 1) * k, text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, colourOf(c.dropColor));
		}
		font->draw_string(item, base, text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, colourOf(c.color));
		m->noteDrawnText("callback", text, colourOf(c.color), Rect2(at, Vector2(font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, px).x, font->get_height(px))));
	}
}

void AptMenuPlayer::redrawCallbackPlaceholders(size_t segment)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	Impl::Segment &g = m->segments[segment];
	for (const Impl::NativeSlot &slot : g.callbackSlots)
	{
		RID &item = g.items[slot.item];
		rs->free_rid(item);
		item = rs->canvas_item_create();
		rs->canvas_item_set_parent(item, slot.parent);
		rs->canvas_item_set_draw_index(item, static_cast<int>(slot.item));
		rs->canvas_item_set_default_texture_filter(item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(item, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		drawRenderCallback(m->drawnList.ops[g.opBegin + slot.op], item);
	}
}

void AptMenuPlayer::redrawNativePlaceholders(size_t segment)
{
	if (!m->nativeHook)
	{
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	const Impl::Segment &g = m->segments[segment];
	for (const Impl::NativeSlot &slot : g.nativeSlots)
	{
		// lane SELPERF-1: the slot's item is cleared and drawn again in place (it keeps its parent, draw index and texture settings); freeing and making
		// a new item per slot every frame cost the selection's command bar (its button images, the portrait) about 0.1 ms a frame on the Deck
		RID item = g.items[slot.item];
		rs->canvas_item_clear(item);
		m->nativeHook->drawPlaceholder(m->drawnList.ops[g.opBegin + slot.op], item);
	}
}

void AptMenuPlayer::countLevels(const AptCanvasRedrawPlan &plan)
{
	// lane HUDREDRAW-1 (get_stats "levels"): per level, whether this rebuild drew some of its segments again and how many
	std::map<int, std::pair<std::uint64_t, std::uint64_t>> seen; // level -> (segments, drawn again)
	for (size_t i = 0; i < m->segments.size(); ++i)
	{
		auto &c = seen[m->segments[i].firstLevel];
		c.first += 1;
		c.second += i < plan.same.size() && plan.same[i] ? 0 : 1;
	}
	for (auto &kv : m->levelCounts)
	{
		kv.second.segments = 0; // a level no longer drawn has none
	}
	for (const auto &kv : seen)
	{
		Impl::LevelCounts &counts = m->levelCounts[kv.first];
		counts.segments = kv.second.first;
		counts.segmentsRedrawn += kv.second.second;
		(kv.second.second ? counts.redrawnFrames : counts.keptFrames) += 1;
	}
}

void AptMenuPlayer::redrawSegments(const std::vector<AptCanvasSegment> &segments, const AptCanvasRedrawPlan &plan)
{
	// lane HUDREDRAW-1: the drawn list takes the new list's ops (the kept segments' ops are the same: moved, not copied); a kept segment keeps its items, a
	// changed one with an old counterpart is drawn again under that one's item, the rest under new items; old segments nothing replaces go to the pool
	RenderingServer *rs = RenderingServer::get_singleton();
	std::vector<bool> taken(m->segments.size(), false);
	std::set<std::string> oldPaths; // the placeholders of what is drawn again: their BinkMovies and View3D viewers are marked unused (used again if still drawn)
	auto release = [&](Impl::Segment &g, bool parentToo) {
		for (size_t k = g.opBegin; k < g.opEnd; ++k)
		{
			const AptCanvasOp &op = m->drawnList.ops[k];
			if (op.kind == AptCanvasOp::Kind::Placeholder)
			{
				oldPaths.insert(op.path);
			}
		}
		m->itemPool.insert(m->itemPool.end(), g.items.begin(), g.items.end());
		if (parentToo)
		{
			m->itemPool.push_back(g.parent);
		}
		g.items.clear();
		g.nativeSlots.clear();
		g.callbackSlots.clear();
	};
	std::vector<Impl::Segment> next(segments.size());
	std::vector<AptCanvasOp> ops;
	ops.reserve(m->list.ops.size());
	bool moved = false; // a segment's parent took another place among the player's children
	for (size_t j = 0; j < segments.size(); ++j)
	{
		Impl::Segment &n = next[j];
		n.firstLevel = segments[j].firstLevel;
		n.lastLevel = segments[j].lastLevel;
		const std::ptrdiff_t o = plan.old[j];
		if (o >= 0)
		{
			Impl::Segment &g = m->segments[(size_t)o];
			taken[(size_t)o] = true;
			if (!plan.same[j])
			{
				release(g, false);
			}
			n.parent = g.parent;
			n.items = std::move(g.items);
			n.nativeSlots = std::move(g.nativeSlots);
			n.callbackSlots = std::move(g.callbackSlots);
			moved = moved || (size_t)o != j;
		}
		n.opBegin = ops.size();
		if (o >= 0 && plan.same[j])
		{
			const Impl::Segment &g = m->segments[(size_t)o];
			ops.insert(ops.end(), std::make_move_iterator(m->drawnList.ops.begin() + (std::ptrdiff_t)g.opBegin),
				std::make_move_iterator(m->drawnList.ops.begin() + (std::ptrdiff_t)g.opEnd));
		}
		else
		{
			ops.insert(ops.end(), m->list.ops.begin() + (std::ptrdiff_t)segments[j].begin, m->list.ops.begin() + (std::ptrdiff_t)segments[j].end);
		}
		n.opEnd = ops.size();
	}
	for (size_t k = 0; k < m->segments.size(); ++k)
	{
		if (!taken[k])
		{
			release(m->segments[k], true);
			moved = true;
		}
	}
	for (auto &kv : m->movies)
	{
		if (oldPaths.count(kv.first))
		{
			kv.second.drawn = false;
		}
	}
	m->drawnList.ops.swap(ops);
	m->segments.swap(next);
	const RID root = get_canvas_item();
	const std::map<std::string, const GameWindow *> gadgetOfPath = gadgetPaths();
	m->view3d.beginBuild(&oldPaths);
	for (size_t j = 0; j < m->segments.size(); ++j)
	{
		Impl::Segment &g = m->segments[j];
		if (!g.parent.is_valid())
		{
			g.parent = takeItem(root, static_cast<int>(1 + j));
		}
		else if (moved)
		{
			rs->canvas_item_set_draw_index(g.parent, static_cast<int>(1 + j));
		}
		if (plan.old[j] >= 0 && plan.same[j])
		{
			redrawNativePlaceholders(j);
			redrawCallbackPlaceholders(j);
		}
		else
		{
			drawSegment(m->drawnList, j, gadgetOfPath);
		}
	}
	m->view3d.endBuild();
	freeItemPool(); // the replaced segments' items their new drawing did not reuse
}

void AptMenuPlayer::drawCanvas(const AptCanvasList &list)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	recycleCanvas();
	for (auto &kv : m->movies)
	{
		kv.second.drawn = false; // lane CAMP-2: a BinkMovie this list does not draw is closed (advanceMovies)
	}
	const RID root = get_canvas_item();
	auto newItem = [&](RID parent) {
		// Godot sorts the children of a parent by their draw index, which is 0 for every new item, with an unstable sort: siblings
		// need increasing indices to keep the render list's order (review finding; masks and material changes make many siblings)
		RID r = takeItem(parent, static_cast<int>(m->items.size()));
		m->items.push_back(r);
		return r;
	};
	// lane FB7-1: the display's backdrop behind every Apt level (Shell::update RW 0x75E1D3 -> RW 0x65C42C slot 0): the mapped image ShellMapLowLOD
	// (HandCreatedMappedImages.ini: InstallLoad.tga, 1024 x 768 of a 1024 x 1024 texture).
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
			const Ref<ImageTexture> texture = m->imageTexture(sm, img, name);
			if (texture.is_valid())
			{
				const AptStageMapping backdrop = m->mapping.fitImage((float)img->imageSize.x, (float)img->imageSize.y);
				float x0, y0, x1, y1;
				backdrop.stageToWindow(0.0f, 0.0f, x0, y0);
				backdrop.stageToWindow(backdrop.stageW, backdrop.stageH, x1, y1);
				const RID backdropItem = newItem(root);
				addTextureUV(rs, backdropItem, Rect2(Vector2(x0, y0), Vector2(x1 - x0, y1 - y0)), texture, img->uvLo, img->uvHi, Color(1, 1, 1, 1));
				m->drawnBackdrop = name; // lane CAMP-2
				if (m->fs.is_valid() && m->fs->is_bfme1())
				{
					Impl::MovieSlot &movie = m->movies["backdrop:FrontEnd"];
					movie.drawn = true;
					if (movie.stream.is_null() && movie.error.empty())
					{
						movie.title = "FrontEnd";
						movie.loop = true;
						GameWorld *world = Object::cast_to<GameWorld>(ObjectDB::get_instance(m->worldId));
						const Dictionary info = world ? world->get_movie("FrontEnd") : Dictionary();
						if (!(bool)info.get("ok", false))
						{
							movie.error = toNative(String(info.get("error", "no GameWorld")));
						}
						else
						{
							movie.stream.instantiate();
							const Dictionary opened = movie.stream->open(String(info.get("full_path", "")));
							if (!(bool)opened.get("ok", false))
							{
								movie.error = toNative(String(opened.get("error", "?")));
								movie.stream.unref();
							}
							else
							{
								movie.durationMs = (double)opened.get("duration_ms", 0.0);
								movie.fps = (double)opened.get("fps", 30.0);
								if (!movie.stream->advance_to(0))
								{
									movie.error = toNative(movie.stream->get_error());
								}
							}
						}
						if (!movie.error.empty())
						{
							m->movieErrors.insert("FrontEnd: " + movie.error);
							UtilityFunctions::printerr(toGodot("MOVIE ERROR FrontEnd: " + movie.error));
						}
					}
					if (movie.stream.is_valid() && movie.error.empty())
					{
						rs->canvas_item_add_texture_rect(backdropItem, Rect2(Vector2(x0, y0), Vector2(x1 - x0, y1 - y0)), movie.stream->get_texture()->get_rid());
					}
				}
			}
		}
	}
	m->gadgetAnchors.clear();
	const std::map<std::string, const GameWindow *> gadgetOfPath = gadgetPaths();
	m->view3d.beginBuild(); // lane UI-2
	// lane HUDREDRAW-1: each segment under an item of its own, in list order after the backdrop
	int labelled = 0;
	for (const AptCanvasSegment &r : AptCanvasSegments(list))
	{
		Impl::Segment g;
		g.firstLevel = r.firstLevel;
		g.lastLevel = r.lastLevel;
		g.opBegin = r.begin;
		g.opEnd = r.end;
		g.parent = takeItem(root, static_cast<int>(1 + m->segments.size()));
		m->segments.push_back(std::move(g));
		if (m->segments.size() == 1 || r.firstLevel != labelled)
		{
			// "_level<n>.<clip>...": the level and its first top clip name the level's movie in get_stats (a level loads other movies over a session)
			labelled = r.firstLevel;
			const std::string &path = list.ops[r.begin].path;
			const size_t dot = path.find('.');
			m->levelCounts[r.firstLevel].movie = path.substr(0, dot == std::string::npos ? std::string::npos : path.find('.', dot + 1));
		}
		drawSegment(list, m->segments.size() - 1, gadgetOfPath);
	}
	m->view3d.endBuild(); // lane UI-2: viewers no op drew stop rendering
}

// lane MENU-1: the gadget window of each component placeholder path (WindowManager's component records)
std::map<std::string, const GameWindow *> AptMenuPlayer::gadgetPaths() const
{
	std::map<std::string, const GameWindow *> gadgetOfPath;
	if (m->shell)
	{
		for (const WindowManager::ComponentRecord &rec : m->shell->wm->components())
		{
			if (rec.instance && rec.window)
			{
				gadgetOfPath[rec.instancePath] = rec.window.get();
			}
		}
	}
	return gadgetOfPath;
}

// lane HUDREDRAW-1: the ops of one segment under its own item (drawCanvas, redrawSegments)
void AptMenuPlayer::drawSegment(const AptCanvasList &list, size_t segment, const std::map<std::string, const GameWindow *> &gadgetOfPath)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	Impl::Segment &g = m->segments[segment];
	std::vector<RID> parents{ g.parent };
	RID current;
	auto newItem = [&](RID parent) {
		// Godot sorts the children of a parent by their draw index, which is 0 for every new item, with an unstable sort: siblings
		// need increasing indices to keep the render list's order (review finding; masks and material changes make many siblings)
		RID r = takeItem(parent, static_cast<int>(g.items.size()));
		g.items.push_back(r);
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

	for (size_t opIndex = g.opBegin; opIndex < g.opEnd; ++opIndex)
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
				memcpy(static_cast<void *>(pts.ptrw()), op.positions.data(), (size_t)n * sizeof(float) * 2);
				PackedColorArray cols;
				cols.resize(n);
				memcpy(static_cast<void *>(cols.ptrw()), op.colors.data(), (size_t)n * sizeof(float) * 4);
				PackedVector2Array uvs;
				if (!op.uvs.empty())
				{
					uvs.resize(n);
					memcpy(static_cast<void *>(uvs.ptrw()), op.uvs.data(), (size_t)n * sizeof(float) * 2);
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
					auto branded = m->brandTextures.find(op.texture);
					if (branded != m->brandTextures.end())
					{
						RID brandItem = newItem(parents.back());
						rs->canvas_item_set_default_texture_filter(brandItem, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS);
						rs->canvas_item_add_triangle_array(brandItem, idx, pts, cols, uvs, PackedInt32Array(), PackedFloat32Array(), branded->second->get_rid());
						current = RID();
						break;
					}
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
				if (!gadgetOfPath.empty())
				{
					// lane MENU-1: RW 0x814BEC draws a component's window when the movie's render reaches its clip (GameWindowManager::drawWindow,
					// RW 0x6C1B52): an empty item here, at the clip's depth, is the parent of the window's draw items (drawGadgets)
					auto g = gadgetOfPath.find(op.path);
					if (g != gadgetOfPath.end())
					{
						m->gadgetAnchors[g->second] = newItem(parents.back());
						current = RID();
					}
				}
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
					// Sol r1 (ONLINE-5, S-4555): only a shell menu screen's View3D (the 3D menu frame) stretches with the stage
					std::vector<int> menuLevels;
					if (m->shell && m->shell->shell)
					{
						for (int i = 0; i < m->shell->shell->screenCount(); ++i)
						{
							if (const AptScreen *screen = m->shell->shell->screenAt(i))
							{
								menuLevels.push_back(screen->level());
							}
						}
					}
					m->view3d.draw(item, Rect2(minx, miny, maxx - minx, maxy - miny), op, AptView3DOnMenuLevel(op.path, menuLevels));
					break;
				}
				if (m->shell && op.nativeTag && m->shell->wm->renderCallback(op.symbolName))
				{
					// lane UI-4: an engine render callback that draws text (AptMainMenu::RenderCredits): drawn in the clip's place, and again in every
					// render frame whose canvas list is unchanged (its text moves without the list changing)
					g.callbackSlots.push_back(Impl::NativeSlot{ opIndex - g.opBegin, g.items.size(), parents.back() });
					RID item = newItem(parents.back());
					current = RID();
					drawRenderCallback(op, item);
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
						Dictionary info;
						if (m->presentationFs.is_valid() && m->presentationFs->is_shell_source() && m->presentationVideos && !title.empty())
						{
							// lane MODCOMPAT-1: the mod's Data\Movies (RW 0x490D1C's Mod Path) and the install-folder mods' movies too: a mod's own intro and menu movies
							const VideoStreamInfo opened = m->presentationVideos->locate(title, toNative(m->presentationFs->install_root()),
								m->presentationFs->modFolder(), DetectRetailLanguageOfInstall(toNative(m->presentationFs->install_root())), // the install's lang\<Language>*.big ("English")
								m->presentationFs->installOverlayFolders());
							info["ok"] = opened.ok;
							info["full_path"] = toGodot(opened.fullPath);
							info["error"] = toGodot(opened.error);
						}
						else
						{
							GameWorld *world = Object::cast_to<GameWorld>(ObjectDB::get_instance(m->worldId));
							info = world && !title.empty() ? world->get_movie(toGodot(title)) : Dictionary();
						}
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
					g.nativeSlots.push_back(Impl::NativeSlot{ opIndex - g.opBegin, g.items.size(), parents.back() });
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
							// Donor AptPlayer::FindRenderImage (BFME2 0x4128F0
							// parameter lookup): after records, try the mapped image
							// name itself (VerticalScrollBar's button/thumb artwork).
							if (!record && m->shell->skins.images.findImageByName(kv.second)) record = &kv.second;
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
						// LogoWithShadow, RW 0x91CA5C, and MainMenu's `Image` clip has no `_imageMap`). Lane MENU-1: first the record of its path below
						// the level's root with '/' (LoadScreen's init names "UIClip/Level/<card>" for the clip UIClip.Level.<card>, RW 0x81CFFA; for
						// MainMenu's root-level `Image` both are "Image"). INFERENCE (S-3506): the RenderImage callback's name rule was not read
						const size_t first = op.path.find('.');
						std::string below = first == std::string::npos ? std::string() : op.path.substr(first + 1);
						std::replace(below.begin(), below.end(), '.', '/');
						record = below.empty() ? nullptr : m->shell->wm->aptImage(below);
						if (!record)
						{
							const size_t dot = op.path.find_last_of('.');
							record = m->shell->wm->aptImage(dot == std::string::npos ? op.path : op.path.substr(dot + 1));
						}
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
							AptTextureStore *pictureTextures = m->originalTextures ? m->originalTextures.get() : m->textures.get();
							const AptTextureStore::Entry &entry = pictureTextures->getFile(picture->file);
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
						tex = m->imageTexture(*m->shell, img, *record);
					}
					if (tex.is_valid())
					{
						float imageScaleX = op.scaleX, imageScaleY = op.scaleY;
						float imageOffsetX = op.offsetX, imageOffsetY = op.offsetY;
						if (record && *record == "LogoWithShadow" && m->shell->shell)
						{
							const ::Image *backdropImage = m->shell->skins.images.findImageByName(m->shell->shell->backdropImage());
							if (backdropImage)
							{
								const AptStageMapping artwork = m->mapping.fitImage((float)backdropImage->imageSize.x, (float)backdropImage->imageSize.y);
								imageScaleX = artwork.scaleX();
								imageScaleY = artwork.scaleY();
								imageOffsetX = artwork.offsetX();
								imageOffsetY = artwork.offsetY();
							}
						}
						float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
						const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
						const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
						for (int i = 0; i < 4; ++i)
						{
							const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * imageScaleX + imageOffsetX;
							const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * imageScaleY + imageOffsetY;
							minx = std::min(minx, sx);
							maxx = std::max(maxx, sx);
							miny = std::min(miny, sy);
							maxy = std::max(maxy, sy);
						}
						const Vector2 size = tex->get_size();
						const Rect2 src(img->uvLo[0] * size.x, img->uvLo[1] * size.y, (img->uvHi[0] - img->uvLo[0]) * size.x, (img->uvHi[1] - img->uvLo[1]) * size.y);
						RID item = newItem(parents.back());
						current = RID();
						// RenderImage._mode is also used by the CAH medal's _gray/_color
						// frames. Reuse the HUD's decoded mode parser and S-5016 shaders.
						std::string modeText;
						for (const auto &kv : op.nativeVars) if (kv.first == "_mode") modeText = kv.second;
						const AptImageMode mode = parseAptImageMode(modeText);
						if (mode != APT_IMAGE_ALPHA)
						{
							auto &material = m->imageModes[(int)mode];
							if (material.is_null())
							{
								Ref<Shader> shader; shader.instantiate();
								shader->set_code(String(aptImageModeShader(mode).c_str()));
								material.instantiate(); material->set_shader(shader);
							}
							rs->canvas_item_set_material(item, material->get_rid());
						}
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
				const float fieldScale = AptTextFontScale(op.matrix); // lane MENUFIX-1: the field's vertical scale (AptCanvas.h), not sqrt(|det|)
				if (det == 0.0f || fieldScale <= 0.0f)
				{
					break;
				}
				// the font follows the device scale by min(scaleX, scaleY) (donor AptDisplayStringAllocation_ctor.cpp:105-110)
				const float k = std::min(op.scaleX, op.scaleY);
				const int px = std::max(1, (int)std::lround(op.drawSize * k * fieldScale));
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
				font = m->cellFont(font, px); // lane UI-6: RotWK's GDI cell (the string height PlaceAptText centres, the baseline in it)
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
	if (m->shell->nav)
	{
		syncNav();
		m->shell->nav->appendFocusFrame(commands); // lane OPTS-6: over everything (no root: drawn after the movies)
	}
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
		if (x.kind != y.kind || x.root != y.root || x.x0 != y.x0 || x.y0 != y.y0 || x.x1 != y.x1 || x.y1 != y.y1 || x.color != y.color || x.dropColor != y.dropColor ||
			x.lineWidth != y.lineWidth || x.wrapWidth != y.wrapWidth || x.wrapCentered != y.wrapCentered || x.image != y.image || x.text != y.text ||
			x.window != y.window || x.font.name != y.font.name || x.font.pointSize != y.font.pointSize || x.font.bold != y.font.bold || x.font.height != y.font.height)
		{
			return false;
		}
	}
	return true;
}

// lane MENU-1: the picture loading screen of a campaign mission (RW 0x7583CC, GameClient/GUI/StillImageLoadScreen.h) over everything the shell draws
// (retail's shell is gone at a game's start): black, the display's image slots in slot order, the "Loading" text and the SmallRing Video
void AptMenuPlayer::drawStillLoadScreen()
{
	if (!m->stillLoadOn || !m->shell)
	{
		return;
	}
	ShellMode &sm = *m->shell;
	RenderingServer *rs = RenderingServer::get_singleton();
	const AptStageMapping &map = m->mapping;
	auto newItem = [&]() {
		RID r = rs->canvas_item_create();
		rs->canvas_item_set_parent(r, get_canvas_item());
		rs->canvas_item_set_draw_index(r, static_cast<int>(1 + m->segments.size() + m->items.size())); // lane HUDREDRAW-1: after the movies' segments
		rs->canvas_item_set_default_texture_filter(r, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
		rs->canvas_item_set_default_texture_repeat(r, RenderingServer::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		m->items.push_back(r);
		return r;
	};
	// screen fractions over the 1024 x 768 stage (S-3505)
	auto toWindow = [&](float fx, float fy) {
		float wx, wy;
		map.stageToWindow(fx * map.stageW, fy * map.stageH, wx, wy);
		return Vector2(wx, wy);
	};
	RID item = newItem();
	rs->canvas_item_add_rect(item, Rect2(Vector2(0, 0), Vector2(map.windowW, map.windowH)), Color(0, 0, 0, 1));
	for (const StillImageSlot &slot : m->stillLoad.slots)
	{
		if (!slot.on)
		{
			continue;
		}
		const ::Image *img = sm.skins.images.findImageByName(slot.image);
		if (!img)
		{
			sm.gadgetErrors.insert("load screen image '" + slot.image + "' is not in the mapped image collection");
			continue;
		}
		Ref<ImageTexture> texture = m->imageTexture(sm, img, slot.image);
		if (texture.is_null())
		{
			continue;
		}
		const Vector2 a = toWindow(slot.x0, slot.y0), b = toWindow(slot.x1, slot.y1);
		addTextureUV(rs, item, Rect2(a, b - a), texture, img->uvLo, img->uvHi, Color(1, 1, 1, 1));
	}
	// the ring: the Video SmallRing, looping (advanceMovies keeps a drawn slot going)
	Impl::MovieSlot &ring = m->movies["loadscreen:" + m->stillLoad.movie];
	ring.drawn = true;
	if (ring.stream.is_null() && ring.error.empty())
	{
		ring.title = m->stillLoad.movie;
		ring.loop = true;
		GameWorld *world = Object::cast_to<GameWorld>(ObjectDB::get_instance(m->worldId));
		Dictionary info = world ? world->get_movie(toGodot(ring.title)) : Dictionary();
		if (!(bool)info.get("ok", false))
		{
			ring.error = toNative(String(info.get("error", "no GameWorld")));
		}
		else
		{
			ring.stream.instantiate();
			Dictionary opened = ring.stream->open(String(info.get("full_path", "")));
			if (!(bool)opened.get("ok", false))
			{
				ring.error = toNative(String(opened.get("error", "?")));
				ring.stream.unref();
			}
			else
			{
				ring.durationMs = (double)opened.get("duration_ms", 0.0);
				ring.fps = (double)opened.get("fps", 30.0);
				ring.stream->advance_to(0);
			}
		}
		if (!ring.error.empty())
		{
			m->movieErrors.insert("load screen " + ring.title + ": " + ring.error);
		}
	}
	if (ring.stream.is_valid() && ring.error.empty())
	{
		// the frame of the time since the screen came up, looping
		const double ms = (double)(Time::get_singleton()->get_ticks_usec() - m->stillLoadStartUs) / 1000.0;
		const double inLoop = ring.durationMs > 1.0 ? std::fmod(ms, ring.durationMs) : 0.0;
		const int64_t target = std::min<int64_t>((int64_t)(inLoop * ring.fps / 1000.0), ring.stream->get_frame_count() - 1);
		if (target < ring.stream->get_frame())
		{
			ring.stream->restart();
		}
		if (target > ring.stream->get_frame() && !ring.stream->advance_to(target))
		{
			ring.error = toNative(ring.stream->get_error());
			m->movieErrors.insert("load screen " + ring.title + ": " + ring.error);
		}
	}
	if (ring.stream.is_valid())
	{
		const float *r = m->stillLoad.movieRect;
		const Vector2 a = toWindow(r[0], r[1]), b = toWindow(r[2], r[3]);
		rs->canvas_item_add_texture_rect(newItem(), Rect2(a, b - a), ring.stream->get_texture()->get_rid());
	}
	// "Loading": GUI:Loading in SachaWynter 18, centred on 0.5, 0.03125 (INFERENCE S-3505: centred on x, the top at y)
	std::u16string label;
	if (!sm.text || !sm.text->fetch(m->stillLoad.textLabel, label))
	{
		sm.gadgetErrors.insert("load screen text: no game text " + m->stillLoad.textLabel);
		return;
	}
	float drawSize = 0;
	Ref<Font> font = m->fontFor(m->stillLoad.textFont, (float)m->stillLoad.textSize, &drawSize);
	if (font.is_null())
	{
		return;
	}
	const int px = std::max(1, (int)std::lround(drawSize * map.uniformScale()));
	font = m->cellFont(font, px); // lane UI-6: the top at y, the baseline in RotWK's GDI cell
	const String text = toGodot(u16ToUtf8(label));
	const Vector2 size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, px);
	const Vector2 at = toWindow(m->stillLoad.textX, m->stillLoad.textY);
	const unsigned int c = m->stillLoad.textColor;
	const Color colour(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f);
	font->draw_string(newItem(), Vector2(at.x - size.x * 0.5f, at.y + font->get_ascent(px)), text, HORIZONTAL_ALIGNMENT_LEFT, -1, px, colour);
	if (m->stillProgress < 0.0)
	{
		return;
	}
	// lane SAVE-4e (S-5012, OpenBFME's): an old save's catch-up between the "Loading" text and the picture (y 0.1640625): the text in the screen's font at
	// two thirds of its size, centred at y 0.085, and a bar in the text's colour from x 0.3 to 0.7, y 0.128 to 0.142 (a one-pixel frame, the fill at percent)
	float smallSize = 0;
	Ref<Font> small = m->fontFor(m->stillLoad.textFont, (float)m->stillLoad.textSize * 2.0f / 3.0f, &smallSize);
	if (small.is_valid() && !m->stillProgressText.empty())
	{
		const int spx = std::max(1, (int)std::lround(smallSize * map.uniformScale()));
		small = m->cellFont(small, spx);
		const String line = toGodot(m->stillProgressText);
		const Vector2 lineSize = small->get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, spx);
		const Vector2 lineAt = toWindow(0.5f, 0.085f);
		small->draw_string(newItem(), Vector2(lineAt.x - lineSize.x * 0.5f, lineAt.y + small->get_ascent(spx)), line, HORIZONTAL_ALIGNMENT_LEFT, -1, spx,
			colour);
	}
	const Vector2 b0 = toWindow(0.3f, 0.128f), b1 = toWindow(0.7f, 0.142f);
	RID bar = newItem();
	rs->canvas_item_add_rect(bar, Rect2(b0, b1 - b0), Color(colour.r, colour.g, colour.b, 0.85f));
	const float inset = std::max(1.0f, map.uniformScale());
	const Vector2 i0 = b0 + Vector2(inset, inset), i1 = b1 - Vector2(inset, inset);
	rs->canvas_item_add_rect(bar, Rect2(i0, i1 - i0), Color(0, 0, 0, 1));
	const float filled = (float)(m->stillProgress / 100.0) * (i1.x - i0.x);
	if (filled > 0.0f)
	{
		rs->canvas_item_add_rect(bar, Rect2(i0, Vector2(filled, i1.y - i0.y)), colour);
	}
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
	// lane MENU-1: each top-level window's commands go under its placeholder's anchor (drawCanvas); the open drop-down list (the lone window, ZH
	// GadgetComboBox) and windows without a placeholder this frame are drawn after the movies. INFERENCE (S-3503): retail's drop-down list of a lobby
	// combo over the rows below it was not read; it is kept on top so the rows drawn later do not cover it
	// lane OPTS-4: only while its list shows: a combo box that stayed the lone window after a choice (ZH) is drawn at its placeholder's depth again, so
	// it no longer shows through a pop-up placed over it later (the known issue of S-1914)
	const GameWindow *lone = sm.layer->openDropDown();
	const GameWindow *currentRoot = nullptr;
	bool firstCommand = true;
	m->unanchoredGadgets = 0;
	auto newItem = [&](RID parent) {
		RID r = takeItem(parent, static_cast<int>(1 + m->segments.size() + m->items.size())); // lane HUDREDRAW-1: a root item after the movies' segments
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
		if (firstCommand || c.root != currentRoot)
		{
			firstCommand = false;
			currentRoot = c.root;
			auto anchor = c.root && c.root != lone ? m->gadgetAnchors.find(c.root) : m->gadgetAnchors.end();
			if (c.root && c.root == lone)
			{
				sm.deviceUnverified.insert("[S-3503] an open drop-down list is drawn over the movies, not at its combo box's depth (retail's draw of the open list was not read)");
			}
			parents.assign(1, anchor != m->gadgetAnchors.end() ? anchor->second : get_canvas_item());
			if (anchor == m->gadgetAnchors.end())
			{
				m->unanchoredGadgets += 1;
			}
			clipOpen = false;
			current = RID();
		}
		switch (c.kind)
		{
			case GadgetDrawCommand::Kind::Image:
			{
				const ::Image *img = sm.skins.images.findImageByName(c.image);
				if (!img)
				{
					break; // reported through unresolvedImages
				}
				Ref<ImageTexture> texture = m->imageTexture(sm, img, c.image);
				if (texture.is_null())
				{
					break; // reported through gadgetErrors
				}
				const Vector2 a = toWindow((float)c.x0, (float)c.y0), b = toWindow((float)c.x1, (float)c.y1);
				addTextureUV(rs, item(), Rect2(a, b - a), texture, img->uvLo, img->uvHi, colour(c.color));
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
				Ref<Font> font = m->fontFor(c.font.name, (float)c.font.pointSize, &drawSize, c.font.bold);
				if (font.is_null() || c.text.empty())
				{
					break;
				}
				const int px = std::max(1, (int)std::lround(drawSize * k));
				font = m->cellFont(font, px); // lane UI-6: the gadgets measured the string with the same cell (ShellFontMetrics)
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
				const Vector2 size = c.wrapWidth > 0 ? font->get_multiline_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, width, px) : font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, px);
				m->noteDrawnText("gadget", text, colour(c.color), Rect2(at, Vector2(size.x, font->get_height(px))));
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
	// the input's own state (lane SPELL-3: a store click that never reaches its button): the current button, the pressed flag, the Apt's pointer
	const AptButtonInst *cur = m->A()->input().currentButton();
	r["current_button"] = cur ? toGodot(cur->targetPath()) : String();
	r["mouse_down"] = m->A()->input().mouseDown();
	float mx = 0.0f, my = 0.0f;
	m->A()->mousePosition(mx, my);
	r["apt_mouse_x"] = mx;
	r["apt_mouse_y"] = my;
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
		d["state"] = (int)b->state(); // lane OPTS-6: 1 up, 2 over, 4 down (the hit-area checks read a press)
		{
			bool seenUp = true; // lane OPTS-6 review: shown up its chain and not faded under one half (MenuNavigator's rule): what a player can see
			for (const AptCharacterInst *c = b; c; c = c->parent())
			{
				seenUp = seenUp && c->visible && c->color.mul[3] >= 0.5f;
			}
			d["seen"] = seenUp;
		}
		float hb[4];
		if (b->hitBounds(hb))
		{
			d["hit_bounds"] = Rect2(hb[0], hb[1], hb[2] - hb[0], hb[3] - hb[1]); // stage
		}
		d["bounds"] = [&]() {
			float ax = 1e30f, ay = 1e30f, bx = -1e30f, by = -1e30f;
			const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
			for (int i = 0; i < 4; ++i)
			{
				float px, py;
				b->globalMatrix().apply(xs[i], ys[i], px, py);
				ax = std::min(ax, px), ay = std::min(ay, py), bx = std::max(bx, px), by = std::max(by, py);
			}
			return Rect2(ax, ay, bx - ax, by - ay); // stage
		}();
		out.push_back(d);
	}
	return out;
}

// lane OPTS-6 (checks): the gadget window a press at a stage point goes to (GameWindowManager::winPickPointerWindow): { found, name, rect (stage) }
Dictionary AptMenuPlayer::gadget_pick(const Vector2 &stage)
{
	Dictionary r;
	r["found"] = false;
	if (!m_booted || !m->shell || !m->shell->layer)
	{
		return r;
	}
	GameWindow *w = m->shell->layer->gadgets().winPickPointerWindow((int)stage.x, (int)stage.y);
	// the windows whose box holds the point, the pick's own filter aside (a check's diagnosis: the status bits and the parent say why one was passed over)
	Array under;
	for (GameWindow *u : m->shell->layer->gadgets().allWindows())
	{
		int ux = 0, uy = 0, uw = 0, uh = 0;
		u->winGetScreenPosition(&ux, &uy);
		u->winGetSize(&uw, &uh);
		if (stage.x >= ux && stage.x <= ux + uw && stage.y >= uy && stage.y <= uy + uh)
		{
			Dictionary d;
			d["name"] = toGodot(u->aptInstanceName().empty() ? u->name() : u->aptInstanceName());
			d["status"] = (int)u->winGetStatus();
			d["parent"] = u->winGetParent() ? toGodot(u->winGetParent()->name()) : String();
			d["rect"] = Rect2((float)ux, (float)uy, (float)uw, (float)uh);
			IRegion2D reg{};
			u->winGetRegion(&reg);
			d["region"] = Rect2((float)reg.lo.x, (float)reg.lo.y, (float)(reg.hi.x - reg.lo.x), (float)(reg.hi.y - reg.lo.y));
			under.push_back(d);
		}
	}
	r["under"] = under;
	if (!w)
	{
		return r;
	}
	int x = 0, y = 0, cw = 0, ch = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&cw, &ch);
	r["found"] = true;
	r["name"] = toGodot(w->aptInstanceName().empty() ? w->name() : w->aptInstanceName());
	r["rect"] = Rect2((float)x, (float)y, (float)cw, (float)ch);
	return r;
}

// lane OPTS-6 (checks): the button the Apt input takes at a stage point (its target path; "" none)
String AptMenuPlayer::hit_test_stage(const Vector2 &stage)
{
	AptButtonInst *b = m_booted ? m->A()->input().hitTestButtons(stage.x, stage.y) : nullptr;
	return b ? toGodot(b->targetPath()) : String();
}

Array AptMenuPlayer::list_gadgets()
{
	// lane MENUWORK-1: the gadget windows of the gadget layer (GameWindowManager::allWindows: creation order), for the menu inventory
	Array out;
	if (!m->shell || !m->shell->layer || !m->shell->wm)
	{
		return out;
	}
	// a gadget made for a movie's placeholder clip is named by the clip (the name InitGadgets got, e.g. OnlineLogin::Nickname, else its path)
	std::map<const GameWindow *, const WindowManager::ComponentRecord *> records;
	for (const WindowManager::ComponentRecord &rec : m->shell->wm->components())
	{
		if (rec.window)
		{
			records[rec.window.get()] = &rec;
		}
	}
	for (GameWindow *w : m->shell->layer->gadgets().allWindows())
	{
		const std::uint32_t style = w->winGetStyle();
		const char *kind = BitTest(style, GWS_PUSH_BUTTON)   ? "button"
			: BitTest(style, GWS_RADIO_BUTTON)               ? "radio"
			: BitTest(style, GWS_CHECK_BOX)                  ? "checkbox"
			: BitTest(style, GWS_ALL_SLIDER)                 ? "slider"
			: BitTest(style, GWS_COMBO_BOX)                  ? "combo"
			: BitTest(style, GWS_SCROLL_LISTBOX)             ? "list"
			: BitTest(style, GWS_ENTRY_FIELD)                ? "entry"
			: BitTest(style, GWS_STATIC_TEXT)                ? "text"
			: BitTest(style, GWS_PROGRESS_BAR)               ? "progress"
			: BitTest(style, GWS_TAB_CONTROL)                ? "tabs"
															 : "window";
		// a gadget is shown when it and every parent are shown (a combo's list box is a child of the combo)
		bool hidden = false;
		for (GameWindow *p = w; p; p = p->winGetParent())
		{
			hidden = hidden || p->winIsHidden();
		}
		int x = 0, y = 0, ww = 0, hh = 0;
		w->winGetScreenPosition(&x, &y);
		w->winGetSize(&ww, &hh);
		Dictionary d;
		auto rec = records.find(w);
		d["name"] = toGodot(rec == records.end() ? w->name() : (rec->second->instanceName.empty() ? rec->second->instancePath : rec->second->instanceName));
		d["path"] = rec == records.end() ? String() : toGodot(rec->second->instancePath);
		d["level"] = rec == records.end() ? -1 : rec->second->level;
		d["kind"] = kind;
		d["parent"] = w->winGetParent() ? toGodot(w->winGetParent()->name()) : String();
		d["x"] = x;
		d["y"] = y;
		d["w"] = ww;
		d["h"] = hh;
		d["enabled"] = w->winGetEnabled();
		d["hidden"] = hidden;
		if (BitTest(style, GWS_CHECK_BOX))
		{
			d["checked"] = GadgetCheckBoxIsChecked(w);
		}
		if (BitTest(style, GWS_ALL_SLIDER))
		{
			d["value"] = GadgetSliderGetPosition(w);
		}
		if (BitTest(style, GWS_SCROLL_LISTBOX))
		{
			d["entries"] = GadgetListBoxGetNumEntries(w);
			// the selected row (a multi-select list: its first selection; -1 none) and the first row's text (an order change shows there)
			const ListboxData *list = static_cast<const ListboxData *>(w->winGetUserData());
			int selected = -1;
			if (list)
			{
				selected = list->multiSelect ? (list->selections.empty() ? -1 : list->selections.front()) : list->selectPos;
			}
			d["selected"] = selected;
			d["row_height"] = list && !list->listData.empty() ? list->listData.front().height : 0;
			d["display_pos"] = list ? (int)list->displayPos : 0;
			d["double_click_time"] = list ? (int64_t)list->doubleClickTime : 0;
			d["time_ms"] = (int64_t)m->shell->layer->gadgets().timeMs();
			std::string first;
			for (int c = 0; c < 6 && GadgetListBoxGetNumEntries(w) > 0; ++c)
			{
				first += u16ToUtf8(GadgetListBoxGetText(w, 0, c)) + "|";
			}
			d["first"] = toGodot(first);
			// every row's texts, hashed: a re-sort shows here even when the first row stays
			std::string all;
			for (int r = 0; r < GadgetListBoxGetNumEntries(w); ++r)
			{
				for (int c = 0; c < 6; ++c)
				{
					all += u16ToUtf8(GadgetListBoxGetText(w, r, c)) + "|";
				}
				all += "\n";
			}
			d["rows"] = (int64_t)std::hash<std::string>{}(all);
		}
		if (BitTest(style, GWS_COMBO_BOX))
		{
			int selected = -1;
			GadgetComboBoxGetSelectedPos(w, &selected);
			d["selected"] = selected;
			d["entries"] = GadgetComboBoxGetLength(w);
			GameWindow *dropDown = GadgetComboBoxGetListBox(w);
			d["open"] = dropDown && !dropDown->winIsHidden(); // the drop-down list is shown
			d["text"] = toGodot(u16ToUtf8(GadgetComboBoxGetText(w)));
		}
		else if (BitTest(style, GWS_ENTRY_FIELD))
		{
			d["text"] = toGodot(u16ToUtf8(GadgetTextEntryGetText(w)));
		}
		out.push_back(d);
	}
	return out;
}

PackedStringArray AptMenuPlayer::take_commands()
{
	PackedStringArray out;
	if (m->shell && m->shell->wm)
	{
		for (const std::string &c : m->shell->wm->takeInvokedCommands())
		{
			out.push_back(toGodot(c));
		}
	}
	return out;
}

Dictionary AptMenuPlayer::screen_state() const
{
	// lane MENUWORK-1: what a press on the top screen changed in the engine (not its picture): the LAN list's sort keys and refills, the online
	// screens' page and state, the skirmish lobby's profile page
	Dictionary out;
	AptScreen *top = m->shell && m->shell->shell ? m->shell->shell->top() : nullptr;
	if (!top)
	{
		return out;
	}
	out["screen"] = toGodot(top->filename());
	if (auto *lan = dynamic_cast<AptLanLobby *>(top))
	{
		out["sort_primary"] = lan->sorter().primary();
		out["sort_secondary"] = lan->sorter().secondary();
		out["sort_status_cycle"] = lan->sorter().statusCycle();
		out["list_fills"] = lan->listFills();
		out["lan_state"] = lan->state();
	}
	else if (auto *online = dynamic_cast<AptOnlineShell *>(top))
	{
		out["online_page"] = toGodot(online->currentPage());
		out["online_state"] = online->openPlayState();
		out["online_chat_lines"] = (int64_t)online->chatLines().size();
		out["online_boxes"] = (int64_t)online->boxesShown().size();
		out["online_sort"] = online->sortColumn() * 2 + (online->sortDescending() ? 1 : 0);
	}
	if (auto *skirmish = dynamic_cast<AptSkirmish *>(top))
	{
		out["profile_state"] = skirmish->profileState();
		out["start_available"] = skirmish->startAvailable(); // Popup states cannot start.
	}
	if (auto *cah = dynamic_cast<AptCreateAHero *>(top))
	{
		out["cah_suppress_promo"] = cah->suppressPromo();
		out["cah_sort"] = cah->heroSortKey0() * 10 + cah->heroSortKey1();
	}
	return out;
}

String AptMenuPlayer::apt_text(const String &name) const
{
	const std::string *text = m->shell && m->shell->wm ? m->shell->wm->aptText(toNative(name)) : nullptr;
	return text ? toGodot(*text) : String();
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
	const AptMatrix stageTransform = inst->globalMatrix();
	r["stage_transform"] = Transform2D(Vector2(stageTransform.a, stageTransform.b), Vector2(stageTransform.c, stageTransform.d), Vector2(stageTransform.tx, stageTransform.ty));
	float x, y;
	stageTransform.apply(0, 0, x, y);
	r["x"] = x;
	r["y"] = y;
	r["visible"] = inst->visible;
	// lane END-1: the content bounds in stage space (the parent's global matrix over the corners of contentBounds)
	float bx0, by0, bx1, by1;
	if (inst->contentBounds(bx0, by0, bx1, by1))
	{
		r["local_bounds"] = Rect2(bx0, by0, bx1 - bx0, by1 - by0);
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

// lane PLAY-1: the window of a component placeholder ("_level1.<path>": a slider, a text entry) in stage space, for scripted input
Rect2 AptMenuPlayer::component_rect(const String &instance_path) const
{
	if (!m->shell)
	{
		return Rect2();
	}
	GameWindow *w = m->shell->wm->componentWindow(toNative(instance_path));
	if (!w)
	{
		return Rect2();
	}
	int x = 0, y = 0, cw = 0, ch = 0;
	w->winGetScreenPosition(&x, &y);
	w->winGetSize(&cw, &ch);
	return Rect2((real_t)x, (real_t)y, (real_t)cw, (real_t)ch);
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
		menu_pointer_moved();
		post_mouse_move_stage(window_to_stage(mm->get_position()));
	}
	else if (const InputEventMouseButton *mb = Object::cast_to<InputEventMouseButton>(event.ptr()))
	{
		menu_pointer_moved();
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
		if (menuKey(key))
		{
			get_viewport()->set_input_as_handled();
		}
	}
}

// lane OPTS-6: the navigation keys (MenuNavigator.h; not retail): the arrows, Tab / Shift+Tab, Enter / Space, Esc, PageUp / PageDown
static bool navKeyOf(const InputEventKey *key, MenuNavKey &out)
{
	switch (key->get_keycode())
	{
		case Key::KEY_UP: out = MenuNavKey::Up; return true;
		case Key::KEY_DOWN: out = MenuNavKey::Down; return true;
		case Key::KEY_LEFT: out = MenuNavKey::Left; return true;
		case Key::KEY_RIGHT: out = MenuNavKey::Right; return true;
		case Key::KEY_TAB: out = key->is_shift_pressed() ? MenuNavKey::Previous : MenuNavKey::Next; return true;
		case Key::KEY_BACKTAB: out = MenuNavKey::Previous; return true;
		case Key::KEY_ENTER:
		case Key::KEY_KP_ENTER:
		case Key::KEY_SPACE: out = MenuNavKey::Activate; return true;
		case Key::KEY_ESCAPE: out = MenuNavKey::Back; return true;
		case Key::KEY_PAGEDOWN: out = MenuNavKey::PageNext; return true;
		case Key::KEY_PAGEUP: out = MenuNavKey::PagePrevious; return true;
		default: return false;
	}
}

void AptMenuPlayer::syncNav() const
{
	if (!m->shell || !m->shell->nav)
	{
		return;
	}
	const AptScreen *top = m->shell->shell ? m->shell->shell->inputTop() : nullptr;
	// MainMenu uses the pre-OPTS-6 dropdown group navigation in menuKey.
	// Keep the spatial navigator off that movie and its animated submenus.
	m->shell->nav->setMenu(top && top->filename() != "MainMenu.apt" && top->isMenu() && !top->isHidden() && top->level() >= 0 ? top->level() : -1, top);
	m->shell->nav->setControllerResults(top && top->filename() == "TimeLine.apt" &&
		!Input::get_singleton()->get_connected_joypads().is_empty());
}

bool AptMenuPlayer::modal_menu() const
{
	// lane SAVE-UI1 / OPTS-6: a menu is the input's top (the shell's top screen, or the quit menu over a game): the game's input is cancelled and not
	// served until it is gone. Not the navigation's own test (menu_has_input: a loaded movie): a screen on its way in (its movie still loading) is modal too
	if (!m_booted || !m->shell || !m->shell->shell)
	{
		return false;
	}
	const AptScreen *top = m->shell->shell->inputTop();
	return top && top->isMenu();
}

bool AptMenuPlayer::menu_escape(bool pressed)
{
	// lane SAVE-UI1: the input top screen's own Escape (AptScreen::inputKey: SaveLoad.apt's RW 0x816627 closes on the release in its state 0); true when the
	// screen used the key (the menus' navigation and the game's quit-menu toggle never see it)
	if (!m_booted || !m->shell || !m->shell->shell)
	{
		return false;
	}
	AptScreen *top = m->shell->shell->inputTop();
	if (!top || !top->inputKey(::KEY_ESC, pressed ? KEY_STATE_DOWN : KEY_STATE_UP))
	{
		return false;
	}
	m->dirty = true;
	return true;
}

bool AptMenuPlayer::menu_has_input() const
{
	if (!m_booted || !m->shell || !m->shell->nav)
	{
		return false;
	}
	syncNav();
	const AptScreen *top = m->shell->shell ? m->shell->shell->inputTop() : nullptr;
	return top && top->isMenu() && !top->isHidden() && top->level() >= 0;
}

void AptMenuPlayer::menu_pointer_moved()
{
	m->mainMenuGroup = m->mainMenuItem = -1;
	if (m_booted && m->shell && m->shell->nav)
	{
		m->shell->nav->setControllerSections(false);
		m->shell->nav->pointerMoved();
	}
}

bool AptMenuPlayer::menu_controller_section(int delta)
{
	if (!m_booted || !m->shell || !m->shell->shell) return false;
	const auto *top = m->shell->shell->inputTop();
	if (!top || !dynamic_cast<const AptSkirmish *>(top)) return false;
	syncNav();
	if (!m->shell->nav->supportsControllerSections()) return false;
	m->shell->nav->setControllerSections(true);
	m->shell->nav->switchControllerSection(delta);
	m->dirty = true;
	return true;
}

bool AptMenuPlayer::menu_input(const Ref<InputEvent> &event)
{
	const InputEventKey *key = Object::cast_to<InputEventKey>(event.ptr());
	return m_booted && key && menuKey(key);
}

Dictionary AptMenuPlayer::menu_nav_state()
{
	Dictionary r;
	r["main_groups"] = m->mainMenuGroups;
	r["main_entries"] = m->mainMenuEntries;
	r["main_focus"] = m->mainMenuFocus;
	r["level"] = -1;
	if (!m_booted || !m->shell || !m->shell->nav)
	{
		return r;
	}
	syncNav();
	MenuNavigator &nav = *m->shell->nav;
	r["level"] = nav.level();
	r["focus"] = toGodot(nav.focusId());
	r["controller_section"] = nav.controllerSection();
	r["frame"] = nav.frameShown();
	float box[4];
	if (nav.focusBox(box))
	{
		r["focus_rect"] = Rect2(box[0], box[1], box[2] - box[0], box[3] - box[1]);
	}
	Array targets;
	for (const MenuNavTarget &t : nav.targets())
	{
		Dictionary d;
		d["id"] = toGodot(t.id);
		d["kind"] = t.kind == MenuNavTarget::Kind::Button ? "button" : t.kind == MenuNavTarget::Kind::Gadget ? "gadget" : "item";
		d["rect"] = Rect2(t.x0, t.y0, t.x1 - t.x0, t.y1 - t.y0);
		targets.push_back(d);
	}
	r["targets"] = targets;
	PackedStringArray catcherIds; // lane OPTS-6 review: the click-away areas (Esc / Tab past the last move the pointer onto one first)
	for (const MenuNavTarget &c : nav.catchers())
	{
		catcherIds.push_back(toGodot(c.id));
	}
	r["catchers"] = catcherIds;
	AptButtonInst *current = m->shell->wm->apt().input().currentButton();
	r["current_button"] = current ? toGodot(current->targetPath()) : String(); // the button the pointer's last event made current
	if (m->shell->layer)
	{
		GameWindowManager &gwm = m->shell->layer->gadgets();
		if (GameWindow *f = gwm.winGetFocus())
		{
			int x = 0, y = 0, w = 0, h = 0;
			f->winGetScreenPosition(&x, &y);
			f->winGetSize(&w, &h);
			r["key_focus_rect"] = Rect2((float)x, (float)y, (float)w, (float)h);
		}
		r["open_list"] = m->shell->layer->openDropDown() != nullptr;
	}
	PackedStringArray log;
	for (const std::string &l : nav.log())
	{
		log.push_back(toGodot(l));
	}
	r["log"] = log;
	return r;
}

bool AptMenuPlayer::menuKey(const InputEventKey *key)
{
	// lane CHAT-1: the chat window's input procedure (RW 0x8ECB8F) sees the key before the entry: Esc, Ctrl+Enter (the rest goes on to the entry below)
	if (m->shell && m->shell->chat && ingame_chat_has_keys() && !key->is_echo())
	{
		const int dik = dikOf(key->get_keycode());
		int state = key->is_pressed() ? KEY_STATE_DOWN : KEY_STATE_UP;
		if (key->is_ctrl_pressed())
		{
			state |= KEY_STATE_LCONTROL;
		}
		if (dik && m->shell->chat->inputKey(dik, state))
		{
			m->dirty = true;
			return true;
		}
	}
	// The mounted movie supplies the groups, entries and button actions. This
	// works for each game's menu and for mods without a content-name table.
	const Key menuKey = key->get_keycode();
	const bool arrow = menuKey == Key::KEY_LEFT || menuKey == Key::KEY_RIGHT || menuKey == Key::KEY_UP || menuKey == Key::KEY_DOWN;
	const bool main = m->shell && m->shell->shell && m->shell->shell->top() &&
		m->shell->shell->top()->filename() == "MainMenu.apt" && !m->shell->wm->componentInputBlocked();
	if (!main || menuKey == Key::KEY_TAB) m->mainMenuGroup = m->mainMenuItem = -1;
	if (main && (arrow || (m->mainMenuGroup >= 0 && (menuKey == Key::KEY_ENTER || menuKey == Key::KEY_KP_ENTER || menuKey == Key::KEY_SPACE))))
	{
		const int level = m->shell->shell->top()->level();
		AptSpriteInst *root = m->A()->level(level);
		if (!root) return false; // A newly pushed screen loads its movie on the next update.
		std::vector<AptSpriteInst *> groups;
		for (AptCharacterInst *child : root->children())
		{
			AptSpriteInst *clip = child->asSprite();
			if (clip && clip->globallyVisible() &&
				((clip->labelFrame("_show") >= 0 && clip->labelFrame("_hide") >= 0) ||
				 (clip->labelFrame("_over") >= 0 && firstButton(clip)))) groups.push_back(clip);
		}
		// Horizontal group order follows the authored heading positions. Entries
		// follow their authored vertical positions after the group opens.
		auto x = [](AptCharacterInst *clip) { float x, y; clip->globalMatrix().apply(0, 0, x, y); return x; };
		std::stable_sort(groups.begin(), groups.end(), [&](auto *a, auto *b) { return x(a) < x(b); });
		m->mainMenuGroups.clear();
		for (auto *group : groups) m->mainMenuGroups.push_back(toGodot(group->pathSegment()));
		if (!groups.empty())
		{
			auto animate = [](AptSpriteInst *clip, const char *label) {
				const int frame = clip->labelFrame(label);
				if (frame >= 0) { clip->gotoFrame(frame); clip->playing = true; }
			};
			if (m->mainMenuGroup >= (int)groups.size()) m->mainMenuGroup = m->mainMenuItem = -1;
			if (key->is_pressed())
			{
				const int previous = m->mainMenuGroup;
				if (menuKey == Key::KEY_LEFT || menuKey == Key::KEY_RIGHT || m->mainMenuGroup < 0)
				{
					const int delta = menuKey == Key::KEY_LEFT ? -1 : 1;
					if (m->mainMenuGroup < 0) m->mainMenuGroup = delta > 0 ? -1 : 0;
					m->mainMenuGroup = (m->mainMenuGroup + delta + (int)groups.size()) % (int)groups.size();
					m->mainMenuItem = -1;
				}
				m->mainMenuFocus = toGodot(groups[m->mainMenuGroup]->pathSegment());
				if (previous != m->mainMenuGroup)
				{
					for (int i = 0; i < (int)groups.size(); ++i)
					{
						const bool chosen = i == m->mainMenuGroup;
						const bool expands = groups[i]->labelFrame("_show") >= 0;
						animate(groups[i], expands ? (chosen ? "_show" : "_hide") : (chosen ? "_over" : "_up"));
					}
				}
				else
				{
					AptSpriteInst *group = groups[m->mainMenuGroup];
					std::vector<AptSpriteInst *> entries;
					for (AptCharacterInst *child : group->children())
					{
						AptSpriteInst *clip = child->asSprite();
						// OpenButton is the heading, not a selectable submenu item.
						if (clip && clip->pathSegment() != "OpenButton" && clip->globallyVisible() &&
							clip->labelFrame("_over") >= 0 && firstButton(clip) && firstButton(clip)->enabled) entries.push_back(clip);
					}
					std::stable_sort(entries.begin(), entries.end(), [](auto *a, auto *b) {
						float ax, ay, bx, by; a->globalMatrix().apply(0, 0, ax, ay); b->globalMatrix().apply(0, 0, bx, by); return ay > by;
					});
					if (m->mainMenuItem >= (int)entries.size()) m->mainMenuItem = -1;
					if (menuKey == Key::KEY_UP || menuKey == Key::KEY_DOWN)
					{
						if (!entries.empty()) m->mainMenuItem = m->mainMenuItem < 0 ? 0 :
							(m->mainMenuItem + (menuKey == Key::KEY_UP ? 1 : -1) + (int)entries.size()) % (int)entries.size();
					}
					else if (!arrow)
					{
						if (m->mainMenuItem < 0 && !entries.empty()) m->mainMenuItem = 0;
						else
						{
							AptSpriteInst *entry = entries.empty() ? group : entries[m->mainMenuItem];
							const Dictionary hit = find_button(level, toGodot(entry->targetPath()));
							if ((bool)hit.get("found", false) && (bool)hit.get("enabled", false) && hit.get("hit", String()) == hit.get("path", String()))
							{
								m->shell->wm->postMouseMove((double)hit["x"], (double)hit["y"]);
								m->shell->wm->postMouseButton(true);
								m->shell->wm->postMouseButton(false);
							}
						}
					}
					m->mainMenuEntries.clear();
					for (int i = 0; i < (int)entries.size(); ++i)
					{
						m->mainMenuEntries.push_back(toGodot(entries[i]->pathSegment()));
						animate(entries[i], i == m->mainMenuItem ? "_over" : "_up");
					}
					if (m->mainMenuItem >= 0) m->mainMenuFocus = toGodot(entries[m->mainMenuItem]->pathSegment());
				}
				m->dirty = true;
			}
			return true;
		}
	}

	if (m->shell && m->shell->nav && m->shell->shell)
	{
		syncNav();
		const auto *top = m->shell->shell->inputTop();
		m->shell->nav->setControllerSections(top && dynamic_cast<const AptSkirmish *>(top) && m->shell->nav->supportsControllerSections() && (bool)key->get_meta("menu_controller", false));
	}
	// lane SAVE-UI1: the top screen's own Escape procedure first (its press and its release are its alone)
	if (key->get_keycode() == Key::KEY_ESCAPE && !key->is_echo() && menu_escape(key->is_pressed()))
	{
		return true;
	}
	// lane OPTS-6: the navigation first (a key it takes reaches neither the movie nor the gadgets; its release neither)
	if (m->shell && m->shell->nav)
	{
		MenuNavKey nk;
		const int code = (int)key->get_keycode();
		if (!key->is_pressed())
		{
			if (m->shell->navSwallowed.erase(code))
			{
				return true;
			}
		}
		else if (navKeyOf(key, nk) && !(key->is_echo() && (nk == MenuNavKey::Activate || nk == MenuNavKey::Back)))
		{
			syncNav();
			// Esc on the OpenBFME page stays the page's CANCEL below (OPTS-3's path); the page's Back does the same
			const bool listOpen = m->shell->layer && m->shell->layer->openDropDown();
			const bool pageEsc = nk == MenuNavKey::Back && !listOpen && m->shell->shell && optionsTop(m->shell->shell.get()) && optionsTop(m->shell->shell.get())->extensionOpen();
			if (!pageEsc && m->shell->nav->key(nk))
			{
				m->shell->navSwallowed.insert(code);
				return true;
			}
		}
	}
	if (key->is_echo())
	{
		return false;
	}
	{
		// lane OPTS-3: Esc on the OpenBFME page is its CANCEL; the movie never sees the key (its own Esc would play Options' closing animation). Lane
		// OPTS-6: an open drop-down list takes Esc first (the navigation closes it; the page stays)
		if (key->get_keycode() == Key::KEY_ESCAPE && m->shell && m->shell->shell)
		{
			if (AptOptionsScreen *o = optionsTop(m->shell->shell.get()); o && o->extensionOpen())
			{
				if (key->is_pressed())
				{
					o->extensionCancel();
				}
				return true;
			}
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
	return false; // the movie's and the gadgets' (the event goes on as before)
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
	size_t items = m->items.size();
	// lane HUDREDRAW-1: per level (its segments' first level): the movie, the rebuilds that drew some of its segments again / kept all, the segments drawn
	// again (summed) and its segments in the last rebuild
	Dictionary levels;
	for (const Impl::Segment &g : m->segments)
	{
		items += 1 + g.items.size();
	}
	for (const auto &kv : m->levelCounts)
	{
		Dictionary l;
		l["movie"] = toGodot(kv.second.movie);
		l["redrawn"] = (int64_t)kv.second.redrawnFrames;
		l["kept"] = (int64_t)kv.second.keptFrames;
		l["segments_redrawn"] = (int64_t)kv.second.segmentsRedrawn;
		l["segments"] = (int64_t)kv.second.segments;
		levels[kv.first] = l;
	}
	s["canvas_items"] = (int64_t)items;
	s["segments"] = (int64_t)m->segments.size();
	s["partial_frames"] = (int64_t)m->partialFrames;
	s["gadget_commands"] = (int64_t)m->gadgets.commands.size(); // lane HUDREDRAW-1: gadgets on screen make a changed level draw everything again
	s["levels"] = levels;
	// lane HUDREDRAW-1: the note lists' lengths (they must not grow with the frames: Apt::noteOnce / WindowManager::noteOnce)
	s["apt_notes"] = m->A() ? (int64_t)m->A()->notes().size() : (int64_t)0;
	WindowManager *notesWm = window_manager();
	s["wm_notes"] = notesWm ? (int64_t)notesWm->notes().size() : (int64_t)0;
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
	freeItemPool();
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
			// lane UI-4: the field's rectangle in window pixels (the instance matrix, then the stage mapping), for checks that sample the drawn label
			float wx0 = 1e9f, wy0 = 1e9f, wx1 = -1e9f, wy1 = -1e9f;
			const float xs[4] = { op.bounds[0], op.bounds[2], op.bounds[2], op.bounds[0] };
			const float ys[4] = { op.bounds[1], op.bounds[1], op.bounds[3], op.bounds[3] };
			for (int i = 0; i < 4; ++i)
			{
				const float sx = (op.matrix.a * xs[i] + op.matrix.c * ys[i] + op.matrix.tx) * op.scaleX + op.offsetX;
				const float sy = (op.matrix.b * xs[i] + op.matrix.d * ys[i] + op.matrix.ty) * op.scaleY + op.offsetY;
				wx0 = std::min(wx0, sx);
				wx1 = std::max(wx1, sx);
				wy0 = std::min(wy0, sy);
				wy1 = std::max(wy1, sy);
			}
			char win[96];
			snprintf(win, sizeof win, " win=(%.0f %.0f %.0f %.0f)", wx0, wy0, wx1, wy1);
			line += win;
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

std::shared_ptr<GodotMouse> AptMenuPlayer::godot_mouse() const
{
	return m->godotMouse;
}

Dictionary AptMenuPlayer::get_cursor_image() const
{
	return m->godotMouse ? m->godotMouse->getCursorImage() : Dictionary();
}

Ref<Font> AptMenuPlayer::font_for(const std::string &name, float size, float *drawSize, bool bold)
{
	return m->fontFor(name, size, drawSize, bold);
}

Ref<Font> AptMenuPlayer::font_cell(const Ref<Font> &font, int px)
{
	return m->cellFont(font, px);
}

void AptMenuPlayer::set_native_hook(AptNativeHook *hook)
{
	if (m->nativeHook && m->nativeHook != hook)
	{
		// lane QA-1: the items the old hook drew into may carry its resources (the HUD's globe material): they go now, before their owner frees them, and the
		// next frame draws the canvas again (else leaving a game freed the material under live items: "Parameter material is null")
		clearCanvas();
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
		d["frames_decoded"] = kv.second.stream.is_valid() ? kv.second.stream->get_stats().get("frames_decoded", (int64_t)0) : Variant((int64_t)0);
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
	r["view3d_presentation_style"] = m->view3d.sourceStyle();
	r["movie_errors"] = toArray(m->movieErrors); // lane CAMP-2: BinkMovie components that could not play
	Array cursorErrors;
	if (m->godotMouse)
	{
		for (const std::string &e : m->godotMouse->cursorErrors())
		{
			cursorErrors.push_back(toGodot(e));
		}
	}
	r["cursor_errors"] = cursorErrors;
	r["presentation_style"] = m->presentationFs.is_valid() && m->presentationFs->is_shell_source() ? "bfme2-106" : "rotwk-201";
	r["presentation_root"] = m->presentationFs.is_valid() ? m->presentationFs->install_root() : String();
	PackedStringArray presentationArchives;
	if (m->presentationFs.is_valid())
	{
		for (const MountedArchive &a : m->presentationFs->mountedArchives())
		{
			presentationArchives.push_back(toGodot(a.canonicalPath));
		}
	}
	r["presentation_archives"] = presentationArchives;
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

namespace
{
// lane CHAT-1: UTF-16 to a Godot String: a surrogate pair is one character, a lone surrogate U+FFFD
String chatU16ToString(const std::u16string &u)
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
		s += String::chr(c);
	}
	return s;
}
} // namespace

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
	const Variant capacityValue = state.get("map_start_count", LEGACY_MAX_SLOTS);
	const Variant rowsValue = state.get("rows", Array());
	if (capacityValue.get_type() != Variant::INT || (int64_t)capacityValue < LEGACY_MAX_SLOTS || (int64_t)capacityValue > MAX_SLOTS ||
		rowsValue.get_type() != Variant::ARRAY)
	{
		out["error"] = "disconnect requires integer map_start_count in 8 .. 16 and an array of rows";
		return out;
	}
	const int capacity = (int)capacityValue;
	const Array src = rowsValue;
	if (src.size() > capacity - 1)
	{
		out["error"] = "disconnect rows exceed map capacity";
		return out;
	}
	std::vector<AptDisconnectScreen::Row> rows((size_t)src.size());
	for (int i = 0; i < src.size(); ++i)
	{
		if (src[i].get_type() != Variant::DICTIONARY)
		{
			out["error"] = "disconnect row must be a dictionary";
			return out;
		}
		const Dictionary d = src[i];
		const Variant used = d.get("used", false), name = d.get("name", ""), votes = d.get("votes", 0), bar = d.get("bar", 100), kick = d.get("kick", false);
		if (used.get_type() != Variant::BOOL || name.get_type() != Variant::STRING || votes.get_type() != Variant::INT ||
			bar.get_type() != Variant::INT || kick.get_type() != Variant::BOOL || (int64_t)votes < 0 || (int64_t)votes > capacity ||
			(int64_t)bar < 0 || (int64_t)bar > 100)
		{
			out["error"] = "invalid disconnect row fields";
			return out;
		}
		auto &r = rows[(size_t)i];
		r.used = (bool)used;
		r.nameUtf8 = toNative(String(name));
		r.votes = (int)votes;
		r.barPercent = (int)bar;
		r.kickShown = (bool)kick;
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
		// lane CHAT-1: the chat line typed into the screen (Chat::OnBttnEnterText, RW 0x9196B9) for the game's network, and the rules (APT:DisconnectRules)
		sm.disconnect->onChat = [smp](const std::u16string &text) {
			Dictionary a;
			a["kind"] = "chat";
			a["text"] = chatU16ToString(text);
			smp->disconnectActions.push_back(a);
		};
		std::u16string rules;
		if (sm.environment.gameText && sm.environment.gameText->fetch("APT:DisconnectRules", rules))
		{
			sm.disconnect->setRulesText(rules);
		}
	}
	const bool loaded = sm.disconnect->apply(rows, capacity);
	m->dirty = true;
	out["ok"] = sm.disconnect->level() >= 0 && sm.disconnect->error().empty();
	out["level"] = sm.disconnect->level();
	out["loaded"] = loaded;
	if (!sm.disconnect->error().empty()) out["error"] = toGodot(sm.disconnect->error());
	if (capacity > LEGACY_MAX_SLOTS)
	{
		out["first_row"] = sm.disconnect->firstRow();
		out["row_count"] = capacity - 1;
	}
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
	const unsigned generation = sl.generation + 1; // lane SAVE-2: an open screen fills its lists again
	sl = SaveLoadInfo();
	sl.generation = generation;
	sl.mode = (int)info.get("mode", 2);
	sl.flags = (unsigned)(int)info.get("flags", 4);
	sl.inGame = (bool)info.get("in_game", false);
	const Array saves = info.get("saves", Array());
	for (int i = 0; i < saves.size(); ++i)
	{
		const Dictionary d = saves[i];
		SaveLoadInfo::Save s;
		s.path = toNative(String(d.get("path", "")));
		s.fileNameUtf8 = toNative(String(d.get("name", "")));
		s.mapPathUtf8 = toNative(String(d.get("map_path", "")));
		s.mapUtf8 = toNative(String(d.get("map", "")));
		s.dateUtf8 = toNative(String(d.get("date", "")));
		s.timeUtf8 = toNative(String(d.get("time", "")));
		s.gameKind = (int)d.get("kind", 2);
		s.loadable = (bool)d.get("loadable", false);
		s.refusalUtf8 = toNative(String(d.get("refusal", "")));
		s.retailImport = (bool)d.get("retail_import", false);
		s.autoSave = (bool)d.get("auto_save", false); // lane CAMPSAVE-1
		sl.saves.push_back(s);
	}
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

// lane CAMPSAVE-1: what RW 0x5EADDE gives CampaignMenu.apt (set before the host pushes the screen; its constructor reads it)
void AptMenuPlayer::set_campaign_menu(const Dictionary &content)
{
	if (!m->shell)
	{
		return;
	}
	CampaignMenuContext &c = m->shell->campaignMenu;
	c = CampaignMenuContext();
	c.haveMission = (bool)content.get("have_mission", false);
	c.missionName = toNative(String(content.get("mission_name", "")));
	c.loadScreenImage = toNative(String(content.get("load_screen_image", "")));
	c.victorious = (bool)content.get("victorious", false);
}

// lane CAMPSAVE-1: RW 0x927F00 .. 0x927F4C: OptionPreferences (RW 0x6E56F3) BCU = "true" written (RW 0x7B274C), TheGlobalData + 0xAF5 = 1
Dictionary AptMenuPlayer::unlock_bonus_campaign()
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "no shell";
		return r;
	}
	m->shell->options.set("BCU", "true");
	m->shell->environment.bonusCampaignUnlocked = true;
	std::string error;
	const bool saved = !m->shell->environment.optionsFile.empty() && m->shell->options.save(m->shell->environment.optionsFile, &error);
	r["ok"] = saved;
	r["file"] = toGodot(m->shell->environment.optionsFile);
	if (!saved)
	{
		r["error"] = toGodot(error.empty() ? std::string("Options.ini: no file name") : error);
	}
	return r;
}

bool AptMenuPlayer::is_bonus_campaign_unlocked() const
{
	return m->shell && m->shell->environment.bonusCampaignUnlocked;
}

void AptMenuPlayer::set_save_folder(const String &folder)
{
	if (m->shell)
	{
		m->shell->environment.saveFolder = toNative(folder);
	}
}

bool AptMenuPlayer::save_load_saved(bool ok, const String &error)
{
	AptSaveLoad *screen = m->shell && m->shell->shell ? dynamic_cast<AptSaveLoad *>(m->shell->shell->top()) : nullptr;
	if (!screen)
	{
		return false;
	}
	screen->saveFinished(ok, toNative(error));
	return true;
}

bool AptMenuPlayer::save_load_refused(const String &name, const String &error)
{
	AptSaveLoad *screen = m->shell && m->shell->shell ? dynamic_cast<AptSaveLoad *>(m->shell->shell->top()) : nullptr;
	if (!screen)
	{
		return false;
	}
	screen->loadRefused(toNative(name), toNative(error));
	return true;
}

bool AptMenuPlayer::ingame_chat_open(int64_t mode, const String &receivers)
{
	if (!m->shell || !m->shell->shell || !m->shell->wm)
	{
		return false;
	}
	ShellMode &sm = *m->shell;
	if (sm.chat || sm.disconnect)
	{
		return false; // RW 0x8EC97D: one window at a time; none over the disconnect screen (DAT_DEA328)
	}
	if (mode < 0 || mode > 4)
	{
		return false;
	}
	// RW 0x8EC97D: the window manager loads InGameChat.apt (the first free level, as the disconnect screen)
	sm.chat = std::make_unique<AptInGameChat>(*sm.wm, *sm.shell, sm.chatModel, (InGameChatMode)mode, toNative(receivers));
	if (sm.chat->level() < 0)
	{
		sm.chat.reset();
		return false;
	}
	ShellMode *smp = &sm;
	sm.chat->onSend = [smp](const std::u16string &text, InGameChatMode md) {
		Dictionary d;
		d["text"] = chatU16ToString(text);
		d["mode"] = (int64_t)md;
		smp->chatSends.push_back(d);
	};
	m->dirty = true;
	return true;
}

Dictionary AptMenuPlayer::ingame_chat_update()
{
	Dictionary out;
	out["open"] = false;
	if (!m->shell)
	{
		return out;
	}
	ShellMode &sm = *m->shell;
	if (sm.chat)
	{
		sm.chat->frameUpdate(); // RW 0x8ECD3E
		if (sm.chat->closed())
		{
			sm.chat.reset(); // state 4: the screen goes
			m->dirty = true;
		}
	}
	out["open"] = sm.chat != nullptr;
	if (sm.chat)
	{
		out["state"] = sm.chat->state();
		out["mode"] = (int64_t)sm.chat->mode();
		out["level"] = sm.chat->level();
		out["entry"] = chatU16ToString(sm.chat->entryText());
		out["has_entry"] = sm.chat->entry() != nullptr;
	}
	out["sends"] = sm.chatSends.duplicate();
	sm.chatSends.clear();
	return out;
}

void AptMenuPlayer::ingame_chat_close()
{
	if (m->shell && m->shell->chat)
	{
		m->shell->chat->close();
		m->dirty = true;
	}
}

bool AptMenuPlayer::ingame_chat_has_keys() const
{
	// the window takes the keyboard from its open until its close animation starts (RW 0x8ECB8F uses every GWM_CHAR); the disconnect screen's chat entry
	// takes it while the screen is up (lane CHAT-1)
	if (m->shell && m->shell->disconnect && m->shell->disconnect->chatEntry())
	{
		return true;
	}
	return m->shell && m->shell->chat && (m->shell->chat->state() == AptInGameChat::STATE_LOADING || m->shell->chat->state() == AptInGameChat::STATE_OPEN);
}

bool AptMenuPlayer::disconnect_screen_chat(const String &line)
{
	if (!m->shell || !m->shell->disconnect)
	{
		return false;
	}
	std::u16string u;
	for (int i = 0; i < line.length(); ++i)
	{
		const char32_t c = line[i];
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
	m->dirty = true;
	return m->shell->disconnect->showChat(u);
}

bool AptMenuPlayer::disconnect_screen_type(const String &text)
{
	if (!m->shell || !m->shell->disconnect || !m->shell->disconnect->chatEntry())
	{
		return false;
	}
	std::u16string u;
	for (int i = 0; i < text.length(); ++i)
	{
		const char32_t c = text[i];
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
	GadgetTextEntrySetText(m->shell->disconnect->chatEntry(), u);
	m->dirty = true;
	return m->shell->wm && m->shell->wm->invokeCallback("AptDisconnectScreen::Chat::OnBttnEnterText", std::string());
}

bool AptMenuPlayer::ingame_chat_type(const String &text)
{
	if (!m->shell || !m->shell->chat || !m->shell->chat->entry())
	{
		return false;
	}
	std::u16string u;
	for (int i = 0; i < text.length(); ++i)
	{
		const char32_t c = text[i];
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
	m->shell->chat->typeText(u);
	m->dirty = true;
	return true;
}

bool AptMenuPlayer::ingame_chat_command(const String &command)
{
	if (!m->shell || !m->shell->chat || !m->shell->wm)
	{
		return false;
	}
	const std::string name = "AptInGameChat::" + toNative(command);
	const bool ok = m->shell->wm->invokeCallback(name, std::string());
	m->dirty = true;
	return ok;
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
	world->set_hero_stats_profile(profileDir.utf8().get_data());
	sm.cahHeroes.load(m->fs->archive_fs(), profileDir.utf8().get_data(), &listErrors);
	for (const std::string &e : listErrors)
	{
		errors.append(toGodot(e));
	}
	sm.cahContext.system = &world->object_world()->setupCreateAHeroSystem(); // lane MAPRESET-1: kept across games (the world reloads after each)
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
	out["camera_revision"] = (int64_t)screen->cameraRevision();
	out["last_message"] = String(screen->lastMessage().c_str()); // lane CAHPIC-1: the last box's labels (the --cah-picture run waits for it)
	out["picture_frames"] = (int64_t)screen->pictureFrames();     // lane CAHPIC-1: the guard clip's draws since PrepareToTakePicture
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

void AptMenuPlayer::set_create_a_hero_portrait(const String &image)
{
	if (m->shell && m->shell->wm)
	{
		m->shell->wm->setAptImage("Cah::Portrait", toNative(image));
		m->dirty = true;
	}
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
	std::string leaf, error;
	const std::string userData = UserDataFolder::userDataFolderOfInstall(toNative(rotwkInstall), toNative(OS::get_singleton()->get_data_dir()), '/', &leaf, &error);
	if (userData.empty())
	{
		out["error"] = toGodot(error);
		return out;
	}
	out["ok"] = true;
	out["leaf"] = toGodot(leaf);
	out["user_data"] = toGodot(userData);
	out["folder"] = toGodot(UserDataFolder::heroSaveFolder(userData, '/'));
	return out;
}

// lane UBERJOIN-1: the screens' performance-mode switch is the world's (GameWorld::switch_profile_for_game / restore_simulation_profile); the world is
// looked up by its object id at each call, so a freed world is an answer of "no world", never a dangling pointer
static void bindProfileSwitch(ProfileSwitch &ps, std::uint64_t worldId)
{
	auto world = [worldId]() { return Object::cast_to<GameWorld>(ObjectDB::get_instance(ObjectID(worldId))); };
	ps.current = [world]() {
		GameWorld *w = world();
		return w ? w->current_profile() : SimulationProfile::Faithful;
	};
	ps.digestOf = [world](SimulationProfile p) {
		GameWorld *w = world();
		return w ? w->profile_digest_for(p) : std::string();
	};
	ps.request = [world](SimulationProfile p, std::string *why) {
		GameWorld *w = world();
		if (!w)
		{
			*why = "no game world";
			return false;
		}
		return w->switch_profile_for_game(p, why);
	};
	ps.restore = [world]() {
		if (GameWorld *w = world())
		{
			w->restore_simulation_profile();
		}
	};
}

bool AptMenuPlayer::set_lan(Object *worldObject)
{
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (!m->shell)
	{
		return false;
	}
	m->shell->environment.lan = world ? world->lan() : nullptr;
	if (world)
	{
		bindProfileSwitch(m->shell->profileSwitch, world->get_instance_id());
		m->shell->environment.profileSwitch = &m->shell->profileSwitch; // lane UBERJOIN-1
	}
	return m->shell->environment.lan != nullptr;
}

// ---- lane ONLINE-4: the online screens' lobby client -------------------------------------------------------------------------------------------------

Dictionary AptMenuPlayer::set_online(Object *worldObject, const Dictionary &options)
{
	Dictionary r;
	r["ok"] = false;
	GameWorld *world = Object::cast_to<GameWorld>(worldObject);
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	int factions = 0, colors = 0;
	if (!world || !world->onlineLimits(factions, colors))
	{
		r["error"] = "set_online needs the world after setup() (the faction and colour counts)";
		return r;
	}
	ShellMode &sm = *m->shell;
	if (sm.shell && sm.shell->findScreenByFilename("OnlineShell.apt"))
	{
		r["error"] = "OnlineShell.apt is up: its client stays";
		return r;
	}
	OnlineLobbyClient::Options o;
	o.installId = toNative(String(options.get("install", String())));
	o.deviceHash = toNative(String(options.get("device", String())));
	const Dictionary client = options.get("client", Dictionary());
	o.clientPlatform = toNative(String(client.get("platform", String())));
	o.clientVersion = toNative(String(client.get("version", String())));
	// lane JOINVER-1: the build day and whether this is an official release (online_identity.gd build_info: kept out of the sign-in's client)
	const Dictionary build = options.get("build", Dictionary());
	o.buildDate = toNative(String(build.get("date", String())));
	o.buildOfficial = build.has("official") ? (bool(build.get("official", false)) ? 1 : 0) : -1;
	o.profileDigest = toNative(world->net_profile_digest());
	o.profileArchives = world->net_profile().archiveLines(); // lane PROFILE-2: told to a lobby with the "archives" feature
	o.platform = localPlatform(); // lane PLATFORMICON-1: the one value told about this machine
	o.playerTemplateCount = factions;
	o.colorCount = colors;
	o.enableSocial = !o.installId.empty();
	o.pollMs = (std::uint32_t)(int64_t)options.get("poll_ms", (int64_t)o.pollMs);
	// lane ONLINE-5: the staging room's polls are held by a lobby that can (20 s: one request per player every 20 s in an idle room); "long_poll_s" 0
	// turns it off (plain polls every poll_ms)
	o.longPollS = (std::uint32_t)std::clamp<int64_t>((int64_t)options.get("long_poll_s", (int64_t)20), 0, OnlineLobbyClient::kMaxHoldSeconds);
	o.profile = world->current_profile(); // lane UBERJOIN-1: net_profile_digest is that profile's
	sm.online = std::make_unique<OnlineLobbyClient>(o);
	bindProfileSwitch(sm.profileSwitch, world->get_instance_id());
	sm.environment.profileSwitch = &sm.profileSwitch;
	sm.onlineUrl = toNative(String(options.get("lobby_url", String())));
	sm.onlineCtx = OnlineShellContext();
	sm.onlineCtx.client = sm.online.get();
	sm.onlineCtx.lobbyUrl = sm.onlineUrl;
	sm.onlineCtx.rememberedName = utf8ToU16(toNative(String(options.get("remembered_name", String()))));
	// the host keeps the name (online_status's remembered_name: game.gd writes it to the player's online settings)
	sm.onlineCtx.rememberName = [](const std::u16string &) {};
	// lane ONLINE-4: the kept lobby session (game.gd reads and writes ONLINE-3's user://online_session.txt): online_status's session_token
	sm.onlineCtx.sessionToken = toNative(String(options.get("session_token", String())));
	sm.onlineCtx.rememberSession = [](const std::string &) {};
	sm.onlineCtx.leftGames = sm.onlineLeftGames; // lane ONLINEUX-1
	sm.environment.online = &sm.onlineCtx;
	r["ok"] = true;
	return r;
}

Array AptMenuPlayer::online_take_requests()
{
	Array out;
	if (!m->shell || !m->shell->online)
	{
		return out;
	}
	ShellMode &sm = *m->shell;
	for (const OnlineLobbyClient::HttpRequest &q : sm.online->takeRequests())
	{
		Dictionary d;
		d["id"] = (int64_t)q.id;
		d["method"] = toGodot(q.method);
		d["path"] = toGodot(q.path); // the transport prefixes the lobby URL the player named (online_http.gd: the release privacy guard checks it)
		d["body"] = String::utf8(q.body.c_str(), (int64_t)q.body.size());
		d["token"] = toGodot(q.token);
		d["held"] = q.held; // lane ONLINE-5: a poll the lobby may keep for hold_seconds: its own connection (online_http.gd)
		d["hold_seconds"] = (int64_t)q.holdSeconds;
		d["max_slots"] = (int64_t)q.maxSlots;
		d["channel"] = q.channel == OnlineLobbyClient::HttpRequest::Social ? "social" :
			q.channel == OnlineLobbyClient::HttpRequest::Room ? "room" : "ordinary";
		out.push_back(d);
	}
	return out;
}

void AptMenuPlayer::online_deliver(int64_t id, int64_t status, const String &body, const String &error)
{
	if (!m->shell || !m->shell->online)
	{
		return;
	}
	const CharString b = body.utf8();
	m->shell->online->deliver((int)id, (int)status, std::string(b.get_data(), (size_t)b.length()), toNative(error));
}

bool AptMenuPlayer::online_start_failed(const String &error)
{
	AptOnlineShell *screen = m->shell && m->shell->shell ? dynamic_cast<AptOnlineShell *>(m->shell->shell->findScreenByFilename("OnlineShell.apt")) : nullptr;
	if (!screen)
	{
		return false;
	}
	screen->startFailed(toNative(error));
	return true;
}

String AptMenuPlayer::default_options_path()
{
	return OS::get_singleton()->get_user_data_dir() + "/Options.ini";
}

Dictionary AptMenuPlayer::read_options_file(const String &path)
{
	Dictionary r;
	Dictionary values;
	const std::string native = toNative(path);
	std::error_code ec;
	r["exists"] = std::filesystem::exists(PathIO::native(native), ec); // anything at the path (a folder there is an unreadable file, not a first run)
	OptionPreferences prefs;
	std::string error;
	if (!(bool)r["exists"])
	{
		r["ok"] = true; // the first run: no entries (boot_shell reads the same)
	}
	else if (std::filesystem::is_directory(PathIO::native(native), ec))
	{
		r["ok"] = false;
		r["error"] = toGodot("cannot read " + native + ": a folder");
	}
	else if (!prefs.load(native, &error))
	{
		r["ok"] = false;
		r["error"] = toGodot(error);
	}
	else
	{
		r["ok"] = true;
		for (const auto &kv : prefs.values())
		{
			values[toGodot(kv.first)] = toGodot(kv.second);
		}
	}
	r["values"] = values;
	return r;
}

void AptMenuPlayer::online_game_failed(const String &text)
{
	if (m->shell)
	{
		m->shell->onlineCtx.gameFailed(utf8ToU16(toNative(text)));
	}
}

void AptMenuPlayer::online_set_counts(const Dictionary &counts)
{
	if (!m->shell)
	{
		return;
	}
	OnlineShellContext &c = m->shell->onlineCtx;
	auto get = [&](const char *key) { return counts.has(key) ? (int)std::clamp<int64_t>((int64_t)counts[key], -1, 1000000000) : -1; };
	c.playersOnline = get("players");
	c.gamesOpen = get("games_open");
	c.gamesRunning = get("games_running");
}

String AptMenuPlayer::online_redact(const String &text)
{
	const CharString t = text.utf8();
	return String::utf8(OnlineLobbyClient::redact(std::string(t.get_data(), (size_t)t.length())).c_str());
}

Dictionary AptMenuPlayer::online_status()
{
	Dictionary r;
	if (!m->shell || !m->shell->online)
	{
		r["open"] = false;
		return r;
	}
	ShellMode &sm = *m->shell;
	const OnlineLobbyClient &c = *sm.online;
	auto u8 = [](const std::u16string &w) { return String::utf8(OnlineLobbyClient::utf16ToUtf8(w).c_str()); };
	auto strings = [](const std::vector<std::string> &v) {
		Array a;
		for (const std::string &x : v)
		{
			a.push_back(String::utf8(x.c_str()));
		}
		return a;
	};
	r["open"] = true;
	r["signed_in"] = c.signedIn();
	r["name"] = u8(c.accountName());
	r["remembered_name"] = u8(sm.onlineCtx.rememberedName);
	r["session_token"] = toGodot(sm.onlineCtx.sessionToken); // lane ONLINE-4
	Array mine;
	for (const OnlineLobbyClient::Listing &g : c.mine())
	{
		mine.push_back(toGodot(g.id));
	}
	r["running"] = mine;
	r["in_game"] = c.inGame();
	// lane ONLINEMENU-1: the corner's ping: the player's latest link to the game relay ("" not measured yet, "none" no answer, else "<ms>")
	r["relay_ping"] = toGodot(!c.ownRelayKnown() ? std::string() : c.ownRelayRttMs() < 0 ? std::string("none") : std::to_string(c.ownRelayRttMs()));
	r["host"] = c.amIHost();
	r["mode"] = toGodot(std::string(OnlineLobbyClient::modeName(c.profile()))); // lane UBERJOIN-1: "normal" or "uber" (Quick Match matches by it)
	r["room_mode"] = toGodot(std::string(OnlineLobbyClient::modeName(c.gameMode())));
	r["slot"] = (int64_t)c.localSlot();
	r["game_id"] = toGodot(c.gameId());
	r["busy"] = c.busy();
	Array ready;
	for (int i = 0; i < c.gameInfo().serializedSlotCount(); ++i)
	{
		ready.push_back(c.slotViews()[(size_t)i].ready);
	}
	r["ready"] = ready;
	Array reportPlayers;
	for (size_t i = 0; i < c.slotViews().size(); ++i)
	{
		const auto &slot = c.slotViews()[i];
		if (slot.state != "player" || slot.playerId.empty()) continue;
		Dictionary player;
		player["id"] = toGodot(slot.playerId);
		player["name"] = u8(c.gameInfo().slots[i].name);
		player["you"] = slot.you;
		reportPlayers.push_back(player);
	}
	r["report_players"] = reportPlayers;
	// lane ONLINE-5: the players' start positions (-1: random) and whether the lobby holds the staging room's polls
	Array startPos;
	for (int i = 0; i < c.gameInfo().serializedSlotCount(); ++i)
	{
		startPos.push_back((int64_t)c.gameInfo().slots[i].startPos);
	}
	r["start_pos"] = startPos;
	r["hold_seconds"] = (int64_t)c.holdSeconds();
	AptOnlineShell *screen = sm.shell ? dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt")) : nullptr;
	r["screen"] = screen != nullptr;
	r["messenger"] = screen && screen->messenger() != nullptr;
	if (screen)
	{
		r["page"] = toGodot(screen->currentPage());
		r["state"] = (int64_t)screen->openPlayState();
		r["games"] = strings(screen->gameRows());
		r["chat"] = strings(screen->chatLines());
		r["motd"] = strings(screen->motdLines());
		r["boxes"] = strings(screen->boxesShown());
		r["connecting"] = screen->connectingShown();
		r["level"] = (int64_t)screen->level();
		// lane ONLINE-5: what each Ready box was told to show (SetReadyState), the host's refused starts, the start spots the map tab shows
		Array icons;
		for (int i = 0; i < c.gameInfo().serializedSlotCount(); ++i)
		{
			icons.push_back(toGodot(screen->readyIcon(i)));
		}
		r["ready_icons"] = icons;
		r["start_refusals"] = strings(screen->startRefusals());
		int64_t spots = 0;
		for (GameWindow *w : screen->currentMapSpots())
		{
			spots += w && !w->winIsHidden() ? 1 : 0;
		}
		r["start_spots"] = spots;
	}
	return r;
}

Dictionary AptMenuPlayer::online_social_status() const
{
	Dictionary out;
	out["available"] = false;
	if (!m->shell || !m->shell->online)
	{
		return out;
	}
	const OnlineLobbyClient &c = *m->shell->online;
	const OnlineLobbyClient::SocialState &s = c.social();
	auto text16 = [](const std::u16string &v) { return String::utf8(OnlineLobbyClient::utf16ToUtf8(v).c_str()); };
	auto player = [&](const OnlineLobbyClient::Player &p) {
		Dictionary d;
		d["pid"] = toGodot(p.pid);
		d["code"] = toGodot(p.code);
		d["nick"] = text16(p.nick);
		return d;
	};
	out["available"] = s.available;
	out["unsupported"] = s.unsupported; // ONLINEMENU-1: the lobby has no social service (the screens say "not available on this server yet")
	Dictionary me;
	me["pid"] = toGodot(s.me.pid);
	me["code"] = toGodot(s.me.code);
	me["nick"] = text16(s.me.nick);
	me["hidden"] = s.me.hidden;
	Dictionary privacy;
	privacy["invites"] = toGodot(s.me.privacy.invites);
	privacy["challenges"] = toGodot(s.me.privacy.challenges);
	privacy["requests"] = toGodot(s.me.privacy.requests);
	privacy["presence"] = toGodot(s.me.privacy.presence);
	me["privacy"] = privacy;
	out["me"] = me;
	Array friends;
	for (const auto &f : s.friends)
	{
		Dictionary d = player(f);
		auto presence = std::find_if(s.presence.begin(), s.presence.end(), [&](const auto &p) { return p.pid == f.pid; });
		d["status"] = presence == s.presence.end() ? String("offline") : toGodot(presence->state);
		d["request"] = false;
		friends.push_back(d);
	}
	out["friends"] = friends;
	Array requests;
	for (const auto &item : s.requests)
	{
		Dictionary d;
		d["id"] = toGodot(item.id);
		d["from"] = player(item.from);
		d["to"] = player(item.to);
		d["at"] = (int64_t)item.at;
		requests.push_back(d);
	}
	out["requests"] = requests;
	Array invites;
	for (const auto &item : s.invites)
	{
		Dictionary d;
		d["id"] = toGodot(item.id);
		d["from"] = player(item.from);
		d["to"] = player(item.to);
		d["game"] = toGodot(item.game);
		d["kind"] = toGodot(item.kind);
		d["mode"] = item.hasMode ? Variant(toGodot(item.mode)) : Variant();
		d["note"] = toGodot(item.note);
		d["expires"] = (int64_t)item.expires;
		invites.push_back(d);
	}
	out["invites"] = invites;
	Array presence;
	for (const auto &item : s.presence)
	{
		Dictionary d;
		d["pid"] = toGodot(item.pid);
		d["state"] = toGodot(item.state);
		d["game"] = item.hasGame ? Variant(toGodot(item.game)) : Variant();
		d["joinable"] = item.joinable;
		d["since"] = (int64_t)item.since;
		d["map"] = item.hasMap ? Variant(toGodot(item.map)) : Variant();
		d["mode"] = item.hasMode ? Variant(toGodot(item.mode)) : Variant();
		presence.push_back(d);
	}
	out["presence"] = presence;
	Array notices;
	for (const auto &item : s.notices)
	{
		Dictionary d;
		d["id"] = toGodot(item.id);
		d["kind"] = toGodot(item.kind);
		d["invite"] = item.hasInvite ? Variant(toGodot(item.invite)) : Variant();
		d["at"] = (int64_t)item.at;
		notices.push_back(d);
	}
	out["notices"] = notices;
	Array recent;
	for (const auto &game : s.recent)
	{
		Dictionary d;
		d["game"] = toGodot(game.game);
		d["at"] = (int64_t)game.at;
		Array players;
		for (const auto &entry : game.players)
		{
			Dictionary p = player(entry.player);
			p["target"] = toGodot(entry.target);
			players.push_back(p);
		}
		d["players"] = players;
		recent.push_back(d);
	}
	out["recent"] = recent;
	Array blocks;
	for (const auto &item : s.blocks)
	{
		Dictionary d = player(item.player);
		d["note"] = toGodot(item.note);
		d["at"] = (int64_t)item.at;
		blocks.push_back(d);
	}
	out["blocks"] = blocks;
	out["blocks_next"] = s.hasBlocksNext ? Variant(toGodot(s.blocksNext)) : Variant();
	Array heads;
	for (const auto &item : s.chatHeads)
	{
		Dictionary d;
		d["pid"] = toGodot(item.pid);
		d["last_id"] = (int64_t)item.lastId;
		d["read_id"] = (int64_t)item.readId;
		heads.push_back(d);
	}
	out["chat_heads"] = heads;
	out["chat"] = heads;
	Array threads;
	for (const auto &item : s.threads)
	{
		Dictionary d;
		d["pid"] = toGodot(item.pid);
		d["nick"] = text16(item.nick);
		d["code"] = toGodot(item.code);
		d["last_id"] = (int64_t)item.lastId;
		d["read_id"] = (int64_t)item.readId;
		threads.push_back(d);
	}
	out["threads"] = threads;
	out["threads_next"] = s.hasThreadsNext ? Variant(toGodot(s.threadsNext)) : Variant();
	Dictionary conversations;
	for (const auto &pair : s.conversations)
	{
		Dictionary conversation;
		Array messages;
		for (const auto &item : pair.second.messages)
		{
			Dictionary d;
			d["id"] = (int64_t)item.id;
			d["from"] = toGodot(item.from);
			d["to"] = toGodot(item.to);
			d["text"] = toGodot(item.text);
			d["at"] = (int64_t)item.at;
			d["client_id"] = toGodot(item.clientId);
			messages.push_back(d);
		}
		conversation["messages"] = messages;
		conversation["next"] = pair.second.hasNext ? Variant((int64_t)pair.second.next) : Variant();
		conversation["has_more"] = pair.second.hasNext;
		conversation["last_id"] = (int64_t)pair.second.lastId;
		conversation["read_id"] = (int64_t)pair.second.readId;
		conversations[toGodot(pair.first)] = conversation;
	}
	out["conversations"] = conversations;
	out["seq"] = toGodot(s.seq);
	Dictionary stats;
	stats["v"] = 1;
	stats["players"] = (int64_t)s.stats.players;
	stats["games_open"] = (int64_t)s.stats.gamesOpen;
	stats["games_running"] = (int64_t)s.stats.gamesRunning;
	stats["as_of"] = (int64_t)s.stats.asOf;
	out["stats"] = stats;
	Array roomPlayers;
	if (c.inGame())
	{
		for (std::size_t i = 0; i < c.slotViews().size(); ++i)
		{
			const auto &slot = c.slotViews()[i];
			if (slot.state != "player" || slot.you) continue;
			Dictionary d;
			d["pid"] = slot.pid.empty() ? Variant() : Variant(toGodot(slot.pid));
			d["nick"] = text16(c.gameInfo().slots[i].name);
			d["blocked"] = slot.blocked;
			d["target"] = slot.playerId.empty() ? Variant() : Variant(toGodot(slot.playerId));
			roomPlayers.push_back(d);
		}
	}
	out["room_players"] = roomPlayers;
	return out;
}

Array AptMenuPlayer::online_social_events()
{
	Array out;
	if (!m->shell || !m->shell->online) return out;
	for (const auto &event : m->shell->online->takeSocialEvents())
	{
		Dictionary d;
		const char *kind = event.kind == OnlineLobbyClient::SocialEvent::Error ? "error" :
			event.kind == OnlineLobbyClient::SocialEvent::Chat ? "chat" :
			event.kind == OnlineLobbyClient::SocialEvent::Recovery ? "recovery" :
			event.kind == OnlineLobbyClient::SocialEvent::NoticeEvent ? "notice" : "changed";
		d["kind"] = kind;
		if (!event.request.empty()) d["request"] = toGodot(event.request);
		if (!event.code.empty()) d["code"] = toGodot(event.code);
		if (!event.pid.empty()) d["pid"] = toGodot(event.pid);
		if (!event.id.empty()) d["id"] = toGodot(event.id);
		if (!event.text.empty() || event.kind == OnlineLobbyClient::SocialEvent::Recovery)
			d[event.kind == OnlineLobbyClient::SocialEvent::Recovery ? "code" : "message"] = toGodot(event.text);
		out.push_back(d);
	}
	return out;
}

Dictionary AptMenuPlayer::online_social_apply(const Dictionary &spec)
{
	Dictionary out;
	auto fail = [&](const std::string &error) {
		out["ok"] = false;
		out["error"] = toGodot(error);
		return out;
	};
	if (!m->shell || !m->shell->online) return fail("not_in_shell");
	OnlineLobbyClient &c = *m->shell->online;
	const std::string action = toNative(String(spec.get("action", String())));
	const std::string pid = toNative(String(spec.get("pid", String())));
	const std::string id = toNative(String(spec.get("id", String())));
	const std::string text = toNative(String(spec.get("text", String())));
	const std::string note = toNative(String(spec.get("note", String())));
	bool ok = false;
	if (action == "request_friend_code") ok = c.requestFriendCode(toNative(String(spec.get("code", String()))));
	else if (action == "request_friend") ok = c.requestFriend(pid);
	else if (action == "accept_friend") ok = c.acceptFriend(id);
	else if (action == "decline_friend") ok = c.declineFriend(id);
	else if (action == "remove_friend") ok = c.removeFriend(pid);
	else if (action == "block_player") ok = c.blockPlayer(pid, note);
	else if (action == "unblock_player") ok = c.unblockPlayer(pid);
	else if (action == "fetch_blocks") ok = c.fetchBlocks(toNative(String(spec.get("after", String()))));
	else if (action == "send_invite")
		ok = c.sendInvite(pid, toNative(String(spec.get("kind", String("invite")))),
			toNative(String(spec.get("mode", String()))), note);
	else if (action == "accept_invite") ok = c.acceptInvite(id);
	else if (action == "leave_accept_invite") ok = c.acceptInviteAfterLeave(id);
	else if (action == "decline_invite") ok = c.declineInvite(id, bool(spec.get("busy", false)));
	else if (action == "fetch_recent") ok = c.fetchRecent();
	else if (action == "fetch_threads") ok = c.fetchThreads(toNative(String(spec.get("after", String()))));
	else if (action == "fetch_messages")
		ok = c.fetchMessages(pid, (std::uint64_t)(int64_t)spec.get("after", (int64_t)0));
	else if (action == "send_message")
		ok = c.sendMessage(pid, toNative(String(spec.get("client_id", String()))), text);
	else if (action == "mark_read")
		ok = c.markRead(pid, (std::uint64_t)(int64_t)spec.get("through", (int64_t)0));
	else if (action == "set_privacy")
	{
		const Dictionary value = spec.get("privacy", Dictionary());
		OnlineLobbyClient::Privacy privacy;
		privacy.invites = toNative(String(value.get("invites", String())));
		privacy.challenges = toNative(String(value.get("challenges", String())));
		privacy.requests = toNative(String(value.get("requests", String())));
		privacy.presence = toNative(String(value.get("presence", String())));
		ok = c.setPrivacy(privacy);
	}
	else if (action == "set_presence")
		ok = c.setPresence(toNative(String(spec.get("state", String()))), spec.has("hidden"), bool(spec.get("hidden", false)));
	else if (action == "generate_recovery") ok = c.generateRecovery();
	else if (action == "leave_game")
	{
		if (!c.inGame()) return fail("not_in_game");
		c.leaveGame();
		ok = true;
	}
	else if (action == "create_challenge")
	{
		AptOnlineShell *screen = m->shell->shell ?
			dynamic_cast<AptOnlineShell *>(m->shell->shell->findScreenByFilename("OnlineShell.apt")) : nullptr;
		if (!screen) return fail("online_screen_closed");
		ok = screen->createChallenge(pid, toNative(String(spec.get("mode", String()))),
			toNative(String(spec.get("password", String()))));
		if (!ok) return fail("challenge_not_started");
	}
	else return fail("bad_action");
	if (!ok) return fail(c.lastSocialError().empty() ? "social_unavailable" : c.lastSocialError());
	out["ok"] = true;
	return out;
}

// Online questions and Messenger synchronization.
namespace
{
AptOnlineShell *onlineShellOf(Shell *shell)
{
	return shell ? dynamic_cast<AptOnlineShell *>(shell->findScreenByFilename("OnlineShell.apt")) : nullptr;
}
} // namespace

bool AptMenuPlayer::online_ask(const String &key, const String &title, const String &text)
{
	AptOnlineShell *s = onlineShellOf(m->shell ? m->shell->shell.get() : nullptr);
	return s && s->askYesNo(toNative(key).substr(0, 64), utf8ToU16(toNative(title)).substr(0, 64), utf8ToU16(toNative(text)).substr(0, 400));
}

bool AptMenuPlayer::online_asking(const String &key)
{
	AptOnlineShell *s = onlineShellOf(m->shell ? m->shell->shell.get() : nullptr);
	return s && s->asking(toNative(key));
}

void AptMenuPlayer::online_cancel_ask(const String &key)
{
	if (AptOnlineShell *s = onlineShellOf(m->shell ? m->shell->shell.get() : nullptr))
	{
		s->cancelAsk(toNative(key));
	}
}

PackedStringArray AptMenuPlayer::online_answers()
{
	PackedStringArray out;
	if (AptOnlineShell *s = onlineShellOf(m->shell ? m->shell->shell.get() : nullptr))
	{
		for (const std::string &a : s->takeAnswers())
		{
			out.push_back(toGodot(a));
		}
	}
	return out;
}

// ---- lane CHEAT-1: the in-game menu's Cheat Mode panel (GUI/AptScreens/AptQuitMenu.h) ---------------------------------------------------------------------

namespace
{
// the in-game menu the device opened (AptMenuPlayer::quit_menu: the shell's overlay, not a screen of its stack)
AptQuitMenu *quitMenuOf(ShellMode *sm)
{
	return sm ? sm->quitMenu.get() : nullptr;
}
} // namespace

bool AptMenuPlayer::quit_menu_cheat_button(const String &label)
{
	AptQuitMenu *q = quitMenuOf(m->shell.get());
	if (q)
	{
		q->setCheatButton(utf8ToU16(toNative(label)).substr(0, 32));
	}
	return q != nullptr;
}

Dictionary AptMenuPlayer::quit_menu_cheat_page_show(const Dictionary &spec)
{
	Dictionary r;
	r["ok"] = false;
	AptQuitMenu *q = quitMenuOf(m->shell.get());
	if (!q)
	{
		r["error"] = "QuitMenu.apt is not up";
		return r;
	}
	std::string error;
	const bool ok = q->openCheatPage(pageSpecOf(spec), &error);
	r["ok"] = ok;
	r["error"] = toGodot(error);
	if (!ok)
	{
		UtilityFunctions::printerr(toGodot("SHELL cheat mode page: " + error));
	}
	return r;
}

void AptMenuPlayer::quit_menu_cheat_page_close()
{
	if (AptQuitMenu *q = quitMenuOf(m->shell.get()))
	{
		q->closeCheatPage();
	}
}

Array AptMenuPlayer::quit_menu_cheat_events()
{
	AptQuitMenu *q = quitMenuOf(m->shell.get());
	return q ? pageEventsOf(q->takeCheatEvents()) : Array();
}

Dictionary AptMenuPlayer::quit_menu_cheat_state() const
{
	Dictionary d;
	AptQuitMenu *q = quitMenuOf(m->shell.get());
	d["menu"] = q != nullptr;
	d["open"] = q && q->cheatPageOpen();
	d["button"] = Rect2(AptQuitMenu::kCheatButtonLeft, AptQuitMenu::kCheatButtonTop, AptQuitMenu::kCheatButtonRight - AptQuitMenu::kCheatButtonLeft,
		AptQuitMenu::kCheatButtonBottom - AptQuitMenu::kCheatButtonTop);
	d["has_button"] = q && !q->cheatButton().empty();
	OpenBFMEOptionsPage *page = q ? q->cheatPage() : nullptr;
	if (!page || !page->shown())
	{
		return d;
	}
	d["tab"] = page->spec().tab;
	Array tabs;
	for (std::size_t i = 0; i < page->spec().tabs.size(); ++i)
	{
		std::array<int, 4> r{};
		page->rect("tab:" + std::to_string(i), r);
		tabs.push_back(stageRect(r));
	}
	d["tabs"] = tabs;
	Dictionary rows;
	for (const OpenBFMEPageRow &row : page->spec().rows)
	{
		if (row.key.empty())
		{
			continue;
		}
		Dictionary rd;
		std::array<int, 4> r{};
		if (page->rect(row.key, r))
		{
			rd["rect"] = stageRect(r);
		}
		rd["value"] = row.value;
		rd["value_text"] = toGodot(u16ToUtf8(row.valueText));
		rd["label"] = toGodot(u16ToUtf8(row.label));
		rows[toGodot(row.key)] = rd;
	}
	d["rows"] = rows;
	d["help"] = toGodot(u16ToUtf8(page->helpText()));
	return d;
}

Dictionary AptMenuPlayer::online_messenger_sync(const Dictionary &spec)
{
	Dictionary out;
	out["open"] = false;
	if (!m->shell || !m->shell->shell) return out;
	AptOnlineShell *screen = dynamic_cast<AptOnlineShell *>(m->shell->shell->findScreenByFilename("OnlineShell.apt"));
	AptMessenger *messenger = screen ? screen->messenger() : nullptr;
	if (!messenger) return out;
	if (spec.has("rows"))
	{
		const Array rows = spec.get("rows", Array());
		for (int tab = 0; tab < 2; ++tab)
		{
			std::vector<AptMessenger::Row> nativeRows;
			if (rows.size() > tab && rows[tab].get_type() == Variant::ARRAY)
			{
				const Array entries = rows[tab];
				for (int i = 0; i < entries.size() && i < 200; ++i)
				{
					AptMessenger::Row row;
					if (entries[i].get_type() == Variant::DICTIONARY)
					{
						const Dictionary item = entries[i];
						row.id = toNative(String(item.get("pid", String())));
						row.name = utf8ToU16(toNative(String(item.get("nick", String()))));
						row.status = utf8ToU16(toNative(String(item.get("status", String()))));
						row.request = bool(item.get("request", false));
						row.blocked = bool(item.get("blocked", false));      // lane ONLINEMENU-1
						row.friendable = bool(item.get("friendable", true));
					}
					else if (entries[i].get_type() == Variant::ARRAY)
					{
						const Array item = entries[i];
						if (item.size() > 0) row.id = toNative(String(item[0]));
						if (item.size() > 1) row.name = utf8ToU16(toNative(String(item[1])));
						if (item.size() > 2) row.status = utf8ToU16(toNative(String(item[2])));
						if (item.size() > 3) row.request = bool(item[3]);
					}
					else continue;
					nativeRows.push_back(std::move(row));
				}
			}
			messenger->setRows(tab, nativeRows);
		}
	}
	if (spec.has("chat"))
	{
		const Array chats = spec.get("chat", Array());
		for (int tab = 0; tab < 2; ++tab)
		{
			std::vector<std::u16string> lines;
			if (chats.size() > tab && chats[tab].get_type() == Variant::ARRAY)
			{
				const Array entries = chats[tab];
				for (int i = 0; i < entries.size() && i < 200; ++i)
					lines.push_back(utf8ToU16(toNative(String(entries[i]))));
			}
			messenger->setChat(tab, lines);
		}
	}
	if (spec.has("clear_entry_if"))
		messenger->acknowledgeMessage(utf8ToU16(toNative(String(spec.get("clear_entry_if", String())))));
	if (spec.has("select")) // lane ONLINEMENU-1 (Sol): the player the FRIENDS page chose
		messenger->selectPlayer(toNative(String(spec.get("select", String()))));
	out["open"] = true;
	out["tab"] = messenger->activeTab();
	out["selected"] = toGodot(messenger->selectedPlayer());
	Array actions;
	for (const auto &action : messenger->takeActions())
	{
		Dictionary d;
		const char *kind = action.kind == AptMessenger::Action::Select ? "select" :
			action.kind == AptMessenger::Action::AddFriend ? "add_friend" :
			action.kind == AptMessenger::Action::RemoveFriend ? "remove_friend" :
			action.kind == AptMessenger::Action::AcceptFriend ? "accept_friend" :
			action.kind == AptMessenger::Action::Invite ? "invite" :
			action.kind == AptMessenger::Action::Ignore ? "block" :
			action.kind == AptMessenger::Action::Unignore ? "unblock" : "send";
		d["kind"] = kind;
		d["pid"] = toGodot(action.player);
		d["text"] = String::utf8(OnlineLobbyClient::utf16ToUtf8(action.text).c_str());
		actions.push_back(d);
	}
	out["actions"] = actions;
	return out;
}

Dictionary AptMenuPlayer::online_apply(const Dictionary &spec)
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
	AptOnlineShell *screen = dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt"));
	if (!screen)
	{
		errors.push_back("OnlineShell.apt is not up");
		return out;
	}
	auto setEntry = [&](const char *key, GameWindow *w, bool combo) {
		if (!spec.has(key))
		{
			return;
		}
		if (!w)
		{
			errors.push_back(String("no gadget for ") + key);
			return;
		}
		const std::u16string text = utf8ToU16(toNative(String(spec[key])));
		if (combo)
		{
			GadgetComboBoxSetText(w, text);
		}
		else
		{
			GadgetTextEntrySetText(w, text);
		}
	};
	setEntry("nickname", screen->nicknameWindow(), true);
	if (spec.has("nickname"))
	{
		screen->nicknameEdited(); // lane ONLINEUX-1: as the combo's text change does when typed (the Login button follows the name)
	}
	setEntry("game_name", screen->gameNameWindow(), false);
	setEntry("game_password", screen->gamePasswordWindow(), false);
	if (spec.has("select_row"))
	{
		if (GameWindow *list = screen->gameList())
		{
			GadgetListBoxSetSelected(list, (int)(int64_t)spec["select_row"]);
		}
		else
		{
			errors.push_back("no game list");
		}
	}
	if (spec.has("point"))
	{
		// the pointer to the centre of the first button under a clip of the shell movie (Toggles.nav.SelectOpenPlay ...): a click is "point", then
		// "mouse" true and false on later frames, as a player's
		// "point" is a clip path (its first button), or "*part": the first visible button whose target path contains `part`
		const std::string where = toNative(String(spec["point"]));
		const bool named = !where.empty() && where[0] == '*';
		AptCharacterInst *clip = named ? sm.wm->apt().level(screen->level()) : sm.wm->apt().resolvePath(sm.wm->apt().level(screen->level()), where);
		std::function<AptButtonInst *(AptCharacterInst *)> first = [&](AptCharacterInst *c) -> AptButtonInst * {
			if (!c)
			{
				return nullptr;
			}
			if (AptButtonInst *b = c->asButton())
			{
				if (named)
				{
					return b->globallyVisible() && b->targetPath().find(where.substr(1)) != std::string::npos ? b : nullptr;
				}
				return b;
			}
			if (AptSpriteInst *sp = c->asSprite())
			{
				for (AptCharacterInst *k : sp->children())
				{
					if (AptButtonInst *b = first(k))
					{
						return b;
					}
				}
			}
			return nullptr;
		};
		AptButtonInst *b = first(clip);
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!b || !b->contentBounds(x0, y0, x1, y1))
		{
			errors.push_back(String("no button under ") + String(spec["point"]));
		}
		else
		{
			float x = 0, y = 0;
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
			sm.wm->postMouseMove(x, y);
			out["x"] = x;
			out["y"] = y;
		}
	}
	if (spec.has("start_spot"))
	{
		// lane ONLINE-5: the pointer to the centre of a start spot of the MAP tab's map window (a gadget, not a movie button); the click is "mouse"
		// true and false on later frames, as a player's
		const int n = (int)(int64_t)spec["start_spot"];
		const std::vector<GameWindow *> &spots = screen->currentMapSpots();
		if (n < 0 || n >= (int)spots.size() || !spots[(size_t)n] || spots[(size_t)n]->winIsHidden())
		{
			errors.push_back(String("no start spot ") + String::num_int64(n) + " is shown");
		}
		else
		{
			int x = 0, y = 0, w = 0, h = 0;
			spots[(size_t)n]->winGetScreenPosition(&x, &y);
			spots[(size_t)n]->winGetSize(&w, &h);
			sm.wm->postMouseMove((float)x + (float)w / 2.0f, (float)y + (float)h / 2.0f);
		}
	}
	if (spec.has("join_details"))
	{
		// lane JOINVER-1: the pointer to the build refusal's DETAILS button (the gadget layer's, beside the GuiFX box); the click is "mouse"
		sm.wm->postMouseMove((AptOnlineShell::kDetailsLeft + AptOnlineShell::kDetailsRight) / 2.0f, (AptOnlineShell::kDetailsTop + AptOnlineShell::kDetailsBottom) / 2.0f);
	}
	if (spec.has("box"))
	{
		// lane ONLINE-4: a button of the message box (Ok / Cancel / Yes / No): its clip's hide and its own command, as the button runs them
		if (AptMessageBox *box = screen->messageBox())
		{
			box->hide(true);
			sm.wm->invokeCallback("_level" + std::to_string(box->level()) + ".MessageBox_OnButton" + toNative(String(spec["box"])), "");
		}
		else
		{
			errors.push_back("no message box");
		}
	}
	if (spec.has("mouse"))
	{
		sm.wm->postMouseButton((bool)spec["mouse"]);
	}
	if (spec.has("chat_tab"))
	{
		// the setup page's CHAT tab (TabButtons.ChatTab), pressed through its own DoButtonCallBack
		std::function<AptCharacterInst *(AptCharacterInst *)> find = [&](AptCharacterInst *c) -> AptCharacterInst * {
			AptSpriteInst *sp = c ? c->asSprite() : nullptr;
			if (!sp)
			{
				return nullptr;
			}
			for (AptCharacterInst *k : sp->children())
			{
				if (k && k->instName() == "ChatTab" && c->instName() == "TabButtons" && k->globallyVisible())
				{
					return k;
				}
				if (AptCharacterInst *f = find(k))
				{
					return f;
				}
			}
			return nullptr;
		};
		AptCharacterInst *tab = find(sm.wm->apt().level(screen->level()));
		std::string err;
		if (!tab || !sm.wm->apt().invoke(tab, "DoButtonCallBack", {}, nullptr, &err))
		{
			errors.push_back(toGodot("the chat tab: " + (tab ? err : std::string("not shown"))));
		}
	}
	if (spec.has("chat"))
	{
		// the staging room's chat entry, sent by its own command (AptMpChat::Send)
		if (GameWindow *entry = screen->chatEntryWindow())
		{
			GadgetTextEntrySetText(entry, utf8ToU16(toNative(String(spec["chat"]))));
			sm.wm->invokeCallback("AptMpChat::Send", "");
		}
		else
		{
			errors.push_back("no chat entry");
		}
	}
	if (spec.has("invoke"))
	{
		// a function of the shell movie's root (its nav: SelectHome / SelectOpenPlay ...), as a button of the movie calls it
		std::vector<std::string> args;
		const Array a = spec.get("args", Array());
		for (int64_t i = 0; i < a.size(); ++i)
		{
			args.push_back(toNative(String(a[i])));
		}
		std::string err;
		if (!sm.wm->invokeAS(screen->level(), toNative(String(spec["invoke"])), args, nullptr, &err))
		{
			errors.push_back(toGodot("invoke " + toNative(String(spec["invoke"])) + ": " + err));
		}
	}
	if (spec.has("command"))
	{
		if (!sm.wm->invokeCallback(toNative(String(spec["command"])), toNative(String(spec.get("argument", String())))))
		{
			errors.push_back(String("unknown command ") + String(spec["command"]));
		}
	}
	out["errors"] = errors;
	out["ok"] = errors.is_empty();
	return out;
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

bool AptMenuPlayer::set_clip_property(int level, const String &path, const String &name, double value)
{
	AptSpriteInst *root = m_booted ? m->A()->level(level) : nullptr;
	AptCharacterInst *inst = root && !path.is_empty() ? m->A()->resolvePath(root, toNative(path)) : nullptr;
	if (!inst || !inst->setProperty(toNative(name.to_lower()), AptValue::number((float)value)))
	{
		return false;
	}
	m->dirty = true;
	return true;
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
Dictionary AptMenuPlayer::get_cursor_tooltip() const
{
	Dictionary out;
	const ShellCursorTooltip *t = m->shell ? m->shell->cursorTooltip.get() : nullptr;
	out["ok"] = t != nullptr;
	if (!t)
	{
		return out;
	}
	out["label"] = toGodot(t->label());
	out["shown"] = toGodot(u16ToUtf8(t->tooltip().shown()));
	float px = 0.0f, py = 0.0f;
	m->shell->wm->apt().mousePosition(px, py);
	out["pointer"] = Vector2(px, py);
	Array calls, errors;
	for (const std::string &c : t->calls())
	{
		calls.push_back(toGodot(c));
	}
	for (const std::string &e : t->errors())
	{
		errors.push_back(toGodot(e));
	}
	out["calls"] = calls;
	out["errors"] = errors;
	return out;
}

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
		// lane OPTS-6: retail pushes QuitMenu.apt on the shell (RW 0x921B0F -> RW 0x75E3B7): a screen it opens (Options, the save / load screen) hides it
		// and takes its input until that screen is popped (Shell::setOverlay)
		sm.shell->setOverlay(sm.quitMenu.get());
		out["ok"] = true;
	}
	else if (kind == "close")
	{
		out["ok"] = sm.quitMenu != nullptr;
		if (sm.shell->overlay() == sm.quitMenu.get())
		{
			sm.shell->clearOverlay(); // lane OPTS-6
		}
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
		out["movie_loaded"] = sm.wm->isAptWindowLoaded(sm.quitMenu->level()); // lane SAVE-UI1: false while a screen over the menu has it unloaded
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

Dictionary AptMenuPlayer::get_command_map()
{
	// lane OPTS-2: the hot keys of the CommandMap (MetaMap::parseMetaMap RW 0x5DAD46; the files the HUD loads, GodotInGameHud: Data\INI\CommandMap.ini then
	// CommandMap.ini) for the OpenBFME screen's read-only Controls tab, with the game text of each record's DisplayName / Description
	Dictionary r;
	Array rows;
	if (!m->shell || !m->shell->environment.fileSystem)
	{
		r["ok"] = false;
		r["error"] = "the shell is not booted (no archives)";
		r["rows"] = rows;
		return r;
	}
	std::vector<CommandMapEntry> entries;
	std::string error;
	if (!ListCommandMap(*m->shell->environment.fileSystem, entries, &error))
	{
		r["ok"] = false;
		r["error"] = toGodot(error);
		r["rows"] = rows;
		return r;
	}
	const GameTextSource *text = m->shell->text.get();
	auto label = [text](const std::string &l) -> String {
		if (l.empty())
		{
			return String();
		}
		bool exists = false;
		const std::u16string t = fetchOrMissing(text, l, &exists);
		return exists ? toGodot(loadScreenU16ToUtf8(t)) : String();
	};
	for (const CommandMapEntry &e : entries)
	{
		Dictionary d;
		d["name"] = toGodot(e.name);
		d["key"] = toGodot(e.key);
		d["transition"] = e.transition;
		d["ctrl"] = e.ctrl;
		d["shift"] = e.shift;
		d["alt"] = e.alt;
		d["game"] = e.game;
		d["shell"] = e.shell;
		d["category"] = e.category;
		d["display_name"] = label(e.displayName);
		d["description"] = label(e.description);
		rows.push_back(d);
	}
	r["ok"] = true;
	r["rows"] = rows;
	return r;
}

Dictionary AptMenuPlayer::shell_mapped_image(const String &name)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "the shell is not booted";
		return r;
	}
	ShellMode &sm = *m->shell;
	const std::string imageName = toNative(name);
	const ::Image *img = sm.skins.images.findImageByName(imageName);
	if (!img)
	{
		r["error"] = toGodot("image '" + imageName + "' is not in the mapped image collection");
		return r;
	}
	Ref<ImageTexture> texture = m->imageTexture(sm, img, imageName);
	if (texture.is_null())
	{
		r["error"] = toGodot("texture '" + img->filename + "' of image '" + imageName + "' cannot be loaded");
		return r;
	}
	const float w = (float)texture->get_width(), h = (float)texture->get_height();
	r["ok"] = true;
	r["texture"] = texture;
	r["region"] = Rect2(img->uvLo[0] * w, img->uvLo[1] * h, (img->uvHi[0] - img->uvLo[0]) * w, (img->uvHi[1] - img->uvLo[1]) * h);
	r["size"] = Vector2((float)img->imageSize.x, (float)img->imageSize.y);
	return r;
}

Dictionary AptMenuPlayer::shell_font(const String &name, float size)
{
	Dictionary r;
	float drawSize = size;
	const std::string request = toNative(name);
	Ref<Font> font = m->fontFor(request, size, &drawSize);
	const FontRequestResult resolved = m->fonts.resolve(request, size);
	r["font"] = font;
	r["size"] = drawSize;
	r["name"] = toGodot(resolved.name);
	r["fallback"] = m->fontFallbacks.count(resolved.name) != 0; // fontFor records a request it answers with the engine default
	return r;
}


// ---- lane ONLINEHUB-1: the platform API and the recorded games (GodotAptPlayer.h) ----------------------------------------------------------------------

Dictionary AptMenuPlayer::set_platform(const Dictionary &options)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	ShellMode &sm = *m->shell;
	const std::string install = toNative(String(options.get("install", String())));
	if (!PlatformApiClient::validInstall(install))
	{
		r["error"] = "the install id is not 32 lowercase hex digits";
		return r;
	}
	if (!sm.platform)
	{
		PlatformApiClient::Options o;
		o.install = install;
		o.nick = toNative(String(options.get("nick", String())));
		o.clientPlatform = toNative(String(options.get("client_platform", String())));
		o.clientVersion = toNative(String(options.get("client_version", String())));
		o.shareResults = (bool)options.get("share_results", true);
		o.searchTimeoutMs = (std::uint32_t)std::clamp<int64_t>((int64_t)options.get("search_timeout_s", (int64_t)600), 10, 3600) * 1000;
		o.resultRetryMs = (std::uint32_t)std::clamp<int64_t>((int64_t)options.get("result_retry_s", (int64_t)60), 1, 3600) * 1000;
		sm.platform = std::make_unique<PlatformApiClient>(o);
	}
	else if (sm.platform->options().install != install)
	{
		r["error"] = "the platform client exists with another install id";
		return r;
	}
	sm.platformUrl = toNative(String(options.get("base_url", String())));
	sm.hubCtx.platform = sm.platform.get();
	sm.hubCtx.records = &sm.records;
	sm.hubCtx.shareResults = (bool)options.get("share_results", true);
	sm.platform->setShareResults(sm.hubCtx.shareResults); // off: the results still queued or waiting for their retry are dropped
	sm.environment.onlineHub = &sm.hubCtx;
	r["ok"] = true;
	return r;
}

void AptMenuPlayer::set_share_results(bool share)
{
	if (!m->shell)
	{
		return;
	}
	m->shell->hubCtx.shareResults = share;
	if (m->shell->platform)
	{
		m->shell->platform->setShareResults(share);
	}
}

Array AptMenuPlayer::platform_take_requests()
{
	Array out;
	if (!m->shell || !m->shell->platform)
	{
		return out;
	}
	PlatformApiClient &p = *m->shell->platform;
	p.update(NetMilliseconds());
	for (const PlatformApiClient::HttpRequest &q : p.takeRequests())
	{
		Dictionary d;
		d["id"] = (int64_t)q.id;
		d["method"] = toGodot(q.method);
		d["path"] = toGodot(q.path);
		d["body"] = String::utf8(q.body.c_str(), (int64_t)q.body.size());
		d["token"] = String();
		out.push_back(d);
	}
	return out;
}

void AptMenuPlayer::platform_deliver(int64_t id, int64_t status, const String &body, const String &error)
{
	if (!m->shell || !m->shell->platform)
	{
		return;
	}
	const CharString utf8 = body.utf8();
	m->shell->platform->deliver((int)id, (int)status, std::string(utf8.get_data(), (std::size_t)utf8.length()), toNative(error));
}

Array AptMenuPlayer::platform_events()
{
	Array out;
	if (!m->shell || !m->shell->platform)
	{
		return out;
	}
	static const char *const kinds[] = { "health", "qm", "tournaments", "tournament", "registered", "global", "result_sent", "result_dropped", "refused" };
	for (const PlatformApiClient::Event &e : m->shell->platform->takeEvents())
	{
		Dictionary d;
		d["kind"] = kinds[(int)e.kind];
		d["id"] = toGodot(e.id);
		d["code"] = toGodot(e.code);
		d["request"] = toGodot(e.request);
		d["text"] = String::utf8(e.text.c_str());
		d["flag"] = e.flag;
		out.push_back(d);
	}
	return out;
}

Dictionary AptMenuPlayer::set_game_records(const String &text)
{
	Dictionary r;
	r["ok"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	ShellMode &sm = *m->shell;
	const CharString utf8 = text.utf8();
	std::vector<std::string> rejected;
	sm.records = GameRecords::load(std::string(utf8.get_data(), (std::size_t)utf8.length()), &rejected);
	sm.hubCtx.records = &sm.records;
	sm.hubCtx.recordsProblem = rejected.empty() ? std::string() : std::to_string(rejected.size()) + " line(s) refused, the first: " + rejected.front();
	sm.environment.onlineHub = &sm.hubCtx;
	Array rej;
	for (const std::string &x : rejected)
	{
		rej.push_back(String::utf8(x.c_str()));
	}
	r["ok"] = true;
	r["count"] = (int64_t)sm.records.size();
	r["rejected"] = rej;
	return r;
}

Dictionary AptMenuPlayer::record_game(const String &json)
{
	Dictionary r;
	r["ok"] = false;
	r["reported"] = false;
	if (!m->shell)
	{
		r["error"] = "not in shell mode";
		return r;
	}
	ShellMode &sm = *m->shell;
	const CharString utf8 = json.utf8();
	GameRecord rec;
	std::string err;
	if (!GameRecords::parse(std::string(utf8.get_data(), (std::size_t)utf8.length()), rec, &err))
	{
		r["error"] = String::utf8(("the game's record was refused: " + err).c_str());
		return r;
	}
	for (const GameRecord &x : sm.records)
	{
		if (x.id == rec.id)
		{
			r["error"] = String::utf8(("game " + rec.id + " is recorded already").c_str());
			return r;
		}
	}
	sm.records.push_back(rec);
	if (sm.environment.fileSystem && isBfme1Data(*sm.environment.fileSystem) && sm.environment.profiles && sm.environment.skirmish)
	{
		std::string error;
		if (!Bfme1SkirmishProfiles::recordGame(*sm.environment.profiles, *sm.environment.skirmish, rec, &error, sm.environment.skirmishHonors))
			sm.wm->note("skirmish-profile-stats", error);
	}
	if (sm.records.size() > GameRecords::kMaxRecords)
	{
		sm.records.erase(sm.records.begin());
	}
	sm.hubCtx.records = &sm.records;
	sm.environment.onlineHub = &sm.hubCtx;
	r["ok"] = true;
	r["line"] = String::utf8(GameRecords::toJson(rec).c_str());
	// the platform's results: LAN and online games only, when the player shares them (the Options row), from the client the host made
	std::string why;
	if (!sm.platform)
	{
		why = "no platform client";
	}
	else if (!sm.hubCtx.shareResults)
	{
		why = "the player does not share game results";
	}
	else
	{
		const std::string body = GameRecords::resultBody(rec, sm.platform->options().install, rec.me() ? rec.me()->nick : std::string(),
			sm.platform->options().clientPlatform, sm.platform->options().clientVersion, &why);
		if (!body.empty())
		{
			sm.platform->sendResult({ rec.id, body });
			r["reported"] = true;
		}
	}
	r["not_reported"] = String::utf8(why.c_str());
	if (sm.shell)
	{
		if (AptSkirmish *screen = dynamic_cast<AptSkirmish *>(sm.shell->findScreenByFilename("Skirmish.apt")))
			screen->recordsChanged();
		if (AptOnlineShell *screen = dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt")))
		{
			if (screen->hub())
			{
				screen->hub()->recordsChanged();
			}
		}
	}
	return r;
}

Dictionary AptMenuPlayer::platform_status()
{
	Dictionary r;
	r["open"] = false;
	if (!m->shell)
	{
		return r;
	}
	ShellMode &sm = *m->shell;
	auto strings = [](const std::vector<std::string> &v) {
		Array a;
		for (const std::string &x : v)
		{
			a.push_back(String::utf8(x.c_str()));
		}
		return a;
	};
	r["records"] = (int64_t)sm.records.size();
	r["records_problem"] = String::utf8(sm.hubCtx.recordsProblem.c_str());
	if (sm.platform)
	{
		const PlatformApiClient &p = *sm.platform;
		r["qm_state"] = PlatformApiClient::qmStateName(p.qmState());
		r["qm_problem"] = String::utf8(p.qmProblem().c_str());
		r["pending_results"] = (int64_t)p.pendingResults();
		r["results_sent"] = (int64_t)p.sentCount("result");
		r["health"] = (int64_t)p.healthKnown();
	}
	AptOnlineShell *screen = sm.shell ? dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt")) : nullptr;
	AptOnlineHub *hub = screen ? screen->hub() : nullptr;
	if (!hub)
	{
		return r;
	}
	r["open"] = true;
	r["quick_match"] = hub->quickMatchUp();
	r["status"] = String::utf8(hub->status().c_str());
	r["room"] = toGodot(hub->roomPhase());
	r["match_id"] = toGodot(hub->matchId());
	r["tournament_id"] = toGodot(hub->tournamentId());
	r["room_host"] = hub->roomHost();
	r["choices"] = toGodot(hub->choices());
	r["tournaments"] = strings(hub->tournamentRows());
	r["details"] = strings(hub->detailRows());
	r["selected"] = (int64_t)hub->selectedTournament();
	if (AptStats *st = hub->stats())
	{
		r["stats_tab"] = (int64_t)st->tab();
		r["stats_focus"] = (int64_t)st->focus();
		r["stats_rows"] = strings(st->rowLines());
		r["stats_filter"] = toGodot(st->filterMode() + "|" + st->filterPeriod());
	}
	return r;
}

Dictionary AptMenuPlayer::platform_apply(const Dictionary &spec)
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
	AptOnlineShell *screen = dynamic_cast<AptOnlineShell *>(sm.shell->findScreenByFilename("OnlineShell.apt"));
	AptOnlineHub *hub = screen ? screen->hub() : nullptr;
	if (!hub)
	{
		errors.push_back("OnlineShell.apt is not up");
		return out;
	}
	// the page's combos chosen as a player chooses them (the selection, then the owner's message): size 0 / 1, side and colour by list position
	auto choose = [&](const char *key, GameWindow *w, bool image) {
		if (!spec.has(key))
		{
			return;
		}
		if (!w)
		{
			errors.push_back(String("no gadget for ") + key);
			return;
		}
		const int n = (int)(int64_t)spec[key];
		if (image)
		{
			GadgetImageComboBoxSetSelectedPos(w, n);
		}
		else
		{
			GadgetComboBoxSetSelectedPos(w, n, false);
		}
	};
	choose("qm_size", hub->sizeCombo(), false);
	choose("qm_side", hub->sideCombo(), false);
	choose("qm_color", hub->colorCombo(), true);
	if (spec.has("tournament_select"))
	{
		hub->chooseTournament((int)(int64_t)spec["tournament_select"]);
	}
	if (spec.has("tournament_action"))
	{
		hub->tournamentAction((int)(int64_t)spec["tournament_action"]);
	}
	if (spec.has("stats_filter"))
	{
		const Array f = spec["stats_filter"];
		if (hub->stats() && f.size() == 2)
		{
			hub->stats()->setFilter((int)(int64_t)f[0], (int)(int64_t)f[1]);
		}
		else
		{
			errors.push_back("no STATS page, or stats_filter is not [mode, period]");
		}
	}
	out["ok"] = errors.is_empty();
	out["errors"] = errors;
	return out;
}

} // namespace godot
