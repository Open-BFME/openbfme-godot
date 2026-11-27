// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Shared AptMapPreview helper (RotWK RW 0x976240 .. 0x9779AA, RW 0x7052BE): owns the movie registrations
// (AptMapPreview::MapGadgetInit, AptMapPreview::GameMapType, AptMapPreview::Picture), resolves CurrentMap
// textures (RW 0x702689) and Picture textures (RW 0x97640C -> 0x975F23), lays out ButtonMapStartPosition*
// (RW 0x976F59), and sets start spot numbers (RW 0x7052BE) for both lobby screens and AptLoadScreen.

#pragma once

#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/GUI/WindowManager.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

class GameTextSource;
class GameWindow;
struct MapCacheEntry;

class AptMapPreview
{
public:
	void attach(WindowManager &windows, const WindowManager::ScreenRefFn &gadgetInitFn,
		const WindowManager::ProviderFn &gameMapTypeFn, const WindowManager::ComponentFn &pictureComponentFn);
	void detach(WindowManager &windows);
	bool isAttached() const { return m_attached; }

	// Handles MapInfo, MapDescription, Picture, CurrentMap (RW 0x9777AB). Returns true if handled.
	bool initGadget(const std::string &name, GameWindow *window);
	void pruneDeadGadgets(const std::set<const GameWindow *> &alive);

	void setPicture(WindowManager &windows, const WindowManager::RenderPicture &picture);
	void updatePicture(WindowManager &windows, const SkirmishSetupSource *setup, const MapCacheEntry *map, bool noteIfNoFs = false);

	static const MapCacheEntry *findMap(const SkirmishSetupSource &setup, const std::string &mapName);
	static std::string resolveCurrentMapImage(WindowManager &windows, GameWindow *currentMap, const SkirmishSetupSource &setup, const MapCacheEntry &map);

	static void populateCurrentMapWindow(WindowManager &windows, GameTextSource *text, GameWindow *window,
		std::vector<GameWindow *> &children, const MapCacheEntry *map, const std::string &image,
		bool startSpotsEnabled, bool showMultiplayerSpots, std::string &errorOut);

	static void updateStartSpots(GameTextSource *text, const MapCacheEntry *map, GameWindow *const *buttons, int buttonCount,
		bool onLoadScreen, const std::function<int(int slot, bool &nonObserver)> &slotPosFn,
		const std::function<void(int slot)> &onOutOfRange = {});

	GameWindow *mapInfo() const { return m_mapInfo; }
	GameWindow *mapDescription() const { return m_mapDescription; }
	GameWindow *mapPicture() const { return m_mapPicture; }
	GameWindow *currentMap() const { return m_currentMap; }
	const std::vector<GameWindow *> &currentMapChildren() const { return m_currentMapChildren; }
	std::vector<GameWindow *> &mutableCurrentMapChildren() { return m_currentMapChildren; }

private:
	bool m_attached = false;
	bool m_hasPicture = false;
	WindowManager::RenderPicture m_picture;
	GameWindow *m_mapInfo = nullptr;
	GameWindow *m_mapDescription = nullptr;
	GameWindow *m_mapPicture = nullptr;
	GameWindow *m_currentMap = nullptr;
	std::vector<GameWindow *> m_currentMapChildren;
};
