// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "util/basic_macros.h"

#include <string>
#include <vector>

/**
 * The configuration is split across files, one per domain. A domain is a
 * subject area ("what is this about") inside a section ("who reads this"),
 * which is what the file layout mirrors:
 *
 *     config/shared/engine.conf
 *     config/client/graphics.conf
 *     config/server/network.conf
 *
 * Every setting the engine knows belongs to exactly one domain. The mapping
 * lives in settings_domain_table.h, generated from builtin/settingtypes.txt by
 * util/generate_settings_domains.py.
 */
enum class ConfigDomain : unsigned char {
	SharedEngine,
	SharedLogging,
	ClientGraphics,
	ClientAudio,
	ClientInput,
	ClientKeybindings,
	ClientInterface,
	ClientNetwork,
	ClientSession,
	ClientCustom,
	ServerServer,
	ServerNetwork,
	ServerGameplay,
	ServerSecurity,
	ServerPerformance,
	ServerWorldgen,
	ServerCustom,

	Count
};

/**
 * Who reads a domain. A dedicated server never opens a client file; a client
 * reads the server section as well, because it hosts the server of a single
 * player game.
 */
enum class ConfigSection : unsigned char {
	Shared,
	Client,
	Server,
};

struct ConfigDomainSpec {
	ConfigDomain domain;
	ConfigSection section;
	/// Path relative to the configuration directory, e.g. "client/audio.conf"
	const char *path;
	/// Written as a comment into the file when it is first created
	const char *summary;
};

/// Entry of the generated name -> domain table
struct SettingDomainEntry {
	const char *name;
	ConfigDomain domain;
};

/// Description of a single domain, i.e. of a single configuration file
const ConfigDomainSpec &getConfigDomainSpec(ConfigDomain domain);

/// All domains, in file layout order
const std::vector<ConfigDomainSpec> &getConfigDomainSpecs();

/**
 * Domain a setting belongs to.
 *
 * @param name Setting name
 * @return The domain, or ConfigDomain::Count if the setting is unknown to the
 *         engine, which is the case for settings of games and mods.
 */
ConfigDomain findSettingDomain(const std::string &name);

/// Fallback domain for settings the engine does not know
ConfigDomain getCustomDomain(ConfigSection section);

/// Section a domain belongs to, i.e. the directory its file lives in
ConfigSection getConfigSection(ConfigDomain domain);
