#pragma once

#include "irrlichttypes.h"
#include "irr_v3d.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

struct DynamicLight {
	u32 id = 0;
	v3f position;
	v3f color;
	f32 radius = 0.0f;
};

class DynamicLightManager {
private:
	std::unordered_map<u32, DynamicLight> m_lights;
	u32 m_next_id = 1;

public:
	DynamicLightManager() = default;
	~DynamicLightManager() = default;

	// Регистрирует новый источник света и возвращает его уникальный ID
	u32 addLight(const v3f &position, const v3f &color, f32 radius) {
		u32 id = m_next_id++;
		m_lights[id] = { id, position, color, radius };
		return id;
	}

	// Полностью обновляет параметры существующего источника
	void updateLight(u32 id, const v3f &position, const v3f &color, f32 radius) {
		auto it = m_lights.find(id);
		if (it != m_lights.end()) {
			it->second.position = position;
			it->second.color = color;
			it->second.radius = radius;
		}
	}

	// Быстрое обновление только позиции (например, при движении игрока)
	void updateLightPosition(u32 id, const v3f &position) {
		auto it = m_lights.find(id);
		if (it != m_lights.end()) {
			it->second.position = position;
		}
	}

	// Удаляет источник света
	void removeLight(u32 id) {
		m_lights.erase(id);
	}

	// Полная очистка менеджера
	void clear() {
		m_lights.clear();
		m_next_id = 1;
	}

	// Возвращает N ближайших источников света к переданной позиции, отсортированных по расстоянию
	std::vector<DynamicLight> getClosestLights(const v3f &target, size_t max_count) const {
		std::vector<DynamicLight> result;
		result.reserve(m_lights.size());
		for (const auto &pair : m_lights) {
			result.push_back(pair.second);
		}

		// Сортируем по квадрату расстояния (быстрее, так как не вычисляем извлечение квадратного корня)
		std::sort(result.begin(), result.end(), [&target](const DynamicLight &a, const DynamicLight &b) {
			return target.getDistanceFromSQ(a.position) < target.getDistanceFromSQ(b.position);
		});

		if (result.size() > max_count) {
			result.resize(max_count);
		}
		return result;
	}
};