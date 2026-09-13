// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <RmlUi/Core/SystemInterface.h>

class IrrlichtDevice;

namespace ui
{

// Часы, журнал, буфер обмена, курсор и клавиатура для RmlUi — всё через
// устройство Irrlicht, чтобы UI вёл себя как остальной клиент.
class System final : public Rml::SystemInterface
{
public:
	explicit System(IrrlichtDevice *device) : m_device(device) {}

	double GetElapsedTime() override;
	bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;
	// Переводится только текст в [[скобках]]. RmlUi зовёт этот метод и для
	// текста, собранного из данных (имя мира, значение настройки), и без
	// метки любое слово, совпавшее с ключом перевода, уехало бы на другой язык.
	int TranslateString(Rml::String &translated, const Rml::String &input) override;
	void JoinPath(Rml::String &translated_path, const Rml::String &document_path,
			const Rml::String &path) override;
	void SetMouseCursor(const Rml::String &cursor_name) override;
	void SetClipboardText(const Rml::String &text) override;
	void GetClipboardText(Rml::String &text) override;
	void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
	void DeactivateKeyboard() override;

private:
	IrrlichtDevice *m_device;
};

}
