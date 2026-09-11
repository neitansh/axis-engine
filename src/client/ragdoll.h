// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "irr_v3d.h"
#include "quaternion.h"
#include <array>
#include <functional>

/*
 * Тряпичная кукла персонажа axis_character: считается на клиенте, в мировых
 * координатах, и раз в кадр отдаёт повороты костей.
 *
 * Тело — не семь твёрдых тел с суставами, а десять точек с расстояниями между
 * ними. Туловище — пять точек (шея, два плеча, два бедра), стянутых попарно:
 * такой пятиугольник не гнётся и даёт туловищу полный поворот. Голова, руки и
 * ноги — по одной точке на конце, на расстоянии от своего сустава; сгибаться
 * им позволяет конус вокруг оси туловища. Это позиционная динамика: точки
 * летят по инерции, потом расстояния и конусы стягиваются обратно, потом
 * карта выталкивает то, что в неё вошло. Она не разлетается ни от какого
 * удара — ошибка в ней всегда остаётся ошибкой положения, а не скорости.
 *
 * Кости в самой модели — сустав и длина; сгибов внутри (колено, локоть) нет,
 * и их тут тоже нет. См. doc/avatar.md.
 */
class Ragdoll
{
public:
	enum Particle : u8 {
		NECK, SHOULDER_R, SHOULDER_L, HIP_R, HIP_L,
		HEAD, HAND_R, HAND_L, FOOT_R, FOOT_L,
		COUNT
	};

	using Positions = std::array<v3f, COUNT>;
	/// Твёрдая ли нода карты по этим координатам.
	using SolidQuery = std::function<bool(v3s16)>;

	/// Повороты костей в мировых осях и мировое положение шеи, из которых
	/// клиент собирает локальные преобразования суставов.
	struct Pose {
		core::quaternion torso;
		core::quaternion head, arm_r, arm_l, leg_r, leg_l;
		v3f neck;
		v3f hip_mid;
	};

	/// @param positions где точки сейчас — снимаются с костей в момент старта,
	///        так что тело падает из той позы, в какой стояло
	/// @param velocity скорость всего тела, единиц движка в секунду
	/// @param hit какую точку толкнуть, COUNT — никакую
	/// @param impulse добавка к скорости точки удара
	void start(const Positions &positions, v3f velocity, Particle hit, v3f impulse);

	void step(f32 dtime, const SolidQuery &solid);

	bool started() const { return m_started; }
	bool asleep() const { return m_asleep; }

	Pose pose() const;

	/// Радиус точки при столкновении с картой, единиц движка.
	static constexpr f32 RADIUS = 1.2f;

private:
	struct Distance {
		Particle a, b;
		f32 rest;
	};
	struct Cone {
		Particle base, tip;
		/// Ось конуса в осях туловища: вверх для головы, вниз для рук и ног.
		v3f axis;
		f32 max_angle;
	};

	void substep(f32 dt, const SolidQuery &solid);
	void collide(int i, const SolidQuery &solid);
	v3f torsoAxis(const std::array<v3f, COUNT> &p, const v3f &axis) const;
	core::quaternion torsoRotation(const std::array<v3f, COUNT> &p) const;

	bool m_started = false;
	bool m_asleep = false;
	f32 m_still_for = 0.0f;
	f32 m_accumulator = 0.0f;

	std::array<v3f, COUNT> m_pos{};
	std::array<v3f, COUNT> m_prev{};
	std::array<f32, COUNT> m_inv_mass{};
	std::array<bool, COUNT> m_contact{};
	std::array<Distance, 15> m_distances{};
	std::array<Cone, 5> m_cones{};
};
