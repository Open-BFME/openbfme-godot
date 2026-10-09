// OpenBFME. GPL-3.0.
// See AptCharacterInst.h for the citations.

#include "Libraries/Source/Apt/AptCharacterInst.h"

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{

constexpr float kPi = 3.14159265358979323846f;

std::string lowerName(const std::string &name)
{
	return AptPropertyMap::foldKey(name);
}

// Script-created clips live at depths >= 0x4000 (the display-list reconcile loop leaves them alone:
// AptDisplayList 0x00AF943F `cmp eax, 0x4000; jge`).
constexpr int kScriptDepthBase = 0x4000;

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// event table
// ---------------------------------------------------------------------------------------------------------------

const std::vector<AptEventNameEntry> &AptEventNames()
{
	// BFME2 0x00DDC2E8, 17 entries in table order (the string ids 0x68 0x70 0x71 0x72 0x6C 0x6E 0x6D 0x6A 0x69 0x6B
	// 0x75 0x65 0x74 0x73 0x67 0x66 0x6F resolve to the names below through the string pool built at 0x00B0B6F0).
	static const std::vector<AptEventNameEntry> table = {
		{ APT_EVT_ENTERFRAME, "onEnterFrame" },
		{ APT_EVT_PRESS, "onPress" },
		{ APT_EVT_RELEASE, "onRelease" },
		{ APT_EVT_RELEASEOUTSIDE, "onReleaseOutside" },
		{ APT_EVT_MOUSEDOWN, "onMouseDown" },
		{ APT_EVT_MOUSEUP, "onMouseUp" },
		{ APT_EVT_MOUSEMOVE, "onMouseMove" },
		{ APT_EVT_KEYUP, "onKeyUp" },
		{ APT_EVT_KEYDOWN, "onKeyDown" },
		{ APT_EVT_LOAD, "onLoad" },
		{ APT_EVT_UNLOAD, "onUnload" },
		{ APT_EVT_DATA, "onData" },
		{ APT_EVT_ROLLOVER, "onRollOver" },
		{ APT_EVT_ROLLOUT, "onRollOut" },
		{ APT_EVT_DRAGOVER, "onDragOver" },
		{ APT_EVT_DRAGOUT, "onDragOut" },
		{ APT_EVT_MOUSEWHEEL, "onMouseWheel" }
	};
	return table;
}

std::size_t AptEventTableIndex(std::uint32_t mask, bool *found)
{
	const std::vector<AptEventNameEntry> &t = AptEventNames();
	for (std::size_t i = 0; i < t.size(); ++i)
	{
		if (t[i].mask & mask)
		{
			if (found)
			{
				*found = true;
			}
			return i;
		}
	}
	if (found)
	{
		*found = false;
	}
	return 0; // 0x00AE22E3: the index stays 0 when no entry matches
}

// ---------------------------------------------------------------------------------------------------------------
// matrix / colour
// ---------------------------------------------------------------------------------------------------------------

AptMatrix AptMatrix::concat(const AptMatrix &inner) const
{
	AptMatrix r;
	r.a = a * inner.a + c * inner.b;
	r.b = b * inner.a + d * inner.b;
	r.c = a * inner.c + c * inner.d;
	r.d = b * inner.c + d * inner.d;
	r.tx = a * inner.tx + c * inner.ty + tx;
	r.ty = b * inner.tx + d * inner.ty + ty;
	return r;
}

AptMatrix AptMatrix::inverse(bool *ok) const
{
	float det = a * d - b * c;
	AptMatrix r;
	if (det == 0.0f)
	{
		if (ok)
		{
			*ok = false;
		}
		return r;
	}
	float id = 1.0f / det;
	r.a = d * id;
	r.b = -b * id;
	r.c = -c * id;
	r.d = a * id;
	r.tx = -(r.a * tx + r.c * ty);
	r.ty = -(r.b * tx + r.d * ty);
	if (ok)
	{
		*ok = true;
	}
	return r;
}

AptColorTransform AptColorTransform::concat(const AptColorTransform &inner) const
{
	AptColorTransform r;
	for (int i = 0; i < 4; ++i)
	{
		// RotWK's render context (AptRenderingContext, RW 0xB0B530 -> 0xB225B0): the multipliers multiply and the additive terms add; the
		// parent's multiplier does not scale the child's additive term (lane UI-2, owner feedback F1; the SWF rule would)
		r.mul[i] = mul[i] * inner.mul[i];
		r.add[i] = add[i] + inner.add[i];
	}
	return r;
}

// ---------------------------------------------------------------------------------------------------------------
// AptCharacterInst
// ---------------------------------------------------------------------------------------------------------------

AptCharacterInst::AptCharacterInst(Apt &apt, Type type) : AptObject(AptObjectKind::Clip), m_apt(apt), m_type(type)
{
}

AptSpriteInst *AptCharacterInst::asSprite()
{
	return isSpriteBase() ? static_cast<AptSpriteInst *>(this) : nullptr;
}

const AptSpriteInst *AptCharacterInst::asSprite() const
{
	return isSpriteBase() ? static_cast<const AptSpriteInst *>(this) : nullptr;
}

AptButtonInst *AptCharacterInst::asButton()
{
	return m_type == Type::Button ? static_cast<AptButtonInst *>(this) : nullptr;
}

AptSpriteInst *AptCharacterInst::movieRoot()
{
	// `_root`: the root of the level the instance is on.  A movie loaded into a clip keeps the level's root (the pages of
	// Create-a-Hero call functions defined by the main movie through `_root`).
	AptCharacterInst *c = this;
	while (c->m_parent)
	{
		c = c->m_parent;
	}
	return c->asSprite();
}

AptMatrix AptCharacterInst::globalMatrix() const
{
	if (!m_parent)
	{
		return matrix;
	}
	return m_parent->globalMatrix().concat(matrix);
}

AptColorTransform AptCharacterInst::globalColor() const
{
	if (!m_parent)
	{
		return color;
	}
	return m_parent->globalColor().concat(color);
}

bool AptCharacterInst::globallyVisible() const
{
	for (const AptCharacterInst *c = this; c; c = c->m_parent)
	{
		if (!c->visible || !c->m_defined)
		{
			return false;
		}
	}
	return true;
}

bool AptCharacterInst::contentBounds(float &, float &, float &, float &) const
{
	return false;
}

std::string AptCharacterInst::targetPath() const
{
	// "_level1.SoloPlayNav": the level segment for a movie root, dotted instance names below it
	std::vector<const AptCharacterInst *> chain;
	for (const AptCharacterInst *c = this; c; c = c->m_parent)
	{
		chain.push_back(c);
	}
	std::string out;
	for (std::size_t i = chain.size(); i-- > 0;)
	{
		const AptCharacterInst *c = chain[i];
		if (i == chain.size() - 1)
		{
			out = c->m_instName.empty() ? "_root" : c->m_instName;
		}
		else
		{
			out += "." + (c->m_instName.empty() ? "instance" + std::to_string(c->m_depth) : c->m_instName);
		}
	}
	return out;
}

void AptCharacterInst::trace(AptGC &gc)
{
	AptObject::trace(gc);
	if (m_parent)
	{
		gc.mark(m_parent);
	}
}

std::string AptCharacterInst::displayString() const
{
	// lane HUD-1 (stop S-289): the movies build the names of their FSCommands and extern keys from the clip itself (`"FSCommand:" + this + "_OnPress"`,
	// extern[this + "_ContentName"]), and the engine registers them as "_level%u.%s_OnPress" (RW strings 0xC8CB50 ..): a clip converts to its target path. BFME1's
	// value-string function prints "[MovieClip]" for the generic value type 30 (Rva008985C0ValueString.cpp), which cannot route a command to its clip.
	return targetPath();
}

// lane UI-1 (S-1262): what the clip transform properties do not reproduce of RotWK's scale / rotation record (RW 0xAF4890): the x87 24-bit precision of its
// arithmetic, the record cached per clip until the matrix is replaced (here recomputed from the matrix), and the _xscale / _yscale / _rotation getters, which
// still read unsigned row lengths and the X axis' angle (a mirrored clip reads _xscale +100, retail -100)
void AptCharacterInst::noteTransformUnverified(const std::string &property) const
{
	// once per clip instance and property: Apt::m_notes is never cleared, and a HUD movie reads these every frame
	static const char *const names[5] = { "_xscale", "_yscale", "_rotation", "_width", "_height" };
	for (unsigned i = 0; i < 5; ++i)
	{
		if (property == names[i])
		{
			if (m_transformNoted & (1u << i))
			{
				return;
			}
			m_transformNoted |= (1u << i);
			break;
		}
	}
	m_apt.note("apt-transform-unverified", property + " [S-1262]");
}

bool AptCharacterInst::getProperty(const std::string &n, AptValue &out) const
{
	if (n.empty() || n[0] != '_')
	{
		if (n == "enabled")
		{
			out = AptValue::boolean(enabled);
			return true;
		}
		if (n == "usehandcursor")
		{
			out = AptValue::boolean(useHandCursor);
			return true;
		}
		return false;
	}
	if (n == "_x")
	{
		out = AptValue::number(matrix.tx);
	}
	else if (n == "_y")
	{
		out = AptValue::number(matrix.ty);
	}
	else if (n == "_xscale")
	{
		noteTransformUnverified(n);
		out = AptValue::number(std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b) * 100.0f);
	}
	else if (n == "_yscale")
	{
		noteTransformUnverified(n);
		out = AptValue::number(std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d) * 100.0f);
	}
	else if (n == "_rotation")
	{
		noteTransformUnverified(n);
		out = AptValue::number(std::atan2(matrix.b, matrix.a) * 180.0f / kPi);
	}
	else if (n == "_alpha")
	{
		out = AptValue::number(color.mul[3] * 100.0f);
	}
	else if (n == "_visible")
	{
		out = AptValue::boolean(visible);
	}
	else if (n == "_name")
	{
		out = AptValue::string(m_instName);
	}
	else if (n == "_parent")
	{
		out = m_parent ? AptValue::object(m_parent) : AptValue();
	}
	else if (n == "_root")
	{
		AptSpriteInst *r = const_cast<AptCharacterInst *>(this)->movieRoot();
		out = r ? AptValue::object(r) : AptValue();
	}
	else if (n == "_currentframe")
	{
		const AptSpriteInst *s = asSprite();
		out = s ? AptValue::integer(s->frame + 1) : AptValue::integer(1);
	}
	else if (n == "_totalframes" || n == "_framesloaded")
	{
		const AptSpriteInst *s = asSprite();
		out = s ? AptValue::integer(s->totalFrames()) : AptValue::integer(1);
	}
	else if (n == "_target")
	{
		out = AptValue::string(targetPath());
	}
	else if (n == "_url")
	{
		out = AptValue::string(std::string());
	}
	else if (n == "_width" || n == "_height")
	{
		// lane UI-1 (S-1123): the clip's content bounds through its own matrix, i.e. in the parent's space (Flash's _width / _height, which EA Apt's movies
		// rely on: DisconnectScreen.apt's SetPercent sets `bar._width` and reads `barFullSize._width`). INFERENCE: the retail property code was not read
		float x0, y0, x1, y1;
		if (!contentBounds(x0, y0, x1, y1))
		{
			out = AptValue::number(0.0f);
		}
		else
		{
			const float xs[4] = { x0, x1, x0, x1 };
			const float ys[4] = { y0, y0, y1, y1 };
			float lo = 1e30f, hi = -1e30f;
			for (int i = 0; i < 4; ++i)
			{
				float ox, oy;
				matrix.apply(xs[i], ys[i], ox, oy);
				const float v = n == "_width" ? ox : oy;
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			out = AptValue::number(hi - lo);
		}
	}
	else if (n == "_xmouse" || n == "_ymouse")
	{
		float mx, my;
		m_apt.mousePosition(mx, my);
		bool ok = false;
		AptMatrix inv = globalMatrix().inverse(&ok);
		float lx = mx, ly = my;
		if (ok)
		{
			inv.apply(mx, my, lx, ly);
		}
		out = AptValue::number(n == "_xmouse" ? lx : ly);
	}
	else if (n == "_focusrect")
	{
		out = AptValue::boolean(focusRect);
	}
	else if (n == "_droptarget" || n == "_quality" || n == "_highquality" || n == "_soundbuftime" || n == "_lockroot")
	{
		m_apt.note("unmodelled-clip-property", n); // S-106: reads as an ordinary (undefined) member
		return false;
	}
	else
	{
		return false;
	}
	return true;
}

