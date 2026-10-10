// OpenBFME. GPL-3.0.
// See GameClient/DrawableIconUI.h for the target facts and the addresses. Client only (excluded from the simulation audit): plain float arithmetic.

#include "GameClient/DrawableIconUI.h"

#include "Common/INI/HostRealText.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "Common/Team.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cstdio>
#include <variant>

namespace
{
int kindIndex(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}
bool kindOf(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

// a template's real field (a parsed float, an integer or the raw token), `def` when the template has none (the constructor's value)
float templateReal(const ThingTemplate &t, const char *name, float def)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return def;
	}
	if (const float *f = std::get_if<float>(v))
	{
		return *f;
	}
	if (const long long *n = std::get_if<long long>(v))
	{
		return (float)*n;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		const std::string tok = raw->tokens.empty() ? std::string() : (!raw->macros.empty() && raw->macros[0].isMacro ? raw->macros[0].value : raw->tokens[0]);
		char *end = nullptr;
		const float f = strtofPortable(tok.c_str(), &end);
		if (!tok.empty() && end != tok.c_str())
		{
			return f;
		}
	}
	return def;
}

// RW 0x6778F4: the drawable's position raised by HealthBoxHeightOffset (template + 0x548, RW 0xBD83D8 10.0 without a template) and the geometry's max height
// RW 0x6778F4: a HORDE's anchor is its contain's (contain vslot 0x7C -> slot 0x22C, HordeContain RW 0x87127D = BFME2 0x46DF9A): the mean drawable position of
// the contained members and of the registered set (H+0x170), used as it is; with none of them (or not a horde) the drawable's position raised by the template's
// HealthBoxHeightOffset and the geometry's height
Coord3D iconAnchor(GameLogic &logic, const DrawableManager &drawables, const Drawable &d, const Object &obj, float heightOffset, float maxHeight)
{
	static const int HORDE = kindIndex("HORDE");
	if (kindOf(obj, HORDE))
	{
		const ContainModuleInterface *c = const_cast<Object &>(obj).getContain();
		const HordeContain *h = c ? dynamic_cast<const HordeContain *>(c) : nullptr;
		if (h)
		{
			Coord3D sum{ 0.0f, 0.0f, 0.0f };
			float count = 0.0f;
			auto add = [&](const Object *m) {
				const Drawable *md = m ? drawables.findByObject(m->getID()) : nullptr;
				if (md)
				{
					sum.x += md->getPosition()->x;
					sum.y += md->getPosition()->y;
					sum.z += md->getPosition()->z;
					count += 1.0f;
				}
			};
			for (const Object *m : *h->getContainedItemsList())
			{
				add(m);
			}
			for (std::uint32_t id : h->core().registeredMembers())
			{
				add(logic.findObjectByID((ObjectID)id));
			}
			if (count != 0.0f)
			{
				return Coord3D{ sum.x / count, sum.y / count, sum.z / count };
			}
		}
	}
	Coord3D p = *d.getPosition();
	p.z += heightOffset;
	p.z += maxHeight;
	return p;
}

bool onScreen(const TacticalView &view, const Coord3D &world, ICoord2D &screen)
{
	if (!view.worldToScreen(world, screen))
	{
		return false;
	}
	const ICoord2D s = view.size();
	return screen.x >= 0 && screen.y >= 0 && screen.x < s.x && screen.y < s.y;
}

// RW 0x671142: the three row colours of a health ratio (tables RW 0xC10FE8 red, 0xC11018 amber, 0xC11048 green; {R, G, B, A} ints, packed 0xAARRGGBB)
const int kRed[3][4] = { { 255, 19, 78, 255 }, { 255, 131, 108, 255 }, { 204, 0, 1, 255 } };
const int kAmber[3][4] = { { 230, 162, 0, 255 }, { 255, 255, 176, 255 }, { 176, 97, 0, 255 } };
const int kGreen[3][4] = { { 12, 156, 36, 255 }, { 221, 245, 142, 255 }, { 5, 113, 22, 255 } };

