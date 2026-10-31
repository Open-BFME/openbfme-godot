// OpenBFME. GPL-3.0.
//
// Lane HUD-6: the parts of InGameHud that drive RotWK's side command bar gate, the radial command bubbles and the help box (see AptPalantirSideBar.cpp,
// ControlBarRadialMenu.h, CommandButtonHelp.h and InGameHelpBox.h for the target facts). Kept apart from InGameHud.cpp (lane UI-4 edits that file).

#include "GameClient/InGameHud.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/BfmeGame.h"
#include "Common/Player.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/UnitHelpSource.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <climits>
#include <cstdio>
#include <cstdlib>

namespace
{
bool imageSizeOf(void *ctx, const std::string &name, int &w, int &h)
{
	const MappedImageCollection *images = static_cast<const MappedImageCollection *>(ctx);
	const Image *img = images ? images->findImageByName(name) : nullptr;
	if (!img)
	{
		return false;
	}
	w = img->getImageWidth();
	h = img->getImageHeight();
	return true;
}

// the tooltip delay of the control bar's command windows: ControlBar.wnd's WINDOW "ControlBar.wnd:ButtonCommand" (the window the 33 command windows are
// made from, RW 0x71E1E7) and its TOOLTIPDELAY (WinInstanceData + 0x1C8 as RW 0x8075E4 reads it); false when the file or the window has none
bool commandWindowTooltipDelay(ArchiveFileSystem &fs, int &delay, std::string *error)
{
	const char *file = "window\\controlbar.wnd";
	std::vector<std::uint8_t> bytes;
	std::string readError;
	if (!fs.readFile(file, bytes, &readError))
	{
		*error = std::string(file) + ": " + readError;
		return false;
	}
	const std::string text(bytes.begin(), bytes.end());
	const size_t name = text.find("\"ControlBar.wnd:ButtonCommand\"");
	if (name == std::string::npos)
	{
		*error = std::string(file) + ": no window ControlBar.wnd:ButtonCommand";
		return false;
	}
	const size_t key = text.find("TOOLTIPDELAY", name);
	const size_t next = text.find("WINDOW", name);
	if (key == std::string::npos || (next != std::string::npos && key > next))
	{
		*error = std::string(file) + ": ControlBar.wnd:ButtonCommand has no TOOLTIPDELAY";
		return false;
	}
	const size_t eq = text.find('=', key);
	delay = std::atoi(text.c_str() + eq + 1);
	return true;
}

// lane DEVPORT-8 (FALSEDEF-1): the WINDOW block of ControlBar.wnd:WinUAttack (see InGameHud.h). The file is read as the WND loader would (ZH
// GameWindowManagerScript.cpp: SCREENRECT positional, STATUS a '+' list, the draw data's first IMAGE); a file without the window leaves `present` false
// (retail's lookup gives NULL and the blink does nothing); a block that does not parse is an error naming the field
bool readAttackWindow(ArchiveFileSystem &fs, InGameHud::AttackWindow &w, std::string *error)
{
	const char *file = "window\\controlbar.wnd";
	std::vector<std::uint8_t> bytes;
	std::string readError;
	if (!fs.readFile(file, bytes, &readError))
	{
		*error = std::string(file) + ": " + readError;
		return false;
	}
	w = InGameHud::AttackWindow();
	const std::string text(bytes.begin(), bytes.end());
	const char *kName = "\"ControlBar.wnd:WinUAttack\"";
	const size_t name = text.find(kName);
	if (name == std::string::npos)
	{
		return true;
	}
	// the block: from the WINDOW line before the name to the END line after it (the window has no children in the retail file; a CHILD would end the
	// block early, so it is refused)
	auto lineIs = [&text](size_t at, const char *word) {
		size_t b = at, e = text.find('\n', at);
		e = e == std::string::npos ? text.size() : e;
		while (b < e && (text[b] == ' ' || text[b] == '\t'))
		{
			++b;
		}
		while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r'))
		{
			--e;
		}
		return text.compare(b, e - b, word) == 0;
	};
	size_t begin = std::string::npos;
	for (size_t at = text.rfind('\n', name); at != std::string::npos && at > 0; at = text.rfind('\n', at - 1))
	{
		if (lineIs(at + 1, "WINDOW"))
		{
			begin = at + 1;
			break;
		}
	}
	size_t end = std::string::npos;
	for (size_t at = text.find('\n', name); at != std::string::npos; at = text.find('\n', at + 1))
	{
		if (lineIs(at + 1, "CHILD"))
		{
			*error = std::string(file) + ": ControlBar.wnd:WinUAttack has children (not read)";
			return false;
		}
		if (lineIs(at + 1, "END"))
		{
			end = at + 1;
			break;
		}
	}
	if (begin == std::string::npos || end == std::string::npos)
	{
		*error = std::string(file) + ": ControlBar.wnd:WinUAttack is not a closed WINDOW block";
		return false;
	}
	const std::string block = text.substr(begin, end - begin);
	auto field = [&block](const char *key, std::string &value) {
		const size_t k = block.find(std::string(key) + " =");
		if (k == std::string::npos)
		{
			return false;
		}
		const size_t eq = block.find('=', k), semi = block.find(';', eq);
		if (semi == std::string::npos)
		{
			return false;
		}
		value = block.substr(eq + 1, semi - eq - 1);
		return true;
	};
	std::string status, rect, enabled, disabled;
	if (!field("STATUS", status) || !field("SCREENRECT", rect) || !field("ENABLEDDRAWDATA", enabled) || !field("DISABLEDDRAWDATA", disabled))
	{
		*error = std::string(file) + ": ControlBar.wnd:WinUAttack lacks STATUS, SCREENRECT or its draw data";
		return false;
	}
	for (size_t b = 0; b <= status.size();)
	{
		size_t e = status.find('+', b);
		e = e == std::string::npos ? status.size() : e;
		std::string bit = status.substr(b, e - b);
		bit.erase(0, bit.find_first_not_of(" \t\r\n"));
		bit.erase(bit.find_last_not_of(" \t\r\n") + 1);
		w.imageStatus = w.imageStatus || bit == "IMAGE";
		w.hidden = w.hidden || bit == "HIDDEN";
		w.enabledStatus = w.enabledStatus || bit == "ENABLED";
		b = e + 1;
	}
	if (std::sscanf(rect.c_str(), " UPPERLEFT: %d %d, BOTTOMRIGHT: %d %d, CREATIONRESOLUTION: %d %d", &w.x0, &w.y0, &w.x1, &w.y1, &w.resX, &w.resY) != 6 ||
		w.resX <= 0 || w.resY <= 0)
	{
		*error = std::string(file) + ": ControlBar.wnd:WinUAttack's SCREENRECT does not parse";
		return false;
	}
	auto firstImage = [](const std::string &data, std::string &out) {
		char image[256] = {};
		if (std::sscanf(data.c_str(), " IMAGE: %255[^,]", image) != 1)
		{
			return false;
		}
		out = image;
		out.erase(out.find_last_not_of(" \t\r\n") + 1);
		if (out == "NoImage")
		{
			out.clear();
		}
		return true;
	};
	if (!firstImage(enabled, w.enabledImage) || !firstImage(disabled, w.disabledImage))
	{
		*error = std::string(file) + ": ControlBar.wnd:WinUAttack's draw data has no IMAGE";
		return false;
	}
	w.present = true;
	return true;
}
} // namespace

