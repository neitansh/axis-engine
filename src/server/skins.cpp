// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "skins.h"

#include "filesys.h"
#include "httpfetch.h"
#include "log.h"
#include "porting.h"
#include "convert_json.h"
#include "server.h"
#include "server/clientiface.h"
#include "server/player_sao.h"
#include "remoteplayer.h"
#include "serverenvironment.h"
#include "settings.h"
#include "util/hashing.h"
#include "util/hex.h"

#include <json/json.h>
#include <memory>
#include "util/string.h"

namespace
{

/// Where a fetched skin is kept between runs. Content named by its hash never
/// changes, so this cache has no way of going stale and nothing to invalidate.
std::string cachePath(const std::string &hash)
{
	return porting::path_cache + DIR_DELIM "skins" DIR_DELIM + hash + ".png";
}

std::string hexDigest(std::string_view data)
{
	return hex_encode(hashing::sha256(data));
}

} // namespace

std::string SkinCache::mediaName(const std::string &hash)
{
	return "axis_skin_" + hash + ".png";
}

std::string SkinCache::want(const std::string &hash)
{
	if (hash.empty() || m_missing.count(hash))
		return "";
	if (m_ready.count(hash))
		return mediaName(hash);
	if (m_pending.count(hash))
		return "";
	// Уже роздан и едет к клиентам: второй раз слать тот же файл незачем.
	for (const auto &[token, sent] : m_awaiting) {
		(void)token;
		if (sent == hash)
			return "";
	}

	// Уже приезжал когда-то: содержимое по хэшу неизменяемо, и спрашивать его
	// второй раз незачем — ни у службы, ни после перезапуска сервера.
	{
		std::string kept;
		if (fs::ReadFile(cachePath(hash), kept, true) && hexDigest(kept) == hash) {
			// Не «готово»: файл с диска доезжает до клиентов так же, как
			// скачанный, и назвать его можно только после доставки.
			if (m_owner)
				publish(m_owner, hash, kept);
			return "";
		}
	}

	const std::string url = g_settings->get("auth_url");
	if (url.empty()) {
		// Nobody to ask. A server without the account service still gets the
		// hash out of the ticket, and there is nothing wrong with the ticket
		// — it simply has no way to turn it into a picture.
		m_missing.insert(hash);
		return "";
	}

	HTTPFetchRequest req;
	req.caller = httpfetch_caller_alloc();
	req.url = url + "/v1/skins/" + hash + ".png";
	req.connect_timeout = 3000;
	req.timeout = 8000;
	httpfetch_async(req);
	m_pending[hash] = Pending{req.caller, porting::getTimeMs()};
	return "";
}

bool SkinCache::publish(Server *server, const std::string &hash, std::string_view data)
{
	const u32 token = server->allocateEngineMediaToken();

	Server::DynamicMediaArgs args;
	args.filename = mediaName(hash);
	args.data = data;
	args.token = token;
	if (!server->dynamicAddMedia(args))
		return false;

	// Готовым облик не считается, пока файл в пути: до тех пор его носитель
	// ходит в том, что везёт движок, и это правильнее сиреневой заглушки.
	m_awaiting[token] = hash;
	return true;
}

void SkinCache::checkDelivered(Server *server)
{
	for (auto it = m_awaiting.begin(); it != m_awaiting.end();) {
		if (server->mediaPending(it->first)) {
			++it;
			continue;
		}
		const std::string hash = it->second;
		m_ready.insert(hash);
		it = m_awaiting.erase(it);
		// Теперь имя можно назвать: у всех, кто видит носителя, файл есть.
		server->refreshSkin(hash);
	}
}

