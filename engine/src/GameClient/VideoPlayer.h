// OpenBFME. GPL-3.0.
//
// Lane CAMP-1H (owner feedback F6, the campaign's voice-over): TheVideoPlayer's registry of the Video blocks and the part of a movie stream that
// reaches the player's ears. The campaign's narrated intros and exits (LinearCampaign OverallCampaignIntroMovie / IntroMovie, the maps'
// PLAY_MOVIE_IN_GAME) are VP6 movies whose sound is not in the movie file: opening the stream plays the audio events named after the file.
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001):
//   * the Video block's fields (table RW 0xCFD2E8): Filename parseAsciiString (+0x0), Comment parseAsciiString (+0x8), HasSubtitles parseBool (+0xC),
//     Volume parsePercentToReal (RW 0x42EEFA, +0x10), IsDefault parseBool (+0x14); the internal name is the block's token;
//   * VideoPlayer::open (RW 0x490EE6): nothing when TheGlobalData + 0x9AD is set; the Video by title (vslot 0x60; none: the debug-only "A movie named
//     ... was requested but can't be found", null); then the first of three search directories (RW 0x490D08: 0x145-byte entries from RW 0xDC9EE8, a
//     leading "enabled" byte) where "%s%s.%s" (directory, Filename, "vp6", RW 0xBDDF78) exists (RW 0xA14AEB): the mod's "<mod>\Data\Movies"
//     (only with a mod directory, TheGlobalData + 0xD38), "Lang/<language>/Data/Movies/" (RW 0xBDDEDC), "Data\Movies" (RW 0xBDDF08); none: the
//     debug-only "Could not open VP6 video file for %s - %s.", null;
//   * the stream's audio (vslot 0x28 of the stream, RW 0x49112C): the audio events "<Filename>_Music" (RW 0xBDDF84) and "<Filename>", each when the
//     audio manager knows it (TheAudio vslot 300), are added (vslot 100) and their handles kept (+0x60 music, +0x5C speech);
//   * the movie file is EA's VP6 container: a "MVhd" chunk (RW's library, RW 0x5B4FC0 ..) with the codec tag "vp60", width, height, the frame count,
//     the largest chunk and the frame rate as numerator / denominator (decoded here only for the length of the movie).
// DONOR FACTS (Open-BFME-2 VideoPlayerAddVideo.cpp RW 0x689FD0 / VideoPlayerGetVideo.cpp RW 0x689B10, BFME2 1.06): addVideo replaces a video of the
// same internal name (exact compare) else appends; getVideo compares the internal names case-insensitively. ZH VideoPlayer::init loads
// Data\INI\Default\Video.ini then Data\INI\Video.ini (INI_LOAD_OVERWRITE); RotWK's loader was not located (INFERENCE: the same two files).
// NOT PORTED (stop S-1710): the VP6 picture (no decoder: the movie shows black for its length), the movie subtitles (HasSubtitles: no Angmar movie
// sets it), TheGlobalData + 0x9AD.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;

struct Video
{
	std::string filename;      ///< +0x0 Filename
	std::string internalName;  ///< +0x4 the block's token
	std::string comment;       ///< +0x8 Comment
	bool hasSubtitles = false; ///< +0xC HasSubtitles
	float volume = 0.0f;       ///< +0x10 Volume (parsePercentToReal)
	bool isDefault = false;    ///< +0x14 IsDefault
};

// what opening a movie gives the client: where the file is, how long it runs, which audio events start with it
struct VideoStreamInfo
{
	bool ok = false;
	std::string error;
	std::string title;    ///< the Video's internal name
	std::string path;     ///< the file found (a path below the install root, the search directory's spelling)
	std::uint32_t width = 0, height = 0, frames = 0;
	std::uint32_t rateNumerator = 0, rateDenominator = 0;
	double durationMs = 0.0; ///< frames / (numerator / denominator)
	std::vector<std::string> audioEvents; ///< "<Filename>_Music" then "<Filename>", the ones the audio manager knows (RW 0x49112C)
};

class VideoPlayer
{
public:
	static const std::vector<std::string> &loadOrder(); ///< Data\INI\Default\Video.ini, Data\INI\Video.ini
	void registerBlock(INIBlockRegistry &registry);
	void parseVideoDefinition(INI *ini);
	void addVideo(const Video &v);                       ///< donor RW 0x689FD0
	const Video *getVideo(const std::string &title) const; ///< donor RW 0x689B10 (case-insensitive)
	const std::vector<Video> &videos() const { return m_videos; }

	// RW 0x490EE6's lookup: `installRoot` is the game directory (the loose movies), `modRoot` the mod's directory ("" none), `language` the install's
	// language ("English"). `audioKnown` answers whether an audio event exists (TheAudio vslot 300). Errors name the title and the paths tried.
	template <class Known>
	VideoStreamInfo open(const std::string &title, const std::string &installRoot, const std::string &modRoot, const std::string &language,
		Known audioKnown) const
	{
		VideoStreamInfo info = locate(title, installRoot, modRoot, language);
		if (info.ok)
		{
			const Video *v = getVideo(title);
			for (const std::string &e : { v->filename + "_Music", v->filename })
			{
				if (audioKnown(e))
				{
					info.audioEvents.push_back(e);
				}
			}
		}
		return info;
	}
	VideoStreamInfo locate(const std::string &title, const std::string &installRoot, const std::string &modRoot, const std::string &language) const;

	// EA's VP6 container header ("MVhd"); false + *error for anything else
	static bool readHeader(const std::uint8_t *data, size_t size, VideoStreamInfo &out, std::string *error);
	static std::vector<std::string> stopLines();

private:
	std::vector<Video> m_videos;
};
