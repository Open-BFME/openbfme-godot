// OpenBFME. GPL-3.0.
//
// The BFME WindowManager (spec menus-apt.md 3.1/3.3, build step A4): the engine side of the Apt player.  It owns the Apt
// player, the twelve `_level` slots, the five name-keyed registries and the host interface the movies call into.
//
// Target facts (RotWK 2.01 game.dat, S-001 caveat; names read from the binary strings): 22 AptMainMenu::* names, 16 AptSkirmish::*
// names and the global names listed in registerBuiltinCallbacks() exist; the extern provider handlers InGame / InBetaDemo /
// InDreamMachineDemo / DoTrace are at 0x0081273C / 0x00812787 / 0x008127A6 (the facts are pinned in AptPlayerTestUtil.h).
// Donor facts (Open-BFME-1 decompile, GameEngine/Source/GameClient/GUI; BFME1 and BFME2 share the structure, bodies differ):
//   WindowManager.hpp layout        loadAptWindow (WindowManager_loadAptWindow.cpp:100-173): file -> slot map, 12 records of
//                                   {directory, file, parameter, index, flags}; a file already mapped answers -1, a requested slot
//                                   >= 12 answers 0, an occupied requested slot answers -1; the .big of the movie is mounted
//                                   (here the archives are mounted already, the Apt loader asks the file source by name)
//   update (WindowManager_update.cpp, retail 0x0046E850)
//                                   pending pop -> TheShell->pop(); dirty flag -> every record with flag bit 0 is loaded (0x00467F20);
//                                   focus refresh (0x0046E170: "OnFocus" "1"/"0" to each loaded window whose focus bit changed);
//                                   elapsed = min(now - last, 60) (and >= 34 when a flag at +0x1C4 is set); AptUpdate(elapsed);
//                                   the hover path -> tooltip callback map (+0x80) or the game text "TOOLTIP:<name>"
//   invokeCallback (WindowManager_invokeCallback.cpp)      command map (+0x08): exact name, functor invoked with the argument
//   invokeCallbackWithArg (..._invokeCallbackWithArg.cpp)  provider map (+0x1C): exact name, retry without the `_levelN/` prefix
//                                   (BfmeSkipLevelPrefix.cpp), functor invoked with (argument, context, flag)
//   registerAptCallback (WindowManager_registerAptCallback.cpp)  component map (+0x30): the first registration of a name is kept
//   bindShown / bindShownWithArg                          the first registration of a name is kept
//   _bfme_closeAptScreen (AptScreenClose.cpp)             erases the name from the (global) screen-reference table and clears the
//                                   window record names equal to it
//   bfme_setAptText / bfme_bindAptText (WindowManager_setAptText.cpp)  name -> {listener, text} records
// Inference (registered as stops S-170..S-179 in docs/STOPS.md): see the comments marked [S-17x] below.

#pragma once

#include "Libraries/Source/Apt/Apt.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

class GameWindow;
class AptGadgetLayer;
class Shell;
class ShellServices;

// A note about something the manager could not do the retail way or has not ported (a stop, an unhandled command ...).
struct WindowManagerNote
{
	std::string kind;
	std::string detail;
};

// What a component factory (the +0x30 map: gadget types, View3D, BinkMovie, render callbacks) is given when a clip instance of
// its symbol was created.  Bounds are the placeholder's axis-aligned box in stage coordinates.
struct AptComponentRequest
{
	AptComponentRequest(class WindowManager &m, AptCharacterInst &i) : manager(m), instance(i) {}
	class WindowManager &manager;
	AptCharacterInst &instance;
	int level = -1;               // the `_level` slot the instance lives in, -1 when it is not in a window of the manager
	std::string movie;            // the movie that exports the symbol (GameWindowGadgets)
	std::string symbol;           // the exported symbol, e.g. "ComboBox"
	std::string instancePath;     // AptCharacterInst::targetPath(), "_level1.OpenPlay.instance1.PlayerList.~0.Player"
	std::string instanceName;     // the name InitGadgets receives [S-170]
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	// The clip variables the placeholder's own script set before the factory ran [S-172]: `_type` (gadget type), `_Load` (skin, relative to
	// window\), `_Init` (the screen reference called with the gadget), `_Draw` (a draw flag).  Empty when the script did not set them.
	std::string type, load, init;
	bool draw = false;
	bool hasType = false, hasLoad = false, hasInit = false, hasDraw = false;
};

// The five name-keyed registries of the WindowManager plus the text records.  Names are exact (case-sensitive) strings, as the
// AsciiString hash maps of the decompile.
class WindowManager final : public AptHost
{
public:
	static constexpr int kAptWindowCount = 12;
	static constexpr int kFocusNone = 12; // the entry the focus vector starts with (WindowManagerInit.cpp: push_back(12))

