// OpenBFME. GPL-3.0.
//
// MovieClip, Color, TextField and the timer / clock natives of the EA Apt runtime (menus-apt.md 3.2 "Natives").
//
// Target facts (BFME2 1.06): the MovieClip method names are the entries of the native name hash at 0x00DDD218
// (attachMovie, loadMovie, startDrag, duplicateMovieClip, gotoAndStop, gotoAndPlay, getDepth, hitTest, swapDepths,
// getBounds, localToGlobal, getURL, unloadMovie, removeMovieClip, play, createEmptyMovieClip, prevFrame, nextFrame,
// setMask ... plus setInterval / clearInterval as globals).  Read bodies:
//   gotoAndPlay / gotoAndStop  0x00AED470 / 0x00AED450 -> 0x00AED390: a string argument is a label (index lookup,
//        absent = no-op), anything else is `toInteger(arg) - 1` (ONE-based, below 1 = no-op); gotoFrame(frame) then
//        the play flag is set to 1 / 0.
//   play 0x00AEE210 (sets the play bit), stop (clears it), nextFrame 0x00AEE270 / prevFrame 0x00AEE310: gotoFrame(frame
//        +-1) and clear the play bit.
// The other bodies were not read; they follow the Flash 7 MovieClip API and are registered as unverified (S-104).  Only names that
// are in the binary's native name table are defined: globalToLocal, stopDrag, getBytesLoaded/Total and the global getTimer are
// NOT there (their strings do not occur in game.dat), so a script calling them gets the loud "not a function" error.

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <cmath>

namespace
{

// The Key object: reading one of its key constants is an assumption (S-108: Flash key codes; EA's ids are not decoded) and is
// reported at the read.
class AptKeyObject : public AptObject
{
public:
	explicit AptKeyObject(Apt &apt) : m_apt(apt) {}
	bool getOwn(const std::string &name, AptValue &out) const override
	{
		const bool found = AptObject::getOwn(name, out);
		if (found && out.isNumber())
		{
			m_apt.note("key-mouse-native-unread", "Key." + name); // UNVERIFIED (S-108)
		}
		return found;
	}

private:
	Apt &m_apt;
};

AptCharacterInst *thisInst(AptCallInfo &c)
{
	if (c.thisValue.isObject() && c.thisValue.asObject())
	{
		return dynamic_cast<AptCharacterInst *>(c.thisValue.asObject());
	}
	return nullptr;
}

const AptValue &argAt(const AptCallInfo &c, std::size_t i)
{
	static const AptValue undef;
	return i < c.args.size() ? c.args[i] : undef;
}

AptObject *makeRect(AptActionInterpreter &vm, float x0, float x1, float y0, float y1)
{
	AptObject *o = vm.newObject();
	o->setMember("xMin", AptValue::number(x0));
	o->setMember("xMax", AptValue::number(x1));
	o->setMember("yMin", AptValue::number(y0));
	o->setMember("yMax", AptValue::number(y1));
	return o;
}

bool getFloatMember(AptActionInterpreter &vm, const AptValue &obj, const char *name, float &out)
{
	AptValue v = vm.getMember(obj, name);
	if (v.isUndefined())
	{
		return false;
	}
	out = v.toNumber();
	return true;
}

// Global bounds of `inst`'s content (stage coordinates).
bool stageBounds(const AptCharacterInst &inst, float &x0, float &y0, float &x1, float &y1)
{
	float cx0, cy0, cx1, cy1;
	if (!inst.contentBounds(cx0, cy0, cx1, cy1))
	{
		return false;
	}
	AptMatrix g = inst.globalMatrix();
	// contentBounds is in the instance's own space; the instance's own matrix is already part of globalMatrix
	const float xs[4] = { cx0, cx1, cx0, cx1 };
	const float ys[4] = { cy0, cy0, cy1, cy1 };
	x0 = y0 = 1e30f;
	x1 = y1 = -1e30f;
	for (int i = 0; i < 4; ++i)
	{
		float ox, oy;
		g.apply(xs[i], ys[i], ox, oy);
		x0 = std::min(x0, ox);
		y0 = std::min(y0, oy);
		x1 = std::max(x1, ox);
		y1 = std::max(y1, oy);
	}
	return true;
}

// gotoAndPlay / gotoAndStop (0x00AED390)
AptValue gotoAndNative(AptCallInfo &c, bool play)
{
	AptCharacterInst *inst = thisInst(c);
	AptSpriteInst *s = inst ? inst->asSprite() : nullptr;
	if (!s)
	{
		c.vm.reportError("gotoAndPlay/gotoAndStop on a value that is not a movie clip");
		return AptValue();
	}
	if (c.args.empty())
	{
		return AptValue();
	}
	int frame;
	const AptValue &a = c.args[0];
	if (a.isString())
	{
		frame = s->labelFrame(a.asString()); // label index; -1 when absent
	}
	else
	{
		frame = a.toInteger() - 1; // one-based
	}
	if (frame < 0)
	{
		if (a.isString())
		{
			s->apt().note("label-not-found", a.asString());
		}
		return AptValue();
	}
	s->gotoFrame(frame);
	s->playing = play;
	return AptValue();
}

} // namespace

