// OpenBFME. GPL-3.0.
// See GameClient/GUI/WindowManager.h for the sources of every rule below.

#include "GameClient/GUI/WindowManager.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellServices.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{

std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

WindowManager::WindowManager(AptFileSource &source, ShellServices &services) : m_source(source), m_services(services)
{
	m_apt = std::make_unique<Apt>(source, *this);
	m_focus.push_back(kFocusNone);
}

WindowManager::~WindowManager() = default;

std::string WindowManager::movieNameOf(const std::string &file)
{
	// "MainMenu.apt" -> "MainMenu" (the movie is addressed by name; the .apt/.const/.dat files are found by the Apt loader)
	if (file.size() > 4 && lowerAscii(file.substr(file.size() - 4)) == ".apt")
	{
		return file.substr(0, file.size() - 4);
	}
	return file;
}

std::string WindowManager::skipLevelPrefix(const std::string &path)
{
	// BfmeSkipLevelPrefix.cpp (BFME1): "_levelN/rest" -> "rest"; "_levelN" alone -> ""
	if (path.compare(0, 6, "_level") != 0)
	{
		return path;
	}
	std::size_t i = 6;
	while (i < path.size() && path[i] != '/' && path[i] != '.')
	{
		++i;
	}
	if (i < path.size())
	{
		++i;
	}
	return path.substr(i);
}

// ---- the background movie (lane FB7-1) ----------------------------------------------------------------------------

namespace
{
bool strcmpiAsciiImpl(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}
} // namespace

bool WindowManager::strcmpiAscii(const std::string &a, const char *b) { return strcmpiAsciiImpl(a, b); }

bool WindowManager::loadBackground()
{
	// RW 0x6224C5
	m_backgroundLevel = loadAptWindow("Apt\\", "Background.apt", true, 0, -1);
	if (m_backgroundLevel < 0)
	{
		note("background", "Background.apt did not load");
		return false;
	}
	return true;
}

void WindowManager::setBackground(int mode)
{
	// RW 0x6230B6
	m_backgroundMode = mode;
	if (mode == 0)
	{
		hideBackground(false);
		return;
	}
	const char *function = mode == 1 ? "ShowFrontEndBackground" : mode == 2 ? "ShowInGameBackground" : nullptr;
	if (!function)
	{
		return;
	}
	m_backgroundHidden = 0;
	std::string error;
	if (!invokeAS(m_backgroundLevel, function, {}, nullptr, &error))
	{
		note("background", std::string(function) + ": " + error);
	}
}

void WindowManager::hideBackground(bool instant)
{
	// RW 0x622C88
	const int mode = m_backgroundMode;
	if (mode == 0)
	{
		if (instant && m_backgroundHidden != 0)
		{
			m_backgroundMode = m_backgroundHidden;
			m_backgroundHidden = 0;
			hideBackground(true);
		}
	}
	else if (mode == 1 || mode == 2)
	{
		std::string error;
		const char *function = mode == 1 ? "HideFrontEndBackground" : "HideInGameBackground";
		if (!invokeAS(m_backgroundLevel, function, { instant ? "1" : "0" }, nullptr, &error))
		{
			note("background", std::string(function) + ": " + error);
		}
		m_backgroundHidden = mode;
	}
	m_backgroundMode = 0;
}

// ---- init / update ----------------------------------------------------------------------------------------------

void WindowManager::init()
{
	// WindowManagerInit.cpp (BFME1): registerAptCallbacks 0x0046FD40 + 0x00464080, then
	// loadAptWindow("Apt\\", "AptLevel0.apt", 1, 0, 0), the focus vector is cleared and gets 12, the dirty/focus flag is set and
	// bit 2 of the AptLevel0 record is cleared.
	registerBuiltinCallbacks();
	// [S-180 candidate] `_global.InGame` is read by the shell movies (MainMenu EscapeKeyPressed, MpGameSetup's host / client switch, SkirmishOpenPlay) and
	// written by none of them (movie scripts searched); spec menus-apt.md 3.3 says the extern object is used "when _global.InGame is set".  The
	// engine hosting the movies is the only writer left, so the manager sets it true.  Retail's writer was not found.
	m_apt->vm().global()->setMember("InGame", AptValue::boolean(true));
	const int index = loadAptWindow("Apt\\", "AptLevel0.apt", true, 0, 0);
	m_focus.clear();
	m_focus.push_back(kFocusNone);
	m_focusDirty = true;
	if (index >= 0)
	{
		m_windows[index].flags = (std::uint8_t)(m_windows[index].flags & ~APTWIN_FLAG4);
	}
}

