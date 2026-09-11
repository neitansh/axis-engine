// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "ragdoll.h"
#include "constants.h"
#include "matrix4.h"
#include <algorithm>
#include <cmath>

namespace {

// Шаг счёта. Кадр клиента может быть любым, а стягивание расстояний
// сходится за фиксированное число итераций только при шаге, который оно
// знает; поэтому кадр режется на подшаги.
constexpr f32 SUBSTEP = 1.0f / 120.0f;
constexpr int ITERATIONS = 8;
constexpr f32 GRAVITY = 9.81f * BS;
// Доля движения, теряемая за подшаг: в воздухе — сопротивление, на опоре —
// трение вдоль неё.
constexpr f32 AIR_DAMPING = 0.004f;
constexpr f32 FRICTION = 0.35f;
// Ниже этой скорости (единиц в секунду) тело считается неподвижным.
constexpr f32 SLEEP_SPEED = 0.6f;
constexpr f32 SLEEP_AFTER = 0.7f;

// Туловище тяжелее конечности: удар в руку дёргает руку, а не тащит корпус.
constexpr f32 TORSO_MASS = 3.0f;
constexpr f32 LIMB_MASS = 1.0f;

v3f normalized(v3f v)
{
	const f32 len = v.getLength();
	return len > 1e-6f ? v / len : v3f(0, 1, 0);
}

} // namespace

void Ragdoll::start(const Positions &positions, v3f velocity, Particle hit, v3f impulse)
{
	m_pos = positions;
	for (int i = 0; i < COUNT; ++i) {
		v3f v = velocity;
		if (i == hit)
			v += impulse;
		m_prev[i] = m_pos[i] - v * SUBSTEP;
		m_inv_mass[i] = 1.0f / (i <= HIP_L ? TORSO_MASS : LIMB_MASS);
		m_contact[i] = false;
	}

	size_t n = 0;
	const Particle torso[] = {NECK, SHOULDER_R, SHOULDER_L, HIP_R, HIP_L};
	for (int a = 0; a < 5; ++a)
		for (int b = a + 1; b < 5; ++b)
			m_distances[n++] = {torso[a], torso[b], 0.0f};
	m_distances[n++] = {NECK, HEAD, 0.0f};
	m_distances[n++] = {SHOULDER_R, HAND_R, 0.0f};
	m_distances[n++] = {SHOULDER_L, HAND_L, 0.0f};
	m_distances[n++] = {HIP_R, FOOT_R, 0.0f};
	m_distances[n++] = {HIP_L, FOOT_L, 0.0f};
	for (auto &d : m_distances)
		d.rest = (m_pos[d.a] - m_pos[d.b]).getLength();

	// Пределы сгиба — от оси туловища, а не от соседней кости: у рига нет
	// ни локтей, ни коленей, и конечность целиком висит на своём суставе.
	const v3f up(0, 1, 0), down(0, -1, 0);
	m_cones = {{
		{NECK, HEAD, up, 60.0f * core::DEGTORAD},
		{SHOULDER_R, HAND_R, down, 160.0f * core::DEGTORAD},
		{SHOULDER_L, HAND_L, down, 160.0f * core::DEGTORAD},
		{HIP_R, FOOT_R, down, 95.0f * core::DEGTORAD},
		{HIP_L, FOOT_L, down, 95.0f * core::DEGTORAD},
	}};

	m_started = true;
	m_asleep = false;
	m_still_for = 0.0f;
	m_accumulator = 0.0f;
}

void Ragdoll::step(f32 dtime, const SolidQuery &solid)
{
	if (!m_started || m_asleep)
		return;
	// Долгий кадр (пауза, загрузка) не должен превращаться в сотни подшагов.
	m_accumulator += std::min(dtime, 0.25f);
	while (m_accumulator >= SUBSTEP) {
		substep(SUBSTEP, solid);
		m_accumulator -= SUBSTEP;
	}
}

void Ragdoll::substep(f32 dt, const SolidQuery &solid)
{
	for (int i = 0; i < COUNT; ++i) {
		const v3f motion = (m_pos[i] - m_prev[i]) * (1.0f - AIR_DAMPING);
		m_prev[i] = m_pos[i];
		m_pos[i] += motion + v3f(0, -GRAVITY * dt * dt, 0);
	}

	for (int it = 0; it < ITERATIONS; ++it) {
		for (const auto &d : m_distances) {
			v3f delta = m_pos[d.b] - m_pos[d.a];
			const f32 len = delta.getLength();
			if (len < 1e-6f)
				continue;
			const f32 w = m_inv_mass[d.a] + m_inv_mass[d.b];
			const v3f correction = delta * ((len - d.rest) / len / w);
			m_pos[d.a] += correction * m_inv_mass[d.a];
			m_pos[d.b] -= correction * m_inv_mass[d.b];
		}
		for (const auto &c : m_cones) {
			const v3f axis = torsoAxis(m_pos, c.axis);
			const v3f limb = m_pos[c.tip] - m_pos[c.base];
			const f32 len = limb.getLength();
			if (len < 1e-6f)
				continue;
			const v3f dir = limb / len;
			const f32 cos_angle = std::clamp(dir.dotProduct(axis), -1.0f, 1.0f);
			const f32 angle = std::acos(cos_angle);
			if (angle <= c.max_angle)
				continue;
			// Кончик уводится к границе конуса; сустав, как более тяжёлый,
			// остаётся на месте.
			v3f side = dir - axis * cos_angle;
			side = normalized(side);
			const v3f limited = axis * std::cos(c.max_angle) + side * std::sin(c.max_angle);
			m_pos[c.tip] = m_pos[c.base] + limited * len;
		}
	}

	f32 max_speed = 0.0f;
	for (int i = 0; i < COUNT; ++i) {
		collide(i, solid);
		max_speed = std::max(max_speed, (m_pos[i] - m_prev[i]).getLength() / dt);
	}

	if (max_speed < SLEEP_SPEED) {
		m_still_for += dt;
		if (m_still_for >= SLEEP_AFTER)
			m_asleep = true;
	} else {
		m_still_for = 0.0f;
	}
}

