#ifndef __RCPLOADER_HPP__
#define __RCPLOADER_HPP__

#include <stdio.h>
#include <stdtype.h>
#include "MidiLib.hpp"

UINT8 LoadRCPAsMidi(const char* fileName, MidiFile& midFile);
UINT8 LoadRCPAsMidi(FILE* infile, MidiFile& midFile);

#endif	// __RCPLOADER_HPP__