void WindowManager::update(int elapsedMs)
{
	// WindowManager::update, retail 0x0046E850 (WindowManager_update.cpp)
	if (m_pendingShellPop)
	{
		m_pendingShellPop = false;
		if (m_shell)
		{
			m_shell->pop();
		}
		else
		{
			note("shell-missing", "a pending Shell pop with no Shell attached");
		}
	}
	if (m_shell)
	{
		m_shell->update(); // lane FB7-1: Shell::update (RW 0x75E1D3), the backdrop part
	}
	if (m_windowsDirty)
	{
		m_windowsDirty = false;
		loadPendingWindows();
		m_focusDirty = true;
	}
	refreshFocus();
	int elapsed = elapsedMs;
	if (elapsed > 60)
	{
		elapsed = 60;
	}
	if (m_minFrameTime && elapsed < 34)
	{
		elapsed = 34;
	}
	m_inAptUpdate = true;
	m_apt->update(elapsed);
	m_inAptUpdate = false;
	if (m_gadgetLayer)
	{
		m_gadgetLayer->update(elapsed);
	}
	if (!m_updateListeners.empty())
	{
		// a listener may add or remove listeners (a screen pushes another screen): iterate a snapshot and skip an owner removed meanwhile
		const std::vector<std::pair<const void *, std::function<void()>>> snapshot = m_updateListeners;
		for (const auto &entry : snapshot)
		{
			bool alive = false;
			for (const auto &current : m_updateListeners)
			{
				alive = alive || current.first == entry.first;
			}
			if (alive)
			{
				entry.second();
			}
		}
	}
	updateTooltip();
}

void WindowManager::loadPendingWindows()
{
	for (int i = 0; i < kAptWindowCount; ++i)
	{
		if (m_windows[i].index == i && (m_windows[i].flags & APTWIN_LOAD_PENDING))
		{
			loadAptWindowInto(i);
		}
	}
}

bool WindowManager::loadAptWindowInto(int index)
{
	// 0x00467F20 (body not in the decompile): the movie of the record goes into level `index`; bit 0 (pending) is cleared and
	// bit 1 (loaded) set.  Inference: the retail routine is AptLoadMovie(file, level) plus the flag bookkeeping the focus
	// refresh depends on.
	AptWindowRecord &w = m_windows[index];
	w.flags = (std::uint8_t)(w.flags & ~APTWIN_LOAD_PENDING);
	std::string error;
	if (!m_apt->loadMovie(index, movieNameOf(w.file), &error))
	{
		m_errors.push_back("loading " + w.file + " into _level" + std::to_string(index) + ": " + error);
		note("movie-load-failed", w.file + ": " + error);
		return false;
	}
	w.flags = (std::uint8_t)(w.flags | APTWIN_LOADED);
	return true;
}

void WindowManager::refreshFocus()
{
	// 0x0046E170: once the flag is set, "OnFocus" "1" / "0" goes to every loaded window whose focus bit differs from
	// "this window is the level on top of the focus vector".
	if (!m_focusDirty)
	{
		return;
	}
	m_focusDirty = false;
	const int focus = focusedLevel();
	for (int i = 0; i < kAptWindowCount; ++i)
	{
		AptWindowRecord &w = m_windows[i];
		if (!(w.flags & APTWIN_LOADED))
		{
			continue;
		}
		bool focused;
		if (focus == kFocusNone)
		{
			focused = true; // the decompile: focus == 12 -> true for every loaded window
		}
		else if (focus == -1)
		{
			focused = false;
		}
		else
		{
			focused = focus == i;
		}
		if (focused != ((w.flags & APTWIN_FOCUS) != 0))
		{
			w.flags = (std::uint8_t)(focused ? (w.flags | APTWIN_FOCUS) : (w.flags & ~APTWIN_FOCUS));
			std::string error;
			if (!invokeAS(i, "OnFocus", { focused ? "1" : "0" }, nullptr, &error))
			{
				// the movie has no OnFocus function: most do not; retail's invoke is silent for a missing member, the port records it
				note("onfocus-not-handled", w.file);
			}
		}
	}
}

// ---- level slots ------------------------------------------------------------------------------------------------

