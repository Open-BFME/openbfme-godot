// OpenBFME. GPL-3.0.
//
// Minimal JSON reader for the engine's own data files (retail archive policies). Strict:
// any syntax error is reported, nothing is guessed.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

class JsonValue
{
public:
	enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

	Type type = NUL;
	bool boolean = false;
	double number = 0.0;
	std::string string;
	std::vector<JsonValue> array;
	std::map<std::string, JsonValue> object;

	bool isObject() const { return type == OBJECT; }
	bool isArray() const { return type == ARRAY; }
	bool isString() const { return type == STRING; }
	bool isNumber() const { return type == NUMBER; }

	// nullptr when absent or not an object.
	const JsonValue *get(const std::string &key) const;

	static bool parse(const std::string &text, JsonValue &out, std::string *error);
};
