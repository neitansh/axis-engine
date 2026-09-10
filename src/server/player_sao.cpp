// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2013-2020 Minetest core developers & community

#include "player_sao.h"
#include "avatar.h"
#include "util/string.h"
#include "itemgroup.h"
#include "luaentity_sao.h"
#include "nodedef.h"
#include "remoteplayer.h"
#include "scripting_server.h"
#include "server.h"
#include "serverenvironment.h"
#include "settings.h"
#include "util/serialize.h"
#include "voxelalgorithms.h"

/// How far from an object a player may sit and still be said to stand on it,
/// in blocks. A deck is wide, so this is generous; what it rules out is a
/// client naming something across the map to have their model drawn there.
static constexpr float PLAYER_RIDE_RANGE = 8.0f;

/**
 * How stale the deck under a rider may be, in seconds.
 *
 * The offset is worked out on the client because only there are the player and
 * the deck seen at one instant. The player half of that instant reaches us in
 * the same packet as the offset, so the only thing that can have moved since is
 * the deck — by however far it travels while the packet is in the air. That is
 * a generous half second, and it is the whole of what "the offset is not the
 * past" is allowed to mean.
 */
static constexpr float PLAYER_RIDE_SLACK_TIME = 0.5f;

/**
 * And a fixed allowance on top of it, in blocks.
 *
 * A deck standing still still needs a little: the position travels in
 * hundredths of a block, the player takes steps on the deck between packets,
 * and neither is worth an accusation.
 */
static constexpr float PLAYER_RIDE_SLACK_DIST = 1.0f;

/**
 * How fast a player may use, place or activate things, in actions per second.
 *
 * Not a limit on the hand — a hand is faster than any check here should care
 * about — but a ceiling on what a single burst of packets can set off.
 */
static constexpr float PLAYER_INTERACT_RATE = 20.0f;

/**
 * And how much of that allowance may be spent at once, in seconds' worth.
 *
 * A link that stalls and then delivers everything together is ordinary, and
 * half a second of held-up actions has to go through. A client that saved up
 * for a minute in order to spend it all in one server step is not that.
 */
static constexpr float PLAYER_INTERACT_BURST = 0.5f;

/**
 * How much travel one position packet may carry beyond the time it took to
 * arrive, in seconds' worth.
 *
 * The server counts the gap between packets in whole server steps and the
 * client walks it out frame by frame, so the two never agree exactly. Half a
 * second is for that disagreement; at ordinary walking speed it is a couple of
 * blocks, which is room to breathe and nowhere to hide.
 */
static constexpr float PLAYER_MOVE_STEP_SLACK = 0.5f;

/**
 * How long a measured speed stands without a fresh position, in seconds.
 *
 * A client stops sending position packets exactly when there is nothing to
 * report — position, speed and keys all unchanged. So silence means standing
 * still, and a speed left over from before the silence would say the opposite:
 * a player who came to a halt would go on being reported at a run, for as long
 * as they stood there, to everything that asks how fast they are moving.
 */
static constexpr float PLAYER_SPEED_STALE = 0.5f;

u16 fallDamageFromDrop(f32 drop, f32 gravity, f32 factor, f32 pushed, u16 hp_max)
{
	if (drop <= 0.0f || factor <= 0.0f)
		return 0;

	// How fast they were going when they arrived. Nothing speeds a body up on
	// its own but falling, and the drop is measured from the top of it, where
	// they were not moving — so this is the whole of it. Plus whatever pushed
	// them, because that was not theirs to invent.
	const f32 speed = std::sqrt(MYMAX(0.0f, 2.0f * gravity * drop)) +
			std::fabs(pushed);

	// Fourteen blocks a second, exactly as on the client — which is about ten
	// blocks of free fall, whatever the comment there says.
	const f32 tolerance = 14.0f;
	const f32 damage = speed * factor - tolerance;
	if (damage <= 0.0f)
		return 0;

	return (u16)MYMIN(damage + 0.5f, (f32)hp_max);
}

f32 jumpReachAfter(f32 jump_speed, f32 gravity, f32 t)
{
	return jump_speed * t - 0.5f * gravity * t * t;
}

PlayerSAO::PlayerSAO(ServerEnvironment *env_, RemotePlayer *player_, session_t peer_id_,
		bool is_singleplayer):
	UnitSAO(env_, v3f(0,0,0)),
	m_player(player_),
	m_player_name(player_->getName()),
	m_peer_id_initial(peer_id_),
	m_is_singleplayer(is_singleplayer)
{
	SANITY_CHECK(m_peer_id_initial != PEER_ID_INEXISTENT);

	m_prop.hp_max = PLAYER_MAX_HP_DEFAULT;
	m_prop.breath_max = PLAYER_MAX_BREATH_DEFAULT;
	m_prop.physical = false;
	m_prop.collisionbox = aabb3f(-0.3f, 0.0f, -0.3f, 0.3f, 1.77f, 0.3f);
	m_prop.selectionbox = aabb3f(-0.3f, 0.0f, -0.3f, 0.3f, 1.77f, 0.3f);
	m_prop.pointable = PointabilityType::POINTABLE;
	// Start of default appearance, this should be overwritten by Lua
	m_prop.visual = OBJECTVISUAL_UPRIGHT_SPRITE;
	m_prop.visual_size = v3f(1, 2, 1);
	m_prop.textures.clear();
	m_prop.textures.emplace_back("player.png");
	m_prop.textures.emplace_back("player_back.png");
	m_prop.colors.clear();
	m_prop.spritediv = v2s16(1,1);
	m_prop.eye_height = 1.625f;
	// End of default appearance
	m_prop.is_visible = true;
	m_prop.backface_culling = false;
	m_prop.makes_footstep_sound = true;
	m_prop.stepheight = PLAYER_DEFAULT_STEPHEIGHT * BS;
	m_prop.show_on_minimap = true;
	m_hp = m_prop.hp_max;
	m_breath = m_prop.breath_max;
	// Disable zoom in survival mode using a value of 0
	m_prop.zoom_fov = g_settings->getBool("creative_mode") ? 15.0f : 0.0f;

	if (!g_settings->getBool("enable_damage"))
		m_armor_groups["immortal"] = 1;
}

// PlayerSAO::~PlayerSAO(): eventually deleted by `ActiveObjectMgr::removeObject`

void PlayerSAO::finalize(RemotePlayer *player, const std::set<std::string> &privs)
{
	assert(player);
	m_player = player;
	m_privs = privs;
}

v3f PlayerSAO::getEyeOffset() const
{
	return v3f(0, BS * m_prop.eye_height, 0);
}

std::string PlayerSAO::getDescription()
{
	return std::string("player ") + m_player->getName();
}

// Called after id has been set and has been inserted in environment
std::string PlayerSAO::getAvatarTexture() const
{
	// Until a signed manifest travels with the player, everyone wears the
	// look the engine ships with.
	return AVATAR_DEFAULT_TEXTURE;
}

/**
 * Worn things while there is nothing to wear from yet.
 *
 * Scaffolding: until a signed manifest travels with the player there is no
 * wardrobe to read, and the drawing of worn things still has to be built and
 * looked at. Goes away with the manifest (doc/avatar.md §5). Format is a list
 * of `bone mesh texture`, separated by commas.
 */