namespace
{
// RW 0xAF4890's angle normalisation: _CIfmod(angle, pi) (RW 0xA3D7A6 with the double RW 0xD03FC0), then - pi from pi / 2 on (RW 0xBD89D0 / 0xD03FB8)
// and + pi below - pi / 2 when also at or below - pi (RW 0xBDCF1C / 0xBDD390: both compares, as the binary makes them)
float wrapAngle(float angle)
{
	const float pi = 3.14159274f;
	angle = (float)std::fmod((double)angle, (double)pi);
	if (angle >= 1.57079637f)
	{
		angle -= pi;
	}
	if (angle < -1.57079637f && angle <= -pi)
	{
		angle += pi;
	}
	return angle;
}
} // namespace

AptScaleRotation AptDecomposeMatrix(const AptMatrix &m)
{
	// lane UI-1: RW 0xAF4890 (the scale / rotation record a clip builds on first use, + 0x44): the X axis' angle atan2(b, a) and the Y axis' atan2(-c, d),
	// each wrapped; their difference wrapped, halved and added to the Y angle is the rotation (stored * 57.2957763671875, RW 0xBD18FC); with its cosine
	// outside (-1e-4, 1e-4) the scales are a / cos and d / cos, else with the sine outside it b / sin and -c / sin, else both 100 (RW 0xAF4A0E .. 0xAF4AB8)
	AptScaleRotation r;
	const float angX = wrapAngle(std::atan2(m.b, m.a));
	const float angY = wrapAngle(std::atan2(-m.c, m.d));
	const float rotation = wrapAngle(angX - angY) * 0.5f + angY;
	r.rotationDegrees = 57.2957763671875f * rotation;
	const float co = std::cos(rotation), si = std::sin(rotation);
	if (co <= -1e-4f || co >= 1e-4f)
	{
		const float inv = 1.0f / co;
		r.xscale = inv * m.a * 100.0f;
		r.yscale = inv * m.d * 100.0f;
	}
	else if (si <= -1e-4f || si >= 1e-4f)
	{
		const float inv = 1.0f / si;
		r.xscale = inv * m.b * 100.0f;
		r.yscale = inv * m.c * -100.0f;
	}
	else
	{
		r.xscale = 100.0f;
		r.yscale = 100.0f;
	}
	return r;
}

