// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "screen.h"

#include "log.h"
#include "main_menu.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

namespace menu
{

Screen::Screen(MainMenu &menu, const std::string &name) :
	m_menu(menu),
	m_name(name)
{
}

Screen::~Screen()
{
	unload();
}

void Screen::load()
{
	Rml::Context &context = m_menu.context();

	Rml::DataModelConstructor model = context.CreateDataModel(m_name);
	if (!model) {
		errorstream << "Screen \"" << m_name << "\": data model could not be created"
				<< std::endl;
		return;
	}
	model.BindEventCallback("nav",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &args) {
				if (!args.empty())
					m_menu.navigate(args[0].Get<Rml::String>());
			});
	model.BindEventCallback("quit",
			[this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
				m_menu.quit();
			});
	bind(model);
	m_model = model.GetModelHandle();

	m_document = context.LoadDocument(m_menu.themeFile(m_name + ".rml"));
	if (!m_document)
		errorstream << "Screen \"" << m_name << "\": document failed to load" << std::endl;
}

void Screen::unload()
{
	if (m_document) {
		m_document->Close();
		m_document = nullptr;
	}
	if (m_model) {
		m_menu.context().RemoveDataModel(m_name);
		m_model = {};
	}
}

void Screen::show()
{
	m_shown = true;
	if (!m_document)
		load();
	refresh();
	if (m_document) {
		// Класс enter снимается при скрытии и ставится при показе: RmlUi
		// запускает анимацию, когда свойство появляется, и без этого
		// вход играл бы только при первом открытии документа.
		m_document->SetClass("enter", true);
		m_document->Show();
	}
}

void Screen::hide()
{
	m_shown = false;
	if (m_document) {
		m_document->SetClass("enter", false);
		m_document->Hide();
	}
}

void Screen::reload()
{
	unload();
	if (m_shown)
		show();
}

}
