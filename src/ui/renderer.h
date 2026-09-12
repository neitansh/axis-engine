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
// Кадр UI рисуется между сценой и endScene(). Драйвер кэширует состояние GL
// и после кадра считает привязанными свои буферы, программу и текстуры —
// beginFrame()/endFrame() возвращают их на место, бэкенд сам этого не делает.
class Renderer final : public RenderInterface_GL3
{
public:
	explicit Renderer(video::IVideoDriver *driver) : m_driver(driver) {}

	void beginFrame();
	void endFrame();

	Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
			const Rml::String &source) override;

private:
	struct Bindings
	{
		int vertex_array = 0;
		int array_buffer = 0;
		int program = 0;
		int framebuffer = 0;
		int active_texture = 0;
		int textures[4] = {};
	};

	video::IVideoDriver *m_driver;
	Bindings m_saved;
};

}