	enum AptWindowFlag : std::uint8_t
	{
		APTWIN_LOAD_PENDING = 1, // loadAptWindow(unknown1): the movie loads on the next update
		APTWIN_LOADED = 2,       // the movie is in its level (0x0046E170 tests it before sending OnFocus)
		APTWIN_FLAG4 = 4,        // cleared for AptLevel0 by init(); meaning not read
		APTWIN_FOCUS = 8,        // the window has focus (0x0046E170)
		APTWIN_HIDDEN = 16       // [S-171] set by hideAptWindow: the level instance is invisible and ignores input
	};

	struct AptWindowRecord
	{
		std::string directory; // "Apt\\"
		std::string file;      // "MainMenu.apt"
		int parameter = 0;
		int index = -1;        // -1 = free
		std::uint8_t flags = 0;
	};

	// name -> functor registries
	using CommandFn = std::function<void(const std::string &argument)>;
	// An extern provider: `value` carries the written string for a set and receives the answer for a get; false for a get means the
	// provider has no value (the script reads undefined), true means `value` is the answer.
	using ProviderFn = std::function<bool(const std::string &name, std::string &value, bool setting)>;
	// Creates the native object for a placeholder instance (nullptr: nothing to keep, e.g. View3D until a device implements it).
	using ComponentFn = std::function<std::shared_ptr<GameWindow>(AptComponentRequest &request)>;
	// `<Prefix>::InitGadgets`: called once per created gadget (menus-apt.md 3.3 "Gadget init").
	using ScreenRefFn = std::function<void(const std::string &instanceName, GameWindow *window)>;
	// Hover tooltip callback of a hovered APT path ("Skirmish/tooltipPlayerLevelIcon"); returns the game-text label to show.
	using TooltipFn = std::function<std::string(const std::string &name)>;
	// Listener of a text record (bfme_bindAptText).
	using TextListener = std::function<void(const std::string &text)>;

	WindowManager(AptFileSource &source, ShellServices &services);
	~WindowManager() override;

	WindowManager(const WindowManager &) = delete;
	WindowManager &operator=(const WindowManager &) = delete;

	// WindowManager::init (WindowManagerInit.cpp): the global callbacks, AptLevel0 into its slot, the focus vector [12].
	void init();
	// WindowManager::update with the elapsed time already measured (the caller owns the clock).
	void update(int elapsedMs);

	Apt &apt() { return *m_apt; }
	ShellServices &services() { return m_services; }
	void setShell(Shell *shell) { m_shell = shell; }
	Shell *shell() const { return m_shell; }

	// ---- the twelve level slots -------------------------------------------------------------------------------
	// loadAptWindow(directory, file, loadNow, parameter, requestedSlot): see the header comment for the return values.
	int loadAptWindow(const std::string &directory, const std::string &file, bool loadNow, int parameter, int requestedSlot);
	const AptWindowRecord &aptWindow(int index) const { return m_windows[(std::size_t)index]; }
	int findAptMovieIndex(const std::string &file) const;
	bool isAptWindowLoaded(int index) const;
	// Hides the window (0x00467F20's counterpart WindowManager_hideAptWindow.cpp: false when the slot is not loaded).
	bool hideAptWindow(int index);
	bool showAptWindow(int index);
	// Closes a window: the movie leaves its level, the slot is free again.  [S-171] the retail close path was not traced.
	bool unloadAptWindow(int index);
	// The `_level` an instance lives in, -1 if none.
	int levelOf(const AptCharacterInst &inst) const;

	// A screen asks the next update to pop the Shell (the flag at +0x1AC, WindowManager::update).
	void requestShellPop() { m_pendingShellPop = true; }
	// Per-update listeners, owned by a screen (retail's screen update, e.g. RotWK AptSkirmish 0x928CDB): run after every update() once the movies ran,
	// until the owner removes them.  A screen removes its listeners when it is destroyed (AptScreen::~AptScreen), so none can outlive it.
	void addUpdateListener(const void *owner, std::function<void()> listener) { m_updateListeners.emplace_back(owner, std::move(listener)); }
	void removeUpdateListeners(const void *owner);
	std::size_t updateListenerCount() const { return m_updateListeners.size(); }
	// The stack of focus candidates; the last entry has focus (kFocusNone = no window of the manager).
	void pushFocus(int level) { m_focus.push_back(level); m_focusDirty = true; }
	void popFocus();
	int focusedLevel() const { return m_focus.empty() ? kFocusNone : m_focus.back(); }

