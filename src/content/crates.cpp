// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include <common/c_internal.h>
#include "content/crates.h"
#include "constants.h"
#include "porting.h"
#include "filesys.h"
#include "settings.h"
#include "log.h"
#include "util/strfnd.h"
#include "map_settings_manager.h"
#include "util/string.h"
#include "exceptions.h"
#include "gettext.h"
#include <algorithm>

// The maximum number of identical world names allowed
#define MAX_WORLD_NAMES 100

// crateid to assume for worlds that are missing world.mt
#define LEGACY_CRATEID "minetest"

namespace
{

bool getCrateConfig(const std::string &crate_path, Settings &conf)
{
	// Настройки, которые крейт хочет поверх умолчаний движка. Рядом читался
	// ещё и "minetest.conf" — так годами возили свои настройки чужие игры;
	// наши крейты свои, и второго имени у файла нет.
	const std::string conf_path = crate_path + DIR_DELIM + "crate_defaults.conf";
	return conf.readConfigFile(conf_path.c_str());
}

// Keep in sync with pkgmgr.lua, `pkgmgr.normalize_crate_id()`.
std::string normalizeCrateId(std::string_view id)
{
	// "_game" — суффикс ContentDB, а не наш: там крейт зовётся игрой, и
	// пакет "mycrate_game" приходит оттуда именно с ним.
	static const char *ends[] = {"_game", nullptr};
	auto shorter = removeStringEnd(id, ends);
	return std::string(shorter.empty() ? id : shorter);
}

std::unordered_set<std::string> getAliasesFromSettings(const Settings &conf)
{
	std::unordered_set<std::string> aliases;
	if (!conf.exists("aliases"))
		return aliases;

	std::vector<std::string> aliases_raw = str_split(conf.get("aliases"), ',');
	for (const std::string &alias : aliases_raw)
		aliases.insert(normalizeCrateId(trim(alias)));
	return aliases;
}

// Где ещё искать крейты, кроме дома игрока и каталога рядом с движком.
//
// Имя у переменной одно: движок наш, крейты наши, и трёх имён одной и той же
// переменной, как было у Luanti, здесь незачем.
std::string getCratePathEnv()
{
	if (const char *path = getenv("AXIS_CRATE_PATH"))
		return std::string(path);
	return "";
}

std::string getWorldPathEnv()
{
	static bool has_warned = false;

	if (const char *path = getenv("LUANTI_WORLD_PATH"))
		return std::string(path);

	if (const char *path = getenv("MINETEST_WORLD_PATH")) {
		if (!has_warned) {
			warningstream << "MINETEST_WORLD_PATH is deprecated, use LUANTI_WORLD_PATH instead."
				      << std::endl;
			has_warned = true;
		}
		return std::string(path);
	}
	return "";
}

}

void CrateSpec::checkAndLog() const
{
	// Log deprecation messages
	auto handling_mode = get_deprecated_handling_mode();
	if (!deprecation_msgs.empty() && handling_mode != DeprecatedHandlingMode::Ignore) {
		std::ostringstream os;
		os << "Crate " << title << " at " << path << ":" << std::endl;
		for (auto msg : deprecation_msgs)
			os << "\t" << msg << std::endl;

		if (handling_mode == DeprecatedHandlingMode::Error)
			throw ModError(os.str());
		else
			warningstream << os.str();
	}
}

struct CrateFindPath
{
	std::string path;
	bool user_specific; // If true, crate is in path_user
	std::unordered_set<std::string> aliases;

	CrateFindPath(const std::string &path, bool user_specific) :
			path(path), user_specific(user_specific)
	{
	}
	CrateFindPath(const std::string &path, bool user_specific, std::unordered_set<std::string>&& aliases) :
			path(path), user_specific(user_specific), aliases(aliases)
	{
	}
};

using CratePathMap = std::unordered_map<std::string, CrateFindPath>;

