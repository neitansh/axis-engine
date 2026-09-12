// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "renderer.h"

#include "log.h"
#include <IImage.h>
#include <IVideoDriver.h>
#include <RmlUi/Core/Log.h>
#include <vector>

namespace ui
{

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
