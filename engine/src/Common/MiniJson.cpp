// OpenBFME. GPL-3.0.

#include "Common/MiniJson.h"

#include <cstdlib>

namespace
{

class Parser
{
public:
	Parser(const std::string &text) : m_text(text) {}

	bool parseDocument(JsonValue &out, std::string *error)
	{
		skipWs();
		if (!parseValue(out, 0))
		{
			return fail(error);
		}
		skipWs();
		if (m_pos != m_text.size())
		{
			m_error = "trailing characters";
			return fail(error);
		}
		return true;
	}

private:
	bool fail(std::string *error)
	{
		if (error)
		{
			*error = "JSON error at byte " + std::to_string(m_pos) + ": " + m_error;
		}
		return false;
	}

	void skipWs()
	{
		while (m_pos < m_text.size())
		{
			char c = m_text[m_pos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++m_pos;
			}
			else
			{
				break;
			}
		}
	}

	bool literal(const char *word)
	{
		size_t n = std::char_traits<char>::length(word);
		if (m_text.compare(m_pos, n, word) == 0)
		{
			m_pos += n;
			return true;
		}
		m_error = std::string("expected ") + word;
		return false;
	}

	bool parseValue(JsonValue &out, int depth)
	{
		if (depth > 64)
		{
			m_error = "nesting too deep";
			return false;
		}
		if (m_pos >= m_text.size())
		{
			m_error = "unexpected end";
			return false;
		}
		char c = m_text[m_pos];
		if (c == '{')
		{
			return parseObject(out, depth);
		}
		if (c == '[')
		{
			return parseArray(out, depth);
		}
		if (c == '"')
		{
			out.type = JsonValue::STRING;
			return parseString(out.string);
		}
		if (c == 't')
		{
			out.type = JsonValue::BOOL;
			out.boolean = true;
			return literal("true");
		}
		if (c == 'f')
		{
			out.type = JsonValue::BOOL;
			out.boolean = false;
			return literal("false");
		}
		if (c == 'n')
		{
			out.type = JsonValue::NUL;
			return literal("null");
		}
		if (c == '-' || (c >= '0' && c <= '9'))
		{
			return parseNumber(out);
		}
		m_error = std::string("unexpected character '") + c + "'";
		return false;
	}

	bool parseNumber(JsonValue &out)
	{
		size_t start = m_pos;
		if (m_text[m_pos] == '-')
		{
			++m_pos;
		}
		while (m_pos < m_text.size())
		{
			char c = m_text[m_pos];
			if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
			{
				++m_pos;
			}
			else
			{
				break;
			}
		}
		std::string token = m_text.substr(start, m_pos - start);
		char *end = nullptr;
		out.type = JsonValue::NUMBER;
		out.number = std::strtod(token.c_str(), &end);
		if (end == nullptr || *end != 0 || token.empty() || token == "-")
		{
			m_error = "bad number '" + token + "'";
			return false;
		}
		return true;
	}

	static void appendUtf8(std::string &s, unsigned cp)
	{
		if (cp < 0x80)
		{
			s.push_back((char)cp);
		}
		else if (cp < 0x800)
		{
			s.push_back((char)(0xC0 | (cp >> 6)));
			s.push_back((char)(0x80 | (cp & 0x3F)));
		}
		else
		{
			s.push_back((char)(0xE0 | (cp >> 12)));
			s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
			s.push_back((char)(0x80 | (cp & 0x3F)));
		}
	}

	bool parseString(std::string &out)
	{
		++m_pos; // opening quote
		out.clear();
		while (m_pos < m_text.size())
		{
			char c = m_text[m_pos++];
			if (c == '"')
			{
				return true;
			}
			if ((unsigned char)c < 0x20)
			{
				m_error = "control character in string";
				return false;
			}
			if (c != '\\')
			{
				out.push_back(c);
				continue;
			}
			if (m_pos >= m_text.size())
			{
				break;
			}
			char e = m_text[m_pos++];
			switch (e)
			{
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\r'); break;
			case 't': out.push_back('\t'); break;
			case 'u':
			{
				if (m_pos + 4 > m_text.size())
				{
					m_error = "short \\u escape";
					return false;
				}
				unsigned cp = (unsigned)std::strtoul(m_text.substr(m_pos, 4).c_str(), nullptr, 16);
				m_pos += 4;
				appendUtf8(out, cp); // surrogate pairs are not needed by engine data
				break;
			}
			default:
				m_error = "bad escape";
				return false;
			}
		}
		m_error = "unterminated string";
		return false;
	}

	bool parseArray(JsonValue &out, int depth)
	{
		out.type = JsonValue::ARRAY;
		++m_pos;
		skipWs();
		if (m_pos < m_text.size() && m_text[m_pos] == ']')
		{
			++m_pos;
			return true;
		}
		for (;;)
		{
			JsonValue item;
			skipWs();
			if (!parseValue(item, depth + 1))
			{
				return false;
			}
			out.array.push_back(std::move(item));
			skipWs();
			if (m_pos >= m_text.size())
			{
				m_error = "unterminated array";
				return false;
			}
			char c = m_text[m_pos++];
			if (c == ']')
			{
				return true;
			}
			if (c != ',')
			{
				m_error = "expected , or ] in array";
				return false;
			}
		}
	}

	bool parseObject(JsonValue &out, int depth)
	{
		out.type = JsonValue::OBJECT;
		++m_pos;
		skipWs();
		if (m_pos < m_text.size() && m_text[m_pos] == '}')
		{
			++m_pos;
			return true;
		}
		for (;;)
		{
			skipWs();
			if (m_pos >= m_text.size() || m_text[m_pos] != '"')
			{
				m_error = "expected object key";
				return false;
			}
			std::string key;
			if (!parseString(key))
			{
				return false;
			}
			skipWs();
			if (m_pos >= m_text.size() || m_text[m_pos] != ':')
			{
				m_error = "expected :";
				return false;
			}
			++m_pos;
			skipWs();
			JsonValue value;
			if (!parseValue(value, depth + 1))
			{
				return false;
			}
			if (out.object.count(key))
			{
				m_error = "duplicate key '" + key + "'";
				return false;
			}
			out.object.emplace(key, std::move(value));
			skipWs();
			if (m_pos >= m_text.size())
			{
				m_error = "unterminated object";
				return false;
			}
			char c = m_text[m_pos++];
			if (c == '}')
			{
				return true;
			}
			if (c != ',')
			{
				m_error = "expected , or } in object";
				return false;
			}
		}
	}

	const std::string &m_text;
	size_t m_pos = 0;
	std::string m_error;
};

} // namespace

const JsonValue *JsonValue::get(const std::string &key) const
{
	if (type != OBJECT)
	{
		return nullptr;
	}
	auto it = object.find(key);
	return it == object.end() ? nullptr : &it->second;
}

bool JsonValue::parse(const std::string &text, JsonValue &out, std::string *error)
{
	out = JsonValue();
	Parser parser(text);
	return parser.parseDocument(out, error);
}
