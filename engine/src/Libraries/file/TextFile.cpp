// OpenBFME. GPL-3.0.
//
// Port of RotWK TextFile::ParseFile (RW 0xA159AA). See TextFile.h for the citation list and the
// description of the state machine.

#include "Libraries/file/TextFile.h"

#include "Common/AsciiString.h"
#include "Libraries/file/Win32Path.h"

#include <algorithm>
#include <cstring>

namespace
{
// RW 0xA15C2A: the scratch copy is min(0x104, remaining) bytes.
const size_t kIncludeScratchBytes = 0x104;
}

// RW 0xDEC5A0 (B2 0xA016C9). Built once over all 256 byte values:
//   1 end of line: \0 \n \r        2 whitespace: C-locale isspace minus \n \r (\t \v \f space)
//   3 comment: ;                   4 slash: /           0 everything else
int TextFile::charClass(unsigned char c)
{
	if (c == 0 || c == '\n' || c == '\r')
	{
		return 1;
	}
	if (c == '\t' || c == '\v' || c == '\f' || c == ' ')
	{
		return 2;
	}
	if (c == ';')
	{
		return 3;
	}
	if (c == '/')
	{
		return 4;
	}
	return 0;
}

bool TextFile::parseFile(const std::string &fileName, const FileReader &reader, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	std::string readError;
	if (!reader(fileName, bytes, &readError))
	{
		if (error)
		{
			*error = readError;
		}
		return false;
	}
	return parseBytes(fileName, bytes, reader, error);
}

bool TextFile::parseBytes(const std::string &fileName, const std::vector<std::uint8_t> &bytes, const FileReader &reader, std::string *error)
{
	m_lines.clear();
	m_fileNames.clear();
	m_includeStack.clear();
	m_bufferCounter = 0;
	return parseBuffer(fileName, bytes, reader, error);
}

// RW 0xA1553C. TARGET FACTS:
//   copy bytes from the line start until CR, LF or (limit - 1) bytes (NULs are copied);
//   strstr(copy, "#include"); skip ' ', '\t', '"'; run = bytes while class 0 (bounded by limit);
//   walk back from the end of the run, NUL-ing '"' bytes; the operand is the C string at the
//   start of the run.
bool TextFile::findIncludeOperand(const std::string &raw, size_t limit, std::string &operand)
{
	std::string scratch;
	for (size_t i = 0; i < raw.size() && i + 1 < limit; ++i)
	{
		const char ch = raw[i];
		if (ch == '\r' || ch == '\n')
		{
			break;
		}
		scratch.push_back(ch);
	}
	scratch.push_back('\0');

	const size_t at = std::string(scratch.c_str()).find("#include");
	if (at == std::string::npos)
	{
		return false;
	}
	size_t p = at + std::strlen("#include");
	while (scratch[p] == ' ' || scratch[p] == '\t' || scratch[p] == '"')
	{
		++p;
	}

	auto at_p = [&](size_t k) -> char { return p + k < scratch.size() ? scratch[p + k] : '\0'; };
	size_t run = 0;
	while (charClass((unsigned char)at_p(run)) == 0 && run < limit)
	{
		++run;
	}
	// walk back from the end of the run, NUL-ing quotes
	size_t back = run;
	while (back > 1)
	{
		--back;
		if (at_p(back) != '"')
		{
			break;
		}
		scratch[p + back] = '\0';
	}
	// `dec eax; test eax,eax; jg` : index 0 is never examined
	operand = std::string(scratch.c_str() + p);
	return true;
}

