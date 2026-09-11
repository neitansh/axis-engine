// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>
// Copyright (C) 2020 appgurueu, Lars Mueller <appgurulars@gmx.de>

#include "secondstage.h"

#include <cstdlib>
#include "client/client.h"
#include "client/fontengine.h"
#include <IGUIFont.h>
#include "client/localplayer.h"
#include "client/shader.h"
#include "settings.h"
#include "plain.h"
#include "porting.h"
#include "profiler.h"
#include <ISceneManager.h>

void ScreenCaptionStep::run(PipelineContext &context)
{
	LocalPlayer *player = context.client->getEnv().getLocalPlayer();
	if (!player || !m_target)
		return;
	const auto &st = player->eyelids;
	if (!st.visible() || st.caption.empty())
		return;

	m_target->activate(context);
	video::IVideoDriver *driver = context.device->getVideoDriver();
	driver->clearBuffers(video::ECBF_COLOR, video::SColor(0, 0, 0, 0));
	const core::dimension2du size = driver->getScreenSize();

	gui::IGUIFont *font = g_fontengine->getFont(
			FontSpec(std::max(16u, size.Height / 6), FM_Standard, true, false));
	if (!font)
		return;

	// Когда подпись видна и насколько, решает сведение; здесь она рисуется
	// в полную силу.
	const u32 alpha = 255;
	const core::rect<s32> frame(0, 0, size.Width, size.Height);
	const s32 drop = std::max<s32>(2, size.Height / 160);
	font->draw(st.caption.c_str(), frame + core::vector2d<s32>(0, drop),
			video::SColor(alpha * 4 / 5, 20, 0, 0), true, true);
	font->draw(st.caption.c_str(), frame, video::SColor(alpha, 236, 38, 38), true, true);
}

PostProcessingStep::PostProcessingStep(u32 _shader_id, const std::vector<u8> &_texture_map,
		const std::string &label) :
	shader_id(_shader_id), m_label(label), texture_map(_texture_map)
{
	assert(texture_map.size() <= video::MATERIAL_MAX_TEXTURES);
	configureMaterial();
}

void PostProcessingStep::configureMaterial()
{
	material.UseMipMaps = false;
	material.ZBuffer = video::ECFN_LESSEQUAL;
	material.ZWriteEnable = video::EZW_ON;
	for (u32 k = 0; k < texture_map.size(); ++k) {
		material.TextureLayers[k].AnisotropicFilter = 0;
		material.TextureLayers[k].MinFilter = video::ETMINF_NEAREST_MIPMAP_NEAREST;
		material.TextureLayers[k].MagFilter = video::ETMAGF_NEAREST;
		material.TextureLayers[k].TextureWrapU = video::ETC_CLAMP_TO_EDGE;
		material.TextureLayers[k].TextureWrapV = video::ETC_CLAMP_TO_EDGE;
	}
}

void PostProcessingStep::setRenderSource(RenderSource *_source)
{
	source = _source;
}

void PostProcessingStep::setRenderTarget(RenderTarget *_target)
{
	target = _target;
}

void PostProcessingStep::reset(PipelineContext &context)
{
}

