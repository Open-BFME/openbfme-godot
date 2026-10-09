// OpenBFME. GPL-3.0.
// See AptObject.h for the citations.

#include "Libraries/Source/Apt/AptObject.h"

#include <algorithm>
#include <cstdio>

// ---- AptPropertyMap -------------------------------------------------------------------------
// Ported from the BFME2 1.06 AptNativeHash (see AptObject.h for the addresses).

namespace
{
char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool equalsNoCase(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (lowerAscii(a[i]) != lowerAscii(b[i]))
		{
			return false;
		}
	}
	return true;
}
} // namespace

std::string AptPropertyMap::foldKey(const std::string &name)
{
	std::string k = name;
	for (char &c : k)
	{
		c = lowerAscii(c);
	}
	return k;
}

std::uint16_t AptPropertyMap::hash16(const std::string &name)
{
	// 0x00AD3800: movsx byte, lowercase A-Z, h = (c ^ h) * 0x01000193 from 0x811C9DC5, return ax, 0 -> 0x4567
	std::uint32_t h = 0x811C9DC5u;
	for (char ch : name)
	{
		std::int32_t c = (std::int32_t)(signed char)ch;
		if (c <= 0x5A && c >= 0x41)
		{
			c += 0x20;
		}
		h = ((std::uint32_t)c ^ h) * 0x01000193u;
	}
	std::uint16_t r = (std::uint16_t)(h & 0xFFFFu);
	return r == 0 ? (std::uint16_t)0x4567 : r;
}

bool AptPropertyMap::isPrototypeName(const std::string &name, std::uint16_t hash)
{
	return hash == 0x0699 && equalsNoCase(name, "prototype");
}

bool AptPropertyMap::keyEquals(const Slot &s, const std::string &name, std::uint16_t hash) const
{
	// 0x00AD36F0: equal 16-bit hashes, then _strcmpi
	return s.hash == hash && equalsNoCase(s.key, name);
}

void AptPropertyMap::window(int slot, int size, int &lo, int &hi)
{
	lo = slot - 8;
	if (lo < 0)
	{
		lo = 0;
		hi = size > 16 ? 16 : size - 1;
	}
	else
	{
		hi = slot + 8;
		if (hi > size - 1)
		{
			hi = size - 1;
			lo = hi - 16;
			if (lo < 0)
			{
				lo = 0;
			}
		}
	}
}

int AptPropertyMap::find(const std::string &name, std::uint16_t hash) const
{
	// BFME2 0x00B0AF90
	if (m_slots.empty())
	{
		return -1;
	}
	int idx = (int)hash & (m_size - 1);
	const Slot &home = m_slots[idx];
	if (home.state == Slot::Empty)
	{
		return -1;
	}
	if (home.state == Slot::Used && keyEquals(home, name, hash))
	{
		return idx;
	}
	int lo, hi;
	window(idx, m_size, lo, hi);
	for (int pos = idx + 1; pos <= hi; ++pos)
	{
		const Slot &s = m_slots[pos];
		if (s.state == Slot::Empty)
		{
			return -1;
		}
		if (s.state == Slot::Used && keyEquals(s, name, hash))
		{
			return pos;
		}
	}
	for (int pos = idx - 1; pos >= lo; --pos)
	{
		const Slot &s = m_slots[pos];
		if (s.state == Slot::Empty)
		{
			return -1;
		}
		if (s.state == Slot::Used && keyEquals(s, name, hash))
		{
			return pos;
		}
	}
	return -1;
}

int AptPropertyMap::slotOf(const std::string &name) const
{
	return find(name, hash16(name));
}

void AptPropertyMap::place(int slot, const std::string &name, std::uint16_t hash, const AptValue &value)
{
	Slot &s = m_slots[slot];
	s.state = Slot::Used;
	s.hash = hash;
	s.key = name;
	s.value = value;
	++m_used;
}

void AptPropertyMap::grow()
{
	// BFME2 0x00B0ABC0: a table of twice the size, live entries re-inserted in slot order (the new
	// table's own Set may grow it again), then the new storage replaces the old
	AptPropertyMap bigger(m_size * 2);
	bigger.m_slots.assign((std::size_t)bigger.m_size, Slot());
	for (const Slot &s : m_slots)
	{
		if (s.state == Slot::Used)
		{
			bigger.rawSet(s.key, s.hash, s.value);
		}
	}
	m_size = bigger.m_size;
	m_slots.swap(bigger.m_slots);
	m_used = bigger.m_used;
}

