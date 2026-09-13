// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "launcher.h"

#include "convert_json.h"
#include "settings.h"

namespace menu
{

bool Launcher::door(std::string &url, std::string &key) const
{
	g_settings->getNoEx("axis_ticket_url", url);
	g_settings->getNoEx("axis_ticket_key", key);
	return !url.empty() && !key.empty();
}

bool Launcher::available() const
{
	std::string url, key;
	return door(url, key);
}

std::vector<std::string> Launcher::headers(const std::string &key) const
{
	return {"Authorization: Bearer " + key};
}

void Launcher::ticket(const std::string &server_id, std::function<void(const Ticket &)> done)
{
	std::string url, key;
	Ticket answer;
	if (!door(url, key)) {
		answer.trouble = "no_launcher";
		done(answer);
		return;
	}
	if (server_id.empty()) {
		answer.trouble = "no_server";
		done(answer);
		return;
	}
	Json::Value body;
	body["server"] = server_id;
	m_net.post(url + "/ticket", body, headers(key), 10000,
			[done = std::move(done)](const Net::Answer &res) {
				Ticket answer;
				if (!res.reached) {
					answer.trouble = "silent";
				} else if (res.code != 200 || !res.body.isObject()
						|| res.body.get("ticket", "").asString().empty()) {
					answer.trouble = "refused";
					if (res.body.isObject())
						answer.said = res.body.get("message", "").asString();
				} else {
					answer.ticket = res.body["ticket"].asString();
					answer.login = res.body.get("login", "").asString();
				}
				done(answer);
			});
}

void Launcher::servers(std::function<void(const Servers &)> done)
{
	std::string url, key;
	if (!door(url, key)) {
		done(Servers());
		return;
	}
	m_net.get(url + "/servers", headers(key), 10000,
			[done = std::move(done)](const Net::Answer &res) {
				Servers answer;
				if (res.ok() && res.body.isObject() && res.body["servers"].isArray()) {
					answer.ok = true;
					answer.servers = res.body["servers"];
					answer.entry = res.body.get("entry", "").asString();
					answer.usable = res.body["usable"];
				}
				done(answer);
			});
}

void Launcher::login(std::function<void(const std::string &)> done)
{
	std::string url, key;
	if (!door(url, key))
		return;
	m_net.get(url + "/login", headers(key), 10000,
			[done = std::move(done)](const Net::Answer &res) {
				if (res.ok() && res.body.isObject())
					done(res.body.get("login", "").asString());
			});
}

void Launcher::invite(std::function<void(const std::string &)> done)
{
	std::string url, key;
	if (!door(url, key)) {
		done("");
		return;
	}
	m_net.get(url + "/invite", headers(key), 5000,
			[done = std::move(done)](const Net::Answer &res) {
				done(res.ok() && res.body.isObject() ? res.body.get("room", "").asString() : "");
			});
}

void Launcher::tell(const Json::Value &what)
{
	std::string url, key;
	if (!door(url, key))
		return;
	const std::string body = fastWriteJson(what);
	if (body == m_said)
		return;
	m_said = body;
	// Секунда на свою же машину — с запасом. Лаунчер, который не ответил и
	// за неё, всё равно ничего не покажет.
	m_net.fire(url + "/presence", what, headers(key), 1000);
}

void Launcher::inMenu()
{
	Json::Value what;
	what["where"] = "menu";
	tell(what);
}

void Launcher::inQueue(const std::string &server, const std::string &mode,
		const std::string &room, int waiting, int needed)
{
	Json::Value what;
	what["where"] = "queue";
	what["server"] = server;
	what["mode"] = mode;
	what["room"] = room;
	what["waiting"] = waiting;
	what["needed"] = needed;
	tell(what);
}

void Launcher::playingMatch(const std::string &server, const std::string &mode)
{
	Json::Value what;
	what["where"] = "match";
	what["server"] = server;
	what["mode"] = mode;
	tell(what);
}

void Launcher::playingServer(const std::string &server)
{
	Json::Value what;
	what["where"] = "server";
	what["server"] = server;
	tell(what);
}

void Launcher::playingSolo()
{
	Json::Value what;
	what["where"] = "solo";
	tell(what);
}

}