void SkinCache::step(Server *server)
{
	// Дисковый кэш отдаётся тем же путём, что и приехавшее по сети, а для
	// этого нужен сервер: раздать медиа умеет только он.
	m_owner = server;

	checkDelivered(server);

	if (m_pending.empty())
		return;

	for (auto it = m_pending.begin(); it != m_pending.end();) {
		const std::string &hash = it->first;
		HTTPFetchResult res;
		if (!httpfetch_async_get(it->second.caller, res)) {
			++it;
			continue;
		}
		httpfetch_caller_free(it->second.caller);

		if (!res.succeeded || res.response_code != 200) {
			warningstream << "Skins: could not get " << hash << " (code "
					<< res.response_code << ")" << std::endl;
			m_missing.insert(hash);
			it = m_pending.erase(it);
			continue;
		}

		// The hash is the name of the thing, so it is also the proof that what
		// came back is the thing. Without this check a service that answered
		// wrongly — or anything sitting between us — would dress players in
		// whatever it liked.
		if (hexDigest(res.data) != hash) {
			errorstream << "Skins: " << hash << " came back as something else"
					<< std::endl;
			m_missing.insert(hash);
			it = m_pending.erase(it);
			continue;
		}

		const std::string path = cachePath(hash);
		fs::CreateAllDirs(fs::RemoveLastPathComponent(path));
		if (!fs::safeWriteToFile(path, res.data))
			warningstream << "Skins: could not keep " << hash << " on disk"
					<< std::endl;

		if (!publish(server, hash, res.data)) {
			warningstream << "Skins: could not hand out " << hash << std::endl;
			m_missing.insert(hash);
		}
		it = m_pending.erase(it);
	}
}

namespace
{

/// Как часто спрашивать службу, во что одеты сидящие. Полминуты — это цена
/// одного запроса на матч и та задержка, с которой переодевание в кабинете
/// доезжает до чужих глаз. Чаще незачем: человек переодевается не каждый шаг.
constexpr float ASK_EVERY = 30.0f;

} // namespace

void SkinCache::stepRefresh(Server *server, float dtime)
{
	if (!avatarsEnabled())
		return;

	if (m_asking) {
		HTTPFetchResult res;
		if (!httpfetch_async_get(m_ask_caller, res))
			return;
		httpfetch_caller_free(m_ask_caller);
		m_asking = false;

		if (!res.succeeded || res.response_code != 200) {
			// Молчащая служба ничего не сказала об облике, и это не повод
			// раздевать людей: носят то, что носили.
			warningstream << "Skins: the account service did not answer (code "
					<< res.response_code << ")" << std::endl;
			return;
		}

		Json::Value answer;
		{
			Json::CharReaderBuilder builder;
			std::string errors;
			const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			if (!reader->parse(res.data.data(), res.data.data() + res.data.size(),
					&answer, &errors) || !answer.isObject())
				return;
		}
		const Json::Value &worn = answer["skins"];
		if (!worn.isObject())
			return;

		for (session_t peer_id : server->getClientIDs()) {
			TicketIdentity id;
			if (!server->getClientIdentity(peer_id, id) || id.uid.empty())
				continue;
			// Нет игрока в ответе — на нём движковый облик: служба присылает
			// только тех, у кого свой.
			const std::string now = worn.get(id.uid, "").asString();
			if (now == id.skin)
				continue;
			server->setClientSkin(peer_id, now);
		}
		return;
	}

	m_ask_in -= dtime;
	if (m_ask_in > 0.0f)
		return;
	m_ask_in = ASK_EVERY;

	const std::string url = g_settings->get("auth_url");
	const std::string token = g_settings->get("auth_token");
	if (url.empty() || token.empty())
		return; // чужой сервер: спрашивать нечем и не у кого

	Json::Value uids(Json::arrayValue);
	for (session_t peer_id : server->getClientIDs()) {
		TicketIdentity id;
		if (server->getClientIdentity(peer_id, id) && !id.uid.empty())
			uids.append(id.uid);
	}
	if (uids.empty())
		return;

	Json::Value body;
	body["uids"] = uids;

	m_ask_caller = httpfetch_caller_alloc();
	HTTPFetchRequest req;
	req.caller = m_ask_caller;
	req.url = url + "/v1/servers/skins";
	req.method = HTTP_POST;
	req.raw_data = fastWriteJson(body);
	req.extra_headers.emplace_back("Content-Type: application/json");
	req.extra_headers.emplace_back("Authorization: Bearer " + token);
	req.connect_timeout = 2000;
	req.timeout = 5000;
	req.quiet = true;
	httpfetch_async(req);
	m_asking = true;
}
