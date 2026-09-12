// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "system.h"

#include "filesys.h"
#include "log_internal.h"
#include "porting.h"
#include <ICursorControl.h>
#include <IOSOperator.h>
#include <IrrlichtDevice.h>

namespace ui
{

double System::GetElapsedTime()
{
	return porting::getTimeUs() / 1.0e6;
}

bool System::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
	LogLevel level = LL_INFO;
	switch (type) {
	case Rml::Log::LT_ALWAYS:
	case Rml::Log::LT_ERROR:
	case Rml::Log::LT_ASSERT:
		level = LL_ERROR;
		break;
	case Rml::Log::LT_WARNING:
		level = LL_WARNING;
		break;
	case Rml::Log::LT_DEBUG:
		level = LL_VERBOSE;
		break;
	default:
		break;
	}
	g_logger.log(level, "RmlUi: " + message);
	return true;
}

void System::JoinPath(Rml::String &translated_path, const Rml::String &document_path,
		const Rml::String &path)
{
	Rml::SystemInterface::JoinPath(translated_path, document_path, path);
	if (fs::PathExists(translated_path))
		return;
	// Чего нет рядом с документом, ищется от корня данных движка: так тема
	// берёт textures/base/pack/… не зная, где сама лежит.
	std::string shared = porting::path_share;
	shared.append(DIR_DELIM).append(path);
	if (fs::PathExists(shared))
		translated_path = shared;
}

void System::SetMouseCursor(const Rml::String &cursor_name)
{
	gui::ICursorControl *cursor = m_device->getCursorControl();
	if (!cursor)
		return;

	gui::ECURSOR_ICON icon = gui::ECI_NORMAL;
	if (cursor_name == "pointer")
		icon = gui::ECI_HAND;
	else if (cursor_name == "text")
		icon = gui::ECI_IBEAM;
	else if (cursor_name == "move")
		icon = gui::ECI_SIZEALL;
	else if (cursor_name == "wait" || cursor_name == "progress")
		icon = gui::ECI_WAIT;
	else if (cursor_name == "not-allowed")
		icon = gui::ECI_NO;
	else if (cursor_name == "help")
		icon = gui::ECI_HELP;
	else if (cursor_name == "crosshair")
		icon = gui::ECI_CROSS;
	else if (cursor_name == "ns-resize" || cursor_name == "row-resize")
		icon = gui::ECI_SIZENS;
	else if (cursor_name == "ew-resize" || cursor_name == "col-resize")
		icon = gui::ECI_SIZEWE;
	cursor->setActiveIcon(icon);
}

void System::SetClipboardText(const Rml::String &text)
{
	m_device->getOSOperator()->copyToClipboard(text.c_str());
}

void System::GetClipboardText(Rml::String &text)
{
	const char *clipboard = m_device->getOSOperator()->getTextFromClipboard();
	text = clipboard ? clipboard : "";
}

void System::ActivateKeyboard(Rml::Vector2f caret_position, float line_height)
{
	const core::rect<s32> area(
			(s32)caret_position.x, (s32)caret_position.y,
			(s32)caret_position.x + 1, (s32)(caret_position.y + line_height));
	m_device->requestTextInput(&area);
}

void System::DeactivateKeyboard()
{
	m_device->requestTextInput(nullptr);
}

}
