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

// Привязки GL, которые драйвер Irrlicht помнит в своём кэше и после чужих
// вызовов считает нетронутыми: буферы, VAO, программа, кадровый буфер,
// текстуры. Всё, что RmlUi делает с GL — кадр, компиляция геометрии при
// создании рендера, освобождение при обновлении, — идёт между capture() и
// restore(), иначе драйвер рисует с чужими привязками и падает.
class GlBindings
{
public:
	void capture();
	void restore() const;
	int framebuffer() const { return m_framebuffer; }

private:
	int m_vertex_array = 0;
	int m_array_buffer = 0;
	int m_program = 0;
	int m_framebuffer = 0;
	int m_active_texture = 0;
	int m_textures[4] = {};
};

class GlBindingsGuard
{
public:
	GlBindingsGuard() { m_bindings.capture(); }
	~GlBindingsGuard() { m_bindings.restore(); }

	GlBindingsGuard(const GlBindingsGuard &) = delete;
	GlBindingsGuard &operator=(const GlBindingsGuard &) = delete;

private:
	GlBindings m_bindings;
};

// Рендер RmlUi поверх контекста OpenGL 3, который держит драйвер Irrlicht.
// Кадр UI рисуется между сценой и endScene(). Бэкенд из поставки сам
// возвращает блендинг, трафарет и область отсечения; привязки — GlBindings.
class Renderer final : public RenderInterface_GL3
{
public:
	explicit Renderer(video::IVideoDriver *driver) : m_driver(driver) {}

	~Renderer();

	// Кадр UI начинается с копии того, что уже нарисовано под ним: иначе
	// backdrop-filter видит пустой слой, а не сцену.
	void beginFrame(int width, int height);
	void endFrame();

	Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
			const Rml::String &source) override;

private:
	void copyScene(int width, int height);

	video::IVideoDriver *m_driver;
	GlBindings m_bindings;
	// Снимок сцены и квад на весь экран, которым он кладётся в слой RmlUi.
	unsigned m_scene_texture = 0;
	Rml::CompiledGeometryHandle m_scene_quad = 0;
	int m_scene_width = 0;
	int m_scene_height = 0;
};

}
