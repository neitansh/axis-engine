// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "server_list.h"
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace menu
{

// Подбор матча: выбор арены и ожидание набора. Ждут здесь же, в меню:
// отдельный мир-лобби пришлось бы поднимать и переезжать из него ради
// экрана со счётчиком. Меню ничего не решает само — куда подключаться,
// говорит Диспетчер выбранного входа.
class Matchmaking
{
public:
	struct Mode
	{
		std::string id;
		std::string title;
		int waiting = 0;
		int players = 0;
	};

	struct Queue
	{
		std::string mode;
		std::string title;
		std::string room;
		std::string room_state;
		int waiting = 0;
		int needed = 0;
		int starts_in = 0;
		int min = 1;
	};

	// Диспетчер назвал адрес: дальше обычный вход на сервер.
	using OnMatch = std::function<void(const std::string &address, int port,
			const std::string &server_id, const std::string &title)>;

	Matchmaking(Net &net, Launcher &launcher, ServerList &servers);

	// Экран открыли: список арен мог измениться, пока нас не было.
	void enter();
	// Ушли с экрана — значит и ждать матча перестали.
	void leave();
	// Каждый кадр, пока экран на виду.
	void update();

	void join(const std::string &mode_id);
	void cancel();
	void retry();
	// Приглашение из Discord, забранное до того, как открыли этот экран.
	void invited(const std::string &room);

	bool shown() const { return m_shown; }
	// Известно ли, каким входом идти. Пока нет — арены не показываются.
	bool decided() const;
	// Отозвался ли выбранный вход: до того его имя не пишется.
	bool answered() const { return m_answered; }
	std::string ownDispatch() const;
	const ServerEntry *entry() const;
	// nullopt — список ещё не приехал.
	const std::optional<std::vector<Mode>> &modes() const { return m_modes; }
	const std::optional<Queue> &queue() const { return m_queue; }
	// Что не так: с последним нажатием или, если оно прошло, со списком арен.
	const std::string &status() const { return m_status.empty() ? m_modes_status : m_status; }

	void setOnChange(std::function<void()> on_change) { m_on_change = std::move(on_change); }
	void setOnMatch(OnMatch on_match) { m_on_match = std::move(on_match); }

private:
	struct Credential
	{
		std::string pass;
		std::string ticket;
	};

	void pickEntry();
	std::string dispatchUrl() const;
	std::string region() const;
	std::string serverId() const;
	void request(const std::string &path, const Json::Value *body,
			std::function<void(const Net::Answer &)> done);
	// nullptr — отказ; why: unreachable или gone.
	static const Json::Value *decode(const Net::Answer &res, std::string &why);
	void refreshModes(bool again);
	bool enterMatch(const Json::Value &body);
	void poll();
	void ticketForJoin(std::function<void(const std::string &ticket, const std::string &trouble)> done);
	void doJoin(const std::string &mode, const Credential &cred);
	void joinRoom(const std::string &room, const Credential &cred);
	void followInvite();
	void stop();
	void changed();
	void readQueue(const Json::Value &body);

	Net &m_net;
	Launcher &m_launcher;
	ServerList &m_servers;

	bool m_shown = false;
	int m_region = -1;
	std::optional<std::vector<Mode>> m_modes;
	std::optional<Queue> m_queue;
	std::string m_pass;
	std::string m_status;
	std::string m_modes_status;
	bool m_asking = false;
	bool m_polling = false;
	// Ответы прошлых заходов нам не нужны.
	int m_epoch = 0;
	// Входы, чей Диспетчер не отозвался.
	std::set<std::string> m_bad;
	bool m_answered = false;
	std::string m_invite;
	bool m_asking_invite = false;
	bool m_invite_asked = false;
	bool m_entering = false;
	std::string m_last_dispatch;

	std::function<void()> m_on_change;
	OnMatch m_on_match;
};

}
