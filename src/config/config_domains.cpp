// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "config_domains.h"
#include "settings_domain_table.h"

#include <algorithm>
#include <cassert>

namespace
{

const std::vector<ConfigDomainSpec> DOMAIN_SPECS = {
	{ConfigDomain::SharedEngine, ConfigSection::Shared, "shared/engine.conf",
			"Engine behaviour both the client and the server rely on."},
	{ConfigDomain::SharedLogging, ConfigSection::Shared, "shared/logging.conf",
			"Logging, profiling and developer switches."},

	{ConfigDomain::ClientGraphics, ConfigSection::Client, "client/graphics.conf",
			"Rendering: window, view distance, shaders, effects."},
	{ConfigDomain::ClientAudio, ConfigSection::Client, "client/audio.conf",
			"Sound output and volumes."},
	{ConfigDomain::ClientInput, ConfigSection::Client, "client/input.conf",
			"Mouse, touchscreen and gamepad behaviour."},
	{ConfigDomain::ClientKeybindings, ConfigSection::Client, "client/keybindings.conf",
			"What every key does. One line per binding."},
	{ConfigDomain::ClientInterface, ConfigSection::Client, "client/interface.conf",
			"Menus, HUD, chat window and fonts."},
	{ConfigDomain::ClientNetwork, ConfigSection::Client, "client/network.conf",
			"How the client talks to servers."},
	{ConfigDomain::ClientSession, ConfigSection::Client, "client/session.conf",
			"State the client remembers between runs, not settings to edit by hand."},
	{ConfigDomain::ClientCustom, ConfigSection::Client, "client/custom.conf",
			"Client settings of mods and texture packs, unknown to the engine."},

	{ConfigDomain::ServerServer, ConfigSection::Server, "server/server.conf",
			"Identity of the server: name, world, message of the day, admin."},
	{ConfigDomain::ServerNetwork, ConfigSection::Server, "server/network.conf",
			"Ports, addresses and how much the server sends to a client."},
	{ConfigDomain::ServerGameplay, ConfigSection::Server, "server/gameplay.conf",
			"Rules of play: damage, physics, privileges."},
	{ConfigDomain::ServerSecurity, ConfigSection::Server, "server/security.conf",
			"Mod security, anticheat and what clients are allowed to do."},
	{ConfigDomain::ServerPerformance, ConfigSection::Server, "server/performance.conf",
			"Load of the server: block sending, emerge threads, map storage."},
	{ConfigDomain::ServerWorldgen, ConfigSection::Server, "server/worldgen.conf",
			"Map generation. Applied to worlds at creation time."},
	{ConfigDomain::ServerCustom, ConfigSection::Server, "server/custom.conf",
			"Server settings of games and mods, unknown to the engine."},
};

} // namespace

const ConfigDomainSpec &getConfigDomainSpec(ConfigDomain domain)
{
	assert(domain != ConfigDomain::Count);

	for (const ConfigDomainSpec &spec : DOMAIN_SPECS) {
		if (spec.domain == domain)
			return spec;
	}

	// The list above covers the enum, so this cannot be reached
	assert(false);
	return DOMAIN_SPECS[0];
}

const std::vector<ConfigDomainSpec> &getConfigDomainSpecs()
{
	return DOMAIN_SPECS;
}

ConfigDomain findSettingDomain(const std::string &name)
{
	constexpr auto begin = std::begin(SETTING_DOMAIN_TABLE);
	constexpr auto end = std::end(SETTING_DOMAIN_TABLE);

	auto it = std::lower_bound(begin, end, name,
			[](const SettingDomainEntry &entry, const std::string &wanted) {
				return wanted.compare(entry.name) > 0;
			});

	if (it != end && name == it->name)
		return it->domain;

	return ConfigDomain::Count;
}

ConfigDomain getCustomDomain(ConfigSection section)
{
	return section == ConfigSection::Server ? ConfigDomain::ServerCustom
			: ConfigDomain::ClientCustom;
}

ConfigSection getConfigSection(ConfigDomain domain)
{
	return getConfigDomainSpec(domain).section;
}
