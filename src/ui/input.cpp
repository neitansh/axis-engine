// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "input.h"

#include "util/string.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>

namespace ui
{

static Rml::Input::KeyIdentifier translateKey(EKEY_CODE key)
{
	using namespace Rml::Input;

	if (key >= KEY_KEY_0 && key <= KEY_KEY_9)
		return KeyIdentifier(KI_0 + (key - KEY_KEY_0));
	if (key >= KEY_KEY_A && key <= KEY_KEY_Z)
		return KeyIdentifier(KI_A + (key - KEY_KEY_A));
	if (key >= KEY_NUMPAD0 && key <= KEY_NUMPAD9)
		return KeyIdentifier(KI_NUMPAD0 + (key - KEY_NUMPAD0));
	if (key >= KEY_F1 && key <= KEY_F24)
		return KeyIdentifier(KI_F1 + (key - KEY_F1));

	switch (key) {
	case KEY_SPACE: return KI_SPACE;
	case KEY_BACK: return KI_BACK;
	case KEY_TAB: return KI_TAB;
	case KEY_CLEAR: return KI_CLEAR;
	case KEY_RETURN: return KI_RETURN;
	case KEY_PAUSE: return KI_PAUSE;
	case KEY_CAPITAL: return KI_CAPITAL;
	case KEY_ESCAPE: return KI_ESCAPE;
	case KEY_PRIOR: return KI_PRIOR;
	case KEY_NEXT: return KI_NEXT;
	case KEY_END: return KI_END;
	case KEY_HOME: return KI_HOME;
	case KEY_LEFT: return KI_LEFT;
	case KEY_UP: return KI_UP;
	case KEY_RIGHT: return KI_RIGHT;
	case KEY_DOWN: return KI_DOWN;
	case KEY_SELECT: return KI_SELECT;
	case KEY_PRINT: return KI_PRINT;
	case KEY_SNAPSHOT: return KI_SNAPSHOT;
	case KEY_INSERT: return KI_INSERT;
	case KEY_DELETE: return KI_DELETE;
	case KEY_HELP: return KI_HELP;
	case KEY_LWIN: return KI_LWIN;
	case KEY_RWIN: return KI_RWIN;
	case KEY_APPS: return KI_APPS;
	case KEY_SLEEP: return KI_SLEEP;
	case KEY_MULTIPLY: return KI_MULTIPLY;
	case KEY_ADD: return KI_ADD;
	case KEY_SEPARATOR: return KI_SEPARATOR;
	case KEY_SUBTRACT: return KI_SUBTRACT;
	case KEY_DECIMAL: return KI_DECIMAL;
	case KEY_DIVIDE: return KI_DIVIDE;
	case KEY_NUMLOCK: return KI_NUMLOCK;
	case KEY_SCROLL: return KI_SCROLL;
	case KEY_SHIFT:
	case KEY_LSHIFT: return KI_LSHIFT;
	case KEY_RSHIFT: return KI_RSHIFT;
	case KEY_CONTROL:
	case KEY_LCONTROL: return KI_LCONTROL;
	case KEY_RCONTROL: return KI_RCONTROL;
	case KEY_MENU:
	case KEY_LMENU: return KI_LMENU;
	case KEY_RMENU: return KI_RMENU;
	case KEY_OEM_1: return KI_OEM_1;
	case KEY_PLUS: return KI_OEM_PLUS;
	case KEY_COMMA: return KI_OEM_COMMA;
	case KEY_MINUS: return KI_OEM_MINUS;
	case KEY_PERIOD: return KI_OEM_PERIOD;
	case KEY_OEM_2: return KI_OEM_2;
	case KEY_OEM_3: return KI_OEM_3;
	case KEY_OEM_4: return KI_OEM_4;
	case KEY_OEM_5: return KI_OEM_5;
	case KEY_OEM_6: return KI_OEM_6;
	case KEY_OEM_7: return KI_OEM_7;
	case KEY_OEM_8: return KI_OEM_8;
	case KEY_OEM_102: return KI_OEM_102;
	default: return KI_UNKNOWN;
	}
}

int Input::modifiers(bool shift, bool control) const
{
	int state = 0;
	if (shift)
		state |= Rml::Input::KM_SHIFT;
	if (control)
		state |= Rml::Input::KM_CTRL;
	if (m_alt_down)
		state |= Rml::Input::KM_ALT;
	return state;
}

bool Input::feed(Rml::Context &context, const SEvent &event)
{
	switch (event.EventType) {
	case EET_MOUSE_INPUT_EVENT: {
		const auto &m = event.MouseInput;
		const int mods = modifiers(m.Shift, m.Control);
		switch (m.Event) {
		case EMIE_MOUSE_MOVED:
			return !context.ProcessMouseMove(m.X, m.Y, mods);
		case EMIE_LMOUSE_PRESSED_DOWN:
			return !context.ProcessMouseButtonDown(0, mods);
		case EMIE_RMOUSE_PRESSED_DOWN:
			return !context.ProcessMouseButtonDown(1, mods);
		case EMIE_MMOUSE_PRESSED_DOWN:
			return !context.ProcessMouseButtonDown(2, mods);
		case EMIE_LMOUSE_LEFT_UP:
			return !context.ProcessMouseButtonUp(0, mods);
		case EMIE_RMOUSE_LEFT_UP:
			return !context.ProcessMouseButtonUp(1, mods);
		case EMIE_MMOUSE_LEFT_UP:
			return !context.ProcessMouseButtonUp(2, mods);
		case EMIE_MOUSE_WHEEL:
			// У Irrlicht положительное колесо — от себя, у RmlUi — к себе.
			return !context.ProcessMouseWheel(-m.Wheel, mods);
		default:
			return false;
		}
	}

	case EET_KEY_INPUT_EVENT: {
		const auto &k = event.KeyInput;
		if (k.Key == KEY_MENU || k.Key == KEY_LMENU || k.Key == KEY_RMENU)
			m_alt_down = k.PressedDown;

		const int mods = modifiers(k.Shift, k.Control);
		const Rml::Input::KeyIdentifier key = translateKey(k.Key);
		if (!k.PressedDown)
			return !context.ProcessKeyUp(key, mods);

		bool consumed = !context.ProcessKeyDown(key, mods);
		// Символ приходит здесь только пока текстовый ввод SDL выключен;
		// с ним буквы идут отдельным строковым событием.
		if (k.Char >= 32 && !k.Control && k.Key != KEY_DELETE) {
			std::wstring ch(1, k.Char);
			consumed = !context.ProcessTextInput(wide_to_utf8(ch)) || consumed;
		} else if (k.Key == KEY_RETURN) {
			consumed = !context.ProcessTextInput('\n') || consumed;
		}
		return consumed;
	}

	case EET_STRING_INPUT_EVENT: {
		std::wstring text(event.StringInput.Str->c_str());
		return !context.ProcessTextInput(wide_to_utf8(text));
	}

	default:
		return false;
	}
}

}
