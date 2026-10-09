// OpenBFME. GPL-3.0.
// See InGameHud.h.

#include "GameClient/InGameHud.h"

#include "Common/Audio/AudioEntryPoints.h"

#include "GameClient/AptCanvas.h"
#include "GameClient/CursorFile.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/WeaponSetToggle.h"
#include "GameClient/PalantirCommandUI.h"
#include "GameClient/MapClassification.h"
#include "Common/AsciiString.h"
#include "GameClient/LiveGame.h"
#include "GameClient/Drawable.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/StealthLook.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameMessage.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>

InGameHud::InGameHud(Config config)
	: m_config(config)
	, m_source(config.fs)
{
	if (m_config.windows && m_config.shell)
	{
		m_wm = m_config.windows;
		m_shellRef = m_config.shell;
	}
	else
	{
		m_ownedWindows = std::make_unique<WindowManager>(m_source, m_config.services);
		m_ownedShell = std::make_unique<Shell>(*m_ownedWindows, m_factories, m_config.services, m_environment);
		m_wm = m_ownedWindows.get();
		m_shellRef = m_ownedShell.get();
	}
	m_environment.gameText = m_config.gameText;
	m_input = std::make_unique<HudInput>(m_config.game.logic(), &m_config.game.ai(), m_config.view, m_config.game.commands(), m_config.mouse, m_config.metaMap);
	m_input->context().commands = &m_config.world.commands();
	// lane QA-1: the pick tests the ray against the drawn models (GameClient/DrawablePick: ZH W3DView::pickDrawable casts it at the scene's render objects);
	// an object without a drawable has nothing in the scene and cannot be picked. The geometry pick of S-284 selected the castle shell (MenFortress, Model =
	// None) for a click on its citadel and a tower in front of a barracks for a click on the barracks.
	// What the device layer does not draw is not in the scene either (GodotGameWorld's fogHidden rule from the presented snapshot: SHROUDED for the local player,
	// invisible to it, or a drawable a container hid), and a FOGGED building is only a ghost: a click on an enemy building in the shroud or the fog is a click on
	// the ground (the army moves there), not an attack order the logic then refuses (S-565: a human's units do not attack a FOGGED or SHROUDED object).
	m_input->context().pickRay = [this](const Object &o, const Coord3D &origin, const Coord3D &dir, float *t) {
		const Drawable *d = m_config.game.drawables().findByObject(o.getID());
		if (!d)
		{
			return DrawablePick::Result::NotDrawn;
		}
		if (const std::shared_ptr<const LogicSnapshot> snap = m_config.game.presentedSnapshot())
		{
			if (const ObjectSnapshot *rec = snap->find(o.getID()))
			{
				// a FOGGED object is retail's ghost: W3DGhostObject draws a snapshot of the render object without the drawable's user data, so the pick
				// meets no drawable there (ZH W3DGhostObject; the ghosts themselves are S-561). The device here still draws the real drawable.
				if (rec->shroudedForLocal || rec->objectShroud >= (int)OBJECTSHROUD_FOGGED || rec->drawableHidden || StealthLook::hidden(*rec))
				{
					return DrawablePick::Result::NotDrawn;
				}
			}
		}
		return DrawablePick::rayTest(*d, origin, dir, t);
	};
	m_bar = std::make_unique<ControlBar>(m_input->context());
	m_radar = std::make_unique<Radar>(m_input->context());
	m_input->commandTranslator().setRadar(m_radar.get()); // lane HUD-4: Space (VIEW_LAST_RADAR_EVENT)
	// AUDIO-2: ZH CommandXlat / SelectionXlat play the unit voice when a command or a selection leaves the translators (RotWK RW 0x8DEDBB)
	m_voice = std::make_unique<UnitVoiceResponse>(m_config.game.logic(), m_voiceSink);
	// the crowd responses of the voices (S-700): Data\INI\CrowdResponse.ini; a missing or malformed file is an error of the HUD, the voices still work
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!m_config.fs.readFile("Data\\INI\\CrowdResponse.ini", bytes, &error) ||
			!m_voice->parseCrowdResponses(std::string(bytes.begin(), bytes.end()), &error))
		{
			m_errors.push_back("unit voice: CrowdResponse.ini: " + error);
		}
	}
	m_input->setLogicMessageObserver([this](const GameMessage &msg) {
		if (const Player *local = m_input->context().localPlayer())
		{
			m_voice->setLocalPlayerIndex(local->getPlayerIndex());
		}
		if (AudioManager *audio = AudioApi::current())
		{
			m_voice->setMinDelayBetweenEnterStateVoiceFrames(audio->ini().settings.minDelayBetweenEnterStateVoiceFrames);
		}
		PlayUnitVoiceForMessage(*m_voice, m_input->ui().selected(), msg);
	});
	// the logic's voice events (ProductionUpdate's VoiceCreated RW 0x8A2A48, ...): the made object alone is the "selection" (RW passes its drawable list)
	AudioApi::installUnitVoiceHandler([this](int voiceEvent, std::uint32_t objectId, std::uint32_t producerId) {
		if (const Player *local = m_input->context().localPlayer())
		{
			m_voice->setLocalPlayerIndex(local->getPlayerIndex());
		}
		UnitVoiceResponse::Info info;
		info.producer = (ObjectID)producerId;
		m_voice->pickAndPlay({ (ObjectID)objectId }, voiceEvent, &info);
	}, this);
}