int WindowManager::loadAptWindow(const std::string &directory, const std::string &file, bool loadNow, int parameter, int requestedSlot)
{
	// WindowManager_loadAptWindow.cpp
	auto found = m_fileToWindow.find(file);
	if (found != m_fileToWindow.end() && found->second != -1)
	{
		return -1;
	}
	int index = requestedSlot;
	if (index == -1)
	{
		index = -1;
		for (int i = 0; i < kAptWindowCount; ++i)
		{
			if (m_windows[i].index == -1)
			{
				index = i;
				break;
			}
		}
		if (index == -1)
		{
			return -1;
		}
	}
	else
	{
		if ((unsigned)index >= (unsigned)kAptWindowCount)
		{
			return 0; // retail returns 0 here (not -1): ported as it is
		}
		if (m_windows[index].index != -1)
		{
			return -1;
		}
	}
	std::string dir = directory;
	if (!endsWith(dir, "\\") && !endsWith(dir, "/"))
	{
		dir += "\\";
	}
	AptWindowRecord &w = m_windows[index];
	w.directory = dir;
	w.file = file;
	w.parameter = parameter;
	w.index = index;
	w.flags = 0;
	m_fileToWindow[file] = index;
	if (loadNow)
	{
		m_windowsDirty = true;
		w.flags = (std::uint8_t)(w.flags | APTWIN_LOAD_PENDING);
	}
	// loadBigFilesFromDirectory(directory, file + ".big", overwrite): the archives are mounted already (RetailArchivePolicy), the
	// movie files are found by name through the Apt file source.
	return index;
}

int WindowManager::findAptMovieIndex(const std::string &file) const
{
	auto it = m_fileToWindow.find(file);
	return it == m_fileToWindow.end() ? -1 : it->second;
}

bool WindowManager::isAptWindowLoaded(int index) const
{
	return index >= 0 && index < kAptWindowCount && (m_windows[index].flags & APTWIN_LOADED) != 0;
}

AptSpriteInst *WindowManager::levelRoot(int index)
{
	if (index < 0 || index >= kAptWindowCount)
	{
		return nullptr;
	}
	return m_apt->level(index);
}

bool WindowManager::hideAptWindow(int index)
{
	// WindowManager_hideAptWindow.cpp: clears the cursor tooltip, false when the window is not loaded.  The hide itself
	// (hideAptWindowInternal) has no body in the decompile: [S-171] the port makes the level instance invisible.
	if ((unsigned)index >= (unsigned)kAptWindowCount)
	{
		return false;
	}
	m_services.setCursorTooltip(std::string());
	m_tooltipLabel.clear();
	if (!(m_windows[index].flags & APTWIN_LOADED))
	{
		return false;
	}
	if (AptSpriteInst *root = levelRoot(index))
	{
		root->visible = false;
	}
	m_windows[index].flags = (std::uint8_t)(m_windows[index].flags | APTWIN_HIDDEN);
	return true;
}

bool WindowManager::showAptWindow(int index)
{
	if ((unsigned)index >= (unsigned)kAptWindowCount || !(m_windows[index].flags & APTWIN_LOADED))
	{
		return false;
	}
	if (AptSpriteInst *root = levelRoot(index))
	{
		root->visible = true;
	}
	m_windows[index].flags = (std::uint8_t)(m_windows[index].flags & ~APTWIN_HIDDEN);
	return true;
}

bool WindowManager::unloadAptWindow(int index)
{
	if ((unsigned)index >= (unsigned)kAptWindowCount || m_windows[index].index == -1)
	{
		return false;
	}
	AptWindowRecord &w = m_windows[index];
	if (w.flags & APTWIN_LOADED)
	{
		m_apt->unloadLevel(index);
	}
	m_fileToWindow.erase(w.file);
	w = AptWindowRecord();
	// the focus stack forgets the level
	m_focus.erase(std::remove(m_focus.begin(), m_focus.end(), index), m_focus.end());
	if (m_focus.empty())
	{
		m_focus.push_back(kFocusNone);
	}
	m_focusDirty = true;
	return true;
}

void WindowManager::popFocus()
{
	if (m_focus.size() > 1)
	{
		m_focus.pop_back();
	}
	m_focusDirty = true;
}

int WindowManager::levelOf(const AptCharacterInst &inst) const
{
	const AptCharacterInst *cur = &inst;
	while (cur->parent())
	{
		cur = cur->parent();
	}
	for (int i = 0; i < kAptWindowCount; ++i)
	{
		if (m_apt->level(i) == cur)
		{
			return i;
		}
	}
	return -1;
}

// ---- registries -------------------------------------------------------------------------------------------------

bool WindowManager::registerCommand(const std::string &name, CommandFn fn)
{
	if (!fn)
	{
		return false;
	}
	return m_commands.emplace(name, std::move(fn)).second;
}

bool WindowManager::registerProvider(const std::string &name, ProviderFn fn)
{
	if (!fn)
	{
		return false;
	}
	return m_providers.emplace(name, std::move(fn)).second;
}

bool WindowManager::registerComponent(const std::string &name, ComponentFn fn)
{
	if (!fn)
	{
		return false;
	}
	return m_components.emplace(name, std::move(fn)).second;
}

