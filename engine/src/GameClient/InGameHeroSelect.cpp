// OpenBFME. GPL-3.0.
// See GameClient/InGameHeroSelect.h for the target facts.

#include "GameClient/InGameHeroSelect.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudContext.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/PalantirCommandUI.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "Common/ModEngineFeatures.h"
#include "Common/Thing/ThingTemplate.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <variant>

namespace
{
// ObjectStatus 92 (RW 0x92BC07: testStatus(0x5C))
constexpr unsigned kStatusNoHeroProperties = 92;

std::string num(int value)
{
	return std::to_string(value);
}

struct CycleTimeoutState
{
	unsigned timeoutMs = 0;
	bool seen = false;
	bool inGameUi = false;
};

size_t fieldIndent(const std::string &line)
{
	const size_t first = line.find_first_not_of(" \t");
	return first == std::string::npos ? line.size() : first;
}

std::string withoutIniComment(const std::string &line)
{
	size_t cut = line.find(';');
	const size_t slash = line.find("//");
	if (slash != std::string::npos && (cut == std::string::npos || slash < cut))
	{
		cut = slash;
	}
	return cut == std::string::npos ? line : line.substr(0, cut);
}

bool isEndField(const std::string &line)
{
	const std::string uncommented = withoutIniComment(line);
	const size_t begin = uncommented.find_first_not_of(" \t");
	if (begin == std::string::npos)
	{
		return false;
	}
	const size_t end = uncommented.find_first_of(" \t=", begin);
	return AsciiStringUtil::compareNoCase(uncommented.substr(begin, end == std::string::npos ? std::string::npos : end - begin), "End") == 0;
}

void skipCycleTimeoutNestedBlock(INI *ini, void *, void *, const void *userData)
{
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData),
				ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (isEndField(line))
		{
			return;
		}
	}
}

void skipCycleTimeoutField(INI *ini, void *, void *, const void *userData)
{
	const std::string header = ini->currentLineText();
	if (withoutIniComment(header).find('=') != std::string::npos)
	{
		return;
	}
	const size_t headerIndent = fieldIndent(header);
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData),
				ini->getFilename().c_str(), ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (isEndField(line) && fieldIndent(line) <= headerIndent)
		{
			return;
		}
	}
}

void parseCycleTimeout(INI *ini, void *instance, void *store, const void *)
{
	INI::parseUnsignedInt(ini, instance, store, nullptr);
	static_cast<CycleTimeoutState *>(instance)->seen = true;
}

const FieldParse kCycleTimeoutFields[] = {
	{ "SelectNearestBuilderCycleTimeOut", parseCycleTimeout, nullptr, (int)offsetof(CycleTimeoutState, timeoutMs) },
	{ "RadiusCursorTemplate", skipCycleTimeoutNestedBlock, "RadiusCursorTemplate", 0 },
	{ nullptr, skipCycleTimeoutField, "InGameUI", 0 }
};

int heroSortOrder(const Object &obj)
{
	// template + 0x648 (field table RW 0xDA3DF8: HeroSortOrder, parseInt); a template that does not set it keeps the ctor's 0x7FFFFFFF (RW 0x740069 ..
	// 0x74006E in ThingTemplate's ctor RW 0x73FB7A): most heroes have no HeroSortOrder and keep their creation order behind the ones that do
	if (const FieldValue *v = obj.getTemplate()->findField("HeroSortOrder"))
	{
		if (const long long *n = std::get_if<long long>(v))
		{
			return (int)*n;
		}
	}
	return 0x7FFFFFFF;
}

bool onScreen(const HudContext &ctx, const Object &obj)
{
	// View vslot 0x120 (isPointOnScreen at scale 1) on the drawable's position
	ICoord2D px;
	if (!ctx.view.worldToScreen(*obj.getPosition(), px))
	{
		return false;
	}
	const ICoord2D s = ctx.view.size();
	return px.x >= 0 && px.y >= 0 && px.x < s.x && px.y < s.y;
}

