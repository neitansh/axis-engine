// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "server_list.h"

#include "convert_json.h"
#include "filesys.h"
#include "log.h"
#include "network/serverping.h"
#include "porting.h"
#include <atomic>
#include <fstream>
#include <thread>

namespace menu
{

// Замер откликов в стороне: главный поток стоять не имеет права. Ответы
// приходят по номеру строки, а список за это время мог смениться, поэтому
// у замера свой снимок адресов, и результат ложится в строки только если
// список с тех пор не менялся.
struct ServerList::PingJob
{
	struct Target
	{
		std::string address;
		int port;
	};
	std::vector<Target> targets;
	std::vector<ServerPing> results;
	std::vector<char> answered;
	std::atomic<bool> stop{false};
	std::atomic<size_t> finished{0};
	std::vector<std::thread> threads;

	bool done() const { return finished >= targets.size(); }

	// Каждому серверу свой поток: молчащий отвечает только по сроку, и
	// по очереди четыре молчуна держали бы список восемь секунд.
	void start()
	{
		results.resize(targets.size());
		answered.assign(targets.size(), 0);
		for (size_t i = 0; i < targets.size(); i++) {
			threads.emplace_back([this, i]() {
				answered[i] = pingServer(targets[i].address, targets[i].port, 2000, results[i], &stop);
				finished++;
			});
		}
	}