void AptPropertyMap::rawSet(const std::string &name, std::uint16_t hash, const AptValue &value)
{
	// BFME2 0x00B0AC90
	for (;;)
	{
		int firstTomb = -1;
		int idx = (int)hash & (m_size - 1);
		Slot &home = m_slots[idx];
		if (home.state == Slot::Empty)
		{
			place(idx, name, hash, value);
			return;
		}
		if (home.state == Slot::Tomb)
		{
			firstTomb = idx;
		}
		else if (keyEquals(home, name, hash))
		{
			home.value = value;
			return;
		}
		int lo, hi;
		window(idx, m_size, lo, hi);
		for (int pos = idx + 1; pos <= hi; ++pos)
		{
			Slot &s = m_slots[pos];
			if (s.state == Slot::Empty)
			{
				place(pos, name, hash, value);
				return;
			}
			if (s.state == Slot::Tomb)
			{
				if (firstTomb != -1) // as compiled: 0x00B0ADE4 updates only an already remembered tombstone
				{
					firstTomb = pos;
				}
			}
			else if (keyEquals(s, name, hash))
			{
				s.value = value;
				return;
			}
		}
		for (int pos = idx - 1; pos >= lo; --pos)
		{
			Slot &s = m_slots[pos];
			if (s.state == Slot::Empty)
			{
				place(pos, name, hash, value);
				return;
			}
			if (s.state == Slot::Tomb)
			{
				if (firstTomb != -1)
				{
					firstTomb = pos;
				}
			}
			else if (keyEquals(s, name, hash))
			{
				s.value = value;
				return;
			}
		}
		if (firstTomb == -1)
		{
			grow();
			continue;
		}
		place(firstTomb, name, hash, value);
		return;
	}
}

bool AptPropertyMap::get(const std::string &name, AptValue &out) const
{
	std::uint16_t hash = hash16(name);
	int slot = find(name, hash);
	if (slot >= 0)
	{
		out = m_slots[(std::size_t)slot].value;
		return true;
	}
	if (m_hasPrototype && isPrototypeName(name, hash))
	{
		out = m_prototype;
		return true;
	}
	auto it = m_natives.find(foldKey(name));
	if (it != m_natives.end())
	{
		out = it->second;
		return true;
	}
	return false;
}

bool AptPropertyMap::has(const std::string &name) const
{
	AptValue ignored;
	return get(name, ignored);
}

void AptPropertyMap::set(const std::string &name, const AptValue &value)
{
	// BFME2 0x00B0B410 (SetMember): an empty key is ignored; "prototype" is a field, not a slot
	if (name.empty())
	{
		return;
	}
	std::uint16_t hash = hash16(name);
	if (isPrototypeName(name, hash))
	{
		m_hasPrototype = true;
		m_prototype = value;
		return;
	}
	if (m_slots.empty())
	{
		m_slots.assign((std::size_t)m_size, Slot()); // 0x00B0AB30: allocated and zeroed at the first Set
	}
	rawSet(name, hash, value);
}

void AptPropertyMap::setNative(const std::string &name, const AptValue &value)
{
	m_natives[foldKey(name)] = value;
}

bool AptPropertyMap::erase(const std::string &name)
{
	// BFME2 0x00B0B2C0 (Remove): the slot's key becomes the empty string, the value is dropped
	if (name.empty())
	{
		return false;
	}
	std::uint16_t hash = hash16(name);
	int slot = find(name, hash);
	if (slot >= 0)
	{
		Slot &s = m_slots[(std::size_t)slot];
		s.state = Slot::Tomb;
		s.key.clear();
		s.value = AptValue();
		--m_used;
		return true;
	}
	if (m_hasPrototype && isPrototypeName(name, hash))
	{
		m_hasPrototype = false;
		m_prototype = AptValue();
		return true;
	}
	return false;
}

void AptPropertyMap::enumerate(std::vector<std::string> &out) const
{
	// BFME2 0x00B0AA40 / 0x00B0AAA0: slot order, used slots with a non-empty key
	for (const Slot &s : m_slots)
	{
		if (s.state == Slot::Used && !s.key.empty())
		{
			out.push_back(s.key);
		}
	}
}

