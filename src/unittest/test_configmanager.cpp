// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test.h"

#include "config/config_manager.h"
#include "filesys.h"
#include "settings.h"

#include <fstream>
#include <set>

class TestConfigManager : public TestBase
{
public:
	TestConfigManager() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestConfigManager"; }

	void runTests(IGameDef *gamedef);

	void testDomainLookup();
	void testLayoutOnLoad();
	void testSplitsBySubject();
	void testRoundTrip();
	void testServerIgnoresClientFiles();
	void testUnknownSettingsStayPut();
	void testMisplacedSettingMoves();

	std::string makeDir(const char *name);
	static std::string readFile(const std::string &path);
	static void writeFile(const std::string &path, const std::string &text);
};

static TestConfigManager g_test_instance;

void TestConfigManager::runTests(IGameDef *gamedef)
{
	TEST(testDomainLookup);
	TEST(testLayoutOnLoad);
	TEST(testSplitsBySubject);
	TEST(testRoundTrip);
	TEST(testServerIgnoresClientFiles);
	TEST(testUnknownSettingsStayPut);
	TEST(testMisplacedSettingMoves);
}

////////////////////////////////////////////////////////////////////////////////

std::string TestConfigManager::makeDir(const char *name)
{
	std::string path = getTestTempDirectory() + DIR_DELIM + name;
	fs::RecursiveDelete(path);
	return path;
}

std::string TestConfigManager::readFile(const std::string &path)
{
	std::ifstream is(path);
	if (!is.good())
		return "";
	return std::string(std::istreambuf_iterator<char>(is),
			std::istreambuf_iterator<char>());
}

void TestConfigManager::writeFile(const std::string &path, const std::string &text)
{
	fs::CreateAllDirs(fs::RemoveLastPathComponent(path));
	UASSERT(fs::safeWriteToFile(path, text));
}

void TestConfigManager::testDomainLookup()
{
	// Settings land in the file of their subject, on the side that reads them
	UASSERTEQ(int, (int)findSettingDomain("viewing_range"),
			(int)ConfigDomain::ClientGraphics);
	UASSERTEQ(int, (int)findSettingDomain("keymap_jump"),
			(int)ConfigDomain::ClientKeybindings);
	UASSERTEQ(int, (int)findSettingDomain("sound_volume"),
			(int)ConfigDomain::ClientAudio);
	UASSERTEQ(int, (int)findSettingDomain("max_users"),
			(int)ConfigDomain::ServerServer);
	UASSERTEQ(int, (int)findSettingDomain("mg_name"),
			(int)ConfigDomain::ServerWorldgen);
	UASSERTEQ(int, (int)findSettingDomain("secure.trusted_mods"),
			(int)ConfigDomain::ServerSecurity);
	UASSERTEQ(int, (int)findSettingDomain("debug_log_level"),
			(int)ConfigDomain::SharedLogging);

	// Settings of mods are not ours to place
	UASSERTEQ(int, (int)findSettingDomain("some_mod_setting"),
			(int)ConfigDomain::Count);

	// Every domain has a file of its own
	std::set<std::string> paths;
	for (const ConfigDomainSpec &spec : getConfigDomainSpecs()) {
		UASSERT(spec.path && *spec.path);
		UASSERT(spec.summary && *spec.summary);
		UASSERT(paths.insert(spec.path).second);
	}
	UASSERTEQ(size_t, paths.size(), (size_t)ConfigDomain::Count);
}

void TestConfigManager::testLayoutOnLoad()
{
	// Loading lays the files out, whether or not the directory is already
	// there. A dedicated server relies on this: it does not save on exit.
	const std::string fresh = makeDir("config_layout_fresh");
	const std::string existing = makeDir("config_layout_existing");
	UASSERT(fs::CreateAllDirs(existing));

	{
		Settings settings;
		ConfigManager config(fresh, ConfigSection::Client, &settings);
		config.load();
		UASSERT(config.isFirstRun());
		UASSERT(fs::PathExists(config.getPath(ConfigDomain::ClientGraphics)));
		UASSERT(fs::PathExists(config.getPath(ConfigDomain::ServerServer)));
	}

	Settings settings;
	ConfigManager config(existing, ConfigSection::Server, &settings);
	config.load();

	UASSERT(fs::PathExists(config.getPath(ConfigDomain::ServerServer)));
	UASSERT(fs::PathExists(config.getPath(ConfigDomain::SharedEngine)));
	// Still nothing of the other side
	UASSERT(!fs::PathExists(config.getPath(ConfigDomain::ClientGraphics)));
	UASSERT(!fs::IsDir(existing + DIR_DELIM + "client"));

	// Nothing next to the directory either, the files go inside it
	UASSERT(!fs::PathExists(existing + "client"));
	UASSERT(!fs::PathExists(existing + "server"));
	UASSERT(!fs::PathExists(existing + "shared"));
}

