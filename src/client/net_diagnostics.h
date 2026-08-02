// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrlichttypes_bloated.h"

#include <deque>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Measures what network motion actually does, instead of leaving it to the eye.
 *
 * Shaking is a property of the second derivative: a position that arrives late
 * is not visible, a speed that changes every frame is. So the instrument
 * records, per object and per frame, where the object was drawn, and derives
 * speed and its change from that - the same quantity the eye reports as
 * "jitter". Around it sits everything needed to tell one cause from another:
 * when packets arrived, how evenly, how deep the playback buffer ran, how
 * often it ran dry, and what the player and the deck under them did.
 *
 * Off by default and cheap when off. Turned on with "netsync_diagnostics",
 * which writes a panel on screen, and "netsync_diagnostics_log", which writes
 * two CSV files into the user directory for looking at afterwards.
 */

class Client;

/// One tracked object, usually a contraption or another player
struct NetDiagObject
{
	u16 id = 0;
	std::string name;

	// --- arrival of packets ---
	/// Local time each position packet arrived, seconds
	std::deque<f32> arrivals;
	/// Interval the server reported with each packet
	std::deque<f32> intervals;
	u32 packets = 0;
	/// Frames since the last packet, and the worst such run
	u32 frames_since_packet = 0;
	u32 frames_since_packet_max = 0;

	// --- playback buffer ---
	f32 buffer_behind = 0.0f;
	f32 buffer_target = 0.0f;
	size_t buffer_samples = 0;
	/// Playback ran past the newest packet and had to hold still
	u32 dry_frames = 0;
	/// Buffer was reset, e.g. after a pause or a teleport
	u32 resets = 0;

	// --- what reached the screen ---
	v3f drawn_pos;
	v3f drawn_speed;
	bool has_drawn = false;
	/// Another player, as opposed to a mob or a contraption
	bool is_player = false;
	/// |Δspeed| per frame, in blocks per second, over a window
	std::deque<f32> jerk;
	f32 jerk_max = 0.0f;
	/// Frames where the object did not move at all while it should have
	u32 stalls = 0;

	f32 last_seen = 0.0f;
};

class NetDiagnostics
{
public:
	NetDiagnostics();
	~NetDiagnostics();

	static bool enabled();

	/// A position packet for an object arrived
	void objectPacket(u16 id, const std::string &name, f32 interval);

	/// State of an object's playback buffer this frame
	void objectPlayback(u16 id, f32 behind, f32 target, size_t samples, bool dry);

	/// The buffer of an object was thrown away
	void objectReset(u16 id);

	/// Where an object ended up on screen this frame
	void objectDrawn(u16 id, v3f pos, f32 dtime, bool is_player = false);

	/// What the local player and the thing they stand on did this step
	void playerStep(v3f pos, v3f speed, v3f platform_delta, f32 gap,
			u16 platform_id, f32 dtime);

	/**
	 * How far the model of the local player sits from the player itself.
	 *
	 * The two are separate things: the player is a position the client
	 * simulates, the model is an object drawn from it. If the model is copied
	 * from the player before the player moves, it trails by one frame - by
	 * speed times frame time, which wobbles with every frame that is a little
	 * longer or shorter than the last. From third person that is the model
	 * shaking against a steady camera.
	 */
	void localModel(v3f player_pos, v3f model_pos, v3f camera_pos);

	/**
	 * The server moved the player itself.
	 *
	 * Position is the client's to simulate, so this only happens on teleports
	 * and corrections - and on a moving contraption a correction lands as a
	 * jump. Counting them tells a client-side wobble from a server-side one.
	 */
	void serverMovedPlayer(v3f from, v3f to);

	/// End of frame: rolls the window forward and writes a log line
	void frame(f32 dtime, Client *client);

	/// Text of the on-screen panel, empty when there is nothing to show
	std::string panel() const;

	/// The object the player is standing on, 0 if none
	u16 platformId() const { return m_platform_id; }

private:
	NetDiagObject &object(u16 id, const std::string &name);
	void openLogs();
	void writeFrameLog(f32 dtime, Client *client);

	std::unordered_map<u16, NetDiagObject> m_objects;

	// --- local player ---
	v3f m_player_pos;
	v3f m_player_speed;
	v3f m_player_platform_delta;
	f32 m_player_gap = 0.0f;
	u16 m_platform_id = 0;
	/// Times the server moved the player, and by how far in total
	u32 m_server_moves = 0;
	f32 m_server_move_worst = 0.0f;
	f32 m_server_move_last = 0.0f;
	/// |Δspeed| of the player per frame
	std::deque<f32> m_player_jerk;
	/// Distance between the player and the model drawn for them, in blocks
	std::deque<f32> m_model_lag;
	/// Frame-to-frame change of that distance: what the eye sees as shaking
	std::deque<f32> m_model_wobble;
	f32 m_model_lag_last = 0.0f;
	bool m_has_model = false;
	/// Change of the camera's own speed, to tell a shaking model from a
	/// shaking camera
	std::deque<f32> m_camera_jerk;
	v3f m_camera_pos;
	v3f m_camera_speed;
	bool m_has_camera = false;
	f32 m_player_jerk_max = 0.0f;
	bool m_has_player = false;

	// --- frames ---
	std::deque<f32> m_frame_times;
	f32 m_now = 0.0f;
	u64 m_frames = 0;

	bool m_logging = false;
	std::ofstream m_frame_log;
	std::ofstream m_packet_log;
};

/// The instrument of the running client, null when switched off
extern std::unique_ptr<NetDiagnostics> g_netdiag;

/// Creates or destroys the instrument to match the settings
void update_net_diagnostics();
