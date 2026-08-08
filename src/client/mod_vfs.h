// SPDX-FileCopyrightText: 2025 Luanti authors
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>
#include <unordered_map>

struct ModVFS
{
	/**
	 * @param verify  check each file against the builtin hashes compiled into
	 *                the binary; only valid when scanning the builtin directory
	 */
	void scanModSubfolder(const std::string &mod_name, const std::string &mod_path,
			std::string mod_subpath, bool verify = false);

	inline void scanModIntoMemory(const std::string &mod_name, const std::string &mod_path,
			bool verify = false)
	{
		scanModSubfolder(mod_name, mod_path, "", verify);
	}

	const std::string *getModFile(std::string filename);

	std::unordered_map<std::string, std::string> m_vfs;
};