// the drawable's horde: a contained object whose container is KindOf HORDE (drawable + 0xFC -> + 0x274, template + 0x115 bit 5)
const Object *hordeOf(const Object &obj)
{
	const Object *c = obj.getContainedBy();
	return c && c->isKindOfName("HORDE") ? c : nullptr;
}
} // namespace

InGameHeroSelect::InGameHeroSelect(HudContext &ctx, unsigned timeoutMs)
	: m_ctx(ctx)
	, m_builderTimeoutFrames((unsigned)SimMath::pc24MulW((double)timeoutMs, (double)0.03f))
{
}

bool InGameHeroSelect::loadCycleTimeout(ArchiveFileSystem &fs, unsigned &ms, std::string *error)
{
	const char *file = "data\\ini\\ingameui.ini";
	if (!fs.doesFileExist(file))
	{
		if (error)
		{
			*error = std::string(file) + ": file not found in any mounted archive";
		}
		return false;
	}
	INIEnvironment env;
	env.fileSystem = &fs;
	INIBlockRecorder recorder;
	RegisterRecordingBlockStubs(env.blocks, recorder, { "InGameUI" }, StubExtent::Lenient);
	CycleTimeoutState state;
	env.blocks.registerBlock("InGameUI", [&state](INI *ini) {
		state.inGameUi = true;
		ini->initFromINI(&state, kCycleTimeoutFields);
	});
	try
	{
		INI ini(env);
		ini.load(file, INI_LOAD_OVERWRITE);
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = std::string(file) + ": " + e.what();
		}
		return false;
	}
	if (!state.inGameUi || !state.seen)
	{
		if (error)
		{
			*error = std::string(file) + ": missing InGameUI.SelectNearestBuilderCycleTimeOut";
		}
		return false;
	}
	ms = state.timeoutMs;
	return true;
}

void InGameHeroSelect::flashHeroButton(const std::string &templateName, int frames)
{
	// RW 0x92C876: case-sensitive template name; a shorter request never truncates a running flash.
	for (Hero &hero : m_heroes)
	{
		if (hero.templateName == templateName && hero.flashFrames < frames)
		{
			hero.flashFrames = frames;
		}
	}
}

std::string InGameHeroSelect::buttonImage(const Object &obj)
{
	// BFME2 donor CreateAHeroHero::Update (RVA 0x4083FF..0x408839) caches
	// the drawable's model-state button image (RVA 0x27272B) alongside its
	// portrait (RVA 0x272709; see PalantirCommandUI::objectPortrait).
	if (obj.isKindOfName("CREATE_A_HERO"))
	{
		ModelConditionFlags flags;
		for (int bit = 0; bit < MODELCONDITION_COUNT; ++bit) flags.set(bit, obj.testModelCondition(bit));
		for (const auto &module : obj.getTemplate()->drawModules().nuggets())
		{
			const auto *data = dynamic_cast<const W3DModelDrawModuleData *>(module.data.get());
			const ModelConditionInfo *state = data ? data->findBestInfo(flags) : nullptr;
			if (state && !state->buttonImageName.empty()) return state->buttonImageName;
		}
	}
	// lane MODS-3: an ObjectImageUpgrade's ButtonImage first (edain-unchained.object-image-upgrade, its hook 0xEF3392 at RW 0x73D0BA)
	std::string upgraded = ObjectImageUpgrade::imageOf(obj, false);
	if (!upgraded.empty())
	{
		return upgraded;
	}
	// RW 0x73D0BA -> RW 0x73CFEF: the template's ButtonImage (+ 0x78)
	if (const FieldValue *v = obj.getTemplate()->findField("ButtonImage"))
	{
		if (const std::string *s = std::get_if<std::string>(v))
		{
			return *s;
		}
	}
	return std::string();
}

bool InGameHeroSelect::isSelectableHero(const HudContext &ctx, const Object &obj)
{
	const Player *p = obj.getControllingPlayer();
	return p && p == ctx.localPlayer() && !obj.testStatus(kStatusNoHeroProperties);
}

