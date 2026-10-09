// OpenBFME. GPL-3.0.
//
// INIException: the exception every INI error is thrown as.
//   BFME1 Source/Common/INI/INIExceptionCtor.cpp, ini.cpp:395-454 (INIException(code, fmt, ...))
//   ZH Include/Common/INI.h:69-88 (error codes)
//   spec ini-and-object-model.md section 1.9
//
// Retail throws a variadic INIException(argCount, fmt, ...). The first integer is carried
// along as the "code"; the codes used in retail text are:
//   2 internal error (no name list)      3 bad data / missing token
//   4 missing End                        5 unknown block / field
//   6 file already open                  7 cannot open file
//   8 anything that is not an INIException, and macro/pre-pass errors
// A few range checks throw a plain `int` 1 in retail (parseByte, parseUnsignedByte,
// parseShort, parseUnsignedShort, parseByteSizedIndexList, parseBitString8): those are
// INIPlainIntError here, and initFromINIMulti wraps them as code 8 exactly like retail does
// for any non-INIException.

#pragma once

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <string>

class INIException : public std::exception
{
public:
	INIException(int code, const char *fmt, ...)
		: m_code(code)
	{
		char buffer[4096];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		buffer[sizeof(buffer) - 1] = 0;
		m_message = buffer;
	}

	// Message already formatted (used when re-wrapping with a prefix).
	static INIException withMessage(int code, const std::string &message)
	{
		INIException e(code, "%s", message.c_str());
		return e;
	}

	int code() const { return m_code; }
	const std::string &message() const { return m_message; }
	const char *what() const noexcept override { return m_message.c_str(); }

private:
	int m_code;
	std::string m_message;
};

// `throw 1;` in retail (ThrowInfo type ".H").
class INIPlainIntError : public std::exception
{
public:
	explicit INIPlainIntError(const char *what) : m_what(what) {}
	int value() const { return 1; }
	const char *what() const noexcept override { return m_what; }

private:
	const char *m_what;
};
