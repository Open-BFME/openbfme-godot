// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptGadgetLayer.h.

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptColorPicker.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "GameClient/GUI/Gadgets.h"

#include <algorithm>
#include <cctype>
#include <optional>

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
} // namespace

bool loadGadgetSkinData(ArchiveFileSystem &fs, GadgetSkinData &out, std::string *error)
{
	INIEnvironment env;
	env.fileSystem = &fs;
	out.images.registerBlocks(env.blocks);
	out.headers.registerBlocks(env.blocks);
	INI ini(env);
	try
	{
		// ImageCollection::load: every file below Data\INI\MappedImages (ZH loads TextureSize_<n> and HandCreated; RotWK's tree adds AptImages,
		// TransitionImages ...: all MappedImage blocks)
		ini.loadDirectory("data\\ini\\mappedimages", true, INI_LOAD_OVERWRITE);
		ini.load("headertemplate.ini", INI_LOAD_OVERWRITE);
	}
	catch (const INIException &e)
	{
		if (error)
		{
			*error = std::string("loading the gadget skin data: ") + e.what();
		}
		return false;
	}
	return true;
}

const std::vector<std::string> &AptGadgetLayer::componentSymbols()
{
	// RotWK game.dat 0x00C4FD..: the nine names that share the gadget handler; View3D has its own handler (device component)
	static const std::vector<std::string> names = { "GameWindow", "HorzSlider", "ComboBox", "ImageComboBox", "CheckBox", "TextEntry", "ListBox", "PushButton", "BinkMovie" };
	return names;
}

std::string AptGadgetLayer::skinPath(const std::string &symbol)
{
	return "window/apt/" + lowerAscii(symbol) + ".wnd";
}

AptGadgetLayer::AptGadgetLayer(WindowManager &windows, AptFileSource &source, GadgetSkinData &skins) : m_windows(windows), m_source(source), m_skins(skins)
{
	m_gwm.setImages(&m_skins.images);
	// the callbacks the shipped skins name (the lexicon of retail resolves the others, "Apt:None", to nothing): the BFME ImageComboBox [S-178]
	m_lexicon.system["GadgetImageComboBoxSystem"] = GadgetImageComboBoxSystem;
	m_lexicon.input["GadgetImageComboBoxInput"] = GadgetImageComboBoxInput;
	m_lexicon.draw["W3DGadgetImageComboBoxDraw"] = W3DGadgetImageComboBoxDraw;
	// lane UI-2: the lobby's map window (window\Apt\MpMapWindow.wnd; RotWK lexicon entries RW 0xD98F64 / 0xDA1A44)
	m_lexicon.draw["W3DDrawMapPreview"] = W3DDrawMapPreview;
	m_lexicon.system["PassSelectedButtonsToParentSystem"] = PassSelectedButtonsToParentSystem;
	m_windows.setGadgetLayer(this);
	m_pickers = std::make_unique<AptColorPickers>(m_windows, m_skins.images, m_source); // lane CAH-2
}

AptGadgetLayer::~AptGadgetLayer()
{
	if (m_windows.gadgetLayer() == this)
	{
		m_windows.setGadgetLayer(nullptr);
	}
	m_pickers.reset(); // lane CAH-2: its externs and component name go before the window manager's records
	// the WindowManager's component records still hold shared_ptrs with deleters into this layer: drop them first
	m_windows.releaseComponentWindows();
	for (const std::string &symbol : componentSymbols())
	{
		m_windows.unregisterComponent(symbol);
	}
	m_gwm.winDestroyAll();
	m_gwm.processDestroyList();
}

void AptGadgetLayer::registerComponents()
{
	if (m_registered)
	{
		return;
	}
	m_registered = true;
	// one shared handler (j_0000ed59 in BFME1): the factory reads window/apt/<symbol>.wnd; a symbol without a skin file or with a device
	// owner reports it when an instance appears
	for (const std::string &symbol : componentSymbols())
	{
		m_windows.registerComponent(symbol, [this](AptComponentRequest &r) -> std::shared_ptr<GameWindow> { return createGadget(r); });
	}
	m_pickers->registerComponent(); // lane CAH-2: "ColorPicker" (RW 0x8151D2) has its own handler
}

