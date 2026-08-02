// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "config_manager.h"

#include "debug.h"
#include "filesys.h"
#include "log.h"
#include "settings.h"

#include <sstream>

ConfigManager *g_config = nullptr;

namespace
{

/// Turns "client/graphics.conf" into a path of this platform, below dir
std::string toNativePath(const std::string &dir, const char *relative)
{
	std::string path = dir;
	path += DIR_DELIM;

	for (const char *c = relative; *c; c++)
		path += (*c == '/') ? DIR_DELIM[0] : *c;

	return path;
}

/// Header of a file that is created from scratch
std::string fileHeader(const ConfigDomainSpec &spec)
{
	std::ostringstream os;
	os << "# " << spec.summary << "\n"
		<< "#\n"
		<< "# Settings written here override the built-in defaults. Lines the\n"
		<< "# engine does not recognise are kept as they are.\n"
		<< "\n";
	return os.str();
}

} // namespace

ConfigManager::ConfigManager(const std::string &dir, ConfigSection side,
		Settings *settings) :
	m_dir(dir),
	m_side(side),
	m_settings(settings)
{
	sanity_check(m_settings);
}

std::string ConfigManager::getPath(ConfigDomain domain) const
{
	return toNativePath(m_dir, getConfigDomainSpec(domain).path);
}

bool ConfigManager::handles(ConfigDomain domain) const
{
	const ConfigSection section = getConfigSection(domain);

	if (section == ConfigSection::Shared)
		return true;

	// The client hosts the server of a single player game, so it needs the
	// server section as well. A dedicated server has no use for client files.
	if (m_side == ConfigSection::Client)
		return true;

	return section == ConfigSection::Server;
}

ConfigDomain ConfigManager::domainOf(const std::string &name) const
{
	const ConfigDomain known = findSettingDomain(name);
	if (known != ConfigDomain::Count)
		return known;

	// Not ours: a game or mod setting. Leave it where it was found.
	const auto it = m_origin.find(name);
	if (it != m_origin.end())
		return it->second;

	return getCustomDomain(m_side);
}

void ConfigManager::load()
{
	m_first_run = !fs::IsDir(m_dir);

	for (const ConfigDomainSpec &spec : getConfigDomainSpecs()) {
		if (handles(spec.domain))
			loadFile(spec);
	}

	infostream << "Configuration directory: " << m_dir
		<< (m_first_run ? " (first run)" : "") << std::endl;

	// Lay out what is not there yet. A file that exists and says what belongs
	// in it is what makes the configuration editable without a manual, and a
	// server would otherwise never write one: it only saves when a mod asks.
	createMissingFiles();
}

void ConfigManager::createMissingFiles()
{
	if (!fs::CreateAllDirs(m_dir)) {
		warningstream << "Could not create the configuration directory "
			<< m_dir << std::endl;
		return;
	}

	for (const ConfigDomainSpec &spec : getConfigDomainSpecs()) {
		if (handles(spec.domain))
			createFile(spec);
	}
}

bool ConfigManager::createFile(const ConfigDomainSpec &spec)
{
	const std::string path = toNativePath(m_dir, spec.path);

	if (fs::PathExists(path))
		return true;

	const std::string dir = fs::RemoveLastPathComponent(path);
	if (!dir.empty() && !fs::CreateAllDirs(dir)) {
		warningstream << "Could not create " << dir << std::endl;
		return false;
	}

	if (!fs::safeWriteToFile(path, fileHeader(spec))) {
		warningstream << "Could not create " << path << std::endl;
		return false;
	}

	return true;
}

void ConfigManager::loadFile(const ConfigDomainSpec &spec)
{
	const std::string path = toNativePath(m_dir, spec.path);

	Settings file;
	if (!file.readConfigFile(path.c_str()))
		return;

	for (const std::string &name : file.getNames()) {
		Settings *group = nullptr;
		std::string value;

		if (file.getGroupNoEx(name, group)) {
			m_settings->setGroup(name, *group);
		} else if (file.getNoEx(name, value)) {
			m_settings->set(name, value);
		} else {
			continue;
		}

		// Remember where settings of games and mods live, so they are written
		// back into the same file instead of piling up in custom.conf
		if (findSettingDomain(name) == ConfigDomain::Count)
			m_origin[name] = spec.domain;
	}

	verbosestream << "Read configuration from " << path << std::endl;
}

bool ConfigManager::save()
{
	if (!fs::CreateAllDirs(m_dir)) {
		errorstream << "Could not create the configuration directory "
			<< m_dir << std::endl;
		return false;
	}

	bool success = true;

	for (const ConfigDomainSpec &spec : getConfigDomainSpecs()) {
		if (handles(spec.domain))
			success &= writeFile(spec);
	}

	return success;
}

bool ConfigManager::writeFile(const ConfigDomainSpec &spec)
{
	const std::string path = toNativePath(m_dir, spec.path);

	if (!createFile(spec))
		return false;

	const auto belongs_here = [&](const std::string &name) {
		return domainOf(name) == spec.domain;
	};

	if (!m_settings->updateConfigFile(path.c_str(), belongs_here)) {
		errorstream << "Could not write " << path << std::endl;
		return false;
	}

	return true;
}