InGameHud::~InGameHud()
{
	// lane SPELL-2 (review r2): an open spell store is a screen of the window manager below: it goes first, without sending its pending purchases
	m_store.reset();
	// the screen before the window manager it registered on
	m_ownedPalantir.reset();
	m_palantirRef = nullptr;
	m_radar.reset();
	m_bar.reset();
	AudioApi::uninstallUnitVoiceHandler(this); // only this HUD's registration (another live HUD keeps its own)
	m_input.reset();
	m_voice.reset();
	m_ownedShell.reset();
	m_ownedWindows.reset();
}

bool InGameHud::boot(std::string *error)
{
	if (m_ownedWindows)
	{
		m_ownedWindows->init();
		m_ownedPalantir = std::make_unique<AptPalantir>(*m_ownedWindows, *m_ownedShell);
		m_palantirRef = m_ownedPalantir.get();
	}
	else
	{
		// the shell-hosted form: the game scene pushed Palantir.apt; its screen is the factory's AptPalantir
		m_palantirRef = dynamic_cast<AptPalantir *>(m_shellRef->findScreenByFilename("Palantir.apt"));
	}
	if (!m_palantirRef)
	{
		m_errors.push_back("Palantir.apt is not on the shell's stack as an AptPalantir");
		if (error)
		{
			*error = m_errors.back();
		}
		return false;
	}
	if (m_palantirRef->level() < 0)
	{
		m_errors.push_back("Palantir.apt was refused a window slot");
		return false;
	}
	m_palantirRef->setButtonHandler([this](int slot, bool inPalantir) {
		const auto worldContext = enterContext(); // the movie's own update and invoke_at reach this outside the scoped entry points (BuildAssistant reads TheCommandStore)
		m_bar->pressButton(slot, inPalantir);
	});
	// lane SPELL-2: the spell book's press and the store button of the Palantir
	m_palantirRef->setSpellPressHandler([this](int buttonIndex) { pressSpell(buttonIndex); });
	m_palantirRef->setSpellStoreHandler([this]() { openSpellStore(); });
	m_radar->setupFromTerrain();
	{
		// lane HUD-2: the map's <stem>_art.tga is the radar picture when the archives have it (W3DRadar::buildTerrainTexture RW 0x44F3AB)
		std::string artError;
		const std::string mapFile = MapClassification::mapPath(AsciiStringUtil::lowered(m_config.game.report().map));
		if (!m_radar->loadArt(m_config.fs, mapFile, &artError))
		{
			m_errors.push_back(artError);
		}
	}
	if (Player *local = m_config.game.players().getLocalPlayer())
	{
		m_palantirRef->setEvil(local->getPlayerTemplate() && local->getPlayerTemplate()->m_evil);
	}
	m_booted = true;
	if (m_ownedWindows)
	{
		m_wm->update(33); // the movie loads on the next update
	}
	return true;
}

void InGameHud::setWindowSize(int width, int height)
{
	m_windowW = std::max(1, width);
	m_windowH = std::max(1, height);
}

std::unique_ptr<RetailObjectWorld::ContextScope> InGameHud::enterContext()
{
	m_config.game.waitIdle();
	return m_config.world.enterContext();
}

