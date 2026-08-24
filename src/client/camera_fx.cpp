// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "camera_fx.h"
#include "irrlichttypes.h"
#include <algorithm>
#include <cmath>

namespace
{

// Шаг интегрирования пружины. Кадр разбивается на целое число таких шагов, и
// остаток переносится в следующий, — поэтому один и тот же импульс даёт один и
// тот же след и при тридцати кадрах, и при двухстах сорока. Считать пружину
// прямо по dtime кадра нельзя: явная схема при крупном шаге даёт другую
// амплитуду, и отдача на слабой машине ощущалась бы иначе.
constexpr f32 FX_STEP = 1.0f / 240.0f;

// Дольше этого за один кадр не догоняем. Просадка в полсекунды не должна
// оборачиваться сотней шагов интегрирования — тем более что смотреть на
// пропущенное всё равно некому.
constexpr f32 FX_MAX_CATCHUP = 0.25f;

// Ниже этого пружина считается пришедшей в покой и перестаёт считаться.
constexpr f32 FX_REST_VALUE = 1.0e-5f;
constexpr f32 FX_REST_VELOCITY = 1.0e-4f;

// Оси колебаний разведены по частоте и фазе. Одинаковые числа дали бы движение
// по одной прямой — то есть покачивание, а не тряску.
constexpr f32 OSC_FREQ[3] = { 1.0f, 0.83f, 1.31f };
constexpr f32 OSC_PHASE[3] = { 0.0f, 1.7f, 3.9f };
// Крен виден сильнее прочего, поэтому его берём вполовину.
constexpr f32 OSC_AXIS[3] = { 1.0f, 0.8f, 0.5f };

} // namespace

void CameraFx::Spring::kick(v3f impulse, f32 new_stiffness, f32 new_damping)
{
	velocity += impulse;
	if (new_stiffness > 0.0f)
		stiffness = new_stiffness;
	if (new_damping >= 0.0f)
		damping = new_damping;
}

void CameraFx::Spring::step(f32 dtime)
{
	// Полунеявная схема Эйлера: сначала скорость по нынешнему положению,
	// потом положение по новой скорости. Она устойчива на порядки больших
	// жёсткостях, чем нужно здесь, и не накачивает энергию, как явная.
	v3f acceleration = -value * stiffness - velocity * damping;
	velocity += acceleration * dtime;
	value += velocity * dtime;
}

bool CameraFx::Spring::isResting() const
{
	return value.getLengthSQ() < FX_REST_VALUE * FX_REST_VALUE &&
			velocity.getLengthSQ() < FX_REST_VELOCITY * FX_REST_VELOCITY;
}

void CameraFx::addRotation(v3f impulse, f32 stiffness, f32 damping)
{
	m_rotation.kick(impulse, stiffness, damping);
}

void CameraFx::addPush(v3f rot_impulse, v3f pos_impulse, f32 stiffness, f32 damping)
{
	m_push_rot.kick(rot_impulse, stiffness, damping);
	m_push_pos.kick(pos_impulse, stiffness, damping);
}

void CameraFx::addOscillation(f32 amplitude, f32 frequency, f32 decay, f32 duration)
{
	if (amplitude <= 0.0f || duration <= 0.0f)
		return;
	// Их может накопиться сколько угодно — толчки складываются сами, — но
	// держать бесконечный список незачем: самые слабые уже не видны.
	if (m_oscillations.size() >= 16)
		m_oscillations.erase(m_oscillations.begin());
	m_oscillations.push_back(Oscillation{ amplitude, frequency, decay, duration, 0.0f });
}

void CameraFx::reset()
{
	m_rotation.reset();
	m_push_rot.reset();
	m_push_pos.reset();
	m_oscillations.clear();
	m_leftover = 0.0f;
}

f32 CameraFx::getOscillationAmplitude() const
{
	f32 total = 0.0f;
	for (const Oscillation &osc : m_oscillations)
		total += osc.amplitude * std::exp(-osc.decay * osc.time);
	return total;
}

CameraFx::Result CameraFx::step(f32 dtime)
{
	Result result;

	if (dtime > 0.0f) {
		m_leftover += std::min(dtime, FX_MAX_CATCHUP);
		const int steps = (int)(m_leftover / FX_STEP);
		m_leftover -= steps * FX_STEP;

		const bool rotation_active = !m_rotation.isResting();
		const bool push_active = !m_push_rot.isResting() || !m_push_pos.isResting();
		for (int i = 0; i < steps; i++) {
			if (rotation_active)
				m_rotation.step(FX_STEP);
			if (push_active) {
				m_push_rot.step(FX_STEP);
				m_push_pos.step(FX_STEP);
			}
		}
		if (!rotation_active)
			m_rotation.reset();
		if (!push_active) {
			m_push_rot.reset();
			m_push_pos.reset();
		}
	}

	// Пружины считаются в радианах, камера ждёт градусы.
	result.rotation = (m_rotation.value + m_push_rot.value) * core::RADTODEG;
	result.offset = m_push_pos.value;

	// Колебания — замкнутая формула от собственного времени события, а не шаг
	// интегрирования: время кадра на них не влияет вовсе.
	for (size_t i = 0; i < m_oscillations.size();) {
		Oscillation &osc = m_oscillations[i];
		osc.time += dtime;
		if (osc.time >= osc.duration) {
			m_oscillations.erase(m_oscillations.begin() + i);
			continue;
		}

		const f32 envelope = osc.amplitude * std::exp(-osc.decay * osc.time);
		// Хвост события гасим ещё и линейно, иначе на последнем кадре
		// колебание обрывается посередине и это читается как щелчок.
		const f32 tail = 1.0f - osc.time / osc.duration;
		const f32 gain = envelope * tail * tail;
		const f32 phase = 2.0f * (f32)M_PI * osc.frequency * osc.time;

		f32 wave[3];
		for (int axis = 0; axis < 3; axis++)
			wave[axis] = OSC_AXIS[axis] * gain *
					std::sin(phase * OSC_FREQ[axis] + OSC_PHASE[axis]);

		result.rotation += v3f(wave[0], wave[1], wave[2]) * core::RADTODEG;
		i++;
	}

	return result;
}
