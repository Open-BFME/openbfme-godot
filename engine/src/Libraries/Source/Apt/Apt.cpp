// OpenBFME. GPL-3.0.
// See Apt.h for the citations.

#include "Libraries/Source/Apt/Apt.h"

#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>

namespace
{

constexpr int kScriptDepthBase = 0x4000; // AS depth 0 is display-list depth 0x4000 (see AptSpriteInst reconcile)

bool parseLevelName(const std::string &name, int &level)
{
	// "_level0".."_level11": ASCII case-insensitive
	std::string k = AptPropertyMap::foldKey(name);
	if (k.compare(0, 6, "_level") != 0 || k.size() == 6)
	{
		return false;
	}
	int v = 0;
	for (std::size_t i = 6; i < k.size(); ++i)
	{
		if (k[i] < '0' || k[i] > '9')
		{
			return false;
		}
		v = v * 10 + (k[i] - '0');
		if (v > 1000000)
		{
			return false;
		}
	}
	level = v;
	return true;
}

} // namespace

// The host the interpreter sees: loadMovie requests are taken by the player (and forwarded to the engine host as a
// notification); everything else goes straight to the engine host.
class AptPlayerHost : public AptHost
{
public:
	AptPlayerHost(Apt &apt, AptHost &user) : m_apt(apt), m_user(user) {}
	void trace(const std::string &message) override { m_user.trace(message); }
	void fscommand(const std::string &command, const std::string &argument) override { m_user.fscommand(command, argument); }
	void loadMovie(const std::string &movieName, const std::string &target) override
	{
		m_apt.requestLoad(movieName, target);
		m_user.loadMovie(movieName, target);
	}
	void loadMovieInto(const std::string &movieName, AptObject *targetClip) override
	{
		m_apt.requestLoadInto(movieName, targetClip);
		m_user.loadMovie(movieName, targetClip ? targetClip->displayString() : std::string());
	}
	void getURL(const std::string &url, const std::string &target) override { m_user.getURL(url, target); }
	AptExternResult getExtern(const std::string &name, std::string &value) override { return m_user.getExtern(name, value); }
	bool setExtern(const std::string &name, const std::string &value) override { return m_user.setExtern(name, value); }
	std::uint32_t random() override { return m_user.random(); }
	void scriptError(const std::string &message) override { m_user.scriptError(message); }
	// S-380: the retail interpreter skips such a call silently; the player keeps a note with the clip the script runs on
	void scriptCallWithoutFunction(const std::string &message) override
	{
		AptCharacterInst *t = m_apt.scriptTarget();
		m_apt.note("call-without-function", message + " [in " + (t ? t->targetPath() : std::string("?")) + "]");
	}
	bool isComponentSymbol(const std::string &movieName, const std::string &symbolName) override { return m_user.isComponentSymbol(movieName, symbolName); }

private:
	Apt &m_apt;
	AptHost &m_user;
};

// ---------------------------------------------------------------------------------------------------------------

Apt::Apt(AptFileSource &source, AptHost &host) : m_source(source), m_userHost(host), m_loader(source)
{
	m_playerHost = std::make_unique<AptPlayerHost>(*this, host);
	m_vm = std::make_unique<AptActionInterpreter>(m_gc, *m_playerHost);
	m_input = std::make_unique<AptInput>(*this);
	installNatives();
}

Apt::~Apt() = default;

void Apt::note(const std::string &kind, const std::string &detail)
{
	m_notes.push_back({ kind, detail });
}

std::size_t Apt::noteCount(const std::string &kind) const
{
	std::size_t n = 0;
	for (const AptNote &note : m_notes)
	{
		if (note.kind == kind)
		{
			++n;
		}
	}
	return n;
}

void Apt::noteBackgroundColor(const std::uint8_t rgba[4])
{
	// 0x00B0F4CE: the first background colour item of the process reaches the engine (callback at 0x00E17738);
	// later ones are ignored.  Kept here for the renderer.
	std::copy(rgba, rgba + 4, m_background);
}

// ---- movie data -----------------------------------------------------------------------------------------------

bool Apt::importsFor(const std::shared_ptr<const AptFile> &file, std::string *error)
{
	if (m_imports.count(file.get()))
	{
		return true;
	}
	std::vector<AptResolvedImport> resolved;
	if (!m_loader.resolveImports(*file, resolved, error))
	{
		return false;
	}
	for (const AptImport &imp : file->imports)
	{
		if (imp.name.compare(0, 11, "__Packages.") == 0)
		{
			// AptImported_Init_Actions (0x00AE46A0 / 0x00AE6A30 for imports named "__Packages.*") is not ported (S-103)
			note("package-import-init-actions-not-run", file->name + ": " + imp.name);
		}
	}
	m_imports[file.get()] = std::move(resolved);
	m_keepMovies.push_back(file);
	for (const AptResolvedImport &r : m_imports[file.get()])
	{
		m_keepMovies.push_back(r.movie);
	}
	return true;
}