bool InGameHeroSelect::isReadyBuilder(const HudContext &, const Object &obj)
{
	// RW 0x92BAD7: a drawable (RW 0x70E013: every logic object has one in the port), not effectively dead (+ 0x458 bit 0), selectable (RW 0x68DE58), and a dozer
	// AI interface (AI + 0x260 vslot 0x170) with no task pending (its vslot 0x20)
	if (HudObjects::isEffectivelyDead(obj) || !PlayerCommands::isSelectable(obj))
	{
		return false;
	}
	const DozerAIUpdate *dozer = dynamic_cast<const DozerAIUpdate *>(obj.getAIUpdateInterface());
	return dozer && !dozer->isAnyTaskPending();
}

// ---- the data -----------------------------------------------------------------------------------------------------------------------------------

bool InGameHeroSelect::listedAsHero(const Object &obj)
{
	// lane MODS-3: edain-unchained.herobar (its hooks 0xED74A8 / 0xED74D9 / 0xED74FE): HERO || HEROBAR || HEROBAR_GROUP takes the hero list
	return obj.isKindOfName("HERO") ||
		(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobar) && (obj.isKindOfName("HEROBAR") || obj.isKindOfName("HEROBAR_GROUP")));
}

void InGameHeroSelect::addObject(const Object &obj)
{
	const bool hero = listedAsHero(obj);
	if (!hero && !obj.isKindOfName("PORTER"))
	{
		return;
	}
	if (buttonImage(obj).empty())
	{
		return; // RW 0x92C74A / 0x92C81F: no button image, no button
	}
	const ObjectID id = obj.getID();
	if (hero)
	{
		for (const Hero &h : m_heroes)
		{
			if (h.id == id)
			{
				return;
			}
		}
		Hero entry;
		entry.id = id;
		entry.templateName = obj.getTemplate()->getName();
		float progress = 0.0f;
		PalantirCommandUI::objectRankAndProgress(obj, entry.rank, progress); // RW 0x92C77A -> RW 0x9D25B7
		if (m_ctx.logic.getFrame() < 6)
		{
			// RW 0x92C786 .. 0x92C7D9: before the first listed hero with a greater HeroSortOrder
			const int order = heroSortOrder(obj);
			auto at = m_heroes.begin();
			for (; at != m_heroes.end(); ++at)
			{
				const Object *other = m_ctx.logic.findObjectByID(at->id);
				if (other && order < heroSortOrder(*other))
				{
					break;
				}
			}
			m_heroes.insert(at, entry);
		}
		else
		{
			m_heroes.push_back(entry);
		}
		return;
	}
	for (const Builder &b : m_builders)
	{
		if (b.id == id)
		{
			return;
		}
	}
	m_builders.push_back(Builder{ id, false }); // RW 0x92C809
}

void InGameHeroSelect::removeObject(const Object &obj)
{
	const ObjectID id = obj.getID();
	if (listedAsHero(obj))
	{
		m_heroes.remove_if([id](const Hero &h) { return h.id == id; });
	}
	else if (obj.isKindOfName("PORTER"))
	{
		m_builders.remove_if([id](const Builder &b) { return b.id == id; });
	}
}

void InGameHeroSelect::trackObjects()
{
	std::vector<const Object *> now;
	for (const Object *o = m_ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isDestroyed())
		{
			now.push_back(o);
		}
	}
	std::sort(now.begin(), now.end(), [](const Object *a, const Object *b) { return a->getID() < b->getID(); });
	std::vector<ObjectID> ids;
	ids.reserve(now.size());
	for (const Object *o : now)
	{
		ids.push_back(o->getID());
	}
	// gone: removed (the hero list keeps only live entries; the port's object is already gone, so the entry is dropped by id)
	for (ObjectID id : m_known)
	{
		if (!std::binary_search(ids.begin(), ids.end(), id))
		{
			m_heroes.remove_if([id](const Hero &h) { return h.id == id; });
			m_builders.remove_if([id](const Builder &b) { return b.id == id; });
		}
	}
	for (const Object *o : now)
	{
		if (!std::binary_search(m_known.begin(), m_known.end(), o->getID()))
		{
			addObject(*o);
		}
	}
	m_known.swap(ids);
}

