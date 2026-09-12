// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "start_screen.h"

namespace menu
{

StartScreen::StartScreen(MainMenu &menu, const std::string &notice) :
	Screen(menu, "start"),
	m_notice(notice)
{
}

void StartScreen::bind(Rml::DataModelConstructor &model)
{
	model.Bind("notice", &m_notice);
	model.BindEventCallback("dismiss",
			[this](Rml::DataModelHandle handle, Rml::Event &, const Rml::VariantList &) {
				m_notice.clear();
				handle.DirtyVariable("notice");
			});
}

void StartScreen::refresh()
{
	model().DirtyAllVariables();
}

}