std::string InGameHud::attackWindowImage() const
{
	// W3DGameWinDefaultDraw (ZH), the image branch: the disabled image when the window is not enabled, else the enabled one (INFERENCE: the hilite image
	// of a hovered window is not used; the window takes no input in RotWK's HUD). A hidden window, or one without the IMAGE status, draws no image
	const ControlBar::AttackFlash &f = m_bar->attackFlash();
	if (!m_attackWindow.present || m_attackWindow.hidden || !m_attackWindow.imageStatus)
	{
		return std::string();
	}
	return f.windowEnabled ? m_attackWindow.enabledImage : m_attackWindow.disabledImage;
}

bool InGameHud::attackWindowRect(int width, int height, int &x, int &y, int &w, int &h) const
{
	// the WND loader's scale: the display resolution over the window's CREATIONRESOLUTION (ZH parseScreenRect)
	if (!m_attackWindow.present)
	{
		return false;
	}
	const float sx = (float)width / (float)m_attackWindow.resX, sy = (float)height / (float)m_attackWindow.resY;
	x = (int)((float)m_attackWindow.x0 * sx);
	y = (int)((float)m_attackWindow.y0 * sy);
	w = (int)((float)m_attackWindow.x1 * sx) - x;
	h = (int)((float)m_attackWindow.y1 * sy) - y;
	return true;
}

