// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "mediahttp.h"

#include <cstring>
#include <vector>

#include "client/clientmedia.h" // MTHASHSET_FILE_SIGNATURE
#include "network/networkprotocol.h" // MEDIA_BUNDLE_*
#include "serialization.h"
#include "filesys.h"
#include "log.h"
#include "util/serialize.h"
#include "util/string.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE_SOCKET(s) closesocket(s)
typedef int socklen_t;
// Winsock зовёт то же самое по-своему: опроса дескрипторов под именем poll там
// нет, а обрывать процесс сигналом на закрытом сокете некому и незачем.
typedef WSAPOLLFD PollFd;
#define POLL_SOCKETS(fds, n, ms) WSAPoll(fds, n, ms)
#define MSG_NOSIGNAL 0
#else
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define CLOSE_SOCKET(s) ::close(s)
typedef struct pollfd PollFd;
#define POLL_SOCKETS(fds, n, ms) ::poll(fds, n, ms)
#endif

namespace
{
	// Сколько ждём запроса и сколько его вообще может быть. Оба числа малы
	// нарочно: сюда ходит наш же клиент за файлом по хэшу, и всё, что длиннее
	// или медленнее, — это не он.
	const int REQUEST_TIMEOUT_MS = 15000;
	const size_t REQUEST_MAX_SIZE = 4096;
	// Сколько соединений держим разом. Клиент берёт файлы десятками сразу, и
	// игроков на сервере может быть много — но бесконечной очереди тут не
	// место: остальные подождут в очереди ядра.
	const int MAX_CONNECTIONS = 256;

	bool sendAll(int sock, const char *data, size_t size)
	{
		while (size > 0) {
			const auto sent = ::send(sock, data, (int)size, MSG_NOSIGNAL);
			if (sent <= 0)
				return false;
			data += sent;
			size -= static_cast<size_t>(sent);
		}
		return true;
	}

	bool sendResponse(int sock, const char *status, const std::string &body,
			const char *type)
	{
		std::string head = std::string("HTTP/1.1 ") + status + "\r\n"
				+ "Content-Type: " + type + "\r\n"
				+ "Content-Length: " + itos(body.size()) + "\r\n"
				// Файл назван своим хэшем: содержимое под этим именем уже не
				// изменится никогда, и переспрашивать его незачем.
				+ "Cache-Control: public, max-age=31536000, immutable\r\n"
				+ "Connection: keep-alive\r\n\r\n";
		return sendAll(sock, head.data(), head.size())
				&& sendAll(sock, body.data(), body.size());
	}

	// Обратное к hex_encode: в util его нет, а нужно оно ровно здесь — набор
	// хэшей клиент читает сырыми двадцатью байтами, а раздаются файлы по hex.
	std::string hexDecode(const std::string &hex)
	{
		auto nibble = [](char c) -> int {
			if (c >= '0' && c <= '9')
				return c - '0';
			if (c >= 'a' && c <= 'f')
				return c - 'a' + 10;
			return -1;
		};

		std::string out;
		if (hex.size() % 2 != 0)
			return out;
		out.reserve(hex.size() / 2);
		for (size_t i = 0; i < hex.size(); i += 2) {
			const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
			if (hi < 0 || lo < 0)
				return std::string();
			out.push_back(static_cast<char>((hi << 4) | lo));
		}
		return out;
	}

	bool isSha1Hex(const std::string &s)
	{
		if (s.size() != 40)
			return false;
		for (char c : s) {
			const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
			if (!ok)
				return false;
		}
		return true;
	}
}