static std::vector<AvatarPart> testParts()
{
	std::vector<AvatarPart> parts;
	for (const std::string &entry : str_split(g_settings->get("player_avatars_test"), ',')) {
		auto words = str_split(std::string(trim(entry)), ' ');
		if (words.size() != 3)
			continue;
		parts.push_back(AvatarPart{words[0], words[1], words[2]});
		if (parts.size() >= AvatarLook::MAX_PARTS)
			break;
	}
	return parts;
}

std::string PlayerSAO::generateSetAvatarCommand() const
{
	std::ostringstream os(std::ios::binary);
	writeU8(os, AO_CMD_SET_AVATAR);
	m_avatar.serialize(os);
	return os.str();
}

void PlayerSAO::enforceAvatar()
{
	if (!avatarsEnabled())
		return;

	AvatarLook look;
	look.body_texture = getAvatarTexture();
	look.parts = testParts();
	if (look != m_avatar) {
		m_avatar = look;
		// The look is not an object property: properties belong to the game,
		// and this does not. It rides the object's own channel, which no mod
		// can write to.
		m_messages_out.emplace(getId(), true, generateSetAvatarCommand());
	}

	m_prop.visual = OBJECTVISUAL_MESH;
	m_prop.mesh = AVATAR_MESH;
	m_prop.textures.assign(1, getAvatarTexture());
	m_prop.colors.clear();

	// Drawn to the height of the collision box, never the other way round:
	// the box is the game's and is picked for the world it stands in, while a
	// character drawn taller than it leaves a head nobody can hit.
	const float box = m_prop.collisionbox.MaxEdge.Y - m_prop.collisionbox.MinEdge.Y;
	const float scale = box > 0.0f ? box / AVATAR_MODEL_HEIGHT : 1.0f;
	m_prop.visual_size = v3f(scale, scale, scale);
}

void PlayerSAO::setAnimation(const scene::TrackId &track,
		scene::TrackAnimSpec anim_spec)
{
	if (avatarsEnabled() && std::holds_alternative<u16>(track)) {
		if (!m_warned_frame_animation) {
			m_warned_frame_animation = true;
			warningstream << "Player avatars are on and the game asked for an "
					"animation by frame numbers on player \"" << m_player_name
					<< "\". The engine's character has named tracks: call "
					"play_animation(\"walk\") and the like instead."
					<< std::endl;
		}
		return;
	}

	UnitSAO::setAnimation(track, std::move(anim_spec));
}

void PlayerSAO::notifyObjectPropertiesModified()
{
	enforceAvatar();
	UnitSAO::notifyObjectPropertiesModified();
}

void PlayerSAO::addedToEnvironment(u32 dtime_s)
{
	ServerActiveObject::addedToEnvironment(dtime_s);
	enforceAvatar();
	m_player->setPlayerSAO(this);
	m_player->setPeerId(m_peer_id_initial);
	m_peer_id_initial = PEER_ID_INEXISTENT; // don't try to use it again.
	m_last_good_position = getBasePosition();
	// Where they came in, so the first measured speed is the distance from
	// there and not from the origin of the world, and the first fall is
	// counted from the ground they arrived on.
	m_speed_reference = getBasePosition();
	m_fall_peak_y = getBasePosition().Y;
}

// Called before removing from environment
void PlayerSAO::removingFromEnvironment()
{
	ServerActiveObject::removingFromEnvironment();

	// If this fails, fix the ActiveObjectMgr code in ServerEnvironment
	SANITY_CHECK(m_player->getPlayerSAO() == this);

	unlinkPlayerSessionAndSave();
	for (u32 attached_particle_spawner : m_attached_particle_spawners) {
		m_env->deleteParticleSpawner(attached_particle_spawner, false);
	}
}

std::string PlayerSAO::getClientInitializationData(u16 protocol_version)
{
	std::ostringstream os(std::ios::binary);

	// Protocol >= 15
	writeU8(os, 1); // version
	os << serializeString16(m_player->getName()); // name
	writeU8(os, 1); // is_player
	writeS16(os, getId()); // id
	writeV3F32(os, getBasePosition());
	writeV3F32(os, m_rotation);
	writeU16(os, getHP());

	std::ostringstream msg_os(std::ios::binary);
	int message_count = 0;
	auto append_message = [&](const std::string &message) {
		msg_os << serializeString32(message);
		++message_count;
	};
	append_message(getPropertyPacket());
	if (avatarsEnabled())
		append_message(generateSetAvatarCommand());
	append_message(generateUpdateArmorGroupsCommand());
	for (const auto &[track, anim] : getAnimation().tracks) {
		if (anim.state != TrackAnimation::State::STOPPED)
			append_message(generateUpdateAnimationCommand(track));
	}
	for (const auto &it : m_bone_override) {
		append_message(generateUpdateBoneOverrideCommand(
			it.first, it.second));
	}
	append_message(generateUpdateAttachmentCommand());
	append_message(generateUpdatePhysicsOverrideCommand());

	for (const auto &id : getAttachmentChildIds()) {
		if (ServerActiveObject *obj = m_env->getActiveObject(id)) {
			append_message(obj->generateUpdateInfantCommand(
				id, protocol_version));
		}
	}

	writeU8(os, message_count);
	std::string serialized = msg_os.str();
	os.write(serialized.c_str(), serialized.size());

	// return result
	return os.str();
}

void PlayerSAO::getStaticData(std::string * result) const
{
	FATAL_ERROR("This function shall not be called for PlayerSAO");
}

