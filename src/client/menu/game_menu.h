// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "screen.h"
#include "screen_host.h"
#include "sounds.h"
#include "ui/host.h"
#include <IEventReceiver.h>
#include <memory>
#include <string>
#include <vector>

class MyEventReceiver;
class IGameCallback;

namespace menu
{

// Меню поверх игры: пауза и настройки на тех же экранах RmlUi, что и главное
// меню, в своём контексте. Пока открыто, забирает весь ввод у игры (как
// модальное окно Irrlicht) и рисуется после сцены и HUD. Одинаково для
// любого крейта: тема — движка, крейт сможет положить свою поверх тем же
// путём, что и для главного меню.
class GameMenu : public ScreenHost, private IEventReceiver
{
public:
	GameMenu(ui::Host &host, MyEventReceiver *receiver, IGameCallback *callback,
			bool singleplayer);
	~GameMenu() override;

	GameMenu(const GameMenu &) = delete;
	GameMenu &operator=(const GameMenu &) = delete;

	bool ok() const { return m_context != nullptr; }
	bool isOpen() const { return m_open; }

	void open();
	void close();

	// Каждый кадр игры: обновление контекста и звука; рисование — после
	// сцены, до endScene.
	void step(f32 dtime, bool window_active);
	void render();

	bool singleplayer() const { return m_singleplayer; }
	// Кнопки паузы: выйти в главное меню, сменить пароль.
	void disconnect();
	void changePassword();

	Rml::Context &context() override { return *m_context; }
	std::string themeFile(const std::string &name) const override;
	void navigate(const std::string &screen) override;
	Screen *findScreen(const std::string &name) override;
	Sounds &sounds() override { return *m_sounds; }
	void showKeys(const std::vector<KeyHint> &keys) override;
	void quit() override;

private:
	bool OnEvent(const SEvent &event) override;
	void addScreen(std::unique_ptr<Screen> screen);
	void loadChrome();

	ui::Host &m_host;
	MyEventReceiver *m_receiver;
	IGameCallback *m_callback;
	bool m_singleplayer;
	Rml::Context *m_context = nullptr;
	std::unique_ptr<Sounds> m_sounds;
	std::vector<std::string> m_theme_dirs;
	std::vector<std::unique_ptr<Screen>> m_screens;
	Screen *m_current = nullptr;
	Rml::ElementDocument *m_chrome = nullptr;
	Rml::DataModelHandle m_chrome_model;
	std::vector<KeyHint> m_keys;
	// Рамка та же, что у главного меню, но версии в углу поверх игры не место.
	Rml::String m_version;
	bool m_open = false;
};

}
