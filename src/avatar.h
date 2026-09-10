// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "settings.h"
#include "util/serialize.h"

#include <sstream>
#include <string>
#include <vector>

/**
 * The player character the engine ships, and the rules around it.
 *
 * See doc/avatar.md. The short of it: when this is on, what a player looks
 * like belongs to the engine — the model, the body texture, what is worn. A
 * game running with it on cannot dress players itself; a game that wants its
 * own characters turns it off and dresses them as before. There is no middle
 * setting on purpose: half of it ours and half theirs is exactly the state
 * players cannot be protected in.
 */

/// Files shipped in builtin/media/avatar. Named with a prefix because they
/// live in the same flat media namespace as everything a game brings.
constexpr const char *AVATAR_MESH = "axis_character.geo.json";
constexpr const char *AVATAR_DEFAULT_TEXTURE = "axis_character_default.png";

/**
 * How tall the character is drawn in the file, in nodes.
 *
 * The model is 32 pixels tall and a pixel is 1/16 of a node. Nothing is drawn
 * at that size, though: the character is scaled to the collision box the game
 * set, so that what is seen is what can be hit. See doc/avatar.md §3.
 */
constexpr float AVATAR_MODEL_HEIGHT = 2.0f;

inline bool avatarsEnabled()
{
	return g_settings->getBool("player_avatars");
}

/// One worn thing with its own geometry: a hat, hair, a backpack. Rides on a
/// bone of the character and is drawn by the client as part of the player —
/// never as an object in the world, or a server could take it off (§2).
struct AvatarPart
{
	std::string bone;
	std::string mesh;
	std::string texture;

	bool operator==(const AvatarPart &o) const
	{
		return bone == o.bone && mesh == o.mesh && texture == o.texture;
	}
};

/// Everything about how one player looks, as the client needs it.
struct AvatarLook
{
	/// Body texture, already composed of the tone and the worn layers.
	std::string body_texture;
	std::vector<AvatarPart> parts;

	/// At most this many parts are drawn. Each one with its own texture is a
	/// draw of its own for every player in sight, and a look nobody can see
	/// through is not worth a frame (§5).
	static constexpr size_t MAX_PARTS = 6;

	bool operator==(const AvatarLook &o) const
	{
		return body_texture == o.body_texture && parts == o.parts;
	}
	bool operator!=(const AvatarLook &o) const { return !(*this == o); }

	void serialize(std::ostream &os) const
	{
		writeU8(os, 1); // version
		os << serializeString16(body_texture);
		writeU16(os, parts.size());
		for (const AvatarPart &p : parts) {
			os << serializeString16(p.bone);
			os << serializeString16(p.mesh);
			os << serializeString16(p.texture);
		}
		// Proof that this look is one we issued. Empty until looks travel
		// signed; then the client checks it and refuses what does not hold up.
		os << serializeString16("");
	}

	void deSerialize(std::istream &is)
	{
		const u8 version = readU8(is);
		if (version != 1)
			throw SerializationError("unsupported AvatarLook version");
		body_texture = deSerializeString16(is);
		parts.clear();
		const u16 count = readU16(is);
		for (u16 i = 0; i < count && i < MAX_PARTS; i++) {
			AvatarPart p;
			p.bone = deSerializeString16(is);
			p.mesh = deSerializeString16(is);
			p.texture = deSerializeString16(is);
			parts.push_back(std::move(p));
		}
		deSerializeString16(is); // signature, not checked yet
	}
};