void PlayerSAO::step(float dtime, bool send_recommended)
{
	bool not_immortal = !isImmortal();

	if (not_immortal && m_flags.drowning
			&& m_drowning_interval.step(dtime, 2.0f)) {
		// Get nose/mouth position, approximate with eye position
		v3s16 p = floatToInt(getEyePosition(), BS);
		MapNode n = m_env->getMap().getNode(p);
		const ContentFeatures &c = m_env->getCrateDef()->ndef()->get(n);
		// If node generates drown
		if (c.drowning > 0 && m_hp > 0) {
			if (m_breath > 0)
				setBreath(m_breath - 1);

			// No more breath, damage player
			if (m_breath == 0) {
				std::string nodename = c.name;
				PlayerHPChangeReason reason(PlayerHPChangeReason::DROWNING, nodename, p);
				setHP(m_hp - c.drowning, reason);
			}
		}
	}

	if (not_immortal && m_flags.breathing
			&& m_breathing_interval.step(dtime, 0.5f)) {
		// Get nose/mouth position, approximate with eye position
		v3s16 p = floatToInt(getEyePosition(), BS);
		MapNode n = m_env->getMap().getNode(p);
		const ContentFeatures &c = m_env->getCrateDef()->ndef()->get(n);
		// If player is alive & not drowning & not in ignore & not immortal, breathe
		if (m_breath < m_prop.breath_max && c.drowning == 0 &&
				n.getContent() != CONTENT_IGNORE && m_hp > 0)
			setBreath(m_breath + 1);
	}

	if (not_immortal && m_flags.node_damage
			&& m_node_hurt_interval.step(dtime, 1.0f)) {
		u32 damage_per_second = 0;
		std::string nodename;
		v3s16 node_pos;
		// Lowest and highest damage points are 0.1 within collisionbox
		float dam_top = m_prop.collisionbox.MaxEdge.Y - 0.1f;

		// Sequence of damage points, starting 0.1 above feet and progressing
		// upwards in 1 node intervals, stopping below top damage point.
		for (float dam_height = 0.1f; dam_height < dam_top; dam_height++) {
			v3s16 p = floatToInt(getBasePosition() +
				v3f(0.0f, dam_height * BS, 0.0f), BS);
			MapNode n = m_env->getMap().getNode(p);
			const ContentFeatures &c = m_env->getCrateDef()->ndef()->get(n);
			if (c.damage_per_second > damage_per_second) {
				damage_per_second = c.damage_per_second;
				nodename = c.name;
				node_pos = p;
			}
		}

		// Top damage point
		v3s16 ptop = floatToInt(getBasePosition() +
			v3f(0.0f, dam_top * BS, 0.0f), BS);
		MapNode ntop = m_env->getMap().getNode(ptop);
		const ContentFeatures &c = m_env->getCrateDef()->ndef()->get(ntop);
		if (c.damage_per_second > damage_per_second) {
			damage_per_second = c.damage_per_second;
			nodename = c.name;
			node_pos = ptop;
		}

		if (damage_per_second != 0 && m_hp > 0) {
			s32 newhp = (s32)m_hp - (s32)damage_per_second;
			PlayerHPChangeReason reason(PlayerHPChangeReason::NODE_DAMAGE, nodename, node_pos);
			setHP(newhp, reason);
		}
	}

	if (!m_properties_sent) {
		m_properties_sent = true;
		std::string str = getPropertyPacket();
		// create message and add to list
		m_messages_out.emplace(getId(), true, str);
		m_env->getScriptIface()->player_event(this, "properties_changed");
	}

	// If attached, check that our parent is still there. If it isn't, detach.
	if (m_attachment_parent_id && !getParent()) {
		// This is handled when objects are removed from the map
		warningstream << "PlayerSAO::step() id=" << m_id <<
			" is attached to nonexistent parent. This is a bug." << std::endl;
		clearParentAttachment();
		setPos(m_last_good_position);
	}

	//dstream<<"PlayerSAO::step: dtime: "<<dtime<<std::endl;

	// Set lag pool maximums based on estimated lag
	const float LAG_POOL_MIN = 5.0f;
	float lag_pool_max = m_env->getMaxLagEstimate() * 2.0f;
	if(lag_pool_max < LAG_POOL_MIN)
		lag_pool_max = LAG_POOL_MIN;
	m_dig_pool.setMax(lag_pool_max);
	m_move_pool.setMax(lag_pool_max);
	// Using an item is not paced by the link the way digging and moving are:
	// its allowance is a fixed half second and does not grow with lag, or a
	// player on a bad line would be handed a bigger burst than anyone needs.
	m_use_pool.setMax(PLAYER_INTERACT_BURST);

	// Increment cheat prevention timers
	m_dig_pool.add(dtime);
	m_move_pool.add(dtime);
	m_use_pool.add(dtime);
	m_time_from_last_speed += dtime;
	m_time_from_last_teleport += dtime;
	m_time_from_last_punch += dtime;
	m_nocheat_dig_time += dtime;
	m_max_speed_override_time = MYMAX(m_max_speed_override_time - dtime, 0.0f);

	// Nothing has arrived for a while, so measure what that silence means. For
	// a player standing still it comes out at nothing, which is the truth; for
	// one whose packets went missing it comes out as the average over the whole
	// gap, which is the best the record allows.
	if (m_time_from_last_speed > PLAYER_SPEED_STALE)
		measureSpeed();

	// Counted here rather than per packet: what this answers is about the
	// ground under somebody, and the ground is there whether or not the client
	// is talking. Not behind the anticheat flag, either — one half of it is
	// fall damage, which is a rule of the world and not a suspicion about the
	// player. Turning the checks off must not turn gravity off with them.
	watchFooting(dtime);

	// Each frame, parent position is copied if the object is attached,
	// otherwise it's calculated normally.
	// If the object gets detached this comes into effect automatically from
	// the last known origin.
	if (auto *parent = getParent()) {
		v3f pos = parent->getBasePosition();
		m_last_good_position = pos;
		setBasePosition(pos);

		if (m_player)
			m_player->setSpeed(v3f());
	}

	m_last_sent_position_timer += dtime;

	if (!send_recommended)
		return;

	if (m_position_not_sent) {
		m_position_not_sent = false;

		// The client stretches the movement over exactly the span named here,
		// so it has to be the span that really passed. A player standing
		// still sends nothing, and the next packet after that is further away
		// than the recommended interval.
		const float update_interval = rangelim(m_last_sent_position_timer,
				m_env->getSendRecommendedInterval(), 1.0f);

		m_last_sent_position_timer = 0.0f;
		v3f pos;
		// When attached, the position is only sent to clients where the
		// parent isn't known
		if (isAttached())
			pos = m_last_good_position;
		else
			pos = getBasePosition();

		// Where they sit on what carries them. Their world position is that
		// object's past; the offset is not, because it barely changes while
		// they stand there.
		u16 ride_id = 0;
		v3f ride_offset;

		// Whether the claim holds was settled when it arrived, in setRide().
		// All that is left here is that the deck may have been removed since.
		if (m_ride_id != 0 && !isAttached()) {
			ServerActiveObject *ride = m_env->getActiveObject(m_ride_id);

			if (ride && !ride->isGone()) {
				ride_id = m_ride_id;
				ride_offset = m_ride_offset;
			}
		}

		std::string str = generateUpdatePositionCommand(
			pos,
			v3f(0.0f, 0.0f, 0.0f),
			v3f(0.0f, 0.0f, 0.0f),
			m_rotation,
			true,
			false,
			update_interval,
			ride_id,
			ride_offset
		);
		// create message and add to list
		m_messages_out.emplace(getId(), false, str);
	}

	if (!m_physics_override_sent) {
		m_physics_override_sent = true;
		// create message and add to list
		m_messages_out.emplace(getId(), true, generateUpdatePhysicsOverrideCommand());
	}

	sendOutdatedData();
}

std::string PlayerSAO::generateUpdatePhysicsOverrideCommand() const
{
	if (!m_player) {
		// Will output a format warning client-side
		return "";
	}

	const auto &phys = m_player->physics_override;
	std::ostringstream os(std::ios::binary);
	// command
	writeU8(os, AO_CMD_SET_PHYSICS_OVERRIDE);
	// parameters
	writeF32(os, phys.speed);
	writeF32(os, phys.jump);
	writeF32(os, phys.gravity);
	// MT 0.4.10 legacy: send inverted for default `true` if the server sends nothing
	writeU8(os, !phys.sneak);
	writeU8(os, !phys.sneak_glitch);
	writeU8(os, !phys.new_move);
	// new physics overrides since 5.8.0
	writeF32(os, phys.speed_climb);
	writeF32(os, phys.speed_crouch);
	writeF32(os, phys.liquid_fluidity);
	writeF32(os, phys.liquid_fluidity_smooth);
	writeF32(os, phys.liquid_sink);
	writeF32(os, phys.acceleration_default);
	writeF32(os, phys.acceleration_air);
	writeF32(os, phys.speed_fast);
	writeF32(os, phys.acceleration_fast);
	writeF32(os, phys.speed_walk);
	return os.str();
}

