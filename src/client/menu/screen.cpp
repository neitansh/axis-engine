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

	m_document = ui::Host::loadDocument(context, m_menu.themeFile(m_name + ".rml"));
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
	entered();
	refresh();
	if (m_document) {
		m_document->Show();
		m_document->SetClass("shown", true);
		m_autofocus_pending = true;
	}
}

// Стрелки и Enter должны работать сразу: фокус встаёт на элемент с autofocus,
// а не ждёт первого нажатия. Ищется после обновления контекста — строки из
// data-for появляются только на нём.
void Screen::settle()
{
	if (m_autofocus_pending && m_document) {
		m_autofocus_pending = false;
		hop("[autofocus]");
	}
	afterUpdate();
}

Rml::Element *Screen::focused() const
{
	return m_menu.context().GetFocusElement();
}

bool Screen::hop(const char *selector)
{
	if (!m_document)
		return false;
	// Первый видимый: у data-for в дереве остаётся скрытый образец строки, и
	// селектор нашёл бы его раньше настоящих.
	Rml::ElementList found;
	m_document->QuerySelectorAll(found, selector);
	for (Rml::Element *target : found) {
		if (!target->IsVisible(true))
			continue;
		target->Focus(true);
		target->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Adaptive));
		return true;
	}
	return false;
}

// Стрелка как направление: 1 — вниз/вправо (вперёд), −1 — вверх/влево, 0 — не
// стрелка. Горизонталь и вертикаль различают сами экраны по коду клавиши.
int Screen::arrowOf(const SEvent &event)
{
	if (event.EventType != EET_KEY_INPUT_EVENT || !event.KeyInput.PressedDown)
		return 0;
	switch (event.KeyInput.Key) {
	case KEY_DOWN:
	case KEY_RIGHT:
		return 1;
	case KEY_UP:
	case KEY_LEFT:
		return -1;
	default:
		return 0;
	}
}

bool Screen::typing() const
{
	Rml::Element *focused = m_menu.context().GetFocusElement();
	return focused && focused->GetTagName() == "input"
			&& focused->GetAttribute<Rml::String>("type", "text") == "text";
}

void Screen::hide()
{
	m_shown = false;
	left();
	if (m_document) {
		m_document->SetClass("shown", false);
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