// ---- the interface ------------------------------------------------------------------------------------------------------------------------------

std::string InGameHeroSelect::commandPrefix() const
{
	return "_level" + std::to_string(m_level) + "." + m_name;
}

std::string InGameHeroSelect::slotKey(int slot, const char *suffix) const
{
	return commandPrefix() + "_Hero" + std::to_string(slot + 1) + suffix; // RW 0xC7EF38 / 0xC7EEF0
}

bool InGameHeroSelect::movieCall(const std::string &function, const std::vector<std::string> &args)
{
	return m_movie.call && m_movie.call(function, args);
}

void InGameHeroSelect::attach(int level, const std::string &clipPath, Movie movie, const std::string &faction)
{
	m_level = level;
	const std::string prefix = "_level" + std::to_string(level) + ".";
	m_name = clipPath.compare(0, prefix.size(), prefix) == 0 ? clipPath.substr(prefix.size()) : clipPath;
	m_movie = std::move(movie);
	m_attached = true;
	m_shown = false;
	m_selectAllShown = false;
	for (Slot &s : m_slots)
	{
		s = Slot();
	}
	movieCall("SetFaction", { faction }); // the ctor's call of BFME2 0x5255E2 (SetFaction with the owner's + 0xF8: the local side name)
}

void InGameHeroSelect::detach()
{
	m_attached = false;
	m_movie = Movie();
}

std::vector<InGameHeroSelect::Builder *> InGameHeroSelect::readyLocalBuilders(bool readyOnly)
{
	std::vector<Builder *> out;
	const Player *local = m_ctx.localPlayer();
	for (Builder &b : m_builders)
	{
		const Object *o = m_ctx.logic.findObjectByID(b.id);
		if (o && local && o->getControllingPlayer() == local)
		{
			if (b.used)
			{
				m_builderUsed = true;
			}
			if (!readyOnly || isReadyBuilder(m_ctx, *o))
			{
				out.push_back(&b);
			}
		}
		else
		{
			b.used = false;
		}
	}
	return out;
}