bool AptCharacterInst::setProperty(const std::string &n, const AptValue &value)
{
	if (n.empty())
	{
		return false;
	}
	if (n == "enabled")
	{
		enabled = value.toBoolean(7);
		return true;
	}
	if (n == "usehandcursor")
	{
		useHandCursor = value.toBoolean(7);
		return true;
	}
	if (n[0] != '_')
	{
		return false;
	}
	if (n == "_x")
	{
		matrix.tx = value.toNumber();
	}
	else if (n == "_y")
	{
		matrix.ty = value.toNumber();
	}
	else if (n == "_xscale")
	{
		float s = value.toNumber() / 100.0f;
		float cur = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
		if (cur == 0.0f)
		{
			matrix.a = s;
			matrix.b = 0;
		}
		else
		{
			matrix.a *= s / cur;
			matrix.b *= s / cur;
		}
	}
	else if (n == "_yscale")
	{
		float s = value.toNumber() / 100.0f;
		float cur = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
		if (cur == 0.0f)
		{
			matrix.d = s;
			matrix.c = 0;
		}
		else
		{
			matrix.c *= s / cur;
			matrix.d *= s / cur;
		}
	}
	else if (n == "_rotation")
	{
		float sx = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
		float sy = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
		float th = value.toNumber() * kPi / 180.0f;
		matrix.a = sx * std::cos(th);
		matrix.b = sx * std::sin(th);
		matrix.c = -sy * std::sin(th);
		matrix.d = sy * std::cos(th);
	}
	else if (n == "_alpha")
	{
		color.mul[3] = value.toNumber() / 100.0f;
	}
	else if (n == "_visible")
	{
		visible = value.toBoolean(7);
	}
	else if (n == "_name")
	{
		m_instName = value.toString();
	}
	else if (n == "_focusrect")
	{
		focusRect = value.toBoolean(7);
	}
	else if (n == "_droptarget" || n == "_quality" || n == "_highquality" || n == "_soundbuftime" || n == "_lockroot")
	{
		m_apt.note("unmodelled-clip-property", n); // S-106: stored as an ordinary member
		return false;
	}
	else if (n == "_width" || n == "_height")
	{
		// lane UI-1 (S-1123): RotWK's setters RW 0xB03197 (_width) / 0xB03294 (_height):
		//   * v = the value; a negative one is ignored, 0 becomes 1e-4 (RW 0xB031C6: 0x38D1B717);
		//   * `current` = the clip's extent in the parent's space (RW 0xAF60A0, the getter's); a zero extent keeps the clip as it is;
		//   * the scale / rotation record (AptDecomposeMatrix, RW 0xAF4890: signed scale percents, the rotation in degrees); rotated (a rotation other than 0):
		//     scale% = old + 100 * (v / current - 1)
		//     (RW 0xB0331A); unrotated: scale% = max(v / current, 1e-4) * the matrix's diagonal element (a for _width, d for _height) * 100 (RW 0xB0332D);
		//   * scale% >= 1.132257342338562 (RW 0xD05820, 0x3F90EDCF), stored as _xscale / _yscale and the matrix rebuilt from both scales and the rotation
		//     (RW 0xAF4B20: a = cos * xs, b = sin * xs, c = -sin * ys, d = cos * ys; no rotation: the diagonal only).
		noteTransformUnverified(n);
		AptValue currentValue;
		float v = value.toNumber();
		if (!(v >= 0.0f) || !getProperty(n, currentValue))
		{
			return true; // RW 0xB031AD: below 0 (and NaN here) nothing changes
		}
		if (v == 0.0f)
		{
			v = 1e-4f;
		}
		const float current = currentValue.toNumber();
		if (current == 0.0f)
		{
			return true;
		}
		const bool width = n == "_width";
		const AptScaleRotation record = AptDecomposeMatrix(matrix);
		float xs = record.xscale, ys = record.yscale;
		const float rotationDegrees = record.rotationDegrees;
		const bool rotated = rotationDegrees != 0.0f;
		float scale;
		if (rotated)
		{
			scale = (width ? xs : ys) + (v - current) / current * 100.0f;
		}
		else
		{
			float r = v / current;
			if (r < 1e-4f)
			{
				r = 1e-4f;
			}
			scale = r * (width ? matrix.a : matrix.d) * 100.0f;
		}
		if (scale < 1.132257342338562f)
		{
			scale = 1.132257342338562f;
		}
		(width ? xs : ys) = scale;
		const float sx = xs * 0.01f, sy = ys * 0.01f;
		if (rotated)
		{
			const float radians = rotationDegrees * 0.0174532924f; // RW 0xAF4BA0: the record's degrees * RW 0xBD1900
			const float co = std::cos(radians), si = std::sin(radians);
			matrix.a = co * sx;
			matrix.b = si * sx;
			matrix.c = -si * sy;
			matrix.d = co * sy;
		}
		else
		{
			matrix.a = sx;
			matrix.b = 0.0f;
			matrix.c = 0.0f;
			matrix.d = sy;
		}
	}
	else
	{
		return false;
	}
	return true;
}

bool AptCharacterInst::getOwn(const std::string &name, AptValue &out) const
{
	if (getProperty(lowerName(name), out))
	{
		return true;
	}
	return AptObject::getOwn(name, out);
}

void AptCharacterInst::setOwn(const std::string &name, const AptValue &value)
{
	if (setProperty(lowerName(name), value))
	{
		return;
	}
	AptObject::setOwn(name, value);
}

void AptCharacterInst::destroy(bool)
{
	m_defined = false;
	m_apt.clearTimersOf(this);
	m_apt.noteInstanceDestroyed(this);
}

// ---------------------------------------------------------------------------------------------------------------
// AptSpriteInst
// ---------------------------------------------------------------------------------------------------------------

AptSpriteInst::AptSpriteInst(Apt &apt, Type type) : AptCharacterInst(apt, type)
{
}

AptCharacterInst *AptSpriteInst::childAtDepth(int depth) const
{
	for (AptCharacterInst *c : m_children)
	{
		if (c->m_depth == depth)
		{
			return c;
		}
	}
	return nullptr;
}

AptCharacterInst *AptSpriteInst::childByName(const std::string &name) const
{
	std::string k = lowerName(name);
	for (AptCharacterInst *c : m_children)
	{
		if (lowerName(c->m_instName) == k)
		{
			return c;
		}
	}
	return nullptr;
}

void AptSpriteInst::insertChild(AptCharacterInst *inst)
{
	auto it = std::upper_bound(m_children.begin(), m_children.end(), inst, [](const AptCharacterInst *a, const AptCharacterInst *b) { return a->m_depth < b->m_depth; });
	m_children.insert(it, inst);
	inst->m_parent = this;
}

