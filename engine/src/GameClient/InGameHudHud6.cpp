#include "Common/BuildAssistant.h"
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
} // namespace

void InGameHud::bootHud6()
{
	std::string error;
	if (!HelpBoxSettings::load(m_config.fs, m_helpSettings, &error))
	{
		m_hud6Errors.push_back("help box settings: " + error);
	}
	if (!commandWindowTooltipDelay(m_config.fs, m_tooltipDelayMs, &error))
	{
		m_hud6Errors.push_back("help box delay: " + error);
	}
	m_radial.setImageSize(imageSizeOf, (void *)nullptr);
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
	m_palantirRef->setSideBarObject(ctx && local && (ctx->isKindOfName("DOZER") || BuildAssistant::isFortressBuilder(*ctx)) && ctx->getControllingPlayer() == local);
	// the radial ring (RW 0x9443CF: the object is locally controlled, or the local player is not active; INFERENCE: a skirmish's local player is active)
	const bool allowed = ctx && local && ctx->getControllingPlayer() == local;
	m_radial.setImageSize(imageSizeOf, (void *)m_hud6Images);
	m_radial.update(logic, &m_config.game.drawables(), m_config.view, m_windowW, m_windowH, context, m_bar->radialButtons(), allowed);
	// the help box: the control bar's test of the last frame, the hover of this one, the movie clip's update
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
	ObjectID unit = INVALID_ID;
	if (!have)
	{
		have = hoveredHelpButton(p, hovered, unit, context); // lane HOVER-1
	}
	// Controller focus feeds the same provider, delay, composition and movie as mouse hover.
	if (m_controllerHoverSlot >= 0)
	{
		have = false;
		const auto &buttons = m_controllerHoverArc ? m_bar->palantirButtons() : m_bar->sideButtons();
		for (const auto &b : buttons) if (b.slot == m_controllerHoverSlot && b.button)
		{
			hovered = b; p.bar = m_controllerHoverArc ? 0 : 1; p.slot = b.slot; p.button = b.button; have = true; break;
		}
	}
	else if (m_controllerHoverUnit != INVALID_ID)
	{
		unit = m_controllerHoverUnit; p.bar = 4; p.object = unit; have = true;
	}
	else if (m_controllerHoverSpell >= 0)
	{
		have = false;
		const int index = m_palantirRef ? m_palantirRef->spellSlotButton(m_controllerHoverSpell) : -1;
		for (const auto &sb : m_spellBar.buttons()) if (sb.index == index && sb.button)
		{
			const CommandButton *button = sb.button;
			hovered = ControlBarButton(); hovered.button = button;
			hovered.textLabel = button->m_textLabel.empty() ? "" : button->m_textLabel.front();
			hovered.descriptLabel = button->m_descriptLabel.empty() ? "" : button->m_descriptLabel.front();
			p.bar = 3; p.button = button; have = true; break;
		}
	}
	const std::vector<ObjectID> &selected = m_input->ui().selected();
	if (have)
	{
		Object *selection = !selected.empty() ? logic.findObjectByID(selected.front()) : nullptr; // TheInGameUI vslot 0x12C: the first selected drawable's object
		Player *player = selection ? selection->getControllingPlayer() : local;                  // RW 0x807AE9 / 0x807AF3
		const MappedImageCollection *images = m_hud6Images;
		const GameTextSource *text = m_config.gameText;
		m_helpBox.showHelp(p, m_hud6ClockMs, m_tooltipDelayMs, [&]() -> std::shared_ptr<CommandButtonHelp> {
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
	return m_radial.mouseButton(button == HudInput::Button::Left, down, x, y, *m_bar); // a release is the ring's only after a press it took
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
}
