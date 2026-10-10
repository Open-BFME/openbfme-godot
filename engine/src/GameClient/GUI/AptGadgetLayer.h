// OpenBFME. GPL-3.0.
//
// The native gadget layer of the APT screens (spec menus-apt.md 3.1 "GameWindow + Gadget*", 3.3 "Component factories", build step A5):
// the GameWindowManager that holds the gadget windows, the component factories registered under the GameWindowGadgets symbol names, and the
// glue to the WindowManager (placement from the placeholder clips, visibility, input routing, draw commands).
//
// Target facts (RotWK game.dat): the component map holds `GameWindow`, `HorzSlider`, `ComboBox`, `ImageComboBox`, `CheckBox`, `TextEntry`,
// `ListBox`, `PushButton`, `BinkMovie` (one shared handler for nine of them, 0x0081491E..) and `View3D` (own handler); the skins are the
// `window\apt\*.wnd` files (spec 1.11); `apt/combobox.wnd` and `apt/horzslider.wnd` are seeded in a map at start (BFME1
// WindowManagerRegisterAptCallbacks00464080.cpp:236-237).  Donor (BFME1 decompile): a clip instance of such a symbol creates a native window
// the size of the placeholder, then `<Prefix>::InitGadgets` runs.
// Inference [S-172, S-177]: the gadget owner is a pseudo window per screen (the screen is a GameWindow in BFME1), input reaches the gadgets
// before the Apt movie (ZH: TheShell->isShellActive() marks window input used), the skin of a symbol is `window/apt/<symbol lower case>.wnd`.

#pragma once

#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/GameWindowManagerScript.h"
#include "GameClient/GUI/HeaderTemplate.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/WindowManager.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ArchiveFileSystem;

// Mapped images and header templates the skins need (retail: data\ini\mappedimages\**, HeaderTemplate.ini of the language archive).
struct GadgetSkinData
{
	MappedImageCollection images;
	HeaderTemplateManager headers;
};

// Loads GadgetSkinData from the mounted archives.  The file locations are retail's: the MappedImage tree and the language archive's
// headertemplate.ini; a missing file or a parse error is an error that reaches the caller.
bool loadGadgetSkinData(ArchiveFileSystem &fs, GadgetSkinData &out, std::string *error);

class AptColorPickers;

class AptGadgetLayer
{
public:
	// `skins` and `source` must outlive the layer.  `lexicon` is the FunctionLexicon the WND callback names resolve through (retail's
	// "Apt:None" names resolve to nothing).
	AptGadgetLayer(WindowManager &windows, AptFileSource &source, GadgetSkinData &skins);
	~AptGadgetLayer();

	AptGadgetLayer(const AptGadgetLayer &) = delete;
	AptGadgetLayer &operator=(const AptGadgetLayer &) = delete;

	GameWindowManager &gadgets() { return m_gwm; }
	AptColorPickers *colorPickers() { return m_pickers.get(); } // lane CAH-2

	// Registers the component factories for the nine gadget symbol names of the binary (View3D, BinkMovie are device components: noted).
	void registerComponents();
	// The symbol names registered (binary order).
	static const std::vector<std::string> &componentSymbols();
	// The skin file of a symbol.
	static std::string skinPath(const std::string &symbol);

	// ---- owners ---------------------------------------------------------------------------------------------------------------------------
	// The window every gadget created in `level` reports its messages to (ZH owner = the screen).
	void setLevelOwner(int level, GameWindow *owner);
	void clearLevelOwner(int level);
	// A hidden pseudo window whose system function is `system`; destroyed with the layer or by destroyOwnerWindow.
	GameWindow *createOwnerWindow(GameWinSystemFunc system, const std::string &name);
	void destroyOwnerWindow(GameWindow *window);
	// lane UI-2: a gadget the engine places itself (no movie placeholder: the Options screen's OpenBFME soft-particles box): the skin `path`
	// (relative to the archives, e.g. "window/apt/checkbox.wnd") at the stage rectangle, reporting to `owner`. Null and `*error` when the skin fails.
	GameWindow *createEngineGadget(const std::string &path, int x, int y, int w, int h, GameWindow *owner, std::string *error);
	void destroyEngineGadget(GameWindow *window);

	// ---- per update (WindowManager::update calls these) ----------------------------------------------------------------------------------
	// Follows the placeholder clips: position, size, visibility; advances the gadget clock.
	void update(int elapsedMs);
	// The draw commands of every visible gadget in this frame.
	void buildDrawList(GadgetDrawList &out);

	// ---- input (stage coordinates) --------------------------------------------------------------------------------------------------------
	// Each returns true when a gadget took the event (the Apt movie must not see it).
	bool mouseMove(float x, float y);
	bool mouseButton(bool down);
	bool mouseWheel(int delta);
	bool key(int dikCode, bool down, int modifiers = 0);
	bool textInput(char16_t ch);

	// The gadget created for an Apt instance path (tests, screens).
	GameWindow *windowOf(const std::string &instancePath) const { return m_windows.componentWindow(instancePath); }
	// Everything that went wrong creating gadgets (a skin that did not parse, an unknown type ...): never silent.
	const std::vector<std::string> &errors() const { return m_errors; }
	const std::vector<std::string> &notes() const { return m_notes; }

private:
	struct Placed
	{
		const AptCharacterInst *instance = nullptr;
		float lastBounds[4] = { 0, 0, 0, 0 };
		bool shown = true;
	};
	std::shared_ptr<GameWindow> createGadget(AptComponentRequest &request);

	WindowManager &m_windows;
	AptFileSource &m_source;
	GadgetSkinData &m_skins;
	std::unique_ptr<AptColorPickers> m_pickers; // lane CAH-2: the "ColorPicker" render components (GUI/AptColorPicker.h)
	GameWindowManager m_gwm;
	WindowFunctionLexicon m_lexicon;
	std::map<int, GameWindow *> m_levelOwners;
	std::vector<GameWindow *> m_ownerWindows;
	std::vector<std::string> m_errors;
	std::vector<std::string> m_notes;
	std::map<const GameWindow *, Placed> m_placed;
	std::uint32_t m_timeMs = 0;
	float m_mouseX = 0, m_mouseY = 0;
	bool m_leftDown = false;
	bool m_registered = false;
};