void AptSpriteInst::reorderChild(AptCharacterInst *child, int newDepth)
{
	unlinkChild(child);
	child->m_depth = newDepth;
	insertChild(child);
}

void AptSpriteInst::unlinkChild(AptCharacterInst *inst)
{
	m_children.erase(std::remove(m_children.begin(), m_children.end(), inst), m_children.end());
}

void AptSpriteInst::registerName(AptCharacterInst &inst, const std::string &name)
{
	inst.m_instName = name;
	if (!name.empty())
	{
		// 0x00AF89A3 (0x00B0B410): the name goes into the parent's own hash
		props.set(name, AptValue::object(&inst));
	}
}

int AptSpriteInst::labelFrame(const std::string &label) const
{
	if (!frames)
	{
		return -1;
	}
	const std::map<std::string, int> &labels = m_apt.labelsOf(frames);
	auto it = labels.find(label); // 0x00B0F010: byte-for-byte compare, case-sensitive
	return it == labels.end() ? -1 : it->second;
}

bool AptSpriteInst::hasHandler(std::uint32_t mask) const
{
	// AptCIH::hasHandler 0x00AE1F90: the sprite's event flags, the character hash flags, the instance hash flags.
	// The hash flags are set by SetEventHandler when a script assigns the member (0x00AE0B20); here the member
	// table is read directly.
	if (eventFlags & mask)
	{
		return true;
	}
	for (const AptEventNameEntry &e : AptEventNames())
	{
		if (e.mask & mask)
		{
			AptValue v;
			if (getMember(e.name, v) && v.isObject() && v.asObject() && v.asObject()->kind() == AptObjectKind::Function)
			{
				return true;
			}
		}
	}
	return false;
}

// AptCIH::fire 0x00AE2010 (the arguments after the mask are the pool tag and "run the member handler too").
bool AptSpriteInst::fire(std::uint32_t mask, std::uint32_t arg, bool runMemberHandler)
{
	if (!hasHandler(mask))
	{
		return false;
	}
	bool result = false;
	// 1. the clip actions (the programs of the place object)
	for (const AptClipEvent &act : clipEvents)
	{
		if (!(act.mask & mask))
		{
			continue;
		}
		std::shared_ptr<const AptFile> file = clipEventFile ? clipEventFile : m_char.file;
		std::string error;
		std::shared_ptr<const AptCodeBlock> code = file ? file->codeAt(act.codeOffset, &error) : nullptr;
		if (!code)
		{
			m_apt.vm().reportError("clip action program at file offset " + std::to_string(act.codeOffset) + " cannot be decoded: " + error);
			continue;
		}
		switch (mask)
		{
			case APT_EVT_ENTERFRAME:
			{
				// 0x00AE2127: push to the FRONT of the action pool (tag, 2)
				AptAction a;
				a.kind = AptAction::Code;
				a.eventMask = mask;
				a.tag = m_apt.currentTag();
				a.frameGuard = guardFrame;
				a.code = code;
				a.codeFile = file;
				a.target = this;
				m_apt.pushActionFront(a);
				break;
			}
			case APT_EVT_KEYPRESS:
			{
				// 0x00AE2282: the clip action's key code must equal arg >> 17; then the front of the pool
				if (act.keyCode != (arg >> 17))
				{
					continue;
				}
				AptAction a;
				a.kind = AptAction::Code;
				a.eventMask = mask;
				a.tag = arg;
				a.frameGuard = guardFrame;
				a.code = code;
				a.codeFile = file;
				a.target = this;
				m_apt.pushActionFront(a);
				break;
			}
			case APT_EVT_UNLOAD:
			case APT_EVT_INITIALIZE:
			case APT_EVT_CONSTRUCT:
				// 0x00AE216A: the program becomes a script function and runs now with this = the instance
				m_apt.callEventProgram(code, this);
				break;
			default:
			{
				AptAction a;
				a.kind = AptAction::Code;
				a.eventMask = mask;
				a.tag = arg;
				a.frameGuard = guardFrame;
				a.code = code;
				a.codeFile = file;
				a.target = this;
				m_apt.pushAction(a); // 0x00AE2149: back of the pool
				break;
			}
		}
		result = true;
	}
	// 2. the member handler (script assigned onXxx), found through the hash and the prototype chain
	if (runMemberHandler)
	{
		bool found = false;
		std::size_t index = AptEventTableIndex(mask, &found);
		const AptEventNameEntry &entry = AptEventNames()[index];
		AptValue fn;
		bool inOwn = props.get(entry.name, fn);
		bool via = inOwn || getMember(entry.name, fn);
		if (via && fn.isObject() && fn.asObject() && fn.asObject()->kind() == AptObjectKind::Function)
		{
			// 0x00AE231D..0x00AE2434 (RotWK counterparts 0x00AF65ED onwards): RollOver (0x2000) and RollOut (0x4000) go through
			// 0x00AE3740, which appends to the call queue; every other member handler goes through 0x00AE3810, which prepends.  The
			// `front` argument of pushFunctionCall is the 0x00AE3810 case.
			const bool back = (entry.mask == APT_EVT_ROLLOUT || entry.mask == APT_EVT_ROLLOVER);
			// A mask without a table entry reuses entry 0 (onEnterFrame): see AptEventTableIndex.
			m_apt.pushFunctionCall(this, fn, 0, mask, arg, !back);
			if (!inOwn && entry.mask == APT_EVT_LOAD)
			{
				// 0x00AE2412..0x00AE242F: the prototype-chain path queues onLoad a second time through 0x00AE3740 (back)
				m_apt.pushFunctionCall(this, fn, 0, mask, arg, false);
			}
			result = true;
		}
	}
	return result;
}