	// ---- registries -------------------------------------------------------------------------------------------
	// Each returns false (and keeps the existing entry) when the name is already registered; a null functor is ignored (false).
	bool registerCommand(const std::string &name, CommandFn fn);
	bool registerProvider(const std::string &name, ProviderFn fn);
	bool registerComponent(const std::string &name, ComponentFn fn);
	bool registerScreenRef(const std::string &name, ScreenRefFn fn);
	bool registerTooltip(const std::string &name, TooltipFn fn);
	bool hasCommand(const std::string &name) const { return m_commands.count(name) != 0; }
	bool hasProvider(const std::string &name) const { return m_providers.count(name) != 0; }
	bool hasComponent(const std::string &name) const { return m_components.count(name) != 0; }
	bool hasScreenRef(const std::string &name) const
	{
		for (const auto &e : m_screenRefs)
		{
			if (e.first == name)
			{
				return true;
			}
		}
		return false;
	}
	bool hasTooltip(const std::string &name) const { return m_tooltips.count(name) != 0; }
	bool unregisterCommand(const std::string &name) { return m_commands.erase(name) != 0; }
	bool unregisterProvider(const std::string &name)
	{
		m_numericProviders.erase(name);
		return m_providers.erase(name) != 0;
	}
	// lane END-1 (INFERENCE, stop S-1063): the provider's decimal integer answers reach the movie as numbers, not strings. RotWK's flag providers answer
	// "0" / "1" (AptTimeLine RW 0x925026) and TimeLine.apt (Apt version 7, where any non-empty string is true) branches on them with Not; only a
	// numeric 0 gives the skirmish score screen retail shows. Only the providers a screen marks are affected.
	void markNumericProvider(const std::string &name) { m_numericProviders.insert(name); }
	bool unregisterComponent(const std::string &name) { return m_components.erase(name) != 0; }
	bool unregisterTooltip(const std::string &name) { return m_tooltips.erase(name) != 0; }
	// _bfme_closeAptScreen: erases the screen-reference `name` and clears the window records named like it.
	void closeAptScreen(const std::string &name);
	std::size_t commandCount() const { return m_commands.size(); }
	std::size_t providerCount() const { return m_providers.size(); }
	std::size_t componentCount() const { return m_components.size(); }
	std::size_t screenRefCount() const { return m_screenRefs.size(); }
	std::vector<std::string> commandNames() const;
	std::vector<std::string> providerNames() const;
	std::vector<std::string> componentNames() const;
	std::vector<std::string> screenRefNames() const;

	// ---- movie -> engine --------------------------------------------------------------------------------------
	// WindowManager::invokeCallback: a registered name runs its functor with the argument and returns true; an unknown name is
	// logged (note "command-unhandled") and returns false, it never throws (retail's FunctorNotSet is for an empty slot only, and
	// an empty slot cannot be registered).  Retail behaviour for a missing name is UNVERIFIED (spec 3.3, open question 3).
	bool invokeCallback(const std::string &name, const std::string &argument);

	// ---- engine -> movie --------------------------------------------------------------------------------------
	// Calls the AS function `function` on `/_level<level>` with string arguments (BfmeLevelPathAN.cpp, Rva00893410InvokeStrings.cpp).
	bool invokeAS(int level, const std::string &function, const std::vector<std::string> &args, std::string *result = nullptr, std::string *error = nullptr);
	// Same, with a clip path below the level root ("lobby/StartGame" style paths use '.' in the Apt resolver).
	bool invokeASAt(int level, const std::string &path, const std::string &function, const std::vector<std::string> &args, std::string *result = nullptr, std::string *error = nullptr);
	// lane UI-1: typed arguments (RW 0x62282A passes AptValues: strings, booleans)
	bool invokeASAtValues(int level, const std::string &path, const std::string &function, const std::vector<AptValue> &args, std::string *error = nullptr);
	// Sets a member of the root clip of `level` (the engine-to-movie variable of a screen, e.g. Skirmish.apt's `_root.vShowAddProfile`).
	bool setAptRootMember(int level, const std::string &name, const AptValue &value, std::string *error = nullptr);

