// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"

namespace menu
{

// Заставка при холодном старте: карточка студии, карточка движка, затем
// стартовый экран. Карточки — элементы документа с id, экран по таймеру
// ставит и снимает им класс on; вид и движение — в теме. Любая клавиша или
// кнопка мыши пропускает, но не раньше полсекунды, чтобы не мигало.
class IntroScreen : public Screen
{
public:
	explicit IntroScreen(MainMenu &menu) : Screen(menu, "intro") {}

	void entered() override;
	void left() override;
	void afterUpdate() override;
	bool onEvent(const SEvent &event) override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	void card(const char *id, bool on);
	void finish();

	u64 m_started = 0;
	int m_stage = 0;
	Rml::String m_version;
};

}