namespace
{
// the template map's keys (RW 0x814EFA), as createGadget spells a skin path; retail lower-cases `_Load` before the lookup (RW 0x81487B's neighbour, the
// create of BFME2 0x008115A9)
bool isTemplateSkin(const std::string &path)
{
	std::string lower = path;
	for (char &ch : lower)
	{
		ch = (char)std::tolower((unsigned char)ch);
	}
	return lower == "window/apt/combobox.wnd" || lower == "window/apt/horzslider.wnd";
}
} // namespace

std::shared_ptr<GameWindow> AptGadgetLayer::createGadget(AptComponentRequest &request)
{
	// BinkMovie is a video host (device); the others read their skin from window/apt/<symbol>.wnd
	if (request.symbol == "BinkMovie")
	{
		m_notes.push_back("BinkMovie placeholder " + request.instancePath + ": the video host is a device component (S-177)");
		return nullptr;
	}
	// the placeholder's script names the skin (`_Load`, relative to window\\); without it the symbol's own skin is read and the gap is noted [S-172]
	std::string path;
	if (request.hasLoad)
	{
		path = "window/" + request.load;
		for (char &ch : path)
		{
			if (ch == '\\')
			{
				ch = '/';
			}
		}
	}
	else
	{
		path = skinPath(request.symbol);
		m_notes.push_back("gadget " + request.instancePath + " (" + request.symbol + "): the placeholder script set no `_Load`; the skin " + path + " is inferred from the symbol");
	}
	std::vector<std::uint8_t> bytes;
	std::string err;
	if (!m_source.readFile(path, bytes, &err))
	{
		m_errors.push_back("gadget " + request.instancePath + " (" + request.symbol + "): the skin " + path + " cannot be read: " + err);
		return nullptr;
	}
	const std::string text(bytes.begin(), bytes.end());
	WindowScriptLoader loader(m_gwm, &m_skins.headers, &m_lexicon);
	WindowLayoutInfo info;
	GameWindow *window = loader.createFromScript(path, text, &info, &err);
	if (!window)
	{
		m_errors.push_back("gadget " + request.instancePath + " (" + request.symbol + "): " + err);
		return nullptr;
	}
	if (info.windows.size() != 1)
	{
		m_notes.push_back(path + " defines " + std::to_string(info.windows.size()) + " windows; the first is the gadget");
	}
	// lane OPTS-4, TARGET FACTS: the two skins seeded in the template map at start (RW 0x814EFA: "apt/combobox.wnd", "apt/horzslider.wnd") are loaded once
	// under the template parent, whose status is 0x08000000 (the context of RW 0x815389), and winCreateFromScript gives every window of a file loaded
	// under such a parent that status (RW 0x7190B4: the parent's bit read at entry, winSetStatus(0x8000000) at RW 0x719393); duplicateGadget (RW 0x718D93)
	// copies the template's status into each component (the create view's + 0x04). The slider's image draw reads the bit (RW 0x4A1130: no 800 x 600
	// multipliers), which is why retail's tick fill is as tall as the window and the port's was 1.28 times that. Other skins are loaded under the
	// level's window and keep their file's status
	if (isTemplateSkin(path))
	{
		window->winSetStatus(WIN_STATUS_APT_TEMPLATE);
	}
	// created at the size of the placeholder (menus-apt.md 1.3 / 3.3)
	const int x = (int)request.x0, y = (int)request.y0;
	const int w = (int)(request.x1 - request.x0), h = (int)(request.y1 - request.y0);
	window->winSetPosition(x, y);
	window->winSetSize(w, h);
	window->setAptInstanceName(request.instanceName);
	auto owner = m_levelOwners.find(request.level);
	if (owner != m_levelOwners.end())
	{
		window->winSetOwner(owner->second);
	}
	Placed placed;
	placed.instance = &request.instance;
	placed.lastBounds[0] = request.x0;
	placed.lastBounds[1] = request.y0;
	placed.lastBounds[2] = request.x1;
	placed.lastBounds[3] = request.y1;
	placed.shown = true;
	GameWindowManager *gwm = &m_gwm;
	std::map<const GameWindow *, Placed> *placedMap = &m_placed;
	// the deleter runs when the WindowManager forgets the component record (its instance was destroyed): the native window goes with it
	std::shared_ptr<GameWindow> sp(window, [gwm, placedMap](GameWindow *w) {
		placedMap->erase(w);
		gwm->winDestroy(w);
	});
	m_placed[window] = placed;
	return sp;
}

