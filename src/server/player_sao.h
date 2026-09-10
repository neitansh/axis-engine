// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2013-2020 Minetest core developers & community

#pragma once

#include "constants.h"
#include "inventorymanager.h" // InventoryLocation
#include "metadata.h"
#include "network/networkprotocol.h"
#include "avatar.h"
#include "unit_sao.h"
#include "util/numeric.h"
#include <set>

/*
	PlayerSAO needs some internals exposed.
*/

class LagPool
{
	float m_pool = 15.0f;
	float m_max = 15.0f;

public:
	LagPool() = default;

	void setMax(float new_max)
	{
		m_max = new_max;
		if (m_pool > new_max)
			m_pool = new_max;
	}

	void add(float dtime)
	{
		m_pool -= dtime;
		if (m_pool < 0)
			m_pool = 0;
	}

	void empty() { m_pool = m_max; }

	bool grab(float dtime)
	{
		if (dtime <= 0)
			return true;
		if (m_pool + dtime > m_max)
			return false;
		m_pool += dtime;
		return true;
	}
};

/**
 * What a drop costs, in hit points.
 *
 * The arithmetic the client has always used for this, moved to where the
 * server can do it too: below the tolerance a fall costs nothing, and what is
 * left of the speed is what it costs. Kept out of the class on purpose — it is
 * four numbers in and one out, and that is a thing a test can hold.
 *
 * @param drop     how far they came down, in blocks
 * @param gravity  blocks per second squared
 * @param factor   what the ground landed on and the player's armour make of
 *                 it; zero means a fall costs nothing here
 * @param pushed   downward speed the server itself gave them, blocks a second
 * @param hp_max   never more than the whole of them
 */
u16 fallDamageFromDrop(f32 drop, f32 gravity, f32 factor, f32 pushed, u16 hp_max);

/**
 * How high a jump could have carried a body by now, above where it left the
 * ground.
 *
 * A body thrown upward and left alone traces one curve and no other. Negative
 * once the jump is spent — by then it is below where it started, and going on
 * down.
 *
 * @param jump_speed what it left the ground with
 * @param gravity    in the same units of length as the speed
 * @param t          seconds since it left
 */
f32 jumpReachAfter(f32 jump_speed, f32 gravity, f32 t);

class RemotePlayer;

class PlayerSAO : public UnitSAO
{
public:
	PlayerSAO(ServerEnvironment *env_, RemotePlayer *player_, session_t peer_id_,
			bool is_singleplayer);

	ActiveObjectType getType() const override { return ACTIVEOBJECT_TYPE_PLAYER; }
	ActiveObjectType getSendType() const override { return ACTIVEOBJECT_TYPE_GENERIC; }
	std::string getDescription() override;

	/*
		Active object <-> environment interface
	*/

	void addedToEnvironment(u32 dtime_s) override;
	void removingFromEnvironment() override;
	bool isStaticAllowed() const override { return false; }
	bool shouldUnload() const override { return false; }
	std::string getClientInitializationData(u16 protocol_version) override;
	void getStaticData(std::string *result) const override;
	void step(float dtime, bool send_recommended) override;
	void setBasePosition(v3f position);
	void setPos(const v3f &pos) override;
	void addPos(const v3f &added_pos) override;
	void moveTo(v3f pos, bool continuous) override;
	void setPlayerYaw(const float yaw);
	std::string getGUID() const override { return m_player_name; }

	void notifyObjectPropertiesModified() override;

	/**
	 * Play a track on this player.
	 *
	 * While avatars are on, a track asked for by frame numbers is refused.
	 * The engine's character has named tracks, and frame ranges written for
	 * another model land wherever they land — in practice they lay the player
	 * out on the ground. Refusing leaves them standing and says in the log
	 * what to call instead. Which track plays is still the game's to decide;
	 * only the way of naming it changed.
	 */
	void setAnimation(const scene::TrackId &track,
			scene::TrackAnimSpec anim_spec) override;

	/**
	 * Put the engine's own character back on the player.
	 *
	 * Does nothing while player avatars are off. While they are on, model,
	 * texture and size are the engine's and a game cannot set them: it may
	 * still call set_properties, but these fields are put back before anyone
	 * is told about the change. See doc/avatar.md §2.
	 */
	void enforceAvatar();

