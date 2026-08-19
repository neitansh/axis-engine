// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "irrlichttypes.h"
#include "threading/mutex_auto_lock.h"

/*
	Раздача медиа по HTTP — рядом с игровым портом, тем же сервером.

	Зачем: медиа по игровому протоколу едет одним потоком с подтверждением
	каждой пачки, и первый заход на большой плейс упирается в разгон окна.
	По HTTP клиент берёт файлы десятками соединений сразу и упирается уже в
	канал, а не в протокол.

	Отдаётся ровно две вещи и только по GET: `index.mth` (набор хэшей, какие
	файлы тут есть) и `<sha1 в hex>` — сам файл. Пути от клиента не принимаются
	вовсе: имя запроса — это хэш, по которому ищется заранее известный путь.
	Ничего другого этот сервер отдать не может по устройству, а не по проверке.
*/
class MediaHttpServer
{
public:
	// port == 0 — не поднимать вовсе.
	MediaHttpServer(u16 port);
	~MediaHttpServer();

	// Заработал ли: порт мог быть занят, и это не повод валить весь сервер —
	// медиа тогда поедет по-старому, игровым протоколом.
	bool isRunning() const { return m_running; }
	u16 getPort() const { return m_port; }

	// Что раздаём. Зовётся при старте и всякий раз, когда набор медиа
	// поменялся (динамическая медиа в игре).
	void setMedia(std::unordered_map<std::string, std::string> &&by_hash);

private:
	void threadMain();
	// Разбирает один запрос и отвечает на него. false — закрыть соединение.
	bool serveOnce(int sock);
	std::string findPath(const std::string &sha1_hex) const;
	std::string buildIndex() const;

	const u16 m_port;
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_stop{false};
	std::atomic<int> m_connections{0};
	int m_listen_sock = -1;
	std::thread m_thread;

	mutable std::mutex m_media_mutex;
	std::unordered_map<std::string, std::string> m_by_hash;
	std::string m_index;
};