void PostProcessingStep::run(PipelineContext &context)
{
	// Разбор шага на части, для замеров. См. AXIS_RENDER_PROBE в ClientMap.
	static const bool probe = getenv("AXIS_RENDER_PROBE") != nullptr;

	const u64 t0 = probe ? porting::getTimeNs() : 0;
	if (target)
		target->activate(context);
	const u64 t1 = probe ? porting::getTimeNs() : 0;

	// attach the shader
	material.MaterialType = context.client->getShaderSource()->getShaderInfo(shader_id).material;

	auto driver = context.device->getVideoDriver();

	for (u32 i = 0; i < texture_map.size(); i++)
		material.TextureLayers[i].Texture = source->getTexture(texture_map[i]);

	static const video::SColor color = video::SColor(0, 0, 0, 255);
	static const video::S3DVertex vertices[4] = {
			video::S3DVertex(1.0, -1.0, 0.0, 0.0, 0.0, -1.0,
					color, 1.0, 0.0),
			video::S3DVertex(-1.0, -1.0, 0.0, 0.0, 0.0, -1.0,
					color, 0.0, 0.0),
			video::S3DVertex(-1.0, 1.0, 0.0, 0.0, 0.0, -1.0,
					color, 0.0, 1.0),
			video::S3DVertex(1.0, 1.0, 0.0, 0.0, 0.0, -1.0,
					color, 1.0, 1.0),
	};
	static const u16 indices[6] = {0, 1, 2, 2, 3, 0};
	const u64 t2 = probe ? porting::getTimeNs() : 0;
	driver->setMaterial(material);
	const u64 t3 = probe ? porting::getTimeNs() : 0;
	driver->drawVertexPrimitiveList(&vertices, 4, &indices, 2);

	if (probe) {
		const u64 t4 = porting::getTimeNs();
		g_profiler->avg("Probe: post target [us]", (t1 - t0) / 1000.0f);
		g_profiler->avg("Probe: post textures [us]", (t2 - t1) / 1000.0f);
		g_profiler->avg("Probe: post material [us]", (t3 - t2) / 1000.0f);
		g_profiler->avg("Probe: post quad [us]", (t4 - t3) / 1000.0f);
	}
}

void PostProcessingStep::setBilinearFilter(u8 index, bool value)
{
	assert(index < video::MATERIAL_MAX_TEXTURES);
	material.TextureLayers[index].MinFilter = value ? video::ETMINF_LINEAR_MIPMAP_NEAREST : video::ETMINF_NEAREST_MIPMAP_NEAREST;
	material.TextureLayers[index].MagFilter = value ? video::ETMAGF_LINEAR : video::ETMAGF_NEAREST;
}

