// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Common/UserPreferences.cpp, QuotedPrintable.cpp).
//
// SkirmishPreferences (lane MENU-1): the Skirmish profiles a player made, kept in "Skirmish.ini" in the user data folder, so the profile the first
// visit of Skirmish asks for is there again at the next start. Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; donor BFME2 decomp Common/SkirmishPreferences.cpp, tier A for every RW address below):
//   * the constructor (RW 0x8221F5) loads "Skirmish.ini" (RW 0xC50B44) as a UserPreferences file, decodes the value of "UserNames" (RW 0xC50B38)
//     from quoted-printable to UTF-16, trims it and splits it on "," into the name list, then keeps the raw value of "CurrentUserName" (RW 0xC50B24);
//   * the current name (RW 0x821A07's sibling, BFME2 0x43B9F5) is the decoded, trimmed CurrentUserName; "has a profile" (RW 0x821A07) is "the list is
//     not empty", which the Skirmish update reads every frame of state 5 to open the add-profile box (RW 0x928CDB: ShowAddProfile);
//   * adding (BFME2 0x43C612) appends the name, rebuilds "UserNames" as every name followed by "," (RW 0x822626) and writes the file; the add-profile
//     handler (BFME2 0x521B56) compares names without case (0x43BBB6) and refuses an empty or known one, then sets the current name (RW 0x82259F:
//     CurrentUserName = the quoted-printable name) and writes again;
//   * removing (BFME2 0x43C2EB) drops the exact name and writes; removing the current one makes the first name current (BFME2 0x522833 with 0x43BB88);
//   * quoted-printable (RW 0x93BD82 / 0x93BF28): each UTF-16 unit as its low byte then its high byte; a byte that isalnum() accepts is kept, any
//     other becomes '_' and two upper-case hex digits; decoding turns '_' and the next one or two hex digits back into a byte.
//   * lane UI-6: a profile's own entries are ":<mode>:<the quoted-printable current name>:<name>" (RW 0x821AB4 formats ":%d:%s:%s", RW 0x821B89 with the
//     current name); the lobby keeps its game in "Map" (RW 0x821E55), "GameInfo" and "HeroIndexes" (RW 0x821D55, through RW 0x821EBD) and reads them back
//     (RW 0x821C29); removing a profile erases every entry of it for both modes (BFME2 0x43C2EB: RW 0x8220A3 with modes 0 and 1).
// INFERENCE (stop S-3504): career statistics are not recorded; BFME1 creation dates use ISO local format; isalnum is the ASCII test (retail's CRT call on a byte above 0x7F was not read); the
// folder is the one Options.ini is in (S-1484).

#pragma once

#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/OptionPreferences.h"

#include <string>
#include <vector>

class SkirmishPreferences : public SkirmishProfileStore
{
public:
	// `path`: the Skirmish.ini file. A missing file is a first run (no profiles); a file that cannot be read is an error (errors()).
	explicit SkirmishPreferences(std::string path);

	std::vector<std::u16string> names() const override { return m_names; }
	std::u16string current() const override;
	bool add(const std::u16string &name) override;
	bool remove(const std::u16string &name) override;
	bool setCurrent(const std::u16string &name) override;
	std::string profileValue(int mode, const std::string &name) const override;
	void setProfileValue(int mode, const std::string &name, const std::string &value) override;
	void flush() override { write(); }
	// lane OWNERFIX-3: the folder Skirmish.ini is in (the user data folder, RW 0x7B2B7F's prefix), with a trailing separator
	std::string folder() const override;
	// RW 0x821AB4: ":%d:%s:%s" (mode, the quoted-printable profile name, the entry's name)
	static std::string profileKey(int mode, const std::string &quotedName, const std::string &name);

	// Everything that went wrong reading or writing the file (never silent).
	const std::vector<std::string> &errors() const { return m_errors; }
	const std::string &path() const { return m_path; }

	// what the port infers (reported in the shell report's unverified_device)
	static const char *unverified()
	{
		return "[S-3504] Skirmish.ini: career statistics are not recorded; BFME1 creation dates use ISO local format and highest rank progress is 100; isalnum is the ASCII test; "
			   "the file is beside Options.ini";
	}
	static std::string unicodeToQuotedPrintable(const std::u16string &text);
	static std::u16string quotedPrintableToUnicode(const std::string &text);

private:
	void write();

	std::string m_path;
	OptionPreferences m_file;
	std::vector<std::u16string> m_names;
	std::string m_currentUserName; // quoted-printable, as the file holds it
	std::vector<std::string> m_errors;
};
