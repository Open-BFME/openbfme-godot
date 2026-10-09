// OpenBFME. GPL-3.0.
//
// TextFile: BFME's whole-file line splitter, with comment stripping and `#include`
// expansion. Zero Hour has no such class (it streams one line at a time in INI::readLine);
// BFME and RotWK read the file whole and hand INI an array of lines.
//
// Sources, in PLAN rule 1 priority order. Port comments separate target facts, donor facts and
// inference:
//   TARGET (RotWK game.dat, disassembled; caveat PLAN rule 9):
//     TextFile::ParseFile   RW 0xA159AA (entry and file-read), 0xA15AFE-0xA15D42 (the scan loop),
//                           0xA1553C (include detection and operand extraction),
//                           0xA15C27-0xA15CB8 (include path: strncpy, PathRemoveFileSpecA,
//                           PathAppendA, '/' -> '\', PathCanonicalizeA), 0xA15D4B (open error)
//     char class table      0xDEC5A0 (0 normal, 1 end of line, 2 whitespace, 3 ';', 4 '/')
//   DONOR: BFME2 1.06 game.dat 0xA01C53-0xA02054, 0xA016C9 (same machine), 0xA0183C-0xA0186E
//          (operand), 0xA01EC8-0xA01FD8 (the lone-slash re-append); BFME1
//          Source/Common/INI/ini.cpp:517-542 (INI::readLine consumes the line array)
//   spec ini-and-object-model.md sections 1.2, 1.3, 2.1 (with the Sol review errata appended)
//
// The scan is ported as the literal state machine of RW 0xA15AFE:
//   * one pass over the NUL-terminated buffer; `cls` is the class of the current byte
//   * ';' and "//" overwrite everything up to the next end-of-line byte with NUL (no quote
//     awareness); a '/' not followed by '/' is reclassified as an ordinary character
//   * runs of NUL/CR/LF bytes are overwritten with NUL; the line counter advances once per CR
//   * a line starts at its first non-end-of-line byte; later whitespace bytes become spaces; the
//     first byte is left alone
//   * a line is emitted when the in-line loop stops on a byte that is not class 0/2, if at least
//     one class-0 byte was seen. THE LONE-SLASH QUIRK FALLS OUT OF THIS: the in-line loop stops on
//     '/', the line is emitted, the '/' is then reclassified as ordinary and scanning continues
//     on the SAME line start, so the line is emitted again at its end. `A/B` gives two entries
//     (n lone slashes give n + 1). Both entries point at the same text.
//   * a ';' or "//" also stops the in-line loop, so the line is emitted BEFORE its comment is
//     blanked. Include detection runs on the raw bytes up to the first CR/LF, so
//     `Foo = 1 ; #include "x.inc"` includes x.inc; a line that STARTS with ';' or "//" never
//     reaches the emit step, so ';#include' and '//#include' do not fire.
//   * include detection: copy at most min(260, remaining) - 1 bytes of the line (to the first
//     CR/LF), strstr "#include" (case sensitive), skip spaces, tabs and quotes, take bytes while
//     class 0 (so the run stops at whitespace, ';' and '/'), then NUL trailing quotes at the end
//     of that run. The operand is the C string from the start of the run, so it is NOT cut at a
//     '/' (and a quote after a '/' stays). Slashes become backslashes afterwards.
//   * no BOM handling, no continuation lines
//
// Inference / limits:
//   * the leading-whitespace-then-lone-slash line hands the retail parser a NULL line pointer
//     (RW 0xA15C1F with [ebp-0x14] cleared); here it is an error, never a silent line.
//   * the stored line number is the CR counter at emit time (as RW 0xA15D29 passes it).
//   * the include copy limit is min(260, fileSize - (lineStart + 1)) and the copy stops one byte
//     short of it (RW 0xA15C27-0xA15C3F, 0xA15557-0xA1555E), so on the last line of a file a short
//     unquoted include keeps its whole operand with a CRLF terminator, loses one byte with LF and
//     two bytes with no terminator.
//   * lone-slash entries SHARE their text buffer (RW 0xA158E9-0xA158FE store the same pointer), so
//     a later in-place edit of one entry is seen by the other: INI's #define pre-pass clears the
//     first byte of the shared text (0x42D104). Lines carry a bufferId for that.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class TextFile
{
public:
	// One kept line: text with comments blanked and whitespace folded, the CR-based line
	// number, and the index of the file it came from (so errors inside included files name the
	// right file).
	struct Line
	{
		std::string text;
		int lineNumber = 0;
		int fileIndex = 0;
		// Entries with the same non-zero bufferId are the lone-slash duplicates of one source line:
		// retail stores the same text pointer in both (RW 0xA158E9-0xA158FE), so editing one edits
		// the other. INI::runDefinePass relies on this.
		int bufferId = 0;
	};

	// Reads a virtual file. Must return false and set *error when the file is missing.
	typedef std::function<bool(const std::string &path, std::vector<std::uint8_t> &out, std::string *error)> FileReader;

	// Character class table (RW 0xDEC5A0). 0 normal, 1 end of line, 2 whitespace, 3 comment,
	// 4 slash.
	static int charClass(unsigned char c);

	// Reads `fileName` through `reader`, splits it and expands includes. Returns false and
	// fills *error (the retail debug text) on a missing file, a missing include, a circular
	// include or the NULL-line case. Replaces any earlier contents.
	bool parseFile(const std::string &fileName, const FileReader &reader, std::string *error);

	// Same for in-memory bytes presented as `fileName` (includes still go through `reader`).
	bool parseBytes(const std::string &fileName, const std::vector<std::uint8_t> &bytes, const FileReader &reader, std::string *error);

	const std::vector<Line> &lines() const { return m_lines; }
	const std::string &fileName(int index) const { return m_fileNames[(size_t)index]; }
	size_t fileCount() const { return m_fileNames.size(); }

	// Include path arithmetic, exposed for tests: PathRemoveFileSpecA + PathAppendA + '/'->'\'
	// + PathCanonicalizeA (Win32Path). Excess ".." does not fail: "a\b.ini" + "..\..\x.inc" is
	// "\x.inc", as in retail. Returns false only for forms Win32Path does not support (UNC, device,
	// doubled separators).
	static bool resolveIncludePath(const std::string &includingFile, const std::string &operand, std::string &out);

	// RW 0xA1553C, exposed for tests: given the raw text of a line (the bytes up to the first
	// CR/LF) and the copy limit, returns the include operand, or false when the line holds none.
	static bool findIncludeOperand(const std::string &rawLine, size_t copyLimit, std::string &operand);

private:
	bool parseBuffer(const std::string &fileName, const std::vector<std::uint8_t> &bytes, const FileReader &reader, std::string *error);
	bool handleInclude(const std::string &operand, int lineNumber, int fileIndex, const FileReader &reader, std::string *error);

	std::vector<Line> m_lines;
	std::vector<std::string> m_fileNames;
	std::vector<std::string> m_includeStack; // files being parsed, innermost last
	int m_bufferCounter = 0;
};