MediaHttpServer::MediaHttpServer(u16 port) : m_port(port)
{
	if (port == 0)
		return;

	m_listen_sock = ::socket(AF_INET6, SOCK_STREAM, 0);
	bool v6 = m_listen_sock >= 0;
	if (!v6)
		m_listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);
	if (m_listen_sock < 0) {
		errorstream << "MediaHttpServer: cannot create socket" << std::endl;
		return;
	}

	int on = 1;
	::setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

	bool bound = false;
	if (v6) {
		// Двойной стек: тот же слушающий сокет принимает и IPv4.
		int off = 0;
		::setsockopt(m_listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof(off));
		sockaddr_in6 addr{};
		addr.sin6_family = AF_INET6;
		addr.sin6_addr = in6addr_any;
		addr.sin6_port = htons(port);
		bound = ::bind(m_listen_sock, (sockaddr *)&addr, sizeof(addr)) == 0;
	} else {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);
		bound = ::bind(m_listen_sock, (sockaddr *)&addr, sizeof(addr)) == 0;
	}

	if (!bound || ::listen(m_listen_sock, 64) != 0) {
		// Порт занят или закрыт — медиа поедет игровым протоколом, как раньше.
		// Ронять из-за этого весь сервер незачем: игра работает и так.
		errorstream << "MediaHttpServer: cannot listen on port " << port
			<< ", media will be sent over the game protocol" << std::endl;
		CLOSE_SOCKET(m_listen_sock);
		m_listen_sock = -1;
		return;
	}

	m_running = true;
	m_thread = std::thread(&MediaHttpServer::threadMain, this);
	infostream << "MediaHttpServer: serving media on TCP port " << port << std::endl;
}

MediaHttpServer::~MediaHttpServer()
{
	m_stop = true;
	if (m_listen_sock >= 0) {
		// Закрываем слушающий сокет: поток сидит в poll и выйдет по ошибке.
		::shutdown(m_listen_sock, 2);
		CLOSE_SOCKET(m_listen_sock);
		m_listen_sock = -1;
	}
	if (m_thread.joinable())
		m_thread.join();
	m_running = false;
}

void MediaHttpServer::setMedia(std::unordered_map<std::string, std::string> &&by_hash)
{
	MutexAutoLock lock(m_media_mutex);
	m_by_hash = std::move(by_hash);
	m_index = buildIndex();
	m_bundle.clear();
	m_bundle_stale = true;
}

std::string MediaHttpServer::buildIndex() const
{
	// Тот же формат, что клиент читает у любой раздачи: подпись, версия и
	// дальше сырые хэши подряд (см. deSerializeHashSet в clientmedia.cpp).
	std::string out;
	out.resize(6);
	writeU32((u8 *)&out[0], MTHASHSET_FILE_SIGNATURE);
	writeU16((u8 *)&out[4], 1);
	out.reserve(6 + m_by_hash.size() * 20);
	for (const auto &it : m_by_hash) {
		const std::string raw = hexDecode(it.first);
		if (raw.size() == 20)
			out.append(raw);
	}
	return out;
}

// Один файл со всем медиа разом.
//
// Ради него всё и затевалось: файлов у большого плейса тысячи, и каждый из них
// по отдельности — это поездка туда-обратно. Сотня микросекунд на файл
// превращается в десяток секунд ожидания, притом что сами байты приезжают за
// пару. Здесь их забирают одним запросом.
//
// Складывается всё в свой простой вид (имя, длина, данные) и жмётся целиком:
// общий словарь на тысячи мелких файлов выигрывает заметно больше, чем сжатие
// каждого по отдельности.
const std::string &MediaHttpServer::bundle()
{
	// Снимок набора берётся под замком, а читается с диска и жмётся уже без
	// него: иначе одно соединение, попросившее файл, ждало бы, пока для
	// другого соберут все семь тысяч.
	std::unordered_map<std::string, std::string> snapshot;
	{
		MutexAutoLock lock(m_media_mutex);
		if (!m_bundle_stale)
			return m_bundle;
		snapshot = m_by_hash;
	}

	std::string raw;
	raw.resize(10);
	writeU32((u8 *)&raw[0], MEDIA_BUNDLE_SIGNATURE);
	writeU16((u8 *)&raw[4], MEDIA_BUNDLE_VERSION);

	u32 count = 0;
	for (const auto &it : snapshot) {
		std::string data;
		if (!fs::ReadFile(it.second, data, true))
			continue;

		// Имя записи — тот же хэш, по которому файл спрашивают поштучно:
		// клиент сверяет содержимое с обещанным, и подложить чужое нельзя.
		u8 len[4];
		writeU32(len, (u32)data.size());
		raw.append(it.first);
		raw.append((const char *)len, 4);
		raw.append(data);
		count++;
	}
	writeU32((u8 *)&raw[6], count);

	std::ostringstream oss(std::ios::binary);
	compressZstd(raw, oss, 3);

	MutexAutoLock lock(m_media_mutex);
	m_bundle = oss.str();
	m_bundle_stale = false;

	infostream << "MediaHttpServer: bundle of " << count << " files, "
		<< (raw.size() >> 10) << "KiB packed into " << (m_bundle.size() >> 10)
		<< "KiB" << std::endl;
	return m_bundle;
}