static CratePathMap getAvailableCratePaths()
{
	CratePathMap cratepaths;
	std::vector<CrateFindPath> crate_search_paths{
		{porting::path_share + DIR_DELIM + "depot", false},
		{porting::path_user + DIR_DELIM + "depot", true}
	};

	Strfnd search_paths(getCratePathEnv());

	while (!search_paths.at_end())
		crate_search_paths.emplace_back(search_paths.next(PATH_DELIM), false);

	for (const CrateFindPath &search_path : crate_search_paths) {
		auto dirlist = fs::GetDirListing(search_path.path);
		for (const fs::DirListNode &dln : dirlist) {
			if (!dln.dir)
				continue;

			// If configuration file is not found or broken, ignore crate
			Settings conf;
			const std::string crate_path = search_path.path + DIR_DELIM + dln.name;
			if (!conf.readConfigFile((crate_path + DIR_DELIM "crate.conf").c_str()))
				continue;

			// Add it to result
			cratepaths.try_emplace(normalizeCrateId(dln.name),
				crate_path, search_path.user_specific, getAliasesFromSettings(conf)
			);
		}
	}
	return cratepaths;
}

static CrateSpec getCrateSpec(const std::string &crate_id,
		const std::string &crate_path,
		const std::unordered_map<std::string, std::string> &mods_paths)
{
	const auto cratemods_path = crate_path + DIR_DELIM + "mods";
	// Get meta
	const std::string conf_path = crate_path + DIR_DELIM + "crate.conf";
	Settings conf;
	conf.readConfigFile(conf_path.c_str());

	std::string crate_title;
	if (conf.exists("title"))
		crate_title = conf.get("title");
	else if (conf.exists("name"))
		crate_title = conf.get("name");
	else
		crate_title = crate_id;

	std::string crate_author;
	if (conf.exists("author"))
		crate_author = conf.get("author");

	int crate_release = 0;
	if (conf.exists("release"))
		crate_release = conf.getS32("release");

	std::string first_mod;
	if (conf.exists("first_mod"))
		first_mod = conf.get("first_mod");

	std::string last_mod;
	if (conf.exists("last_mod"))
		last_mod = conf.get("last_mod");

	auto aliases = getAliasesFromSettings(conf);

	CrateSpec spec(crate_id, crate_path, cratemods_path, mods_paths, crate_title,
			crate_author, crate_release, first_mod, last_mod, aliases);

	if (conf.exists("name") && !conf.exists("title"))
		spec.deprecation_msgs.push_back("\"name\" setting in crate.conf is deprecated, please use \"title\" instead");

	return spec;
}

std::set<std::string> getAvailableCrateIds()
{
	CratePathMap cratepaths = getAvailableCratePaths();
	std::set<std::string> crateids;
	for (auto &&p : cratepaths)
		crateids.insert(p.first);
	return crateids;
}

std::vector<CrateSpec> getAvailableCrates()
{
	std::vector<CrateSpec> specs;
	std::set<std::string> crateids = getAvailableCrateIds();
	specs.reserve(crateids.size());
	for (const auto &crateid : crateids)
		specs.push_back(findCrate(crateid));
	// TODO: Optimize such that `getAvailableCratePaths()` is not run N times.
	return specs;
}

CrateSpec findCrate(const std::string &id)
{
	if (id.empty())
		return CrateSpec();

	std::string idv = normalizeCrateId(id);

	CratePathMap cratepaths = getAvailableCratePaths();
	auto found = cratepaths.find(idv);
	if (found == cratepaths.end()) { // Failed to find the crate, try to find aliased crate
		for (auto it = cratepaths.begin(); it != cratepaths.end(); ++it) {
			if (it->second.aliases.find(idv) != it->second.aliases.end()) {
				found = it;
				break;
			}
		}
	}

	if (found == cratepaths.end()) // Failed to find the crate taking aliases into account
		return CrateSpec();

	// Found the crate, proceed
	const CrateFindPath &data = found->second;
	const std::string &crate_path = data.path;
	bool user_crate = data.user_specific;


	// Find mod directories
	const std::string &share = porting::path_share;
	const std::string &user = porting::path_user;
	std::unordered_map<std::string, std::string> mods_paths;
	mods_paths["mods"] = user + DIR_DELIM + "mods";
	if (!user_crate && user != share)
		mods_paths["share"] = share + DIR_DELIM + "mods";

	for (const std::string &mod_path : getEnvModPaths()) {
		mods_paths[fs::AbsolutePath(mod_path)] = mod_path;
	}

	return getCrateSpec(found->first, crate_path, mods_paths);
}