// AptCIH::gotoFrame 0x00AE2C10
void AptSpriteInst::gotoFrame(int target)
{
	const int n = totalFrames();
	if (target < 0 || target >= n || target == frame)
	{
		return; // 0x00AE2C60..0x00AE2C79: out of range or already there: nothing happens (not even the frame actions)
	}
	if (target == frame + 1)
	{
		frame = target;
		doFrameControls(target); // 0x00AE2C8D
	}
	else
	{
		// 0x00AE2C97: build the placements in effect at `target` by replaying the frames' place / remove items
		// (0x00B0F040, forward from the current frame, backward from frame 0), then reconcile the display list
		// with them (0x00AF9410).  Init actions, background colours and frame actions of the replayed frames are
		// not run.
		const bool forward = frame < target;
		const int start = forward ? frame : 0;
		struct Command
		{
			bool remove = false;
			bool hasBase = false;
			AptPlaceObject base;   // the place object that created the placement (character, name, clip actions)
			AptPlaceObject overlay; // moves applied on top (flags of the fields they carry)
			bool anyOverlay = false;
			std::shared_ptr<const AptFile> file;
		};
		std::map<int, Command> commands;
		const std::shared_ptr<const AptFile> &file = timelineFile;
		for (int f = std::max(start, 0); f <= target && f < n; ++f)
		{
			for (const AptFrameItem &item : (*frames)[(std::size_t)f].items)
			{
				if (item.type == APT_ITEM_PLACEOBJECT && item.place)
				{
					const AptPlaceObject &p = *item.place;
					auto it = commands.find(p.depth);
					bool hasChar = (p.flags & APT_PLACE_HASCHARACTER) != 0 && p.characterId >= 0;
					if (it != commands.end() && !it->second.remove && !hasChar)
					{
						// 0x00B0F0AB: a move merges into the pending command
						Command &c = it->second;
						AptPlaceObject &dst = c.hasBase ? c.base : c.overlay;
						if (p.flags & APT_PLACE_HASMATRIX)
						{
							std::copy(p.matrix, p.matrix + 4, dst.matrix);
							std::copy(p.translation, p.translation + 2, dst.translation);
						}
						if (p.flags & APT_PLACE_HASCOLORTRANSFORM)
						{
							std::copy(p.tint, p.tint + 4, dst.tint);
							std::copy(p.additive, p.additive + 4, dst.additive);
						}
						if (p.flags & APT_PLACE_HASRATIO)
						{
							dst.ratio = p.ratio;
						}
						if (p.flags & APT_PLACE_HASCLIPDEPTH)
						{
							dst.clipDepth = p.clipDepth;
						}
						dst.flags |= (p.flags & (APT_PLACE_HASMATRIX | APT_PLACE_HASCOLORTRANSFORM | APT_PLACE_HASRATIO | APT_PLACE_HASCLIPDEPTH));
						if (!c.hasBase)
						{
							c.anyOverlay = true;
						}
					}
					else
					{
						Command c;
						c.file = file;
						if (hasChar)
						{
							c.hasBase = true;
							c.base = p;
						}
						else
						{
							c.overlay = p;
							c.anyOverlay = true;
						}
						commands[p.depth] = c;
					}
				}
				else if (item.type == APT_ITEM_REMOVEOBJECT)
				{
					Command c;
					c.remove = true;
					commands[item.removeDepth] = c;
				}
			}
		}
		// reconcile: current children against the commands, in depth order
		std::vector<AptCharacterInst *> current = m_children;
		std::set<int> depths;
		for (AptCharacterInst *c : current)
		{
			depths.insert(c->m_depth);
		}
		for (const auto &kv : commands)
		{
			depths.insert(kv.first);
		}
		for (int depth : depths)
		{
			AptCharacterInst *existing = nullptr;
			for (AptCharacterInst *c : current)
			{
				if (c->m_depth == depth)
				{
					existing = c;
				}
			}
			auto cit = commands.find(depth);
			if (existing && depth >= kScriptDepthBase)
			{
				continue; // script-created clips are not owned by the timeline
			}
			if (cit == commands.end())
			{
				if (existing && !forward)
				{
					removeObject(depth); // 0x00AF96FC: not part of the target frame
				}
				continue;
			}
			Command &cmd = cit->second;
			if (cmd.remove)
			{
				if (existing)
				{
					removeObject(depth);
				}
				continue;
			}
			if (cmd.hasBase)
			{
				bool sameCharacter = false;
				if (existing)
				{
					AptCharRef ref;
					std::string error;
					if (m_apt.resolveCharacter(timelineFile, (std::uint32_t)cmd.base.characterId, ref, &error))
					{
						sameCharacter = existing->m_char.character == ref.character && existing->m_char.file == ref.file;
						// lane MP-2: a clip a movie was loaded into keeps that movie while the timeline still places the clip there (Flash keeps an
						// instance whose depth and character match on a backward seek; LanLobby.apt's CancelGame goes back to "_lobby" with the
						// LanOpenPlay movie loaded into the clip that frame 1 places. S-105: EA's handling not traced)
						sameCharacter = sameCharacter || (existing->m_type == Type::Movie && existing->m_placedChar.character == ref.character &&
															 existing->m_placedChar.file == ref.file);
					}
				}
				if (existing && sameCharacter)
				{
					applyPlaceFields(*existing, cmd.base, false); // 0x00AF95A5: same character: update in place
				}
				else
				{
					if (existing)
					{
						removeObject(depth); // 0x00AF9350: replace
					}
					placeCharacter(cmd.base, *timelineFile, timelineFile);
				}
			}
			else if (cmd.anyOverlay && existing)
			{
				applyPlaceFields(*existing, cmd.overlay, false);
			}
			else if (cmd.anyOverlay)
			{
				m_apt.note("move-without-instance", "a move at depth " + std::to_string(depth) + " found no instance while seeking to frame " + std::to_string(target));
			}
		}
		frame = target;
	}
	guardFrame = frame; // 0x00AE2D38: positive guard
	queueFrameActions(frame);
	guardFrame = frame;
}

// AptCIH advance 0x00AE2D60 (the sprite-base half)
void AptSpriteInst::advance()
{
	if (!m_defined)
	{
		return;
	}
	const bool wasPlaying = playing;
	guardFrame = 0; // 0x00AE2DAC
	bool skipControls = false;
	bool skipAll = false;
	if (wasPlaying)
	{
		++frame; // restart flag +0x2C (always 0 in this port: see the header)
		const int n = totalFrames();
		if (frame == 1 && n == 1)
		{
			frame = 0; // 0x00AE2DD5: a one-frame clip does not run its frame again
			skipAll = true;
		}
		else if (frame == n)
		{
			gotoFrame(0); // 0x00AE2DEA: loop
			skipAll = true;
		}
	}
	if (!skipAll)
	{
		if (wasPlaying)
		{
			doFrameControls(frame);
		}
		if (playing)
		{
			// 0x00AE2E09: the frame actions with a NEGATIVE guard: they are dropped when the clip is no longer on
			// this frame when the pool runs
			guardFrame = -frame;
			queueFrameActions(frame);
			guardFrame = frame;
		}
	}
	(void)skipControls;
	// 0x00AE2E2F: events.  EnterFrame fires when the load events are already done, or always for a movie root.
	if (!needsLoad || m_type == Type::Movie)
	{
		if (hasHandler(APT_EVT_ENTERFRAME))
		{
			fire(APT_EVT_ENTERFRAME, m_apt.currentTag(), true);
		}
	}
	if (needsLoad)
	{
		fire(APT_EVT_LOAD, m_apt.currentTag(), true); // 0x00AE2E79
		needsLoad = false;                              // and ~0x1000000
	}
	advanceChildren(); // 0x00AF7A30
}

void AptSpriteInst::advanceChildren()
{
	// 0x00AF7A30: walk the list from the head; children inserted during the walk (the display list is linked) are
	// reached too, removed ones are not
	std::vector<AptCharacterInst *> snapshot = m_children;
	for (AptCharacterInst *c : snapshot)
	{
		if (!c->m_defined || c->m_parent != this)
		{
			continue;
		}
		if (AptSpriteInst *s = c->asSprite())
		{
			s->advance();
		}
		else if (AptButtonInst *b = c->asButton())
		{
			b->advance();
		}
	}
}

