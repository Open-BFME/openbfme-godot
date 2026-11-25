// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.

#include "GameClient/GUI/AptScreens/AptMapPreview.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string normalizeMapKey(std::string s)
{
	for (char &c : s)
	{
		c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string asciiLower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

void registerIfFree(WindowManager &windows, const std::string &what, bool ok)
{
	if (!ok)
	{
		windows.note("registration-refused", what);
	}
}
} // namespace

void AptMapPreview::attach(WindowManager &windows, const WindowManager::ScreenRefFn &gadgetInitFn,
	const WindowManager::ProviderFn &gameMapTypeFn, const WindowManager::ComponentFn &pictureComponentFn)
{
	if (!m_attached)
	{
		registerIfFree(windows, "screenRef:AptMapPreview::MapGadgetInit",
			windows.registerScreenRef("AptMapPreview::MapGadgetInit", gadgetInitFn));
		registerIfFree(windows, "provider:AptMapPreview::GameMapType",
			windows.registerProvider("AptMapPreview::GameMapType", gameMapTypeFn));
		registerIfFree(windows, "component:AptMapPreview::Picture",
			windows.registerComponent("AptMapPreview::Picture", pictureComponentFn));
		m_attached = true;
	}
	if (m_hasPicture)
	{
		windows.setRenderPicture("AptMapPreview::Picture", m_picture);
	}
}

void AptMapPreview::detach(WindowManager &windows)
{
	if (!m_attached)
	{
		return;
	}
	windows.closeAptScreen("AptMapPreview::MapGadgetInit");
	windows.unregisterProvider("AptMapPreview::GameMapType");
	windows.unregisterComponent("AptMapPreview::Picture");
	windows.clearRenderPicture("AptMapPreview::Picture");
	m_attached = false;
}

bool AptMapPreview::initGadget(const std::string &name, GameWindow *window)
{
	if (!window)
	{
		return false;
	}
	if (name == "MapInfo")
	{
		m_mapInfo = window;
		window->winEnable(true);
		GadgetListBoxSetAudioFeedback(window, true);
		return true;
	}
	if (name == "MapDescription")
	{
		m_mapDescription = window;
		window->winEnable(true);
		GadgetListBoxSetAudioFeedback(window, true);
		return true;
	}
	if (name == "MapPicture") // RW 0x00C85034
	{
		m_mapPicture = window;
		return true;
	}
	if (name == "CurrentMap") // RW 0x00C85028
	{
		if (m_currentMap != window)
		{
			m_currentMap = window;
			m_currentMapChildren.clear();
			for (GameWindow *c = window->winGetChild(); c && (int)m_currentMapChildren.size() < LEGACY_MAX_SLOTS; c = c->winGetNext())
			{
				m_currentMapChildren.push_back(c);
				c->winHide(true);
				c->winSetStatus(WIN_STATUS_RIGHT_CLICK); // RW 0x9778B0: status | 0x20000
			}
		}
		return true;
	}
	return false;
}

void AptMapPreview::pruneDeadGadgets(const std::set<const GameWindow *> &alive)
{
	auto keep = [&](GameWindow *&w) {
		if (w && alive.count(w) == 0)
		{
			w = nullptr;
		}
	};
	keep(m_mapInfo);
	keep(m_mapDescription);
	keep(m_mapPicture);
	keep(m_currentMap);
	m_currentMapChildren.erase(
		std::remove_if(m_currentMapChildren.begin(), m_currentMapChildren.end(),
			[&](const GameWindow *w) { return !w || alive.count(w) == 0; }),
		m_currentMapChildren.end());
}

void AptMapPreview::setPicture(WindowManager &windows, const WindowManager::RenderPicture &picture)
{
	m_picture = picture;
	m_hasPicture = true;
	if (m_attached)
	{
		windows.setRenderPicture("AptMapPreview::Picture", m_picture);
	}
}

void AptMapPreview::updatePicture(WindowManager &windows, const SkirmishSetupSource *setup, const MapCacheEntry *map, bool noteIfNoFs)
{
	// RW 0x97640C -> 0x975F23: "<map without .map>_pic.tga" when present, else mapped image MissingMap.
	WindowManager::RenderPicture picture;
	if (!map || map->name.size() < 4)
	{
		picture.mappedImage = "MissingMap";
	}
	else
	{
		std::string file = map->name.substr(0, map->name.size() - 4) + "_pic.tga";
		std::replace(file.begin(), file.end(), '/', '\\');
		if (setup && setup->fileExists(file))
		{
			picture.file = file;
		}
		else
		{
			if (noteIfNoFs && setup && !setup->hasFileSystem())
			{
				windows.note("map-picture", "the setup source has no file system: " + file + " cannot be looked up, MissingMap is shown");
			}
			picture.mappedImage = "MissingMap";
		}
	}
	setPicture(windows, picture);
}

const MapCacheEntry *AptMapPreview::findMap(const SkirmishSetupSource &setup, const std::string &mapName)
{
	const std::string key = normalizeMapKey(mapName);
	for (const MapCacheEntry &m : setup.maps())
	{
		if (normalizeMapKey(m.name) == key)
		{
			return &m;
		}
	}
	return nullptr;
}

std::string AptMapPreview::resolveCurrentMapImage(WindowManager &windows, GameWindow *currentMap, const SkirmishSetupSource &setup, const MapCacheEntry &map)
{
	// RW 0x702689: strip .map, replace '/'/'\\' / ':' with '_', look up mapped image; else "<path>_art.tga" (UV (0,1)-(1,0)),
	// else "<path>.tga" (UV (0,0)-(1,1)), else MissingMap (RW 0x9774C6).
	if (map.name.size() < 4)
	{
		return std::string();
	}
	MappedImageCollection *images = currentMap ? currentMap->manager().mutableImages()
		: (windows.gadgetLayer() ? windows.gadgetLayer()->gadgets().mutableImages() : nullptr);
	if (!images)
	{
		return std::string();
	}
	std::string pathBase = map.name.substr(0, map.name.size() - 4);
	std::replace(pathBase.begin(), pathBase.end(), '/', '\\');
	std::string mappedCandidate = pathBase;
	for (char &c : mappedCandidate)
	{
		if (c == '\\' || c == ':')
		{
			c = '_';
		}
	}
	if (const Image *img = images->findImageByName(mappedCandidate))
	{
		return img->name;
	}
	if (!setup.hasFileSystem())
	{
		windows.note("map-preview", "the setup source has no file system: the preview of " + pathBase + " cannot be looked up");
		return std::string();
	}
	Image image;
	image.name = mappedCandidate;
	image.status = IMAGE_STATUS_RAW_TEXTURE;
	image.textureSize.x = image.textureSize.y = 128;
	// Map art needs a vertical flip after the texture loader normalizes TGA storage order.
	if (setup.fileExists(pathBase + "_art.tga"))
	{
		image.filename = pathBase + "_art.tga";
		image.uvLo[0] = 0.0f;
		image.uvLo[1] = 1.0f;
		image.uvHi[0] = 1.0f;
		image.uvHi[1] = 0.0f;
	}
	else if (setup.fileExists(pathBase + ".tga"))
	{
		image.filename = pathBase + ".tga";
		image.uvLo[0] = 0.0f;
		image.uvLo[1] = 1.0f;
		image.uvHi[0] = 1.0f;
		image.uvHi[1] = 0.0f;
	}
	else
	{
		return std::string();
	}
	images->addImage(image);
	return mappedCandidate;
}

void AptMapPreview::populateCurrentMapWindow(WindowManager &windows, GameTextSource *text, GameWindow *window,
	std::vector<GameWindow *> &children, const MapCacheEntry *map, const std::string &previewImage,
	bool startSpotsEnabled, bool showMultiplayerSpots, std::string &errorOut)
{
	if (!window)
	{
		return;
	}
	window->winSetEnabledImage(1, std::string());
	std::string image = previewImage;
	if (map && !image.empty() && window->manager().images())
	{
		const Image *made = window->manager().images()->findImageByName(image);
		const std::string art = "_art.tga";
		if (made && made->filename.size() >= art.size() && asciiLower(made->filename.substr(made->filename.size() - art.size())) == art)
		{
			window->winSetEnabledImage(1, "ScrollShroud"); // RW 0x97745A: the radar's shroud over an _art picture
		}
	}
	window->winSetUserData(const_cast<MapCacheEntry *>(map)); // W3DDrawMapPreview reads the map (RW 0x97749A)
	if (image.empty())
	{
		image = "MissingMap"; // RW 0x9774C6
	}
	if (window->manager().images() && window->manager().images()->findImageByName(image))
	{
		window->winSetStatus(WIN_STATUS_IMAGE);
		window->winSetEnabledImage(0, image);
	}
	else
	{
		window->winClearStatus(WIN_STATUS_IMAGE); // RW 0x977522
	}
	// OpenBFME extended maps reuse the retail start button's skin and input, owned by the same map window.
	if (map && map->numPlayers > LEGACY_MAX_SLOTS && !children.empty())
	{
		GameWindow *prototype = children.front();
		int w = 0, h = 0;
		prototype->winGetSize(&w, &h);
		while ((int)children.size() < MAX_SLOTS)
		{
			WinInstanceData instance = *prototype->winGetInstanceData();
			GameWindow *button = window->manager().gogoGadgetPushButton(window, prototype->winGetStatus(), 0, 0, w, h, &instance, prototype->winGetFont(), false);
			if (!button)
			{
				break;
			}
			children.push_back(button);
		}
	}
	// RW 0x976F59: the start spots. Validate the whole preview before showing any marker.
	GameWindow *buttons[MAX_SLOTS] = {};
	for (std::size_t i = 0; i < children.size() && i < MAX_SLOTS; ++i)
	{
		buttons[i] = children[i];
	}
	for (GameWindow *button : buttons)
	{
		if (button)
		{
			button->winHide(true);
		}
	}
	if (map)
	{
		std::string error;
		if (map->numPlayers > MAX_SLOTS)
		{
			error = "map preview: unsupported player capacity " + std::to_string(map->numPlayers);
		}
		for (int i = 0; error.empty() && i < map->numPlayers; ++i)
		{
			if (!map->startPositions.count(i + 1) || !buttons[i])
			{
				error = "map preview: missing start position or control " + std::to_string(i + 1);
			}
		}
		if (!error.empty())
		{
			errorOut = error;
			windows.note("map-preview-error", error);
			window->winEnable(false);
			for (GameWindow *button : buttons)
			{
				if (button)
				{
					button->winHide(true);
				}
			}
			return;
		}
	}
	int shown = 0;
	if (map)
	{
		window->winEnable(true);
		// RW 0x976F9C .. 0x976FB6: only for a multiplayer map (MapMetaData + 0x24) when not a War of the Ring strategic lobby.
		if (showMultiplayerSpots && map->isMultiplayer && map->numPlayers > 0)
		{
			for (; shown < map->numPlayers && shown < MAX_SLOTS; ++shown)
			{
				auto spot = map->startPositions.find(shown + 1); // "Player_%d_Start"
				GameWindow *button = buttons[shown];
				MapPreviewPositionStartSpot(button, window, spot->second, *map, buttons, MAX_SLOTS);
				if (button)
				{
					// RW 0x976FFE: m_enableStartSpots (+0x61) — true in setup lobbies, false on AptLoadScreen (RW 0x9762A1)
					button->winEnable(startSpotsEnabled);
					button->winHide(false);
				}
			}
		}
	}
	else
	{
		window->winEnable(false);
	}
	for (int i = shown; i < MAX_SLOTS; ++i)
	{
		if (buttons[i])
		{
			buttons[i]->winHide(true);
			GadgetButtonSetText(buttons[i], fetchOrMissing(text, "GUI:Blank")); // RW 0x977047
		}
	}
}

void AptMapPreview::updateStartSpots(GameTextSource *text, const MapCacheEntry *map, GameWindow *const *buttons, int buttonCount,
	bool onLoadScreen, const std::function<int(int slot, bool &nonObserver)> &slotPosFn,
	const std::function<void(int slot)> &onOutOfRange)
{
	// RW 0x7052BE (updateMapStartSpots(GameInfo*, GameWindow**, bool onLoadScreen))
	if (!buttons || buttonCount <= 0)
	{
		return;
	}
	if (!map)
	{
		const std::u16string blank = fetchOrMissing(text, "GUI:Blank"); // RW 0x7052F5
		for (int i = 0; i < buttonCount; ++i)
		{
			if (buttons[i])
			{
				GadgetButtonSetText(buttons[i], blank);
			}
		}
		return;
	}
	const std::u16string startTip = onLoadScreen ? std::u16string() : fetchOrMissing(text, "TOOLTIP:StartPosition");
	for (int i = 0; i < buttonCount; ++i)
	{
		if (buttons[i])
		{
			GadgetButtonSetText(buttons[i], std::u16string());
			if (!onLoadScreen)
			{
				buttons[i]->winSetTooltip(startTip); // RW 0x705336 .. 0x705395: only when !onLoadScreen
			}
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		bool nonObserver = false;
		const int pos = slotPosFn(i, nonObserver);
		if (pos >= map->numPlayers)
		{
			if (onOutOfRange)
			{
				onOutOfRange(i);
			}
			continue;
		}
		if (pos < 0 || !nonObserver || pos >= buttonCount || !buttons[pos])
		{
			continue;
		}
		// RW 0x7053E8..0x705427: format "NUMBER:%d" (slot + 1) and call GadgetButtonSetText (RW 0x729E4A)
		GadgetButtonSetText(buttons[pos], fetchOrMissing(text, "NUMBER:" + std::to_string(i + 1)));
		if (!onLoadScreen)
		{
			std::u16string tip = fetchOrMissing(text, "TOOLTIP:StartPositionN"); // RW 0x7054B0
			const std::size_t at = tip.find(u"%d");
			if (at != std::u16string::npos)
			{
				const std::string n = std::to_string(i + 1);
				tip.replace(at, 2, std::u16string(n.begin(), n.end()));
			}
			buttons[pos]->winSetTooltip(tip);
		}
	}
}
