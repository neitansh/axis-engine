// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irrlichttypes.h"

#include <EMaterialTypes.h>
#include <IDummyTransformationSceneNode.h>
#include <AnimSpec.h>

#include "object_properties.h"
#include "clientobject.h"
#include "constants.h"
#include "itemgroup.h"
#include "client/tile.h"
#include <cassert>
#include <memory>
#include <deque>

namespace scene {
	class IMeshSceneNode;
	class IBillboardSceneNode;
	class AnimatedMeshSceneNode;
}

class Client;
struct Nametag;
struct MinimapMarker;
class WieldMeshSceneNode;

enum class LocalPlayerAnimation : u8;

/*
	SmoothTranslator and other helpers
*/

template<typename T>
struct SmoothTranslator
{
	T val_old;
	T val_current;
	T val_target;
	/// Current rate of change, kept between steps so that it stays continuous
	T val_rate;
	f32 anim_time = 0;
	f32 anim_time_counter = 0;
	bool aim_is_end = true;

	SmoothTranslator() = default;

	void init(T current);

	void update(T new_target, bool is_end_position = false,
		float update_interval = -1);

	void translate(f32 dtime);
};

struct SmoothTranslatorWrapped : SmoothTranslator<f32>
{
	void translate(f32 dtime);
};

struct SmoothTranslatorWrappedv3f : SmoothTranslator<v3f>
{
	void translate(f32 dtime);
};

struct MeshAnimationInfo {
	u32 i; /// index of mesh buffer
	int frame; /// last animation frame
	TileLayer tile;
};

/*
	GenericCAO
*/

class GenericCAO : public ClientActiveObject
{
private:

	// Only set at initialization
	std::string m_name = "";
	bool m_is_player = false;
	bool m_is_local_player = false;
	// Property-ish things
	ObjectProperties m_prop;
	//
	scene::ISceneManager *m_smgr = nullptr;
	Client *m_client = nullptr;
	aabb3f m_selection_box = aabb3f(-BS/3.,-BS/3.,-BS/3., BS/3.,BS/3.,BS/3.);

	// Visuals
	scene::IMeshSceneNode *m_meshnode = nullptr;
	scene::AnimatedMeshSceneNode *m_animated_meshnode = nullptr;
	WieldMeshSceneNode *m_wield_meshnode = nullptr;
	scene::IBillboardSceneNode *m_spritenode = nullptr;
	scene::IDummyTransformationSceneNode *m_matrixnode = nullptr;
	Nametag *m_nametag = nullptr;
	MinimapMarker *m_marker = nullptr;
	bool m_visuals_expired = false;
	video::SColor m_last_light = video::SColor(0xFFFFFFFF);
	bool m_is_visible = false;
	std::vector<MeshAnimationInfo> m_meshnode_animation;

	// Material
	video::E_MATERIAL_TYPE m_material_type = video::EMT_INVALID;

	// Movement
	v3f m_position = v3f(0.0f, 10.0f * BS, 0);
	v3f m_velocity;
	v3f m_acceleration;
	v3f m_rotation;
	u16 m_hp = 1;
	SmoothTranslator<v3f> pos_translator;
	/// Diagnosis only: when the last position packet arrived
	u64 m_last_packet_ms = 0;

	/**
	 * The moving object this one stands on, and where on it.
	 *
	 * Not an attachment: nothing is glued and the server keeps sending world
	 * coordinates as before. But those coordinates were true of the deck as
	 * the standing client saw it, while this client sees the deck at a
	 * different moment, so drawing them straight puts the two out of step by
	 * however far the deck travelled in between. The offset does not have
	 * that problem: standing still on a deck is exactly what it describes.
	 */
	u16 m_ride_id = 0;
	v3f m_ride_offset;
	/// Ride the object had last frame, to notice when it changes
	u16 m_ride_previous = 0;
	/// Distance the change of ride would have jumped, and time left to
	/// swallow it: stepping on or off a moving deck switches which of two
	/// positions is drawn, and they differ by however far the deck has
	/// travelled since the world position was true.
	v3f m_ride_blend;
	f32 m_ride_blend_left = 0.0f;
	v3f m_ride_last_drawn;
	bool m_ride_has_last = false;


