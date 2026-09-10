// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "skins.h"

#include "filesys.h"
#include "httpfetch.h"
#include "log.h"
#include "porting.h"
#include "server.h"
#include "settings.h"
#include "util/hashing.h"
#include "util/hex.h"
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

	// Уже приезжал когда-то: содержимое по хэшу неизменяемо, и спрашивать его
	// второй раз незачем — ни у службы, ни после перезапуска сервера.
	{
		std::string kept;
		if (fs::ReadFile(cachePath(hash), kept, true) && hexDigest(kept) == hash) {
			if (m_owner && publish(m_owner, hash, kept))
				return mediaName(hash);
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
	Server::DynamicMediaArgs args;
	args.filename = mediaName(hash);
	args.data = data;
	args.token = 0;
	if (!server->dynamicAddMedia(args))
		return false;

	m_ready.insert(hash);
	// Everyone wearing it is drawn again: until now they were in the look the
	// engine ships, because the one they own was not here yet.
	server->refreshSkin(hash);
	return true;
}

void SkinCache::step(Server *server)
{
	// Дисковый кэш отдаётся тем же путём, что и приехавшее по сети, а для
	// этого нужен сервер: раздать медиа умеет только он.
	m_owner = server;

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