// ---- AptObject ------------------------------------------------------------------------------

bool AptObject::getMember(const std::string &name, AptValue &out) const
{
	const AptObject *o = this;
	for (int depth = 0; o; ++depth)
	{
		if (depth >= kMaxProtoLinks)
		{
			if (m_error.empty())
			{
				m_error = "prototype chain lookup of '" + name + "' exceeded " + std::to_string(kMaxProtoLinks) +
					" objects: an implementation limit, not an EA value (acceptance stop S-015); the result is not a miss";
			}
			return false;
		}
		if (o->getOwn(name, out))
		{
			return true;
		}
		o = o->m_proto;
	}
	return false;
}

bool AptObject::hasMember(const std::string &name) const
{
	AptValue v;
	return getMember(name, v);
}

namespace
{
bool isProtoLinkName(const std::string &name)
{
	return AptPropertyMap::hash16(name) == 0x6BBD && AptPropertyMap::foldKey(name) == "__proto__";
}
} // namespace

bool AptObject::getOwn(const std::string &name, AptValue &out) const
{
	if (isProtoLinkName(name))
	{
		if (m_proto)
		{
			out = AptValue::object(m_proto);
			return true;
		}
		if (m_hasProtoValue)
		{
			out = m_protoValue; // 0x00B0B3F4 returns the stored field whatever it holds
			return true;
		}
		return false;
	}
	return props.get(name, out);
}

void AptObject::setOwn(const std::string &name, const AptValue &value)
{
	if (isProtoLinkName(name))
	{
		// BFME2 0x00ADB36E keeps the boxed value; only an object continues the prototype chain
		if (value.isObject() && value.asObject())
		{
			m_proto = value.asObject();
			m_hasProtoValue = false;
			m_protoValue = AptValue();
		}
		else
		{
			m_proto = nullptr;
			m_protoValue = value;
			m_hasProtoValue = true;
		}
		return;
	}
	if (props.hasNative(name) && m_error.empty())
	{
		m_error = "member '" + name + "' is stored over a built-in native member of the same name: the EA lookup order between the two is not decoded (acceptance stop)";
	}
	props.set(name, value);
}

bool AptObject::deleteOwn(const std::string &name)
{
	if (isProtoLinkName(name))
	{
		bool had = m_proto != nullptr || m_hasProtoValue;
		m_proto = nullptr;
		m_protoValue = AptValue();
		m_hasProtoValue = false;
		return had;
	}
	return props.erase(name);
}

bool AptObject::enumerateForIn(std::vector<std::string> &out, std::string &error) const
{
	const AptObject *o = this;
	for (int hops = 0; o; ++hops)
	{
		if (hops > 4096)
		{
			error = "for-in prototype chain exceeds 4096 links (cycle)";
			return false;
		}
		if (!o->forInDecoded())
		{
			error = std::string("for-in reached an ") + (o == this ? "" : "inherited ") + "array, function or movie clip, which is an acceptance stop: the class hash size and "
					"member enumeration of those EA classes are not decoded (BFME2 0x00B00170 walks the hash of the "
					"object's class)";
			return false;
		}
		if (o->m_hasProtoValue)
		{
			error = "for-in reached a non-object __proto__ value, which is an acceptance stop: how the EA walk treats it is not decoded";
			return false;
		}
		o->props.enumerate(out);
		o = o->m_proto;
	}
	return true;
}

void AptObject::trace(AptGC &gc)
{
	gc.mark(m_proto);
	gc.mark(m_protoValue);
	props.forEachValue([&](const AptValue &v) { gc.mark(v); });
}

std::string AptObject::displayString() const
{
	switch (m_kind)
	{
		case AptObjectKind::Clip:
			return "[MovieClip]"; // type 30 in Rva008985C0ValueString.cpp
		case AptObjectKind::Function:
			return "[function]";
		default:
			return "[object Object]";
	}
}

// ---- AptArray -------------------------------------------------------------------------------

bool AptArray::parseIndex(const std::string &name, std::size_t &index)
{
	if (name.empty() || name.size() > 9)
	{
		return false;
	}
	if (name.size() > 1 && name[0] == '0')
	{
		return false;
	}
	std::size_t v = 0;
	for (char c : name)
	{
		if (c < '0' || c > '9')
		{
			return false;
		}
		v = v * 10 + (std::size_t)(c - '0');
	}
	index = v;
	return true;
}

