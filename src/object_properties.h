// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include <optional>
#include <string>
#include "irrlichttypes_bloated.h"
#include <iostream>
#include <vector>
#include "util/pointabilities.h"
#include "mapnode.h"

struct EnumString;

enum ObjectVisual : u8 {
	OBJECTVISUAL_UNKNOWN,
	OBJECTVISUAL_SPRITE,
	OBJECTVISUAL_UPRIGHT_SPRITE,
	OBJECTVISUAL_CUBE,
	OBJECTVISUAL_MESH,
	OBJECTVISUAL_ITEM,
	OBJECTVISUAL_WIELDITEM,
	OBJECTVISUAL_NODE,
	OBJECTVISUAL_VOXELS,
};

extern const EnumString es_ObjectVisual[];

enum class StepUpMode : u8 {
	LEGACY,
	FLOATY,
	RIGID,
};

extern const EnumString es_StepUpMode[];

struct ObjectProperties
{
	/* member variables ordered roughly by size */

	std::vector<std::string> textures;
	std::vector<video::SColor> colors; // Currently unused
	// Values are BS=1
	aabb3f collisionbox = aabb3f(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f);
	// Values are BS=1
	aabb3f selectionbox = aabb3f(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f);
	ObjectVisual visual = OBJECTVISUAL_SPRITE;
	std::string mesh;
	std::string damage_texture_modifier = "^[brighten";
	std::string nametag;
	std::string infotext;
	// For dropped items, this contains the serialized item.
	std::string wield_item;
	v3f visual_size = v3f(1, 1, 1);
	video::SColor nametag_color = video::SColor(255, 255, 255, 255);
	std::optional<video::SColor> nametag_bgcolor;
	v2s16 spritediv = v2s16(1, 1);
	v2s16 initial_sprite_basepos;
	f32 stepheight = 0.0f;
	float automatic_rotate = 0.0f;
	f32 automatic_face_movement_dir_offset = 0.0f;
	f32 automatic_face_movement_max_rotation_per_sec = -1.0f;
	float eye_height = 1.625f;
	float zoom_fov = 0.0f;
	std::optional<u32> nametag_fontsize;
	MapNode node = MapNode(CONTENT_IGNORE);

	/**
	 * A piece of a voxel-shaped object: which node it is and where it sits,
	 * counted from the object's own origin in whole nodes.
	 */
	struct VoxelPiece
	{
		v3s16 offset;
		MapNode node;
	};

	/**
	 * The nodes this object is made of, drawn as one mesh (OBJECTVISUAL_VOXELS).
	 *
	 * The engine has always been able to show a single node as an object; a
	 * heap of them had to be a heap of objects, one per node, each sent to
	 * every player. That is what a falling tree or a moving contraption
	 * actually is, and paying an object per node for it is why such things
	 * stay small.
	 *
	 * Geometry comes from the same generator that draws the map, so a node
	 * looks here exactly as it does in the world — nodeboxes, connections and
	 * all.
	 */
	std::vector<VoxelPiece> voxels;

	/// More than this many pieces are refused: the set travels inside the
	/// object's properties, and properties are resent whole on every change.
	static constexpr size_t MAX_VOXELS = 4096;
	u16 hp_max = 1;
	u16 breath_max = 0;
	s8 glow = 0;
	PointabilityType pointable = PointabilityType::POINTABLE;
	// In a future protocol these could be a flag field.
	bool physical = false;
	bool collideWithObjects = true;
	bool rotate_selectionbox = false;
	bool is_visible = true;
	bool makes_footstep_sound = false;
	bool automatic_face_movement_dir = false;
	bool backface_culling = true;
	bool static_save = true;
	bool use_texture_alpha = false;
	bool shaded = true;
	/*!
	 * Смещение по глубине, в шагах буфера глубины.
	 *
	 * Нужно плоским накладкам — отметинам от пуль, подпалинам, следам, — то
	 * есть всему, что рисуется вплотную к стене. Отодвигать их от стены в
	 * мире нельзя: с угла видно, что накладка висит в воздухе. А оставленные
	 * ровно на грани, они спорят с ней за глубину и рвутся полосами: буфер
	 * хранит расстояние с конечной точностью, и на дальней стене обе
	 * поверхности попадают в один и тот же шаг.
	 *
	 * Отрицательное значение придвигает объект к камере — ровно настолько,
	 * чтобы выиграть спор, и нисколько в мире.
	 */
	f32 depth_bias = 0.0f;
	/*!
	 * Пишется ли объект в карту теней.
	 *
	 * Плоской накладке тень отбрасывать нечем — она сама лежит на
	 * поверхности, — а в карту теней она рисуется наравне со всеми, то есть
	 * стоит второго прохода отрисовки ни за что.
	 */
	bool casts_shadow = true;
	bool show_on_minimap = false;
	bool nametag_scale_z = false;
	StepUpMode step_up_mode = StepUpMode::LEGACY;

	/*
	 * Вид от первого лица. Объект, прицепленный к игроку, обычно висит на его
	 * модели — и в собственных глазах владельца поэтому не виден вовсе. Оружию
	 * же полагается быть перед глазами: там его место в шутере, и там его видит
	 * только сам стрелок.
	 *
	 * Поэтому у такого объекта две позы: обычная — от кости, к которой он
	 * прицеплен (её видят остальные), и эта — от камеры владельца. Переключение
	 * происходит само, по тому, от какого лица владелец сейчас смотрит.
	 */
	bool first_person = false;
	v3f first_person_position;
	v3f first_person_rotation;

	/*
	 * Вещь, которую видит только сам владелец. Не «где рисовать», а «рисовать
	 * ли вообще»: руки, которые стрелок видит перед собой, остальным не нужны —
	 * со стороны у него есть свои.
	 *
	 * Отдельно от first_person потому, что это про другое. First_person
	 * переносит вещь к камере, а сама вещь висит на игроке. Здесь же вещь может
	 * висеть на чём угодно — хоть на кости оружия, которое само у камеры, — и
	 * «своими глазами» относится ко всей цепочке привязок разом: смотрит ли
	 * своими глазами тот игрок, на котором эта цепочка в конце концов держится.
	 *
	 * Решает это клиент, а не сервер: от какого лица смотреть, сервер не знает.
	 */
	bool first_person_only = false;

	ObjectProperties();

	std::string dump() const;

	bool operator==(const ObjectProperties &other) const;
	bool operator!=(const ObjectProperties &other) const {
		return !(*this == other);
	}

	/**
	 * Check limits of some important properties that'd cause exceptions later on.
	 * Errornous values are discarded after printing a warning.
	 * @return true if a problem was found
	*/
	bool validate();

	void serialize(std::ostream &os) const;
	void deSerialize(std::istream &is);
};