// AptMovie 0x00B0F680: queue the frame's actions (only after the frame's place / remove items ran)
void AptSpriteInst::queueFrameActions(int frameNo)
{
	if (!frames || frameNo < 0 || frameNo >= (int)frames->size())
	{
		return;
	}
	for (const AptFrameItem &item : (*frames)[(std::size_t)frameNo].items)
	{
		if (item.type != APT_ITEM_ACTION)
		{
			continue;
		}
		std::string error;
		std::shared_ptr<const AptCodeBlock> code = timelineFile->codeAt(item.codeOffset, &error);
		if (!code)
		{
			m_apt.vm().reportError("frame action program at file offset " + std::to_string(item.codeOffset) + " cannot be decoded: " + error);
			continue;
		}
		AptAction a;
		a.kind = AptAction::Code;
		a.eventMask = 0x200000; // 0x00B0F6B2
		a.tag = m_apt.currentTag();
		a.frameGuard = guardFrame;
		a.code = code;
		a.codeFile = timelineFile;
		a.target = this;
		m_apt.pushAction(a);
	}
}

// AptMovie 0x00B0F370: init actions of the frame, then place / remove / background colour items in order
void AptSpriteInst::doFrameControls(int frameNo)
{
	if (!frames || frameNo < 0 || frameNo >= (int)frames->size())
	{
		return;
	}
	const AptFrame &fr = (*frames)[(std::size_t)frameNo];
	m_apt.runFrameInitActions(timelineFile, fr, this);
	for (const AptFrameItem &item : fr.items)
	{
		if (!m_defined)
		{
			break;
		}
		switch (item.type)
		{
			case APT_ITEM_PLACEOBJECT:
				if (item.place)
				{
					placeObject(*item.place, *timelineFile, timelineFile);
				}
				break;
			case APT_ITEM_REMOVEOBJECT:
				removeObject(item.removeDepth);
				break;
			case APT_ITEM_BACKGROUNDCOLOR:
				m_apt.noteBackgroundColor(item.color);
				break;
			default:
				break; // actions, labels and init actions are handled elsewhere (0x00B0F5A0 table)
		}
	}
}

void AptSpriteInst::applyPlaceFields(AptCharacterInst &inst, const AptPlaceObject &place, bool isNew)
{
	if (place.flags & APT_PLACE_HASMATRIX)
	{
		inst.matrix.a = place.matrix[0];
		inst.matrix.b = place.matrix[1];
		inst.matrix.c = place.matrix[2];
		inst.matrix.d = place.matrix[3];
		inst.matrix.tx = place.translation[0];
		inst.matrix.ty = place.translation[1];
	}
	if (place.flags & APT_PLACE_HASCOLORTRANSFORM)
	{
		// Both colours of a place object are 0xAARRGGBB dwords: the file bytes are B, G, R, A.  Target facts (clean BFME2 1.06 game.dat,
		// the byte -> float conversion at 0x00AF7160): the four bytes of the tint become floats 0..3 in the order byte 3, 2, 1, 0 (each
		// times 1/255, constant 0x00BBB8F0) and the additive bytes floats 4..7 in the order byte 7, 6, 5, 4; the corpus puts the alpha
		// fades in byte 3 (24,841 of the 26,119 colour transforms differ from 255 there, 1,003 in each of bytes 0-2).  Inference: the
		// floats are (a, r, g, b), the D3DCOLOR order.  This port keeps (r, g, b, a) in `mul` and `add`.  The additive unit stays 0..255
		// (the binary scales it by 1/255 too; RotWK folds it into the vertex colour: AptCanvas.h AptRetailVertexColour).
		const int fileByte[4] = { 2, 1, 0, 3 }; // r, g, b, a
		for (int i = 0; i < 4; ++i)
		{
			inst.color.mul[i] = place.tint[fileByte[i]] / 255.0f;
			inst.color.add[i] = (float)place.additive[fileByte[i]];
		}
	}
	if (place.flags & APT_PLACE_HASRATIO)
	{
		inst.ratio = place.ratio;
	}
	if (place.flags & APT_PLACE_HASCLIPDEPTH)
	{
		inst.clipDepth = place.clipDepth;
	}
	if (!isNew && (place.flags & APT_PLACE_HASNAME) && !place.name.empty())
	{
		registerName(inst, place.name); // 0x00AF8977: a move with a name renames
	}
}

AptCharacterInst *AptSpriteInst::placeObject(const AptPlaceObject &place, const AptFile &placeFile, const std::shared_ptr<const AptFile> &placeFileShared)
{
	if (place.flags & APT_PLACE_HASCHARACTER) // 0x00AF8EE7: `test bl(2), al` first
	{
		return placeCharacter(place, placeFile, placeFileShared);
	}
	if (place.flags & APT_PLACE_MOVE)
	{
		AptCharacterInst *existing = childAtDepth(place.depth);
		if (!existing)
		{
			m_apt.note("move-without-instance", "a move at depth " + std::to_string(place.depth) + " found no instance on frame " + std::to_string(frame));
			return nullptr;
		}
		applyPlaceFields(*existing, place, false);
		if ((place.flags & APT_PLACE_HASCLIPACTION) && !place.clipEvents.empty())
		{
			if (AptSpriteInst *s = existing->asSprite())
			{
				s->clipEvents = place.clipEvents;
				s->clipEventFile = placeFileShared;
				s->hasClipEventList = true;
				for (const AptClipEvent &e : place.clipEvents)
				{
					s->eventFlags |= (e.mask & 0xBFDFF);
				}
			}
		}
		return existing;
	}
	return nullptr;
}

void AptSpriteInst::firePlacementEvents(AptSpriteInst &inst)
{
	// AptDisplayList setup 0x00AF7E00 (called with `1` at 0x00AF8CB3 for a sprite carrying clip actions): the event
	// flags are the union of the actions' masks (minus Initialize/Construct), then Initialize and Construct run now.
	for (const AptClipEvent &e : inst.clipEvents)
	{
		inst.eventFlags |= (e.mask & 0xBFDFF);
	}
	inst.eventFlags = (inst.eventFlags & ~0x40200u) | 0x40200u;
	inst.fire(APT_EVT_INITIALIZE, m_apt.currentTag(), true);
	inst.fire(APT_EVT_CONSTRUCT, m_apt.currentTag(), true);
}