bool Apt::resolveCharacter(const std::shared_ptr<const AptFile> &file, std::uint32_t id, AptCharRef &out, std::string *error)
{
	std::shared_ptr<const AptFile> f = file;
	std::uint32_t cid = id;
	for (int hop = 0; hop < 16; ++hop)
	{
		if (cid >= f->characters.size())
		{
			if (error)
			{
				*error = "character id " + std::to_string(cid) + " is outside the " + std::to_string(f->characters.size()) + " characters of " + f->name;
			}
			return false;
		}
		const AptCharacter &c = f->characters[cid];
		if (c.type != APT_CHAR_NULL)
		{
			out.file = f;
			out.character = &c;
			return true;
		}
		if (!importsFor(f, error))
		{
			return false;
		}
		const AptResolvedImport *hit = nullptr;
		for (const AptResolvedImport &r : m_imports[f.get()])
		{
			if (r.localSlot == cid)
			{
				hit = &r;
				break;
			}
		}
		if (!hit)
		{
			if (error)
			{
				*error = "character slot " + std::to_string(cid) + " of " + f->name + " is empty and no import fills it";
			}
			return false;
		}
		f = hit->movie;
		cid = hit->characterId;
	}
	if (error)
	{
		*error = "import chain of character " + std::to_string(id) + " in " + file->name + " is longer than 16 hops";
	}
	return false;
}

const std::map<std::string, int> &Apt::labelsOf(const std::vector<AptFrame> *frames)
{
	auto it = m_labels.find(frames);
	if (it != m_labels.end())
	{
		return it->second;
	}
	std::map<std::string, int> labels;
	for (std::size_t f = 0; f < frames->size(); ++f)
	{
		for (const AptFrameItem &item : (*frames)[f].items)
		{
			if (item.type == APT_ITEM_FRAMELABEL)
			{
				labels.emplace(item.label, (int)f); // the EA search returns the first match (0x00B0F010)
			}
		}
	}
	return m_labels.emplace(frames, std::move(labels)).first->second;
}

// ---- init actions ---------------------------------------------------------------------------------------------

void Apt::runFrameInitActions(const std::shared_ptr<const AptFile> &file, const AptFrame &frame, AptCharacterInst *target)
{
	// 0x00B0F3CD: an init action item whose stored id is >= 0 runs with the sprite as target, then the id is negated
	// (0x00B0F441); an id of 0 stays 0 and so runs again each time (UNVERIFIED consequence, S-103: no retail item has 0)
	for (const AptFrameItem &item : frame.items)
	{
		if (item.type != APT_ITEM_INITACTION)
		{
			continue;
		}
		auto key = std::make_pair(file.get(), item.offset);
		auto it = m_initState.find(key);
		std::int32_t id = it != m_initState.end() ? it->second : (std::int32_t)item.spriteId;
		if (id < 0)
		{
			continue;
		}
		if (id == 0)
		{
			note("init-action-id-zero-reruns", file->name + " @" + std::to_string(item.offset)); // S-103: -0 == 0
		}
		std::string error;
		std::shared_ptr<const AptCodeBlock> code = file->codeAt(item.codeOffset, &error);
		if (!code)
		{
			m_vm->reportError("init action program at file offset " + std::to_string(item.codeOffset) + " cannot be decoded: " + error);
		}
		else
		{
			executeProgram(code, target);
		}
		m_initState[key] = -id;
	}
}

void Apt::runInitActionsFor(const std::shared_ptr<const AptFile> &file, std::uint32_t characterId, AptCharacterInst *target)
{
	// AptMovie data 0x00AE6B40: for an imported character the exporter's tables are used; the init actions of the
	// characters placed on the first frame of the (exporter's character's / the parent's) timeline run first, then
	// the character's own (0x00AE6A30).
	std::shared_ptr<const AptFile> owner = file;
	std::uint32_t id = characterId;
	const std::vector<AptFrame> *frames0 = nullptr;
	if (AptSpriteInst *parent = target ? target->asSprite() : nullptr)
	{
		frames0 = parent->frames;
	}
	if (characterId < file->characters.size() && file->characters[characterId].type == APT_CHAR_NULL)
	{
		AptCharRef ref;
		std::string error;
		if (resolveCharacter(file, characterId, ref, &error))
		{
			owner = ref.file;
			id = ref.character->id;
			frames0 = &ref.character->frames;
		}
	}
	auto runFor = [&](std::uint32_t wanted) {
		// the init action table of `owner`: every type-8 item of its root frames (0x00AE6A30 scans `[edx+4]`)
		for (const AptFrame &fr : owner->frames)
		{
			for (const AptFrameItem &item : fr.items)
			{
				if (item.type != APT_ITEM_INITACTION)
				{
					continue;
				}
				auto key = std::make_pair(owner.get(), item.offset);
				auto it = m_initState.find(key);
				std::int32_t stored = it != m_initState.end() ? it->second : (std::int32_t)item.spriteId;
				if (stored < 0 || (std::uint32_t)stored != wanted)
				{
					continue;
				}
				std::string error;
				std::shared_ptr<const AptCodeBlock> code = owner->codeAt(item.codeOffset, &error);
				if (!code)
				{
					m_vm->reportError("init action program at file offset " + std::to_string(item.codeOffset) + " cannot be decoded: " + error);
				}
				else
				{
					executeProgram(code, target);
				}
				m_initState[key] = -stored;
			}
		}
	};
	if (frames0 && !frames0->empty())
	{
		for (const AptFrameItem &item : (*frames0)[0].items)
		{
			if (item.type == APT_ITEM_PLACEOBJECT && item.place && item.place->characterId >= 0 && (item.place->flags & APT_PLACE_HASCHARACTER))
			{
				runFor((std::uint32_t)item.place->characterId);
			}
		}
	}
	runFor(id);
}

