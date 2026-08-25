// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2022 x2048, Dmitry Kostenko <codeforsmile@gmail.com>

#include "pipeline.h"
#include "client/client.h"
#include "client/hud.h"
#include "gettext.h"
#include "IRenderTarget.h"
#include "SColor.h"
#include "profiler.h"
#include "threading/mutex_auto_lock.h"

#include <mutex>

#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif

#include <vector>
#include <memory>


TextureBuffer::~TextureBuffer()
{
	for (u32 index = 0; index < m_textures.size(); index++)
		m_driver->removeTexture(m_textures[index]);
	m_textures.clear();
}

video::ITexture *TextureBuffer::getTexture(u8 index)
{
	if (index >= m_textures.size())
		return nullptr;
	return m_textures[index];
}


void TextureBuffer::setTexture(u8 index, core::dimension2du size, const std::string &name, video::ECOLOR_FORMAT format, bool clear, u8 msaa)
{
	assert(index != NO_DEPTH_TEXTURE);

	if (m_definitions.size() <= index)
		m_definitions.resize(index + 1);

	auto &definition = m_definitions[index];
	definition.valid = true;
	definition.dirty = true;
	definition.fixed_size = true;
	definition.size = size;
	definition.name = name;
	definition.format = format;
	definition.clear = clear;
	definition.msaa = msaa;
}

void TextureBuffer::setTexture(u8 index, v2f scale_factor, const std::string &name, video::ECOLOR_FORMAT format, bool clear, u8 msaa)
{
	assert(index != NO_DEPTH_TEXTURE);

	if (m_definitions.size() <= index)
		m_definitions.resize(index + 1);

	auto &definition = m_definitions[index];
	definition.valid = true;
	definition.dirty = true;
	definition.fixed_size = false;
	definition.scale_factor = scale_factor;
	definition.name = name;
	definition.format = format;
	definition.clear = clear;
	definition.msaa = msaa;
}

void TextureBuffer::reset(PipelineContext &context)
{
	if (!m_driver)
		m_driver = context.device->getVideoDriver();

	// remove extra textures
	if (m_textures.size() > m_definitions.size()) {
		for (unsigned i = m_definitions.size(); i < m_textures.size(); i++)
			if (m_textures[i])
				m_driver->removeTexture(m_textures[i]);

		m_textures.set_used(m_definitions.size());
	}

	// add placeholders for new definitions
	while (m_textures.size() < m_definitions.size())
		m_textures.push_back(nullptr);

	// change textures to match definitions
	for (u32 i = 0; i < m_definitions.size(); i++) {
		video::ITexture **ptr = &m_textures[i];
		ensureTexture(ptr, m_definitions[i], context);
		if (m_definitions[i].valid && !*ptr) {
			throw ShaderException(
				fmtgettext("Failed to create the texture \"%s\" for the rendering pipeline.",
					m_definitions[i].name.c_str()) +
				strgettext("\nCheck debug.txt for details."));
		}
		m_definitions[i].dirty = false;
	}

	RenderSource::reset(context);
}

void TextureBuffer::swapTextures(u8 texture_a, u8 texture_b)
{
	assert(m_definitions[texture_a].valid && m_definitions[texture_b].valid);

	video::ITexture *temp = m_textures[texture_a];
	m_textures[texture_a] = m_textures[texture_b];
	m_textures[texture_b] = temp;
}


