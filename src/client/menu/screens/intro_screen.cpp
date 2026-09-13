// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "intro_screen.h"

#include "client/menu/main_menu.h"
#include "porting.h"
#include "util/basic_macros.h"
#include "version.h"
#include <RmlUi/Core/ElementDocument.h>
#include <cstring>

namespace menu
{

namespace
{

struct Step
{
	u64 at_ms;
	const char *card;
	bool on;
};

// Карточка живёт полторы секунды, гаснет за 0.4 (переход в теме), следующая
// встаёт после паузы. Музыка начинается вместе с карточкой движка.
const Step TIMELINE[] = {
	{0, "studio", true},
	{1600, "studio", false},
	{2000, "engine", true},
	{3600, "engine", false},
	{4000, nullptr, false},
};

constexpr u64 SKIP_AFTER_MS = 500;

}

void IntroScreen::bind(Rml::DataModelConstructor &model)
{
	m_version = g_version_hash;
	model.Bind("version", &m_version);
}

void IntroScreen::entered()
{
	m_started = porting::getTimeMs();
	m_stage = 0;
	menu().showChrome(false);
	menu().sounds().holdMusic(true);
	menu().sounds().play("sting");
}

void IntroScreen::left()
{
	menu().showChrome(true);
}

void IntroScreen::afterUpdate()
{
	const u64 elapsed = porting::getTimeMs() - m_started;
	while (m_stage < (int)ARRLEN(TIMELINE) && elapsed >= TIMELINE[m_stage].at_ms) {
		const Step &step = TIMELINE[m_stage++];
		if (!step.card) {
			finish();
			return;
		}
		card(step.card, step.on);
		if (step.on && strcmp(step.card, "engine") == 0)
			menu().sounds().holdMusic(false);
	}
}

bool IntroScreen::onEvent(const SEvent &event)
{
	const bool key = event.EventType == EET_KEY_INPUT_EVENT && event.KeyInput.PressedDown;
	const bool button = event.EventType == EET_MOUSE_INPUT_EVENT
			&& (event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN
					|| event.MouseInput.Event == EMIE_RMOUSE_PRESSED_DOWN);
	if (!key && !button)
		return false;
	if (porting::getTimeMs() - m_started >= SKIP_AFTER_MS)
		finish();
	return true;
}

void IntroScreen::card(const char *id, bool on)
{
	if (Rml::Element *element = document() ? document()->GetElementById(id) : nullptr)
		element->SetClass("on", on);
}

void IntroScreen::finish()
{
	menu().sounds().holdMusic(false);
	menu().navigate("start");
}

}