void InGameHeroSelect::update()
{
	if (!m_attached)
	{
		return;
	}
	std::vector<Builder *> builders = readyLocalBuilders(true);
	if (!m_shown)
	{
		bool any = !builders.empty();
		for (const Hero &h : m_heroes)
		{
			const Object *o = m_ctx.logic.findObjectByID(h.id);
			any = any || (o && isSelectableHero(m_ctx, *o));
		}
		if (any)
		{
			movieCall("Show", {});
			m_shown = true;
		}
		return;
	}
	int slotIndex = 0;
	auto num = [](int v) { return std::to_string(v); };
	auto setImage = [&](int i, const std::string &image) {
		if (m_movie.setImage)
		{
			m_movie.setImage(slotKey(i, "Image"), image);
		}
		m_slots[i].image = image;
	};
	if (!builders.empty())
	{
		Slot &s = m_slots[0];
		if (!s.builder)
		{
			if (s.hero != INVALID_ID)
			{
				movieCall("KillButtonEffects", { num(1) });
				s.hero = INVALID_ID;
			}
			s.builder = true;
			const Object *first = m_ctx.logic.findObjectByID(builders.front()->id);
			const std::string image = first ? buttonImage(*first) : std::string();
			if (s.image.empty())
			{
				movieCall("SetButtonState", { num(1), "_up" });
			}
			setImage(0, image);
			movieCall("SetButtonRankProgress", { num(1), num(1) });
			s.rankProgress = 1;
			movieCall("SetButtonHealthBar", { num(1), num(100) });
			s.health = 100;
		}
		const int count = (int)builders.size();
		if (s.rank != count)
		{
			if (m_movie.setText)
			{
				m_movie.setText("APT:" + slotKey(0, "Rank"), std::to_string(count)); // UnicodeString::format(L"%d")
			}
			s.rank = count;
		}
		bool selected = false;
		for (const Builder *b : builders)
		{
			selected = selected || m_ctx.ui.isSelected(b->id);
		}
		if (selected != s.selected)
		{
			movieCall("SetButtonSelectedHighlightState", { num(1), selected ? "_show" : "_hide" });
			s.selected = selected;
		}
		const bool flash = m_builderFlashFrames > 0;
		if (flash != s.flash)
		{
			movieCall("SetButtonFlashEffectState", { num(1), flash ? "_show" : "_hide" });
			s.flash = flash;
		}
		++slotIndex;
	}
	else
	{
		m_slots[0].builder = false;
	}
	bool anyHero = false;
	// lane MODS-3: edain-unchained.herobar's group merge (its 0xED7599 / 0xED75AB): the HEROBAR_GROUP templates already given a slot this update (at most 16)
	const bool herobar = ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobar);
	std::vector<const ThingTemplate *> groupsShown;
	for (auto it = m_heroes.begin(); it != m_heroes.end(); ++it)
	{
		Hero &h = *it;
		const Object *hero = m_ctx.logic.findObjectByID(h.id);
		if (!hero)
		{
			continue;
		}
		int rank = 0;
		float progress = 0.0f;
		PalantirCommandUI::objectRankAndProgress(*hero, rank, progress); // RW 0x92D3AF -> RW 0x9D2437
		const bool levelUp = h.rank > 0 && rank > h.rank;
		h.rank = rank;
		bool group = false, groupSelected = false;
		if (herobar && isSelectableHero(m_ctx, *hero) && slotIndex < kSlots && hero->isKindOfName("HEROBAR_GROUP"))
		{
			const ThingTemplate *tt = static_cast<const ThingTemplate *>(hero->getTemplate());
			if (std::find(groupsShown.begin(), groupsShown.end(), tt) != groupsShown.end())
			{
				if (h.flashFrames > 0)
				{
					--h.flashFrames; // a later instance takes no slot (its 0xED75AB jumps to RW 0x92D76F)
				}
				continue;
			}
			if (groupsShown.size() < 16)
			{
				groupsShown.push_back(tt);
				group = true;
				int count = 0; // the selectable instances from this node to the end (RW 0x92BBEF); the slot's rank text shows the count
				for (auto jt = it; jt != m_heroes.end(); ++jt)
				{
					const Object *o = m_ctx.logic.findObjectByID(jt->id);
					if (o && o->getTemplate() == hero->getTemplate() && isSelectableHero(m_ctx, *o))
					{
						++count;
						groupSelected = groupSelected || m_ctx.ui.isSelected(o->getID());
					}
				}
				rank = count;
			}
		}
		if (isSelectableHero(m_ctx, *hero) && slotIndex < kSlots)
		{
			Slot &s = m_slots[slotIndex];
			const std::string n = num(slotIndex + 1);
			if (s.hero != h.id || s.builder)
			{
				movieCall("KillButtonEffects", { n });
				s.hero = h.id;
				s.builder = false;
			}
			s.group = group;
			const std::string image = buttonImage(*hero);
			if (image != s.image)
			{
				if (s.image.empty())
				{
					movieCall("SetButtonState", { n, "_up" });
				}
				setImage(slotIndex, image);
			}
			if (rank != s.rank)
			{
				if (m_movie.setText)
				{
					m_movie.setText("APT:" + slotKey(slotIndex, "Rank"), std::to_string(rank));
				}
				s.rank = rank;
			}
			const int rankProgress = progress >= 0.0f ? std::max((int)std::floor(progress * 100.0f + 0.5f), 1) : 1;
			if (rankProgress != s.rankProgress)
			{
				movieCall("SetButtonRankProgress", { n, num(rankProgress) });
				s.rankProgress = rankProgress;
			}
			int health = 100;
			if (const BodyModuleInterface *body = hero->getBodyModule())
			{
				const float maxHealth = body->getMaxHealth();
				const float ratio = maxHealth > 0.0f ? body->getHealth() / maxHealth : 0.0f;
				health = std::min(100, std::max(0, (int)(ratio * 100.0f + 0.5f)));
			}
			health = std::max(health, 1);
			if (health != s.health)
			{
				movieCall("SetButtonHealthBar", { n, num(health) });
				s.health = health;
			}
			const bool selected = group ? groupSelected : m_ctx.ui.isSelected(h.id); // a group slot: any instance selected (its 0xED7458)
			if (selected != s.selected)
			{
				movieCall("SetButtonSelectedHighlightState", { n, selected ? "_show" : "_hide" });
				s.selected = selected;
			}
			const bool flash = h.flashFrames > 0;
			if (flash != s.flash)
			{
				movieCall("SetButtonFlashEffectState", { n, flash ? "_show" : "_hide" });
				s.flash = flash;
			}
			if (levelUp)
			{
				movieCall("PlayButtonLevelUpEffect", { n });
			}
			++slotIndex;
			anyHero = true;
		}
		if (h.flashFrames > 0)
		{
			--h.flashFrames;
		}
	}
	for (; slotIndex < kSlots; ++slotIndex)
	{
		Slot &s = m_slots[slotIndex];
		const std::string n = num(slotIndex + 1);
		s.group = false;
		if (s.hero != INVALID_ID)
		{
			movieCall("KillButtonEffects", { n });
			s.hero = INVALID_ID;
		}
		if (!s.image.empty())
		{
			movieCall("SetButtonState", { n, "_unused" });
			setImage(slotIndex, std::string());
		}
		s.rank = -1;
		s.rankProgress = -1;
		s.health = -1;
		s.selected = false;
		if (s.flash)
		{
			movieCall("SetButtonFlashEffectState", { n, "_hide" });
			s.flash = false;
		}
	}
	if (anyHero)
	{
		showSelectAll();
	}
	else
	{
		hideSelectAll();
	}
	if (m_builderFlashFrames > 0)
	{
		--m_builderFlashFrames;
	}
}