void Ragdoll::collide(int i, const SolidQuery &solid)
{
	v3f &p = m_pos[i];
	m_contact[i] = false;

	auto node_of = [](const v3f &v) {
		return v3s16(std::floor(v.X / BS + 0.5f), std::floor(v.Y / BS + 0.5f),
				std::floor(v.Z / BS + 0.5f));
	};

	// Точка внутри твёрдой ноды: выход через ближайшую грань, за которой
	// пусто. Так тело, вошедшее в пол на подшаге, выходит наверх, а не
	// проваливается к его нижней грани.
	v3s16 n = node_of(p);
	if (solid(n)) {
		const v3f center(n.X * BS, n.Y * BS, n.Z * BS);
		struct Face {
			v3f normal;
			f32 dist;
		};
		Face faces[6];
		int k = 0;
		for (int axis = 0; axis < 3; ++axis) {
			for (int sign = -1; sign <= 1; sign += 2) {
				v3f normal(0, 0, 0);
				f32 &nc = axis == 0 ? normal.X : axis == 1 ? normal.Y : normal.Z;
				nc = sign;
				const f32 pc = axis == 0 ? p.X : axis == 1 ? p.Y : p.Z;
				const f32 cc = axis == 0 ? center.X : axis == 1 ? center.Y : center.Z;
				faces[k++] = {normal, (cc + sign * BS / 2) * sign - pc * sign};
			}
		}
		std::sort(faces, faces + 6, [](const Face &a, const Face &b) { return a.dist < b.dist; });
		for (const Face &f : faces) {
			const v3s16 beyond = n + v3s16(f.normal.X, f.normal.Y, f.normal.Z);
			if (solid(beyond))
				continue;
			p += f.normal * (f.dist + RADIUS);
			m_contact[i] = true;
			break;
		}
	}

	// Соседние твёрдые ноды ближе радиуса: точка касается их грани.
	n = node_of(p);
	v3f push_normal(0, 0, 0);
	for (int axis = 0; axis < 3; ++axis) {
		for (int sign = -1; sign <= 1; sign += 2) {
			v3s16 nb = n;
			f32 &pc = axis == 0 ? p.X : axis == 1 ? p.Y : p.Z;
			const s16 nc = axis == 0 ? n.X : axis == 1 ? n.Y : n.Z;
			(axis == 0 ? nb.X : axis == 1 ? nb.Y : nb.Z) = nc + sign;
			if (!solid(nb))
				continue;
			const f32 face = nc * BS + sign * BS / 2;
			const f32 gap = (face - pc) * sign;
			if (gap < RADIUS) {
				pc = face - sign * RADIUS;
				(axis == 0 ? push_normal.X : axis == 1 ? push_normal.Y : push_normal.Z) = -sign;
				m_contact[i] = true;
			}
		}
	}

	if (m_contact[i]) {
		// Трение: движение вдоль опоры гасится, поперёк — остаётся как есть.
		const v3f nrm = normalized(push_normal);
		const v3f motion = p - m_prev[i];
		const v3f normal_part = nrm * motion.dotProduct(nrm);
		const v3f tangent = motion - normal_part;
		p = m_prev[i] + normal_part + tangent * (1.0f - FRICTION);
	}
}

v3f Ragdoll::torsoAxis(const std::array<v3f, COUNT> &p, const v3f &axis) const
{
	return torsoRotation(p) * axis;
}

core::quaternion Ragdoll::torsoRotation(const std::array<v3f, COUNT> &p) const
{
	const v3f hip_mid = (p[HIP_R] + p[HIP_L]) / 2.0f;
	v3f up = normalized(p[NECK] - hip_mid);
	v3f right = p[SHOULDER_R] - p[SHOULDER_L];
	right = normalized(right - up * right.dotProduct(up));
	const v3f forward = right.crossProduct(up);
	core::matrix4 m;
	m[0] = right.X;   m[1] = right.Y;   m[2] = right.Z;
	m[4] = up.X;      m[5] = up.Y;      m[6] = up.Z;
	m[8] = forward.X; m[9] = forward.Y; m[10] = forward.Z;
	return core::quaternion(m);
}

Ragdoll::Pose Ragdoll::pose() const
{
	Pose out;
	out.torso = torsoRotation(m_pos);
	out.neck = m_pos[NECK];
	out.hip_mid = (m_pos[HIP_R] + m_pos[HIP_L]) / 2.0f;

	core::quaternion inv = out.torso;
	inv.makeInverse();
	auto limb = [&](Particle base, Particle tip, const v3f &rest) {
		const v3f local = inv * normalized(m_pos[tip] - m_pos[base]);
		core::quaternion q;
		q.rotationFromTo(rest, local);
		return q;
	};
	const v3f up(0, 1, 0), down(0, -1, 0);
	out.head = limb(NECK, HEAD, up);
	out.arm_r = limb(SHOULDER_R, HAND_R, down);
	out.arm_l = limb(SHOULDER_L, HAND_L, down);
	out.leg_r = limb(HIP_R, FOOT_R, down);
	out.leg_l = limb(HIP_L, FOOT_L, down);
	return out;
}