void AptGadgetLayer::setLevelOwner(int level, GameWindow *owner)
{
	m_levelOwners[level] = owner;
}

void AptGadgetLayer::clearLevelOwner(int level)
{
	m_levelOwners.erase(level);
}

GameWindow *AptGadgetLayer::createOwnerWindow(GameWinSystemFunc system, const std::string &name)
{
	// a see-thru, hidden, input-less user window: the owner of a screen's gadgets (BFME1: the screen object is itself a GameWindow)
	WinInstanceData inst;
	inst.m_style = GWS_USER_WINDOW;
	inst.m_decoratedNameString = name;
	GameWindow *w = m_gwm.winCreate(nullptr, WIN_STATUS_HIDDEN | WIN_STATUS_NO_INPUT | WIN_STATUS_SEE_THRU, 0, 0, 0, 0, std::move(system), &inst);
	m_ownerWindows.push_back(w);
	return w;
}

void AptGadgetLayer::destroyOwnerWindow(GameWindow *window)
{
	auto it = std::find(m_ownerWindows.begin(), m_ownerWindows.end(), window);
	if (it != m_ownerWindows.end())
	{
		m_ownerWindows.erase(it);
		for (auto o = m_levelOwners.begin(); o != m_levelOwners.end();)
		{
			if (o->second == window)
			{
				o = m_levelOwners.erase(o);
			}
			else
			{
				++o;
			}
		}
		m_gwm.winReleaseOwned(window); // lane MENUFIX-2: gadgets that outlive their screen no longer point at the freed owner
		m_gwm.winDestroy(window);
		m_gwm.processDestroyList();
	}
}

GameWindow *AptGadgetLayer::createEngineGadget(const std::string &path, int x, int y, int w, int h, GameWindow *owner, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	if (!m_source.readFile(path, bytes, &err))
	{
		if (error)
		{
			*error = "the skin " + path + " cannot be read: " + err;
		}
		return nullptr;
	}
	const std::string text(bytes.begin(), bytes.end());
	WindowScriptLoader loader(m_gwm, &m_skins.headers, &m_lexicon);
	WindowLayoutInfo info;
	GameWindow *window = loader.createFromScript(path, text, &info, &err);
	if (!window)
	{
		if (error)
		{
			*error = path + ": " + err;
		}
		return nullptr;
	}
	// lane OPTS-4: an engine gadget stands where a component of the movie would (stage pixels), so it is unscaled as the template skins' components are
	// (createGadget); not retail: retail has no engine-placed gadgets
	window->winSetStatus(WIN_STATUS_APT_TEMPLATE);
	window->winSetPosition(x, y);
	window->winSetSize(w, h);
	if (owner)
	{
		window->winSetOwner(owner);
	}
	return window;
}

void AptGadgetLayer::destroyEngineGadget(GameWindow *window)
{
	if (window)
	{
		m_gwm.winDestroy(window);
		m_gwm.processDestroyList();
	}
}

void AptGadgetLayer::update(int elapsedMs)
{
	if (m_pickers)
	{
		m_pickers->update(); // lane CAH-2: RW 0xB552BF's per-draw work
	}
	m_timeMs += (std::uint32_t)elapsedMs;
	m_gwm.setTimeMs(m_timeMs);
	syncPlacements();
}