void PlayerSAO::setBasePosition(v3f position)
{
	if (m_player && position != getBasePosition())
		m_player->setDirty(true);

	// This needs to be ran for attachments too
	ServerActiveObject::setBasePosition(position);

	// Updating is not wanted/required for player migration
	if (m_env) {
		m_position_not_sent = true;
	}
}

void PlayerSAO::setPos(const v3f &pos)
{
	if (isAttached())
		return;

	// Send mapblock of target location
	v3s16 blockpos = v3s16(pos.X / MAP_BLOCKSIZE, pos.Y / MAP_BLOCKSIZE, pos.Z / MAP_BLOCKSIZE);
	m_env->getServer()->SendBlock(getPeerID(), blockpos);

	setBasePosition(pos);
	// Movement caused by this command is always valid
	m_last_good_position = getBasePosition();
	m_move_pool.empty();
	m_time_from_last_teleport = 0.0;
	// And the fall they were in the middle of, if any, was not finished by
	// them. A mod snatching somebody out of a drop is a rescue; charging them
	// for the part they had already fallen would make it a sentence.
	m_fall_depth = 0.0f;
	m_fall_peak_y = getBasePosition().Y;
	m_env->getServer()->SendMovePlayer(this);
}

void PlayerSAO::addPos(const v3f &added_pos)
{
	if (isAttached())
		return;

	// Backward compatibility for older clients
	if (m_player->protocol_version < 44) {
		setPos(getBasePosition() + added_pos);
		return;
	}

	// Send mapblock of target location
	v3f pos = getBasePosition() + added_pos;
	v3s16 blockpos = v3s16(pos.X / MAP_BLOCKSIZE, pos.Y / MAP_BLOCKSIZE, pos.Z / MAP_BLOCKSIZE);
	m_env->getServer()->SendBlock(getPeerID(), blockpos);

	setBasePosition(pos);
	// Movement caused by this command is always valid
	m_last_good_position = getBasePosition();
	m_move_pool.empty();
	m_time_from_last_teleport = 0.0;
	// And the fall they were in the middle of, if any, was not finished by
	// them. A mod snatching somebody out of a drop is a rescue; charging them
	// for the part they had already fallen would make it a sentence.
	m_fall_depth = 0.0f;
	m_fall_peak_y = getBasePosition().Y;
	m_env->getServer()->SendMovePlayerRel(getPeerID(), added_pos);
}

void PlayerSAO::moveTo(v3f pos, bool continuous)
{
	if(isAttached())
		return;

	setBasePosition(pos);
	// Movement caused by this command is always valid
	m_last_good_position = getBasePosition();
	m_move_pool.empty();
	m_time_from_last_teleport = 0.0;
	// And the fall they were in the middle of, if any, was not finished by
	// them. A mod snatching somebody out of a drop is a rescue; charging them
	// for the part they had already fallen would make it a sentence.
	m_fall_depth = 0.0f;
	m_fall_peak_y = getBasePosition().Y;
	m_env->getServer()->SendMovePlayer(this);
}

void PlayerSAO::setPlayerYaw(const float yaw)
{
	v3f rotation(0, yaw, 0);
	if (m_player && yaw != m_rotation.Y)
		m_player->setDirty(true);

	// Set player model yaw, not look view
	UnitSAO::setRotation(rotation);
}

void PlayerSAO::setFov(const float fov)
{
	if (m_player && fov != m_fov)
		m_player->setDirty(true);

	m_fov = fov;
}

void PlayerSAO::setWantedRange(const s16 range)
{
	if (m_player && range != m_wanted_range)
		m_player->setDirty(true);

	m_wanted_range = range;
}

void PlayerSAO::setPlayerYawAndSend(const float yaw)
{
	setPlayerYaw(yaw);
	m_env->getServer()->SendMovePlayer(this);
}

void PlayerSAO::setLookPitch(const float pitch)
{
	if (m_player && pitch != m_pitch)
		m_player->setDirty(true);

	m_pitch = pitch;
}

void PlayerSAO::setLookPitchAndSend(const float pitch)
{
	setLookPitch(pitch);
	m_env->getServer()->SendMovePlayer(this);
}

u32 PlayerSAO::punch(v3f dir,
	const ToolCapabilities &toolcap,
	ServerActiveObject *puncher,
	float time_from_last_punch,
	u16 initial_wear)
{
	// No effect if PvP disabled or if immortal
	if (isImmortal() || !g_settings->getBool("enable_pvp")) {
		if (puncher && puncher->getType() == ACTIVEOBJECT_TYPE_PLAYER) {
			// create message and add to list
			sendPunchCommand();
			return 0;
		}
	}

	s32 old_hp = getHP();
	HitParams hitparams = getHitParams(m_armor_groups, toolcap,
			time_from_last_punch, initial_wear);

	PlayerSAO *playersao = m_player->getPlayerSAO();

	bool damage_handled = m_env->getScriptIface()->on_punchplayer(playersao,
				puncher, time_from_last_punch, toolcap, dir,
				hitparams.hp);

	if (!damage_handled) {
		setHP((s32)getHP() - (s32)hitparams.hp,
				PlayerHPChangeReason(PlayerHPChangeReason::PLAYER_PUNCH, puncher));
	} else { // override client prediction
		if (puncher->getType() == ACTIVEOBJECT_TYPE_PLAYER) {
			// create message and add to list
			sendPunchCommand();
		}
	}

	if (puncher) {
		actionstream << puncher->getDescription() << " (id=" << puncher->getId() <<
				", hp=" << puncher->getHP() << ")";
	} else {
		actionstream << "(none)";
	}
	actionstream << " punched " <<
			getDescription() << " (id=" << m_id << ", hp=" << m_hp <<
			"), damage=" << (old_hp - (s32)getHP()) <<
			(damage_handled ? " (handled by Lua)" : "") << std::endl;

	return hitparams.wear;
}

void PlayerSAO::rightClick(ServerActiveObject *clicker)
{
	m_env->getScriptIface()->on_rightclickplayer(this, clicker);
}

void PlayerSAO::setHP(s32 target_hp, const PlayerHPChangeReason &reason, bool from_client)
{
	if (target_hp == m_hp || (m_hp == 0 && target_hp < 0))
		return; // Nothing to do

	// Protect against overflow.
	s32 hp_change = std::max<s64>((s64)target_hp - (s64)m_hp, S32_MIN);

	hp_change = m_env->getScriptIface()->on_player_hpchange(this, hp_change, reason);
	hp_change = std::min<s32>(hp_change, U16_MAX); // Protect against overflow

	s32 hp = (s32)m_hp + hp_change;
	hp = rangelim(hp, 0, U16_MAX);

	if (hp > m_prop.hp_max)
		hp = m_prop.hp_max;

	if (hp < m_hp && isImmortal())
		hp = m_hp; // Do not allow immortal players to be damaged

	// Update properties on death
	if ((hp == 0) != (m_hp == 0))
		m_properties_sent = false;

	if (hp != m_hp) {
		m_hp = hp;
		m_env->getServer()->HandlePlayerHPChange(this, reason);
	} else if (from_client)
		m_env->getServer()->SendPlayerHP(this, true);
}