void InGameHeroSelect::showSelectAll()
{
	if (m_selectAllShown)
	{
		return;
	}
	movieCall("SetSelectAllHeroesButtonState", { "_up" });
	m_selectAllShown = true;
}

void InGameHeroSelect::hideSelectAll()
{
	if (!m_selectAllShown)
	{
		return;
	}
	movieCall("SetSelectAllHeroesButtonState", { "_unused" });
	m_selectAllShown = false;
}

void InGameHeroSelect::onButtonPressed(const std::string &param)
{
	if (!m_ctx.ui.getInputEnabled())
	{
		return; // lane OWNERFIX-1 (INFERENCE, S-4964): the hero bar selects nothing while a script has the input off (retail drops the logic half in the stream)
	}
	if (param.compare(0, 4, "Hero") != 0)
	{
		return;
	}
	const int index = std::atoi(param.c_str() + 4) - 1;
	if (index < 0 || index >= kSlots)
	{
		return;
	}
	if (m_slots[index].builder)
	{
		selectNearestBuilder(false);
		return;
	}
	if (m_slots[index].hero == INVALID_ID)
	{
		return;
	}
	if (m_slots[index].group)
	{
		pressGroupSlot(index);
		return;
	}
	const Object *obj = m_ctx.logic.findObjectByID(m_slots[index].hero);
	if (!obj)
	{
		return;
	}
	if (obj->isKindOfName("HERO") && obj->getContainedBy())
	{
		obj = obj->getContainedBy(); // a hero inside a building or transport stands for it
	}
	if (!obj || !PlayerCommands::isSelectable(*obj))
	{
		return;
	}
	if (const Object *horde = hordeOf(*obj))
	{
		obj = horde;
	}
	const ObjectID id = obj->getID();
	const std::vector<ObjectID> &selected = m_ctx.ui.selected();
	const bool onlyThis = selected.size() == 1 && selected.front() == id;
	if (!m_ctx.ui.isInPreferSelectionMode() || selected.empty() || onlyThis)
	{
		if (onlyThis)
		{
			m_ctx.view.lookAt(*obj->getPosition()); // the second click on the hero that is the selection
		}
		else
		{
			m_ctx.ui.deselectAll(false);
			ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
			m.appendBoolean(true);
			m.appendObjectID(id);
			m_ctx.ui.selectObject(id);
		}
		return;
	}
	if (m_ctx.ui.isSelected(id))
	{
		ClientMessage &m = m_ctx.stream.append(MSG_REMOVE_FROM_SELECTED_GROUP);
		m.appendObjectID(id);
		m_ctx.ui.deselectObject(id);
	}
	else
	{
		ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
		m.appendBoolean(false);
		m.appendObjectID(id);
		m_ctx.ui.selectObject(id);
	}
}

