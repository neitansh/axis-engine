// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#pragma once

#include "RmlUi_Renderer_GL3.h"

namespace video
{
class IVideoDriver;
}

namespace ui
{

// Рендер RmlUi поверх контекста OpenGL 3, который держит драйвер Irrlicht.
// Кадр UI рисуется между сценой и endScene(): бэкенд сам сохраняет и
// возвращает состояние GL, так что кэш состояния в драйвере остаётся верным.
class Renderer final : public RenderInterface_GL3
{
public:
	explicit Renderer(video::IVideoDriver *driver) : m_driver(driver) {}

	Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
			const Rml::String &source) override;

private:
	video::IVideoDriver *m_driver;
};

}
