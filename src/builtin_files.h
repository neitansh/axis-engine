// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 Luanti contributors

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

extern const std::unordered_map<std::string_view, std::string_view> g_builtin_file_sha256_map;

/**
 * Verify a builtin file against the hash compiled into the binary.
 *
 * @param path_local      path relative to the builtin directory
 * @param code            the file contents as read from disk
 * @param error           set to a human-readable message when verification fails
 * @param warn_if_unknown log files that have no known hash. Pass false when
 *                        scanning the whole builtin directory, which also holds
 *                        locale files and unit tests that are never hashed.
 * @return false only if the file has a known hash that the contents don't match.
 *         Files without a known hash are accepted, since the build system
 *         doesn't hash every file shipped in builtin.
 */
bool checkBuiltinFileIntegrity(const std::string &path_local, std::string_view code,
		std::string *error, bool warn_if_unknown = true);