bool TextureBuffer::ensureTexture(video::ITexture **texture, const TextureDefinition& definition, PipelineContext &context)
{
	bool modify;
	core::dimension2du size;
	if (definition.valid) {
		if (definition.fixed_size)
			size = definition.size;
		else
			size = core::dimension2du(
					(u32)(context.target_size.X * definition.scale_factor.X),
					(u32)(context.target_size.Y * definition.scale_factor.Y));

		modify = definition.dirty || (*texture == nullptr) || (*texture)->getSize() != size;
	}
	else {
		modify = (*texture != nullptr);
	}

	if (!modify)
		return false;

	if (*texture) {
		m_driver->removeTexture(*texture);
		*texture = nullptr;
	}

	if (definition.valid) {
		if (!m_driver->queryTextureFormat(definition.format)) {
			errorstream << "Failed to create texture \"" << definition.name
				<< "\": unsupported format " << video::ColorFormatName(definition.format)
				<< std::endl;
			return false;
		}

		const core::dimension2du max_size = m_driver->getMaxTextureSize();
		if (size.Width > max_size.Width || size.Height > max_size.Height) {
			errorstream << "Failed to create texture \"" << definition.name
				<< "\": exceeds limit " << size.Width << "x" << size.Height
				<< std::endl;
			return false;
		}

		if (definition.clear) {
			// We're not able to clear a render target texture
			// We're not able to create a normal texture with MSAA
			// (could be solved by more refactoring in Irrlicht, but not needed for now)
			sanity_check(definition.msaa < 1);

			*texture = m_driver->addTexture(size, definition.name.c_str(), definition.format);
		} else if (definition.msaa > 0) {
			*texture = m_driver->addRenderTargetTextureMs(size, definition.msaa, definition.name.c_str(), definition.format);
		} else {
			*texture = m_driver->addRenderTargetTexture(size, definition.name.c_str(), definition.format);
		}

		if (!*texture) {
			errorstream << "Failed to create texture \"" << definition.name
				<< "\"" << std::endl;
			return false;
		}
	}

	return true;
}

TextureBufferOutput::TextureBufferOutput(TextureBuffer *_buffer, u8 _texture_index)
	: buffer(_buffer), texture_map({_texture_index})
{}

TextureBufferOutput::TextureBufferOutput(TextureBuffer *_buffer, const std::vector<u8> &_texture_map)
	: buffer(_buffer), texture_map(_texture_map)
{}

TextureBufferOutput::TextureBufferOutput(TextureBuffer *_buffer, const std::vector<u8> &_texture_map, u8 _depth_stencil)
	: buffer(_buffer), texture_map(_texture_map), depth_stencil(_depth_stencil)
{}

TextureBufferOutput::~TextureBufferOutput()
{
	if (render_target && driver)
		driver->removeRenderTarget(render_target);
}

void TextureBufferOutput::activate(PipelineContext &context)
{
	if (!driver)
		driver = context.device->getVideoDriver();

	if (!render_target)
		render_target = driver->addRenderTarget();

	core::array<video::ITexture *> textures;
	core::dimension2du size(0, 0);
	for (size_t i = 0; i < texture_map.size(); i++) {
		video::ITexture *texture = buffer->getTexture(texture_map[i]);
		textures.push_back(texture);
		if (texture && size.Width == 0)
			size = texture->getSize();
	}

	video::ITexture *depth_texture = nullptr;
	if (depth_stencil != NO_DEPTH_TEXTURE)
		depth_texture = buffer->getTexture(depth_stencil);

	if (render_target) {
		render_target->setTexture(textures, depth_texture);

		driver->setRenderTargetEx(render_target, m_clear ? video::ECBF_ALL : video::ECBF_NONE, context.clear_color);
		driver->OnResize(size);
	}

	RenderTarget::activate(context);
}

video::IRenderTarget *TextureBufferOutput::getIrrRenderTarget(PipelineContext &context)
{
	activate(context); // Needed to make sure that render_target is set up.
	return render_target;
}

u8 DynamicSource::getTextureCount()
{
	assert(isConfigured());
	return upstream->getTextureCount();
}

video::ITexture *DynamicSource::getTexture(u8 index)
{
	assert(isConfigured());
	return upstream->getTexture(index);
}

void ScreenTarget::activate(PipelineContext &context)
{
	auto driver = context.device->getVideoDriver();
	driver->setRenderTargetEx(nullptr, m_clear ? video::ECBF_ALL : video::ECBF_NONE, context.clear_color);
	driver->OnResize(size);
	RenderTarget::activate(context);
}

void DynamicTarget::activate(PipelineContext &context)
{
	if (!isConfigured())
		throw std::logic_error("Dynamic render target is not configured before activation.");
	upstream->activate(context);
}

void ScreenTarget::reset(PipelineContext &context)
{
	RenderTarget::reset(context);
	size = context.device->getVideoDriver()->getScreenSize();
}