void InGameHud::update(double seconds)
{
	// SMOOTH-1 (S-810): the per-frame update reads the live game (selection, control bar, production, the movie's callbacks); while the logic worker runs
	// a frame it is skipped for this render frame instead of waiting (the next render frame does it), so a long logic frame never stalls the render
	if (!m_config.game.logicIdle())
	{
		++m_skippedUpdates;
		return;
	}
	const auto worldContext = enterContext(); // this world's stores for the whole call (see InGameHud.h)
	if (!m_booted)
	{
		return;
	}
	m_radar->setClientFrame(++m_clientFrames); // lane HUD-4: the client clock the radar's event jump compares (RW 0x5DCB4C; INFERENCE: one tick per HUD update)
	m_input->update();
	m_bar->update();
	if (m_ownedWindows)
	{
		m_wm->update((int)(seconds * 1000.0));
	}
	// lane HUD-2: the local side and the control bar's context the Palantir's update reads (AptPalantir::syncLocal, PalantirCommandUI.h). INFERENCE: the context
	// drawable is the selection's only object; a multiple selection hands NULL, as ZH's ControlBar does for its multi-select context (the RotWK context rules,
	// RW 0x944390 .. 0x9450B3, were not read)
	AptPalantir::LocalState local;
	if (Player *p = m_config.game.players().getLocalPlayer())
	{
		const PlayerTemplate *pt = p->getPlayerTemplate();
		local.evil = pt && pt->m_evil;
		local.faction = !p->getSide().empty() ? p->getSide() : (pt && pt->m_playableSide ? pt->m_side : std::string());
	}
	const std::vector<ObjectID> &selected = m_input->ui().selected();
	local.context = true;
	local.contextObject = selected.size() == 1 ? selected.front() : (ObjectID)INVALID_ID;
	local.commandSet = m_bar->commandSetName();
	local.portrait = PalantirCommandUI::portraitFor(m_input->context(), local.contextObject, selected);
	m_palantirRef->setLocalState(local);
	m_palantirRef->sync(*m_bar);
	syncSpellBook();
}

// ---- lane SPELL-2 ------------------------------------------------------------------------------------------------------------------------------
void InGameHud::send(const std::vector<GameMessage> &msgs)
{
	for (const GameMessage &m : msgs)
	{
		m_config.game.commands().append(m);
	}
}

// RW 0x9312B9's slot pass: the spell book command set's visible buttons in order. INFERENCE (S-923): ControlBar::getCommandAvailability (RW 0x942733) for a
// SPELL_BOOK button is taken as: hidden (3) while none of the power's sciences is owned; disabled (4) when the power cannot be used; not ready (5, with the
// recharge percent) while it recharges; else available (1): _up, or _static for a NONPRESSABLE button
void InGameHud::syncSpellBook()
{
	Player *local = m_config.game.players().getLocalPlayer();
	std::vector<AptPalantir::SpellSlot> slots;
	if (local && TheCommandStore && m_spellBar.refresh(*TheCommandStore, m_config.game.logic(), *local))
	{
		for (const InGameSpellBookModel::Button &b : m_spellBar.buttons())
		{
			if (!b.owned || (int)slots.size() >= AptPalantir::kSpellSlots)
			{
				continue;
			}
			AptPalantir::SpellSlot sl;
			sl.image = b.image;
			sl.buttonIndex = b.index;
			if (!b.usable)
			{
				sl.state = AptPalantir::SPELL_DISABLED;
			}
			else if (!b.ready)
			{
				sl.state = AptPalantir::SPELL_NOT_READY;
				sl.timer = b.percent;
			}
			else
			{
				sl.state = (b.button && (b.button->m_options & 0x10000000u)) ? AptPalantir::SPELL_STATIC : AptPalantir::SPELL_UP;
			}
			slots.push_back(sl);
		}
	}
	m_palantirRef->syncSpellBook(slots);
	if (m_store)
	{
		m_store->update();
		if (m_store->closing())
		{
			closeSpellStore();
		}
	}
}

bool InGameHud::pressSpell(int buttonIndex)
{
	Player *local = m_config.game.players().getLocalPlayer();
	if (!local)
	{
		return false;
	}
	std::vector<GameMessage> out;
	const bool ok = m_spellBar.press(buttonIndex, local->getPlayerIndex(), out);
	send(out);
	return ok;
}

