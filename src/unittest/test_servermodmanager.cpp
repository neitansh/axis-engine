// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2018 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#include "test.h"
#include <algorithm>
#include <fstream>
#include "content/subgames.h"
#include "filesys.h"
#include "server/mods.h"
#include "settings.h"

// The game and the mods this module runs against are built in the temporary
// directory, so the test states what it expects instead of depending on the
// content of some game that happens to be installed.
#define SUBGAME_ID "testgame"

class TestServerModManager : public TestBase
{
public:
	TestServerModManager() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestServerModManager"; }

	void runTests(IGameDef *gamedef);

	std::string m_worlddir;

	static ServerModManager makeManager(const std::string &worldpath) {
		return ServerModManager(worldpath, findWorldSubgame(worldpath));
	}

	void testCreation();
	void testIsConsistent();
	void testUnsatisfiedMods();
	void testGetMods();
	void testLoadsInstalledMods();
	void testGetModsWrongDir();
	void testGetModspec();
	void testGetModNamesWrongDir();
	void testGetModNames();
	void testGetModMediaPathsWrongDir();
	void testGetModMediaPaths();

private:
	/// Writes a mod that does nothing, with a textures directory of its own
	static void makeMod(const std::string &path, const std::string &name,
			const std::string &depends = "");
	static void writeFile(const std::string &path, const std::string &content);
};

static TestServerModManager g_test_instance;

void TestServerModManager::writeFile(const std::string &path,
		const std::string &content)
{
	std::ofstream os(path, std::ios::out | std::ios::binary);
	os << content;
}

void TestServerModManager::makeMod(const std::string &path,
		const std::string &name, const std::string &depends)
{
	fs::CreateAllDirs(path);
	fs::CreateAllDirs(path + DIR_DELIM "textures");

	std::string conf = "name = " + name + "\ndescription = Does nothing\n";
	if (!depends.empty())
		conf += "depends = " + depends + "\n";

	writeFile(path + DIR_DELIM "mod.conf", conf);
	writeFile(path + DIR_DELIM "init.lua", "-- intentionally empty\n");
	writeFile(path + DIR_DELIM "textures" DIR_DELIM "test.png", "");
}

void TestServerModManager::runTests(IGameDef *gamedef)
{
	// A game of four mods: one forced first, one forced last, and a pair
	// where the second overrides media of the first.
	const auto games = getTestTempDirectory().append(DIR_DELIM "test_games");
	const auto game = games + (DIR_DELIM SUBGAME_ID);
	const auto gamemods = game + (DIR_DELIM "mods" DIR_DELIM);

	fs::CreateAllDirs(game);
	writeFile(game + (DIR_DELIM "game.conf"),
			"title = Test Game\n"
			"first_mod = first_mod\n"
			"last_mod = last_mod\n");

	makeMod(gamemods + "first_mod", "first_mod");
	makeMod(gamemods + "base_mod", "base_mod");
	makeMod(gamemods + "dependent_mod", "dependent_mod", "base_mod");
	makeMod(gamemods + "last_mod", "last_mod");

	setenv("LUANTI_GAME_PATH", games.c_str(), 1);

	// A mod outside of the game, as a player would install it
	const auto test_mods = getTestTempDirectory().append(DIR_DELIM "test_mods");
	makeMod(test_mods + (DIR_DELIM "test_mod"), "test_mod");

	setenv("LUANTI_MOD_PATH", test_mods.c_str(), 1);

	m_worlddir = getTestTempDirectory().append(DIR_DELIM "world");
	fs::CreateDir(m_worlddir);

	// The mod folders of the machine this runs on must not decide what the
	// tests below see, so the world states its mods explicitly. The one test
	// that is about the other policy turns it back on for itself.
	const bool enable_all_mods = g_settings->getBool("enable_all_mods");
	g_settings->setBool("enable_all_mods", false);

	TEST(testCreation);
	TEST(testIsConsistent);
	TEST(testGetModsWrongDir);
	TEST(testUnsatisfiedMods);
	TEST(testGetMods);
	TEST(testLoadsInstalledMods);
	TEST(testGetModspec);
	TEST(testGetModNamesWrongDir);
	TEST(testGetModNames);
	TEST(testGetModMediaPathsWrongDir);
	TEST(testGetModMediaPaths);

	g_settings->setBool("enable_all_mods", enable_all_mods);
	unsetenv("LUANTI_MOD_PATH");
	unsetenv("LUANTI_GAME_PATH");
}

void TestServerModManager::testCreation()
{
	std::string path = m_worlddir + DIR_DELIM + "world.mt";
	Settings world_config;
	world_config.set("gameid", SUBGAME_ID);
	world_config.set("load_mod_test_mod", "true");
	UASSERTEQ(bool, world_config.updateConfigFile(path.c_str()), true);

	auto sm = makeManager(m_worlddir);
}

void TestServerModManager::testGetModsWrongDir()
{
	// Test in non worlddir to ensure no mods are found
	auto sm = makeManager(m_worlddir + DIR_DELIM "..");
	UASSERTEQ(bool, sm.getMods().empty(), true);
}

