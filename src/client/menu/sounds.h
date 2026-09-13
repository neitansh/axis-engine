// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/sound.h"
#include <RmlUi/Core/EventListener.h>
#include <memory>
#include <string>
#include <vector>

namespace Rml
{
class Context;
class Element;
}

namespace menu
{

// Звук меню: отклик на наведение и нажатие и музыка под меню. Файлы берутся
// из темы (`sounds/hover.ogg`, `sounds/click.ogg`, `sounds/music*.ogg`), треки
// идут по кругу в случайном порядке. Наведение и нажатие ловятся на корне
// контекста: звучит любой элемент с курсором-рукой, тема ничего не размечает.
class Sounds : public Rml::EventListener
{
public:
	explicit Sounds(const std::vector<std::string> &theme_dirs);
	~Sounds() override;

	Sounds(const Sounds &) = delete;
	Sounds &operator=(const Sounds &) = delete;

	void attach(Rml::Context &context);
	void step(f32 dtime, bool window_active);

	// Звук из темы по имени файла без расширения: hover, click, sting.
	void play(const std::string &name);

	// Пока держим, музыка не начинается: заставка пускает её сама, на своей
	// карточке, а не с первого кадра чёрного экрана.
	void holdMusic(bool hold) { m_hold_music = hold; }

	// Тема перечитана — прежние элементы мертвы, и наведение считается заново.
	void forgetHover() { m_hovered = nullptr; }

	void ProcessEvent(Rml::Event &event) override;

private:
	void nextTrack();

	std::unique_ptr<ISoundManager> m_manager;
	std::vector<std::string> m_music;
	size_t m_music_pos = 0;
	sound_handle_t m_track = 0;
	float m_music_gain = 0.0f;
	bool m_hold_music = false;
	Rml::Element *m_hovered = nullptr;
};

}