bool InGameHud::openSpellStore(std::string *error)
{
	Player *local = m_config.game.players().getLocalPlayer();
	if (m_store || !local || !TheCommandStore)
	{
		return m_store != nullptr;
	}
	auto store = std::make_unique<AptSpellStore>(*m_wm, *m_shellRef);
	if (store->level() < 0)
	{
		if (error)
		{
			*error = "SpellStore.apt was refused a window slot";
		}
		return false;
	}
	if (!store->bind(*TheCommandStore, m_config.game.logic(), *local, m_config.gameText, error))
	{
		return false;
	}
	m_store = std::move(store);
	return true;
}

void InGameHud::closeSpellStore()
{
	if (!m_store)
	{
		return;
	}
	send(m_store->takePurchases()); // RW 0x8232ED: the pending purchases go to the logic as the store goes
	m_store.reset();
}

bool InGameHud::radarSquare(float out[4])
{
	if (!m_booted || !m_palantirRef)
	{
		return false;
	}
	AptRenderList list;
	m_wm->apt().buildRenderList(list);
	const AptRenderCommand *clip = nullptr;
	for (const AptRenderCommand &c : list.commands)
	{
		if (c.kind == AptRenderCommand::Kind::Placeholder && c.nativeTag && c.symbolName == "AptPalantir::RenderRadar")
		{
			clip = &c;
			break;
		}
	}
	if (!clip)
	{
		return false;
	}
	AptStageMapping m;
	m.windowW = (float)m_windowW;
	m.windowH = (float)m_windowH;
	float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
	const float xs[2] = { clip->bounds[0], clip->bounds[2] }, ys[2] = { clip->bounds[1], clip->bounds[3] };
	for (int i = 0; i < 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			float sx, sy, wx, wy;
			clip->matrix.apply(xs[i], ys[j], sx, sy);
			m.stageToWindow(sx, sy, wx, wy);
			lo[0] = std::min(lo[0], wx);
			lo[1] = std::min(lo[1], wy);
			hi[0] = std::max(hi[0], wx);
			hi[1] = std::max(hi[1], wy);
		}
	}
	// lane HUD-2: the picture's rectangle inside the clip (W3DRadar RW 0x6D89F2), where the device draws it
	if (!m_radar->ready())
	{
		return false;
	}
	int ul[2], lr[2];
	m_radar->drawRect((int)lo[0], (int)lo[1], (int)(hi[0] - lo[0]), (int)(hi[1] - lo[1]), ul, lr);
	out[0] = (float)ul[0];
	out[1] = (float)ul[1];
	out[2] = (float)lr[0];
	out[3] = (float)lr[1];
	return lr[0] - ul[0] > 1 && lr[1] - ul[1] > 1;
}

bool InGameHud::radarClick(HudInput::Button button, int x, int y, int timeMs)
{
	(void)timeMs;
	float sq[4];
	if (!radarSquare(sq) || (float)x < sq[0] || (float)x > sq[2] || (float)y < sq[1] || (float)y > sq[3] || !m_radar->ready())
	{
		return false;
	}
	const int size = 128;
	const float kx = (float)size / (sq[2] - sq[0]), ky = (float)size / (sq[3] - sq[1]);
	float wx, wy;
	if (!m_radar->click(((float)x - sq[0]) * kx, ((float)y - sq[1]) * ky, size, wx, wy))
	{
		return true;
	}
	const Coord3D world{ wx, wy, m_config.game.logic().getGroundHeight(wx, wy) };
	if (button == HudInput::Button::Left)
	{
		m_config.view.lookAt(world);
	}
	else if (button == HudInput::Button::Right && m_bar->sourceObject() != INVALID_ID && m_input->commandTranslator().areSelectedObjectsControllable())
	{
		m_input->commandTranslator().issueRadarMove(world);
		m_input->update();
	}
	return true;
}

bool InGameHud::isOverGui(int x, int y)
{
	float sq[4];
	if (radarSquare(sq) && (float)x >= sq[0] && (float)x <= sq[2] && (float)y >= sq[1] && (float)y <= sq[3])
	{
		return true;
	}
	AptStageMapping m;
	m.windowW = (float)m_windowW;
	m.windowH = (float)m_windowH;
	float sx, sy;
	m.windowToStage((float)x, (float)y, sx, sy);
	return m_booted && m_wm->apt().input().hitTestButtons(sx, sy) != nullptr;
}