AptCharacterInst *AptSpriteInst::placeCharacter(const AptPlaceObject &place, const AptFile &, const std::shared_ptr<const AptFile> &placeFileShared)
{
	AptCharRef ref;
	std::string error;
	if (place.characterId < 0 || !m_apt.resolveCharacter(placeFileShared, (std::uint32_t)place.characterId, ref, &error))
	{
		m_apt.vm().reportError("place object at depth " + std::to_string(place.depth) + ": character " + std::to_string(place.characterId) + " cannot be resolved: " + error);
		return nullptr;
	}
	// 0x00AE6B40: the init actions of the character (and of the characters its first frame places) run once, before
	// the first placement
	m_apt.runInitActionsFor(placeFileShared, (std::uint32_t)place.characterId, this);
	if (AptCharacterInst *old = childAtDepth(place.depth))
	{
		// A character placed over an occupied depth.  AptDisplayList::place (0x00AF85E0) is called by PlaceObject2 (0x00AF8EC0 ->
		// 0x00AF8E10 -> 0x00AF8B70) with its replace argument 0 (the `push 0` at 0x00AF8FFC): a DEFINED occupant is reused, not
		// recreated (0x00AF86DA..0x00AF8707), its matrix and colour transform are overwritten from the place object
		// (0x00AF8BF6..0x00AF8C27) and the character pointer of its data is set to the new character (0x00AF8B2C `mov [ecx+0xC],
		// ebx`).  For a shape that is the shape tween: the instance keeps its identity and shows the next shape (the 13 swaps the
		// corpus runs execute, and all 62 occupied-depth records of a static scan of every retail timeline, are shape -> shape).  For other characters the data swap would leave the old timeline state under the new
		// character: not traced, the port replaces the instance as Flash does (S-102).
		if (old->type() == Type::Shape && ref.character->type == APT_CHAR_SHAPE)
		{
			old->m_char = ref;
			applyPlaceFields(*old, place, true);
			return old;
		}
		m_apt.note("place-over-occupied-depth", "character type " + std::to_string(ref.character->type) + " over type " + std::to_string((unsigned)old->type()));
		removeObject(place.depth);
		if (!m_defined || m_destroying || childAtDepth(place.depth))
		{
			// an Unload callback of the replaced instance refilled the depth or destroyed this clip (S-104, as vacateDepth)
			m_apt.note("script-depth-reentrant", "a place object over depth " + std::to_string(place.depth));
			m_apt.vm().reportError("place object at depth " + std::to_string(place.depth) + ": an Unload callback refilled the depth or destroyed the clip; nothing was inserted");
			return nullptr;
		}
	}
	AptCharacterInst *inst = attachCharacter(ref, (place.flags & APT_PLACE_HASNAME) ? place.name : std::string(), place.depth, false);
	if (!inst)
	{
		return nullptr;
	}
	applyPlaceFields(*inst, place, true);
	if (AptSpriteInst *s = inst->asSprite())
	{
		if ((place.flags & APT_PLACE_HASCLIPACTION) && !place.clipEvents.empty())
		{
			s->clipEvents = place.clipEvents;
			s->clipEventFile = placeFileShared;
			s->hasClipEventList = true;
			firePlacementEvents(*s);
		}
	}
	return inst;
}

AptCharacterInst *AptSpriteInst::attachCharacter(const AptCharRef &refIn, const std::string &name, int depth, bool fromScript)
{
	const AptCharRef ref = refIn; // by value: `refIn` may live in the occupant that is removed below
	if (fromScript && !vacateDepth(depth, "attachMovie / duplicateMovieClip"))
	{
		return nullptr;
	}
	AptCharacterInst *inst = m_apt.createInstance(ref);
	inst->m_depth = depth;
	insertChild(inst);
	registerName(*inst, name);
	if (inst->type() == Type::Sprite || inst->type() == Type::Button)
	{
		m_apt.noteNewInstance(inst);
	}
	(void)fromScript;
	return inst;
}

// Empties `depth` before a script-created instance is inserted there.  What EA's attachMovie / duplicateMovieClip /
// createEmptyMovieClip do with an occupied depth: they pass replacement flag 1 to AptDisplayList::place (BFME2 0x00AED69E,
// 0x00AEE142 and, through 0x00B092F0, 0x00B0941E), which finalizes and removes the occupant (0x00AF7090 / 0x00AF7230) before it
// allocates the new instance (0x00AF867C..0x00AF8682).  The removal runs the occupant's Unload callbacks, and what a callback may
// do to this display list was not traced (S-104): when one refilled the depth or destroyed this clip, the pending insertion is
// declined, reported, and the display list keeps its one instance per depth.  No remove-until-empty loop (an Unload can
// recreate indefinitely).
bool AptSpriteInst::vacateDepth(int depth, const char *native)
{
	if (!childAtDepth(depth))
	{
		return true;
	}
	m_apt.note("script-depth-occupied", "depth " + std::to_string((long long)depth - kScriptDepthBase));
	removeObject(depth);
	if (!m_defined || m_destroying)
	{
		m_apt.note("script-depth-reentrant", "the parent was destroyed by an Unload callback");
		m_apt.vm().reportError(std::string(native) + ": the clip was destroyed while the occupant of the depth was removed; nothing was inserted");
		return false;
	}
	if (childAtDepth(depth))
	{
		m_apt.note("script-depth-reentrant", "an Unload callback refilled depth " + std::to_string((long long)depth - kScriptDepthBase));
		m_apt.vm().reportError(std::string(native) + ": an Unload callback refilled depth " + std::to_string((long long)depth - kScriptDepthBase) + "; nothing was inserted");
		return false;
	}
	return true;
}

AptSpriteInst *AptSpriteInst::createEmptyClip(const std::string &name, int depth)
{
	// createEmptyMovieClip: a sprite with no frames (EA allocates a sprite instance with an empty animation)
	if (!vacateDepth(depth, "createEmptyMovieClip"))
	{
		return nullptr;
	}
	AptSpriteInst *s = m_apt.gc().create<AptSpriteInst>(m_apt, Type::Sprite);
	s->frames = nullptr;
	s->timelineFile = timelineFile;
	s->frame = 0;
	s->needsLoad = false;
	s->playing = false;
	s->m_depth = depth;
	s->setProto(m_apt.movieClipPrototype());
	insertChild(s);
	registerName(*s, name);
	return s;
}

void AptSpriteInst::removeObject(int depth)
{
	AptCharacterInst *c = childAtDepth(depth);
	if (!c)
	{
		return;
	}
	// 0x00AF7230 / 0x00AE2B40: unlink, erase the name from the parent's hash when it still points at the instance,
	// then the finalizer (Unload event, children, timers)
	unlinkChild(c);
	if (!c->m_instName.empty())
	{
		AptValue v;
		if (props.get(c->m_instName, v) && v.isObject() && v.asObject() == c)
		{
			props.erase(c->m_instName); // 0x00AF96EB: BfmeLookup erase
		}
	}
	c->destroy(true);
}

void AptSpriteInst::removeAllChildren(bool fireUnload)
{
	std::vector<AptCharacterInst *> copy = m_children;
	if (fireUnload)
	{
		for (AptCharacterInst *c : copy)
		{
			if (AptSpriteInst *s = c->asSprite())
			{
				if (s->hasHandler(APT_EVT_UNLOAD))
				{
					// S-101: the order in which the Unload events of a removed clip's children run is not traced
					m_apt.note("unload-descendant-order", c->targetPath());
					break;
				}
			}
		}
	}
	for (AptCharacterInst *c : copy)
	{
		if (c->m_destroying || !c->m_defined)
		{
			continue; // torn down by a callback of an earlier child
		}
		unlinkChild(c);
		if (!c->m_instName.empty())
		{
			AptValue v;
			if (props.get(c->m_instName, v) && v.isObject() && v.asObject() == c)
			{
				props.erase(c->m_instName);
			}
		}
		c->destroy(fireUnload);
	}
}

