#ifndef __M3UARGPARSE_HPP__
#define __M3UARGPARSE_HPP__

#include <string>
#include <vector>
#include <stdtype.h>

struct SongFileList
{
	std::string fileName;
	size_t playlistID;
};

UINT8 ParseSongFiles(const std::vector<std::string> args, std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);

#endif	// __M3UARGPARSE_HPP__
