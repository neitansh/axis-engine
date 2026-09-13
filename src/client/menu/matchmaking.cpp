// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "matchmaking.h"

#include "gettext.h"
#include "settings.h"

namespace menu
{

Matchmaking::Matchmaking(Net &net, Launcher &launcher, ServerList &servers) :
	m_net(net),
	m_launcher(launcher),
	m_servers(servers)
{
}

void Matchmaking::changed()
{
	if (m_on_change)
		m_on_change();
}

// Вход не спрашивают у игрока: какой узел ближе и через какой дойдёт до
// матча — вопрос сети, а не вкуса. Меряет лаунчер при запуске и присылает
// свой выбор вместе со списком; здесь он только берётся. Пока ждём набора,
// вход не меняется: очередь стоит у него, и переезд — это выход из очереди.
void Matchmaking::pickEntry()
{
	if (m_queue)
		return;
	const std::vector<ServerEntry> &all = m_servers.all();
	int first = -1;
	for (size_t i = 0; i < all.size(); i++) {
		const ServerEntry &server = all[i];
		if (server.role != "matches" || server.dispatch.empty() || m_bad.count(server.dispatch)
				|| !m_servers.usable(server))
			continue;
		if (first < 0)
			first = (int)i;
		if (!m_servers.entry().empty() && server.region == m_servers.entry()) {
			m_region = (int)i;
			return;
		}
	}
	// Лаунчер не назвал вход или названный подвёл — берём первый годный.
	m_region = first;
}

// Настройка сильнее списка: заполняют её те, кто поднял своего Диспетчера
// или проверяет его на стенде, — и им нужен именно он.
std::string Matchmaking::ownDispatch() const
{
	std::string own;
	g_settings->getNoEx("matchmaking_url", own);
	return own;
}

bool Matchmaking::decided() const
{
	if (!ownDispatch().empty())
		return true;
	// Список приходит от лаунчера не мгновенно; показать «арен нет» в эту
	// секунду значит соврать.
	return m_servers.loaded();
}

const ServerEntry *Matchmaking::entry() const
{
	const std::vector<ServerEntry> &all = m_servers.all();
	if (m_region < 0 || (size_t)m_region >= all.size())
		return nullptr;
	return &all[m_region];
}

std::string Matchmaking::dispatchUrl() const
{
	std::string own = ownDispatch();
	if (!own.empty())
		return own;
	const ServerEntry *server = entry();
	return server ? server->dispatch : "";
}

std::string Matchmaking::region() const
{
	const ServerEntry *server = entry();
	return server ? server->region : "";
}

// На какой сервер выписывать билет: входов два, сервер за ними один.
std::string Matchmaking::serverId() const
{
	const ServerEntry *server = entry();
	return server ? server->id : "";
}

void Matchmaking::request(const std::string &path, const Json::Value *body,
		std::function<void(const Net::Answer &)> done)
{
	const std::string url = dispatchUrl();
	if (url.empty()) {
		done(Net::Answer());
		return;
	}
	if (body)
		m_net.post(url + path, *body, {}, 20000, std::move(done));
	else
		m_net.get(url + path, {}, 20000, std::move(done));
}

const Json::Value *Matchmaking::decode(const Net::Answer &res, std::string &why)
{
	if (!res.reached) {
		why = "unreachable";
		return nullptr;
	}
	// «Тебя тут нет» — обычный ответ, а не поломка: очередь могла
	// разойтись, пока мы отворачивались.
	if (res.code == 404) {
		why = "gone";
		return nullptr;
	}
	if (res.code != 200 || !res.body.isObject()) {
		why = "unreachable";
		return nullptr;
	}
	return &res.body;
}

// Список арен спрашивается снова и снова, пока экран на виду: на плитках
// написано, сколько человек уже ждёт, и цифра должна быть живой. Диспетчер
// придерживает повторный ответ на секунду, поэтому круг никого не молотит.
void Matchmaking::refreshModes(bool again)
{
	if (m_asking && !again)
		return;
	m_asking = true;
	const std::string asked = dispatchUrl();
	const int epoch = m_epoch;
	request("/v1/modes" + std::string(again ? "?wait=1" : ""), nullptr,
			[this, asked, epoch](const Net::Answer &res) {
				if (epoch != m_epoch)
					return;
				std::string why;
				const Json::Value *body = decode(res, why);
				if (body) {
					std::vector<Mode> modes;
					for (const Json::Value &mode : (*body)["modes"]) {
						if (!mode.isObject())
							continue;
						Mode entry;
						entry.id = mode.get("id", "").asString();
						entry.title = mode.get("title", entry.id).asString();
						entry.waiting = mode.get("waiting", 0).asInt();
						entry.players = mode.get("players", 0).asInt();
						modes.push_back(entry);
					}
					m_modes = std::move(modes);
					m_modes_status.clear();
					m_answered = true;
				} else {
					// Лаунчер говорил, что сюда дойдёт, а Диспетчер молчит.
					// Больше этот вход не предлагаем и сразу пробуем следующий.
					if (!asked.empty() && !m_queue) {
						m_bad.insert(asked);
						m_answered = false;
						pickEntry();
					}
					if (!dispatchUrl().empty() && dispatchUrl() != asked) {
						m_asking = false;
						refreshModes(false);
						return;
					}
					// Круг обрывается: без ответа его нечем крутить, а без
					// входа отказ приходит тут же, и круг стал бы рекурсией.
					m_modes = std::vector<Mode>();
					m_modes_status = strgettext("Matchmaking is unavailable");
					m_asking = false;
					changed();
					return;
				}
				changed();

				if (m_shown && !m_queue) {
					refreshModes(true);
				} else {
					m_asking = false;
				}
			});
}

void Matchmaking::readQueue(const Json::Value &body)
{
	Queue queue;
	queue.mode = body.get("mode", "").asString();
	queue.title = body.get("title", queue.mode).asString();
	queue.room = body.get("room", "").asString();
	queue.room_state = body.get("room_state", "").asString();
	queue.waiting = body.get("waiting", 0).asInt();
	queue.needed = body.get("needed", 0).asInt();
	queue.starts_in = body.get("starts_in", 0).asInt();
	queue.min = body.get("min", 1).asInt();
	m_queue = queue;
	const ServerEntry *server = entry();
	m_launcher.inQueue(server ? server->server_name : "", queue.title, queue.room,
			queue.waiting, queue.needed);
}

// Диспетчер назвал адрес — дальше обычный вход на сервер, только адрес
// получен, а не набран. true — ушли в игру.
bool Matchmaking::enterMatch(const Json::Value &body)
{
	const std::string address = body.get("address", "").asString();
	const int port = body.get("port", 0).asInt();
	if (address.empty() || port <= 0)
		return false;
	m_queue.reset();
	// Экрана уже нет: бросать игрока в игру мимо его воли нельзя.
	if (!m_shown) {
		stop();
		return true;
	}
	const std::string title = body.get("title", body.get("mode", "").asString()).asString();
	if (m_on_match)
		m_on_match(address, port, serverId(), title);
	return true;
}

// Опрос очереди. Диспетчер придерживает ответ на секунду, поэтому цикл
// идёт сам собой.
void Matchmaking::poll()
{
	if (m_polling || !m_queue || !m_shown)
		return;
	m_polling = true;
	const int epoch = m_epoch;
	Json::Value body;
	body["pass"] = m_pass;
	body["region"] = region();
	request("/v1/queue", &body, [this, epoch](const Net::Answer &res) {
		if (epoch != m_epoch)
			return;
		m_polling = false;
		std::string why;
		const Json::Value *answer = decode(res, why);
		if (!answer) {
			m_queue.reset();
			m_launcher.inMenu();
			// Пропавшая очередь — не беда: за ней никто уже не стоит.
			m_status = why != "gone" ? strgettext("Matchmaking is unavailable") : "";
			changed();
			return;
		}
		readQueue(*answer);
		if (enterMatch(*answer))
			return;
		changed();
		poll();
	});
}

// Диспетчер пускает в очередь по билету, а не по имени: подпись билета
// подделать нельзя. Билет тот же, что и при входе на сервер, только нужен
// раньше. Дальше личность несёт пропуск.
void Matchmaking::ticketForJoin(
		std::function<void(const std::string &, const std::string &)> done)
{
	m_launcher.ticket(serverId(), [done = std::move(done)](const Launcher::Ticket &answer) {
		done(answer.ticket, answer.trouble);
	});
}

void Matchmaking::doJoin(const std::string &mode, const Credential &cred)
{
	const int epoch = m_epoch;
	Json::Value body;
	body["mode"] = mode;
	body["region"] = region();
	body["pass"] = cred.pass;
	body["ticket"] = cred.ticket;
	request("/v1/join", &body, [this, epoch](const Net::Answer &res) {
		if (epoch != m_epoch)
			return;
		std::string why;
		const Json::Value *answer = decode(res, why);
		if (!answer) {
			m_status = strgettext("Matchmaking is unavailable");
			changed();
			return;
		}
		// Пропуск живёт только в памяти меню: на диск ему незачем.
		const std::string pass = answer->get("pass", "").asString();
		if (!pass.empty())
			m_pass = pass;
		readQueue(*answer);
		m_status.clear();
		// Диспетчер мог и не ставить в очередь: если место есть, он сразу
		// называет адрес.
		if (enterMatch(*answer))
			return;
		changed();
		poll();
	});
}

// Позвали из Discord: входим в ту самую комнату, а не просто на её режим.
void Matchmaking::joinRoom(const std::string &room, const Credential &cred)
{
	const int epoch = m_epoch;
	Json::Value body;
	body["room"] = room;
	body["region"] = region();
	body["pass"] = cred.pass;
	body["ticket"] = cred.ticket;
	request("/v1/join", &body, [this, epoch](const Net::Answer &res) {
		if (epoch != m_epoch)
			return;
		std::string why;
		const Json::Value *answer = decode(res, why);
		if (!answer) {
			// Комната уехала или набралась — обычное дело; игрок остаётся на
			// экране арен и встанет в очередь сам.
			m_status = why == "gone"
					? strgettext("The match you were invited to has already started")
					: strgettext("Matchmaking is unavailable");
			changed();
			return;
		}
		const std::string pass = answer->get("pass", "").asString();
		if (!pass.empty())
			m_pass = pass;
		readQueue(*answer);
		m_status.clear();
		if (enterMatch(*answer))
			return;
		changed();
		poll();
	});
}

// Лаунчер отдаёт приглашение один раз, поэтому спрашивается оно, только
// когда есть чем воспользоваться; не сумевшее сработать помнится и
// пробуется снова.
void Matchmaking::followInvite()
{
	if (m_asking_invite || m_entering || serverId().empty())
		return;

	auto go = [this](const std::string &room) {
		m_invite = room;
		m_entering = true;
		if (!m_pass.empty()) {
			m_invite.clear();
			m_entering = false;
			joinRoom(room, {m_pass, ""});
			return;
		}
		const int epoch = m_epoch;
		ticketForJoin([this, room, epoch](const std::string &ticket, const std::string &trouble) {
			m_entering = false;
			if (epoch != m_epoch)
				return;
			if (ticket.empty()) {
				m_status = trouble == "no_launcher"
						? strgettext("Start the game through the launcher to play online")
						: strgettext("Matchmaking is unavailable");
				changed();
				return;
			}
			m_invite.clear();
			joinRoom(room, {"", ticket});
		});
	};

	if (!m_invite.empty()) {
		go(m_invite);
		return;
	}

	// Раз за открытие экрана: лаунчер отдаёт приглашение один раз, и
	// спрашивать его каждый кадр незачем.
	if (m_invite_asked)
		return;
	m_invite_asked = true;
	m_asking_invite = true;
	m_launcher.invite([this, go](const std::string &room) {
		m_asking_invite = false;
		if (!room.empty() && m_shown && !m_queue)
			go(room);
		else if (!room.empty())
			m_invite = room;
	});
}

void Matchmaking::join(const std::string &mode)
{
	if (!decided())
		return;
	m_status.clear();
	changed();
	// Пропуск уже есть — личность несёт он: смена режима не требует нового
	// билета.
	if (!m_pass.empty()) {
		doJoin(mode, {m_pass, ""});
		return;
	}
	const int epoch = m_epoch;
	ticketForJoin([this, mode, epoch](const std::string &ticket, const std::string &trouble) {
		if (epoch != m_epoch)
			return;
		if (ticket.empty()) {
			m_status = trouble == "no_launcher"
					? strgettext("Start the game through the launcher to play online")
					: strgettext("Matchmaking is unavailable");
			changed();
			return;
		}
		doJoin(mode, {"", ticket});
	});
}

void Matchmaking::cancel()
{
	stop();
	refreshModes(false);
	changed();
}

// Ручная попытка прощает всё: если вход отвалился по случайности, второй
// заход по кнопке должен его вернуть.
void Matchmaking::retry()
{
	m_bad.clear();
	m_modes.reset();
	pickEntry();
	refreshModes(false);
	changed();
}

void Matchmaking::invited(const std::string &room)
{
	m_invite = room;
}

// Снять себя с очереди и забыть ответы, которые ещё в пути.
void Matchmaking::stop()
{
	m_epoch++;
	m_asking = false;
	m_polling = false;
	if (m_queue) {
		m_queue.reset();
		m_launcher.inMenu();
		// Снимает с очереди пропуск, а не имя: иначе уйти можно было бы за
		// любого, назвав его ник.
		Json::Value body;
		body["pass"] = m_pass;
		request("/v1/leave", &body, [](const Net::Answer &) {});
		m_pass.clear();
	}
	m_status.clear();
	m_modes_status.clear();
}

void Matchmaking::enter()
{
	m_shown = true;
	m_invite_asked = false;
	m_modes.reset();
	// Замер за прошлый заход мог устареть, а вход, объявленный плохим, —
	// починиться: сеть меняется чаще, чем открывают меню.
	m_bad.clear();
	m_servers.sync();
	pickEntry();
	m_last_dispatch = dispatchUrl();
	if (decided())
		refreshModes(false);
}

void Matchmaking::leave()
{
	m_shown = false;
	stop();
}

void Matchmaking::update()
{
	if (!m_shown)
		return;
	if (!m_queue)
		followInvite();
	// Список приходит от лаунчера позже, чем открывается экран, поэтому вход
	// пересматривается на каждом кадре: как только стало известно, кто
	// ближе, экран сам переедет.
	pickEntry();
	const std::string now = dispatchUrl();
	if (decided() && (!m_modes || now != m_last_dispatch)) {
		if (now != m_last_dispatch) {
			m_modes.reset();
			m_asking = false;
		}
		m_last_dispatch = now;
		refreshModes(false);
	}
}

}