void AptGadgetLayer::syncPlacements()
{
	for (const WindowManager::ComponentRecord &rec : m_windows.components())
	{
		GameWindow *window = rec.window.get();
		if (!window)
		{
			continue;
		}
		auto it = m_placed.find(window);
		if (it == m_placed.end() || rec.instance == nullptr)
		{
			continue;
		}
		Placed &p = it->second;
		const AptCharacterInst &inst = *rec.instance;
		float b[4];
		if (WindowManager::stageBounds(inst, b))
		{
			if (b[0] != p.lastBounds[0] || b[1] != p.lastBounds[1] || b[2] != p.lastBounds[2] || b[3] != p.lastBounds[3])
			{
				window->winSetPosition((int)b[0], (int)b[1]);
				const bool dropListOpen = (BitTest(window->winGetStyle(), GWS_COMBO_BOX) && GadgetComboBoxGetListBox(window) && !GadgetComboBoxGetListBox(window)->winIsHidden())
					|| GadgetImageComboBoxIsOpen(window);
				if (!dropListOpen)
				{
					window->winSetSize((int)(b[2] - b[0]), (int)(b[3] - b[1]));
				}
				for (int i = 0; i < 4; ++i)
				{
					p.lastBounds[i] = b[i];
				}
			}
		}
		const bool visible = inst.globallyVisible();
		if (visible != p.shown)
		{
			window->winHide(!visible);
			p.shown = visible;
		}
	}
	m_gwm.processDestroyList();
}

void AptGadgetLayer::buildDrawList(GadgetDrawList &out)
{
	if (m_decorator)
	{
		// lane OPTS-3: under the gadgets. An image the mapped image collection lacks is reported (unresolvedImages: the renderer's gadget errors)
		// and not drawn, as the gadgets' own images are (GameWindowManager::winDrawImage)
		const std::size_t first = out.commands.size();
		m_decorator(out);
		for (std::size_t i = first; i < out.commands.size();)
		{
			GadgetDrawCommand &c = out.commands[i];
			if (c.kind == GadgetDrawCommand::Kind::Image && !m_skins.images.findImageByName(c.image))
			{
				out.noteUnresolved(c.image);
				out.commands.erase(out.commands.begin() + (std::ptrdiff_t)i);
				continue;
			}
			++i;
		}
	}
	const std::size_t firstWindow = out.commands.size();
	m_gwm.winRepaint(out);
	// lane OPTS-4: the open drop-down list (GameWindowManager::winGetOpenDropDown's rule) is drawn after every other gadget, whatever its place in the
	// window list: a row made later (nearer the list's head) drew its value and button through the list of the row above it. Its commands (its own
	// window's and its children's, found by the drawing window, so a combo box inside another window counts) move to the end as one block under its
	// own root, with the clip an ancestor had set when it began (the device keeps a clip until its end or the next root), and the input router takes
	// the pointer inside that clip only. INFERENCE (S-3503): RotWK draws at the placeholder's depth; promoting the open list is the port's
	GameWindow *drop = const_cast<GameWindow *>(openDropDown());
	m_gwm.setOpenDropDownClip(nullptr);
	if (!drop)
	{
		return;
	}
	std::vector<GadgetDrawCommand> kept, moved;
	std::optional<GadgetDrawCommand> clip; // the ancestors' clip in force (a ClipBegin of the same root not ended yet)
	for (std::size_t i = firstWindow; i < out.commands.size(); ++i)
	{
		GadgetDrawCommand &c = out.commands[i];
		const bool ofDrop = c.drawer && (c.drawer == drop || drop->winIsChild(const_cast<GameWindow *>(c.drawer)));
		if (!ofDrop)
		{
			if (clip && clip->root != c.root)
			{
				clip.reset();
			}
			if (c.kind == GadgetDrawCommand::Kind::ClipBegin)
			{
				clip = c;
			}
			else if (c.kind == GadgetDrawCommand::Kind::ClipEnd)
			{
				clip.reset();
			}
			kept.push_back(std::move(c));
			continue;
		}
		if (moved.empty() && clip && clip->root == c.root)
		{
			GadgetDrawCommand inherited = *clip;
			inherited.root = drop;
			IRegion2D r;
			r.lo.x = inherited.x0;
			r.lo.y = inherited.y0;
			r.hi.x = inherited.x1;
			r.hi.y = inherited.y1;
			m_gwm.setOpenDropDownClip(&r);
			moved.push_back(std::move(inherited));
		}
		c.root = drop;
		moved.push_back(std::move(c));
	}
	if (!moved.empty() && moved.front().kind == GadgetDrawCommand::Kind::ClipBegin && moved.back().kind != GadgetDrawCommand::Kind::ClipEnd)
	{
		GadgetDrawCommand end;
		end.kind = GadgetDrawCommand::Kind::ClipEnd;
		end.window = moved.front().window;
		end.root = drop;
		end.drawer = drop;
		moved.push_back(std::move(end));
	}
	out.commands.resize(firstWindow);
	for (GadgetDrawCommand &c : kept)
	{
		out.commands.push_back(std::move(c));
	}
	for (GadgetDrawCommand &c : moved)
	{
		out.commands.push_back(std::move(c));
	}
}