AptCharacterInst *Apt::createInstance(const AptCharRef &ref)
{
	using Type = AptCharacterInst::Type;
	AptCharacterInst *inst = nullptr;
	const AptCharacter &ch = *ref.character;
	switch (ch.type)
	{
		case APT_CHAR_SPRITE:
		{
			AptSpriteInst *s = m_gc.create<AptSpriteInst>(*this, Type::Sprite);
			s->frames = &ch.frames;
			s->timelineFile = ref.file;
			s->frame = -1; // 0x00AF871C
			s->playing = true;
			s->needsLoad = true;
			inst = s;
			break;
		}
		case APT_CHAR_BUTTON:
		{
			AptButtonInst *b = m_gc.create<AptButtonInst>(*this);
			b->setup(ref);
			inst = b;
			break;
		}
		case APT_CHAR_SHAPE:
			inst = m_gc.create<AptShapeInst>(*this);
			break;
		case APT_CHAR_EDITTEXT:
		{
			AptTextInst *t = m_gc.create<AptTextInst>(*this, Type::EditText);
			if (ch.text)
			{
				t->text = ch.text->initialText;
				t->variable = ch.text->variableName;
				t->colorArgb = AptTextInst::packColor(ch.text->color);
			}
			inst = t;
			break;
		}
		default:
		{
			// Morph, static text, video, sound...: the parser has no layout for them (A1).  The character is placed as an
			// inert instance so the rest of the timeline still runs, and the gap is reported (S-100).
			Type t = ch.type == APT_CHAR_STATICTEXT ? Type::StaticText : ch.type == APT_CHAR_MORPH ? Type::Morph : Type::Unsupported;
			inst = m_gc.create<AptOpaqueInst>(*this, t);
			note("opaque-character", "character type " + std::to_string(ch.type) + " id " + std::to_string(ch.id) + " in " + (ref.file ? ref.file->name : std::string("?")));
			break;
		}
	}
	inst->m_char = ref;
	inst->setProto(inst->type() == Type::EditText ? m_textFieldProto : m_movieClipProto);
	return inst;
}

// ---- levels ---------------------------------------------------------------------------------------------------

AptSpriteInst *Apt::makeRoot(const std::shared_ptr<const AptFile> &movie)
{
	AptSpriteInst *r = m_gc.create<AptSpriteInst>(*this, AptCharacterInst::Type::Movie);
	r->frames = &movie->frames;
	r->timelineFile = movie;
	r->m_char.file = movie;
	r->setProto(m_movieClipProto);
	return r;
}

AptSpriteInst *Apt::level(int n) const
{
	auto it = m_levels.find(n);
	return it == m_levels.end() ? nullptr : it->second;
}

std::vector<int> Apt::loadedLevels() const
{
	std::vector<int> out;
	for (const auto &kv : m_levels)
	{
		out.push_back(kv.first);
	}
	return out;
}

bool Apt::loadMovie(int level, const std::string &movieName, std::string *error)
{
	std::shared_ptr<const AptFile> movie = m_loader.loadMovie(movieName, error);
	if (!movie)
	{
		return false;
	}
	if (!importsFor(movie, error))
	{
		return false;
	}
	if (m_levels.count(level))
	{
		unloadLevel(level);
	}
	AptSpriteInst *root = makeRoot(movie);
	root->m_instName = "_level" + std::to_string(level);
	root->m_depth = level;
	m_levels[level] = root;
	m_vm->global()->props.set(root->m_instName, AptValue::object(root));
	// 0x00AD1A41: flags |= 0x1000000, then advance: the first frame is processed now.  The load tracker (0x00AD17F0) runs the
	// action pool right after it completed a load (0x00AD1C58), so the frame actions of frame 0 have run when this returns.
	root->needsLoad = true;
	root->advance();
	runActions();
	return true;
}

bool Apt::unloadLevel(int level)
{
	auto it = m_levels.find(level);
	if (it == m_levels.end())
	{
		return false;
	}
	AptSpriteInst *root = it->second;
	m_levels.erase(it);
	m_vm->global()->props.erase("_level" + std::to_string(level));
	root->destroy(true);
	m_input->forget(root);
	return true;
}

int Apt::msPerFrame() const
{
	if (m_levels.empty())
	{
		return 0;
	}
	const AptSpriteInst *first = m_levels.begin()->second;
	return first->timelineFile ? (int)first->timelineFile->msPerFrame : 0;
}

// ---- update ---------------------------------------------------------------------------------------------------

