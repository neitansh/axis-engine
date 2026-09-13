// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "httpfetch.h"
#include <functional>
#include <json/json.h>
#include <string>
#include <vector>

namespace menu
{

// HTTP из меню. Главный поток стоять не имеет права — меню перестанет
// рисоваться, — поэтому запрос уходит качалке движка, а ответ приходит
// обратным вызовом из poll() на следующем кадре.
class Net
{
public:
	struct Answer
	{
		bool reached = false;
		long code = 0;
		Json::Value body;
		bool ok() const { return reached && code == 200; }
	};
	using Callback = std::function<void(const Answer &)>;

	Net();
	~Net();

	Net(const Net &) = delete;
	Net &operator=(const Net &) = delete;

	void get(const std::string &url, const std::vector<std::string> &headers,
			long timeout_ms, Callback callback);
	void post(const std::string &url, const Json::Value &body,
			const std::vector<std::string> &headers, long timeout_ms, Callback callback);
	// Ответ не читается: отправить и забыть.
	void fire(const std::string &url, const Json::Value &body,
			const std::vector<std::string> &headers, long timeout_ms);

	void poll();

private:
	struct Pending
	{
		u64 id;
		Callback callback;
	};

	void send(HTTPFetchRequest &request, Callback callback);

	u64 m_caller;
	u64 m_next_id = 1;
	std::vector<Pending> m_pending;
};

}
