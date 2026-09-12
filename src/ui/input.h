// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include <IEventReceiver.h>

namespace Rml
{
class Context;
}

namespace ui
{

// Переводит события Irrlicht в вызовы контекста RmlUi. Возвращает true, если
// событие поглощено интерфейсом и дальше по клиенту идти не должно.
class Input
{
public:
	bool feed(Rml::Context &context, const SEvent &event);

private:
	int modifiers(bool shift, bool control) const;

	bool m_alt_down = false;
};

}
