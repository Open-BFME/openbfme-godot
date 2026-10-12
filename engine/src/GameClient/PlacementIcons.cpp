// OpenBFME. GPL-3.0.
// See PlacementIcons.h (lane BUILD-5 r2; RotWK RW 0x69C5E6, RW 0x6A2AE5).

#include "GameClient/PlacementIcons.h"

#include "Common/AsciiString.h"
#include "Common/ModelState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/PlacementGhost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace
{
constexpr float kPlacementOpacity = 0.45f;          // RW 0xC12408
constexpr DrawableID kFirstGhostId = 0x7FFF0000u;  // the ghost's drawables are no DrawableManager slots; ids apart from them (the scripts' frame state keys on them)

float placementViewAngle(const ThingTemplate &tt)
{
	if (const FieldValue *v = tt.findField("PlacementViewAngle"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}
} // namespace

PlacementIcons::Icon::Icon() = default;
PlacementIcons::Icon::~Icon() = default;
PlacementIcons::Icon::Icon(Icon &&) noexcept = default;
PlacementIcons::Icon &PlacementIcons::Icon::operator=(Icon &&) noexcept = default;

PlacementIcons::PlacementIcons(DrawServices &services, GameLogic &logic)
	: m_services(services)
	, m_logic(logic)
{
}

PlacementIcons::~PlacementIcons() = default;

void PlacementIcons::error(const std::string &e)
{
	if (std::find(m_errors.begin(), m_errors.end(), e) == m_errors.end())
	{
		m_errors.push_back(e);
	}
}

void PlacementIcons::clear()
{
	m_icons.clear();
	m_template.clear();
	m_source = INVALID_ID;
	m_castle = false;
}

std::uint32_t PlacementIcons::indicatorColor(const GameLogic &logic, ObjectID builder)
{
	// RW 0x69C731 .. 0x69C74D: GlobalData + 0x134 == 4 (NIGHT) -> Player + 0x2A4 (the night colour), else Player + 0x2A0
	const Object *o = const_cast<GameLogic &>(logic).findObjectByID(builder);
	const Player *p = o ? o->getControllingPlayer() : nullptr;
	if (!p)
	{
		return 0;
	}
	return logic.settings().night ? p->getPlayerNightColor() : p->getPlayerColor();
}

std::unique_ptr<Drawable> PlacementIcons::make(const ThingTemplate &tt)
{
	// RW 0x6CFE8C (ThingFactory::newDrawable): the draw modules of the template; then opacity 0.45 and BUILD_PLACEMENT_CURSOR (RW 0x69C714 / 0x69C724)
	std::vector<std::string> problems;
	const std::vector<ClientEvent::ClientModule> noClientModules;
	std::unique_ptr<Drawable> d(new Drawable(m_services, tt.getFinalOverride(), (DrawableID)(kFirstGhostId + (DrawableID)m_icons.size()), INVALID_ID, noClientModules, problems));
	for (const std::string &p : problems)
	{
		error(tt.getName() + ": " + p);
	}
	d->setDrawableOpacity(kPlacementOpacity);
	ModelConditionFlags flags;
	const int bit = ModelCondition::indexOf("BUILD_PLACEMENT_CURSOR");
	if (bit < 0)
	{
		error("the model condition BUILD_PLACEMENT_CURSOR is not in the binary's name table");
	}
	else
	{
		flags.set((size_t)bit, true);
	}
	d->setModelConditionFlags(flags);
	return d;
}

void PlacementIcons::rebuild(const InGameUI &ui)
{
	clear();
	m_template = ui.placeBuildTemplate();
	m_source = ui.placeBuildSource();
	const ThingTemplate *tt = m_logic.things().findTemplate(m_template);
	if (!tt)
	{
		error("the pending template " + m_template + " is unknown");
		return;
	}
	Icon main;
	main.tt = tt->getFinalOverride();
	main.drawable = make(*tt);
	main.angle = placementViewAngle(*main.tt); // RW 0x69C6FC
	main.drawable->setOrientation(main.angle);
	main.drawable->setHouseColor(indicatorColor(m_logic, m_source)); // RW 0x674504
	m_icons.push_back(std::move(main));
	// a castle: the local faction's layout (RW 0x6A3484 .. 0x6A3760)
	const Player *local = m_logic.players().getLocalPlayer();
	if (!m_castles)
	{
		return;
	}
	const PlacementGhost::CastleLook look = PlacementGhost::castleLookOf(*tt, local ? local->getSide() : std::string(), *m_castles,
		[this](const std::string &name) { return m_logic.things().findTemplate(name); });
	m_castle = look.castle;
	if (!look.castle)
	{
		return;
	}
	if (!look.error.empty())
	{
		error(m_template + ": castle layout: " + look.error);
	}
	static const int baseSite = ObjectTemplateInfoBuilder::kindOfIndex("BASE_SITE");
	// every slot of a castle, slot 0 (the castle's own drawable) included, is judged by its template's BASE_SITE (RW 0x6A396A: the loop runs from slot 0)
	m_icons[0].baseSite = baseSite >= 0 && MaskTest(m_logic.templateInfo(m_icons[0].tt).kindOf, (unsigned)baseSite);
	for (const PlacementGhost::Piece &p : look.pieces)
	{
		const ThingTemplate *pt = m_logic.things().findTemplate(p.templateName);
		if (!pt)
		{
			continue;
		}
		Icon icon;
		icon.tt = pt->getFinalOverride();
		icon.piece = true;
		icon.offX = p.x;
		icon.offY = p.y;
		icon.baseSite = baseSite >= 0 && MaskTest(m_logic.templateInfo(icon.tt).kindOf, (unsigned)baseSite); // template + 0x117 & 1 (RW 0x6A3993)
		icon.drawable = make(*pt);
		m_icons.push_back(std::move(icon));
	}
}

void PlacementIcons::collectProblems()
{
	// the draw modules' own problems (a model no archive holds, an animation that does not resolve, a script error): reported, never a ghost that silently
	// draws nothing (review r2 #3; DrawableManager::collect does the same for the game's drawables)
	for (const Icon &icon : m_icons)
	{
		if (!icon.drawable || !icon.tt)
		{
			continue;
		}
		for (const DrawEntry &e : icon.drawable->entries())
		{
			if (e.missingModel)
			{
				error(icon.tt->getName() + " " + e.className + ": the model " + e.lastModel + " is not in the mounted archives: the ghost does not draw it");
			}
			if (!e.draw)
			{
				continue;
			}
			if (e.kind == W3D_DRAWKIND_MODEL)
			{
				// a state that names a model the asset manager cannot load: the draw module poses nothing (a state's "None" is retail's no-model, not a problem)
				W3DDrawPoseView f;
				e.draw->poseView(f);
				if (!f.model && f.modelName && !f.modelName->empty() && AsciiStringUtil::compareNoCase(*f.modelName, "None") != 0)
				{
					error(icon.tt->getName() + " " + e.className + ": the model " + *f.modelName + " does not load: the ghost does not draw it");
				}
			}
			for (const std::string &err : e.draw->errors())
			{
				// a hide request of a sub object the model lacks is counted (a note: DrawableManager::Sink::message's HIDE_MISS class, the script hiding V1FLAG
				// that GBBarracks_SKN lacks); every other problem is an error (review r3 #2): an animation that does not resolve (the class DATA_DEFECT, a
				// note for the game's drawables) leaves the ghost unposed, so it reaches push_error and the game's report like a model that does not load
				const DrawMessageClass c = classifyDrawMessage(err);
				const std::string line = icon.tt->getName() + " " + e.className + ": " + err;
				// a hide / show request while the state has no model (a castle's own drawable: MenFortress' BUILD_PLACEMENT_CURSOR model is None) changes nothing
				const bool hideWithoutModel = err.rfind("hide/show of sub object ", 0) == 0 && err.find(" with no model") != std::string::npos;
				if (c.kind == DrawMessageClass::HIDE_MISS || hideWithoutModel)
				{
					if (std::find(m_notes.begin(), m_notes.end(), line) == m_notes.end())
					{
						m_notes.push_back(line);
					}
				}
				else
				{
					error(line);
				}
			}
		}
	}
}

void PlacementIcons::update(const InGameUI &ui, double elapsedMs, unsigned ghostOptions, bool judge)
{
	if (!ui.isPlacing())
	{
		if (!m_icons.empty() || !m_template.empty())
		{
			clear();
		}
		return;
	}
	if (ui.placeBuildTemplate() != m_template || ui.placeBuildSource() != m_source)
	{
		if (!judge)
		{
			return; // a new template's drawables are made on an idle frame (the player colour and the kinds read the logic)
		}
		rebuild(ui);
	}
	if (m_icons.empty() || !ui.placeHasGhost())
	{
		return;
	}
	// slot 0 at the ghost's site and angle (RW 0x6A3847 / 0x6A385B)
	const Coord3D site = ui.placeLocation();
	const float angle = ui.placeAngle();
	Icon &main = m_icons[0];
	main.angle = angle;
	main.drawable->setPosition(&site);
	main.drawable->setOrientation(angle);
	// the castle's entries (RW 0x6A359E .. 0x6A36FB): x' = x cos - y sin, y' = x sin + y cos about the site, z on the ground, turned by the ghost's angle
	const double c = std::cos((double)angle), s = std::sin((double)angle);
	for (size_t i = 1; i < m_icons.size(); ++i)
	{
		Icon &icon = m_icons[i];
		Coord3D at;
		at.x = (float)((double)site.x + (double)icon.offX * c - (double)icon.offY * s);
		at.y = (float)((double)site.y + (double)icon.offX * s + (double)icon.offY * c);
		at.z = m_logic.getGroundHeight(at.x, at.y);
		icon.angle = angle;
		icon.drawable->setPosition(&at);
		icon.drawable->setOrientation(angle);
	}
	for (Icon &icon : m_icons)
	{
		icon.drawable->advanceAnimation(elapsedMs);
	}
	collectProblems();
	// RW 0x6A3939: the judgement on odd client frames (and only while the logic is idle: the device's busy frames keep the last answers)
	if (!judge || !(ui.clientFrame() & 1))
	{
		return;
	}
	Object *builder = m_logic.findObjectByID(m_source);
	bool plotFailed = false; // RW 0x6A3960: [ebp - 0xd]
	for (size_t i = 0; i < m_icons.size(); ++i)
	{
		Icon &icon = m_icons[i];
		int code = 0;
		if (!m_castle)
		{
			// a single building: the translator's answer of this frame is the same call (the site and angle of slot 0, 0x48F, the builder)
			code = ui.placeLegalCode();
		}
		else if (icon.baseSite)
		{
			code = (int)BuildPlacement::isLocationLegalToBuild(m_logic, *icon.drawable->getPosition(), *icon.tt, icon.angle, ghostOptions, builder, nullptr);
			plotFailed = plotFailed || code != 0;
		}
		icon.code = code;
		if (code == 0)
		{
			icon.tint = plotFailed ? TINT_ILLEGAL : TINT_NONE; // RW 0x6A3AE2 .. 0x6A3AF6 / RW 0x6A3B35
			icon.bib = BIB_NONE;                              // vslot 0x34
		}
		else if (code == 9)
		{
			icon.tint = TINT_CODE9;
			icon.tintBlue = (ui.clientFrame() & 4) ? 1.7f : 0.5f; // RW 0xC1348C / 0xBD869C
			icon.bib = BIB_NORMAL;
		}
		else
		{
			icon.tint = TINT_ILLEGAL;
			icon.bib = BIB_HIGHLIGHT;
		}
	}
}