void AptSpriteInst::destroy(bool fireUnload)
{
	// A script callback of an earlier sibling's teardown may already have removed this instance (its Unload ran then): never
	// a second Unload, never a second teardown.
	if (!beginDestroy())
	{
		return;
	}
	// AptCIH finalizer 0x00AE2690: pending actions of the instance are dropped, the Unload event runs now (only for a
	// sprite instance, type 0x0D), then the display list is torn down.
	m_apt.dropActionsOf(this);
	if (m_type == Type::Movie)
	{
		m_apt.clearTimersOf(this); // 0x00AE2789 -> 0x00AE44F0: timers of the movie die with it
	}
	if (fireUnload && m_type == Type::Sprite)
	{
		fire(APT_EVT_UNLOAD, 0, false);
		AptValue fn;
		if (getMember("onUnload", fn) && fn.isObject() && fn.asObject() && fn.asObject()->kind() == AptObjectKind::Function)
		{
			m_apt.vm().callFunction(fn, AptValue::object(this), std::vector<AptValue>()); // 0x00AE2804: now, this = the instance
		}
	}
	removeAllChildren(fireUnload);
	AptCharacterInst::destroy(fireUnload);
}

bool AptSpriteInst::timelineOp(const AptTimelineRequest &r)
{
	switch (r.op)
	{
		case AptTimelineOp::NextFrame: // 0x00B00560: gotoFrame(frame + 1) and stop
			gotoFrame(frame + 1);
			playing = false;
			return true;
		case AptTimelineOp::Play: // 0x00B00660
			playing = true;
			return true;
		case AptTimelineOp::Stop: // 0x00B006D0
			playing = false;
			return true;
		case AptTimelineOp::GotoFrame: // 0x00B04900: the operand is a zero-based frame; the clip stops
			gotoFrame(r.a.toInteger());
			playing = false;
			return true;
		case AptTimelineOp::GotoLabel: // 0x00B04A10
		{
			int f = labelFrame(r.a.toString());
			if (f >= 0)
			{
				gotoFrame(f);
				playing = false;
			}
			else
			{
				m_apt.note("label-not-found", r.a.toString());
			}
			return true;
		}
		case AptTimelineOp::GotoFrame2: // 0x00B04FE0
		{
			int f = -1;
			AptSpriteInst *target = this;
			if (r.a.isString())
			{
				std::string s = r.a.toString();
				std::string label = s;
				std::size_t colon = s.find(':');
				if (colon != std::string::npos)
				{
					// "path:label" (0x00AFEC00 splits the target path from the label)
					AptCharacterInst *t = m_apt.resolvePath(this, s.substr(0, colon));
					label = s.substr(colon + 1);
					if (t && t->asSprite())
					{
						target = t->asSprite();
					}
					else
					{
						m_apt.note("goto-path-unresolved", s);
						return true;
					}
				}
				f = target->labelFrame(label);
				if (f < 0)
				{
					m_apt.note("label-not-found", label);
				}
			}
			else if (r.a.isInteger())
			{
				f = r.a.toInteger(); // the stack value is used as the frame index directly
			}
			else
			{
				m_apt.note("goto-operand-not-string-or-integer", r.a.typeOf());
				return true;
			}
			if (f != -1)
			{
				target->gotoFrame(f);
				target->playing = r.flag;
			}
			return true;
		}
		case AptTimelineOp::CloneSprite:
			return m_apt.duplicateClip(this, r.a, r.b, r.c);
		case AptTimelineOp::RemoveSprite:
			return m_apt.removeClipByTarget(this, r.a);
	}
	return false;
}

void AptSpriteInst::becomeMovie(const std::shared_ptr<const AptFile> &movie)
{
	// 0x00AD17F0: the target clip becomes a movie instance (type 0x12) with the loaded movie's data; the previous
	// display list is released; the instance keeps its name, parent and placement; the first frame runs at once
	removeAllChildren(true);
	if (m_type != Type::Movie)
	{
		m_placedChar = m_char;
	}
	m_type = Type::Movie;
	frames = &movie->frames;
	timelineFile = movie;
	m_char = AptCharRef();
	m_char.file = movie;
	frame = -1;
	playing = true;
	needsLoad = true;
	m_defined = true;
	eventFlags = 0;
	clipEvents.clear();
}

bool AptSpriteInst::contentBounds(float &x0, float &y0, float &x1, float &y1) const
{
	bool any = false;
	x0 = y0 = 1e30f;
	x1 = y1 = -1e30f;
	for (const AptCharacterInst *c : m_children)
	{
		float cx0, cy0, cx1, cy1;
		if (!c->contentBounds(cx0, cy0, cx1, cy1))
		{
			continue;
		}
		const float xs[4] = { cx0, cx1, cx0, cx1 };
		const float ys[4] = { cy0, cy0, cy1, cy1 };
		for (int i = 0; i < 4; ++i)
		{
			// contentBounds of a child is in the child's own space: transform by the child's matrix into ours
			float ox, oy;
			c->matrix.apply(xs[i], ys[i], ox, oy);
			x0 = std::min(x0, ox);
			y0 = std::min(y0, oy);
			x1 = std::max(x1, ox);
			y1 = std::max(y1, oy);
		}
		any = true;
	}
	return any;
}

void AptSpriteInst::trace(AptGC &gc)
{
	AptCharacterInst::trace(gc);
	for (AptCharacterInst *c : m_children)
	{
		gc.mark(c);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// leaves
// ---------------------------------------------------------------------------------------------------------------

bool AptShapeInst::contentBounds(float &x0, float &y0, float &x1, float &y1) const
{
	if (!m_char.character)
	{
		return false;
	}
	x0 = m_char.character->bounds[0];
	y0 = m_char.character->bounds[1];
	x1 = m_char.character->bounds[2];
	y1 = m_char.character->bounds[3];
	return true;
}

bool AptTextInst::contentBounds(float &x0, float &y0, float &x1, float &y1) const
{
	if (!m_char.character || !m_char.character->text)
	{
		return false;
	}
	x0 = m_char.character->text->bounds[0];
	y0 = m_char.character->text->bounds[1];
	x1 = m_char.character->text->bounds[2];
	y1 = m_char.character->text->bounds[3];
	return true;
}

bool AptTextInst::getOwn(const std::string &name, AptValue &out) const
{
	std::string n = lowerName(name);
	if (n == "text" || n == "htmltext")
	{
		out = AptValue::string(text);
		return true;
	}
	if (n == "variable")
	{
		out = AptValue::string(variable);
		return true;
	}
	if (n == "textcolor")
	{
		out = AptValue::number((float)(colorArgb & 0xFFFFFFu)); // 0x00AEFE89: the low 24 bits as a number
		return true;
	}
	return AptCharacterInst::getOwn(name, out);
}

void AptTextInst::setOwn(const std::string &name, const AptValue &value)
{
	std::string n = lowerName(name);
	if (n == "text" || n == "htmltext")
	{
		text = value.toString();
		textAssigned = true;
		return;
	}
	if (n == "variable")
	{
		variable = value.toString();
		return;
	}
	if (n == "textcolor")
	{
		colorArgb = (std::uint32_t)value.toInteger() | 0xFF000000u; // 0x00AEEB20..0x00AEEB3B: alpha is forced to 255
		return;
	}
	AptCharacterInst::setOwn(name, value);
}