int Apt::update(int elapsedMs)
{
	if (m_levels.empty())
	{
		return 0;
	}
	const int frameMs = msPerFrame();
	if (frameMs <= 0)
	{
		m_vm->reportError("the lowest level's movie has no frame duration (ms/frame is 0); the update cannot step");
		return 0;
	}
	std::int64_t total = m_carryMs + (elapsedMs < 0 ? 0 : elapsedMs);
	if (total < frameMs)
	{
		m_carryMs = total;
		return 0;
	}
	int steps = 0;
	do
	{
		stepFrame(frameMs);
		total -= frameMs;
		++steps;
	} while (total >= frameMs && !m_levels.empty());
	m_carryMs = total;
	return steps;
}

void Apt::stepFrame(int frameMs)
{
	runTimers(frameMs);
	std::vector<AptSpriteInst *> lvls;
	for (const auto &kv : m_levels)
	{
		lvls.push_back(kv.second);
	}
	for (AptSpriteInst *l : lvls)
	{
		if (l->defined() && m_levels.count((int)l->depth()) && m_levels[(int)l->depth()] == l)
		{
			l->advance();
		}
	}
	runActions();
	m_input->processQueued();
	processLoads();
	m_clockMs += frameMs;
	++m_frameCount;
	if (m_gc.objectCount() > m_gcAfter)
	{
		collectGarbage();
	}
}

// ---- action pool ----------------------------------------------------------------------------------------------

void Apt::pushAction(const AptAction &action)
{
	m_pool.push_back(action);
}

void Apt::pushActionFront(const AptAction &action)
{
	if (m_running)
	{
		// 0x00AE4A90 releases the entries between the (moved) head and the tail without running them, and the run loop
		// walks forward from the head it read at its start: an entry pushed to the front during the run never runs
		note("front-action-dropped", "mask 0x" + std::to_string(action.eventMask));
		return;
	}
	m_pool.push_front(action);
}

void Apt::pushFunctionCall(AptCharacterInst *ctx, const AptValue &function, std::int32_t argCount, std::uint32_t mask, std::uint32_t tag, bool front)
{
	AptAction a;
	a.kind = AptAction::Function;
	a.eventMask = mask;
	a.tag = tag;
	a.function = function;
	a.target = ctx;
	a.argCount = argCount;
	if (front)
	{
		pushActionFront(a);
	}
	else
	{
		pushAction(a);
	}
}

void Apt::pushFunctionCallOn(const AptValue &thisValue, const AptValue &function, const std::vector<AptValue> &args, std::uint32_t mask, std::uint32_t tag, bool front)
{
	AptAction a;
	a.kind = AptAction::Function;
	a.eventMask = mask;
	a.tag = tag;
	a.function = function;
	a.thisValue = thisValue;
	a.args = args;
	a.argCount = (std::int32_t)args.size();
	if (front)
	{
		pushActionFront(a);
	}
	else
	{
		pushAction(a);
	}
}

void Apt::noteNewInstance(AptCharacterInst *inst)
{
	m_newInsts.push_back(inst);
}

void Apt::dropActionsOf(AptCharacterInst *inst)
{
	// 0x00AE4D50: the queued actions of a destroyed instance are released
	for (AptAction &a : m_pool)
	{
		if (a.target == inst)
		{
			a.target = nullptr;
			a.code.reset();
			a.function = AptValue();
		}
	}
	m_newInsts.erase(std::remove(m_newInsts.begin(), m_newInsts.end(), inst), m_newInsts.end());
}

void Apt::notifyComponentCreated(AptCharacterInst *inst)
{
	// Instance of an exported symbol the host registered as a native component (gadget, View3D, BinkMovie ...): the host creates
	// its window.  The export list of a character is movie data, the component test is the host's (its map can change).
	if (!inst->charRef().file || !inst->charRef().character || inst->type() == AptCharacterInst::Type::Movie)
	{
		return;
	}
	if (m_componentInstances.count(inst))
	{
		return;
	}
	const AptFile &file = *inst->charRef().file;
	const std::uint32_t id = inst->charRef().character->id;
	auto key = std::make_pair(&file, id);
	auto it = m_exportNames.find(key);
	if (it == m_exportNames.end())
	{
		std::vector<std::string> names;
		for (const AptExport &e : file.exports)
		{
			if (e.characterId == id)
			{
				names.push_back(e.name);
			}
		}
		it = m_exportNames.emplace(key, std::move(names)).first;
	}
	for (const std::string &name : it->second)
	{
		if (m_userHost.isComponentSymbol(file.name, name))
		{
			m_componentInstances.insert(inst);
			m_userHost.componentInstanceCreated(*inst, file.name, name);
			return;
		}
	}
	// lane UI-2: RotWK's render components are one registry keyed by name (RW 0x814ED0 .. 0x815206 register GameWindow, HorzSlider, ComboBox,
	// ImageComboBox, CheckBox, TextEntry, ListBox, PushButton, BinkMovie, LivingWorldMap, View3D and ColorPicker through RW 0x624348, the registry
	// AptMapPreview::Picture also uses); the name a clip asks for is its `_type`. A movie's own clip that tags itself `_type = "GameWindow"`
	// (MpGameSetup's CurrentMap, character 73, exported only as "CurrentMapWin") is therefore a component although no export names it.
	AptValue type;
	if (inst->getOwn("_type", type) && type.isString())
	{
		const std::string typeName = type.toString();
		if (m_userHost.isComponentSymbol(file.name, typeName))
		{
			m_componentInstances.insert(inst);
			m_userHost.componentInstanceCreated(*inst, file.name, typeName);
		}
	}
}