void PlayerSAO::setBreath(const u16 breath, bool send)
{
	if (m_player && breath != m_breath)
		m_player->setDirty(true);

	m_breath = rangelim(breath, 0, m_prop.breath_max);

	if (send)
		m_env->getServer()->SendPlayerBreath(this);
}

void PlayerSAO::respawn()
{
	infostream << "PlayerSAO::respawn(): Player " << m_player->getName()
			<< " respawns" << std::endl;

	setHP(m_prop.hp_max, PlayerHPChangeReason(PlayerHPChangeReason::RESPAWN));
	setBreath(m_prop.breath_max);

	bool repositioned = m_env->getScriptIface()->on_respawnplayer(this);
	if (!repositioned) {
		// setPos will send the new position to client
		setPos(m_env->getServer()->findSpawnPos());
	}
}

Inventory *PlayerSAO::getInventory() const
{
	return m_player ? &m_player->inventory : nullptr;
}

InventoryLocation PlayerSAO::getInventoryLocation() const
{
	InventoryLocation loc;
	loc.setPlayer(m_player->getName());
	return loc;
}

u16 PlayerSAO::getWieldIndex() const
{
	return m_player->getWieldIndex();
}

ItemStack PlayerSAO::getWieldedItem(ItemStack *selected, ItemStack *hand) const
{
	return m_player->getWieldedItem(selected, hand);
}

bool PlayerSAO::setWieldedItem(const ItemStack &item)
{
	InventoryList *mlist = m_player->inventory.getList(getWieldList());
	if (mlist) {
		mlist->changeItem(m_player->getWieldIndex(), item);
		return true;
	}
	return false;
}

void PlayerSAO::disconnected()
{
	markForRemoval();
	m_player->setPeerId(PEER_ID_INEXISTENT);
}

session_t PlayerSAO::getPeerID() const
{
	// Before adding `this` to the server env, m_player is still nullptr.
	return m_player ? m_player->getPeerId() : PEER_ID_INEXISTENT;
}

void PlayerSAO::unlinkPlayerSessionAndSave()
{
	assert(m_player->getPlayerSAO() == this);
	m_env->savePlayer(m_player);
	m_env->removePlayer(m_player);
}

std::string PlayerSAO::getPropertyPacket()
{
	m_prop.is_visible = (true);
	return generateSetPropertiesCommand(m_prop);
}

void PlayerSAO::setMaxSpeedOverride(const v3f &vel)
{
	if (m_max_speed_override_time == 0.0f)
		m_max_speed_override = vel;
	else
		m_max_speed_override += vel;
	if (m_player) {
		float accel = MYMIN(m_player->movement_acceleration_default,
				m_player->movement_acceleration_air);
		m_max_speed_override_time = m_max_speed_override.getLength() / accel / BS;
	}
}

void PlayerSAO::setRide(u16 ride_id, v3f ride_offset)
{
	// Refused until proven otherwise. A claim that does not hold leaves the
	// player drawn where they are, which is always a truthful answer.
	m_ride_id = 0;
	m_ride_offset = v3f();

	if (ride_id == 0) {
		m_ride_refused = 0;
		return;
	}

	// Someone held by an attachment is already drawn against their parent;
	// a deck underneath has nothing left to explain, and the send path skips
	// it anyway.
	if (isAttached()) {
		m_ride_refused = 0;
		return;
	}

	ServerActiveObject *ride = m_env->getActiveObject(ride_id);
	// Gone since the client looked at it. That is not a lie — objects are
	// removed constantly and the client is always a moment behind — so the
	// claim is dropped without a word.
	if (!ride || ride->isGone()) {
		m_ride_refused = 0;
		return;
	}

	if (!rideHolds(ride, ride_offset)) {
		// Say it once per claim. A client insisting on an impossible one
		// sends it with every position packet, and twenty lines a second
		// about one player buries the log it belongs in.
		if (m_ride_refused != ride_id) {
			m_ride_refused = ride_id;
			m_env->getScriptIface()->on_cheat(this, "impossible_ride");
		}
		return;
	}

	m_ride_refused = 0;
	m_ride_id = ride_id;
	m_ride_offset = ride_offset;
}