void InGameHud::bootHud6()
{
	std::string error;
	if (!HelpBoxSettings::load(m_config.fs, m_helpSettings, &error))
	{
		m_hud6Errors.push_back("help box settings: " + error);
	}
	{
		// the money texts' timings (GameClient/FloatingText.h): InGameUI FloatingTextTimeOut / MoveUpSpeed / VanishRate
		m_floatingText.disable();
		FloatingTextSettings floating;
		if (FloatingTextSettings::load(m_config.fs, floating, &error))
		{
			m_floatingText.setSettings(floating);
		}
		else
		{
			m_floatingTextErrors.push_back("settings: " + error);
		}
	}
	if (!commandWindowTooltipDelay(m_config.fs, m_tooltipDelayMs, &error))
	{
		m_hud6Errors.push_back("help box delay: " + error);
	}
	// lane DEVPORT-8 (FALSEDEF-1): the blink's window (RW 0x71E1E7 at RW 0x71E8C4: winGetWindowFromId(ControlBar.wnd:WinUAttack) into ControlBar + 0x2A0)
	if (!readAttackWindow(m_config.fs, m_attackWindow, &error))
	{
		m_hud6Errors.push_back("attack blink window: " + error);
	}
	m_bar->setAttackFlashWindow(m_attackWindow.present, m_attackWindow.enabledStatus);
	m_radial.setImageSize(imageSizeOf, (void *)nullptr);
	// lane OWNERFIX-3: the command windows' count look (ControlBar.wnd:ButtonCommand's FONT and TEXTCOLOR); a failure is reported (S-2703), counts are then
	// not drawn and the ring reports why (ControlBarRadialMenu::errors)
	{
		std::vector<std::uint8_t> bytes;
		std::string readError;
		CommandWindowLook look;
		if (!m_config.fs.readFile("window\\controlbar.wnd", bytes, &readError))
		{
			m_hud6Errors.push_back("command window look: window\\controlbar.wnd: " + readError);
		}
		else if (!CommandWindowLook::scan(std::string(bytes.begin(), bytes.end()), look, &readError))
		{
			m_hud6Errors.push_back("command window look: " + readError);
		}
		m_radial.setCountLook(look);
	}
	AptPalantir *palantir = m_palantirRef;
	m_palantirRef->setHelpBoxHandler([this, palantir](const std::string &clip, bool loaded) {
		if (loaded)
		{
			m_helpBox.loaded(clip, [palantir](const std::string &path, const std::string &fn, const std::vector<std::string> &args) {
				return palantir->invoke(path, fn, args);
			});
		}
		else
		{
			m_helpBox.unloaded();
		}
	});
	if (isBfme1Data(m_config.fs))
	{
		// BFME1 has no OnHelpBoxLoaded callback/content-frame protocol.
		// Its native tooltip is drawn by the device over the HUD.
		m_helpBox.loaded("_level" + std::to_string(palantir->movieLevel()) + ".BFME1Help", [](const auto &, const auto &, const auto &) { return true; });
	}

}