void Apt::noteInstanceDestroyed(AptCharacterInst *inst)
{
	if (m_componentInstances.erase(inst))
	{
		m_userHost.componentInstanceDestroyed(*inst);
	}
}

void Apt::flushNewInstances()
{
	// 0x00AE4390: instances created by place objects or scripts that have not run their first frame yet
	for (std::size_t i = 0; i < m_newInsts.size(); ++i)
	{
		AptCharacterInst *inst = m_newInsts[i];
		if (!inst || !inst->defined())
		{
			continue;
		}
		notifyComponentCreated(inst);
		if (AptButtonInst *b = inst->asButton())
		{
			if (b->state() == AptButtonInst::State::None)
			{
				b->setState(AptButtonInst::State::Up);
			}
		}
		else if (AptSpriteInst *s = inst->asSprite())
		{
			if (inst->type() == AptCharacterInst::Type::Sprite && s->frame == -1)
			{
				s->advance();
			}
		}
	}
	m_newInsts.clear();
}

void Apt::runActions()
{
	if (m_running)
	{
		return;
	}
	m_running = true;
	while (!m_pool.empty())
	{
		AptAction a = std::move(m_pool.front());
		m_pool.pop_front();
		m_tag = a.tag;
		if (a.kind == AptAction::Code)
		{
			AptCharacterInst *target = a.target;
			if (!target || !target->defined() || !a.code)
			{
				continue; // 0x00AE6591: an undefined context is skipped
			}
			if (AptSpriteInst *s = target->asSprite())
			{
				if (a.frameGuard < 0 && -a.frameGuard != s->frame)
				{
					continue; // 0x00AE661F..0x00AE665B: the clip moved on since the actions were queued
				}
			}
			executeProgram(a.code, target);
			flushNewInstances();
		}
		else
		{
			AptCharacterInst *ctx = a.target;
			if (!a.function.isObject() || (!ctx && a.thisValue.isUndefined()))
			{
				continue;
			}
			AptCharacterInst *prev = m_scriptTarget;
			m_scriptTarget = ctx;
			m_vm->callFunction(a.function, a.thisValue.isUndefined() ? AptValue::object(ctx) : a.thisValue, a.args);
			m_scriptTarget = prev;
		}
	}
	flushNewInstances(); // 0x00AE69D4
	m_running = false;
}

// ---- scripts --------------------------------------------------------------------------------------------------

bool Apt::executeProgram(const std::shared_ptr<const AptCodeBlock> &code, AptCharacterInst *target)
{
	AptCharacterInst *prev = m_scriptTarget;
	m_scriptTarget = target;
	AptSpriteInst *root = target ? target->movieRoot() : nullptr;
	bool ok = m_vm->execute(*code, target, root);
	m_scriptTarget = prev;
	return ok;
}

void Apt::callEventProgram(const std::shared_ptr<const AptCodeBlock> &code, AptCharacterInst *target)
{
	// 0x00AE2216: a script function is built from the clip action's program and called with this = the instance
	AptFunction *fn = m_gc.create<AptFunction>();
	auto def = std::make_shared<AptFunctionDef>();
	def->name = "onClipEvent";
	def->isV2 = false;
	def->body = code;
	def->swfVersion = code->swfVersion;
	fn->def = def;
	fn->scopeTarget = target;
	fn->scopeRoot = target ? target->movieRoot() : nullptr;
	fn->setProto(m_vm->functionPrototype());
	AptCharacterInst *prev = m_scriptTarget;
	m_scriptTarget = target;
	m_vm->callFunction(AptValue::object(fn), AptValue::object(target), std::vector<AptValue>());
	m_scriptTarget = prev;
}

bool Apt::invoke(AptCharacterInst *scope, const std::string &function, const std::vector<std::string> &args, std::string *result, std::string *error)
{
	std::vector<AptValue> values;
	for (const std::string &a : args)
	{
		values.push_back(AptValue::string(a));
	}
	return invokeValues(scope, function, values, result, error);
}

bool Apt::invokeValues(AptCharacterInst *scope, const std::string &function, const std::vector<AptValue> &values, std::string *result, std::string *error)
{
	if (!scope)
	{
		if (error)
		{
			*error = "invoke(" + function + "): no scope";
		}
		return false;
	}
	AptValue fn = m_vm->getMember(AptValue::object(scope), function);
	if (!fn.isObject() || !fn.asObject() || fn.asObject()->kind() != AptObjectKind::Function)
	{
		if (error)
		{
			*error = "invoke: '" + function + "' is not a function of " + scope->targetPath();
		}
		return false;
	}
	AptCharacterInst *prev = m_scriptTarget;
	m_scriptTarget = scope;
	AptValue r = m_vm->callFunction(fn, AptValue::object(scope), values);
	m_scriptTarget = prev;
	// lane HUD-3: the host's call does NOT flush the new-instance list. BFME2's call-function entry (0x00ACCB80, RotWK's 0x00AE0CF0 via 0x0062279C) reaches
	// neither the flush (0x00AE4390, called only from the pool run 0x00AE6540 and two natives) nor the pool within six levels of direct calls: the clips a call
	// creates are first advanced by the next frame step (advanceChildren), whose pool then runs their frame-0 actions on frame 0. Flushing here advanced them
	// to frame 0 at once, so the next step moved them to frame 1 before their frame-0 `Stop` ran (the Palantir's PlayerMagic.ProgressBar showed its 1% wedge).
	if (result)
	{
		*result = r.isUndefined() ? std::string() : r.toString();
	}
	return true;
}