std::string MediaHttpServer::findPath(const std::string &sha1_hex) const
{
	MutexAutoLock lock(m_media_mutex);
	auto it = m_by_hash.find(sha1_hex);
	return it == m_by_hash.end() ? std::string() : it->second;
}

bool MediaHttpServer::serveOnce(int sock)
{
	// Заголовок запроса целиком, но не длиннее разумного.
	std::string request;
	while (request.find("\r\n\r\n") == std::string::npos) {
		if (request.size() > REQUEST_MAX_SIZE)
			return false;

		PollFd pfd{};
		pfd.fd = sock;
		pfd.events = POLLIN;
		if (POLL_SOCKETS(&pfd, 1, REQUEST_TIMEOUT_MS) <= 0)
			return false;

		char buf[1024];
		const auto got = ::recv(sock, buf, (int)sizeof(buf), 0);
		if (got <= 0)
			return false;
		request.append(buf, got);
	}

	// Нас интересует только первая строка: метод и путь.
	const size_t line_end = request.find("\r\n");
	const std::string line = request.substr(0, line_end);
	if (line.compare(0, 4, "GET ") != 0) {
		sendResponse(sock, "405 Method Not Allowed", "", "text/plain");
		return false;
	}
	const size_t path_end = line.find(' ', 4);
	if (path_end == std::string::npos)
		return false;
	std::string path = line.substr(4, path_end - 4);

	// Путь у нас всегда один уровень: ведущий слеш и имя. Ничего другого не
	// разбираем — а значит и вырваться некуда, тут просто нет разбора путей.
	if (path.empty() || path[0] != '/')
		return false;
	path.erase(0, 1);

	if (path == MTHASHSET_FILE_NAME) {
		std::string index;
		{
			MutexAutoLock lock(m_media_mutex);
			index = m_index;
		}
		return sendResponse(sock, "200 OK", index, "application/octet-stream");
	}

	if (path == MEDIA_BUNDLE_FILE_NAME) {
		// Копия под замком не берётся: пока файл собирают, отдавать нечего, а
		// собирается он один раз на набор.
		const std::string &packed = bundle();
		return sendResponse(sock, "200 OK", packed, "application/octet-stream");
	}

	if (!isSha1Hex(path)) {
		sendResponse(sock, "404 Not Found", "", "text/plain");
		return true;
	}

	const std::string file_path = findPath(path);
	std::string data;
	if (file_path.empty() || !fs::ReadFile(file_path, data, true)) {
		sendResponse(sock, "404 Not Found", "", "text/plain");
		return true;
	}

	return sendResponse(sock, "200 OK", data, "application/octet-stream");
}

void MediaHttpServer::threadMain()
{
	while (!m_stop) {
		PollFd pfd{};
		pfd.fd = m_listen_sock;
		pfd.events = POLLIN;
		const int ready = POLL_SOCKETS(&pfd, 1, 200);
		if (ready < 0)
			break;
		if (ready == 0)
			continue;

		const int sock = ::accept(m_listen_sock, nullptr, nullptr);
		if (sock < 0)
			continue;

		// Соединения обслуживаются каждое своим потоком, и это здесь не роскошь:
		// клиент открывает их десятками сразу, и очередь из одного означала бы,
		// что мы вернули ту же однопоточную выдачу, от которой уходили.
		if (m_connections >= MAX_CONNECTIONS) {
			CLOSE_SOCKET(sock);
			continue;
		}

		// Мелкие ответы не должны ждать попутчиков.
		int on = 1;
		::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));

		m_connections++;
		std::thread([this, sock]() {
			// Соединение живёт, пока клиент просит файлы: он берёт их сотнями,
			// и рукопожатие на каждый было бы дороже самого файла.
			while (!m_stop && serveOnce(sock)) {
			}
			CLOSE_SOCKET(sock);
			m_connections--;
		}).detach();
	}
}