	/// The body texture this player wears, as the client will resolve it.
	/// Games need the name for the arms a player sees in front of themselves.
	std::string getAvatarTexture() const;

	/// What this player wears. Empty while avatars are off.
	const AvatarLook &getAvatarLook() const { return m_avatar; }

	/// Tell the clients that see this player what they should draw on them.
	std::string generateSetAvatarCommand() const;
	// Data should not be sent at player initialization
	void setPlayerYawAndSend(const float yaw);
	void setLookPitch(const float pitch);
	// Data should not be sent at player initialization
	void setLookPitchAndSend(const float pitch);
	f32 getLookPitch() const { return m_pitch; }
	f32 getRadLookPitch() const { return m_pitch * core::DEGTORAD; }
	// Deprecated
	f32 getRadLookPitchDep() const { return -1.0 * m_pitch * core::DEGTORAD; }
	void setFov(const float pitch);
	f32 getFov() const { return m_fov; }
	void setWantedRange(const s16 range);
	s16 getWantedRange() const { return m_wanted_range; }
	void setCameraInverted(bool camera_inverted) { m_camera_inverted = camera_inverted; }
	bool getCameraInverted() const { return m_camera_inverted; }

	/**
	 * Notes the moving object the player says they stand on, and where on it.
	 *
	 * Where this ends up is not the player's own screen but everybody else's:
	 * a viewer draws a rider against the object rather than against the world.
	 * That makes it the one thing a client says about itself that decides
	 * where others see it, so it is checked before it is believed, and a claim
	 * that does not hold is dropped rather than passed on. See the
	 * implementation for what "holds" means.
	 *
	 * Call this after the position from the same packet has been accepted:
	 * the check compares the two, and comparing against a position that is
	 * about to be rolled back would answer the wrong question.
	 */
	void setRide(u16 ride_id, v3f ride_offset);

	/*
		Interaction interface
	*/

	u32 punch(v3f dir, const ToolCapabilities &toolcap, ServerActiveObject *puncher,
			float time_from_last_punch, u16 initial_wear = 0) override;
	void rightClick(ServerActiveObject *clicker) override;
	void setHP(s32 hp, const PlayerHPChangeReason &reason) override
	{
		return setHP(hp, reason, false);
	}
	void setHP(s32 hp, const PlayerHPChangeReason &reason, bool from_client);
	void setHPRaw(u16 hp) { m_hp = hp; }
	u16 getBreath() const { return m_breath; }
	void setBreath(const u16 breath, bool send = true);
	void respawn();

	/*
		Inventory interface
	*/
	Inventory *getInventory() const override;
	InventoryLocation getInventoryLocation() const override;
	void setInventoryModified() override {}
	std::string getWieldList() const override { return "main"; }
	u16 getWieldIndex() const override;
	ItemStack getWieldedItem(ItemStack *selected, ItemStack *hand = nullptr) const override;
	bool setWieldedItem(const ItemStack &item) override;

	/*
		PlayerSAO-specific
	*/

	void disconnected();

	void setPlayer(RemotePlayer *player) { m_player = player; }
	RemotePlayer *getPlayer() { return m_player; }
	session_t getPeerID() const;

	// Cheat prevention

