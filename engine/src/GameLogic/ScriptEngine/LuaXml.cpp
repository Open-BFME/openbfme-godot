// OpenBFME. GPL-3.0. See LuaXml.h (RW 0x949147 constructor, 0x9491D1 next, 0x948E05 tag, 0x9490D2 text, 0x94907C comment, 0x94901C entity).

#include "GameLogic/ScriptEngine/LuaXml.h"

#include <cstring>

namespace
{
bool isSpaceC(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool isAlnumC(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// RW 0xDB72C4: five entries {name, replacement}; the standard XML predefined entities (names read in the binary-fact test)
struct Entity
{
	const char *name;
	char value;
};
const Entity kEntities[5] = { { "quot", '"' }, { "apos", '\'' }, { "amp", '&' }, { "lt", '<' }, { "gt", '>' } };
} // namespace

EaXmlLexer::EaXmlLexer(const std::string &text, size_t bufferLimit)
	: m_limit(bufferLimit)
{
	const size_t nul = text.find('\0');
	m_text = nul == std::string::npos ? text : text.substr(0, nul);
	if (at(0) == '<' && at(1) == '?')
	{
		if (m_text.compare(0, 0x15, "<?xml version=\"1.0\"?>") == 0)
		{
			m_pos = 0x15;
		}
		else
		{
			m_crashesRetail = true; // RW 0x9491AF: the cursor becomes NULL + 0x15
			m_posValid = false;
		}
	}
}

int EaXmlLexer::fail()
{
	// RW 0x948D9A: the cursor, the line start, the line number and the depth are zeroed; the result is the argument, -1
	m_posValid = false;
	m_line = 0;
	m_depth = 0;
	return ERROR_TOKEN;
}

size_t EaXmlLexer::skipSpace(size_t p)
{
	// RW 0x948D31
	while (at(p) != '\0' && isSpaceC(at(p)))
	{
		if (at(p) == '\n')
		{
			++m_line;
		}
		++p;
	}
	return p;
}

bool EaXmlLexer::skipComment()
{
	// RW 0x94907C: `<!` is at the cursor
	m_pos += 2;
	if (at(m_pos) != '-')
	{
		return false;
	}
	++m_pos;
	if (at(m_pos) != '-')
	{
		return false;
	}
	++m_pos;
	if (at(m_pos) == '\0')
	{
		return false;
	}
	// 0x949099: scan to the next '-'; then the character after it, then after that; the retry label sits at the cursor the
	// failed '-->' test left, which is why "--->" never closes the comment
	for (;;)
	{
		size_t p = m_pos;
		while (at(p) != '\0' && at(p) != '-')
		{
			++p;
		}
		m_pos = p;
		if (at(p) == '\0')
		{
			return false;
		}
		++p;
		m_pos = p;
		if (at(p) == '\0')
		{
			return false;
		}
		if (at(p) != '-')
		{
			continue;
		}
		++p;
		m_pos = p;
		if (at(p) == '\0')
		{
			return false;
		}
		if (at(p) != '>')
		{
			continue;
		}
		++p;
		m_pos = p;
		return true;
	}
}

bool EaXmlLexer::readEntity(std::string &out)
{
	// RW 0x94901C: the cursor is at '&'
	++m_pos;
	if (at(m_pos) == '\0')
	{
		return false;
	}
	for (const Entity &e : kEntities)
	{
		const size_t n = std::strlen(e.name);
		if (m_text.compare(m_pos, n, e.name) == 0)
		{
			m_pos += n;
			if (at(m_pos) != ';')
			{
				return false;
			}
			out.push_back(e.value);
			return true; // the cursor stays on ';' (the caller steps over it)
		}
	}
	return false;
}

int EaXmlLexer::readText()
{
	// RW 0x9490D2. edi counts the characters stored; at the limit the last slot is overwritten
	std::string buf;
	size_t edi = 0;
	for (;;)
	{
		const char c = at(m_pos);
		bool decoded = false;
		char stored = c;
		if (c == '&')
		{
			std::string one;
			if (!readEntity(one))
			{
				return fail();
			}
			stored = one[0];
			decoded = true;
		}
		(void)decoded;
		if (buf.size() <= edi)
		{
			buf.resize(edi + 1);
		}
		buf[edi] = stored;
		const char cl = at(m_pos);
		if (cl == '<' || cl == '\0')
		{
			buf.resize(edi); // buf[edi] = 0
			m_tail = buf;
			return TEXT;
		}
		++m_pos;
		if (at(m_pos) == '\0')
		{
			return fail();
		}
		if (!(edi > m_limit))
		{
			++edi;
		}
	}
}

int EaXmlLexer::readTag()
{
	// RW 0x948E05: the cursor is at '<'
	size_t p = skipSpace(m_pos);
	++p;
	m_pos = p;
	m_selfClosing = false;
	int kind = START;
	if (at(p) == '\0')
	{
		return fail();
	}
	if (at(p) == '/')
	{
		++p;
		m_pos = p;
		kind = FINISH;
		if (at(p) == '\0')
		{
			return fail();
		}
	}
	p = skipSpace(p);
	m_pos = p;
	if (at(p) == '\0')
	{
		return fail();
	}
	const size_t nameStart = p;
	while (at(p) != '\0' && (isAlnumC(at(p)) || at(p) == '_'))
	{
		++p;
	}
	m_pos = p;
	if (at(p) == '\0')
	{
		return fail();
	}
	const size_t nameEnd = p;
	m_attrNames.clear();
	m_attrValues.clear();
	p = skipSpace(p);
	m_pos = p;
	if (at(p) == '\0')
	{
		return fail();
	}
	if (kind == START)
	{
		while (isAlnumC(at(m_pos)))
		{
			const size_t an = m_pos;
			size_t q = an;
			while (at(q) != '\0' && (isAlnumC(at(q)) || at(q) == '_'))
			{
				++q;
			}
			m_pos = q;
			if (at(q) == '\0')
			{
				return fail();
			}
			const size_t anEnd = q;
			q = skipSpace(q);
			m_pos = q;
			if (at(q) == '\0' || at(q) != '=')
			{
				return fail();
			}
			++q;
			m_pos = q;
			if (at(q) == '\0')
			{
				return fail();
			}
			q = skipSpace(q);
			m_pos = q;
			if (at(q) == '\0' || (at(q) != '"' && at(q) != '\''))
			{
				return fail();
			}
			const char quote = at(q);
			const size_t vs = q + 1;
			m_pos = vs;
			if (at(vs) == '\0')
			{
				return fail();
			}
			size_t ve = vs;
			while (at(ve) != '\0' && at(ve) != quote)
			{
				++ve;
			}
			m_pos = ve;
			if (at(ve) == '\0')
			{
				return fail();
			}
			q = ve + 1;
			m_pos = q;
			if (at(q) == '\0')
			{
				return fail();
			}
			q = skipSpace(q);
			m_pos = q;
			if (at(q) == '\0')
			{
				return fail();
			}
			if (m_attrNames.size() < (size_t)MAX_ATTRIBUTES)
			{
				m_attrNames.push_back(m_text.substr(an, std::min<size_t>(anEnd - an, NAME_LIMIT)));
				m_attrValues.push_back(m_text.substr(vs, std::min<size_t>(ve - vs, VALUE_LIMIT)));
			}
		}
	}
	if (at(m_pos) == '/')
	{
		m_selfClosing = true;
	}
	else if (at(m_pos) == '>')
	{
		++m_pos;
	}
	else
	{
		return fail();
	}
	m_tail = m_text.substr(nameStart, nameEnd - nameStart);
	if (kind == START)
	{
		++m_depth;
	}
	else
	{
		--m_depth;
	}
	return kind;
}

int EaXmlLexer::next()
{
	// RW 0x9491D1
	if (m_selfClosing && m_posValid && at(m_pos) == '/')
	{
		++m_pos;
		if (at(m_pos) != '>')
		{
			return fail();
		}
		++m_pos;
		return FINISH;
	}
	if (!m_posValid)
	{
		return fail();
	}
	for (;;)
	{
		m_pos = skipSpace(m_pos);
		if (at(m_pos) == '\0')
		{
			m_tail.clear();
			m_attrNames.clear();
			m_attrValues.clear();
			return END;
		}
		if (at(m_pos) != '<')
		{
			return readText();
		}
		if (at(m_pos + 1) != '!')
		{
			return readTag();
		}
		if (!skipComment())
		{
			return fail();
		}
	}
}
