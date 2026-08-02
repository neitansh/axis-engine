// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrlichttypes.h"
#include "address.h"
#include <deque>
#include <memory>
#include <random>
#include <vector>

/*
	A worse network than the one that is actually there.

	Motion looks smooth on a machine talking to itself, and that is the one
	case nobody plays in. This holds incoming datagrams back, spreads their
	arrival out and drops some of them, so the client can be measured under
	the conditions it will meet: a distant server, a busy connection, a lossy
	link.

	Off unless asked for, and it only ever touches what comes in - nothing
	here reaches the wire, so a server is never affected by a client running
	with it on.
*/
class NetImpairment
{
public:
	/// Builds one from the settings, or nothing at all when it is switched off
	static std::unique_ptr<NetImpairment> create();

	/// Takes a datagram out of the world for a while. Returns false when the
	/// packet was dropped outright and will never come back.
	bool hold(const Address &sender, const u8 *data, u32 size);

	/// Hands back a datagram whose time has come, or 0 when none is due
	u32 release(Address &sender, u8 *data, u32 capacity);

	/// How long to wait before something is due, in milliseconds
	int waitMs() const;

	u32 held() const { return m_queue.size(); }

private:
	NetImpairment(int latency_ms, int jitter_ms, float loss, u32 seed);

	struct Held
	{
		u64 due_ms;
		Address sender;
		std::vector<u8> data;
	};

	/// Sorted by the time each is due, so the front is always the next one out
	std::deque<Held> m_queue;

	std::mt19937 m_random;
	int m_latency_ms;
	int m_jitter_ms;
	float m_loss;
};