void InGameHud::mouseMove(int x, int y, int keyState)
{
	const auto worldContext = enterContext(); // this world's stores for the whole call (see InGameHud.h)
	AptStageMapping m;
	m.windowW = (float)m_windowW;
	m.windowH = (float)m_windowH;
	float sx, sy;
	m.windowToStage((float)x, (float)y, sx, sy);
	if (m_booted)
	{
		m_wm->postMouseMove(sx, sy);
	}
	m_input->mouseMove(x, y, keyState);
}

void InGameHud::mouseButton(HudInput::Button button, bool down, int x, int y, int keyState, int timeMs, bool doubleClick)
{
	const auto worldContext = enterContext(); // this world's stores for the whole call (see InGameHud.h)
	const bool over = isOverGui(x, y);
	// lane SPELL-2: a spell book power waiting for its target takes the next click on the ground (left: cast there, right: cancel)
	if (m_spellBar.targeting() && down && !over && !m_store)
	{
		if (button == HudInput::Button::Left)
		{
			Coord3D world;
			ICoord2D pixel{ x, y };
			Player *local = m_config.game.players().getLocalPlayer();
			if (local && m_config.view.screenToTerrain(pixel, m_config.game.logic(), world))
			{
				std::vector<GameMessage> out;
				m_spellBar.clickWorld(world, local->getPlayerIndex(), out);
				send(out);
			}
			return;
		}
		if (button == HudInput::Button::Right)
		{
			m_spellBar.cancel();
			return;
		}
	}
	if (down && over && radarClick(button, x, y, timeMs))
	{
		m_input->mouseButton(button, down, x, y, keyState, timeMs, doubleClick, true); // the world sees neither the press nor its release
		return;
	}
	if (m_booted && button == HudInput::Button::Left && (over || !down))
	{
		m_wm->postMouseButton(down);
	}
	m_input->mouseButton(button, down, x, y, keyState, timeMs, doubleClick, over);
}

void InGameHud::mouseWheel(int delta, int x, int y)
{
	const auto worldContext = enterContext(); // this world's stores for the whole call (see InGameHud.h)
	if (isOverGui(x, y))
	{
		if (m_booted)
		{
			m_wm->postMouseWheel(delta);
		}
		return;
	}
	m_input->mouseWheel(delta, x, y);
}

void InGameHud::key(int keyCode, int keyState)
{
	const auto worldContext = enterContext(); // this world's stores for the whole call (see InGameHud.h)
	m_input->key(keyCode, keyState);
}

std::vector<std::string> InGameHud::stops() const
{
	std::vector<std::string> out = m_input->stops();
	out.push_back(DrawablePick::stopLine()); // lane QA-1: the HUD's pick is the drawn-model ray test, not S-284's geometry volumes
	out.push_back("[S-1201] context commands (lane QA-1): a click with a builder on our rising structure is MSG_RESUME_CONSTRUCTION, on our damaged finished structure "
				  "MSG_DO_REPAIR (CommandTranslator::evaluateContextCommand), by ZH ActionManager::canResumeConstructionOf / canRepairObject (a DOZER of the same player); "
				  "RotWK's ActionManager checks and the order of its context checks are not read");
	out.push_back("[S-1202] hotkeys (lane QA-1): E (SELECT_MATCHING_UNITS) selects the selection's templates on the screen, else across the map (RotWK handler RW 0x69B9C8: "
				  "screen, then map; the donor ZH selectUnitsMatchingCurrentSelection supplies the bodies); H (VIEW_HOME_BASE) looks at the local player's best structure by RW 0x81FD5B -> "
				  "0x6AC722 (a command centre, the lowest id, else the costliest); not identified: the status bits 0 and 3 of + 0x458 the walk skips (bit 0 taken as destroyed) and "
				  "the look-at argument");
	for (const std::string &l : ControlBar::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : AptPalantir::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : UnitVoiceResponse::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : Radar::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : PalantirCommandUI::acceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : CursorAcceptanceStops())
	{
		out.push_back(l);
	}
	for (const std::string &l : WeaponSetToggle::acceptanceStops()) // lane HUD-4
	{
		out.push_back(l);
	}
	return out;
}