std::uint32_t pack(const int c[4])
{
	return ((std::uint32_t)c[3] << 24) | ((std::uint32_t)(c[0] & 0xFF) << 16) | ((std::uint32_t)(c[1] & 0xFF) << 8) | (std::uint32_t)(c[2] & 0xFF);
}
// RW 0x670ED3: per channel (int)(low * (1 - t) + high * t) (_ftol truncates)
std::uint32_t blend(const int low[4], const int high[4], float t)
{
	int c[4];
	for (int i = 0; i < 4; ++i)
	{
		c[i] = (int)((float)low[i] * (1.0f - t) + (float)high[i] * t);
	}
	return pack(c);
}
// RW 0x670FDE (a STRUCTURE's bar): the same bands over four rows (tables RW 0xC10F28 red, 0xC10F68 amber, 0xC10FA8 green); RW 0x670FA0 (a construction's
// bar): four rows blended from RW 0xC10EA8 to RW 0xC10EE8 by the health ratio
const int kStructRed[4][4] = { { 223, 3, 32, 255 }, { 255, 183, 108, 255 }, { 255, 42, 25, 255 }, { 209, 1, 13, 255 } };
const int kStructAmber[4][4] = { { 208, 144, 0, 255 }, { 255, 255, 197, 255 }, { 255, 178, 0, 255 }, { 189, 111, 0, 255 } };
const int kStructGreen[4][4] = { { 11, 128, 8, 255 }, { 255, 255, 128, 255 }, { 77, 180, 3, 255 }, { 4, 93, 3, 255 } };
const int kBuildLow[4][4] = { { 21, 86, 173, 255 }, { 107, 224, 245, 255 }, { 18, 154, 220, 255 }, { 8, 39, 117, 255 } };
const int kBuildHigh[4][4] = { { 28, 207, 251, 255 }, { 172, 255, 254, 255 }, { 16, 223, 229, 255 }, { 3, 165, 186, 255 } };

void structureBandColors(float v, std::uint32_t out[4])
{
	for (int i = 0; i < 4; ++i)
	{
		if (v >= 0.8f)
		{
			out[i] = pack(kStructGreen[i]);
		}
		else if (v >= 0.6f)
		{
			out[i] = blend(kStructAmber[i], kStructGreen[i], (v - 0.6f) * 5.0f);
		}
		else if (v >= 0.4f)
		{
			out[i] = pack(kStructAmber[i]);
		}
		else if (v >= 0.2f)
		{
			out[i] = blend(kStructRed[i], kStructAmber[i], (v - 0.2f) * 5.0f);
		}
		else
		{
			out[i] = pack(kStructRed[i]);
		}
	}
}

void constructionColors(float v, std::uint32_t out[4])
{
	for (int i = 0; i < 4; ++i)
	{
		out[i] = blend(kBuildLow[i], kBuildHigh[i], v);
	}
}

void bandColors(float v, std::uint32_t out[3])
{
	for (int i = 0; i < 3; ++i)
	{
		if (v >= 0.8f)
		{
			out[i] = pack(kGreen[i]);
		}
		else if (v >= 0.6f)
		{
			out[i] = blend(kAmber[i], kGreen[i], (v - 0.6f) * 5.0f);
		}
		else if (v >= 0.4f)
		{
			out[i] = pack(kAmber[i]);
		}
		else if (v >= 0.2f)
		{
			out[i] = blend(kRed[i], kAmber[i], (v - 0.2f) * 5.0f);
		}
		else
		{
			out[i] = pack(kRed[i]);
		}
	}
}

