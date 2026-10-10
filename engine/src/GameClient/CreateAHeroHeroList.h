// OpenBFME. GPL-3.0.
//
// CreateAHeroHeroList (lane CAH-1): the Create-a-Heroes a player can pick: the system heroes of the mounted archives and the profile's own heroes,
// which the builder screen (GameClient/GUI/AptScreens/AptCreateAHero.h) creates, edits, saves and deletes and the lobby's Hero combo lists.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the file list (RW 0x61EEB6): "MyHero*.cah" (RW 0xBFC5E8) of "Data\SystemHeroes\" (RW 0xBFC5D4) in the archives, each kept as (file name after the
//     last '\', system = true) and sorted (RW 0x61EE28 -> 0x61ED51 / 0x61D664); then, unless the flag RW 0xDE3D8C is set, L"MyHero*.cah" (RW 0xBFC5BC) of
//     the profile's save folder (RW 0x6DD398: the user data folder RW 0x6427AA + L"Save\" RW 0xC1A320) as (name, system = false);
//   * a hero's file is "MyHero_" + its unique id + ".cah" (RW 0x61A3AD: "%s%s%s" RW 0xBFC140 over "MyHero_" RW 0xBFBF0C / ".cah" RW 0xBFBF04);
//   * a hero gets its unique id the first time it is named (RW 0x80A352: when +0x4C is empty, CoCreateGuid and "%X%X%X%X%X%X%X" RW 0xC4F28C over Data1,
//     Data2, Data3 and the first four bytes of Data4; the record becomes valid, +0x71);
//   * a system hero (+0x48) cannot be deleted (AptCreateAHero Manager::OnDeleteHero RW 0x9C5D6F asks GUI:AreYouSureDelete only for one that is not, unless
//     the flag RW 0xDE3D8C is set).
// INFERENCE (stop S-1400): the comparator of the sort was not read: the port sorts by file name, case-insensitively, system heroes first; the profile
// folder is the device's (the Godot user data folder + "Save"); the GUID comes from the client's random device (client state, not logic: the id only
// names files, the per-rank command sets and the level chain, and travels with the record).

#pragma once

#include "Common/CreateAHeroRecord.h"

#include <string>
#include <vector>

class ArchiveFileSystem;

struct CreateAHeroListEntry
{
	CreateAHeroHero hero;
	std::string fileName; ///< "MyHero_<id>.cah" (the name after the folder)
	bool system = false;  ///< from Data\SystemHeroes\ (the archives) rather than the profile
};

class CreateAHeroHeroList
{
public:
	// RW 0x61EEB6: the system heroes of `fs` and the profile's heroes of `profileDir` (empty: none). A file that does not load is an error in *errors and
	// is left out (retail keeps no broken record either: the loader RW 0x80B4F5 fails the read).
	void load(ArchiveFileSystem *fs, const std::string &profileDir, std::vector<std::string> *errors);
	const std::vector<CreateAHeroListEntry> &entries() const { return m_entries; }
	size_t size() const { return m_entries.size(); }
	const CreateAHeroListEntry *at(int index) const { return index >= 0 && (size_t)index < m_entries.size() ? &m_entries[(size_t)index] : nullptr; }
	int findByUniqueID(const std::string &id) const;
	const std::string &profileDir() const { return m_profileDir; }

	// Saves a profile hero to `profileDir`/MyHero_<id>.cah (RW 0x61A3AD) and puts it into the list (replacing the entry of that id); the hero must have a
	// unique id. False + *error when the folder or the file cannot be written. Returns the hero's index through *index.
	bool save(const CreateAHeroHero &hero, std::string *error, int *index = nullptr);
	// Removes a profile hero and its file; a system hero is refused (false).
	bool remove(int index, std::string *error);

	// RW 0x80A352's id: "%X%X%X%X%X%X%X" of a fresh GUID
	static std::string newUniqueID();
	static std::string fileNameOf(const std::string &uniqueID) { return "MyHero_" + uniqueID + ".cah"; }

private:
	void sort();
	std::vector<CreateAHeroListEntry> m_entries;
	std::string m_profileDir;
};