CrateSpec findWorldCrate(const std::string &world_path)
{
	std::string world_crateid = getWorldCrateId(world_path, true);
	// See if world contains an embedded crate; if so, use it.
	std::string world_cratepath = world_path + DIR_DELIM + "crate";
	if (fs::PathExists(world_cratepath))
		return getCrateSpec(world_crateid, world_cratepath, {});
	return findCrate(world_crateid);
}

bool getWorldExists(const std::string &world_path)
{
	if (world_path.empty())
		return false;
	// Note: very old worlds are valid without a world.mt
	return (fs::IsFile(world_path + DIR_DELIM + "map_meta.txt") ||
			fs::IsFile(world_path + DIR_DELIM + "world.mt"));
}

//! Try to get the displayed name of a world
std::string getWorldName(const std::string &world_path, const std::string &default_name)
{
	std::string conf_path = world_path + DIR_DELIM + "world.mt";
	Settings conf;
	bool succeeded = conf.readConfigFile(conf_path.c_str());
	if (!succeeded) {
		return default_name;
	}

	if (!conf.exists("world_name"))
		return default_name;
	return conf.get("world_name");
}

std::string getWorldCrateId(const std::string &world_path, bool can_be_legacy)
{
	std::string conf_path = world_path + DIR_DELIM + "world.mt";
	Settings conf;
	bool succeeded = conf.readConfigFile(conf_path.c_str());
	if (!succeeded) {
		if (can_be_legacy) {
			// If map_meta.txt exists, it is probably a very old world
			if (fs::PathExists(world_path + DIR_DELIM + "map_meta.txt"))
				return LEGACY_CRATEID;
		}
		return "";
	}
	if (!conf.exists("crateid"))
		return "";
	return conf.get("crateid");
}

std::vector<WorldSpec> getAvailableWorlds()
{
	std::vector<WorldSpec> worlds;
	std::set<std::string> worldspaths;

	Strfnd search_paths(getWorldPathEnv());

	while (!search_paths.at_end())
		worldspaths.insert(search_paths.next(PATH_DELIM));

	worldspaths.insert(porting::path_user + DIR_DELIM + "worlds");
	infostream << "Searching worlds..." << std::endl;
	for (const std::string &worldspath : worldspaths) {
		infostream << "  In " << worldspath << ": ";
		std::vector<fs::DirListNode> dirvector = fs::GetDirListing(worldspath);
		for (const fs::DirListNode &dln : dirvector) {
			if (!dln.dir)
				continue;
			std::string fullpath = worldspath + DIR_DELIM + dln.name;
			std::string name = getWorldName(fullpath, dln.name);
			// Just allow filling in the crateid always for now
			bool can_be_legacy = true;
			std::string crateid = getWorldCrateId(fullpath, can_be_legacy);
			WorldSpec spec(fullpath, name, crateid);
			if (!spec.isValid()) {
				infostream << "(invalid: " << name << ") ";
			} else {
				infostream << name << " ";
				worlds.push_back(spec);
			}
		}
		infostream << std::endl;
	}
	// Check old world location
	do {
		std::string fullpath = porting::path_user + DIR_DELIM + "world";
		if (!fs::PathExists(fullpath))
			break;
		std::string name = "Old World";
		std::string crateid = getWorldCrateId(fullpath, true);
		WorldSpec spec(fullpath, name, crateid);
		infostream << "Old world found." << std::endl;
		worlds.push_back(spec);
	} while (false);
	infostream << worlds.size() << " found." << std::endl;
	return worlds;
}