bool AptArray::getOwn(const std::string &name, AptValue &out) const
{
	std::size_t idx;
	if (parseIndex(name, idx))
	{
		if (idx < items.size())
		{
			out = items[idx];
			return true;
		}
		return false;
	}
	if (AptPropertyMap::foldKey(name) == "length")
	{
		out = AptValue::integer((std::int32_t)items.size());
		return true;
	}
	return AptObject::getOwn(name, out);
}

void AptArray::setOwn(const std::string &name, const AptValue &value)
{
	std::size_t idx;
	if (parseIndex(name, idx))
	{
		if (idx >= items.size())
		{
			if (idx >= kMaxLength)
			{
				m_error = "array index " + name + " exceeds the resource bound of " + std::to_string(kMaxLength) + " elements";
				return;
			}
			items.resize(idx + 1);
		}
		items[idx] = value;
		return;
	}
	if (AptPropertyMap::foldKey(name) == "length")
	{
		std::int32_t n = value.toInteger();
		std::size_t want = n < 0 ? 0 : (std::size_t)n;
		if (want > kMaxLength)
		{
			m_error = "array length " + std::to_string(n) + " exceeds the resource bound of " + std::to_string(kMaxLength) + " elements";
			return;
		}
		items.resize(want);
		return;
	}
	AptObject::setOwn(name, value);
}

bool AptArray::deleteOwn(const std::string &name)
{
	std::size_t idx;
	if (parseIndex(name, idx))
	{
		if (idx < items.size())
		{
			items[idx] = AptValue();
			return true;
		}
		return false;
	}
	return AptObject::deleteOwn(name);
}

void AptArray::ownNames(std::vector<std::string> &out) const
{
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		out.push_back(std::to_string(i));
	}
	props.names(out);
}

void AptArray::trace(AptGC &gc)
{
	AptObject::trace(gc);
	for (const AptValue &v : items)
	{
		gc.mark(v);
	}
}

std::string AptArray::displayString() const
{
	// getName case 22: bfmeBuildString1284(output, ",") joins the elements' names with commas.
	std::string out;
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		if (i)
		{
			out += ',';
		}
		out += items[i].toString();
	}
	return out;
}

// ---- AptFunction ----------------------------------------------------------------------------

void AptFunction::trace(AptGC &gc)
{
	AptObject::trace(gc);
	gc.mark(scopeTarget);
	gc.mark(scopeRoot);
	for (const AptScope *scope = outerScope.get(); scope; scope = scope->parent.get())
	{
		scope->locals.forEachValue([&](const AptValue &v) { gc.mark(v); });
	}
}

std::string AptFunction::displayString() const
{
	if (isNative())
	{
		return "[native function 0x00000000]"; // case 9: the retail text embeds the callback address
	}
	return "[function]";
}

// ---- AptGC ----------------------------------------------------------------------------------

void AptGC::mark(AptObject *o)
{
	if (o && !o->m_marked)
	{
		o->m_marked = true;
		m_work.push_back(o);
	}
}

void AptGC::mark(const AptValue &v)
{
	if (v.isObject())
	{
		mark(v.asObject());
	}
}

std::size_t AptGC::collect(const std::function<void(AptGC &)> &markRoots)
{
	for (auto &o : m_objects)
	{
		o->m_marked = false;
	}
	m_work.clear();
	markRoots(*this);
	while (!m_work.empty())
	{
		AptObject *o = m_work.back();
		m_work.pop_back();
		o->trace(*this);
	}
	std::size_t before = m_objects.size();
	m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(), [](const std::unique_ptr<AptObject> &o) { return !o->m_marked; }),
		m_objects.end());
	return before - m_objects.size();
}

// ---- property names -------------------------------------------------------------------------

const char *AptPropertyName(int index)
{
	// SWF ActionGetProperty/SetProperty property table (SWF file format spec v19, ActionGetProperty).
	static const char *const names[] = { "_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha", "_visible", "_width", "_height",
		"_rotation", "_target", "_framesloaded", "_name", "_droptarget", "_url", "_highquality", "_focusrect", "_soundbuftime", "_quality", "_xmouse",
		"_ymouse" };
	if (index < 0 || index >= (int)(sizeof(names) / sizeof(names[0])))
	{
		return nullptr;
	}
	return names[index];
}
