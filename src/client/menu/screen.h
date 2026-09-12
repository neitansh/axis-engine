// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <IEventReceiver.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <string>

namespace Rml
{
class Context;
class ElementDocument;
}

namespace menu
{

class MainMenu;

// Экран главного меню: один документ RmlUi и одна модель данных под ним.
// Вид экрана — файл <имя>.rml в теме, логика — наследник этого класса.
// Документ грузится при первом показе и переживает скрытие; reload()
// перечитывает файл, чтобы вёрстку и стили можно было править вживую.
class Screen
{
public:
	Screen(MainMenu &menu, const std::string &name);
	virtual ~Screen();

	Screen(const Screen &) = delete;
	Screen &operator=(const Screen &) = delete;

	const std::string &name() const { return m_name; }

	void show();
	void hide();
	void reload();

	// Зовётся перед каждым показом: экран подтягивает то, что могло
	// измениться, пока его не было видно.
	virtual void refresh() {}

	// Ввод до RmlUi: экран забирает событие, вернув true. Нужно тому, что
	// документ сам не умеет — например, поймать клавишу для привязки.
	virtual bool onEvent(const SEvent &event) { return false; }

	// Зовётся после каждого обновления контекста, пока экран показан.
	virtual void afterUpdate() {}

protected:
	// Наследник вешает свои переменные и действия на модель; общие
	// действия (nav, quit) уже стоят.
	virtual void bind(Rml::DataModelConstructor &model) = 0;

	MainMenu &menu() { return m_menu; }
	Rml::DataModelHandle model() { return m_model; }
	Rml::ElementDocument *document() { return m_document; }

private:
	void load();
	void unload();

	MainMenu &m_menu;
	std::string m_name;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_model;
	bool m_shown = false;
};

}