bool InGameHud::hoveredAptButton(InGameHelpBox::Provider &p, ControlBarButton &b)
{
	AptButtonInst *button = m_wm ? m_wm->apt().input().currentButton() : nullptr;
	bool arc = false;
	int slot = -1;
	if (!button || !m_palantirRef || !m_palantirRef->contentSlotAt(button->targetPath(), arc, slot))
	{
		return false;
	}
	for (const ControlBarButton &c : arc ? m_bar->palantirButtons() : m_bar->sideButtons())
	{
		if (c.slot == slot)
		{
			p.bar = arc ? 0 : 1;
			p.slot = slot;
			p.button = c.button;
			b = c;
			return true;
		}
	}
	return false;
}

// lane HOVER-1 (S-4612): the spell book's slot handler (RW 0x930DC4: the slot's command button through RW 0x807A00(0, button)), the hero bar's slot handler
// (RW 0x92BF34: the builder slot "NonCommand_SelectNearestBuilder", a hero's UnitHelpSource) and its select-all handler (RW 0x92C265:
// "NonCommand_SelectAllHeroes"). INFERENCE: the hovered button is the Apt player's current button below the slot's clip (retail keys its over-button
// handlers by "Palantir/<clip>/Spell%d/" and "<clip>/Hero%d/", RW 0x931698 / 0x92DFDE)
bool InGameHud::hoveredHelpButton(InGameHelpBox::Provider &p, ControlBarButton &b, ObjectID &unit, ObjectID context)
{
	AptButtonInst *button = m_wm ? m_wm->apt().input().currentButton() : nullptr;
	if (!button || !m_palantirRef)
	{
		return false;
	}
	const std::string path = button->targetPath();
	auto under = [&path](const std::string &clip) {
		return path == clip || (path.size() > clip.size() && path.compare(0, clip.size(), clip) == 0 && path[clip.size()] == '.');
	};
	auto commandHelp = [&p, &b](const CommandButton *cb) {
		if (!cb)
		{
			return false; // RW 0x92BF98 / 0x930DD2: no button, no help
		}
		b = ControlBarButton();
		b.button = cb;
		b.textLabel = cb->m_textLabel.empty() ? std::string() : cb->m_textLabel.front();
		b.descriptLabel = cb->m_descriptLabel.empty() ? std::string() : cb->m_descriptLabel.front();
		p.bar = 3;
		p.slot = -1;
		p.button = cb;
		return true;
	};
	// the Palantir's portrait (PalantirCommandUI's over-button handler "CommandUI/PortraitBackground", BFME2 decomp PalantirCommandInterface.cpp, RW 0x93099D:
	// Rva00529F3DMethod.cpp, tier A): the portrait's object (+ 0x30), when it exists and the local player is not active or is its ally (RW 0x6A8803:
	// isPlayerActive RW 0x6AAC52, then the relationship to the object's team == ALLIES), shows its UnitHelpSource
	if (under(m_palantirRef->portraitKey().substr(0, m_palantirRef->portraitKey().rfind('.')) + ".PortraitBackground"))
	{
		const Object *o = context != INVALID_ID ? m_config.game.logic().findObjectByID(context) : nullptr;
		const Player *local = m_config.game.players().getLocalPlayer();
		const bool active = local && !local->isObserver() && !local->isDefeated();
		if (!o || !local || (active && local->getRelationship(o->getTeam()) != ALLIES))
		{
			return false;
		}
		p.bar = 4;
		p.slot = -1;
		p.button = nullptr;
		p.object = (std::uint32_t)context;
		unit = context;
		return true;
	}
	const std::string spellBook = m_palantirRef->spellBookTarget();
	if (!spellBook.empty())
	{
		for (int i = 0; i < AptPalantir::kSpellSlots; ++i)
		{
			if (!under(spellBook + ".Spell" + std::to_string(i + 1)))
			{
				continue;
			}
			const int index = m_palantirRef->spellSlotButton(i); // the slot table (+ 0x54, 0x14 bytes a slot)
			for (const InGameSpellBookModel::Button &sb : m_spellBar.buttons())
			{
				if (sb.index == index)
				{
					return commandHelp(sb.button);
				}
			}
			return false;
		}
	}
	if (m_heroSelect && m_heroSelect->attached())
	{
		const std::string prefix = m_heroSelect->commandPrefix();
		if (under(prefix + ".SelectAllHeroesBttn"))
		{
			return commandHelp(TheCommandStore ? TheCommandStore->findCommandButton("NonCommand_SelectAllHeroes") : nullptr);
		}
		for (int i = 0; i < InGameHeroSelect::kSlots; ++i)
		{
			if (!under(prefix + ".Hero" + std::to_string(i + 1)))
			{
				continue;
			}
			const InGameHeroSelect::Slot &slot = m_heroSelect->slot(i);
			if (slot.builder) // + 0x16
			{
				return commandHelp(TheCommandStore ? TheCommandStore->findCommandButton("NonCommand_SelectNearestBuilder") : nullptr);
			}
			if (slot.hero == INVALID_ID) // the slot's iterator is the list's end
			{
				return false;
			}
			p.bar = 4;
			p.slot = i;
			p.button = nullptr;
			p.object = (std::uint32_t)slot.hero;
			unit = slot.hero;
			return true;
		}
	}
	return false;
}

