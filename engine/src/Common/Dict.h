// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Include/Common/Dict.h as far as the map loader needs it: a small
// ordered key/value dictionary.
//
// Differences from ZH, all deliberate:
//  * keys are the chunk-table-of-contents names (std::string), not NameKeyType. ZH interns the
//    name through TheNameKeyGenerator; the rebuild has no global name table yet, and the name
//    is the identity in both. Lookup is exact-case like nameToKey.
//  * pairs keep the order they were read in (so dumps are reproducible); a repeated key keeps
//    its first position and takes the last value, like Dict::setInt on an existing key.
//  * type codes are ZH Dict::DataType: 0 bool, 1 int, 2 real, 3 ascii, 4 unicode (types
//    kept per pair; an unknown code is a parse error, see DataChunkInput::readDict).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Dict
{
public:
	enum DataType
	{
		DICT_NONE = -1,
		DICT_BOOL = 0,
		DICT_INT = 1,
		DICT_REAL = 2,
		DICT_ASCIISTRING = 3,
		DICT_UNICODESTRING = 4
	};

	struct Pair
	{
		std::string key;
		DataType type = DICT_NONE;
		bool b = false;
		std::int32_t i = 0;
		float r = 0.0f;
		std::string s;    // ascii (Windows-1252 bytes as read)
		std::u16string u; // unicode (UTF-16 code units)
	};

	void setBool(const std::string &key, bool v);
	void setInt(const std::string &key, std::int32_t v);
	void setReal(const std::string &key, float v);
	void setAsciiString(const std::string &key, const std::string &v);
	void setUnicodeString(const std::string &key, const std::u16string &v);

	const Pair *find(const std::string &key) const;
	bool known(const std::string &key) const { return find(key) != nullptr; }

	// Typed getters: *exists (optional) reports presence AND matching type, like ZH
	// Dict::getBool(key, &exists). A missing or mistyped key returns the zero value; callers
	// that need to distinguish pass `exists`.
	bool getBool(const std::string &key, bool *exists = nullptr) const;
	std::int32_t getInt(const std::string &key, bool *exists = nullptr) const;
	float getReal(const std::string &key, bool *exists = nullptr) const;
	std::string getAsciiString(const std::string &key, bool *exists = nullptr) const;
	std::u16string getUnicodeString(const std::string &key, bool *exists = nullptr) const;

	size_t getPairCount() const { return m_pairs.size(); }
	const std::vector<Pair> &pairs() const { return m_pairs; }

private:
	Pair &slot(const std::string &key);
	std::vector<Pair> m_pairs;
};
