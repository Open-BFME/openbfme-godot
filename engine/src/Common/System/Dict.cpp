// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See Common/Dict.h.

#include "Common/Dict.h"

Dict::Pair &Dict::slot(const std::string &key)
{
	for (Pair &p : m_pairs)
	{
		if (p.key == key)
		{
			return p;
		}
	}
	m_pairs.emplace_back();
	m_pairs.back().key = key;
	return m_pairs.back();
}

void Dict::setBool(const std::string &key, bool v)
{
	Pair &p = slot(key);
	p.type = DICT_BOOL;
	p.b = v;
}

void Dict::setInt(const std::string &key, std::int32_t v)
{
	Pair &p = slot(key);
	p.type = DICT_INT;
	p.i = v;
}

void Dict::setReal(const std::string &key, float v)
{
	Pair &p = slot(key);
	p.type = DICT_REAL;
	p.r = v;
}

void Dict::setAsciiString(const std::string &key, const std::string &v)
{
	Pair &p = slot(key);
	p.type = DICT_ASCIISTRING;
	p.s = v;
}

void Dict::setUnicodeString(const std::string &key, const std::u16string &v)
{
	Pair &p = slot(key);
	p.type = DICT_UNICODESTRING;
	p.u = v;
}

const Dict::Pair *Dict::find(const std::string &key) const
{
	for (const Pair &p : m_pairs)
	{
		if (p.key == key)
		{
			return &p;
		}
	}
	return nullptr;
}

namespace
{
const Dict::Pair *typed(const Dict &d, const std::string &key, Dict::DataType t, bool *exists)
{
	const Dict::Pair *p = d.find(key);
	bool ok = p && p->type == t;
	if (exists)
	{
		*exists = ok;
	}
	return ok ? p : nullptr;
}
} // namespace

bool Dict::getBool(const std::string &key, bool *exists) const
{
	const Pair *p = typed(*this, key, DICT_BOOL, exists);
	return p ? p->b : false;
}

std::int32_t Dict::getInt(const std::string &key, bool *exists) const
{
	const Pair *p = typed(*this, key, DICT_INT, exists);
	return p ? p->i : 0;
}

float Dict::getReal(const std::string &key, bool *exists) const
{
	const Pair *p = typed(*this, key, DICT_REAL, exists);
	return p ? p->r : 0.0f;
}

std::string Dict::getAsciiString(const std::string &key, bool *exists) const
{
	const Pair *p = typed(*this, key, DICT_ASCIISTRING, exists);
	return p ? p->s : std::string();
}

std::u16string Dict::getUnicodeString(const std::string &key, bool *exists) const
{
	const Pair *p = typed(*this, key, DICT_UNICODESTRING, exists);
	return p ? p->u : std::u16string();
}
