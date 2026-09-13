// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2023 DS

#include "sound.h"

#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "settings.h"
#include "util/numeric.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

std::vector<std::string> SoundFallbackPathProvider::
		getLocalFallbackPathsForSoundname(const std::string &name)
{
	std::vector<std::string> paths;

	// only try each name once
	if (m_done_names.count(name))
		return paths;
	m_done_names.insert(name);

	addThePaths(name, paths);

	// remove duplicates
	std::sort(paths.begin(), paths.end());
	auto end = std::unique(paths.begin(), paths.end());
	paths.erase(end, paths.end());

	return paths;
}

void SoundFallbackPathProvider::addAllAlternatives(const std::string &common,
		std::vector<std::string> &paths)
{
	paths.reserve(paths.size() + 11);
	for (auto &&ext : {".ogg", ".0.ogg", ".1.ogg", ".2.ogg", ".3.ogg", ".4.ogg",
			".5.ogg", ".6.ogg", ".7.ogg", ".8.ogg", ".9.ogg", }) {
		paths.push_back(common + ext);
	}
}

void SoundFallbackPathProvider::addThePaths(const std::string &name,
		std::vector<std::string> &paths)
{
	addAllAlternatives(porting::path_share + DIR_DELIM + "sounds" + DIR_DELIM + name, paths);
	addAllAlternatives(porting::path_user + DIR_DELIM + "sounds" + DIR_DELIM + name, paths);
}

void ISoundManager::reportRemovedSound(sound_handle_t id)
{
	if (id <= 0)
		return;

	freeId(id);
	m_removed_sounds.push_back(id);
}

sound_handle_t ISoundManager::allocateId(u32 num_owners)
{
	while (m_occupied_ids.find(m_next_id) != m_occupied_ids.end()
			|| m_next_id == SOUND_HANDLE_T_MAX) {
		m_next_id = static_cast<sound_handle_t>(
				myrand() % static_cast<u32>(SOUND_HANDLE_T_MAX - 1) + 1);
	}
	sound_handle_t id = m_next_id++;
	m_occupied_ids.emplace(id, num_owners);
	return id;
}

void ISoundManager::freeId(sound_handle_t id, u32 num_owners)
{
	auto it = m_occupied_ids.find(id);
	if (it == m_occupied_ids.end())
		return;
	if (it->second <= num_owners)
		m_occupied_ids.erase(it);
	else
		it->second -= num_owners;
}

void sound_volume_control(ISoundManager *sound_mgr, bool is_window_active)
{
	float target = 0.0f;
	if (!g_settings->getBool("mute_sound")) {
		// Check if volume is in the proper range, else fix it.
		float old_volume = g_settings->getFloat("sound_volume");
		target = rangelim(old_volume, 0.0f, 1.0f);

		if (old_volume != target) {
			g_settings->setFloat("sound_volume", target);
		}

		if (!is_window_active) {
			target *= g_settings->getFloat("sound_volume_unfocused");
			target = rangelim(target, 0.0f, 1.0f);
		}
	}

	// Скольжение к цели: смена окна гасит звук за полсекунды и так же
	// возвращает, а не обрывает на полуслове. Состояние общее на процесс —
	// слушатель у OpenAL один, кто бы им ни владел, меню или игра.
	static float current = -1.0f;
	static u64 last_ms = 0;
	const u64 now_ms = porting::getTimeMs();
	if (current < 0.0f || now_ms < last_ms || now_ms - last_ms > 1000) {
		current = target;
	} else {
		const float k = std::min(1.0f, (now_ms - last_ms) * 0.006f);
		current += (target - current) * k;
		if (std::fabs(target - current) < 0.002f)
			current = target;
	}
	last_ms = now_ms;
	sound_mgr->setListenerGain(current);
}
