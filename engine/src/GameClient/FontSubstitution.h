// OpenBFME. GPL-3.0.
//
// `data/ini/fontsubstitution.ini`: which font and size the game draws when a movie asks for a font.
//
// Target facts: none read (the binary's parser was not read, stop S-132).  The rule is the file's own header comment (retail data):
//   FontSubstitution "Albertus MT"
//     Size 10 = 10 Arial
//     Size 20 = 16 Arial
//     Size 24 = 18 Times New Roman
//   End
//   "The Font manager will interpolate sizes between adjacent values, and will always use the font name specified for the value less
//   than or equal to the font size requested. Values are clamped to the lowest and highest sizes specified"; a `+BOLD` / `-BOLD`
//   token before the substitute name forces / removes bold.  Examples in the comment: 9 -> 10 Arial, 15 -> 13 Arial, 22 -> 17 Arial,
//   24 -> 18 Times New Roman, 72 -> 36 Times New Roman.  The rounding of an interpolated size is not stated (truncated here, S-132).
//
// A font with no block is drawn as itself at the requested size.

#pragma once

#include <map>
#include <string>
#include <vector>

struct FontRequestResult
{
	std::string name;
	float size = 0;
	int bold = 0; // 0 as requested, +1 force bold, -1 force regular
	bool substituted = false;
};

class FontSubstitution
{
public:
	// Parse the file text; false + error on a line the grammar does not accept.
	bool parse(const std::string &text, std::string *error);
	FontRequestResult resolve(const std::string &fontName, float size) const;
	std::size_t blockCount() const { return m_blocks.size(); }

private:
	struct Entry
	{
		float requested = 0;
		float size = 0;
		std::string name;
		int bold = 0;
	};
	std::map<std::string, std::vector<Entry>> m_blocks; // key: lower-cased font name; entries sorted by `requested`
};