void TestConfigManager::testSplitsBySubject()
{
	const std::string dir = makeDir("config_split");

	Settings settings;
	settings.set("viewing_range", "123");
	settings.set("sound_volume", "0.5");
	settings.set("max_users", "42");
	settings.set("debug_log_level", "verbose");

	ConfigManager config(dir, ConfigSection::Client, &settings);

	// A file sits in the section directory, below the configuration directory
	UASSERTEQ(std::string, config.getPath(ConfigDomain::ClientGraphics),
			dir + DIR_DELIM + "client" + DIR_DELIM + "graphics.conf");
	UASSERTEQ(std::string, config.getPath(ConfigDomain::SharedEngine),
			dir + DIR_DELIM + "shared" + DIR_DELIM + "engine.conf");

	UASSERT(config.save());
	UASSERT(fs::IsDir(dir + DIR_DELIM + "client"));
	UASSERT(fs::IsDir(dir + DIR_DELIM + "server"));
	UASSERT(fs::IsDir(dir + DIR_DELIM + "shared"));

	const std::string graphics = readFile(config.getPath(ConfigDomain::ClientGraphics));
	const std::string audio = readFile(config.getPath(ConfigDomain::ClientAudio));
	const std::string server = readFile(config.getPath(ConfigDomain::ServerServer));
	const std::string logging = readFile(config.getPath(ConfigDomain::SharedLogging));

	UASSERT(graphics.find("viewing_range = 123") != std::string::npos);
	UASSERT(audio.find("sound_volume = 0.5") != std::string::npos);
	UASSERT(server.find("max_users = 42") != std::string::npos);
	UASSERT(logging.find("debug_log_level = verbose") != std::string::npos);

	// A file holds its own subject and nothing else
	UASSERT(graphics.find("sound_volume") == std::string::npos);
	UASSERT(graphics.find("max_users") == std::string::npos);
	UASSERT(server.find("viewing_range") == std::string::npos);

	// Files of untouched domains exist as well, so the layout is discoverable
	UASSERT(!readFile(config.getPath(ConfigDomain::ClientInput)).empty());
}

void TestConfigManager::testRoundTrip()
{
	const std::string dir = makeDir("config_roundtrip");

	{
		Settings settings;
		settings.set("viewing_range", "200");
		settings.set("max_users", "7");

		ConfigManager config(dir, ConfigSection::Client, &settings);
		UASSERT(config.save());
	}

	Settings settings;
	ConfigManager config(dir, ConfigSection::Client, &settings);
	config.load();

	UASSERT(!config.isFirstRun());
	UASSERTEQ(std::string, settings.get("viewing_range"), "200");
	UASSERTEQ(std::string, settings.get("max_users"), "7");
}

void TestConfigManager::testServerIgnoresClientFiles()
{
	const std::string dir = makeDir("config_sides");

	writeFile(dir + DIR_DELIM + "client" + DIR_DELIM + "graphics.conf",
			"viewing_range = 300\n");
	writeFile(dir + DIR_DELIM + "server" + DIR_DELIM + "server.conf",
			"max_users = 9\n");

	Settings settings;
	ConfigManager config(dir, ConfigSection::Server, &settings);
	config.load();

	UASSERT(settings.exists("max_users"));
	UASSERT(!settings.exists("viewing_range"));

	// Saving must not touch what it does not read
	settings.set("max_users", "10");
	UASSERT(config.save());
	UASSERT(readFile(dir + DIR_DELIM + "client" + DIR_DELIM + "graphics.conf")
			.find("viewing_range = 300") != std::string::npos);
	UASSERT(!fs::PathExists(config.getPath(ConfigDomain::ClientAudio)));
}

void TestConfigManager::testUnknownSettingsStayPut()
{
	const std::string dir = makeDir("config_unknown");

	writeFile(dir + DIR_DELIM + "server" + DIR_DELIM + "custom.conf",
			"a_mod_setting = 1\n");

	Settings settings;
	ConfigManager config(dir, ConfigSection::Client, &settings);
	config.load();

	// Known from the file it was read from
	UASSERTEQ(int, (int)config.domainOf("a_mod_setting"),
			(int)ConfigDomain::ServerCustom);
	// Never seen before: the side decides
	UASSERTEQ(int, (int)config.domainOf("another_mod_setting"),
			(int)ConfigDomain::ClientCustom);

	settings.set("another_mod_setting", "2");
	UASSERT(config.save());

	UASSERT(readFile(config.getPath(ConfigDomain::ServerCustom))
			.find("a_mod_setting = 1") != std::string::npos);
	UASSERT(readFile(config.getPath(ConfigDomain::ClientCustom))
			.find("another_mod_setting = 2") != std::string::npos);
}

void TestConfigManager::testMisplacedSettingMoves()
{
	const std::string dir = makeDir("config_misplaced");

	// A setting edited into the wrong file still applies...
	writeFile(dir + DIR_DELIM + "server" + DIR_DELIM + "server.conf",
			"viewing_range = 150\n");

	Settings settings;
	ConfigManager config(dir, ConfigSection::Client, &settings);
	config.load();
	UASSERTEQ(std::string, settings.get("viewing_range"), "150");

	// ...and is filed where it belongs on the next write
	UASSERT(config.save());
	UASSERT(readFile(config.getPath(ConfigDomain::ServerServer))
			.find("viewing_range") == std::string::npos);
	UASSERT(readFile(config.getPath(ConfigDomain::ClientGraphics))
			.find("viewing_range = 150") != std::string::npos);
}