bool WindowManager::registerScreenRef(const std::string &name, ScreenRefFn fn)
{
	if (!fn)
	{
		return false;
	}
	for (const auto &e : m_screenRefs)
	{
		if (e.first == name)
		{
			return false;
		}
	}
	m_screenRefs.emplace_back(name, std::move(fn));
	return true;
}

bool WindowManager::registerTooltip(const std::string &name, TooltipFn fn)
{
	if (!fn)
	{
		return false;
	}
	return m_tooltips.emplace(name, std::move(fn)).second;
}

void WindowManager::closeAptScreen(const std::string &name)
{
	// _bfme_closeAptScreen (AptScreenClose.cpp): the record names equal to `name` are cleared and the screen reference erased
	for (AptWindowRecord &w : m_windows)
	{
		(void)w; // the window table of the decompile keys records by screen name; the port keys them by file (m_fileToWindow)
	}
	m_screenRefs.erase(std::remove_if(m_screenRefs.begin(), m_screenRefs.end(), [&](const std::pair<std::string, ScreenRefFn> &e) { return e.first == name; }), m_screenRefs.end());
}

namespace
{
template <class Map>
std::vector<std::string> keysOf(const Map &m)
{
	std::vector<std::string> out;
	for (const auto &e : m)
	{
		out.push_back(e.first);
	}
	return out;
}
} // namespace

std::vector<std::string> WindowManager::commandNames() const { return keysOf(m_commands); }
std::vector<std::string> WindowManager::providerNames() const { return keysOf(m_providers); }
std::vector<std::string> WindowManager::componentNames() const { return keysOf(m_components); }
std::vector<std::string> WindowManager::screenRefNames() const
{
	std::vector<std::string> out;
	for (const auto &e : m_screenRefs)
	{
		out.push_back(e.first);
	}
	return out;
}

// ---- movie -> engine --------------------------------------------------------------------------------------------

bool WindowManager::invokeCallback(const std::string &name, const std::string &argument)
{
	if (name.empty())
	{
		return false; // WindowManager::invokeCallback: a null name returns
	}
	auto it = m_commands.find(name);
	if (it == m_commands.end())
	{
		note("command-unhandled", name + (argument.empty() ? std::string() : " (" + argument + ")"));
		return false;
	}
	// copy: the handler may unregister itself (a screen closing from its own command)
	CommandFn fn = it->second;
	fn(argument);
	return true;
}

void WindowManager::fscommand(const std::string &command, const std::string &argument)
{
	invokeCallback(command, argument);
}

void WindowManager::loadMovie(const std::string &movieName, const std::string &target)
{
	// the Apt player performs the load; this is the notification (the unload form has an empty movie name)
	m_movieLoads.push_back({ movieName, target });
}

void WindowManager::getURL(const std::string &url, const std::string &target)
{
	note("geturl-unhandled", url + " -> " + target);
}

void WindowManager::trace(const std::string &message)
{
	m_traces.push_back(message);
}

void WindowManager::scriptError(const std::string &message)
{
	m_errors.push_back(message);
}

std::uint32_t WindowManager::random()
{
	// ActionRandom reads the client RNG (bfmeNext1221).  The client random stream is not wired into the shell: a deterministic
	// LCG stands in and the first use is reported [S-174].
	if (noteCount("apt-random-unwired") == 0)
	{
		note("apt-random-unwired", "ActionRandom uses the shell's own deterministic generator, not the client RNG");
	}
	m_randomState = m_randomState * 1664525u + 1013904223u;
	return m_randomState >> 8;
}

AptExternResult WindowManager::getExtern(const std::string &name, std::string &value)
{
	// invokeCallbackWithArg: exact name, then without the `_levelN/` prefix
	auto it = m_providers.find(name);
	if (it == m_providers.end())
	{
		it = m_providers.find(skipLevelPrefix(name));
	}
	if (it == m_providers.end())
	{
		note("extern-read-unhandled", name);
		return AptExternResult::NoProvider;
	}
	ProviderFn fn = it->second;
	const bool numeric = m_numericProviders.count(it->first) != 0;
	value.clear();
	if (!fn(name, value, false))
	{
		return AptExternResult::Undefined;
	}
	if (numeric && !value.empty() && value.size() <= 10)
	{
		bool digits = true;
		for (size_t i = 0; digits && i < value.size(); ++i)
		{
			digits = (value[i] >= '0' && value[i] <= '9') || (value[i] == '-' && i == 0 && value.size() > 1);
		}
		if (digits)
		{
			return AptExternResult::Number;
		}
	}
	return AptExternResult::Value;
}

