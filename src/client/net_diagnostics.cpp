// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "net_diagnostics.h"

#include "constants.h"
#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

std::unique_ptr<NetDiagnostics> g_netdiag;

namespace
{

/// How many frames of history each measurement keeps
constexpr size_t WINDOW = 240;
/// How many packet arrivals are remembered per object
constexpr size_t ARRIVALS = 64;
/// An object not heard of for this long is forgotten
constexpr f32 FORGET_AFTER = 10.0f;

f32 average(const std::deque<f32> &values)
{
	if (values.empty())
		return 0.0f;

	f32 sum = 0.0f;
	for (f32 v : values)
		sum += v;

	return sum / values.size();
}

f32 deviation(const std::deque<f32> &values)
{
	if (values.size() < 2)
		return 0.0f;

	const f32 mean = average(values);
	f32 sum = 0.0f;
	for (f32 v : values)
		sum += (v - mean) * (v - mean);

	return std::sqrt(sum / (values.size() - 1));
}

f32 highest(const std::deque<f32> &values)
{
	f32 best = 0.0f;
	for (f32 v : values)
		best = std::max(best, v);
	return best;
}

/// Value below which the given share of the window sits
f32 percentile(std::deque<f32> values, f32 share)
{
	if (values.empty())
		return 0.0f;

	std::vector<f32> sorted(values.begin(), values.end());
	std::sort(sorted.begin(), sorted.end());

	size_t index = (size_t)(share * (sorted.size() - 1));
	return sorted[index];
}

template<typename T>
void keep(std::deque<T> &values, size_t limit)
{
	while (values.size() > limit)
		values.pop_front();
}

} // namespace

bool NetDiagnostics::enabled()
{
	return g_netdiag != nullptr;
}

NetDiagnostics::NetDiagnostics()
{
	m_logging = g_settings->getBool("netsync_diagnostics_log");
	if (m_logging)
		openLogs();

	infostream << "Network diagnostics on"
		<< (m_logging ? ", writing CSV logs" : "") << std::endl;
}

NetDiagnostics::~NetDiagnostics()
{
	if (m_frame_log.is_open())
		m_frame_log.close();
	if (m_packet_log.is_open())
		m_packet_log.close();
}

void NetDiagnostics::openLogs()
{
	const std::string dir = porting::path_user + DIR_DELIM + "netsync";
	fs::CreateAllDirs(dir);

	m_frame_log.open(dir + DIR_DELIM + "frames.csv", std::ios::out | std::ios::trunc);
	m_packet_log.open(dir + DIR_DELIM + "packets.csv", std::ios::out | std::ios::trunc);

	if (m_frame_log.is_open()) {
		m_frame_log << "time,frame,dtime,"
				"player_x,player_y,player_z,player_speed,player_jerk,"
				"model_lag,model_wobble,camera_jerk,"
				"platform_id,platform_dx,platform_dy,platform_dz,platform_gap,"
				"object_id,object_x,object_y,object_z,object_speed,object_jerk,"
				"buffer_behind,buffer_target,buffer_samples,frames_since_packet,"
				"peer_id,peer_x,peer_y,peer_z,peer_ride\n";
	}

	if (m_packet_log.is_open())
		m_packet_log << "time,object_id,object_name,interval,gap_since_previous\n";
}

NetDiagObject &NetDiagnostics::object(u16 id, const std::string &name)
{
	NetDiagObject &obj = m_objects[id];

	if (obj.id == 0) {
		obj.id = id;
		obj.name = name;
	}

	obj.last_seen = m_now;
	return obj;
}

void NetDiagnostics::objectPacket(u16 id, const std::string &name, f32 interval)
{
	NetDiagObject &obj = object(id, name);

	const f32 gap = obj.arrivals.empty() ? 0.0f : m_now - obj.arrivals.back();

	obj.arrivals.push_back(m_now);
	obj.intervals.push_back(interval);
	keep(obj.arrivals, ARRIVALS);
	keep(obj.intervals, ARRIVALS);

	obj.packets++;
	obj.frames_since_packet = 0;

	if (m_packet_log.is_open()) {
		m_packet_log << std::fixed << std::setprecision(4)
			<< m_now << "," << id << "," << name << ","
			<< interval << "," << gap << "\n";
	}
}

void NetDiagnostics::objectPlayback(u16 id, f32 behind, f32 target,
		size_t samples, bool dry)
{
	NetDiagObject &obj = object(id, "");

	obj.buffer_behind = behind;
	obj.buffer_target = target;
	obj.buffer_samples = samples;

	if (dry)
		obj.dry_frames++;
}

void NetDiagnostics::objectReset(u16 id)
{
	object(id, "").resets++;
}