	v3f getLastGoodPosition() const { return m_last_good_position; }
	float resetTimeFromLastPunch()
	{
		float r = m_time_from_last_punch;
		m_time_from_last_punch = 0;
		return r;
	}
	void noCheatDigStart(const v3s16 &p)
	{
		m_nocheat_dig_pos = p;
		m_nocheat_dig_time = 0;
	}
	v3s16 getNoCheatDigPos() { return m_nocheat_dig_pos; }
	float getNoCheatDigTime() { return m_nocheat_dig_time; }
	void noCheatDigEnd() { m_nocheat_dig_pos = v3s16(32767, 32767, 32767); }
	LagPool &getDigPool() { return m_dig_pool; }
	/**
	 * Take one use, place or activate out of the player's allowance.
	 *
	 * Digging has its own pool and punching its own timer; using an item had
	 * neither, so a client could set off as many of them as it could fit in a
	 * packet. One player emptying an inventory into a single server step is
	 * not a hand moving quickly — it is a whole game's worth of work asked for
	 * at once, and whatever the items do, the server does all of it.
	 *
	 * @return false when the action is coming faster than anyone could ask for
	 *         it, and should be dropped.
	 */
	bool grabInteraction();
	void setMaxSpeedOverride(const v3f &vel);
	/**
	 * Work out how fast the player is going, from where they have been.
	 *
	 * Call this once the position from a packet has been settled. See the
	 * implementation for why the speed the packet itself carries is not it.
	 */
	void measureSpeed();
	/**
	 * Watch the ground under the player: what they land from, and what keeps
	 * them up when nothing should.
	 *
	 * Both questions are the same question — is anything holding this player —
	 * asked once a step, which is why they are answered in one place.
	 */
	void watchFooting(float dtime);
	/**
	 * Weigh the position the player just claimed.
	 *
	 * @return the name of what they were caught at, ready for on_cheat, or
	 *         nullptr when the claim held up. The position is put back to the
	 *         last one that did whenever a name is returned.
	 */
	const char *checkMovementCheat();

	// Other

	void updatePrivileges(const std::set<std::string> &privs)
	{
		m_privs = privs;
	}

	inline void setNewPlayer() { m_is_new_player = true; }
	inline bool isNewPlayer()  { return m_is_new_player; }

	bool getCollisionBox(aabb3f *toset) const override;
	bool getSelectionBox(aabb3f *toset) const override;
	bool collideWithObjects() const override { return true; }

	void finalize(RemotePlayer *player, const std::set<std::string> &privs);

	v3f getEyePosition() const { return getBasePosition() + getEyeOffset(); }
	v3f getEyeOffset() const;
	float getZoomFOV() const;

	inline SimpleMetadata &getMeta() { return m_meta; }

private:
	std::string getPropertyPacket();
	void unlinkPlayerSessionAndSave();
	std::string generateUpdatePhysicsOverrideCommand() const;
	/// Whether a ride claim describes something that can actually be ridden,
	/// and a place on it the player could actually be. See setRide().
	bool rideHolds(ServerActiveObject *ride, const v3f &offset) const;
	/// Whether the straight way between two positions runs through solid
	/// ground. See the implementation for why this is the one movement
	/// question with an answer that does not depend on the game.
	bool wentThroughSolid(const v3f &from, const v3f &to) const;
	/**
	 * Whether anything is holding the player up: ground under their feet,
	 * something to climb or swim in, or an object they are standing on.
	 *
	 * @param soft set when the only thing holding them is liquid or something
	 *             climbable — support enough to stand on, and not a landing.
	 */
	bool isSupported(bool *soft) const;
	/// What the fall the player has just finished cost them, by the same
	/// arithmetic the client uses. See the implementation.
	u16 fallDamage() const;

	RemotePlayer *m_player = nullptr;
	// Extra variable because during shutdown m_player is unavailable, but we still need to know.
	std::string m_player_name; ///< used as GUID
	AvatarLook m_avatar;
	bool m_warned_frame_animation = false;
	session_t m_peer_id_initial = 0; ///< only used to initialize RemotePlayer

	// Cheat prevention
	LagPool m_dig_pool;
	LagPool m_move_pool;
	LagPool m_use_pool;
	v3f m_last_good_position;
	float m_time_from_last_teleport = 0.0f;
	float m_time_from_last_punch = 0.0f;
	v3s16 m_nocheat_dig_pos = v3s16(32767, 32767, 32767);
	float m_nocheat_dig_time = 0.0f;
	float m_max_speed_override_time = 0.0f;
	v3f m_max_speed_override = v3f(0.0f, 0.0f, 0.0f);

	// Timers
	IntervalLimiter m_breathing_interval;
	IntervalLimiter m_drowning_interval;
	IntervalLimiter m_node_hurt_interval;