// RW 0xA15AFE-0xA15D42, ported line for line. Variable names follow the disassembly's roles:
//   esi = index, c = class of buf[esi] (the cls local), newLine = [ebp-0xC], lineNo = [ebp-0x10],
//   ptr = [ebp-0x14] (line start, -1 = NULL), startNext = [ebp-0x18] (line start + 1), al = the
//   "saw a class-0 byte" flag.
bool TextFile::parseBuffer(const std::string &fileName, const std::vector<std::uint8_t> &bytes, const FileReader &reader, std::string *error)
{
	const int fileIndex = (int)m_fileNames.size();
	m_fileNames.push_back(fileName);
	m_includeStack.push_back(fileName);

	std::vector<char> buf(bytes.begin(), bytes.end());
	buf.push_back('\0');
	const size_t n = bytes.size();

	struct Pending
	{
		size_t lineIndex;
		long start;
	};
	std::vector<Pending> pending;

	auto cls = [&](size_t i) { return charClass((unsigned char)buf[i]); };
	auto fail = [&](const std::string &message) {
		if (error)
		{
			*error = message;
		}
		m_includeStack.pop_back();
		return false;
	};

	size_t esi = 0;
	int c = cls(0);
	bool newLine = true;
	int lineNo = 1;
	long ptr = -1;
	size_t startNext = 0;
	bool al = false;

	while (esi < n)
	{
		if (c == 4)
		{
			// RW 0xA15B1F: "//" is a comment, a lone '/' is an ordinary character
			if (esi + 1 < n && buf[esi + 1] == '/')
			{
				// comment to end of line: RW 0xA15B7F
				while (esi < n)
				{
					if (c == 1)
					{
						break;
					}
					buf[esi] = 0;
					c = cls(esi + 1);
					++esi;
				}
				newLine = true;
				continue;
			}
			c = 0;
		}

		if (c == 3)
		{
			// ';' comment to end of line: RW 0xA15B9F
			while (esi < n)
			{
				if (c == 1)
				{
					break;
				}
				buf[esi] = 0;
				++esi;
				c = cls(esi);
			}
			newLine = true;
			continue;
		}

		if (c == 1)
		{
			// RW 0xA15BC8: a run of NUL/CR/LF bytes; CR advances the line counter
			newLine = true;
			while (c == 1)
			{
				if (buf[esi] == '\r')
				{
					++lineNo;
				}
				buf[esi] = 0;
				++esi;
				c = cls(esi);
				if (esi >= n)
				{
					break;
				}
			}
			continue;
		}

		// c is 0 or 2: RW 0xA15B30
		al = (c == 0);
		if (newLine)
		{
			ptr = (long)esi;
			++esi;
			c = cls(esi);
			startNext = esi;
		}
		newLine = false;
		for (;;)
		{
			const bool cont = (esi < n && c == 0) || c == 2;
			if (!cont)
			{
				break;
			}
			if (c != 2)
			{
				al = true;
			}
			else
			{
				buf[esi] = ' ';
			}
			++esi;
			c = cls(esi);
		}

		// RW 0xA15C1F: the in-line loop stopped on end of line, ';' or '/'
		if (!al)
		{
			ptr = -1; // RW 0xA15D36
			continue;
		}
		if (ptr < 0)
		{
			return fail("TextFile: " + fileName + " line " + std::to_string(lineNo) +
				": whitespace followed by a lone '/' hands the line parser a NULL line (RW 0xA15C1F); retail has no such line");
		}

		const size_t limit = std::min<size_t>(kIncludeScratchBytes, n - startNext);
		std::string raw;
		for (size_t i = (size_t)ptr; i < n + 1 && raw.size() < limit; ++i)
		{
			raw.push_back(buf[i]);
		}
		std::string operand;
		if (findIncludeOperand(raw, limit, operand))
		{
			if (!handleInclude(operand, lineNo, fileIndex, reader, error))
			{
				m_includeStack.pop_back();
				return false;
			}
		}
		else
		{
			Line line;
			line.lineNumber = lineNo;
			line.fileIndex = fileIndex;
			// a second emit of the same line start shares the first entry's buffer
			line.bufferId = (!pending.empty() && pending.back().start == ptr) ? m_lines[pending.back().lineIndex].bufferId : ++m_bufferCounter;
			pending.push_back({ m_lines.size(), ptr });
			m_lines.push_back(std::move(line));
		}
	}

	// both entries of a lone-slash line point at the same text; text is read after the scan
	for (const Pending &p : pending)
	{
		m_lines[p.lineIndex].text = std::string(&buf[(size_t)p.start]);
	}
	m_includeStack.pop_back();
	return true;
}

// RW 0xA15C27-0xA15CB8: strncpy(tmp, currentFile, 260); PathRemoveFileSpecA(tmp);
// PathAppendA(tmp, operand); every '/' to '\'; PathCanonicalizeA.
bool TextFile::resolveIncludePath(const std::string &includingFile, const std::string &operand, std::string &out)
{
	// RW 0xA15C6C-0xA15CCB: strncpy(tmp, currentFile, 260); PathRemoveFileSpecA(tmp);
	// PathAppendA(tmp, operand); every '/' to '\'; PathCanonicalizeA(canon, tmp). The shlwapi calls
	// are ported in Win32Path (fitted to the real calls, re-verified by tests on Windows). Forms
	// outside that port's domain (UNC, device, doubled separators) are refused loudly.
	std::string tmp = includingFile.substr(0, 260);
	if (!Win32Path::isSupportedPathForm(tmp))
	{
		return false;
	}
	Win32Path::removeFileSpec(tmp);
	const std::string appended = (!operand.empty() && operand[0] == '\\') ? operand.substr(1) : operand; // PathAppend drops one leading backslash
	if (!Win32Path::isSupportedPathForm(tmp) || !Win32Path::isSupportedPathForm(operand) || Win32Path::inLengthBand(tmp + "\\" + appended))
	{
		return false;
	}
	// RW 0xA15C97 ignores PathAppendA's result: past MAX_PATH it returns false with the buffer
	// cleared, and the sequence carries on with the empty string (which canonicalises to "\\").
	Win32Path::append(tmp, operand);
	for (char &ch : tmp)
	{
		if (ch == '/')
		{
			ch = '\\';
		}
	}
	// RW 0xA15CCB: the canonical form is used when PathCanonicalizeA succeeds, else the raw buffer
	std::string canonical;
	out = Win32Path::canonicalize(tmp, canonical) ? canonical : tmp;
	return true;
}

// RW 0xA15C47-0xA15D4B; error strings at RW 0xC931B4 / 0xC93210 / 0xC931C8.
bool TextFile::handleInclude(const std::string &operand, int lineNumber, int fileIndex, const FileReader &reader, std::string *error)
{
	const std::string &currentFile = m_fileNames[(size_t)fileIndex];

	std::string path;
	if (operand.empty() || !resolveIncludePath(currentFile, operand, path))
	{
		if (error)
		{
			*error = "ERROR: Could not open include in " + currentFile + " line " + std::to_string(lineNumber) + ", file " + operand + ", parent file " + currentFile;
		}
		return false;
	}

	for (const std::string &open : m_includeStack)
	{
		if (AsciiStringUtil::compareNoCase(open, path) == 0)
		{
			if (error)
			{
				*error = "ERROR: Circular include in " + currentFile + " line " + std::to_string(lineNumber) + ", Circular file " + path + ", parent file " + currentFile;
			}
			return false;
		}
	}

	std::vector<std::uint8_t> bytes;
	std::string readError;
	if (!reader(path, bytes, &readError))
	{
		if (error)
		{
			*error = "ERROR: Could not open include in " + currentFile + " line " + std::to_string(lineNumber) + ", file " + path + ", parent file " + currentFile + " (" + readError + ")";
		}
		return false;
	}
	return parseBuffer(path, bytes, reader, error);
}