void TestServerModManager::testUnsatisfiedMods()
{
	auto sm = makeManager(m_worlddir);
	UASSERTEQ(bool, sm.getUnsatisfiedMods().empty(), true);
}

void TestServerModManager::testIsConsistent()
{
	auto sm = makeManager(m_worlddir);
	UASSERTEQ(bool, sm.isConsistent(), true);
}

void TestServerModManager::testGetMods()
{
	auto sm = makeManager(m_worlddir);
	const auto &mods = sm.getMods();
	// The four mods of the game plus the one from LUANTI_MOD_PATH
	UASSERTEQ(std::size_t, mods.size(), 4 + 1);

	bool game_mod_found = false;
	bool test_mod_found = false;
	for (const auto &m : mods) {
		if (m.name == "base_mod")
			game_mod_found = true;
		if (m.name == "test_mod")
			test_mod_found = true;

		// Verify if paths are not empty
		UASSERTEQ(bool, m.path.empty(), false);
	}

	UASSERTEQ(bool, game_mod_found, true);
	UASSERTEQ(bool, test_mod_found, true);

	// The game decides what runs before and after everything else
	UASSERT(mods.front().name == "first_mod");
	UASSERT(mods.back().name == "last_mod");

	// A mod is loaded after what it depends on
	auto base = std::find_if(mods.begin(), mods.end(),
			[](const ModSpec &m) { return m.name == "base_mod"; });
	auto dependent = std::find_if(mods.begin(), mods.end(),
			[](const ModSpec &m) { return m.name == "dependent_mod"; });
	UASSERT(base != mods.end() && dependent != mods.end());
	UASSERT(base < dependent);
}

void TestServerModManager::testLoadsInstalledMods()
{
	// With this on, a mod that is installed runs without the world saying so,
	// and the world keeps no mod list at all.
	g_settings->setBool("enable_all_mods", true);

	std::string path = m_worlddir + DIR_DELIM + "world.mt";
	Settings world_config;
	world_config.set("gameid", SUBGAME_ID);
	UASSERTEQ(bool, world_config.updateConfigFile(path.c_str()), true);

	{
		auto sm = makeManager(m_worlddir);
		std::vector<std::string> names;
		sm.getModNames(names);
		UASSERT(std::find(names.begin(), names.end(), "test_mod") != names.end());
	}

	// The list of the world is gone, and the mod is still loaded
	Settings written;
	UASSERTEQ(bool, written.readConfigFile(path.c_str()), true);
	for (const std::string &name : written.getNames())
		UASSERT(name.compare(0, 9, "load_mod_") != 0);

	g_settings->setBool("enable_all_mods", false);

	// Without it the world decides again, and it no longer lists the mod
	{
		auto sm = makeManager(m_worlddir);
		std::vector<std::string> names;
		sm.getModNames(names);
		UASSERT(std::find(names.begin(), names.end(), "test_mod") == names.end());
	}

	// Leave the world as the other tests expect it
	world_config.set("load_mod_test_mod", "true");
	UASSERTEQ(bool, world_config.updateConfigFile(path.c_str()), true);
}

void TestServerModManager::testGetModspec()
{
	auto sm = makeManager(m_worlddir);
	UASSERTEQ(const ModSpec *, sm.getModSpec("wrongmod"), NULL);
	UASSERT(sm.getModSpec("base_mod") != NULL);
}

void TestServerModManager::testGetModNamesWrongDir()
{
	auto sm = makeManager(m_worlddir + DIR_DELIM "..");
	std::vector<std::string> result;
	sm.getModNames(result);
	UASSERTEQ(bool, result.empty(), true);
}

void TestServerModManager::testGetModNames()
{
	auto sm = makeManager(m_worlddir);
	std::vector<std::string> result;
	sm.getModNames(result);
	UASSERTEQ(bool, result.empty(), false);
	UASSERT(std::find(result.begin(), result.end(), "base_mod") != result.end());
}

void TestServerModManager::testGetModMediaPathsWrongDir()
{
	auto sm = makeManager(m_worlddir + DIR_DELIM "..");
	std::vector<std::string> result;
	sm.getModsMediaPaths(result);
	UASSERTEQ(bool, result.empty(), true);
}

void TestServerModManager::testGetModMediaPaths()
{
	auto sm = makeManager(m_worlddir);
	std::vector<std::string> result;
	sm.getModsMediaPaths(result);
	UASSERTEQ(bool, result.empty(), false);

	// Test media overriding:
	// dependent_mod depends on base_mod, so its media has to come first in the
	// returned paths to take priority over the media of base_mod
	auto it = std::find(result.begin(), result.end(),
			sm.getModSpec("dependent_mod")->path + DIR_DELIM + "textures");
	UASSERT(it != result.end());
	UASSERT(std::find(++it, result.end(),
			sm.getModSpec("base_mod")->path + DIR_DELIM + "textures") != result.end());
}
