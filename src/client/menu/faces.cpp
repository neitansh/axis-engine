// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "faces.h"

#include "filesys.h"
#include "porting.h"
#include <algorithm>
#include <cctype>

namespace menu
{

static bool isHash(const std::string &skin)
{
	return skin.size() == 64 && std::all_of(skin.begin(), skin.end(),
			[](unsigned char c) { return std::isxdigit(c); });
}

std::string Faces::path(const std::string &skin, const std::string &url)
{
	// Хэш становится именем файла: чужое в нём — путь мимо кэша.
	if (!isHash(skin) || url.empty())
		return "";
	const std::string dir = porting::path_cache + DIR_DELIM "faces";
	const std::string file = dir + DIR_DELIM + skin + ".png";
	if (fs::PathExists(file))
		return file;
	if (!m_asked.insert(skin).second)
		return "";
	m_net.get(url, {}, 10000, [this, dir, file](const Net::Answer &res) {
		if (!res.ok() || res.raw.empty())
			return;
		if (!fs::CreateAllDirs(dir) || !fs::safeWriteToFile(file, res.raw))
			return;
		if (m_on_change)
			m_on_change();
	});
	return "";
}

}