// lane MODS-3: edain-unchained.herobar's group press (its 0xED76EA): a second press of the same slot within 15 client frames (500 ms x 0.03) looks at the instance
// the first one picked; else the next selectable instance of the template after the one this slot picked last (wrapping) becomes the selection, alone (no
// container or horde substitution, no additive selection)
void InGameHeroSelect::pressGroupSlot(int index)
{
	const Object *first = m_ctx.logic.findObjectByID(m_slots[index].hero);
	if (!first)
	{
		return;
	}
	const bool remembered = index < 16; // its arrays hold 16 slots
	if (remembered && m_groupLastSlot == index && m_groupLastId[index] != INVALID_ID && m_clientFrame < m_groupDoubleClickDeadline)
	{
		if (const Object *o = m_ctx.logic.findObjectByID(m_groupLastId[index]))
		{
			m_ctx.view.lookAt(*o->getPosition()); // the tactical view's look-at (vslot 0x54) at the drawn position (RW 0x676711)
		}
		return;
	}
	std::vector<ObjectID> instances;
	for (const Hero &h : m_heroes)
	{
		const Object *o = m_ctx.logic.findObjectByID(h.id);
		if (o && o->getTemplate() == first->getTemplate() && isSelectableHero(m_ctx, *o) && PlayerCommands::isSelectable(*o))
		{
			instances.push_back(h.id);
		}
	}
	if (instances.empty())
	{
		return;
	}
	ObjectID pick = instances.front();
	if (remembered)
	{
		auto at = std::find(instances.begin(), instances.end(), m_groupLastId[index]);
		if (at != instances.end() && at + 1 != instances.end())
		{
			pick = *(at + 1);
		}
		m_groupLastId[index] = pick;
		m_groupLastSlot = index;
		m_groupDoubleClickDeadline = m_clientFrame + 15;
	}
	m_ctx.ui.deselectAll(false);
	ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
	m.appendBoolean(true);
	m.appendObjectID(pick);
	m_ctx.ui.selectObject(pick);
}

void InGameHeroSelect::selectAllHeroes()
{
	if (!m_ctx.ui.getInputEnabled())
	{
		return; // lane OWNERFIX-1 (INFERENCE, S-4964): the hero bar selects nothing while a script has the input off (retail drops the logic half in the stream)
	}
	bool clear = !m_ctx.ui.isInPreferSelectionMode();
	auto eligible = [&](int i, const Object *&out) {
		if (m_slots[i].builder || m_slots[i].hero == INVALID_ID)
		{
			return false;
		}
		const Object *hero = m_ctx.logic.findObjectByID(m_slots[i].hero);
		if (!hero || !PlayerCommands::isSelectable(*hero) || HudObjects::isEffectivelyDead(*hero))
		{
			return false;
		}
		if (!hero->isKindOfName("HERO") && listedAsHero(*hero))
		{
			return false; // lane MODS-3: edain-unchained.herobar (its 0xED7523 / 0xED755E): a HEROBAR-only object is not a hero for select-all
		}
		if (!clear && m_ctx.ui.isSelected(hero->getID()))
		{
			return false;
		}
		out = hero;
		return true;
	};
	int last = 0;
	for (int i = 0; i < kSlots; ++i)
	{
		const Object *h = nullptr;
		if (eligible(i, h))
		{
			last = i;
		}
	}
	for (int i = 0; i < last + 1; ++i)
	{
		const Object *hero = nullptr;
		if (!eligible(i, hero))
		{
			continue;
		}
		if (const Object *horde = hordeOf(*hero))
		{
			hero = horde;
		}
		if (clear)
		{
			m_ctx.ui.deselectAll(false);
		}
		ClientMessage &m = m_ctx.stream.append(i == last ? MSG_CREATE_SELECTED_GROUP : MSG_CREATE_SELECTED_GROUP_NO_SOUND);
		m.appendBoolean(clear);
		m.appendObjectID(hero->getID());
		m_ctx.ui.selectObject(hero->getID());
		clear = false;
	}
}