	/*
		Playback buffer for network motion.

		The server does not send at an even rhythm and cannot be made to: its
		send timer is compared against a threshold equal to its own step, so
		the smallest wobble in step time turns one gap into two, and the
		distance threshold skips further packets for anything moving slowly.
		Guessing the next gap from the last one therefore guesses wrong on
		every change, and the error lands on the screen as a change of speed.

		So we stop guessing. Packets are stamped onto a timeline built from
		the intervals the server reports, and playback runs that timeline a
		little behind the newest packet. Whatever the packets did on the way
		here, the motion drawn is the motion the server produced.
	*/
	struct MotionSample
	{
		/// Stamp on the timeline rebuilt from the server's own intervals
		f32 time;
		/// When it actually reached us, by the local clock
		f32 arrived;
		v3f pos;
		v3f rot;
	};

	std::deque<MotionSample> m_motion;
	/// Server-timeline stamp of the newest sample
	f32 m_motion_newest = 0.0f;
	/// Where playback currently is on that timeline
	f32 m_motion_clock = 0.0f;
	bool m_motion_active = false;

	/// Appends a packet to the timeline.
	void pushMotion(f32 interval, v3f pos, v3f rot);
	/// Advances playback and returns whether it produced a position.
	bool playMotion(f32 dtime, v3f *pos, v3f *rot);
	void resetMotion();
	SmoothTranslatorWrappedv3f rot_translator;

	// Spritesheet stuff
	// TODO move into own struct
	v2f m_tx_size = v2f(1,1);
	v2s16 m_tx_basepos;
	bool m_initial_tx_basepos_set = false;
	bool m_tx_select_horiz_by_yawpitch = false;
	int m_anim_frame = 0;
	int m_anim_num_frames = 1;
	float m_anim_framelength = 0.2f;
	float m_anim_timer = 0.0f;

	/// The animation for all tracks as specified by the server.
	/// This is usually what is used, unless overridden by a local player animation.
	scene::AnimSpec m_animation;
	/// For the local player CAO, animations may be overridden by the client
	/// based on the in-game state of the local player (e.g. walking, digging, idling).
	/// See also LocalPlayerAnimation (player.h), LocalPlayer::last_animation (localplayer.h).
	bool m_local_player_animation = false;
	/// Deferred set animation commands, to be run once the scene node exists
	std::vector<std::pair<std::string, scene::TrackAnimSpec>> deferred_set_animation_cmds;

	void applyTrackAnimation(scene::TrackId &&track_id, scene::TrackAnimSpec anim);

	// stores position and rotation for each bone name
	BoneOverrideMap m_bone_override;

	// Attachments
	object_t m_attachment_parent_id = 0;
	std::unordered_set<object_t> m_attachment_child_ids;
	std::string m_attachment_bone = "";
	v3f m_attachment_position;
	v3f m_attachment_rotation;
	bool m_attached_to_local = false;
	bool m_force_visible = false;

	ItemGroupList m_armor_groups;
	float m_reset_textures_timer = -1.0f;
	// stores texture modifier before punch update
	std::string m_previous_texture_modifier = "";
	// last applied texture modifier
	std::string m_current_texture_modifier = "";
	float m_step_distance_counter = 0.0f;

	bool visualExpiryRequired(const ObjectProperties &newprops) const;

public:

	GenericCAO(Client *client, ClientEnvironment *env);

	~GenericCAO();

	static std::unique_ptr<ClientActiveObject> create(Client *client, ClientEnvironment *env)
	{
		return std::make_unique<GenericCAO>(client, env);
	}

	inline ActiveObjectType getType() const override
	{
		return ACTIVEOBJECT_TYPE_GENERIC;
	}
	inline const ItemGroupList &getGroups() const
	{
		return m_armor_groups;
	}
	void initialize(const std::string &data) override;

	void processInitData(const std::string &data);

	bool getCollisionBox(aabb3f *toset) const override;

	bool collideWithObjects() const override;

	virtual bool getSelectionBox(aabb3f *toset) const override;

	const v3f getPosition() const override final;

	const v3f getVelocity() const override final { return m_velocity; }