RenderStep *addPostProcessing(RenderPipeline *pipeline, RenderStep *previousStep, v2f scale, Client *client,
		ScreenCaptionStep *caption)
{
	auto buffer = pipeline->createOwned<TextureBuffer>();
	auto driver = client->getSceneManager()->getVideoDriver();

	// configure texture formats
	video::ECOLOR_FORMAT color_format = selectColorFormat(driver);
	video::ECOLOR_FORMAT depth_format = selectDepthFormat(driver);

	verbosestream << "addPostProcessing(): color = "
		<< video::ColorFormatName(color_format) << ", depth = "
		<< video::ColorFormatName(depth_format) << std::endl;

	// init post-processing buffer
	static const u8 TEXTURE_COLOR = 0;
	static const u8 TEXTURE_DEPTH = 1;
	static const u8 TEXTURE_BLOOM = 2;
	static const u8 TEXTURE_EXPOSURE_1 = 3;
	static const u8 TEXTURE_EXPOSURE_2 = 4;
	static const u8 TEXTURE_FXAA = 5;
	static const u8 TEXTURE_VOLUME = 6;

	static const u8 TEXTURE_MSAA_COLOR = 7;
	static const u8 TEXTURE_MSAA_DEPTH = 8;

	static const u8 TEXTURE_SCALE_DOWN = 10;
	static const u8 TEXTURE_SCALE_UP = 20;

	// because bloom_format is floating point
	const bool bloom_available = driver->queryFeature(video::EVDF_RENDER_TO_FLOAT_TEXTURE);
	const bool enable_bloom = g_settings->getBool("enable_bloom") && bloom_available;
	const bool enable_volumetric_light = g_settings->getBool("enable_volumetric_lighting") && enable_bloom;
	const bool enable_auto_exposure = g_settings->getBool("enable_auto_exposure") && bloom_available;
	if (g_settings->getBool("enable_bloom") && !bloom_available) {
		warningstream << "Ignoring configured bloom since it's not supported by "
			"the current video driver." << std::endl;
	}
	if (g_settings->getBool("enable_auto_exposure") && !bloom_available) {
		warningstream << "Ignoring configured auto exposure since it's not supported by "
			"the current video driver." << std::endl;
	}

	verbosestream << "addPostProcessing(): bloom = "
		<< enable_bloom << (enable_volumetric_light ? " + volumetric" : "")
		<< ", exposure = " << enable_auto_exposure << std::endl;

	const std::string antialiasing = g_settings->get("antialiasing");
	const u16 antialiasing_scale = MYMAX(2, g_settings->getU16("fsaa"));

	// This code only deals with MSAA in combination with post-processing. MSAA without
	// post-processing works via a flag at OpenGL context creation instead.
	// To make MSAA work with post-processing, we need multisample texture support,
	// which has higher OpenGL (ES) version requirements.
	// Note: This is not about renderbuffer objects, but about textures,
	// since that's what we use and what Irrlicht allows us to use.

	const bool msaa_available = driver->queryFeature(video::EVDF_TEXTURE_MULTISAMPLE);
	const bool enable_msaa = antialiasing == "fsaa" && msaa_available;
	if (antialiasing == "fsaa" && !msaa_available) {
		warningstream << "Ignoring configured FSAA since it's not supported in "
			"combination with post-processing by the current video driver." << std::endl;
	}

	const bool enable_ssaa = antialiasing == "ssaa";
	const bool enable_fxaa = g_settings->getBool("fxaa");

	verbosestream << "addPostProcessing(): AA = "
		<< (enable_msaa ? "msaa" : enable_ssaa ? "ssaa" : "none")
		<< " " << antialiasing_scale << "x" << (enable_fxaa ? " + fxaa" : "") << std::endl;

	// Super-sampling is simply rendering into a larger texture.
	// Downscaling is done by the final step when rendering to the screen.
	if (enable_ssaa) {
		scale *= antialiasing_scale;
	}

	if (enable_msaa) {
		buffer->setTexture(TEXTURE_MSAA_COLOR, scale, "3d_render_msaa", color_format, false, antialiasing_scale);
		buffer->setTexture(TEXTURE_MSAA_DEPTH, scale, "3d_depthmap_msaa", depth_format, false, antialiasing_scale);
	}

	buffer->setTexture(TEXTURE_COLOR, scale, "3d_render", color_format);
	buffer->setTexture(TEXTURE_EXPOSURE_1, core::dimension2du(1,1), "exposure_1", color_format, /*clear:*/ true);
	buffer->setTexture(TEXTURE_EXPOSURE_2, core::dimension2du(1,1), "exposure_2", color_format, /*clear:*/ true);
	buffer->setTexture(TEXTURE_DEPTH, scale, "3d_depthmap", depth_format);

	// attach buffer to the previous step
	if (enable_msaa) {
		TextureBufferOutput *msaa = pipeline->createOwned<TextureBufferOutput>(buffer, std::vector<u8> { TEXTURE_MSAA_COLOR }, TEXTURE_MSAA_DEPTH);
		previousStep->setRenderTarget(msaa);
		TextureBufferOutput *normal = pipeline->createOwned<TextureBufferOutput>(buffer, std::vector<u8> { TEXTURE_COLOR }, TEXTURE_DEPTH);
		pipeline->addStep<ResolveMSAAStep>(msaa, normal);
	} else {
		previousStep->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, std::vector<u8> { TEXTURE_COLOR }, TEXTURE_DEPTH));
	}

	// Подпись на закрытых глазах: своя текстура, в экранном размере, без
	// сглаживания — мягкость ей даёт уже сведение.
	static const u8 TEXTURE_CAPTION = 30;
	buffer->setTexture(TEXTURE_CAPTION, v2f(1.0f, 1.0f), "caption", video::ECF_A8R8G8B8);
	caption->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_CAPTION));

	// shared variables
	u32 shader_id;

	/*
	 * Объёмные облака подмешиваются в кадр до всего остального, чтобы дальше
	 * идти наравне с миром: попадать в свечение, в тонирование, в сглаживание.
	 * Читать и писать одну текстуру нельзя, поэтому результат ложится в
	 * отдельную, и следующие шаги берут за исходную уже её.
	 */
	u8 scene_color = TEXTURE_COLOR;

	if (g_settings->getBool("enable_volumetric_clouds")) {
		static const u8 TEXTURE_CLOUDS = 9;

		buffer->setTexture(TEXTURE_CLOUDS, scale, "clouds", color_format);

		shader_id = client->getShaderSource()->getShaderRaw("volumetric_clouds");
		auto clouds = pipeline->addStep<PostProcessingStep>(shader_id,
				std::vector<u8> { TEXTURE_COLOR, TEXTURE_DEPTH }, "volumetric_clouds");
		clouds->setRenderSource(buffer);
		clouds->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(
				buffer, TEXTURE_CLOUDS));

		scene_color = TEXTURE_CLOUDS;
	}

	// Number of mipmap levels of the bloom downsampling texture
	// (this affects the bloom strength, so don't blindly change it)
	const u8 MIPMAP_LEVELS = 4;

	// color_format can be a normalized integer format, but bloom requires
	// values outside of [0,1] so this needs to be a different one.
	const auto bloom_format = video::ECF_A16B16G16R16F;

	// post-processing stage

	u8 source = scene_color;

	// common downsampling step for bloom or autoexposure
	if (enable_bloom || enable_auto_exposure) {

		v2f downscale = scale * 0.5f;
		for (u8 i = 0; i < MIPMAP_LEVELS; i++) {
			buffer->setTexture(TEXTURE_SCALE_DOWN + i, downscale, std::string("downsample") + std::to_string(i), bloom_format);
			if (enable_bloom)
				buffer->setTexture(TEXTURE_SCALE_UP + i, downscale, std::string("upsample") + std::to_string(i), bloom_format);
			downscale *= 0.5f;
		}

		if (enable_bloom) {
			/*
			 * Яркие места выделяются в половинном разрешении.
			 *
			 * Эта картинка нигде не показывается: следующим шагом её всё равно
			 * уменьшают вдвое и размывают. В полном разрешении она стоила
			 * полноэкранной записи шестнадцати байт на точку, а давала ровно
			 * то же самое свечение. Выборка идёт с усреднением, так что четыре
			 * точки складываются в одну честно, а не через одну.
			 */
			const bool bloom_half = [] {
				const char *v = getenv("AXIS_RENDER_BLOOM_HALF");
				return !(v && v[0] == '0');
			}();
			const v2f bloom_scale = bloom_half ? scale * 0.5f : scale;
			buffer->setTexture(TEXTURE_BLOOM, bloom_scale, "bloom", bloom_format);

			// get bright spots
			u32 shader_id = client->getShaderSource()->getShaderRaw("extract_bloom");
			auto *extract_bloom = pipeline->addStep<PostProcessingStep>(shader_id, std::vector<u8> { source, TEXTURE_EXPOSURE_1 }, "extract_bloom");
			if (bloom_half)
				extract_bloom->setBilinearFilter(0, true);
			extract_bloom->setRenderSource(buffer);
			extract_bloom->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_BLOOM));
			source = TEXTURE_BLOOM;
		}

		if (enable_volumetric_light) {
			/*
			 * Половина разрешения по каждой оси, то есть четверть пикселей.
			 *
			 * Этот проход берёт три десятка выборок глубины на пиксель, и
			 * выборки идут по расходящимся к солнцу лучам, так что кэш текстур
			 * почти не помогает - в полном разрешении он стоит больше, чем вся
			 * остальная постобработка вместе взятая. Считать его точно незачем:
			 * его результат идёт не на экран, а в цепочку размытия ниже, и
			 * оттуда возвращается уже растёкшимся.
			 */
			buffer->setTexture(TEXTURE_VOLUME, scale * 0.5f, "volume", bloom_format);

			shader_id = client->getShaderSource()->getShaderRaw("volumetric_light");
			auto volume = pipeline->addStep<PostProcessingStep>(shader_id, std::vector<u8> { source, TEXTURE_DEPTH }, "volumetric_light");
			// Картинка читается вдвое реже, чем в неё писали, поэтому берём
			// среднее по четырём точкам, а не одну из них. Глубину (слой 1)
			// сглаживать нельзя: шейдер сравнивает её с единицей, и на краях
			// геометрии усреднение выдумало бы промежуточные значения.
			volume->setBilinearFilter(0, true);
			volume->setRenderSource(buffer);
			volume->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_VOLUME));
			source = TEXTURE_VOLUME;
		}

		// downsample
		shader_id = client->getShaderSource()->getShaderRaw("bloom_downsample");
		for (u8 i = 0; i < MIPMAP_LEVELS; i++) {
			auto step = pipeline->addStep<PostProcessingStep>(shader_id, std::vector<u8> { source }, "downsample" + std::to_string(i));
			step->setRenderSource(buffer);
			step->setBilinearFilter(0, true);
			step->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_SCALE_DOWN + i));
			source = TEXTURE_SCALE_DOWN + i;
		}
	}

	// Bloom pt 2
	if (enable_bloom) {
		// upsample
		shader_id = client->getShaderSource()->getShaderRaw("bloom_upsample");
		for (u8 i = MIPMAP_LEVELS - 1; i > 0; i--) {
			auto step = pipeline->addStep<PostProcessingStep>(shader_id, std::vector<u8> { u8(TEXTURE_SCALE_DOWN + i - 1), source }, "upsample" + std::to_string(i - 1));
			step->setRenderSource(buffer);
			step->setBilinearFilter(0, true);
			step->setBilinearFilter(1, true);
			step->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, u8(TEXTURE_SCALE_UP + i - 1)));
			source = TEXTURE_SCALE_UP + i - 1;
		}
	}

	// Dynamic Exposure pt2
	if (enable_auto_exposure) {
		shader_id = client->getShaderSource()->getShaderRaw("update_exposure");
		auto update_exposure = pipeline->addStep<PostProcessingStep>(shader_id, std::vector<u8> { TEXTURE_EXPOSURE_1, u8(TEXTURE_SCALE_DOWN + MIPMAP_LEVELS - 1) }, "update_exposure");
		update_exposure->setBilinearFilter(1, true);
		update_exposure->setRenderSource(buffer);
		update_exposure->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_EXPOSURE_2));
	}

	// FXAA
	u8 final_stage_source = scene_color;

	if (enable_fxaa) {
		final_stage_source = TEXTURE_FXAA;

		buffer->setTexture(TEXTURE_FXAA, scale, "fxaa", color_format);
		shader_id = client->getShaderSource()->getShaderRaw("fxaa");
		PostProcessingStep *effect = pipeline->createOwned<PostProcessingStep>(shader_id, std::vector<u8> { scene_color }, "fxaa");
		pipeline->addStep(effect);
		effect->setBilinearFilter(0, true);
		effect->setRenderSource(buffer);
		effect->setRenderTarget(pipeline->createOwned<TextureBufferOutput>(buffer, TEXTURE_FXAA));
	}

	// final merge
	shader_id = client->getShaderSource()->getShaderRaw("second_stage");
	PostProcessingStep *effect = pipeline->createOwned<PostProcessingStep>(shader_id, std::vector<u8> { final_stage_source, TEXTURE_SCALE_UP, TEXTURE_EXPOSURE_2, TEXTURE_CAPTION }, "second_stage");
	pipeline->addStep(effect);
	if (enable_ssaa)
		effect->setBilinearFilter(0, true);
	effect->setBilinearFilter(1, true);
	effect->setRenderSource(buffer);

	if (enable_auto_exposure) {
		pipeline->addStep<SwapTexturesStep>(buffer, TEXTURE_EXPOSURE_1, TEXTURE_EXPOSURE_2);
	}

	return effect;
}

void ResolveMSAAStep::run(PipelineContext &context)
{
	context.device->getVideoDriver()->blitRenderTarget(msaa_fbo->getIrrRenderTarget(context),
			target_fbo->getIrrRenderTarget(context));
}