bool WindowManager::setExtern(const std::string &name, const std::string &value)
{
	auto it = m_providers.find(name);
	if (it == m_providers.end())
	{
		it = m_providers.find(skipLevelPrefix(name));
	}
	if (it == m_providers.end())
	{
		note("extern-write-unhandled", name);
		return false;
	}
	ProviderFn fn = it->second;
	std::string buffer = value;
	fn(name, buffer, true);
	return true;
}

// ---- engine -> movie --------------------------------------------------------------------------------------------

bool WindowManager::invokeAS(int level, const std::string &function, const std::vector<std::string> &args, std::string *result, std::string *error)
{
	AptSpriteInst *root = levelRoot(level);
	if (!root)
	{
		if (error)
		{
			*error = "no movie in _level" + std::to_string(level);
		}
		return false;
	}
	return m_apt->invoke(root, function, args, result, error);
}

bool WindowManager::invokeASAt(int level, const std::string &path, const std::string &function, const std::vector<std::string> &args, std::string *result, std::string *error)
{
	AptSpriteInst *root = levelRoot(level);
	if (!root)
	{
		if (error)
		{
			*error = "no movie in _level" + std::to_string(level);
		}
		return false;
	}
	AptCharacterInst *scope = m_apt->resolvePath(root, path);
	if (!scope)
	{
		if (error)
		{
			*error = "path '" + path + "' does not resolve in _level" + std::to_string(level);
		}
		return false;
	}
	return m_apt->invoke(scope, function, args, result, error);
}

bool WindowManager::invokeASAtValues(int level, const std::string &path, const std::string &function, const std::vector<AptValue> &args, std::string *error)
{
	AptSpriteInst *root = levelRoot(level);
	AptCharacterInst *scope = root ? m_apt->resolvePath(root, path) : nullptr;
	if (!scope)
	{
		if (error)
		{
			*error = root ? "path '" + path + "' does not resolve in _level" + std::to_string(level) : "no movie in _level" + std::to_string(level);
		}
		return false;
	}
	return m_apt->invokeValues(scope, function, args, nullptr, error);
}

bool WindowManager::setAptRootMember(int level, const std::string &name, const AptValue &value, std::string *error)
{
	AptSpriteInst *root = levelRoot(level);
	if (!root)
	{
		if (error)
		{
			*error = "no movie in _level" + std::to_string(level);
		}
		return false;
	}
	root->setMember(name, value);
	return true;
}

// ---- text -------------------------------------------------------------------------------------------------------

void WindowManager::setAptText(const std::string &name, const std::string &text)
{
	TextRecord &r = m_texts[name];
	r.text = text;
	r.set = true;
	if (r.listener)
	{
		r.listener(r.text);
	}
}

void WindowManager::bindAptText(const std::string &name, const std::string &defaultText, TextListener listener)
{
	if (listener)
	{
		TextRecord &r = m_texts[name];
		r.listener = std::move(listener);
		if (r.text.empty())
		{
			r.text = defaultText;
		}
		r.listener(r.text);
	}
	else
	{
		auto it = m_texts.find(name);
		if (it != m_texts.end())
		{
			it->second.listener = nullptr;
		}
	}
}

bool WindowManager::aptTextShown(const std::string &name, std::string &out) const
{
	auto it = m_texts.find(name);
	if (it == m_texts.end() || (it->second.text.empty() && !it->second.set))
	{
		return false;
	}
	out = it->second.text;
	return true;
}

const std::string *WindowManager::aptText(const std::string &name) const
{
	auto it = m_texts.find(name);
	return it == m_texts.end() ? nullptr : &it->second.text;
}

// ---- components -------------------------------------------------------------------------------------------------

bool WindowManager::isComponentSymbol(const std::string &, const std::string &symbolName)
{
	return m_components.count(symbolName) != 0;
}

std::string WindowManager::gadgetInstanceName(const AptCharacterInst &inst)
{
	// [S-170] The name `<Prefix>::InitGadgets` receives.  Target facts: the MpGameSetup handler (RotWK 0x00840906, same body as BFME1
	// MpGameSetupOnInitGadget.cpp) compares the name with "MapList", "MpGameSetup::GameType" and "MpGameSetup::MapType" for
	// plain names and otherwise reads a slot number with sscanf(name, "%d") (the format string at 0x00BD4194 is "%d") and takes
	// the leaf after the last '~' (else the last '/') (bfmePathLeafAfterMarker, 0x008155A7) as "Player", "PlayerTemplate",
	// "Team", "Color" or "ReadyButton"; the retail movies name those slot gadgets `PlayerList.~N.<leaf>`.  The composition rule
	// is inference: a plain instance name stays as it is; a gadget whose parent clip is named `~<digits>` gets
	// "<digits>/<name>" (so the number comes first and the leaf follows the last '/').
	const std::string &own = inst.instName();
	const AptCharacterInst *parent = inst.parent();
	if (parent)
	{
		const std::string &pn = parent->instName();
		if (pn.size() > 1 && pn[0] == '~' && std::isdigit((unsigned char)pn[1]))
		{
			return pn.substr(1) + "/" + own;
		}
	}
	return own;
}