void NetDiagnostics::objectDrawn(u16 id, v3f pos, f32 dtime, bool is_player,
		u16 ride_id)
{
	if (dtime <= 0.0f)
		return;

	NetDiagObject &obj = object(id, "");

	obj.is_player = is_player;
	obj.ride_id = ride_id;

	if (obj.has_drawn) {
		const v3f speed = (pos - obj.drawn_pos) / dtime;
		// What the eye reads as shaking is the change of speed, not the speed
		const f32 change = (speed - obj.drawn_speed).getLength() / BS;

		obj.jerk.push_back(change);
		keep(obj.jerk, WINDOW);
		obj.jerk_max = std::max(obj.jerk_max, change);

		// An object that had speed and suddenly has none for a frame is a
		// stall: the buffer ran out, or a packet was replaced by a hold
		if (obj.drawn_speed.getLength() > 0.05f * BS &&
				speed.getLength() < 0.001f * BS)
			obj.stalls++;

		obj.drawn_speed = speed;
	}

	obj.drawn_pos = pos;
	obj.has_drawn = true;
	obj.frames_since_packet++;
	obj.frames_since_packet_max = std::max(obj.frames_since_packet_max,
			obj.frames_since_packet);
}

void NetDiagnostics::playerStep(v3f pos, v3f speed, v3f platform_delta, f32 gap,
		u16 platform_id, f32 dtime)
{
	if (dtime > 0.0f && m_has_player) {
		const f32 change = (speed - m_player_speed).getLength() / BS;
		m_player_jerk.push_back(change);
		keep(m_player_jerk, WINDOW);
		m_player_jerk_max = std::max(m_player_jerk_max, change);
	}

	m_player_pos = pos;
	m_player_speed = speed;
	// The player's physics runs in sub-steps while a platform moves once per
	// frame, so one sub-step of a frame carries them and the rest carry them
	// nowhere. Keeping only the last one reads as a platform that stood still:
	// what the frame did is the sum, not the tail.
	m_player_platform_delta += platform_delta;
	m_player_gap = gap;
	m_platform_id = platform_id;
	m_has_player = true;
}

void NetDiagnostics::localModel(v3f player_pos, v3f model_pos, v3f camera_pos)
{
	const f32 lag = (model_pos - player_pos).getLength() / BS;

	if (m_has_model) {
		// A steady trail is only latency; a trail that changes size every
		// frame is what shakes
		m_model_wobble.push_back(std::fabs(lag - m_model_lag_last));
		keep(m_model_wobble, WINDOW);
	}

	m_model_lag.push_back(lag);
	keep(m_model_lag, WINDOW);
	m_model_lag_last = lag;
	m_has_model = true;

	if (m_has_camera && !m_frame_times.empty() && m_frame_times.back() > 0.0f) {
		const f32 dtime = m_frame_times.back();
		const v3f speed = (camera_pos - m_camera_pos) / dtime;
		m_camera_jerk.push_back((speed - m_camera_speed).getLength() / BS);
		keep(m_camera_jerk, WINDOW);
		m_camera_speed = speed;
	}

	m_camera_pos = camera_pos;
	m_has_camera = true;
}

void NetDiagnostics::serverMovedPlayer(v3f from, v3f to)
{
	const f32 distance = (to - from).getLength() / BS;

	m_server_moves++;
	m_server_move_worst = std::max(m_server_move_worst, distance);
	m_server_move_last = distance;

	if (m_packet_log.is_open()) {
		m_packet_log << std::fixed << std::setprecision(4)
			<< m_now << ",0,server_moved_player," << distance << ",0\n";
	}
}

void NetDiagnostics::frame(f32 dtime, Client *client)
{
	m_now += dtime;
	m_frames++;

	m_frame_times.push_back(dtime);
	keep(m_frame_times, WINDOW);

	// Objects that went away take their statistics with them
	for (auto it = m_objects.begin(); it != m_objects.end(); ) {
		if (m_now - it->second.last_seen > FORGET_AFTER)
			it = m_objects.erase(it);
		else
			++it;
	}

	if (m_logging)
		writeFrameLog(dtime, client);
}