void loadCrateConfAndInitWorld(const std::string &path, const std::string &name,
		const CrateSpec &cratespec, bool create_world)
{
	std::string final_path = path;

	// If we're creating a new world, ensure that the path isn't already taken
	if (create_world) {
		int counter = 1;
		while (fs::PathExists(final_path) && counter < MAX_WORLD_NAMES) {
			final_path = path + "_" + std::to_string(counter);
			counter++;
		}

		if (fs::PathExists(final_path)) {
			throw BaseException("Too many similar filenames");
		}
	}

	Settings *crate_settings = Settings::getLayer(SL_CRATE);
	const bool new_crate_settings = (crate_settings == nullptr);
	if (new_crate_settings) {
		// Called by main-menu without a Server instance running
		// -> create and free manually
		crate_settings = Settings::createLayer(SL_CRATE);
	}

	getCrateConfig(cratespec.path, *crate_settings);
	crate_settings->removeSecureSettings();

	infostream << "Initializing world at " << final_path << std::endl;

	fs::CreateAllDirs(final_path);

	// Create world.mt if does not already exist
	std::string worldmt_path = final_path + DIR_DELIM "world.mt";
	if (!fs::PathExists(worldmt_path)) {
		Settings crateconf;
		std::string crateconf_path = cratespec.path + DIR_DELIM "crate.conf";
		crateconf.readConfigFile(crateconf_path.c_str());

		Settings conf; // for world.mt

		conf.set("world_name", name);
		conf.set("crateid", cratespec.id);

		std::string backend = "sqlite3";
		if (crateconf.exists("map_persistent") && !crateconf.getBool("map_persistent")) {
			backend = "dummy";
		}
		conf.set("backend", backend);

		conf.set("player_backend", "sqlite3");
		conf.set("auth_backend", "sqlite3");
		conf.set("mod_storage_backend", "sqlite3");
		conf.setBool("creative_mode", g_settings->getBool("creative_mode"));
		conf.setBool("enable_damage", g_settings->getBool("enable_damage"));
		if (MAP_BLOCKSIZE != 16)
			conf.set("blocksize", std::to_string(MAP_BLOCKSIZE));

		if (!conf.updateConfigFile(worldmt_path.c_str())) {
			throw BaseException("Failed to update world.mt");
		}
	}

	// Create map_meta.txt if does not already exist
	std::string map_meta_path = final_path + DIR_DELIM + "map_meta.txt";
	if (!fs::PathExists(map_meta_path)) {
		MapSettingsManager mgr(map_meta_path);

		mgr.setMapSetting("seed", g_settings->get("fixed_map_seed"));

		mgr.makeMapgenParams();
		mgr.saveMapMeta();
	}

	// The Settings object is no longer needed for created worlds
	if (new_crate_settings)
		delete crate_settings;
}

std::string createWorld(const std::string &name, const std::string &crateid,
		const std::unordered_map<std::string, std::string> &settings)
{
	const std::string path = porting::path_user + DIR_DELIM "worlds" DIR_DELIM
			+ sanitizeDirName(name, "world_");

	const std::vector<CrateSpec> crates = getAvailableCrates();
	auto crate = std::find_if(crates.begin(), crates.end(),
			[&crateid](const CrateSpec &spec) { return spec.id == crateid; });
	if (crate == crates.end())
		return strgettext("Game ID not found");

	// Генератор карты читает свои настройки из g_settings, и передать их
	// иначе некуда; после создания мира они возвращаются как были.
	std::unordered_map<std::string, std::string> backup;
	for (const auto &it : settings) {
		if (g_settings->existsLocal(it.first))
			backup[it.first] = g_settings->get(it.first);
		g_settings->set(it.first, it.second);
	}

	std::string error;
	try {
		loadCrateConfAndInitWorld(path, name, *crate, true);
	} catch (const BaseException &e) {
		error = strgettext("Failed to initialize world: ") + e.what();
	}

	for (const auto &it : settings) {
		auto was = backup.find(it.first);
		if (was == backup.end())
			g_settings->remove(it.first);
		else
			g_settings->set(it.first, was->second);
	}
	return error;
}

std::string deleteWorld(const WorldSpec &world)
{
	if (!fs::RecursiveDelete(world.path))
		return strgettext("Failed to delete world");
	return "";
}

std::vector<std::string> getEnvModPaths()
{
	static bool has_warned = false;

	std::vector<std::string> paths;
	const char *c_mod_path = nullptr;
	if ((c_mod_path = getenv("LUANTI_MOD_PATH"))) {
		// no-op
	} else if ((c_mod_path = getenv("MINETEST_MOD_PATH"))) {
		if (!has_warned) {
			warningstream << "MINETEST_MOD_PATH is deprecated, use LUANTI_MOD_PATH instead."
				      << std::endl;
			has_warned = true;
		}
	}

	if (c_mod_path) {
		Strfnd search_paths(c_mod_path);
		while (!search_paths.at_end())
			paths.push_back(search_paths.next(PATH_DELIM));
	}
	return paths;
}
