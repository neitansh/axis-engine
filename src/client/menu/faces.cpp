// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "faces.h"

#include "filesys.h"
#include "porting.h"
#include "util/hashing.h"
#include "util/hex.h"
#include <algorithm>
#include <cctype>

namespace menu
{

static bool isHash(const std::string &skin)
{
	return skin.size() == 64 && std::all_of(skin.begin(), skin.end(),
			[](unsigned char c) { return std::isxdigit(c); });
}

static bool isCrateId(const std::string &id)
{
	return !id.empty() && std::all_of(id.begin(), id.end(),
			[](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

std::string Faces::fetch(const std::string &dir, const std::string &file, const std::string &url)
{
	if (fs::PathExists(file))
		return file;
	if (!m_asked.insert(file).second)
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

std::string Faces::path(const std::string &skin, const std::string &url)
{
	// Хэш становится именем файла: чужое в нём — путь мимо кэша.
	if (!isHash(skin) || url.empty())
		return "";
	const std::string dir = porting::path_cache + DIR_DELIM "faces";
	return fetch(dir, dir + DIR_DELIM + skin + ".png", url);
}

std::string Faces::cover(const std::string &crate, const std::string &url)
{
	if (!isCrateId(crate) || url.empty())
		return "";
	const std::string dir = porting::path_cache + DIR_DELIM "covers";
	const std::string version = hex_encode(hashing::sha1(url)).substr(0, 12);
	return fetch(dir, dir + DIR_DELIM + crate + "-" + version + ".png", url);
}

}
