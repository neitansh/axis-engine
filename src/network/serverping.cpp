// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "serverping.h"

#include "network/address.h"
#include "network/mtp/internal.h"
#include "network/networkexceptions.h"
#include "network/networkprotocol.h"
#include "network/socket.h"
#include "porting.h"
#include "util/serialize.h"

bool pingServer(const std::string &address, int port, int timeout_ms,
		ServerPing &out, const std::atomic<bool> *stop)
{
	if (port <= 0 || port > 65535)
		return false;

	Address dest;
	try {
		dest.Resolve(address.c_str());
	} catch (const ResolveError &) {
		return false;
	}
	dest.setPort(port);

	UDPSocket socket;
	if (!socket.init(dest.isIPv6(), true))
		return false;

	try {
		if (dest.isIPv6()) {
			IPv6AddressBytes any;
			socket.Bind(Address(&any, 0));
		} else {
			socket.Bind(Address((u32)0, (u16)0));
		}
	} catch (const SocketException &) {
		// An ephemeral port is best effort; sending still works without it
	}

	// Ask how busy the server is. A server that does not know the query stays
	// silent, so an empty reliable original follows as a plain reachability
	// probe: every server answers that one by handing out a peer id.
	u8 query[BASE_HEADER_SIZE + 2] = {};
	writeU32(&query[0], PROTOCOL_ID);
	writeU16(&query[4], PEER_ID_INEXISTENT);
	query[6] = 0; // channel
	query[7] = con::PACKET_TYPE_CONTROL;
	query[8] = con::CONTROLTYPE_QUERY_INFO;

	u8 probe[BASE_HEADER_SIZE + 3 + 1] = {};
	writeU32(&probe[0], PROTOCOL_ID);
	writeU16(&probe[4], PEER_ID_INEXISTENT);
	probe[6] = 0; // channel
	probe[7] = con::PACKET_TYPE_RELIABLE;
	writeU16(&probe[8], SEQNUM_INITIAL);
	probe[10] = con::PACKET_TYPE_ORIGINAL;

	const u64 sent_at = porting::getTimeMs();

	try {
		socket.Send(dest, query, sizeof(query));
		socket.Send(dest, probe, sizeof(probe));
	} catch (const SendFailedException &) {
		return false;
	}

	char buffer[1024];
	Address sender;
	bool have_ping = false;
	out = ServerPing();

	// Both packets are in flight; keep reading until the info reply shows up or
	// the budget runs out, so the counts are not lost to ordering.
	while (true) {
		const u64 elapsed = porting::getTimeMs() - sent_at;
		if ((int)elapsed >= timeout_ms)
			break;
		if (stop && *stop)
			break;

		// Ждём порциями, а не одним куском: иначе просьбу закругляться мы
		// заметили бы только по истечении всего срока.
		const int slice = MYMIN(200, timeout_ms - (int)elapsed);
		if (!socket.WaitData(slice))
			continue;

		const int received = socket.Receive(sender, buffer, sizeof(buffer));
		if (received < BASE_HEADER_SIZE)
			continue;

		const u8 *data = (const u8 *)buffer;
		if (readU32(data) != PROTOCOL_ID || sender.getPort() != dest.getPort())
			continue;

		if (!have_ping) {
			out.ping_ms = porting::getTimeMs() - sent_at;
			have_ping = true;
		}

		if (received >= BASE_HEADER_SIZE + 6 &&
				data[BASE_HEADER_SIZE] == con::PACKET_TYPE_CONTROL &&
				data[BASE_HEADER_SIZE + 1] == con::CONTROLTYPE_SERVER_INFO) {
			out.clients = readU16(data + BASE_HEADER_SIZE + 2);
			out.clients_max = readU16(data + BASE_HEADER_SIZE + 4);
			break;
		}
	}

	return have_ping;
}