void InGameHeroSelect::onHeroAttacked(ObjectID id)
{
	if (id == INVALID_ID)
	{
		return;
	}
	for (int i = 0; i < kSlots; ++i)
	{
		if (m_slots[i].hero == id)
		{
			movieCall("PlayButtonAttackedEffect", { num(i + 1) }); // RW 0x92BFED: first matching visible slot
			return;
		}
	}
}

void InGameHeroSelect::resetBuilderRound()
{
	for (Builder &b : m_builders)
	{
		b.used = false;
	}
	m_builderUsed = false;
}

void InGameHeroSelect::sortByCameraDistance(std::vector<Builder *> &list)
{
	const Coord3D camera = m_ctx.view.position();
	std::vector<std::pair<float, Builder *>> keyed;
	for (Builder *b : list)
	{
		float d = 0.0f;
		if (const Object *o = m_ctx.logic.findObjectByID(b->id))
		{
			const float dx = o->getPosition()->x - camera.x, dy = o->getPosition()->y - camera.y;
			d = dx * dx + dy * dy;
		}
		keyed.emplace_back(d, b);
	}
	std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	for (size_t i = 0; i < keyed.size(); ++i)
	{
		list[i] = keyed[i].second;
	}
}

InGameHeroSelect::Builder *InGameHeroSelect::findReadyLocalBuilder(const std::vector<Builder *> &list)
{
	const bool oneSelected = m_ctx.ui.selected().size() == 1;
	for (Builder *b : list)
	{
		if (b->used)
		{
			continue;
		}
		const Object *o = m_ctx.logic.findObjectByID(b->id);
		if (!o || !isReadyBuilder(m_ctx, *o))
		{
			continue;
		}
		if (oneSelected && m_ctx.ui.isSelected(b->id) && onScreen(m_ctx, *o))
		{
			continue;
		}
		return b;
	}
	return nullptr;
}

void InGameHeroSelect::selectNearestBuilder(bool noCamera)
{
	if (!m_ctx.ui.getInputEnabled())
	{
		return; // lane OWNERFIX-1 (INFERENCE, S-4964): the hero bar selects nothing while a script has the input off (retail drops the logic half in the stream)
	}
	if (m_ctx.ui.isInPreferSelectionMode())
	{
		noCamera = true; // TheKeyboard->isShift() (INFERENCE: the port's Shift is InGameUI's prefer-selection state)
	}
	if (m_ctx.ui.selected().size() > 1 || (m_builderUsed && m_builderDeadline <= m_clientFrame))
	{
		resetBuilderRound();
	}
	std::vector<Builder *> list = readyLocalBuilders(true);
	sortByCameraDistance(list);
	Builder *entry = findReadyLocalBuilder(list);
	if (!entry && m_builderUsed)
	{
		resetBuilderRound();
		entry = findReadyLocalBuilder(list);
	}
	if (!entry)
	{
		return;
	}
	const Object *builder = m_ctx.logic.findObjectByID(entry->id);
	if (!builder)
	{
		return;
	}
	m_ctx.ui.deselectAll(false);
	ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE);
	m.appendBoolean(true);
	m.appendObjectID(builder->getID());
	m_ctx.ui.selectObject(builder->getID());
	if (!noCamera && !onScreen(m_ctx, *builder))
	{
		m_ctx.view.lookAt(*builder->getPosition());
	}
	entry->used = true;
	m_builderUsed = true;
	m_builderDeadline = m_clientFrame + m_builderTimeoutFrames;
}
