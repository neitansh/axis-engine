// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "irrlichttypes.h"
#include <atomic>
#include <string>

struct ServerPing
{
	u64 ping_ms = 0;
	// −1 — сервер откликнулся, но сколько на нём народу, не сказал.
	int clients = -1;
	int clients_max = -1;
};

// Стучится к серверу и ждёт отклика не дольше timeout_ms. Блокирует поток:
// звать из рабочего, не из главного. stop — просьба закругляться раньше срока.
bool pingServer(const std::string &address, int port, int timeout_ms,
		ServerPing &out, const std::atomic<bool> *stop = nullptr);