void NetDiagnostics::writeFrameLog(f32 dtime, Client *client)
{
	if (!m_frame_log.is_open())
		return;

	// One line per frame about the object that matters: what the player rides,
	// or else the one that moves the most
	const NetDiagObject *subject = nullptr;

	if (m_platform_id != 0) {
		auto it = m_objects.find(m_platform_id);
		if (it != m_objects.end())
			subject = &it->second;
	}

	if (!subject) {
		f32 fastest = 0.0f;
		for (const auto &entry : m_objects) {
			const f32 speed = entry.second.drawn_speed.getLength();
			if (speed > fastest) {
				fastest = speed;
				subject = &entry.second;
			}
		}
	}

	m_frame_log << std::fixed << std::setprecision(5)
		<< m_now << "," << m_frames << "," << dtime << ","
		<< (m_player_pos.X / BS) << "," << (m_player_pos.Y / BS) << ","
		<< (m_player_pos.Z / BS) << "," << (m_player_speed.getLength() / BS) << ","
		<< (m_player_jerk.empty() ? 0.0f : m_player_jerk.back()) << ","
		<< (m_model_lag.empty() ? 0.0f : m_model_lag.back()) << ","
		<< (m_model_wobble.empty() ? 0.0f : m_model_wobble.back()) << ","
		<< (m_camera_jerk.empty() ? 0.0f : m_camera_jerk.back()) << ","
		<< m_platform_id << ","
		<< (m_player_platform_delta.X / BS) << ","
		<< (m_player_platform_delta.Y / BS) << ","
		<< (m_player_platform_delta.Z / BS) << ","
		<< m_player_gap << ",";

	if (subject) {
		m_frame_log << subject->id << ","
			<< (subject->drawn_pos.X / BS) << "," << (subject->drawn_pos.Y / BS) << ","
			<< (subject->drawn_pos.Z / BS) << ","
			<< (subject->drawn_speed.getLength() / BS) << ","
			<< (subject->jerk.empty() ? 0.0f : subject->jerk.back()) << ","
			<< subject->buffer_behind << "," << subject->buffer_target << ","
			<< subject->buffer_samples << "," << subject->frames_since_packet;
	} else {
		m_frame_log << "0,0,0,0,0,0,0,0,0,0";
	}

	// Another player, drawn from the same scene as the deck they stand on.
	// Whether the two agree is the question: each is played back on its own
	// buffer, and a buffer that is deeper puts its object further into the
	// past than the one beside it.
	const NetDiagObject *peer = nullptr;

	for (const auto &entry : m_objects) {
		if (entry.second.is_player && entry.second.has_drawn) {
			peer = &entry.second;
			break;
		}
	}

	if (peer) {
		m_frame_log << "," << peer->id << ","
			<< (peer->drawn_pos.X / BS) << "," << (peer->drawn_pos.Y / BS) << ","
			<< (peer->drawn_pos.Z / BS) << "," << peer->ride_id;
	} else {
		m_frame_log << ",0,0,0,0,0";
	}

	m_frame_log << "\n";

	// The carry is summed across the frame's sub-steps; the next frame starts
	// its own sum
	m_player_platform_delta = v3f();
}

std::string NetDiagnostics::panel() const
{
	std::ostringstream os;
	os << std::fixed << std::setprecision(2);

	const f32 frame_avg = average(m_frame_times);

	os << "Network motion\n";
	os << "  frame " << (frame_avg * 1000.0f) << " ms"
		<< "  spread " << (deviation(m_frame_times) * 1000.0f) << " ms\n";

	if (m_has_model) {
		os << "  own model trails " << average(m_model_lag) << " b"
			<< "  wobble " << average(m_model_wobble)
			<< "  p99 " << percentile(m_model_wobble, 0.99f) << " b\n";
		os << "  camera jerk " << average(m_camera_jerk)
			<< "  p99 " << percentile(m_camera_jerk, 0.99f) << " b/s\n";
	}

	if (m_has_player) {
		os << "  player jerk " << (m_player_jerk.empty() ? 0.0f : average(m_player_jerk))
			<< "  p99 " << percentile(m_player_jerk, 0.99f)
			<< "  worst " << m_player_jerk_max << " b/s\n";

		if (m_server_moves > 0) {
			os << "  server moved us " << m_server_moves << " times"
				<< "  worst " << m_server_move_worst << " b\n";
		}

		if (m_platform_id != 0) {
			os << "  riding #" << m_platform_id
				<< "  step " << (m_player_platform_delta.getLength() / BS)
				<< " b  gap " << m_player_gap << " b\n";
		}
	}

	// Objects that are actually moving, worst first
	std::vector<const NetDiagObject *> shown;
	for (const auto &entry : m_objects) {
		if (entry.second.packets > 0)
			shown.push_back(&entry.second);
	}

	std::sort(shown.begin(), shown.end(),
			[](const NetDiagObject *a, const NetDiagObject *b) {
				return average(a->jerk) > average(b->jerk);
			});

	if (shown.size() > 4)
		shown.resize(4);

	for (const NetDiagObject *obj : shown) {
		std::deque<f32> gaps;
		for (size_t i = 1; i < obj->arrivals.size(); i++)
			gaps.push_back(obj->arrivals[i] - obj->arrivals[i - 1]);

		os << "  #" << obj->id << " " << obj->name << "\n";
		os << "    packets every " << (average(gaps) * 1000.0f) << " ms"
			<< "  spread " << (deviation(gaps) * 1000.0f)
			<< "  worst " << (highest(gaps) * 1000.0f) << " ms\n";
		os << "    buffer " << (obj->buffer_behind * 1000.0f) << " of "
			<< (obj->buffer_target * 1000.0f) << " ms"
			<< "  samples " << obj->buffer_samples
			<< "  dry " << obj->dry_frames
			<< "  resets " << obj->resets << "\n";
		os << "    jerk " << average(obj->jerk)
			<< "  p99 " << percentile(obj->jerk, 0.99f)
			<< "  worst " << obj->jerk_max << " b/s"
			<< "  stalls " << obj->stalls
			<< "  quiet frames " << obj->frames_since_packet_max << "\n";
	}

	return os.str();
}

void update_net_diagnostics()
{
	const bool wanted = g_settings->getBool("netsync_diagnostics");

	if (wanted && !g_netdiag)
		g_netdiag = std::make_unique<NetDiagnostics>();
	else if (!wanted && g_netdiag)
		g_netdiag.reset();
}
