// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2017 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#pragma once

#include <string>
#include <ctime>

// Что это за сообщение — решает, где и как клиент его покажет. Тип идёт по
// сети вместе с текстом, чтобы клиенту не приходилось угадывать по «<имя> »
// в начале строки.
enum ChatMessageType
{
	// Готовая строка, как есть (старые сервера, отладка).
	CHATMESSAGE_TYPE_RAW = 0,
	// Реплика игрока: sender — кто, message — что; вид даёт клиент.
	CHATMESSAGE_TYPE_NORMAL = 1,
	// Объявление сервера всем: остановка, выгон.
	CHATMESSAGE_TYPE_ANNOUNCE = 2,
	// Слово игры игроку: вошёл, вышел, сообщение мода.
	CHATMESSAGE_TYPE_SYSTEM = 3,
	// Ответ на команду чата: длинный уходит в консоль, а не в чат.
	CHATMESSAGE_TYPE_COMMAND = 4,
	CHATMESSAGE_TYPE_MAX = 5,
};

struct ChatMessage
{
	ChatMessage(const std::wstring &m = L"") : message(m) {}

	ChatMessage(ChatMessageType t, const std::wstring &m, const std::wstring &s = L"",
			std::time_t ts = std::time(0)) :
			type(t),
			message(m), sender(s), timestamp(ts)
	{
	}

	ChatMessageType type = CHATMESSAGE_TYPE_RAW;
	std::wstring message = L"";
	std::wstring sender = L"";
	std::time_t timestamp = std::time(0);
};
