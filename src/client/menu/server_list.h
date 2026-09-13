// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "launcher.h"
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace menu
{

// Один вход — одна дорога к серверу. В реестре запись — это сервер, а
// входов у него несколько: Россия и Европа — две дороги к одному
// salvo-official, а не два сервера; id у строк одного сервера общий, билет
// выписывается на сервер и годен любой дорогой.
struct ServerEntry
{
	std::string id;
	std::string name;
	std::string server_name;
	std::string address;
	int port = 0;
	// Диспетчер этого входа: у обычного сервера его нет.
	std::string dispatch;
	std::string region;
	int probe_port = 0;
	bool listed = false;
	// matches — лобби подбора матчей, server — обычный игровой.
	std::string role;
	bool official = false;
	// −1 — ещё не мерили или не откликнулся.
	int ping_ms = -1;
	int clients = -1;
	int clients_max = -1;
};

// Список серверов из реестра. Приходит через лаунчер: до реестра надо дойти
// тем входом, который лаунчер выбрал замером, и адрес реестра клиенту
// неоткуда взять. Без лаунчера список пуст, и это не поломка: билета без
// него тоже не будет.
class ServerList
{
public:
	explicit ServerList(Launcher &launcher);
	~ServerList();

	ServerList(const ServerList &) = delete;
	ServerList &operator=(const ServerList &) = delete;

	void sync();
	// Пришли ли замеры откликов; true — строки изменились.
	bool poll();
	// Замер по нынешнему списку уже прошёл.
	bool measured() const { return m_measured; }

	// nullptr — список ещё не спрашивали или он не приехал.
	bool loaded() const { return m_loaded; }
	const std::vector<ServerEntry> &all() const { return m_servers; }
	// Строки вкладки серверов: лобби подбора матчей сюда не попадает.
	const std::vector<ServerEntry> &shown() const { return m_shown; }
	// Каким входом ходит лаунчер и какие входы годны по его замеру.
	const std::string &entry() const { return m_entry; }
	bool usable(const ServerEntry &server) const;

	const ServerEntry *find(const std::string &address, int port) const;
	std::string idOf(const std::string &address, int port) const;
	std::string nameOf(const std::string &id, const std::string &address, int port) const;

	// Любимые — по id сервера, не по входу. Наши сервера не отмечаются:
	// они и так наверху отдельным разделом.
	bool isFavorite(const std::string &id) const;
	void setFavorite(const std::string &id, bool on);

	void setOnChange(std::function<void()> on_change) { m_on_change = std::move(on_change); }

private:
	struct PingJob;

	void unfold(const Json::Value &servers);
	void measure();
	void loadFavorites() const;
	void saveFavorites() const;
	std::string favoritesPath() const;

	Launcher &m_launcher;
	bool m_loaded = false;
	bool m_measured = false;
	bool m_in_flight = false;
	std::vector<ServerEntry> m_servers;
	std::vector<ServerEntry> m_shown;
	std::string m_entry;
	std::set<std::string> m_usable;
	mutable std::set<std::string> m_favorites;
	mutable bool m_favorites_loaded = false;
	std::shared_ptr<PingJob> m_ping;
	std::function<void()> m_on_change;
};

}