AptCharacterInst *Apt::resolvePath(AptCharacterInst *base, const std::string &path)
{
	if (path.empty() || path == ".")
	{
		return base;
	}
	AptCharacterInst *cur = base;
	std::size_t pos = 0;
	bool first = true;
	while (pos <= path.size())
	{
		std::size_t end = path.find_first_of("./", pos);
		if (end == std::string::npos)
		{
			end = path.size();
		}
		std::string seg = path.substr(pos, end - pos);
		pos = end + 1;
		if (seg.empty())
		{
			if (first && end == 0)
			{
				cur = base ? base->movieRoot() : nullptr; // a leading '/'
			}
			first = false;
			if (end >= path.size())
			{
				break;
			}
			continue;
		}
		first = false;
		std::string k = AptPropertyMap::foldKey(seg);
		int lvl;
		if (!cur)
		{
			return nullptr;
		}
		if (k == "_root")
		{
			cur = cur->movieRoot();
		}
		else if (k == "_parent")
		{
			cur = cur->parent();
		}
		else if (k == "this" || k == ".")
		{
			// stays
		}
		else if (parseLevelName(seg, lvl))
		{
			cur = level(lvl);
		}
		else
		{
			AptCharacterInst *next = nullptr;
			if (AptSpriteInst *s = cur->asSprite())
			{
				next = s->childByName(seg);
			}
			if (!next)
			{
				AptValue v;
				if (cur->getMember(seg, v) && v.isObject())
				{
					next = dynamic_cast<AptCharacterInst *>(v.asObject());
				}
			}
			cur = next;
		}
		if (!cur || end >= path.size())
		{
			break;
		}
	}
	return cur;
}

// ---- timers ---------------------------------------------------------------------------------------------------

std::int32_t Apt::setInterval(const AptValue &function, const AptValue &thisValue, float ms, const std::vector<AptValue> &params, AptCharacterInst *owner)
{
	AptTimer t;
	t.active = true;
	t.function = function;
	t.thisValue = thisValue;
	t.intervalMs = ms;
	t.remainingMs = ms; // UNVERIFIED (S-106): the first call comes one interval after the call
	t.params = params;
	t.owner = owner;
	for (std::size_t i = 0; i < m_timers.size(); ++i)
	{
		if (!m_timers[i].active)
		{
			m_timers[i] = t;
			return (std::int32_t)i + 1;
		}
	}
	m_timers.push_back(t);
	return (std::int32_t)m_timers.size();
}

bool Apt::clearInterval(std::int32_t id)
{
	if (id < 1 || (std::size_t)id > m_timers.size() || !m_timers[(std::size_t)id - 1].active)
	{
		return false;
	}
	m_timers[(std::size_t)id - 1] = AptTimer();
	return true;
}

void Apt::clearTimersOf(AptCharacterInst *owner)
{
	for (AptTimer &t : m_timers)
	{
		if (t.active && t.owner == owner)
		{
			t = AptTimer();
		}
	}
}

void Apt::runTimers(int frameMs)
{
	// AptTimerFunc 0x00AE4150 (RotWK counterpart 0x00AF8460, budget 0x00AF8466 / 0x00AF8470, decrement 0x00AF865E; pinned by tools/retail_oracle/test_apt_player_counterparts.py):
	// remaining -= elapsed; when it is <= 0 the function is called and the interval is added back (one call per timer per
	// step).  The walk takes the number of active entries when it starts (0x00AE4156 loads [this+0x38], 0x00AE4160 keeps it
	// in a local) and counts it down once per ACTIVE entry it visits (0x00AE434E), leaving the loop at zero (0x00AE4352).  The
	// budget is a count, not a snapshot of identities: callbacks do not update it, so a timer that is appended behind the walk
	// is usually not reached in the same step, but when a callback clears or replaces an entry that has not been visited yet
	// (and a new timer takes its slot, or the slot of a finished one ahead of the index) the new timer is visited and consumes
	// part of the budget.  The entry array is re-read every iteration (0x00AE4358: callbacks may grow it), hence the index loop.
	std::size_t budget = 0;
	for (const AptTimer &t : m_timers)
	{
		budget += t.active ? 1 : 0;
	}
	for (std::size_t i = 0; i < m_timers.size(); ++i)
	{
		if (!m_timers[i].active)
		{
			continue;
		}
		runTimerEntry(i, frameMs);
		if (--budget == 0)
		{
			break; // 0x00AE434E / 0x00AE4352: every entry that was active when the walk started has been handled
		}
	}
}