	inline const v3f &getRotation() const { return m_rotation; }

	bool isImmortal() const;

	inline const ObjectProperties &getProperties() const { return m_prop; }

	inline const std::string &getName() const { return m_name; }

	scene::ISceneNode *getSceneNode() const override;

	scene::AnimatedMeshSceneNode *getAnimatedMeshSceneNode() const override;

	// m_matrixnode controls the position and rotation of the child node
	// for all scene nodes, as a workaround for an Irrlicht problem with
	// rotations. The child node's position can't be used because it's
	// rotated, and must remain as 0.
	// Note that m_matrixnode.setPosition() shouldn't be called. Use
	// m_matrixnode->getRelativeTransformationMatrix().setTranslation()
	// instead (aka getPosRotMatrix().setTranslation()).
	inline core::matrix4 &getPosRotMatrix()
	{
		assert(m_matrixnode);
		return m_matrixnode->getRelativeTransformationMatrix();
	}

	inline const core::matrix4 *getAbsolutePosRotMatrix() const
	{
		if (!m_matrixnode)
			return nullptr;
		return &m_matrixnode->getAbsoluteTransformation();
	}

	inline f32 getStepHeight() const
	{
		return m_prop.stepheight;
	}

	inline bool isLocalPlayer() const override
	{
		return m_is_local_player;
	}

	inline bool isPlayer() const
	{
		return m_is_player;
	}

	inline bool isVisible() const
	{
		return m_is_visible;
	}

	inline void setVisible(bool toset)
	{
		m_is_visible = toset;
	}

	void setChildrenVisible(bool toset);
	void setAttachment(object_t parent_id, const std::string &bone, v3f position,
			v3f rotation, bool force_visible) override;
	void getAttachment(object_t *parent_id, std::string *bone, v3f *position,
			v3f *rotation, bool *force_visible) const override;
	void clearChildAttachments() override;
	void addAttachmentChild(object_t child_id) override;
	void removeAttachmentChild(object_t child_id) override;
	ClientActiveObject *getParent() const override;
	const std::unordered_set<object_t> &getAttachmentChildIds() const override
	{ return m_attachment_child_ids; }
	void updateAttachments() override;

	void removeFromScene(bool permanent) override;

	void addToScene(ITextureSource *tsrc, scene::ISceneManager *smgr) override;

	inline void expireVisuals()
	{
		m_visuals_expired = true;
	}

	void updateLight(u32 day_night_ratio) override;

	void setNodeLight(const video::SColor &light);

	/* Get light position(s).
	 * returns number of positions written into pos[], which must have space
	 * for at least 3 vectors. */
	u16 getLightPosition(v3s16 *pos);

	void updateNametag();

	void updateMarker();

	void updateNodePos();

	/**
	 * Places the model of the local player where the player now is.
	 *
	 * The player is simulated by the client and the model is drawn from it, so
	 * the order within a frame decides what is seen: copied before the player
	 * moves, the model trails by one frame - by speed times frame time - and
	 * that trail grows and shrinks with every frame that runs long or short.
	 * Against a camera that sits exactly on the player, the model then shakes.
	 * Called at the end of ClientEnvironment::step(), once the player is where
	 * this frame leaves them.
	 */
	void followLocalPlayer();

	void step(float dtime, ClientEnvironment *env) override;

	void updateTextureAnim();

	// ffs this HAS TO BE a string copy! See #5739 if you think otherwise
	// Reason: updateTextures(m_previous_texture_modifier);
	void updateTextures(std::string mod);

	void updateAnimation(u16 track_nr);
	void setLocalPlayerAnimation(LocalPlayerAnimation local_anim, float speed);

	/// @note logs a warning for invalid IDs
	std::optional<u16> resolveTrackId(const scene::TrackId &id);

	void processMessage(const std::string &data) override;

	bool directReportPunch(v3f dir, const ItemStack *punchitem,
			const ItemStack *hand_item, float time_from_last_punch=1000000) override;

	std::string debugInfoText() override;

	std::string infoText() override
	{
		return m_prop.infotext;
	}

	void updateMeshCulling();

private:

	/// Update the parent chain so getPosition() returns an up to date position.
	void updateParentChain() const;

};