void Apt::installNatives()
{
	AptActionInterpreter &vm = *m_vm;

	m_movieClipProto = vm.newObject();
	m_textFieldProto = vm.newObject();
	m_colorProto = vm.newObject();
	Apt *self = this;

	// ---- globals --------------------------------------------------------------------------------------------
	{
		AptFunction *ctor = vm.defineGlobalNative("MovieClip", [](AptCallInfo &c) -> AptValue {
			c.vm.reportError("new MovieClip(): constructing a movie clip from script has no decoded EA behaviour (S-104)");
			return AptValue();
		});
		ctor->setMember("prototype", AptValue::object(m_movieClipProto));
		m_movieClipProto->setNativeMember("constructor", AptValue::object(ctor));
	}
	{
		AptFunction *ctor = vm.defineGlobalNative("TextField", [](AptCallInfo &c) -> AptValue {
			c.vm.reportError("new TextField(): constructing a text field from script has no decoded EA behaviour (S-104)");
			return AptValue();
		});
		ctor->setMember("prototype", AptValue::object(m_textFieldProto));
	}
	vm.defineGlobalNative("setInterval", [self](AptCallInfo &c) -> AptValue {
		// setInterval(function, ms, params...) or setInterval(object, "method", ms, params...)
		if (c.args.size() < 2)
		{
			c.vm.reportError("setInterval needs at least a function and an interval");
			return AptValue();
		}
		AptValue function, thisV;
		float ms;
		std::size_t first;
		if (c.args[0].isObject() && c.args[0].asObject() && c.args[0].asObject()->kind() == AptObjectKind::Function)
		{
			function = c.args[0];
			ms = c.args[1].toNumber();
			first = 2;
		}
		else if (c.args.size() >= 3 && c.args[1].isString())
		{
			thisV = c.args[0];
			function = c.vm.getMember(thisV, c.args[1].asString());
			ms = c.args[2].toNumber();
			first = 3;
			if (!function.isObject())
			{
				c.vm.reportError("setInterval: '" + c.args[1].asString() + "' is not a function of the target object");
				return AptValue();
			}
		}
		else
		{
			c.vm.reportError("setInterval: the first argument is not a function");
			return AptValue();
		}
		std::vector<AptValue> params(c.args.begin() + (std::ptrdiff_t)first, c.args.end());
		self->note("setInterval-id-and-first-call", std::string()); // UNVERIFIED (S-106): the id (slot + 1) and the first call one interval later
		return AptValue::integer(self->setInterval(function, thisV, ms, params, self->scriptTarget()));
	});
	vm.defineGlobalNative("clearInterval", [self](AptCallInfo &c) -> AptValue {
		if (c.args.empty() || !self->clearInterval(c.args[0].toInteger()))
		{
			self->note("clearInterval-unknown-id", c.args.empty() ? "" : c.args[0].toString());
		}
		return AptValue();
	});

	// ---- MovieClip prototype ---------------------------------------------------------------------------------
	AptObject *mc = m_movieClipProto;
	vm.defineNative(mc, "gotoAndPlay", [](AptCallInfo &c) { return gotoAndNative(c, true); });
	vm.defineNative(mc, "gotoAndStop", [](AptCallInfo &c) { return gotoAndNative(c, false); });
	vm.defineNative(mc, "play", [](AptCallInfo &c) -> AptValue {
		if (AptCharacterInst *i = thisInst(c))
		{
			if (AptSpriteInst *s = i->asSprite())
			{
				s->playing = true;
			}
		}
		return AptValue();
	});
	vm.defineNative(mc, "stop", [](AptCallInfo &c) -> AptValue {
		if (AptCharacterInst *i = thisInst(c))
		{
			if (AptSpriteInst *s = i->asSprite())
			{
				s->playing = false;
			}
		}
		return AptValue();
	});
	vm.defineNative(mc, "nextFrame", [](AptCallInfo &c) -> AptValue {
		if (AptCharacterInst *i = thisInst(c))
		{
			if (AptSpriteInst *s = i->asSprite())
			{
				s->gotoFrame(s->frame + 1);
				s->playing = false;
			}
		}
		return AptValue();
	});
	vm.defineNative(mc, "prevFrame", [](AptCallInfo &c) -> AptValue {
		if (AptCharacterInst *i = thisInst(c))
		{
			if (AptSpriteInst *s = i->asSprite())
			{
				s->gotoFrame(s->frame - 1);
				s->playing = false;
			}
		}
		return AptValue();
	});
	vm.defineNative(mc, "getDepth", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "getDepth"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		return i ? AptValue::integer(Apt::scriptDepthOf(i->depth())) : AptValue();
	});
	vm.defineNative(mc, "attachMovie", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "attachMovie"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		AptSpriteInst *s = i ? i->asSprite() : nullptr;
		if (!s || c.args.size() < 3)
		{
			c.vm.reportError("attachMovie needs a movie clip and (linkage name, new name, depth)");
			return AptValue();
		}
		std::string linkage = c.args[0].toString();
		// the exports of the movie that owns the clip's timeline (UNVERIFIED S-104: which movie, and the compare)
		std::shared_ptr<const AptFile> file = s->timelineFile;
		AptCharRef ref;
		std::string error;
		std::uint32_t id;
		bool ambiguous = false;
		if (!file || !file->findExport(linkage, id, &ambiguous))
		{
			c.vm.reportError("attachMovie: no export named '" + linkage + "'");
			return AptValue();
		}
		if (!self->resolveCharacter(file, id, ref, &error))
		{
			c.vm.reportError("attachMovie('" + linkage + "'): " + error);
			return AptValue();
		}
		int depth = 0;
		if (!self->scriptDepth(c.args[2], "attachMovie", depth))
		{
			return AptValue();
		}
		AptCharacterInst *inst = s->attachCharacter(ref, c.args[1].toString(), depth, true);
		if (!inst)
		{
			return AptValue();
		}
		if (c.args.size() > 3 && c.args[3].isObject() && c.args[3].asObject())
		{
			std::vector<std::string> names;
			std::string err;
			if (c.args[3].asObject()->enumerateForIn(names, err))
			{
				for (const std::string &n : names)
				{
					inst->setMember(n, c.vm.getMember(c.args[3], n));
				}
			}
		}
		self->runInitActionsFor(file, id, s);
		// lane HUD-1 (stop S-292): the new instance runs its first frame NOW. The movies size an attached clip right after attaching it (libInGameUI CreateContent:
		// `contentClip._width = placeholder._width`), which only has an effect once the clip's art exists; whether retail's attachMovie flushes the new-instance list (0x00AE4390) or
		// its `_width` setter remembers the request was not traced.
		self->flushNewInstances();
		return AptValue::object(inst);
	});
	vm.defineNative(mc, "createEmptyMovieClip", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "createEmptyMovieClip"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		AptSpriteInst *s = i ? i->asSprite() : nullptr;
		if (!s || c.args.size() < 2)
		{
			c.vm.reportError("createEmptyMovieClip needs a movie clip and (name, depth)");
			return AptValue();
		}
		int depth = 0;
		if (!self->scriptDepth(c.args[1], "createEmptyMovieClip", depth))
		{
			return AptValue();
		}
		AptSpriteInst *created = s->createEmptyClip(c.args[0].toString(), depth);
		return created ? AptValue::object(created) : AptValue();
	});
	vm.defineNative(mc, "duplicateMovieClip", [self](AptCallInfo &c) -> AptValue {
		AptCharacterInst *i = thisInst(c);
		AptSpriteInst *s = i ? i->asSprite() : nullptr;
		if (!s || c.args.size() < 2)
		{
			c.vm.reportError("duplicateMovieClip needs (new name, depth)");
			return AptValue();
		}
		AptCharacterInst *copy = nullptr;
		self->duplicateClip(s, AptValue::object(s), c.args[0], c.args[1], &copy);
		return copy ? AptValue::object(copy) : AptValue();
	});
	vm.defineNative(mc, "removeMovieClip", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "removeMovieClip"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		if (i)
		{
			self->removeClipByTarget(i->asSprite(), AptValue::object(i));
		}
		return AptValue();
	});
	vm.defineNative(mc, "loadMovie", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "loadMovie"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		if (!i || c.args.empty())
		{
			return AptValue();
		}
		std::string url = c.args[0].toString();
		std::string movie;
		std::size_t n = url.size();
		if (n >= 4 && AptPropertyMap::foldKey(url.substr(n - 4)) == ".swf")
		{
			movie = url.substr(0, n - 4);
		}
		else
		{
			c.vm.reportError("loadMovie: '" + url + "' is not a .swf movie (S-104)");
			return AptValue();
		}
		AptCharacterInst *prev = self->m_scriptTarget;
		self->m_scriptTarget = i;
		self->requestLoad(movie, "this");
		self->m_scriptTarget = prev;
		self->host().loadMovie(movie, i->targetPath());
		return AptValue();
	});
	vm.defineNative(mc, "unloadMovie", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "unloadMovie"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		if (i)
		{
			AptCharacterInst *prev = self->m_scriptTarget;
			self->m_scriptTarget = i;
			self->requestLoad(std::string(), "this");
			self->m_scriptTarget = prev;
		}
		return AptValue();
	});
	vm.defineNative(mc, "getURL", [self](AptCallInfo &c) -> AptValue {
		if (c.args.empty())
		{
			return AptValue();
		}
		std::string url = c.args[0].toString();
		std::string target = c.args.size() > 1 ? c.args[1].toString() : std::string();
		static const char kPrefix[] = "FSCommand:";
		if (url.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0)
		{
			self->host().fscommand(url.substr(sizeof(kPrefix) - 1), target);
		}
		else
		{
			self->host().getURL(url, target);
		}
		return AptValue();
	});
	vm.defineNative(mc, "swapDepths", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "swapDepths"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		AptSpriteInst *p = i ? dynamic_cast<AptSpriteInst *>(i->parent()) : nullptr;
		if (!i || !p || c.args.empty())
		{
			return AptValue();
		}
		// Everything is validated before anything is mutated: the receiver must be the live occupant of its depth in its parent, an
		// object argument a live sibling (the same parent: a root, an ancestor, a descendant, a clip of another parent or a
		// non-clip object would make the display list a graph with a cycle or a foreign owner).
		auto live = [p](const AptCharacterInst *x) { return x && x->defined() && !x->destroying() && x->parent() == p && p->childAtDepth(x->depth()) == x; };
		if (!live(i))
		{
			c.vm.reportError("swapDepths: the receiver is not a live clip of its parent (a stale reference)");
			return AptValue();
		}
		int newDepth = 0;
		AptCharacterInst *other = nullptr;
		if (c.args[0].isObject())
		{
			other = dynamic_cast<AptCharacterInst *>(c.args[0].asObject());
			if (!live(other))
			{
				c.vm.reportError("swapDepths: the target is not a live sibling clip");
				return AptValue();
			}
			newDepth = other->depth();
		}
		else
		{
			if (!self->scriptDepth(c.args[0], "swapDepths", newDepth))
			{
				return AptValue();
			}
			other = p->childAtDepth(newDepth);
		}
		int old = i->depth();
		p->reorderChild(i, newDepth);
		if (other && other != i)
		{
			p->reorderChild(other, old);
		}
		return AptValue();
	});
	vm.defineNative(mc, "getBounds", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "getBounds"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		if (!i)
		{
			return AptValue();
		}
		AptCharacterInst *target = i;
		if (!c.args.empty() && !c.args[0].isUndefined())
		{
			// the coordinate space: a placed clip.  Anything else (a plain object, an array, null, a number) has no matrix.
			target = c.args[0].isObject() ? dynamic_cast<AptCharacterInst *>(c.args[0].asObject()) : nullptr;
			if (!target || !target->defined())
			{
				c.vm.reportError("getBounds: the target space is not a live clip");
				return AptValue();
			}
		}
		float x0, y0, x1, y1;
		if (!stageBounds(*i, x0, y0, x1, y1))
		{
			return AptValue::object(makeRect(c.vm, 0, 0, 0, 0));
		}
		bool ok = false;
		AptMatrix inv = target->globalMatrix().inverse(&ok);
		if (ok)
		{
			float xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
			float nx0 = 1e30f, ny0 = 1e30f, nx1 = -1e30f, ny1 = -1e30f;
			for (int k = 0; k < 4; ++k)
			{
				float ox, oy;
				inv.apply(xs[k], ys[k], ox, oy);
				nx0 = std::min(nx0, ox);
				ny0 = std::min(ny0, oy);
				nx1 = std::max(nx1, ox);
				ny1 = std::max(ny1, oy);
			}
			x0 = nx0;
			y0 = ny0;
			x1 = nx1;
			y1 = ny1;
		}
		return AptValue::object(makeRect(c.vm, x0, x1, y0, y1));
	});
	vm.defineNative(mc, "localToGlobal", [self](AptCallInfo &c) -> AptValue {
		self->note("movieclip-native-unread", "localToGlobal"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
		AptCharacterInst *i = thisInst(c);
		if (!i || c.args.empty())
		{
			return AptValue();
		}
		float x, y;
		if (!getFloatMember(c.vm, c.args[0], "x", x) || !getFloatMember(c.vm, c.args[0], "y", y))
		{
			return AptValue();
		}
		float ox, oy;
		i->globalMatrix().apply(x, y, ox, oy);
		c.vm.setMember(c.args[0], "x", AptValue::number(ox));
		c.vm.setMember(c.args[0], "y", AptValue::number(oy));
		return AptValue();
	});
	vm.defineNative(mc, "hitTest", [self](AptCallInfo &c) -> AptValue {
		AptCharacterInst *i = thisInst(c);
		if (!i)
		{
			return AptValue::boolean(false);
		}
		float x0, y0, x1, y1;
		if (!stageBounds(*i, x0, y0, x1, y1))
		{
			return AptValue::boolean(false);
		}
		if (c.args.size() >= 2 && c.args[0].isNumber() && c.args[1].isNumber())
		{
			float px = c.args[0].toNumber(), py = c.args[1].toNumber();
			if (c.args.size() >= 3 && c.args[2].toBoolean(7))
			{
				self->note("hitTest-shape-flag-uses-bounds", i->targetPath()); // UNVERIFIED (S-104)
			}
			return AptValue::boolean(px >= x0 && px <= x1 && py >= y0 && py <= y1);
		}
		if (!c.args.empty() && c.args[0].isObject())
		{
			AptCharacterInst *o = dynamic_cast<AptCharacterInst *>(c.args[0].asObject());
			float a0, b0, a1, b1;
			if (o && stageBounds(*o, a0, b0, a1, b1))
			{
				return AptValue::boolean(!(a1 < x0 || a0 > x1 || b1 < y0 || b0 > y1));
			}
		}
		return AptValue::boolean(false);
	});
	vm.defineNative(mc, "startDrag", [self](AptCallInfo &c) -> AptValue {
		AptCharacterInst *i = thisInst(c);
		self->note("startDrag-unsupported", i ? i->targetPath() : std::string());
		return AptValue();
	});
	vm.defineNative(mc, "setMask", [self](AptCallInfo &c) -> AptValue {
		AptCharacterInst *i = thisInst(c);
		self->note("setMask-unsupported", i ? i->targetPath() : std::string());
		return AptValue();
	});

	// ---- TextField prototype: the movie clip members plus the text format calls ---------------------------------
	m_textFieldProto->setProto(m_movieClipProto);
	for (const char *name : { "setTextFormat", "getTextFormat", "getNewTextFormat" })
	{
		vm.defineNative(m_textFieldProto, name, [self, name](AptCallInfo &) -> AptValue {
			self->note("textformat-unsupported", name);
			return AptValue();
		});
	}

	// ---- Color ---------------------------------------------------------------------------------------------------
	{
		AptFunction *ctor = vm.defineGlobalNative("Color", [self](AptCallInfo &c) -> AptValue {
			self->note("movieclip-native-unread", "Color"); // UNVERIFIED (S-104)
			AptObject *o = c.vm.newObject();
			o->setProto(self->m_colorProto);
			o->setMember("target", c.args.empty() ? AptValue() : c.args[0]);
			return AptValue::object(o);
		});
		ctor->setMember("prototype", AptValue::object(m_colorProto));
		auto targetOf = [](AptCallInfo &c) -> AptCharacterInst * {
			if (!c.thisValue.isObject())
			{
				return nullptr;
			}
			AptValue t = c.vm.getMember(c.thisValue, "target");
			return t.isObject() ? dynamic_cast<AptCharacterInst *>(t.asObject()) : nullptr;
		};
		vm.defineNative(m_colorProto, "setRGB", [self, targetOf](AptCallInfo &c) -> AptValue {
			self->note("movieclip-native-unread", "Color.setRGB"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			AptCharacterInst *t = targetOf(c);
			if (!t || c.args.empty())
			{
				return AptValue();
			}
			std::uint32_t rgb = (std::uint32_t)c.args[0].toInteger();
			t->color.mul[0] = t->color.mul[1] = t->color.mul[2] = 0.0f;
			t->color.add[0] = (float)((rgb >> 16) & 0xFF);
			t->color.add[1] = (float)((rgb >> 8) & 0xFF);
			t->color.add[2] = (float)(rgb & 0xFF);
			return AptValue();
		});
		vm.defineNative(m_colorProto, "getRGB", [self, targetOf](AptCallInfo &c) -> AptValue {
			self->note("movieclip-native-unread", "Color.getRGB"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			AptCharacterInst *t = targetOf(c);
			if (!t)
			{
				return AptValue();
			}
			std::uint32_t rgb = ((std::uint32_t)t->color.add[0] << 16) | ((std::uint32_t)t->color.add[1] << 8) | (std::uint32_t)t->color.add[2];
			return AptValue::integer((std::int32_t)rgb);
		});
		vm.defineNative(m_colorProto, "setTransform", [self, targetOf](AptCallInfo &c) -> AptValue {
			self->note("movieclip-native-unread", "Color.setTransform"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			AptCharacterInst *t = targetOf(c);
			if (!t || c.args.empty())
			{
				return AptValue();
			}
			static const char *const mulNames[4] = { "ra", "ga", "ba", "aa" };
			static const char *const addNames[4] = { "rb", "gb", "bb", "ab" };
			for (int k = 0; k < 4; ++k)
			{
				float v;
				if (getFloatMember(c.vm, c.args[0], mulNames[k], v))
				{
					t->color.mul[k] = v / 100.0f;
				}
				if (getFloatMember(c.vm, c.args[0], addNames[k], v))
				{
					t->color.add[k] = v;
				}
			}
			return AptValue();
		});
		vm.defineNative(m_colorProto, "getTransform", [self, targetOf](AptCallInfo &c) -> AptValue {
			self->note("movieclip-native-unread", "Color.getTransform"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			AptCharacterInst *t = targetOf(c);
			AptObject *o = c.vm.newObject();
			if (t)
			{
				static const char *const mulNames[4] = { "ra", "ga", "ba", "aa" };
				static const char *const addNames[4] = { "rb", "gb", "bb", "ab" };
				for (int k = 0; k < 4; ++k)
				{
					o->setMember(mulNames[k], AptValue::number(t->color.mul[k] * 100.0f));
					o->setMember(addNames[k], AptValue::number(t->color.add[k]));
				}
			}
			return AptValue::object(o);
		});
	}

	// ---- Mouse / Key (AptInput; the binary has both objects with isDown / getCode / addListener ..., bodies not read: S-108) ----
	{
		AptObject *mouse = vm.newObject();
		vm.global()->props.set("Mouse", AptValue::object(mouse));
		vm.defineNative(mouse, "addListener", [self](AptCallInfo &c) -> AptValue {
			self->note("key-mouse-native-unread", "Mouse.addListener"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			if (!c.args.empty())
			{
				self->input().addMouseListener(c.args[0]);
			}
			return AptValue::boolean(true);
		});
		vm.defineNative(mouse, "removeListener", [self](AptCallInfo &c) -> AptValue {
			self->note("key-mouse-native-unread", "Mouse.removeListener"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			if (!c.args.empty())
			{
				self->input().removeMouseListener(c.args[0]);
			}
			return AptValue::boolean(true);
		});

		AptObject *key = m_gc.create<AptKeyObject>(*this);
		key->setProto(vm.newObject()->proto());
		vm.global()->props.set("Key", AptValue::object(key));
		static const struct { const char *name; int code; } kKeys[] = { { "BACKSPACE", 8 }, { "CAPSLOCK", 20 }, { "CONTROL", 17 }, { "DELETEKEY", 46 }, { "DOWN", 40 },
			{ "END", 35 }, { "ENTER", 13 }, { "ESCAPE", 27 }, { "HOME", 36 }, { "INSERT", 45 }, { "LEFT", 37 }, { "PGDN", 34 }, { "PGUP", 33 }, { "RIGHT", 39 },
			{ "SHIFT", 16 }, { "SPACE", 32 }, { "TAB", 9 }, { "UP", 38 } };
		for (const auto &k : kKeys)
		{
			key->props.set(k.name, AptValue::integer(k.code));
		}
		vm.defineNative(key, "addListener", [self](AptCallInfo &c) -> AptValue {
			self->note("key-mouse-native-unread", "Key.addListener"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			if (!c.args.empty())
			{
				self->input().addKeyListener(c.args[0]);
			}
			return AptValue();
		});
		vm.defineNative(key, "removeListener", [self](AptCallInfo &c) -> AptValue {
			self->note("key-mouse-native-unread", "Key.removeListener"); // UNVERIFIED (S-104 / S-108): the body of this native was not read
			if (!c.args.empty())
			{
				self->input().removeKeyListener(c.args[0]);
			}
			return AptValue::boolean(true);
		});
		vm.defineNative(key, "isDown", [self](AptCallInfo &c) -> AptValue {
			self->note("key-mouse-native-unread", "Key.isDown"); // UNVERIFIED (S-108)
			return AptValue::boolean(!c.args.empty() && self->input().isKeyDown(c.args[0].toInteger()));
		});
		vm.defineNative(key, "getCode", [self](AptCallInfo &) -> AptValue {
			self->note("key-mouse-native-unread", "Key.getCode"); // UNVERIFIED (S-108)
			return AptValue::integer(self->input().lastKeyCode());
		});

	}
}