const GameWindow *AptGadgetLayer::openDropDown()
{
	// a modal box of a movie (DisableComponents() without a path, RW 0x81456F) takes every gadget's input: no list is above it
	if (m_windows.componentInputBlocked())
	{
		return nullptr;
	}
	return m_gwm.winGetOpenDropDown();
}

bool AptGadgetLayer::mouseMove(float x, float y)
{
	ICoord2D pos;
	pos.x = (int)x;
	pos.y = (int)y;
	// lane OPTS-6: the drag's step is the change of the integer position the windows see (ZH: the mouse's pixel minus the last one), so the dragged
	// thumb follows the pointer to the pixel. A window larger than the stage gives moves under one stage pixel each (2560 wide: 0.4 per window
	// pixel), and the truncated float difference of each move was 0: the thumb stayed behind while the pointer went far off the slider (the owner,
	// 2026-10-15)
	ICoord2D delta;
	delta.x = pos.x - (int)m_mouseX;
	delta.y = pos.y - (int)m_mouseY;
	m_mouseX = x;
	m_mouseY = y;
	if (m_leftDown)
	{
		return m_gwm.winProcessMouseEvent(GWM_LEFT_DRAG, &pos, &delta) == WIN_INPUT_USED;
	}
	return m_gwm.winProcessMouseEvent(GWM_MOUSE_POS, &pos, nullptr) == WIN_INPUT_USED;
}

bool AptGadgetLayer::mouseButton(bool down)
{
	ICoord2D pos;
	pos.x = (int)m_mouseX;
	pos.y = (int)m_mouseY;
	m_leftDown = down;
	const bool used = m_gwm.winProcessMouseEvent(down ? GWM_LEFT_DOWN : GWM_LEFT_UP, &pos, nullptr) == WIN_INPUT_USED;
	// [S-177] ZH closes an open combo box (the lone window) when the click lands on another window; in an APT screen the click may land
	// on no gadget at all (the Apt movie takes it), so a release that no gadget used closes the open list too
	if (!down && !used && m_gwm.winGetLoneWindow())
	{
		m_gwm.winSetLoneWindow(nullptr);
	}
	return used;
}

bool AptGadgetLayer::mouseWheel(int delta)
{
	ICoord2D pos;
	pos.x = (int)m_mouseX;
	pos.y = (int)m_mouseY;
	int wheel = delta;
	return m_gwm.winProcessMouseEvent(delta > 0 ? GWM_WHEEL_UP : GWM_WHEEL_DOWN, &pos, &wheel) == WIN_INPUT_USED;
}

bool AptGadgetLayer::key(int dikCode, bool down, int modifiers)
{
	const int state = (down ? KEY_STATE_DOWN : KEY_STATE_UP) | modifiers;
	return m_gwm.winProcessKey((std::uint8_t)dikCode, (std::uint8_t)state) == WIN_INPUT_USED;
}

bool AptGadgetLayer::textInput(char16_t ch)
{
	return m_gwm.winProcessChar(ch) == WIN_INPUT_USED;
}