void Apt::runTimerEntry(std::size_t i, int frameMs)
{
	m_timers[i].remainingMs -= (float)frameMs;
	if (m_timers[i].remainingMs > 0.0f)
	{
		return;
	}
	AptTimer t = m_timers[i]; // the call may clear or add timers
	AptObject *fobj = t.function.isObject() ? t.function.asObject() : nullptr;
	AptFunction *fn = (fobj && fobj->kind() == AptObjectKind::Function) ? static_cast<AptFunction *>(fobj) : nullptr;
	AptObject *scope = fn ? fn->scopeTarget : nullptr;
	AptCharacterInst *scopeInst = dynamic_cast<AptCharacterInst *>(scope);
	if (!fn || (!fn->isNative() && (!scopeInst || !scopeInst->defined())))
	{
		m_timers[i] = AptTimer(); // 0x00AE432B: the function or its clip is gone
		return;
	}
	AptValue thisV = t.thisValue.isUndefined() ? (scope ? AptValue::object(scope) : AptValue()) : t.thisValue;
	AptCharacterInst *prev = m_scriptTarget;
	m_scriptTarget = scopeInst;
	m_vm->callFunction(t.function, thisV, t.params);
	m_scriptTarget = prev;
	if (i < m_timers.size() && m_timers[i].active)
	{
		m_timers[i].remainingMs += m_timers[i].intervalMs;
	}
}

// ---- loadMovie ------------------------------------------------------------------------------------------------

void Apt::requestLoad(const std::string &movie, const std::string &target)
{
	m_loadRequests.push_back({ movie, target, m_scriptTarget, nullptr });
}

void Apt::requestLoadInto(const std::string &movie, AptObject *targetClip)
{
	m_loadRequests.push_back({ movie, std::string(), m_scriptTarget, dynamic_cast<AptCharacterInst *>(targetClip) });
}

void Apt::processLoads()
{
	// 0x00AD17F0: requests complete at the end of the step; the target clip becomes the loaded movie and its first frame
	// runs at once
	std::vector<LoadRequest> requests;
	requests.swap(m_loadRequests);
	int completed = 0;
	for (const LoadRequest &req : requests)
	{

		AptCharacterInst *ctx = req.context;
		int lvl = -1;
		bool isLevel = req.targetClip ? false : parseLevelName(req.target, lvl);
		if (req.movie.empty())
		{
			// getURL("", target): unload
			if (isLevel)
			{
				unloadLevel(lvl);
			}
			else if (AptCharacterInst *t = req.targetClip ? req.targetClip : resolvePath(ctx, req.target))
			{
				if (AptSpriteInst *p = dynamic_cast<AptSpriteInst *>(t->parent()))
				{
					p->removeObject(t->depth());
				}
			}
			continue;
		}
		std::string error;
		if (isLevel)
		{
			if (!loadMovie(lvl, req.movie, &error))
			{
				m_vm->reportError("loadMovie(\"" + req.movie + ".swf\", \"" + req.target + "\") failed: " + error);
			}
			else
			{
				++completed; // loadMovie already ran the pool for this level
			}
			continue;
		}
		AptCharacterInst *t = req.targetClip ? req.targetClip : resolvePath(ctx, req.target);
		AptSpriteInst *clip = t ? t->asSprite() : nullptr;
		if (!clip)
		{
			m_vm->reportError("loadMovie(\"" + req.movie + ".swf\", \"" + req.target + "\"): the target is not a movie clip");
			continue;
		}
		std::shared_ptr<const AptFile> mv = m_loader.loadMovie(req.movie, &error);
		if (!mv || !importsFor(mv, &error))
		{
			m_vm->reportError("loadMovie(\"" + req.movie + ".swf\", \"" + req.target + "\") failed: " + error);
			continue;
		}
		note("movie-in-clip-root-is-level-root", clip->targetPath()); // S-105: Flash semantics, EA not traced
		clip->becomeMovie(mv);
		clip->advance();
		++completed;
	}
	if (completed > 0)
	{
		runActions(); // 0x00AD1C58: the pool runs after the loads of this pass completed
	}
}

// ---- duplicate / remove ---------------------------------------------------------------------------------------

bool Apt::scriptDepth(const AptValue &arg, const char *native, int &out)
{
	const std::int64_t sum = (std::int64_t)arg.toInteger() + kScriptDepthBase;
	if (sum > std::numeric_limits<std::int32_t>::max())
	{
		note("script-depth-out-of-range", std::string(native) + ": " + std::to_string(arg.toInteger()));
		m_vm->reportError(std::string(native) + ": the depth " + std::to_string(arg.toInteger()) + " plus 0x4000 does not fit a 32-bit display-list depth; nothing was changed");
		return false;
	}
	out = (int)sum;
	return true;
}

std::int32_t Apt::scriptDepthOf(int displayDepth)
{
	return (std::int32_t)((std::uint32_t)displayDepth - (std::uint32_t)kScriptDepthBase);
}