void WindowManager::componentInstanceCreated(AptCharacterInst &inst, const std::string &movieName, const std::string &symbolName)
{
	auto it = m_components.find(symbolName);
	if (it == m_components.end())
	{
		return;
	}
	AptComponentRequest req{ *this, inst };
	req.level = levelOf(inst);
	req.movie = movieName;
	req.symbol = symbolName;
	req.instancePath = inst.targetPath();
	req.instanceName = gadgetInstanceName(inst);
	float box[4] = { 0, 0, 0, 0 };
	if (stageBounds(inst, box))
	{
		req.x0 = box[0];
		req.y0 = box[1];
		req.x1 = box[2];
		req.y1 = box[3];
	}
	// The placeholder's Construct clip event (mask 0x40000, run when the instance is placed, before this notification) set the clip
	// variables `_type`, `_Load`, `_Init`, `_Draw` [S-172]; target fact: every gadget placeholder of Skirmish.apt and MpGameSetup.apt carries
	// such a program.
	auto readString = [&](const char *name, std::string &out, bool &has) {
		AptValue v;
		if (inst.getMember(name, v) && v.isString())
		{
			out = v.asString();
			has = true;
		}
	};
	readString("_type", req.type, req.hasType);
	readString("_Load", req.load, req.hasLoad);
	readString("_Init", req.init, req.hasInit);
	{
		AptValue v;
		if (inst.getMember("_Draw", v))
		{
			req.hasDraw = true;
			req.draw = v.toBoolean(7);
		}
	}
	ComponentFn fn = it->second;
	ComponentRecord rec;
	rec.instance = &inst;
	rec.level = req.level;
	rec.instancePath = req.instancePath;
	rec.instanceName = req.instanceName;
	rec.symbol = symbolName;
	rec.movie = movieName;
	rec.init = req.init;
	// lane MP-2: a record of the same instance address or the same instance path is stale: its placeholder's subtree was released without a
	// destroy notification (the gadgets of a removed placeholder clip stay, as the native windows of a screen do) and the clip is placed again
	// (LanLobby.apt: LanOpenPlay's CreateComponents at frame 0, back from the setup). Left in place, a later destroy matched by address would
	// take the new gadget's window
	for (std::size_t i = m_componentRecords.size(); i-- > 0;)
	{
		const ComponentRecord &old = m_componentRecords[i];
		if (old.instance == &inst || (old.level == req.level && old.instancePath == req.instancePath))
		{
			m_componentRecords.erase(m_componentRecords.begin() + (std::ptrdiff_t)i);
		}
	}
	rec.window = fn(req);
	m_componentRecords.push_back(rec);
	if (!rec.window)
	{
		return;
	}
	if (!req.hasInit)
	{
		note("component-no-init", req.instancePath + " (" + req.symbol + "): the placeholder script set no `_Init`; no screen reference is called");
		return;
	}
	std::vector<std::pair<std::string, ScreenRefFn>> refs = m_screenRefs;
	for (const auto &ref : refs)
	{
		if (ref.first == req.init)
		{
			ref.second(req.instanceName, rec.window.get());
			return;
		}
	}
	note("unknown-screen-ref", req.instancePath + ": `_Init` names '" + req.init + "', which no screen registered");
}

void WindowManager::componentInstanceDestroyed(AptCharacterInst &inst)
{
	// Lane WINCRASH-1: the gadget window stays while its level is loaded. RotWK's component handler (RW 0x8142D2, the creation side) keeps
	// the window in the hash table at RW 0xDE8A28 under the instance's name; only the level's unload (RW 0x814BA9 = BFME2 decomp
	// BfmeConv1292.cpp Rva00411E80, called with the level index) or a new placement under the same name replaces it, never the removal of the
	// placeholder clip. The screens keep the windows InitGadgets gave them and read them after their movie removed the clips: Options.apt's
	// Advanced button plays Main's advanced page, which removes the basic page's placeholders, and its Done then closes the screen with
	// GameCode('Save') (AptOptions::Save, RW 0x91FC9C, reads the basic page's sliders). The port destroyed the windows with the clips (the
	// slider's GWM_DESTROY clears its SliderData), and Save read Brightness through a null SliderData: the owner's Windows crash of
	// 2026-10-09 (e204772c, GadgetSliderGetPosition <- AptOptionsScreen::save). The window is hidden:
	// a clip that is not on the display list is not drawn, so neither is its gadget [INFERENCE: retail's render callback is not called for
	// it; whether retail also hides the window is not traced].
	for (ComponentRecord &r : m_componentRecords)
	{
		if (r.instance == &inst)
		{
			r.instance = nullptr;
			if (r.window)
			{
				r.window->winHide(true);
			}
			return;
		}
	}
}