void InGameHud::updateHud6(ObjectID context, const std::string &faction, double seconds)
{
	m_hud6ClockMs += (std::uint32_t)(seconds * 1000.0); // the help's hover clock (retail: timeGetTime)
	GameLogic &logic = m_config.game.logic();
	Player *local = m_config.game.players().getLocalPlayer();
	Object *ctx = context != INVALID_ID ? logic.findObjectByID(context) : nullptr;
	// the side command bar's gate (RW 0x92F27D): a DOZER the local player controls
	m_palantirRef->setSideBarObject(ctx && local && ctx->isKindOfName("DOZER") && ctx->getControllingPlayer() == local);
	// the radial ring (RW 0x9443CF: the object is locally controlled, or the local player is not active, RW 0x6AAC52; lane OWNERFIX-3: the second half
	// was an inference before, now ported: an observer or a defeated player sees the ring of the object it selects; another player's object, otherwise, none
	// (RW 0x945938))
	const bool allowed = ctx && (ctx->getControllingPlayer() == local || !ControlBar::localPlayerActive(local));
	m_radial.setImageSize(imageSizeOf, (void *)m_hud6Images);
	m_radial.setFontMetrics(m_hud6Metrics); // lane OWNERFIX-3: the count's measure (the device's fonts)
	m_radial.update(logic, &m_config.game.drawables(), m_config.view, m_windowW, m_windowH, context, m_bar->sideButtons(), allowed);
	// the help box: the control bar's test of the last frame, the hover of this one, the movie clip's update
	m_helpBox.setEndGameTimer(m_config.endGameTimer ? m_config.endGameTimer() : -1);
	// RW 0x91988F / 0x919158: the disconnect menu owns this registration for its whole lifetime, including before movie initialization.
	m_helpBox.setDisconnectMenuPresent(m_wm && m_wm->hasScreenRef("DisconnectScreen::InitGadgets"));
	m_helpBox.beginFrame();
	InGameHelpBox::Provider p;
	ControlBarButton hovered;
	bool have = false;
	for (const ControlBarRadialMenu::Button &rb : m_radial.buttons())
	{
		if (m_radial.hilitedSlot() >= 0 && rb.slot == m_radial.hilitedSlot())
		{
			p.bar = 2;
			p.slot = rb.slot;
			p.button = rb.source.button;
			hovered = rb.source;
			have = true;
		}
	}
	if (!have)
	{
		have = hoveredAptButton(p, hovered);
	}
	// lane CAMPUI-1: a stance item of the toggle's radial: its help (RW 0x96F33E) is the toggle's TextLabel / DescriptLabel at the item stance's index in the
	// toggle's Stances (RW 0x96F2DA), made as a command button help with no cost (RW 0x975070 with empty cost / command point texts); provider kind 5, keyed
	// by the stance. INFERENCE: its delay is the arc's (the item provider's vslot 2 was not read)
	AptPalantir::StanceItemHit stanceItem;
	if (!have && m_wm && m_palantirRef)
	{
		AptButtonInst *button = m_wm->apt().input().currentButton();
		if (button && m_palantirRef->stanceItemAt(button->targetPath(), stanceItem))
		{
			p.bar = 5;
			p.slot = stanceItem.slot;
			p.button = stanceItem.toggle;
			p.object = (std::uint32_t)stanceItem.stance;
			have = true;
		}
	}
	ObjectID unit = INVALID_ID;
	if (!have)
	{
		have = hoveredHelpButton(p, hovered, unit, context); // lane HOVER-1
	}
	const std::vector<ObjectID> &selected = m_input->ui().selected();
	if (have)
	{
		Object *selection = !selected.empty() ? logic.findObjectByID(selected.front()) : nullptr; // TheInGameUI vslot 0x12C: the first selected drawable's object
		Player *player = selection ? selection->getControllingPlayer() : local;                  // RW 0x807AE9 / 0x807AF3
		const MappedImageCollection *images = m_hud6Images;
		const GameTextSource *text = m_config.gameText;
		// the provider's delay (vslot 2): a command button's is its window's TOOLTIPDELAY (+ 0x1C8), 0 without a window (RW 0x8075E4, lane ROTWK-C), so the spell
		// book's and the hero bar's command buttons (bar 3) show at once; a UnitHelpSource's (bar 4) is its + 0x08 (RW 0x9F9F16, its value not read: S-2703)
		const int delay = p.bar == 3 ? 0 : m_tooltipDelayMs;
		m_helpBox.showHelp(p, m_hud6ClockMs, delay, [&]() -> std::shared_ptr<CommandButtonHelp> {
			if (p.bar == 4)
			{
				// RW 0x8EBC61: a gone object makes no help
				const Object *o = logic.findObjectByID(unit);
				if (!o)
				{
					return nullptr;
				}
				const UnitHelpSource::Text u = UnitHelpSource::compose(*o, text, logic, local);
				return std::make_shared<CommandButtonHelp>(CommandButtonHelp::simple(u.title, u.text, u.unported));
			}
			if (p.bar == 5)
			{
				CommandButtonHelp::Text t;
				const size_t k = stanceItem.listIndex < 0 ? (size_t)-1 : (size_t)stanceItem.listIndex;
				const CommandButton &toggle = *stanceItem.toggle;
				// RW 0x75CE90 / 0x75CEFC: the label of the slot (an index past the lists, or a stance not in them, gives the missing-label text)
				t.name = fetchOrMissing(text, k < toggle.m_textLabel.size() ? toggle.m_textLabel[k] : std::string());
				t.description = fetchOrMissing(text, k < toggle.m_descriptLabel.size() ? toggle.m_descriptLabel[k] : std::string());
				if (k >= toggle.m_textLabel.size() || k >= toggle.m_descriptLabel.size())
				{
					t.unported.push_back("stance " + std::to_string(stanceItem.stance) + " has no label slot in " + toggle.m_name);
				}
				return std::make_shared<CommandButtonHelp>(t, text, imageSizeOf, (void *)images);
			}
			CommandButtonHelp::Text t = CommandButtonHelp::compose(hovered, text, logic, player, local, selection, faction, &selected);
			return std::make_shared<CommandButtonHelp>(t, text, imageSizeOf, (void *)images);
		});
	}
	FontMetricsSource &metrics = m_hud6Metrics ? *m_hud6Metrics : m_hud6DefaultMetrics;
	AptStageMapping m;
	m.windowW = (float)m_windowW;
	m.windowH = (float)m_windowH;
	float sx0, sy0, sx1, sy1;
	m.windowToStage(0.0f, 0.0f, sx0, sy0);
	m.windowToStage((float)m_windowW, (float)m_windowH, sx1, sy1);
	const float stagePerPixelY = (sy1 - sy0) / (float)m_windowH;
	const float pixelPerStageX = (float)m_windowW / (sx1 - sx0), pixelPerStageY = (float)m_windowH / (sy1 - sy0);
	const float fontScale = pixelPerStageX < pixelPerStageY ? pixelPerStageX : pixelPerStageY;
	const HelpBoxSettings &settings = m_helpSettings;
	m_helpBox.update(
		[&](CommandButtonHelp &h, int width) { return h.setWidthAndComputeHeight(width, settings, fontScale, pixelPerStageX, pixelPerStageY, metrics); },
		stagePerPixelY);
}