	// ---- text -------------------------------------------------------------------------------------------------
	// bfme_setAptText: the record's text is replaced and its listener (when bound) is told.
	void setAptText(const std::string &name, const std::string &text);
	// bfme_bindAptText: binds (or with an empty listener unbinds) a listener; a record with no text takes `defaultText`.
	void bindAptText(const std::string &name, const std::string &defaultText, TextListener listener);
	const std::string *aptText(const std::string &name) const;
	// lane UI-1: the text a field bound to `name` shows: a record bfme_setAptText wrote (even empty: a listener bound before is given the empty text), else a
	// non-empty one; false: the field falls back to the label's game text (bfme_bindAptText's default for an empty record)
	bool aptTextShown(const std::string &name, std::string &out) const;
	// lane END-2: the image records a RenderImage clip's `_imageMap` names (RW 0x6236F6 / 0x6236DE: name -> MappedImage, e.g. TimeLine:PlayerFactionIcon:0 =
	// "AptIconMen", RW 0x92632E); the device draws the mapped image into the clip. An empty image removes the record.
	void setAptImage(const std::string &name, const std::string &mappedImage);
	const std::string *aptImage(const std::string &name) const;
	// lane UI-2: the picture an engine render callback draws into the clip the movie tags with the callback's name (`_type`): RotWK's
	// AptMapPreview::Picture (RW 0x9757C0) draws its Image (RW 0x975F23: a 128 x 128 image of the texture file `<map>_pic.tga`, UV 0..1; else
	// the mapped image MissingMap, RW 0x97640C) over the clip's rectangle in opaque white (RW 0x44CF58). Exactly one of the two names is set;
	// clearRenderPicture removes it (the callback then draws nothing, as retail's with no image).
	struct RenderPicture
	{
		std::string mappedImage; // a MappedImage name
		std::string file;        // a texture file path in the mounted archives (an image the engine made from a file)
		// the file image's UV in the port's top-row-first textures: RotWK's texture rows are bottom-up (its Targa loader Y-flips a top-origin file,
		// RW 0xA28575), so a retail V is 1 - V here; retail's UV (0,0)-(1,1) of RW 0x975F23 is (0,1)-(1,0)
		float uvLo[2] = { 0.0f, 1.0f };
		float uvHi[2] = { 1.0f, 0.0f };
	};
	void setRenderPicture(const std::string &renderName, const RenderPicture &picture) { m_renderPictures[renderName] = picture; }
	void clearRenderPicture(const std::string &renderName) { m_renderPictures.erase(renderName); }
	const RenderPicture *renderPicture(const std::string &renderName) const
	{
		auto it = m_renderPictures.find(renderName);
		return it == m_renderPictures.end() ? nullptr : &it->second;
	}

	// ---- components -------------------------------------------------------------------------------------------
	// The windows created for the component instances that are alive, in creation order.
	struct ComponentRecord
	{
		const AptCharacterInst *instance = nullptr;
		int level = -1;
		std::string instancePath;
		std::string instanceName;
		std::string symbol;
		std::string movie;
		std::string init;                   // the `_Init` screen reference that was called
		std::shared_ptr<GameWindow> window; // may be null (a factory that creates no window)
	};
	const std::vector<ComponentRecord> &components() const { return m_componentRecords; }
	GameWindow *componentWindow(const std::string &instancePath) const;
	// The name InitGadgets gets for an instance [S-170].
	static std::string gadgetInstanceName(const AptCharacterInst &inst);

	// The axis-aligned box of an instance in stage coordinates (x0 y0 x1 y1); false when it has no content.
	static bool stageBounds(const AptCharacterInst &inst, float out[4]);
	// Drops the native windows of the component records (the gadget layer is going away); the records stay.
	void releaseComponentWindows();

	// ---- native gadget layer and input routing -----------------------------------------------------------------------
	void setGadgetLayer(AptGadgetLayer *layer) { m_gadgetLayer = layer; }
	AptGadgetLayer *gadgetLayer() const { return m_gadgetLayer; }
	// Input entry points of the shell.  The native gadgets see a mouse event and the Apt movie sees it too: ZH's WindowTranslator marks every
	// mouse event used while the shell is active (it is never meant for the game behind), and the Apt input is a separate queue of the
	// Apt player (spec 3.2: BFME1 registers OnClickThroughPress/Release for the clicks that miss APT).  [S-177] inference: both layers
	// get the event.  Returns true when a gadget used it.
	bool postMouseMove(float x, float y);
	bool postMouseButton(bool down);
	bool postMouseWheel(int delta);
	// A key for the Apt movie (Apt key code) and, separately, for the gadgets (DirectInput code + state, ZH KeyDefs.h).
	void postAptKey(int aptKeyCode, bool down);
	bool postGadgetKey(int dikCode, bool down, int modifiers = 0);
	bool postTextInput(char16_t ch);

	// ---- tooltips ---------------------------------------------------------------------------------------------
	// The APT hover path the tooltip logic works on: "<file without .apt>/<clip path below the root>" of the hovered clip, "" if
	// the pointer is over nothing (WindowManager::update 0x0046E850, with the path source inferred [S-173]).
	std::string hoverPath() const;
	const std::string &currentTooltipLabel() const { return m_tooltipLabel; }

