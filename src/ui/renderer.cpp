// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "renderer.h"

#include <IImage.h>
#include <IVideoDriver.h>
#include <RmlUi/Core/Log.h>
#include <mt_opengl.h>
#include <vector>

namespace ui
{

void GlBindings::capture()
{
	GL.GetIntegerv(GL.VERTEX_ARRAY_BINDING, &m_vertex_array);
	GL.GetIntegerv(GL.ARRAY_BUFFER_BINDING, &m_array_buffer);
	GL.GetIntegerv(GL.CURRENT_PROGRAM, &m_program);
	GL.GetIntegerv(GL.FRAMEBUFFER_BINDING, &m_framebuffer);
	GL.GetIntegerv(GL.ACTIVE_TEXTURE, &m_active_texture);
	for (int i = 0; i < 4; i++) {
		GL.ActiveTexture(GL.TEXTURE0 + i);
		GL.GetIntegerv(GL.TEXTURE_BINDING_2D, &m_textures[i]);
	}
	GL.ActiveTexture(m_active_texture);
}

void GlBindings::restore() const
{
	for (int i = 0; i < 4; i++) {
		GL.ActiveTexture(GL.TEXTURE0 + i);
		GL.BindTexture(GL.TEXTURE_2D, m_textures[i]);
	}
	GL.ActiveTexture(m_active_texture);
	GL.BindFramebuffer(GL.FRAMEBUFFER, m_framebuffer);
	GL.UseProgram(m_program);
	GL.BindVertexArray(m_vertex_array);
	GL.BindBuffer(GL.ARRAY_BUFFER, m_array_buffer);
}

Renderer::~Renderer()
{
	if (m_scene_quad)
		ReleaseGeometry(m_scene_quad);
	if (m_scene_texture)
		GL.DeleteTextures(1, &m_scene_texture);
}

void Renderer::beginFrame(int width, int height)
{
	m_bindings.capture();
	BeginFrame();
	copyScene(width, height);
}

// Сцена снимается в текстуру и рисуется квадом в слой RmlUi. Блит был бы
// проще, но слой многосэмпловый ради ровных скруглений, а блит из обычного
// буфера в многосэмпловый OpenGL запрещает.
void Renderer::copyScene(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	if (!m_scene_texture)
		GL.GenTextures(1, &m_scene_texture);
	GL.BindTexture(GL.TEXTURE_2D, m_scene_texture);
	if (width != m_scene_width || height != m_scene_height) {
		GL.TexImage2D(GL.TEXTURE_2D, 0, GL.RGBA8, width, height, 0, GL.RGBA,
				GL.UNSIGNED_BYTE, nullptr);
		GL.TexParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.LINEAR);
		GL.TexParameteri(GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER, GL.LINEAR);
		GL.TexParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, GL.CLAMP_TO_EDGE);
		GL.TexParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, GL.CLAMP_TO_EDGE);
		m_scene_width = width;
		m_scene_height = height;

		if (m_scene_quad)
			ReleaseGeometry(m_scene_quad);
		// Текстура из буфера кадра лежит снизу вверх, у RmlUi верх — ноль.
		const float w = (float)width, h = (float)height;
		const Rml::ColourbPremultiplied white(255, 255, 255, 255);
		const Rml::Vertex vertices[4] = {
			{Rml::Vector2f(0, 0), white, Rml::Vector2f(0, 1)},
			{Rml::Vector2f(w, 0), white, Rml::Vector2f(1, 1)},
			{Rml::Vector2f(w, h), white, Rml::Vector2f(1, 0)},
			{Rml::Vector2f(0, h), white, Rml::Vector2f(0, 0)},
		};
		const int indices[6] = {0, 1, 2, 2, 3, 0};
		m_scene_quad = CompileGeometry(Rml::Span<const Rml::Vertex>(vertices, 4),
				Rml::Span<const int>(indices, 6));
	}

	// BeginFrame() оставил привязанным слой для UI; читаем из буфера, что
	// был текущим до нас.
	GL.BindFramebuffer(GL.READ_FRAMEBUFFER, m_bindings.framebuffer());
	GL.BindTexture(GL.TEXTURE_2D, m_scene_texture);
	GL.CopyTexSubImage2D(GL.TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
	RenderGeometry(m_scene_quad, Rml::Vector2f(0, 0), (Rml::TextureHandle)m_scene_texture);
}

void Renderer::endFrame()
{
	EndFrame();
	m_bindings.restore();
}

Rml::TextureHandle Renderer::LoadTexture(Rml::Vector2i &texture_dimensions,
		const Rml::String &source)
{
	video::IImage *image = m_driver->createImageFromFile(source.c_str());
	if (!image) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load image \"%s\"",
				source.c_str());
		return 0;
	}

	const core::dimension2du size = image->getDimension();
	std::vector<Rml::byte> pixels(size.Width * size.Height * 4);
	image->copyToScaling(pixels.data(), size.Width, size.Height,
			video::ECF_A8R8G8B8);
	image->drop();

	// Irrlicht отдаёт BGRA с обычной альфой, RmlUi ждёт RGBA с домноженной.
	for (size_t i = 0; i < pixels.size(); i += 4) {
		const Rml::byte b = pixels[i], r = pixels[i + 2], a = pixels[i + 3];
		pixels[i] = Rml::byte(r * a / 255);
		pixels[i + 1] = Rml::byte(pixels[i + 1] * a / 255);
		pixels[i + 2] = Rml::byte(b * a / 255);
	}

	texture_dimensions = Rml::Vector2i(size.Width, size.Height);
	return GenerateTexture(pixels, texture_dimensions);
}

}
