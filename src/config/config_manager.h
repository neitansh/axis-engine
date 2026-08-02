// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "config_domains.h"

#include <string>
#include <unordered_map>

class Settings;

/**
 * Owns the configuration directory and keeps it in sync with a Settings
 * object.
 *
 * The configuration is a directory of files rather than a single one. Each
 * file holds one domain (see config_domains.h), so a value always has one
 * obvious home and files stay small enough to read as a whole:
 *
 *     config/shared/engine.conf
 *     config/client/graphics.conf
 *     config/server/network.conf
 *
 * A dedicated server reads and writes the shared and server sections only; it
 * never opens a client file. A client reads all three, because the server of a
 * single player game runs inside it, but it still writes every value into the
 * file of its own domain. Neither side can therefore leak its settings into
 * the files of the other.
 *
 * Settings the engine does not know - those of games and mods - are remembered
 * with the file they came from and written back there. New ones end up in the
 * "custom" file of the running side.
 */
class ConfigManager
{
public:
	/**
	 * @param dir Configuration directory. Does not have to exist yet.
	 * @param side ConfigSection::Client or ConfigSection::Server
	 * @param settings Object to load into and save from, usually g_settings
	 */
	ConfigManager(const std::string &dir, ConfigSection side, Settings *settings);

	DISABLE_CLASS_COPY(ConfigManager)

	/// Reads every file this side is responsible for
	void load();

	/// Writes every setting into the file of its domain, creating what is missing
	bool save();

	/// Directory the files live in
	const std::string &getDir() const { return m_dir; }

	/// Full path of the file a domain is stored in
	std::string getPath(ConfigDomain domain) const;

	/// True if this side reads and writes the given domain
	bool handles(ConfigDomain domain) const;

	/// File a setting is stored in
	ConfigDomain domainOf(const std::string &name) const;

	/// True if no configuration existed when load() ran
	bool isFirstRun() const { return m_first_run; }

private:
	void loadFile(const ConfigDomainSpec &spec);
	bool writeFile(const ConfigDomainSpec &spec);

	std::string m_dir;
	ConfigSection m_side;
	Settings *m_settings;
	bool m_first_run = false;

	// Domain of settings the engine does not know, by the file they came from
	std::unordered_map<std::string, ConfigDomain> m_origin;
};

/// The configuration of the running process. Set up in main().
extern ConfigManager *g_config;
