// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "irrlichttypes.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

class Server;

/**
 * Skins the server hands out to its clients.
 *
 * A ticket says which look a player wears, by the hash of the picture. The
 * picture itself is fetched here — by the server, once, and then given to
 * everyone who sees that player as ordinary media.
 *
 * The client never goes for it itself, and that is the point. A stranger's
 * server would otherwise learn the address of everyone who joins it just by
 * naming our host, which is the same hole the media downloader already
 * refuses to open (see clientmedia.cpp). Here the server pays instead: three
 * kilobytes per player, once, from a service it talks to anyway.
 *
 * What is named by a hash cannot change, so a skin that arrived once is kept
 * on disk and never asked for again.
 */
class SkinCache
{
public:
	/// Media name for this look, or empty while it is not here yet.
	/// Asking starts the fetch; asking again is free.
	std::string want(const std::string &hash);

	/// Pick up answers and hand what arrived to the clients.
	void step(Server *server);

	/**
	 * Ask the account service what the players here are wearing now.
	 *
	 * A ticket says what somebody wore when they connected, and it cannot be
	 * rewritten afterwards — so a player who changes their look in the cabinet
	 * without leaving the game would otherwise keep the old one until they
	 * came back. This is the server asking instead, every so often, about the
	 * people sitting on it.
	 *
	 * Only a server holding a service token can ask. A stranger's server has
	 * none: there the new look arrives with the next join, and that is an
	 * honest difference between our server and a rented one.
	 */
	void stepRefresh(Server *server, float dtime);

	/// Media name a look is published under.
	static std::string mediaName(const std::string &hash);

private:
	struct Pending
	{
		u64 caller;
		u64 started;
	};

	bool publish(Server *server, const std::string &hash, std::string_view data);
	void checkDelivered(Server *server);

	/// Кому раздавать. Ставится на первом же шаге: без сервера медиа не
	/// раздать, а спрашивают облик раньше, чем случается шаг.
	Server *m_owner = nullptr;
	std::unordered_map<std::string, Pending> m_pending;
	std::unordered_set<std::string> m_ready;
	/// Роздано клиентам, но ещё не всеми получено: номер раздачи → облик.
	///
	/// Пока файл в пути, называть его в свойствах игрока нельзя. Клиент,
	/// которому имя пришло раньше файла, подставляет заглушку — ту самую
	/// сиреневую — и второй раз за текстурой не идёт: имя не изменилось,
	/// значит и перерисовывать ему нечего.
	std::unordered_map<u32, std::string> m_awaiting;
	/// Looks we could not get. Kept so a missing skin is asked for once and
	/// not on every step for as long as its owner is playing.
	std::unordered_set<std::string> m_missing;

	/// Сколько осталось до следующего вопроса службе, в секундах.
	float m_ask_in = 0.0f;
	/// Идёт ли вопрос прямо сейчас: второй поверх первого не задаётся.
	bool m_asking = false;
	u64 m_ask_caller = 0;
};
