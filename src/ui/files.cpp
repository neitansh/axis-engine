// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "files.h"

#include "filesys.h"
#include <cstdio>
#include <fstream>
#include <memory>

namespace ui
{

static std::ifstream &stream(Rml::FileHandle file)
{
	return *reinterpret_cast<std::ifstream *>(file);
}

Rml::FileHandle Files::Open(const Rml::String &path)
{
	auto ifs = std::make_unique<std::ifstream>(open_ifstream(path.c_str(), true));
	if (!ifs->good())
		return 0;
	return reinterpret_cast<Rml::FileHandle>(ifs.release());
}

void Files::Close(Rml::FileHandle file)
{
	delete &stream(file);
}

size_t Files::Read(void *buffer, size_t size, Rml::FileHandle file)
{
	std::ifstream &ifs = stream(file);
	ifs.read(static_cast<char *>(buffer), size);
	return ifs.gcount();
}

bool Files::Seek(Rml::FileHandle file, long offset, int origin)
{
	std::ifstream &ifs = stream(file);
	ifs.clear();
	std::ios::seekdir dir = std::ios::beg;
	if (origin == SEEK_CUR)
		dir = std::ios::cur;
	else if (origin == SEEK_END)
		dir = std::ios::end;
	ifs.seekg(offset, dir);
	return ifs.good();
}

size_t Files::Tell(Rml::FileHandle file)
{
	return stream(file).tellg();
}

size_t Files::Length(Rml::FileHandle file)
{
	std::ifstream &ifs = stream(file);
	const auto pos = ifs.tellg();
	ifs.seekg(0, std::ios::end);
	const auto length = ifs.tellg();
	ifs.seekg(pos);
	return length;
}

}
