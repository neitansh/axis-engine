// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include <string>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>


struct PlaceSpec
{
	std::string id;
	std::string title;
	std::string author;
	int release;
	std::string first_mod; // "" <=> no mod
	std::string last_mod; // "" <=> no mod
	std::string path;
	std::string placemods_path;

	/**
	 * Map from virtual path to mods path
	 */
	std::unordered_map<std::string, std::string> addon_mods_paths;

	/**
	 * All worlds are marked with a specific placeid. To allow inheriting worlds
	 * by a game with different placeid after deprecation or renaming, this set
	 * contains the placeid values that used to refer to the game and may be used
	 * to automatically find the best matching placeid.
	 */
	std::unordered_set<std::string> aliases;

	// For logging purposes
	std::vector<const char *> deprecation_msgs;

	PlaceSpec(const std::string &id = "", const std::string &path = "",
			const std::string &placemods_path = "",
			const std::unordered_map<std::string, std::string> &addon_mods_paths = {},
			const std::string &title = "",
			const std::string &author = "", int release = 0,
			const std::string &first_mod = "",
			const std::string &last_mod = "",
			const std::unordered_set<std::string> &aliases = {}) :
			id(id),
			title(title), author(author), release(release),
			first_mod(first_mod),
			last_mod(last_mod),
			path(path),
			placemods_path(placemods_path),
			addon_mods_paths(addon_mods_paths),
			aliases(aliases)
	{
	}

	bool isValid() const { return (!id.empty() && !path.empty()); }
	void checkAndLog() const;
};

PlaceSpec findPlace(const std::string &id);
PlaceSpec findWorldPlace(const std::string &world_path);

std::set<std::string> getAvailablePlaceIds();
std::vector<PlaceSpec> getAvailablePlaces();
// Get the list of paths to mods in the environment variable LUANTI_MOD_PATH
std::vector<std::string> getEnvModPaths();

bool getWorldExists(const std::string &world_path);
//! Try to get the displayed name of a world
std::string getWorldName(const std::string &world_path, const std::string &default_name);
std::string getWorldPlaceId(const std::string &world_path, bool can_be_legacy = false);

struct WorldSpec
{
	std::string path;
	std::string name;
	std::string placeid;

	WorldSpec(const std::string &path = "", const std::string &name = "",
			const std::string &placeid = "") :
			path(path),
			name(name), placeid(placeid)
	{
	}

	bool isValid() const
	{
		return (!name.empty() && !path.empty() && !placeid.empty());
	}
};

std::vector<WorldSpec> getAvailableWorlds();

// loads the place's config and creates world directory
// and world.mt if they don't exist
void loadPlaceConfAndInitWorld(const std::string &path, const std::string &name,
		const PlaceSpec &placespec, bool create_world);
