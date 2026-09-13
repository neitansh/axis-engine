// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <IEventReceiver.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <string>
#include <vector>

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

	// Пока экран показан, у body стоит класс shown: анимация появления в теме
	// вешается на него и потому заново идёт при каждом возврате на экран.
	void show();
	void hide();
	void reload();

	// Зовётся перед каждым показом и всякий раз, когда меню узнало что-то
	// новое для экрана (пришли отклики серверов, билет): экран перечитывает
	// своё состояние в модель.
	virtual void refresh() {}

	// Экран открыли или закрыли: тому, что живёт при меню (очередь на матч),
	// важно знать, на виду ли он.
	virtual void entered() {}
	virtual void left() {}

	// Ввод до RmlUi: экран забирает событие, вернув true. Нужно тому, что
	// документ сам не умеет — например, поймать клавишу для привязки.
	virtual bool onEvent(const SEvent &event) { return false; }

	// Клавиша, которую RmlUi не взял: стрелка упёрлась в край прокручиваемого
	// списка — RmlUi не ходит стрелками через его границу, и экран сам
	// переносит фокус на соседний остров (см. hop()).
	virtual void onUnhandledKey(const SEvent &event) {}

	// Зовётся меню после каждого обновления контекста, пока экран показан.
	void settle();
	virtual void afterUpdate() {}

	// Подсказка по клавишам в нижней строке рамки: пары «клавиша — действие».
	struct KeyHint
	{
		Rml::String key;
		Rml::String label;
	};
	virtual std::vector<KeyHint> keys() const { return {}; }

protected:
	// Наследник вешает свои переменные и действия на модель; общие
	// действия (nav, quit) уже стоят.
	virtual void bind(Rml::DataModelConstructor &model) = 0;

	MainMenu &menu() { return m_menu; }
	Rml::DataModelHandle model() { return m_model; }
	Rml::ElementDocument *document() { return m_document; }

	// Фокус в текстовом поле: буквы-горячие клавиши экрана в это время — текст.
	bool typing() const;

	// Элемент в фокусе и перенос фокуса на первый элемент по селектору.
	Rml::Element *focused() const;
	bool hop(const char *selector);
	// Снова встать на элемент с autofocus после ближайшего обновления —
	// когда модель поменялась и разметка с ним ещё не перестроена.
	void refocus() { m_autofocus_pending = true; }
	static int arrowOf(const SEvent &event);

private:
	void load();
	void unload();

	MainMenu &m_menu;
	std::string m_name;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_model;
	bool m_shown = false;
	bool m_autofocus_pending = false;
};

}
