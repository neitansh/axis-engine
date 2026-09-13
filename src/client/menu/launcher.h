// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "net.h"
#include <functional>
#include <string>

namespace menu
{

// Дверца лаунчера: билеты, список серверов, имя игрока, приглашения и
// доклады о том, чем игрок занят. Адрес дверцы и ключ запуска приходят
// в командной строке (--ticket-url, --ticket-key); без них клиент запущен
// не лаунчером, и дверцы нет — все вопросы отвечают отказом сразу.
class Launcher
{
public:
	struct Ticket
	{
		std::string ticket;
		std::string login;
		// Пусто — билет выдан. Иначе: no_launcher, silent, refused.
		std::string trouble;
		// Слова лаунчера об отказе, если он их сказал.
		std::string said;
	};

	struct Servers
	{
		bool ok = false;
		Json::Value servers;
		std::string entry;
		Json::Value usable;
	};

	explicit Launcher(Net &net) : m_net(net) {}

	bool available() const;

	void ticket(const std::string &server_id, std::function<void(const Ticket &)> done);
	void servers(std::function<void(const Servers &)> done);
	void login(std::function<void(const std::string &)> done);
	// Куда позвали из Discord; лаунчер отдаёт приглашение один раз.
	void invite(std::function<void(const std::string &room)> done);

	// Чем игрок занят. Показывает это лаунчер в профиле Discord; отсюда
	// уезжают только факты, ни адресов, ни имён миров. Одно и то же дважды
	// не шлётся.
	void inMenu();
	void inQueue(const std::string &server, const std::string &mode,
			const std::string &room, int waiting, int needed);
	void playingMatch(const std::string &server, const std::string &mode);
	void playingServer(const std::string &server);
	void playingSolo();

private:
	bool door(std::string &url, std::string &key) const;
	std::vector<std::string> headers(const std::string &key) const;
	void tell(const Json::Value &what);

	Net &m_net;
	std::string m_said;
};

}
