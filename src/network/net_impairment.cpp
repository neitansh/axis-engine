// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "net_impairment.h"
#include "log.h"
#include "settings.h"
#include "porting.h"
#include <algorithm>

std::unique_ptr<NetImpairment> NetImpairment::create()
{
	const int latency = g_settings->getS16("net_emulate_latency");
	const int jitter = g_settings->getS16("net_emulate_jitter");
	const float loss = g_settings->getFloat("net_emulate_loss") / 100.0f;

	if (latency <= 0 && jitter <= 0 && loss <= 0.0f)
		return nullptr;

	// A fixed seed by default: two runs of the same measurement should meet
	// the same network, or the numbers cannot be compared
	const u32 seed = (u32)g_settings->getU16("net_emulate_seed");

	warningstream << "Network impairment on: " << latency << " ms latency, "
		<< jitter << " ms jitter, " << (loss * 100.0f) << "% loss"
		<< std::endl;

	return std::unique_ptr<NetImpairment>(
			new NetImpairment(latency, jitter, loss, seed));
}

NetImpairment::NetImpairment(int latency_ms, int jitter_ms, float loss, u32 seed) :
	m_random(seed),
	m_latency_ms(std::max(0, latency_ms)),
	m_jitter_ms(std::max(0, jitter_ms)),
	m_loss(std::max(0.0f, std::min(1.0f, loss)))
{
}

bool NetImpairment::hold(const Address &sender, const u8 *data, u32 size)
{
	if (m_loss > 0.0f) {
		std::uniform_real_distribution<float> chance(0.0f, 1.0f);

		if (chance(m_random) < m_loss)
			return false;
	}

	int delay = m_latency_ms;

	if (m_jitter_ms > 0) {
		std::uniform_int_distribution<int> spread(-m_jitter_ms, m_jitter_ms);

		delay = std::max(0, delay + spread(m_random));
	}

	Held held;
	held.due_ms = porting::getTimeMs() + (u64)delay;
	held.sender = sender;
	held.data.assign(data, data + size);

	// Jitter reorders packets, which is exactly what a real link does; the
	// queue stays sorted by due time so that release() only looks at the front
	auto at = std::upper_bound(m_queue.begin(), m_queue.end(), held.due_ms,
			[](u64 due, const Held &other) { return due < other.due_ms; });

	m_queue.insert(at, std::move(held));
	return true;
}

u32 NetImpairment::release(Address &sender, u8 *data, u32 capacity)
{
	if (m_queue.empty())
		return 0;

	const Held &front = m_queue.front();

	if (front.due_ms > porting::getTimeMs())
		return 0;

	const u32 size = (u32)std::min((size_t)capacity, front.data.size());

	memcpy(data, front.data.data(), size);
	sender = front.sender;

	m_queue.pop_front();
	return size;
}

int NetImpairment::waitMs() const
{
	// Nothing waiting: the caller may block for as long as it likes
	if (m_queue.empty())
		return 100;

	const u64 now = porting::getTimeMs();
	const u64 due = m_queue.front().due_ms;

	if (due <= now)
		return 0;

	return (int)std::min<u64>(due - now, 100);
}