void WindowManager::levelUnloaded(int level)
{
	// RW 0x814BA9: the records of the level go (the windows with them, AptGadgetLayer's deleter)
	m_componentRecords.erase(std::remove_if(m_componentRecords.begin(), m_componentRecords.end(), [level](const ComponentRecord &r) { return r.level == level; }),
		m_componentRecords.end());
}

GameWindow *WindowManager::componentWindow(const std::string &instancePath) const
{
	for (const ComponentRecord &r : m_componentRecords)
	{
		if (r.instance && r.instancePath == instancePath) // a placed clip's (a detached record's window is not the clip's any more)
		{
			return r.window.get();
		}
	}
	return nullptr;
}

// ---- tooltips ---------------------------------------------------------------------------------------------------

std::string WindowManager::hoverPath() const
{
	// 0x0046E850 reads a path string from bfmeGo929E (0x008922A0, "copies a path string into the caller's buffer": the engine's
	// current hover target).  Inference [S-173]: the hover target is the clip under the pointer (its button's parent clip when a
	// button is current); retail turns "_levelN/a/b" into "<movie file name without '.'-suffix>/a/b".
	const AptCharacterInst *target = nullptr;
	if (AptButtonInst *b = m_apt->input().currentButton())
	{
		target = b->parent();
	}
	if (!target)
	{
		target = m_apt->input().hoverClip();
	}
	if (!target)
	{
		return std::string();
	}
	std::vector<const AptCharacterInst *> chain;
	for (const AptCharacterInst *c = target; c; c = c->parent())
	{
		chain.push_back(c);
	}
	const AptCharacterInst *root = chain.back();
	int level = -1;
	for (int i = 0; i < kAptWindowCount; ++i)
	{
		if (m_apt->level(i) == root)
		{
			level = i;
		}
	}
	if (level < 0)
	{
		return std::string();
	}
	std::string name = movieNameOf(m_windows[level].file);
	for (std::size_t i = chain.size() - 1; i-- > 0;)
	{
		name += "/" + chain[i]->instName();
	}
	return name;
}

void WindowManager::removeUpdateListeners(const void *owner)
{
	m_updateListeners.erase(std::remove_if(m_updateListeners.begin(), m_updateListeners.end(), [owner](const auto &e) { return e.first == owner; }), m_updateListeners.end());
}

void WindowManager::updateTooltip()
{
	// tail of 0x0046E850 (WindowManager_update.cpp): with no tooltip showing and no game window under the cursor the hover name is
	// looked up in the tooltip callback map; failing that the game text "TOOLTIP:<name>" is shown unless the name ends with '/'.
	const std::string name = hoverPath();
	if (name.empty())
	{
		if (!m_tooltipPath.empty())
		{
			m_tooltipPath.clear();
			m_tooltipLabel.clear();
			m_services.setCursorTooltip(std::string());
		}
		return;
	}
	if (name == m_tooltipPath)
	{
		return;
	}
	m_tooltipPath = name;
	std::string label;
	auto it = m_tooltips.find(name);
	if (it != m_tooltips.end())
	{
		TooltipFn fn = it->second;
		label = fn(name);
	}
	else if (name.back() != '/')
	{
		label = "TOOLTIP:" + name;
	}
	m_tooltipLabel = label;
	m_services.setCursorTooltip(label);
}

// ---- built-in callbacks -----------------------------------------------------------------------------------------