void InGameHud::helpBoxRender(float x, float y, float w, float h, std::vector<HelpDrawOp> &out)
{
	m_helpBox.render(x, y, w, h, out);
}

bool InGameHud::hud6OverGui(int x, int y) const
{
	// the ring belongs to the control bar's context (RW 0x71D993: a context switch hands the ring its new object at once); a selection changed since the
	// last update has no ring yet
	const std::vector<ObjectID> &selected = m_input->ui().selected();
	if (selected.size() != 1 || selected.front() != m_radial.object())
	{
		return false;
	}
	return m_radial.hit(x, y) >= 0;
}

// lane INTEG-6 (S-3507): an Apt button under the pointer (the quit menu's, Options', the Palantir's) takes the pointer before the radial bubbles under it,
// as it takes it before the world (isOverGui). DONOR: ZH's message stream runs TheWindowManager's WindowTranslator ahead of the in-game UI's translators;
// MENU-1 draws the bubbles under the menus by the same donor order. INFERENCE: RotWK's radial input order was not read
bool InGameHud::aptButtonAt(int x, int y)
{
	if (!m_booted)
	{
		return false;
	}
	AptStageMapping m;
	m.windowW = (float)m_windowW;
	m.windowH = (float)m_windowH;
	float sx, sy;
	m.windowToStage((float)x, (float)y, sx, sy);
	return m_wm->apt().input().hitTestButtons(sx, sy) != nullptr;
}