	// ---- diagnostics ------------------------------------------------------------------------------------------
	const std::vector<WindowManagerNote> &notes() const { return m_notes; }
	std::size_t noteCount(const std::string &kind) const;
	void note(const std::string &kind, const std::string &detail) { m_notes.push_back({ kind, detail }); }
	// Movie load failures and script errors the Apt player reported (never swallowed).
	const std::vector<std::string> &errors() const { return m_errors; }
	void clearErrors() { m_errors.clear(); }

	// The frame-time clamp flag at +0x1C4 of retail's WindowManager (min 34 ms); its meaning is not read, the port exposes it.
	void setMinimumFrameTime(bool on) { m_minFrameTime = on; }

	// ---- AptHost ----------------------------------------------------------------------------------------------
	bool isComponentSymbol(const std::string &movieName, const std::string &symbolName) override;
	void componentInstanceCreated(AptCharacterInst &inst, const std::string &movieName, const std::string &symbolName) override;
	void componentInstanceDestroyed(AptCharacterInst &inst) override;
	void trace(const std::string &message) override;
	void fscommand(const std::string &command, const std::string &argument) override;
	void loadMovie(const std::string &movieName, const std::string &target) override;
	void getURL(const std::string &url, const std::string &target) override;
	AptExternResult getExtern(const std::string &name, std::string &value) override;
	bool setExtern(const std::string &name, const std::string &value) override;
	std::uint32_t random() override;
	void scriptError(const std::string &message) override;

	// Names of the movie sub-loads the movies asked for (loadMovie notifications), in order; for tests.
	struct MovieLoadNote
	{
		std::string movie;
		std::string target;
	};
	const std::vector<MovieLoadNote> &movieLoads() const { return m_movieLoads; }
	const std::vector<std::string> &traces() const { return m_traces; }
	// The client random source of ActionRandom (bfmeNext1221): a deterministic generator seeded by the owner (S-174).
	void seedRandom(std::uint32_t seed) { m_randomState = seed; }

private:
	struct TextRecord
	{
		std::string text;
		TextListener listener;
		bool set = false; // lane UI-1: bfme_setAptText wrote it (an empty text then stays empty on screen, BFME1 WindowManager_setAptText.cpp:88-104)
	};

	void registerBuiltinCallbacks();
	void loadPendingWindows();                         // 0x00467F20 for every record with flag bit 0
	bool loadAptWindowInto(int index);
	void refreshFocus();                               // 0x0046E170
	void updateTooltip();                              // tail of 0x0046E850
	static std::string movieNameOf(const std::string &file);
	static std::string skipLevelPrefix(const std::string &path); // BfmeSkipLevelPrefix.cpp
	AptSpriteInst *levelRoot(int index);

	AptFileSource &m_source;
	ShellServices &m_services;
	Shell *m_shell = nullptr;
	std::unique_ptr<Apt> m_apt;

	AptWindowRecord m_windows[kAptWindowCount];
	std::map<std::string, int> m_fileToWindow;
	std::vector<int> m_focus;
	bool m_windowsDirty = false;   // +0x1AD
	bool m_focusDirty = false;     // +0x1A4
	bool m_pendingShellPop = false; // +0x1AC
	std::vector<std::pair<const void *, std::function<void()>>> m_updateListeners;
	bool m_minFrameTime = false;   // +0x1C4
	bool m_inAptUpdate = false;    // +0x1AE

	std::map<std::string, CommandFn> m_commands;
	std::map<std::string, ProviderFn> m_providers;
	std::set<std::string> m_numericProviders; // lane END-1
	std::map<std::string, ComponentFn> m_components;
	std::vector<std::pair<std::string, ScreenRefFn>> m_screenRefs; // registration order [S-172]
	std::map<std::string, TooltipFn> m_tooltips;
	std::map<std::string, TextRecord> m_texts;
	std::map<std::string, std::string> m_images; // lane END-2
	std::map<std::string, RenderPicture> m_renderPictures; // lane UI-2

	AptGadgetLayer *m_gadgetLayer = nullptr;
	std::vector<ComponentRecord> m_componentRecords;
	std::vector<WindowManagerNote> m_notes;
	std::vector<std::string> m_errors;
	std::vector<std::string> m_traces;
	std::vector<MovieLoadNote> m_movieLoads;
	std::string m_tooltipLabel;
	std::string m_tooltipPath;
	std::uint32_t m_randomState = 1u;
};
