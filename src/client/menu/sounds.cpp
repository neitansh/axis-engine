// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "sounds.h"

#include "client/sound/sound_openal.h"
#include "filesys.h"
#include "sound_spec.h"
#include "util/string.h"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <algorithm>
#include <random>

namespace menu
{

namespace
{

constexpr float EFFECT_GAIN = 0.7f;
constexpr float MUSIC_GAIN = 0.45f;
constexpr float MUSIC_FADE_IN = 0.25f;

// Курсор наследуется, и у слова внутри пункта он тот же, что у пункта;
// звучит верхний из подряд идущих элементов с рукой — сам пункт.
Rml::Element *pointerTarget(Rml::Element *element)
{
	Rml::Element *found = nullptr;
	for (; element; element = element->GetParentNode()) {
		if (element->GetComputedValues().cursor() == "pointer")
			found = element;
		else if (found)
			break;
	}
	return found;
}

}

Sounds::Sounds(const std::vector<std::string> &theme_dirs)
{
#if USE_SOUND
	if (g_sound_manager_singleton)
		m_manager = createOpenALSoundManager(g_sound_manager_singleton.get(),
				std::make_unique<SoundFallbackPathProvider>());
#endif
	if (!m_manager)
		return;

	for (const std::string &dir : theme_dirs) {
		const std::string sounds = dir + DIR_DELIM "sounds";
		for (const char *effect : {"hover", "click"}) {
			if (m_manager->loadSoundFile(effect,
					sounds + DIR_DELIM + effect + ".ogg"))
				m_manager->addSoundToGroup(effect, effect);
		}
		if (!m_music.empty())
			continue;
		for (const fs::DirListNode &node : fs::GetDirListing(sounds)) {
			if (node.dir || node.name.rfind("music", 0) != 0
					|| !str_ends_with(node.name, ".ogg"))
				continue;
			const std::string name = "menu:" + node.name;
			if (m_manager->loadSoundFile(name, sounds + DIR_DELIM + node.name)) {
				m_manager->addSoundToGroup(name, name);
				m_music.push_back(name);
			}
		}
	}

	std::shuffle(m_music.begin(), m_music.end(), std::mt19937(std::random_device{}()));
	nextTrack();
}

Sounds::~Sounds()
{
	if (m_manager && m_track > 0)
		m_manager->freeId(m_track);
}

void Sounds::attach(Rml::Context &context)
{
	if (!m_manager)
		return;
	context.AddEventListener("mouseover", this);
	context.AddEventListener("mousedown", this);
}

void Sounds::step(f32 dtime, bool window_active)
{
	if (!m_manager)
		return;
	sound_volume_control(m_manager.get(), window_active);
	m_manager->step(dtime);
	for (sound_handle_t id : m_manager->pollRemovedSounds()) {
		m_manager->freeId(id);
		if (id == m_track) {
			m_track = 0;
			nextTrack();
		}
	}
}

void Sounds::ProcessEvent(Rml::Event &event)
{
	Rml::Element *target = pointerTarget(event.GetTargetElement());
	if (event.GetId() == Rml::EventId::Mouseover) {
		if (target && target != m_hovered)
			playEffect("hover");
		m_hovered = target;
	} else if (event.GetId() == Rml::EventId::Mousedown) {
		if (target && event.GetParameter<int>("button", 0) == 0)
			playEffect("click");
	}
}

void Sounds::playEffect(const std::string &name)
{
	m_manager->playSound(0, SoundSpec(name, EFFECT_GAIN));
}

void Sounds::nextTrack()
{
	if (m_music.empty())
		return;
	const std::string &name = m_music[m_music_pos];
	m_music_pos = (m_music_pos + 1) % m_music.size();
	m_track = m_manager->allocateId(1);
	m_manager->playSound(m_track, SoundSpec(name, MUSIC_GAIN, false, MUSIC_FADE_IN));
}

}