void InGameHud::hud6MouseMove(int x, int y)
{
	if (aptButtonAt(x, y))
	{
		m_radial.mouseMove(INT_MIN / 2, INT_MIN / 2); // lane INTEG-6: no bubble is hovered under a menu's button (no help box over the pause menu)
		return;
	}
	m_radial.mouseMove(x, y);
}

bool InGameHud::hud6MouseButton(HudInput::Button button, bool down, int x, int y)
{
	if (button != HudInput::Button::Left && button != HudInput::Button::Right)
	{
		return false;
	}
	if (down && (!hud6OverGui(x, y) || aptButtonAt(x, y))) // lane INTEG-6: a menu's button over a bubble takes the press
	{
		return false;
	}
	return m_radial.mouseButton(button == HudInput::Button::Left, down, x, y, *m_bar, (m_keyState & KEY_STATE_SHIFT) != 0); // a release is the ring's only after a press it took
}

void InGameHud::hud6Stops(std::vector<std::string> &out) const
{
	for (const std::string &l : ControlBarRadialMenu::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : CommandButtonHelp::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : InGameHelpBox::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : UnitHelpSource::acceptanceStops())
	{
		out.push_back(l);
	}
	out.push_back("[S-2700] side command bar (HUD-6): ported RW 0x92F27D's gate (a DOZER the local player controls) and RW 0x92F082 / 0x92F015's SetButtonState "
				  "_show / _hide of the frames; INFERENCE: the local player's + 0x770 is 0, a frame whose window changed keeps its content (updated in place)");
	for (const std::string &e : m_hud6Errors)
	{
		out.push_back("[S-2703] " + e);
	}
	for (const std::string &l : FloatingTextList::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &e : m_floatingTextErrors)
	{
		out.push_back("[S-283] floating text: " + e);
	}
}