// RW 0x676346 (BFME2 0x67601B): whether the drawable's object gets a health bar
bool wantsHealthBar(const Object &obj, const IconUISettings &s, const std::set<ObjectID> &selected, ObjectID mousedOver, const Player *local)
{
	static const int HERO = kindIndex("HERO"), MACHINE = kindIndex("MACHINE"), MONSTER = kindIndex("MONSTER"), MINE = kindIndex("MINE");
	static const int STRUCTURE = kindIndex("STRUCTURE"), HAS_HEALTH_BAR = kindIndex("HAS_HEALTH_BAR"), INFANTRY = kindIndex("INFANTRY"), CAVALRY = kindIndex("CAVALRY");
	static const int WALL_UPGRADE = kindIndex("WALL_UPGRADE"), DEFENSIVE_WALL = kindIndex("DEFENSIVE_WALL"), UNATTACKABLE = kindIndex("UNATTACKABLE");
	static const int ROCK_VENDOR = kindIndex("ROCK_VENDOR"), HORDE = kindIndex("HORDE");
	if (!s.showObjectHealth)
	{
		return false;
	}
	bool sel = selected.count(obj.getID()) != 0;
	if (!sel && obj.getContainedBy() && kindOf(*obj.getContainedBy(), HORDE))
	{
		sel = selected.count(obj.getContainedBy()->getID()) != 0;
	}
	if (!sel && mousedOver != obj.getID())
	{
		return false;
	}
	// RW 0x6763DA: an invisible object (RW 0x68FC2F) gets no bar from an active local player that is not its ally (its team's relationship to the local player's
	// default team; RW 0x6AAC52: the local player is not defeated)
	if (InvisibilityManager::isInvisible(obj))
	{
		const Team *team = obj.getTeam();
		if (local && team && !local->isDefeated() && team->getRelationship(local->getDefaultTeam()) != ALLIES)
		{
			return false;
		}
	}
	if (!kindOf(obj, HERO) && !kindOf(obj, MACHINE) && !kindOf(obj, MONSTER) && !kindOf(obj, MINE) && !kindOf(obj, STRUCTURE) && !kindOf(obj, HAS_HEALTH_BAR))
	{
		if (!s.allHealthBars || (!kindOf(obj, INFANTRY) && !kindOf(obj, CAVALRY)))
		{
			return false;
		}
	}
	if (kindOf(obj, WALL_UPGRADE))
	{
		return false; // INFERENCE (S-1950): the WallUpgradeUpdate's "upgrade active" test is not ported; a wall upgrade gets no bar
	}
	if (kindOf(obj, DEFENSIVE_WALL) && !kindOf(obj, HAS_HEALTH_BAR))
	{
		return false;
	}
	return !kindOf(obj, UNATTACKABLE) && !kindOf(obj, ROCK_VENDOR) && !kindOf(obj, HORDE);
}
} // namespace

// the template's health box values, computed once per template (Object + 0xB8 is the object's GeometryInfo bounding circle: the template's shapes; a gate's own
// shape flags are not followed here)
const DrawableIconUI::TemplateBox &DrawableIconUI::boxOf(const ThingTemplate &tt)
{
	auto it = m_boxes.find(&tt);
	if (it != m_boxes.end())
	{
		return it->second;
	}
	TemplateBox b;
	try
	{
		PathfindGeometry g;
		ObjectGeometry::fillPathfindGeometry(tt, g);
		b.radius = g.boundingCircleRadius();
		b.maxHeight = ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(tt));
	}
	catch (const std::exception &e)
	{
		m_notes.insert(tt.getName() + ": " + e.what());
	}
	b.scale = templateReal(tt, "HealthBoxScale", 1.5f);
	b.heightOffset = templateReal(tt, "HealthBoxHeightOffset", 10.0f);
	return m_boxes.emplace(&tt, b).first->second;
}