bool Apt::duplicateClip(AptSpriteInst *context, const AptValue &source, const AptValue &newName, const AptValue &depth, AptCharacterInst **created)
{
	AptCharacterInst *src = nullptr;
	if (source.isObject())
	{
		src = dynamic_cast<AptCharacterInst *>(source.asObject());
	}
	else
	{
		src = resolvePath(context, source.toString());
	}
	AptSpriteInst *parent = src ? dynamic_cast<AptSpriteInst *>(src->parent()) : nullptr;
	if (!src || !parent || !src->charRef().character)
	{
		m_vm->reportError("duplicateMovieClip: the source clip does not resolve to a placed character");
		return false;
	}
	int d = 0;
	if (!scriptDepth(depth, "duplicateMovieClip", d))
	{
		return false;
	}
	AptCharacterInst *copy = parent->attachCharacter(src->charRef(), newName.toString(), d, true);
	if (!copy)
	{
		return false;
	}
	if (created)
	{
		*created = copy;
	}
	copy->matrix = src->matrix;
	copy->color = src->color;
	copy->visible = src->visible;
	copy->clipDepth = src->clipDepth;
	note("duplicate-copies-placement-only", std::string()); // UNVERIFIED (S-104): clip actions and properties
	return true;
}

bool Apt::removeClipByTarget(AptSpriteInst *context, const AptValue &target)
{
	AptCharacterInst *t = target.isObject() ? dynamic_cast<AptCharacterInst *>(target.asObject()) : resolvePath(context, target.toString());
	if (!t)
	{
		m_vm->reportError("removeMovieClip: the target does not resolve to a clip");
		return false;
	}
	if (AptSpriteInst *p = dynamic_cast<AptSpriteInst *>(t->parent()))
	{
		// the target must be the live occupant of its depth: a reference kept across a replacement (createEmptyMovieClip, attachMovie
		// or a place object over the depth), an unload or a depth change is stale and must not remove whatever is there now
		if (!t->defined() || t->destroying() || p->childAtDepth(t->depth()) != t)
		{
			m_vm->reportError("removeMovieClip: the target is not a live clip of its parent (a stale reference)");
			return false;
		}
		p->removeObject(t->depth());
		return true;
	}
	if (!t->defined())
	{
		m_vm->reportError("removeMovieClip: the target is not a live clip (a stale reference)");
		return false;
	}
	m_vm->reportError("removeMovieClip: a level root cannot be removed with removeMovieClip");
	return false;
}

// ---- GC -------------------------------------------------------------------------------------------------------

void Apt::markRoots(AptGC &gc)
{
	m_vm->markRoots(gc);
	for (const auto &kv : m_levels)
	{
		gc.mark(kv.second);
	}
	gc.mark(m_movieClipProto);
	gc.mark(m_textFieldProto);
	gc.mark(m_colorProto);
	for (AptAction &a : m_pool)
	{
		gc.mark(a.target);
		gc.mark(a.function);
		gc.mark(a.thisValue);
		for (const AptValue &v : a.args)
		{
			gc.mark(v);
		}
	}
	for (AptCharacterInst *i : m_newInsts)
	{
		gc.mark(i);
	}
	for (AptTimer &t : m_timers)
	{
		if (t.active)
		{
			gc.mark(t.function);
			gc.mark(t.thisValue);
			for (const AptValue &p : t.params)
			{
				gc.mark(p);
			}
			gc.mark(t.owner);
		}
	}
	for (const LoadRequest &r : m_loadRequests)
	{
		gc.mark(r.context);
		gc.mark(r.targetClip);
	}
	gc.mark(m_scriptTarget);
	m_input->markRoots(gc);
}

void Apt::collectGarbage()
{
	if (m_running)
	{
		return;
	}
	m_gc.collect([this](AptGC &gc) { markRoots(gc); });
	m_gcAfter = std::max<std::size_t>(4096, m_gc.objectCount() * 2);
}

// ---- debugging ------------------------------------------------------------------------------------------------

std::string Apt::dumpTree(AptCharacterInst *root, int maxDepth) const
{
	std::string out;
	std::function<void(AptCharacterInst *, int)> rec = [&](AptCharacterInst *c, int ind) {
		out += std::string((std::size_t)ind * 2, ' ');
		char buf[200];
		const AptSpriteInst *s = c->asSprite();
		std::snprintf(buf, sizeof buf, "[%02x] d=%d name='%s' xy=(%g,%g)", (unsigned)c->type(), c->depth(), c->instName().c_str(), c->matrix.tx, c->matrix.ty);
		out += buf;
		if (s)
		{
			std::snprintf(buf, sizeof buf, " frame=%d/%d %s", s->frame, s->totalFrames(), s->playing ? "playing" : "stopped");
			out += buf;
		}
		out += "\n";
		if (ind < maxDepth)
		{
			if (s)
			{
				for (AptCharacterInst *k : s->children())
				{
					rec(k, ind + 1);
				}
			}
			else if (c->asButton())
			{
				for (AptCharacterInst *k : c->asButton()->children())
				{
					rec(k, ind + 1);
				}
			}
		}
	};
	rec(root, 0);
	return out;
}
