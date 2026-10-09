// OpenBFME. GPL-3.0.
//
// EaXmlLexer: the XML reader EA's LuaScriptEngine reads ScriptEvents.xml with (RW 0x949147 constructor, 0x9491D1 next-token). It is NOT a
// conforming XML parser: it is a small tokenizer, and the retail loader's control flow depends on exactly what it accepts and returns, so the
// port follows the disassembly (target facts; spec 4.1, B1's BfmeLexEAN has no body). Read from RW (caveat S-001):
//
//   * constructor 0x949147(text, buffer, limit 0xFFF): line number 1, no tag, no attributes. If the text starts with "<?", the first 0x15 bytes
//     must equal `<?xml version="1.0"?>` (string at RW 0xC8054C) and are skipped; any other "<?" start (an encoding attribute, a different
//     version) leaves the cursor at NULL + 0x15 and retail faults on the first read. The port reports it (crashesRetail()) and reads nothing.
//   * next() 0x9491D1 returns 0 at the end of the text, 1 for a start tag, 2 for an end tag, 3 for text, -1 for a malformed construct. After
//     -1 the cursor, depth and line are zeroed (RW 0x948D9A) and every later next() returns -1.
//       - a pending "/>" (set by the start tag) is consumed first and returned as an end tag (2); a "/" not followed by ">" is -1;
//       - the character a tag name was cut at is restored, white space is skipped (isspace, counting '\n'), "<!" starts a comment (0x94907C: needs
//         "<!--" and ends at the first "-->" the scan sees; "--->" does NOT end a comment) and any other "<!" is -1; a "<" that is not "<!" is
//         a tag (0x948E05), anything else is text (0x9490D2); the end of the text returns 0 with no tag;
//       - a tag: "<" ["/"] name; the name is a run of isalnum or '_' (so no '-', ':' or '.'), followed by white space, then (start tags only)
//         attributes name = "value" or name = 'value' while the next character is isalnum, then "/" (self-closing: next() returns 2 next time) or
//         ">". Anything else, or the end of the text, is -1. Only the first 4 attributes are kept, a name cut at 32 and a value at 63 characters
//         (0x948F7C / 0x948FA6: strncpy 0x20 / 0x3F). Attribute values are not entity-decoded. Start tags increment the depth, end tags decrement it;
//       - text: leading white space skipped, up to the next "<", with &quot; &apos; &amp; &lt; &gt; style entities (table 0xDB72C4, five entries:
//         the name, then ';') decoded; text that reaches the end of the input without a "<" is -1; characters beyond the buffer limit are dropped.
//         The text token's value (tail) is the text, white space inside and after it kept.
//   * tail() is [this+0x1C]: the tag name or the text of the last token.
//
// The entity table at 0xDB72C4 is read in the unit tests (binary-fact test); the port uses the five XML names.

#pragma once

#include <string>
#include <vector>

class EaXmlLexer
{
public:
	enum
	{
		END = 0,
		START = 1,
		FINISH = 2, ///< an end tag
		TEXT = 3,
		ERROR_TOKEN = -1
	};
	enum
	{
		MAX_ATTRIBUTES = 4,
		NAME_LIMIT = 0x20,
		VALUE_LIMIT = 0x3F
	};

	// `text` is the whole file; a NUL byte ends it, as in retail's C string
	explicit EaXmlLexer(const std::string &text, size_t bufferLimit = 0xFFF);

	int next();
	const std::string &tail() const { return m_tail; }          ///< 0x7B0F25 / [this+0x1C]
	int attributeCount() const { return (int)m_attrNames.size(); } ///< 0x4986C8
	const std::string &attributeName(int i) const { return m_attrNames.at((size_t)i); }
	const std::string &attributeValue(int i) const { return m_attrValues.at((size_t)i); }
	int depth() const { return m_depth; }
	int line() const { return m_line; }
	bool crashesRetail() const { return m_crashesRetail; }       ///< the text starts with "<?" but not with the exact declaration

private:
	int fail();
	size_t skipSpace(size_t p);
	bool skipComment();
	int readTag();
	int readText();
	bool readEntity(std::string &out);
	char at(size_t p) const { return p < m_text.size() ? m_text[p] : '\0'; }

	std::string m_text;
	size_t m_pos = 0;
	bool m_posValid = true;   // false: the cursor is NULL (after an error or the faulting declaration)
	int m_line = 1;
	int m_depth = 0;
	bool m_selfClosing = false; // [this+0x10]
	std::string m_tail;
	std::vector<std::string> m_attrNames, m_attrValues;
	size_t m_limit;
	bool m_crashesRetail = false;
};
