// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "client/menu/screen.h"
#include <vector>

namespace menu
{

// О программе: версия и сборка, папка данных, ссылки и авторы Luanti —
// движка, из которого Axis вырос.
class AboutScreen final : public Screen
{
public:
	explicit AboutScreen(MainMenu &menu);

	void refresh() override;
	bool onEvent(const SEvent &event) override;

protected:
	void bind(Rml::DataModelConstructor &model) override;

private:
	struct CreditLine
	{
		Rml::String kind;
		Rml::String name;
		Rml::String note;
	};

	void loadCredits();
	std::string report() const;

	Rml::String m_version;
	Rml::String m_build;
	Rml::String m_renderer;
	Rml::String m_platform;
	Rml::String m_user_path;
	Rml::String m_copied;
	std::vector<CreditLine> m_credits;
};

}