void WindowManager::registerBuiltinCallbacks()
{
	// WindowManagerRegisterAptCallbacks0046FD40.cpp / ...00464080.cpp (BFME1); the RotWK binary carries the same names.
	registerCommand("PlaySound", [this](const std::string &arg) { m_services.playSound(arg); });
	registerCommand("SetBackground", [this](const std::string &arg) {
		// RW 0x815507 (lane FB7-1): "fadein" shows the front-end background, "fadeout" hides it with its animation, "off" at once (_strcmpi);
		// another argument does nothing. The shell is told as before (a device hook)
		if (strcmpiAscii(arg, "fadein"))
		{
			setBackground(1);
		}
		else if (strcmpiAscii(arg, "fadeout"))
		{
			hideBackground(false);
		}
		else if (strcmpiAscii(arg, "off"))
		{
			hideBackground(true);
		}
		m_services.setBackground(arg);
	});
	registerCommand("MouseSetVisibility", [this](const std::string &arg) {
		// the argument is the movie's boolean as a string ("1"/"true"); the exact accepted spellings were not read [S-175]
		m_services.setMouseVisible(arg == "1" || arg == "true" || arg == "True" || arg == "TRUE");
	});
	registerCommand("CloseWindow", [this](const std::string &arg) {
		note("unported-command", "CloseWindow(" + arg + ") [S-175]");
	});
	registerCommand("OnClickThroughPress", [this](const std::string &arg) {
		note("unported-command", "OnClickThroughPress(" + arg + ") [S-175]");
	});
	registerCommand("OnClickThroughRelease", [this](const std::string &arg) {
		note("unported-command", "OnClickThroughRelease(" + arg + ") [S-175]");
	});
	registerCommand("EnableComponents", [this](const std::string &arg) {
		note("unported-command", "EnableComponents(" + arg + ") [S-175]");
	});
	registerCommand("DisableComponents", [this](const std::string &arg) {
		note("unported-command", "DisableComponents(" + arg + ") [S-175]");
	});
	// Providers answered with the facts of the RotWK handlers (AptPlayerTestUtil.h, AptStubHost): a read answers "1" for InGame and
	// "0" for the demo flags; DoTrace answers "0" (the debug flag at 0x00E0302C is clear in a retail run); a write is ignored.
	auto constant = [](const char *answer) {
		return [answer](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = answer;
			}
			return true;
		};
	};
	registerProvider("InGame", constant("1"));
	registerProvider("InBetaDemo", constant("0"));
	registerProvider("InDreamMachineDemo", constant("0"));
	registerProvider("DoTrace", constant("0"));
}

std::size_t WindowManager::noteCount(const std::string &kind) const
{
	std::size_t n = 0;
	for (const WindowManagerNote &note : m_notes)
	{
		if (note.kind == kind)
		{
			++n;
		}
	}
	return n;
}

bool WindowManager::stageBounds(const AptCharacterInst &inst, float out[4])
{
	float bx0, by0, bx1, by1;
	if (!inst.contentBounds(bx0, by0, bx1, by1))
	{
		return false;
	}
	const AptMatrix m = inst.globalMatrix();
	const float xs[4] = { bx0, bx1, bx0, bx1 };
	const float ys[4] = { by0, by0, by1, by1 };
	out[0] = out[1] = 1e30f;
	out[2] = out[3] = -1e30f;
	for (int i = 0; i < 4; ++i)
	{
		float ox, oy;
		m.apply(xs[i], ys[i], ox, oy);
		out[0] = std::min(out[0], ox);
		out[1] = std::min(out[1], oy);
		out[2] = std::max(out[2], ox);
		out[3] = std::max(out[3], oy);
	}
	return true;
}

void WindowManager::releaseComponentWindows()
{
	for (ComponentRecord &r : m_componentRecords)
	{
		r.window.reset();
	}
}

bool WindowManager::postMouseMove(float x, float y)
{
	const bool used = m_gadgetLayer ? m_gadgetLayer->mouseMove(x, y) : false;
	m_apt->input().postMouseMove(x, y);
	return used;
}

bool WindowManager::postMouseButton(bool down)
{
	const bool used = m_gadgetLayer ? m_gadgetLayer->mouseButton(down) : false;
	m_apt->input().postMouseButton(down);
	return used;
}

bool WindowManager::postMouseWheel(int delta)
{
	const bool used = m_gadgetLayer ? m_gadgetLayer->mouseWheel(delta) : false;
	m_apt->input().postMouseWheel(delta);
	return used;
}

void WindowManager::postAptKey(int aptKeyCode, bool down)
{
	m_apt->input().postKey(aptKeyCode, down);
}

bool WindowManager::postGadgetKey(int dikCode, bool down, int modifiers)
{
	return m_gadgetLayer ? m_gadgetLayer->key(dikCode, down, modifiers) : false;
}

bool WindowManager::postTextInput(char16_t ch)
{
	return m_gadgetLayer ? m_gadgetLayer->textInput(ch) : false;
}

// ---- lane END-2: image records -------------------------------------------------------------------------------------------------------------------------

void WindowManager::setAptImage(const std::string &name, const std::string &mappedImage)
{
	if (mappedImage.empty())
	{
		m_images.erase(name);
	}
	else
	{
		m_images[name] = mappedImage;
	}
}

const std::string *WindowManager::aptImage(const std::string &name) const
{
	auto it = m_images.find(name);
	return it == m_images.end() ? nullptr : &it->second;
}