	void join()
	{
		for (std::thread &thread : threads) {
			if (thread.joinable())
				thread.join();
		}
	}
};

ServerList::ServerList(Launcher &launcher) : m_launcher(launcher)
{
}

ServerList::~ServerList()
{
	if (m_ping) {
		m_ping->stop = true;
		m_ping->join();
	}
}

void ServerList::sync()
{
	if (!m_launcher.available()) {
		m_servers.clear();
		m_shown.clear();
		m_loaded = true;
		if (m_on_change)
			m_on_change();
		return;
	}
	// Второй запрос дал бы второй ответ, и какой из них ляжет последним,
	// решала бы сеть.
	if (m_in_flight)
		return;
	m_in_flight = true;

	m_launcher.servers([this](const Launcher::Servers &answer) {
		m_in_flight = false;
		if (!answer.ok) {
			// Молчание лаунчера не повод стирать список: он мог быть верным.
			warningstream << "ServerList: the launcher gave no list" << std::endl;
			if (m_on_change)
				m_on_change();
			return;
		}
		m_entry = answer.entry;
		m_usable.clear();
		if (answer.usable.isArray()) {
			for (const Json::Value &region : answer.usable)
				m_usable.insert(region.asString());
		}
		unfold(answer.servers);
		m_loaded = true;
		m_measured = false;
		measure();
		if (m_on_change)
			m_on_change();
	});
}

void ServerList::unfold(const Json::Value &servers)
{
	m_servers.clear();
	m_shown.clear();
	for (const Json::Value &server : servers) {
		if (!server.isObject() || !server["entrances"].isArray())
			continue;
		for (const Json::Value &entrance : server["entrances"]) {
			// Запись без входа никуда не ведёт. Реестр такого не выписывает,
			// но список приходит снаружи, и верить ему на слово незачем.
			if (!entrance.isObject() || !entrance["address"].isString()
					|| !entrance["port"].isNumeric())
				continue;
			ServerEntry row;
			row.id = server.get("id", "").asString();
			row.server_name = server.get("name", "").asString();
			row.name = row.server_name + " · " + entrance.get("name", "").asString();
			row.address = entrance["address"].asString();
			row.port = entrance["port"].asInt();
			row.dispatch = entrance.get("dispatch", "").asString();
			row.region = entrance.get("region", "").asString();
			row.probe_port = entrance.get("probe_port", 0).asInt();
			row.listed = server.get("listed", false).asBool();
			row.role = server.get("role", "server").asString();
			row.official = server.get("official", false).asBool();
			m_servers.push_back(row);
		}
	}
	// Лобби подбора матчей в список не попадает: строка звала бы игрока
	// туда, где играть не во что, — играют на арене, а адрес её выдаёт
	// Диспетчер.
	for (const ServerEntry &server : m_servers) {
		if (server.listed && server.role != "matches")
			m_shown.push_back(server);
	}
}

void ServerList::measure()
{
	if (m_ping && !m_ping->done())
		return;
	if (m_ping)
		m_ping->join();
	m_ping = std::make_shared<PingJob>();
	for (const ServerEntry &server : m_shown)
		m_ping->targets.push_back({server.address, server.probe_port > 0 ? server.probe_port : server.port});
	if (m_ping->targets.empty()) {
		m_ping.reset();
		return;
	}
	m_ping->start();
}

bool ServerList::poll()
{
	if (!m_ping || !m_ping->done())
		return false;
	m_ping->join();
	std::shared_ptr<PingJob> job = std::move(m_ping);
	m_ping.reset();

	bool same = job->targets.size() == m_shown.size();
	for (size_t i = 0; same && i < m_shown.size(); i++) {
		same = job->targets[i].address == m_shown[i].address
				&& job->targets[i].port == (m_shown[i].probe_port > 0 ? m_shown[i].probe_port : m_shown[i].port);
	}
	if (!same)
		return false;
	m_measured = true;
	for (size_t i = 0; i < m_shown.size(); i++) {
		if (!job->answered[i])
			continue;
		m_shown[i].ping_ms = (int)job->results[i].ping_ms;
		m_shown[i].clients = job->results[i].clients;
		m_shown[i].clients_max = job->results[i].clients_max;
	}
	return true;
}

bool ServerList::usable(const ServerEntry &server) const
{
	// Пока замер не приехал, годно всё: молчать в эту секунду — значит
	// соврать, что входов нет.
	return m_usable.empty() || m_usable.count(server.region) > 0;
}

const ServerEntry *ServerList::find(const std::string &address, int port) const
{
	for (const ServerEntry &server : m_servers) {
		if (server.address == address && server.port == port)
			return &server;
	}
	return nullptr;
}

std::string ServerList::idOf(const std::string &address, int port) const
{
	const ServerEntry *server = find(address, port);
	return server ? server->id : "";
}

// Имя сервера из списка. Не нашлось — пусто: лучше общее «на сервере», чем
// адрес в чужом профиле.
std::string ServerList::nameOf(const std::string &id, const std::string &address, int port) const
{
	for (const ServerEntry &server : m_servers) {
		if ((!id.empty() && server.id == id) || (server.address == address && server.port == port))
			return server.server_name;
	}
	return "";
}

std::string ServerList::favoritesPath() const
{
	return porting::path_user + DIR_DELIM "client" DIR_DELIM "serverlist" DIR_DELIM
			"favorites.json";
}

void ServerList::loadFavorites() const
{
	if (m_favorites_loaded)
		return;
	m_favorites_loaded = true;
	std::ifstream in(favoritesPath());
	if (!in.good())
		return;
	Json::Value list;
	Json::CharReaderBuilder builder;
	std::string errors;
	if (!Json::parseFromStream(builder, in, &list, &errors) || !list.isArray())
		return;
	// Файл правит кто угодно, поэтому берётся только то, что имеет смысл.
	for (const Json::Value &id : list) {
		if (id.isString() && !id.asString().empty())
			m_favorites.insert(id.asString());
	}
}

void ServerList::saveFavorites() const
{
	Json::Value list(Json::arrayValue);
	for (const std::string &id : m_favorites)
		list.append(id);
	const std::string path = favoritesPath();
	fs::CreateAllDirs(fs::RemoveLastPathComponent(path));
	fs::safeWriteToFile(path, fastWriteJson(list));
}

bool ServerList::isFavorite(const std::string &id) const
{
	loadFavorites();
	return !id.empty() && m_favorites.count(id) > 0;
}

void ServerList::setFavorite(const std::string &id, bool on)
{
	loadFavorites();
	if (id.empty())
		return;
	if (on)
		m_favorites.insert(id);
	else
		m_favorites.erase(id);
	saveFavorites();
}

}