void DrawableIconUI::build(GameLogic &logic, const DrawableManager &drawables, const TacticalView &view, const IconUISettings &s, const std::set<ObjectID> &selected,
	ObjectID mousedOver, std::vector<IconUIOp> &out)
{
	out.clear();
	static const int IGNORED_IN_GUI = kindIndex("IGNORED_IN_GUI"), WALL_SEGMENT = kindIndex("WALL_SEGMENT"), DEFENSIVE_WALL = kindIndex("DEFENSIVE_WALL");
	const float zoomScale = s.zoom > 0.0f ? 1.0f / s.zoom : 1.0f;
	struct Queued
	{
		const Drawable *d;
		Object *obj;
		float x0, y0, x1, y1;
		bool vet;
	};
	std::vector<Queued> queued;
	for (size_t id = 1; id < drawables.slotCount(); ++id)
	{
		const Drawable *d = drawables.find((DrawableID)id);
		Object *obj = d ? logic.findObjectByID(d->getObjectID()) : nullptr;
		if (!obj || !obj->getTemplate())
		{
			continue;
		}
		// RW 0x4853EB: only a drawn drawable queues its icons (not hidden, RW 0x67055A; the shroud of the object for the local player at most 2, RW 0x68D8F7):
		// an enemy's invisible object is not drawn (InvisibilityManager::clientLook 5, the drawable's hidden state RW 0x6760F9) and shows no bar, text or mark.
		// INFERENCE (S-1951): the fog / shroud part is the device's (a fogged object's drawable is the device's ghost here)
		if (InvisibilityManager::clientLook(*obj, logic.players().getLocalPlayer()) == 5)
		{
			continue;
		}
		// RW 0x68DF5B: the health box width
		if (kindOf(*obj, IGNORED_IN_GUI))
		{
			continue;
		}
		const TemplateBox &box = boxOf(*obj->getTemplate());
		float half = box.radius * box.scale;
		half = std::min(std::max(half, 20.0f), 150.0f);
		const float width = std::max(half * 2.0f, 20.0f);
		Coord3D anchor = iconAnchor(logic, drawables, *d, *obj, box.heightOffset, box.maxHeight);
		ICoord2D sp;
		if (!onScreen(view, anchor, sp))
		{
			if (!obj->isUnderConstruction())
			{
				continue;
			}
			anchor.z -= (anchor.z - d->getPosition()->z) * 0.333f; // RW 0x6791C2 (0.333 RW 0xC11140)
			if (!onScreen(view, anchor, sp))
			{
				continue;
			}
		}
		const float w = zoomScale * width;
		Queued q;
		q.d = d;
		q.obj = obj;
		q.x0 = (float)(int)((float)sp.x - w * 0.45f);
		q.y0 = (float)(int)((float)sp.y - 2.0f);
		q.x1 = (float)(int)(q.x0 + w);
		q.y1 = (float)(int)(q.y0 + 4.0f);
		q.vet = !obj->isEffectivelyDead();
		queued.push_back(q);
	}
	// list 0: the health bar, then the construction text
	for (const Queued &q : queued)
	{
		Object &obj = *q.obj;
		BodyModuleInterface *body = obj.getBodyModule();
		if (body && wantsHealthBar(obj, s, selected, mousedOver, logic.players().getLocalPlayer()) && body->getMaxHealth() != 0.0f && body->getHealth() != 0.0f)
		{
			// RW 0x674445 picks the look: UNDER_CONSTRUCTION (status 2) -> RW 0x671FD5, KindOf STRUCTURE -> RW 0x6721CC, else RW 0x6723C3. The two building looks
			// have frames 10 / 8 / 6 pixels high and four colour rows; a unit's 9 / 7 / 5 and three
			const bool building = obj.isUnderConstruction() || kindOf(obj, kindIndex("STRUCTURE"));
			const int rowsCount = building ? 4 : 3;
			const float ratio = body->getHealth() / body->getMaxHealth();
			const float width = q.x1 - q.x0;
			IconUIOp a;
			a.object = obj.getID();
			a.kind = IconUIOp::OPEN_RECT;
			a.x = q.x0 - 3.0f, a.y = q.y0 - 3.0f, a.w = width + 6.0f, a.h = building ? 10.0f : 9.0f, a.color = 0x7F000000u;
			out.push_back(a);
			a.x = q.x0 - 2.0f, a.y = q.y0 - 2.0f, a.w = width + 4.0f, a.h = building ? 8.0f : 7.0f, a.color = 0xFFBA9252u;
			out.push_back(a);
			a.kind = IconUIOp::FILL_RECT;
			a.x = q.x0 - 1.0f, a.y = q.y0 - 1.0f, a.w = width + 2.0f, a.h = building ? 6.0f : 5.0f, a.color = 0xFF000000u;
			out.push_back(a);
			std::uint32_t rows[4];
			if (obj.isUnderConstruction())
			{
				constructionColors(ratio, rows);
			}
			else if (building)
			{
				structureBandColors(ratio, rows);
			}
			else
			{
				bandColors(ratio, rows);
			}
			for (int i = 0; i < rowsCount; ++i)
			{
				a.x = q.x0, a.y = q.y0 + (float)i, a.w = width * ratio, a.h = 1.0f, a.color = rows[i];
				out.push_back(a);
			}
		}
		// RW 0x677CF9: the construction text
		if (obj.isUnderConstruction() && !obj.testStatus(19)) // status 0x13 SOLD (TheObjectStatusNames RW 0xD8AFF0)
		{
			std::string text;
			if (!kindOf(obj, WALL_SEGMENT) && !kindOf(obj, DEFENSIVE_WALL))
			{
				std::u16string fmt;
				if (s.text && s.text->fetch("CONTROLBAR:UnderConstructionDesc", fmt))
				{
					char buf[256];
					std::snprintf(buf, sizeof(buf), loadScreenU16ToUtf8(fmt).c_str(), (double)obj.getConstructionPercent()); // "Building: %.0f%%"
					text = buf;
				}
				else
				{
					text = loadScreenU16ToUtf8(fetchOrMissing(s.text, "CONTROLBAR:UnderConstructionDesc"));
				}
			}
			const TemplateBox &box = boxOf(*obj.getTemplate());
			Coord3D pos = iconAnchor(logic, drawables, *q.d, obj, box.heightOffset, box.maxHeight);
			pos.z -= (pos.z - q.d->getPosition()->z) * 0.333f;
			ICoord2D sp;
			if (!text.empty() && onScreen(view, pos, sp))
			{
				IconUIOp t;
				t.kind = IconUIOp::TEXT;
				t.object = obj.getID();
				t.x = (float)sp.x;
				t.y = (float)sp.y;
				t.text = text;
				t.color = 0xFFFFFFFFu;
				t.dropColor = 0xFF000000u;
				out.push_back(t);
			}
		}
	}
	// list 2: the veterancy marks
	for (const Queued &q : queued)
	{
		Object &obj = *q.obj;
		static const int HORDE = kindIndex("HORDE");
		// RW 0x67939F: not a horde's member (RW 0x6939DF: HORDE_MEMBER inside a HORDE), with an experience tracker
		const Object *container = obj.getContainedBy();
		const bool hordeMember = container && kindOf(*container, HORDE);
		const ExperienceTracker *xp = obj.getExperienceTracker();
		if (!q.vet || !xp || hordeMember)
		{
			continue;
		}
		if (!s.veterancyFilter)
		{
			m_notes.insert("[S-1951] no VeterancyPipDrawObjectFilter: no veterancy marks");
			continue;
		}
		if (!ObjectFilterMatch::allows(logic, *s.veterancyFilter, obj, nullptr))
		{
			continue;
		}
		// the rank of the tracker's current level (RW 0x79D12A -> 0x689024 / 0x688EEC): none at rank <= 1, nor for a level of RequiredExperience <= 1 without
		// a next level (RW 0x68845B .. 0x67948A)
		const ExperienceLevelTemplate *level = TheExperienceLevelSystem ? TheExperienceLevelSystem->findLevel(xp->getLevelName()) : nullptr;
		if (!level || level->m_rank <= 1 || (level->m_requiredExperience <= 1 && !xp->upcomingLevel()))
		{
			continue;
		}
		// whose drawable carries the marks: a HORDE's banner carrier (horde interface slot 0x118 -> H+0x26C, RW 0x8705A6), else its first member (slot 0x110,
		// RW 0x87055D: the first contained object, else the first of H+0x170); none: no marks. Another object: its own
		const Drawable *markDrawable = q.d;
		const Object *markObject = &obj;
		if (kindOf(obj, HORDE) && obj.getContain())
		{
			const HordeContain *h = dynamic_cast<const HordeContain *>(obj.getContain());
			const Object *carrier = nullptr;
			if (h)
			{
				carrier = h->bannerCarrier() ? logic.findObjectByID(h->bannerCarrier()) : nullptr;
				if (!carrier && !h->getContainedItemsList()->empty())
				{
					carrier = h->getContainedItemsList()->front();
				}
				if (!carrier && !h->core().registeredMembers().empty())
				{
					carrier = logic.findObjectByID((ObjectID)*h->core().registeredMembers().begin());
				}
			}
			markDrawable = carrier ? drawables.findByObject(carrier->getID()) : nullptr;
			markObject = carrier;
			if (!markDrawable)
			{
				continue;
			}
		}
		const Player *p = obj.getControllingPlayer();
		const bool evil = p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_evil; // RW 0x67900B: the player template's + 0x1BC
		const char *iconName = evil ? "Evil_Vet" : "Good_Vet";
		const char *dotName = evil ? "Evil_Vet_Dot" : "Good_Vet_Dot";
		const Image *icon = s.images ? s.images->findImageByName(iconName) : nullptr;
		const Image *dot = s.images ? s.images->findImageByName(dotName) : nullptr;
		if (!icon || !dot)
		{
			m_notes.insert(std::string("the veterancy images ") + iconName + " / " + dotName + " are unknown");
			continue;
		}
		// RW 0x6780DF
		const ICoord2D screen = view.size();
		const float scaleX = ((float)screen.x / 1024.0f) * 0.5f * zoomScale;
		const float scaleY = ((float)screen.y / 768.0f) * 0.5f * zoomScale;
		const float spacingX = 2.0f * scaleX, spacingY = 2.0f * scaleY;
		ICoord2D sp;
		const TemplateBox &box = boxOf(*markObject->getTemplate());
		if (!onScreen(view, iconAnchor(logic, drawables, *markDrawable, *markObject, box.heightOffset, box.maxHeight), sp))
		{
			continue;
		}
		const int rank = level->m_rank;
		int numIcons = rank / 5, numDots = rank % 5;
		float y = (float)sp.y - 7.0f;
		const float dotH = (float)dot->getImageHeight() * scaleY;
		y -= dotH;
		IconUIOp im;
		im.kind = IconUIOp::IMAGE;
		im.object = obj.getID();
		if (numDots > 0)
		{
			const float dotW = (float)dot->getImageWidth() * scaleX;
			float x = (float)sp.x - ((float)numDots * dotW + (float)(numDots - 1) * spacingX) * 0.5f;
			for (; numDots > 0; --numDots)
			{
				im.image = dotName, im.x = x, im.y = y, im.w = dotW, im.h = dotH;
				out.push_back(im);
				x += dotW + spacingX;
			}
		}
		if (numIcons > 0)
		{
			const float iconH = (float)icon->getImageHeight() * scaleY;
			y -= iconH + spacingY;
			const float iconW = (float)icon->getImageWidth() * scaleX;
			float x = (float)sp.x - ((float)numIcons * iconW + (float)(numIcons - 1) * spacingX) * 0.5f;
			for (; numIcons > 0; --numIcons)
			{
				im.image = iconName, im.x = x, im.y = y, im.w = iconW, im.h = iconH;
				out.push_back(im);
				x += iconW + spacingX;
			}
		}
	}
}

std::vector<std::string> DrawableIconUI::acceptanceStops()
{
	return {
		"[S-1950] the drawable decorations (HUD-5): the health bar (unit, structure and construction looks RW 0x6723C3 / 0x6721CC / 0x671FD5), the construction text and "
		"the veterancy marks (a horde's on its banner carrier or first member, RW 0x67939F) run as RW 0x679129 / 0x676346 / 0x677CF9 / 0x6780DF, with the stealth tests "
		"(RW 0x4853EB, 0x6763DA); not ported: the wall upgrade's \"upgrade active\" test, the other decoration lists (the group number, the caption, lists 1, 3, 5)",
		"[S-1951] the drawable decorations' inference: the in-game UI's drawable caption font of the construction text (InGameUI + 0x7A0 / + 0x7A4), the icon toggle "
		"(TheGameLogic + 0x9A) and the script letterbox (TheScriptEngine + 0x1A238) of the queue, the queue's fog / shroud test (RW 0x68D8F7 <= 2) left to the device, the "
		"horde anchor's H+0x170 set taken as HordeContainCore's registered members (the members on their way into a garrison are kept apart)",
	};
}
