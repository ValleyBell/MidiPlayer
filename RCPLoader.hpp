#ifndef __RCPLOADER_HPP__
#define __RCPLOADER_HPP__

#include <stdio.h>
#include <stdtype.h>
#include "MidiLib.hpp"

UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile, std::vector<std::string>& initFiles);
UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile, std::vector<std::string>& initFiles);
UINT8 Cm62Syx(const char* fileName, std::vector<UINT8>& syxData);
UINT8 Cm62Syx(FILE* infile, std::vector<UINT8>& syxData);
UINT8 Gsd2Syx(const char* fileName, std::vector<UINT8>& syxData);
UINT8 Gsd2Syx(FILE* infile, std::vector<UINT8>& syxData);

#endif	// __RCPLOADER_HPP__
