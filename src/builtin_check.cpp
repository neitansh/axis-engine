// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 Luanti contributors

#include "builtin_files.h"
#include "config.h"
#include "filesys.h"
#include "log.h"
#include "util/hashing.h"
#include "util/hex.h"
#include <algorithm>

bool checkBuiltinFileIntegrity(const std::string &path_local, std::string_view code,
		std::string *error, bool warn_if_unknown)
{
	// The hash map is keyed by the paths as spelled in builtin's CMakeLists,
	// which always use '/'.
	std::string key = path_local;
	std::replace(key.begin(), key.end(), DIR_DELIM_CHAR, '/');

	auto it = g_builtin_file_sha256_map.find(key);
	if (it == g_builtin_file_sha256_map.end()) {
		if (warn_if_unknown) {
			warningstream << "No SHA256 known for builtin file \"" << key << "\""
					<< std::endl;
		}
		return true;
	}

	const std::string digest = hex_encode(hashing::sha256(code));
	if (it->second == digest)
		return true;

	std::string msg = "SHA256 of builtin file \"" + key + "\" does not match."
			"\nExpected: " + std::string(it->second) +
			"\nFound:    " + digest;

	if (!ENFORCE_BUILTIN_INTEGRITY) {
		warningstream << msg << std::endl;
		return true;
	}

	if (error)
		*error = msg + "\nThis build refuses to run modified builtin files.";
	return false;
}