bool PlayerSAO::rideHolds(ServerActiveObject *ride, const v3f &offset) const
{
	// A deck is an object with a body. Players are not decks, and neither are
	// the bodiless odds and ends a world fills up with — decals, spent shells,
	// splinters, markers. Those exist in numbers, near everybody, and letting
	// one carry a rider is what turns this field from "which platform" into
	// "draw me wherever I like".
	if (ride->getType() != ACTIVEOBJECT_TYPE_LUAENTITY)
		return false;

	aabb3f box(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	if (!ride->getCollisionBox(&box))
		return false;

	// The offset has to fit on the thing it describes.
	if (offset.getLength() >= PLAYER_RIDE_RANGE * BS)
		return false;

	// And the check this whole function exists for.
	//
	// Where the client asks to be drawn is `deck + offset`. Where the player
	// is, is what the very same packet just told us. Those two are the same
	// instant on the client, so on the server they may differ by exactly one
	// thing: how far the deck moved while the packet travelled. A deck that
	// stands still leaves no room at all.
	//
	// Without this the offset was bounded and the deck was not, so naming an
	// object on the far side of the world moved a player's body there on
	// every other screen while the server went on shooting at the real one.
	float deck_speed = 0.0f;
	if (auto *entity = dynamic_cast<LuaEntitySAO *>(ride))
		deck_speed = entity->getVelocity().getLength();

	const v3f drawn = ride->getBasePosition() + offset;
	const float drift = (drawn - getBasePosition()).getLength();
	const float allowed = deck_speed * PLAYER_RIDE_SLACK_TIME
			+ PLAYER_RIDE_SLACK_DIST * BS;

	return drift <= allowed;
}

/**
 * How many nodes of a single step are worth examining for solid ground.
 *
 * A step longer than this is not a step at all, and the speed check below deals
 * with it on its own terms. The bound is here so that a made-up position on the
 * far side of the world costs one comparison rather than a walk across the map.
 */
static constexpr int PLAYER_PASSAGE_MAX_NODES = 64;

bool PlayerSAO::grabInteraction()
{
	return m_use_pool.grab(1.0f / PLAYER_INTERACT_RATE);
}

void PlayerSAO::measureSpeed()
{
	const v3f now = getBasePosition();
	const float dtime = m_time_from_last_speed;

	// Attached players are carried, and step() already says their own speed is
	// nothing. Nothing to measure and nothing to overwrite.
	if (isAttached()) {
		m_time_from_last_speed = 0.0f;
		m_speed_reference = now;
		return;
	}

	// Two positions from the same instant say nothing about speed: this packet
	// and the one before it arrived inside a single server step. The clock and
	// the mark stay where they are so the next measurement covers the whole
	// stretch — starting over here would hand a client that sends twice in a
	// step exactly what measuring was meant to take away from it, a way to
	// report itself standing still.
	if (dtime < 0.0001f)
		return;

	m_time_from_last_speed = 0.0f;

	// A jump the server itself made is not travel, and dividing it by time
	// would report a speed nobody moved at.
	if (m_time_from_last_teleport < dtime) {
		m_speed_reference = now;
		m_player->setSpeed(v3f());
		return;
	}

	m_player->setSpeed((now - m_speed_reference) / dtime);
	m_speed_reference = now;

	// Watch the descent while it happens: this record is the whole of what the
	// landing will be charged for. Anything but going down starts it again.
	if (m_player->getSpeed().Y < 0.0f) {
		m_fall_depth = MYMAX(m_fall_depth, (m_fall_peak_y - now.Y) / BS);
	} else {
		m_fall_peak_y = now.Y;
	}
}

u16 PlayerSAO::fallDamage() const
{
	// In blocks a second, not in the engine's own units: the drop is counted
	// in blocks and the threshold is a speed in blocks, so this is where the
	// two have to be spoken in the same language.
	const f32 gravity = m_player->movement_gravity / BS *
			m_player->physics_override.gravity;
	const f32 pushed = m_max_speed_override_time > 0.0f
			? std::fabs(m_max_speed_override.Y) / BS : 0.0f;

	// What the ground landed on and what the player is wearing make of it.
	// Both are read rather than guessed, so a game that makes falls hurt more
	// or not at all is obeyed and not overruled.
	const v3s16 below = floatToInt(getBasePosition() + v3f(0.0f, -0.1f * BS, 0.0f), BS);
	const ContentFeatures &ground = m_env->getCrateDef()->ndef()->get(
			m_env->getMap().getNode(below));

	f32 factor = 1.0f + itemgroup_get(ground.groups, "fall_damage_add_percent") / 100.0f;
	factor *= 1.0f + itemgroup_get(getArmorGroups(), "fall_damage_add_percent") / 100.0f;

	return fallDamageFromDrop(m_fall_depth, gravity, factor, pushed, m_prop.hp_max);
}

/// How far below the feet to look for ground, in blocks. A player resting on a
/// surface is a hair above it, and a player who has just stepped off it is not
/// falling yet.
static constexpr float PLAYER_SUPPORT_REACH = 0.2f;

bool PlayerSAO::isSupported(bool *soft) const
{
	*soft = false;

	aabb3f body(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	if (!getCollisionBox(&body)) {
		*soft = true;
		return true; // no body to hold up
	}

	// Two boxes, because two different things hold a player up.
	//
	// Water and ladders hold the whole of them: a ladder at chest height is
	// enough, and so is being waist-deep. Ground holds their feet and only
	// their feet — a wall beside a falling player does not stop them, and
	// neither does a ceiling above one, however closely their head brushes it.
	// Measuring both against the whole body made hugging a ceiling a way of
	// being held up by it.
	aabb3f footing = body;
	footing.MinEdge.Y -= PLAYER_SUPPORT_REACH * BS;
	footing.MaxEdge.Y = body.MinEdge.Y + 0.5f * BS;

	Map &map = m_env->getMap();
	const NodeDefManager *ndef = m_env->getCrateDef()->ndef();

	const v3s16 min = floatToInt(footing.MinEdge, BS);
	const v3s16 max = floatToInt(body.MaxEdge, BS);

	std::vector<aabb3f> boxes;
	// Bottom up: the ground under the feet answers this on the first row,
	// which is where nearly every player nearly always is.
	for (s16 y = min.Y; y <= max.Y; y++)
	for (s16 z = min.Z; z <= max.Z; z++)
	for (s16 x = min.X; x <= max.X; x++) {
		const v3s16 p(x, y, z);
		bool pos_ok = false;
		const MapNode n = map.getNode(p, &pos_ok);
		// Nothing known here, so nothing to conclude. Somewhere the server
		// cannot see is not a place to catch anybody out.
		if (!pos_ok || n.getContent() == CONTENT_IGNORE) {
			*soft = true;
			return true;
		}

		const ContentFeatures &f = ndef->get(n);
		// Something to climb or to swim in is not ground to land on, either:
		// nobody breaks their legs on water.
		if (f.climbable || f.liquid_move_physics) {
			*soft = true;
			return true;
		}
		if (!f.walkable)
			continue;

		boxes.clear();
		n.getCollisionBoxes(ndef, &boxes, n.getNeighbors(p, &map));

		const v3f node_pos = intToFloat(p, BS);
		for (aabb3f node_box : boxes) {
			node_box.MinEdge += node_pos;
			node_box.MaxEdge += node_pos;
			if (node_box.intersectsWithBox(footing))
				return true;
		}
	}

	// And whatever else is standing about. A deck, a boat, another player's
	// head — the client's own physics collide with objects, so a check that
	// only knew about nodes would call standing on one of them impossible.
	std::vector<ServerActiveObject *> objects;
	m_env->getObjectsInArea(objects, footing, nullptr);
	for (ServerActiveObject *obj : objects) {
		if (obj == this || obj->isGone())
			continue;
		aabb3f other(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		if (obj->getCollisionBox(&other) && other.intersectsWithBox(footing))
			return true;
	}

	return false;
}

/**
 * How much higher than its own arithmetic the engine lets a jump go.
 *
 * A body thrown upward and left alone traces one curve and no other, and the
 * engine knows every number in it: the jump speed it granted, the gravity it
 * applies. The slack is for the difference between watching that curve twenty
 * times a second and living it frame by frame, and for nothing else.
 */
static constexpr float PLAYER_AIR_SLACK = 1.3f;

/**
 * And how long a flight has to have lasted before its height is worth
 * measuring, in seconds.
 *
 * The first fraction of a jump is where the two clocks disagree most and where
 * the curve is steepest, so a tenth of a second either way is a large fraction
 * of the answer. A quarter of a second in, it is not.
 */
static constexpr float PLAYER_AIR_GRACE = 0.25f;

void PlayerSAO::watchFooting(float dtime)
{
	const v3f pos = getBasePosition();

	bool soft = false;
	const bool supported = isSupported(&soft);

	/*
		The landing.

		Fall damage used to be the client's word entirely: it worked out what
		the drop cost and told the server, which subtracted it. A client that
		said nothing therefore fell from any height for free, and one that said
		sixty-five thousand ended itself on the spot — at will, in the middle of
		a fight, taking the kill away from whoever was about to earn it.

		The physics that produce a fall are still the client's, and they have to
		be: it runs them frame by frame and the server does not. But the drop
		itself is no longer the client's to describe. The server has watched
		where this player has been, and a fall is a distance — so it works out
		the cost by the same arithmetic the client uses and applies it here,
		when the ground arrives.

		Landing in water or catching a ladder is not landing: those hold a
		player up without stopping them.
	*/
	if (supported && !m_was_supported && !soft && !isImmortal() && !isDead()) {
		const u16 damage = fallDamage();
		if (damage > 0) {
			PlayerHPChangeReason reason(PlayerHPChangeReason::FALL);
			setHP((s32)getHP() - (s32)damage, reason, true);
		} else if (m_fall_depth > 0.5f) {
			// The client works the same sum out for itself and has already
			// moved its own bar by the answer. When ours comes out at nothing
			// there is no change to send, and without this the player would go
			// on seeing whatever their own arithmetic came to.
			m_env->getServer()->SendPlayerHP(this, true);
		}
	}
	if (supported) {
		m_fall_depth = 0.0f;
		m_fall_peak_y = pos.Y;
	}

	/*
		Everything that legitimately keeps a player off the ground is let
		through here, and there is a good deal of it: being carried, being
		thrown by a mod, a game that has turned gravity off, the privilege that
		exists for exactly this, and the moment right after the server moved
		somebody.
	*/
	const bool excused = isAttached() || m_is_singleplayer ||
			m_privs.count("fly") != 0 ||
			m_player->physics_override.gravity <= 0.0f ||
			m_max_speed_override_time > 0.0f ||
			m_time_from_last_teleport < PLAYER_AIR_GRACE ||
			m_ride_id != 0;

	if (supported || excused) {
		// Standing again, so the next flight starts from here. What the ground
		// under them is matters: a bouncy node throws a player upward by an
		// amount the engine itself does not bound — the comment in
		// checkMovementCheat() has said so for years — and there is no curve to
		// measure a flight that starts off one.
		m_air_time = 0.0f;
		m_air_from_y = pos.Y;
		if (supported) {
			const v3s16 below = floatToInt(
					pos + v3f(0.0f, -0.1f * BS, 0.0f), BS);
			const ContentFeatures &ground = m_env->getCrateDef()->ndef()->get(
					m_env->getMap().getNode(below));
			m_air_bouncy = itemgroup_get(ground.groups, "bouncy") != 0;
		}
		m_was_supported = supported;
		return;
	}

	m_was_supported = supported;
	m_air_time += dtime;

	// The other half is a suspicion about the player, and that half is what
	// the anticheat flag governs.
	static thread_local const u32 anticheat_flags =
		g_settings->getFlagStr("anticheat_flags", flagdesc_anticheat, nullptr);
	if (!(anticheat_flags & AC_MOVEMENT))
		return;

	if (m_air_bouncy || m_air_time < PLAYER_AIR_GRACE)
		return;

	/*
		And the question this was all for: is this still a jump?

		A body thrown upward and left alone traces one curve. Its highest point
		is set by the speed it left with, and the engine granted that speed
		itself — the jump it allows this player, with this game's gravity. So
		the height at any moment since the ground let go has a ceiling, and it
		is arithmetic rather than judgement.

		Whoever is above it is not falling and not jumping. That is as far as
		the engine will go: it can prove that its own physics bring such a
		player down, not that no game ever had a reason to hold one up. So it
		says what it saw, undoes nothing, and leaves the deciding to the game,
		which knows whether it has flying machines in it.
	*/
	// Speeds and positions are both in the engine's own units here, so the
	// curve is worked out in them and compared in them.
	const float jump = m_player->movement_speed_jump *
			m_player->physics_override.jump * PLAYER_AIR_SLACK;
	const float gravity = m_player->movement_gravity *
			m_player->physics_override.gravity;
	const float ceiling = m_air_from_y +
			jumpReachAfter(jump, gravity, m_air_time);

	if (pos.Y <= ceiling)
		return;

	// Said once per stretch, not once per step, and then measured afresh from
	// where they are: the point is to say that it is happening, and to go on
	// saying so while it does.
	m_air_time = 0.0f;
	m_air_from_y = pos.Y;
	m_env->getScriptIface()->on_cheat(this, "hovering");
}

bool PlayerSAO::wentThroughSolid(const v3f &from, const v3f &to) const
{
	// The line is drawn through the middle of the player's own collision box,
	// not at some fixed height: a game may make its players any size it likes,
	// and the middle of a body is the part a wall is surest to stop.
	const float mid = (m_prop.collisionbox.MinEdge.Y + m_prop.collisionbox.MaxEdge.Y)
			* 0.5f * BS;
	const v3f eye_from = from + v3f(0.0f, mid, 0.0f);
	const v3f eye_to = to + v3f(0.0f, mid, 0.0f);

	const v3f travel = eye_to - eye_from;
	const f32 length = travel.getLength();
	if (length < 0.001f)
		return false; // stood still; nothing was crossed

	const v3f middle = (eye_from + eye_to) * 0.5f;
	const v3f direction = travel / length;
	const f32 half_length = length * 0.5f;

	Map &map = m_env->getMap();
	const NodeDefManager *ndef = m_env->getCrateDef()->ndef();

	// Whatever the player is standing in *now* is not evidence against them: a
	// mod can drop a node onto somebody, and walking out of it is the right
	// thing to do rather than a cheat. Where they say they are going is
	// another matter and is not forgiven — otherwise a wall could be crossed
	// in two steps, ending inside it and then leaving it.
	const v3s16 leaving = floatToInt(eye_from / BS, 1.0f);

	voxalgo::VoxelLineIterator iterator(eye_from / BS, (eye_to - eye_from) / BS);
	std::vector<aabb3f> boxes;

	for (int steps = 0; iterator.hasNext() && steps < PLAYER_PASSAGE_MAX_NODES;
			++steps) {
		iterator.next();
		const v3s16 p = iterator.m_current_node_pos;
		if (p == leaving)
			continue;

		bool pos_ok = false;
		const MapNode n = map.getNode(p, &pos_ok);
		// Not loaded here, so we do not know what is here, so nobody is
		// accused of anything. Silence is the only honest answer.
		if (!pos_ok || n.getContent() == CONTENT_IGNORE)
			return false;

		const ContentFeatures &f = ndef->get(n);
		if (!f.walkable)
			continue;

		// The node's real shape, the same one the client's own collision code
		// was given. Stairs, slabs, fences and plants are walkable and do not
		// fill their cube; measuring them as full blocks would accuse honest
		// players on every staircase.
		boxes.clear();
		n.getCollisionBoxes(ndef, &boxes, n.getNeighbors(p, &map));

		const v3f node_pos = intToFloat(p, BS);
		for (aabb3f box : boxes) {
			box.MinEdge += node_pos;
			box.MaxEdge += node_pos;
			if (box.intersectsWithLine(middle, direction, half_length))
				return true;
		}
	}

	return false;
}

const char *PlayerSAO::checkMovementCheat()
{
	static thread_local const u32 anticheat_flags =
		g_settings->getFlagStr("anticheat_flags", flagdesc_anticheat, nullptr);

	if (m_is_singleplayer ||
			isAttached() ||
			!(anticheat_flags & AC_MOVEMENT)) {
		m_last_good_position = getBasePosition();
		return nullptr;
	}

	/*
		Was this way even open?

		Everything below asks how long the move should have taken, and every
		part of that answer belongs to the game: its walking speeds, the
		player's privileges, what a mod did to them a moment ago, how much the
		link lags. Whether they went through a wall belongs to none of it —
		which is what makes it the one thing that can be refused outright
		instead of paid for out of a pool.

		A player holding `noclip` is not asked: passing through the world is
		exactly what that privilege grants. Neither is one the server itself
		has just moved — a mod may put anybody anywhere, and the straight line
		from wherever they were is nobody's route.
	*/
	if (m_privs.count("noclip") == 0 && m_time_from_last_teleport > 1.0f &&
			wentThroughSolid(m_last_good_position, getBasePosition())) {
		actionstream << "Server: " << m_player->getName()
				<< " moved through solid ground; resetting position."
				<< std::endl;
		setBasePosition(m_last_good_position);
		return "moved_through_solid";
	}

	bool cheated = false;
	/*
		Check player movements

		NOTE: Actually the server should handle player physics like the
		client does and compare player's position to what is calculated
		on our side. This is required when eg. players fly due to an
		explosion. Although a node-based alternative might be possible
		too, and much more lightweight.
	*/

	float override_max_H, override_max_V;
	if (m_max_speed_override_time > 0.0f) {
		override_max_H = MYMAX(fabs(m_max_speed_override.X), fabs(m_max_speed_override.Z));
		override_max_V = fabs(m_max_speed_override.Y);
	} else {
		override_max_H = override_max_V = 0.0f;
	}

	float player_max_walk = 0; // horizontal movement
	float player_max_jump = 0; // vertical upwards movement

	float speed_walk = m_player->movement_speed_walk * m_player->physics_override.speed_walk;
	float speed_fast = m_player->movement_speed_fast * m_player->physics_override.speed_fast;
	float speed_crouch = m_player->movement_speed_crouch * m_player->physics_override.speed_crouch;
	float speed_climb = m_player->movement_speed_climb * m_player->physics_override.speed_climb;

	speed_walk *= m_player->physics_override.speed;
	speed_fast *= m_player->physics_override.speed;
	speed_crouch *= m_player->physics_override.speed;
	speed_climb *= m_player->physics_override.speed;

	// Get permissible max. speed
	if (m_privs.count("fast") != 0) {
		// Fast priv: Get the highest speed of fast, walk or crouch
		// (it is not forbidden the 'fast' speed is
		// not actually the fastest)
		player_max_walk = MYMAX(speed_crouch, speed_fast);
		player_max_walk = MYMAX(player_max_walk, speed_walk);
	} else {
		// Get the highest speed of walk or crouch
		// (it is not forbidden the 'walk' speed is
		// lower than the crouch speed)
		player_max_walk = MYMAX(speed_crouch, speed_walk);
	}

	player_max_walk = MYMAX(player_max_walk, override_max_H);

	player_max_jump = m_player->movement_speed_jump * m_player->physics_override.jump;
	// FIXME: Bouncy nodes cause practically unbound increase in Y speed,
	//        until this can be verified correctly, tolerate higher jumping speeds
	player_max_jump *= 2.0;
	player_max_jump = MYMAX(player_max_jump, speed_climb);
	player_max_jump = MYMAX(player_max_jump, override_max_V);

	// Don't divide by zero!
	if (player_max_walk < 0.0001f)
		player_max_walk = 0.0001f;
	if (player_max_jump < 0.0001f)
		player_max_jump = 0.0001f;

	v3f diff = (getBasePosition() - m_last_good_position);
	float d_vert = diff.Y;
	diff.Y = 0;
	float d_horiz = diff.getLength();
	float required_time = d_horiz / player_max_walk;

	// FIXME: Checking downwards movement is not easily possible currently,
	//        the server could calculate speed differences to examine the gravity
	if (d_vert > 0) {
		// In certain cases (swimming, climbing, flying) walking speed is applied
		// vertically
		float s = MYMAX(player_max_jump, player_max_walk);
		required_time = MYMAX(required_time, d_vert / s);
	}

	static thread_local float anticheat_movement_tolerance =
		std::max(g_settings->getFloat("anticheat_movement_tolerance"), 1.0f);

	required_time /= anticheat_movement_tolerance;

	/*
		A stall is forgiven; a jump inside a heartbeat is not.

		The pool fills to five seconds or more, and it has to: a link that goes
		quiet and then delivers everything at once is ordinary, and the player
		on the other end of it really did walk all that way. But saved-up time
		could also be spent all at once — stand still for a moment, then be
		twenty blocks away, with the seconds honestly in hand and nothing in the
		log to say otherwise.

		What tells the two apart is not how much time was saved but how much of
		it really passed. A stalled client sends its position three seconds
		later; one covering the same ground in a single heartbeat sends it a
		heartbeat later. So the travel a packet carries is measured against the
		time since the last one, and the pool goes on covering everything else.
	*/
	const float elapsed = m_time_from_last_speed;
	const bool one_step_too_far =
			required_time > elapsed + PLAYER_MOVE_STEP_SLACK;

	if (!one_step_too_far && m_move_pool.grab(required_time)) {
		m_last_good_position = getBasePosition();
	} else {
		const float LAG_POOL_MIN = 5.0;
		float lag_pool_max = m_env->getMaxLagEstimate() * 2.0;
		lag_pool_max = MYMAX(lag_pool_max, LAG_POOL_MIN);
		if (m_time_from_last_teleport > lag_pool_max) {
			actionstream << "Server: " << m_player->getName()
					<< " moved too fast: V=" << d_vert << ", H=" << d_horiz
					<< (one_step_too_far ? ", all of it in one step" : "")
					<< "; resetting position." << std::endl;
			cheated = true;
		} else {
			// Right after the server moved somebody, the same refusal happens
			// without a word: the pool was emptied by the move and the player
			// is not to blame for what it cannot pay for yet. But the position
			// is still thrown away, and a position thrown away in silence is
			// the hardest kind of thing to find afterwards — a player who
			// stops falling in mid-air leaves no trace at all.
			verbosestream << "Server: " << m_player->getName()
					<< " moved V=" << d_vert << ", H=" << d_horiz
					<< " which the pool cannot pay for yet ("
					<< m_time_from_last_teleport << "s since it was moved); "
					<< "position dropped quietly." << std::endl;
		}
		setBasePosition(m_last_good_position);
	}
	return cheated ? "moved_too_fast" : nullptr;
}

bool PlayerSAO::getCollisionBox(aabb3f *toset) const
{
	//update collision box
	toset->MinEdge = m_prop.collisionbox.MinEdge * BS;
	toset->MaxEdge = m_prop.collisionbox.MaxEdge * BS;

	toset->MinEdge += getBasePosition();
	toset->MaxEdge += getBasePosition();
	return true;
}

bool PlayerSAO::getSelectionBox(aabb3f *toset) const
{
	if (!m_prop.is_visible) {
		return false;
	}

	toset->MinEdge = m_prop.selectionbox.MinEdge * BS;
	toset->MaxEdge = m_prop.selectionbox.MaxEdge * BS;

	return true;
}

float PlayerSAO::getZoomFOV() const
{
	return m_prop.zoom_fov;
}
