#ifndef __M3UARGPARSE_HPP__
#define __M3UARGPARSE_HPP__

#include <stdtype.h>

struct SongFileList
{
	std::string fileName;
	size_t playlistID;
};

UINT8 ParseSongFiles(int argc, char* argv[], std::vector<SongFileList>& songList, std::vector<std::string>& playlistList);

#endif	// __M3UARGPARSE_HPP__