SetRenderTargetStep::SetRenderTargetStep(RenderStep *_step, RenderTarget *_target)
	: step(_step), target(_target)
{
}

void SetRenderTargetStep::run(PipelineContext &context)
{
	step->setRenderTarget(target);
}

SwapTexturesStep::SwapTexturesStep(TextureBuffer *_buffer, u8 _texture_a, u8 _texture_b)
		: buffer(_buffer), texture_a(_texture_a), texture_b(_texture_b)
{
}

void SwapTexturesStep::run(PipelineContext &context)
{
	buffer->swapTextures(texture_a, texture_b);
}

RenderSource *RenderPipeline::getInput()
{
	return &m_input;
}

RenderTarget *RenderPipeline::getOutput()
{
	return &m_output;
}

namespace
{
	// Имена участков по номерам. Заполняется один раз на шаг, при первом
	// замере, и дальше только читается.
	std::vector<std::string> g_gpu_slot_names;
	std::vector<bool> g_gpu_slot_nested;
	std::mutex g_gpu_slot_mutex;

	// Насколько глубоко мы сейчас внутри конвейеров. Ноль - верхний уровень.
	thread_local int g_pipeline_depth = 0;
}

u32 RenderStep::getGpuSlot()
{
	if (!m_gpu_slot_valid) {
		MutexAutoLock lock(g_gpu_slot_mutex);
		m_gpu_slot = (u32)g_gpu_slot_names.size();
		g_gpu_slot_names.push_back(getProfilerName());
		g_gpu_slot_nested.push_back(g_pipeline_depth > 1);
		m_gpu_slot_valid = true;
	}
	return m_gpu_slot;
}

const std::string &getGpuSlotName(u32 slot)
{
	static const std::string empty;
	MutexAutoLock lock(g_gpu_slot_mutex);
	if (slot >= g_gpu_slot_names.size())
		return empty;
	return g_gpu_slot_names[slot];
}

bool isGpuSlotNested(u32 slot)
{
	MutexAutoLock lock(g_gpu_slot_mutex);
	return slot < g_gpu_slot_nested.size() && g_gpu_slot_nested[slot];
}

const std::string &RenderStep::getProfilerName()
{
	if (m_profiler_name.empty()) {
		const char *raw = typeid(*this).name();
#if defined(__GNUC__) || defined(__clang__)
		// У GCC и Clang имя типа приходит закодированным (12PostProcess...),
		// и читать профиль в таком виде нельзя.
		int status = 0;
		char *pretty = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
		if (status == 0 && pretty) {
			m_profiler_name = std::string("Pipeline: ") + pretty + " [us]";
			free(pretty);
		}
#endif
		if (m_profiler_name.empty())
			m_profiler_name = std::string("Pipeline: ") + raw + " [us]";
	}
	return m_profiler_name;
}

void RenderPipeline::run(PipelineContext &context)
{
	v2u32 original_size = context.target_size;
	context.target_size = v2u32(original_size.X * scale.X, original_size.Y * scale.Y);

	for (auto &object : m_objects)
		object->reset(context);

	auto *driver = context.device->getVideoDriver();
	const bool gpu_timing = driver->supportsTimerQueries();

	g_pipeline_depth++;

	for (auto &step: m_pipeline) {
		// Два замера на шаг, и они меряют разное. Секундомер здесь считает
		// процессорное время: сколько заняли сами вызовы. Метки видеокарты
		// считают её собственную работу, и ответ приходит через кадр-другой —
		// его подбирает Game::updateProfilers().
		ScopeProfiler sp(g_profiler, step->getProfilerName(), SPT_AVG, PRECISION_MICRO);
		if (gpu_timing)
			driver->beginTimerQuery(step->getGpuSlot());
		step->run(context);
		if (gpu_timing)
			driver->endTimerQuery();
	}

	g_pipeline_depth--;
	context.target_size = original_size;
}

void RenderPipeline::setRenderSource(RenderSource *source)
{
	m_input.setRenderSource(source);
}

void RenderPipeline::setRenderTarget(RenderTarget *target)
{
	m_output.setRenderTarget(target);
}