	bool m_position_not_sent = false;
	/// What the player stands on, 0 for none, and where on it. Only ever set
	/// through setRide(), which is where a client's claim is weighed.
	u16 m_ride_id = 0;
	v3f m_ride_offset;
	/// The last claim that did not hold up. Kept so that a client repeating an
	/// impossible one twenty times a second is reported once, not twenty times.
	u16 m_ride_refused = 0;
	/// Where the player was when their speed was last worked out, and how long
	/// ago that was. See measureSpeed().
	v3f m_speed_reference;
	float m_time_from_last_speed = 0.0f;
	/// The current descent, as the server has watched it: the height it
	/// started from, how deep it has got, and how long ago it last went on.
	/// See allowedFallDamage().
	float m_fall_peak_y = 0.0f;
	float m_fall_depth = 0.0f;
	/// The flight the player is in the middle of: how long since the ground
	/// let go of them, where it let go, and whether the ground in question
	/// was something that throws people. See watchFooting().
	float m_air_time = 0.0f;
	float m_air_from_y = 0.0f;
	bool m_air_bouncy = false;
	/// Whether something was holding them up a step ago. A player who arrives
	/// standing has not just landed, so this starts true.
	bool m_was_supported = true;
	/**
	 * Seconds since the last position packet went out.
	 *
	 * Clients rebuild motion from the interval each packet carries, so the
	 * interval has to be the real gap between packets. A player is only sent
	 * when they moved, which is not every send tick, and claiming the
	 * recommended interval regardless makes the client replay the timeline
	 * faster than packets arrive - its buffer empties and the model stutters.
	 */
	float m_last_sent_position_timer = 0.0f;

	// Cached privileges for enforcement
	std::set<std::string> m_privs;
	const bool m_is_singleplayer;
	bool m_is_new_player = false;

	u16 m_breath = PLAYER_MAX_BREATH_DEFAULT;
	f32 m_pitch = 0.0f;
	f32 m_fov = 0.0f;
	s16 m_wanted_range = 0.0f;

	bool m_camera_inverted = false; // this is not stored in the player db

	SimpleMetadata m_meta;

public:
	struct {
		bool breathing : 1;
		bool drowning : 1;
		bool node_damage : 1;
	} m_flags = {true, true, true};

	bool m_physics_override_sent = false;
};

struct PlayerHPChangeReason
{
	enum Type : u8
	{
		SET_HP,
		SET_HP_MAX, // internal type to allow distinguishing hp reset and damage (for effects)
		PLAYER_PUNCH,
		FALL,
		NODE_DAMAGE,
		DROWNING,
		RESPAWN
	};

	Type type = SET_HP;
	bool from_mod = false;
	int lua_reference = -1;

	// For PLAYER_PUNCH
	ServerActiveObject *object = nullptr;
	// For NODE_DAMAGE and DROWNING
	std::string node;
	v3s16 node_pos;

	inline bool hasLuaReference() const { return lua_reference >= 0; }

	bool setTypeFromString(const std::string &typestr)
	{
		if (typestr == "set_hp")
			type = SET_HP;
		else if (typestr == "punch")
			type = PLAYER_PUNCH;
		else if (typestr == "fall")
			type = FALL;
		else if (typestr == "node_damage")
			type = NODE_DAMAGE;
		else if (typestr == "drown")
			type = DROWNING;
		else if (typestr == "respawn")
			type = RESPAWN;
		else
			return false;

		return true;
	}

	std::string getTypeAsString() const
	{
		switch (type) {
		case PlayerHPChangeReason::SET_HP:
		case PlayerHPChangeReason::SET_HP_MAX:
			return "set_hp";
		case PlayerHPChangeReason::PLAYER_PUNCH:
			return "punch";
		case PlayerHPChangeReason::FALL:
			return "fall";
		case PlayerHPChangeReason::NODE_DAMAGE:
			return "node_damage";
		case PlayerHPChangeReason::DROWNING:
			return "drown";
		case PlayerHPChangeReason::RESPAWN:
			return "respawn";
		default:
			return "?";
		}
	}

	PlayerHPChangeReason(Type type) : type(type) {}

	PlayerHPChangeReason(Type type, ServerActiveObject *object) :
			type(type), object(object)
	{
	}

	PlayerHPChangeReason(Type type, std::string node, v3s16 node_pos) :
			type(type), node(std::move(node)), node_pos(node_pos) {}
};
